#version 450

// Reads the G-buffer and works out the lighting for every opaque pixel on the
// screen at once.
//
// This is the pass M23 exists to make possible. It buys nothing in frame time
// today - there is one light in the world - but depth, normals, roughness and
// ambient occlusion are now textures, which is what shadows, the sky, water and
// any later post-processing all need.

#include "octahedral.glsl"
#include "lighting.glsl"

layout(set = 0, binding = 0) uniform sampler2D gbufferAlbedo;
layout(set = 0, binding = 1) uniform sampler2D gbufferLight;
layout(set = 0, binding = 2) uniform sampler2D gbufferMaterial;
layout(set = 0, binding = 3) uniform sampler2D sceneDepth;

#include "frame.glsl"

// The sun's own view of the world, one layer per cascade. `Shadow` on the type
// is what makes a lookup a comparison rather than a read.
layout(set = 0, binding = 5) uniform sampler2DArrayShadow sunShadowMap;

#include "shadow.glsl"
#include "clouds.glsl"
#include "sky.glsl"
#include "water.glsl"

layout(location = 0) in vec2 fragUv;

// The push block, from the file that owns it.
//
// **This was a second hand-written copy until 2026-08-19, and it had already
// drifted** - it documented `imageParams.w` as the debug view while neither
// `post_common.glsl` nor `PostPushConstants` mentioned `w` at all, so anyone
// editing the shared file had no reason to look here. There was never much
// reason for the copy: this pass already includes seven other `.glsl` files,
// and nothing it includes pulls in `post_common.glsl`, so there is no double
// declaration to fear.
//
// Only `y` is read in this pass: the contact-occlusion radius in pixels. `x` is
// the anti-alias amount in `tonemap.frag` and one texel of the source in
// `bloom_down.frag` - one field, three meanings, so do not carry a value over
// from another pass. `PostPushConstants` in PushConstants.hpp is the owner and
// holds the full four-pass table.
#include "post_common.glsl"

layout(location = 0) out vec4 outColor;

// `kEmissionScale` is in lighting.glsl, which this file includes - one
// definition shared with triangle.frag rather than two that agree until they do
// not.

// A point light riding on the player. Off by default and deliberately so: its
// direction *is* the view direction, so the half-vector is the view vector and
// every surface faced head-on sits at the peak of its own highlight. That reads
// as a bright spot glued to the middle of the screen, following you around -
// which is exactly what it was reported as. `frame.sunDirection.w` carries how
// much of it the player has asked for.
const float kHandheldReach = 12.0;

/// Contact shadow, from the depth buffer this pass is already reading.
///
/// **No pass, no render target and no blur of its own.** Eight taps in a small
/// screen-space disc, each rotated by an interleaved-gradient offset so the
/// sampling pattern differs per pixel rather than repeating - which is what
/// turns visible banding into grain the eye reads as texture. A separate SSAO
/// pass with a bilateral blur would be better and costs two more images, a
/// second descriptor set and two layout transitions; this is a tenth of the
/// work for most of the effect.
///
/// The baked ambient occlusion the mesher writes is still the main one. This
/// only adds what a per-block value cannot know: where a creature stands, where
/// a dropped item rests, where two surfaces meet at a distance no vertex knows
/// about.
float contactOcclusion(vec2 uv, vec3 worldPosition, vec3 normal, float radius) {
    if (radius <= 0.0) {
        return 1.0;
    }

    vec2 size = vec2(textureSize(sceneDepth, 0));
    float rotation = fract(52.9829189 * fract(0.06711056 * gl_FragCoord.x + 0.00583715 * gl_FragCoord.y));
    float occluded = 0.0;

    for (int i = 0; i < 8; ++i) {
        float angle = (float(i) + rotation) * 0.7853981;
        float reach = radius * (0.35 + 0.65 * float(i + 1) / 8.0);
        vec2 offset = vec2(cos(angle), sin(angle)) * reach / size;
        vec2 tap = clamp(uv + offset, vec2(0.001), vec2(0.999));

        float depth = texture(sceneDepth, tap).r;
        if (depth >= 1.0) {
            continue;
        }
        vec4 world = frame.inverseViewProjection * vec4(tap * 2.0 - 1.0, depth, 1.0);
        vec3 point = world.xyz / world.w;

        vec3 toPoint = point - worldPosition;
        float distance = length(toPoint);
        // Anything far enough away is a different surface entirely, not a
        // corner - and range-checking it is what stops a foreground object
        // drawing a dark halo on the sky behind it.
        if (distance < 0.05 || distance > 2.5) {
            continue;
        }
        occluded += max(dot(normal, toPoint / distance), 0.0) / (1.0 + distance);
    }

    return clamp(1.0 - occluded * 0.30, 0.0, 1.0);
}

void main() {
    float depth = texture(sceneDepth, fragUv).r;

    // Back out of clip space through the inverse of the matrix everything was
    // drawn with, so the position cannot disagree with the picture.
    vec4 clip = vec4(fragUv * 2.0 - 1.0, depth, 1.0);
    vec4 world = frame.inverseViewProjection * clip;
    vec3 worldPosition = world.xyz / world.w;

    // Nothing was drawn here, so this pixel is sky. **Written rather than left
    // to the clear colour**, which is what a flat sky was until M25: one colour
    // over the whole hemisphere has no horizon, no zenith and nowhere for a
    // sunset to happen.
    if (depth >= 1.0) {
        vec4 far = frame.inverseViewProjection * vec4(fragUv * 2.0 - 1.0, 1.0, 1.0);
        vec3 direction = normalize(far.xyz / far.w - frame.eye.xyz);
        outColor = vec4(skyRadiance(direction), kSkyDistance);
        return;
    }

    vec4 albedoShade = texture(gbufferAlbedo, fragUv);
    vec4 light = texture(gbufferLight, fragUv);
    vec4 material = texture(gbufferMaterial, fragUv);

    Surface surface;
    surface.albedo = albedoShade.rgb;
    surface.faceShade = albedoShade.a;
    surface.skyLight = light.r;
    surface.blockLight = light.g;
    surface.occlusion = light.b;
    surface.emissive = light.a;
    surface.roughness = material.r;
    surface.metallic = material.g;
    surface.normal = octDecodeNormal(material.ba);

    // **Rain darkens what it lands on and makes it shine.** Water fills the pores
    // of a surface, so less light escapes back out of it - which is why wet
    // stone reads darker - and it leaves a smooth film on top, which is why it
    // also catches a highlight. Snow does the opposite and is left alone.
    // Gated on sky light, so the inside of a house stays dry.
    float wet = clamp(frame.weather.x, 0.0, 1.0) * (1.0 - frame.weather.y) *
                smoothstep(0.55, 0.95, surface.skyLight);
    if (wet > 0.0) {
        surface.albedo *= mix(1.0, 0.68, wet);
        surface.roughness = mix(surface.roughness, surface.roughness * 0.35, wet);
    }

    vec3 viewDirection = normalize(frame.eye.xyz - worldPosition);
    float eyeDistance = length(frame.eye.xyz - worldPosition);

    // Fades out with distance, where a screen-space radius covers whole blocks
    // and the result is a smear rather than a corner.
    surface.occlusion *= contactOcclusion(fragUv, worldPosition, surface.normal,
                                          post.filterParams.y *
                                              (1.0 - smoothstep(24.0, 64.0, eyeDistance)));

    float sunShadow =
        sunShadowFactor(sunShadowMap, worldPosition, surface.normal, frame.sunDirection.xyz, eyeDistance);

    // A cloud darkens the ground under it. One coverage sample along the light,
    // and it is by some distance the cheapest part of the whole cloud layer -
    // and most of what sells it, because a moving shadow on open ground is
    // visible from anywhere while the deck itself needs you to look up.
    if (frame.cloud.w > 0.5) {
        sunShadow *= cloudSunShadow(worldPosition, frame.sunDirection.xyz, frame.cloud.x, frame.cloud.y,
                                    frame.cloud.z);
    }

    float level = diffuseLevel(surface, frame.sunDirection.xyz, frame.lighting.xyz, sunShadow, frame.water.w);
    vec3 rgb = surface.albedo * level;

    // Specular from the sun. Modulated by sky light so a cave does not catch a
    // highlight from a sun it cannot see, and capped so a highlight can never
    // overload bloom into a white blob.
    vec3 sunSpecular = specularLobe(surface, frame.sunDirection.xyz, viewDirection);
    float sunVisibility = surface.skyLight * frame.lighting.y * surface.occlusion * sunShadow;
    rgb += min(sunSpecular * sunVisibility, vec3(4.0));

    float handheldIntensity = frame.sunDirection.w;
    vec3 toEye = frame.eye.xyz - worldPosition;
    float distanceSquared = dot(toEye, toEye);
    if (handheldIntensity > 0.0 && distanceSquared < kHandheldReach * kHandheldReach) {
        vec3 handheldDirection = toEye * inversesqrt(max(distanceSquared, 1e-4));
        float falloff = 1.0 / (distanceSquared + 1.0);
        // Capped for the same reason the sun's is: glass and ice are smooth
        // enough to concentrate a whole lobe into one pixel, and an uncapped
        // highlight overloads bloom into a white blob.
        vec3 handheld = min(specularLobe(surface, handheldDirection, viewDirection), vec3(4.0));
        handheld += surface.albedo * max(dot(surface.normal, handheldDirection), 0.0) * 0.35;
        rgb += handheld * falloff * handheldIntensity * surface.occlusion;
    }

    if (surface.emissive > 0.0) {
        rgb += surface.albedo * surface.emissive * kEmissionScale;
    }

    // **The caustic net, on everything, while the eye is under water.** From
    // below there is no water surface between the eye and the bed to hang it
    // off, so the fact that the whole view is submerged is the test. Anything
    // above the surface is behind several metres of underwater fog by then.
    if (frame.eye.w <= 0.0 && frame.fog.w > 0.0 && frame.waterDetail.y > 0.0) {
        float texels = frame.waterDetail.w;
        vec2 lit = (floor(worldPosition.xz * texels) + 0.5) / texels;
        float caustic = waterCaustics(lit, frame.water.x);
        rgb *= 1.0 + caustic * frame.waterDetail.y * 1.4 * surface.skyLight * frame.lighting.y;
    }

    // **Light shafts, from the surface down.** The same rippling surface that
    // focuses light into the caustic net on the seabed is what lets some of it
    // through and blocks the rest, so the shafts are that same field sampled
    // where a vertical ray from each step would leave the water - which is why
    // they line up with the pattern on the floor instead of being a second,
    // unrelated effect.
    if (frame.volumetric.y > 0.0 && frame.eye.w <= 0.0) {
        const int kShaftSteps = 12;
        float surfaceY = frame.volumetric.x;
        vec3 toPixel = worldPosition - frame.eye.xyz;
        float span = min(length(toPixel), frame.fog.w);
        vec3 direction = toPixel / max(length(toPixel), 1e-4);
        // The same interleaved-gradient offset the contact shadows use, so the
        // twelve steps read as grain rather than as twelve bands.
        float jitter = fract(52.9829189 * fract(0.06711056 * gl_FragCoord.x +
                                                0.00583715 * gl_FragCoord.y));
        float shaft = 0.0;
        for (int i = 0; i < kShaftSteps; ++i) {
            float t = (float(i) + jitter) / float(kShaftSteps);
            vec3 point = frame.eye.xyz + direction * (span * t);
            float below = surfaceY - point.y;
            if (below <= 0.0) {
                continue;
            }
            float texels = frame.waterDetail.w;
            vec2 lit = (floor(point.xz * texels) + 0.5) / texels;
            // Dimmer the deeper the sample is, which is what gives a shaft its
            // taper rather than a uniform bar of light.
            shaft += waterCaustics(lit, frame.water.x) * exp(-below * 0.09);
        }
        shaft *= span / float(kShaftSteps);
        rgb += frame.fog.rgb * shaft * frame.volumetric.y * frame.lighting.y * 0.05;
    }

    // Radial from the eye, not depth along the view axis: view depth is only
    // about 0.7 of the true distance at the corners of the screen, so the
    // corners stayed clear and chunks were seen loading in exactly the place
    // peripheral vision picks up movement.
    float fogDistance = frame.fog.w;
    if (fogDistance > 0.0) {
        float startFraction = frame.eye.w;
        float start = fogDistance * startFraction;
        float distance = length(worldPosition - frame.eye.xyz);
        float fogged = clamp((distance - start) / max(fogDistance - start, 0.001), 0.0, 1.0);
        rgb = mix(rgb, frame.fog.rgb, fogged);
    }
    // Twenty lines that are the whole verification mechanism for this pass: if
    // the picture is wrong, one of these says which input is wrong.
    int view = int(post.imageParams.w + 0.5);
    if (view == 1) {
        rgb = surface.albedo;
    } else if (view == 2) {
        rgb = surface.normal * 0.5 + 0.5;
    } else if (view == 3) {
        rgb = vec3(surface.roughness);
    } else if (view == 4) {
        rgb = vec3(surface.metallic);
    } else if (view == 5) {
        rgb = vec3(surface.occlusion);
    } else if (view == 6) {
        rgb = vec3(surface.emissive);
    } else if (view == 7) {
        rgb = vec3(surface.skyLight, surface.blockLight, 0.0);
    } else if (view == 8) {
        rgb = vec3(fract(length(worldPosition - frame.eye.xyz) / 16.0));
    } else if (view == 9) {
        rgb = vec3(sunShadow);
    }

    outColor = vec4(rgb, eyeDistance);
}
