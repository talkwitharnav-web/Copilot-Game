#version 450

#include "lighting.glsl"

// Face shading, interpolated across the triangle.
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) flat in float fragLayer;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in float fragViewDepth;
layout(location = 5) flat in uint fragNormalCode;
layout(location = 6) in float fragOcclusion;

layout(push_constant) uniform Push {
    mat4 modelViewProjection;
    uvec4 flags;
} push;

// Everything in here is the same for every draw in the frame.
#include "frame.glsl"
#include "sky.glsl"
#include "water.glsl"

// One packed word per texture layer. R roughness, G metallic, B emissive,
// A flags - see game/src/world/Material.hpp, which owns what they mean.
layout(set = 0, binding = 5) readonly buffer Materials {
    uint rows[];
} materials;

// Must match the kDrawFlag* constants in PushConstants.hpp.
const uint kDrawFlagLit = 1u;
const uint kDrawFlagFogged = 2u;
const uint kDrawFlagEmissive = 4u;
const uint kDrawFlagSky = 8u;

// How far above ordinary diffuse an emissive surface sits. It has to clear 1.0
// by a good margin or the tone curve has nothing to turn into a white core with
// a coloured fringe, which is what a light actually looks like.
const float kEmissionScale = 4.0;

// Must match the kNormal* codes in Vertex.hpp.
const uint kNormalUnaligned = 7u;

/// The exact normal of an axis-aligned voxel face, or - for a rotated creature
/// limb or a plant blade at 45 degrees - the one the triangle's own geometry
/// implies. Turned toward the eye, because a cutout quad is drawn with both
/// windings and either side of it may be the one in front of you.
vec3 surfaceNormal() {
    vec3 axis;
    switch (fragNormalCode) {
    case 0u: axis = vec3( 1.0,  0.0,  0.0); break;
    case 1u: axis = vec3(-1.0,  0.0,  0.0); break;
    case 2u: axis = vec3( 0.0,  1.0,  0.0); break;
    case 3u: axis = vec3( 0.0, -1.0,  0.0); break;
    case 4u: axis = vec3( 0.0,  0.0,  1.0); break;
    case 5u: axis = vec3( 0.0,  0.0, -1.0); break;
    default:
        // Recovered from how world position changes across the triangle. Exact
        // for a flat face, and the only thing available with no axis to name.
        axis = normalize(cross(dFdx(fragWorldPosition), dFdy(fragWorldPosition)));
        break;
    }
    return dot(axis, frame.eye.xyz - fragWorldPosition) < 0.0 ? -axis : axis;
}

layout(set = 0, binding = 0) uniform sampler2DArray blockTextures;
// Separate from the block array because HUD sheets are a different size, and a
// texture array requires every layer to match.
layout(set = 0, binding = 1) uniform sampler2DArray hudTexture;
layout(set = 0, binding = 2) uniform sampler2DArray fontTexture;
// Creature skins: one unwrapped net per species, so a box face can carry the
// right pixels rather than the whole texture stretched over it.
layout(set = 0, binding = 3) uniform sampler2DArray skinTexture;
// The lit world as it stood before anything translucent was drawn into it, with
// each pixel's distance from the eye in its alpha. Water reflects out of this.
layout(set = 0, binding = 6) uniform sampler2D sceneCopy;

layout(location = 0) out vec4 outColor;

void main() {
    // World geometry. The sky, the block outline and everything in screen space
    // are drawn through this same shader without it, and stay flat.
    bool worldLit = (push.flags.x & kDrawFlagLit) != 0u;

    // Water is meshed with one fixed layer and redirected to whichever frame the
    // clock is on, so a moving surface costs no re-meshing at all.
    float layer = fragLayer;
    if (abs(layer - frame.animation.x) < 0.25) {
        layer = frame.animation.y;
    } else if (abs(layer - frame.animation.z) < 0.25) {
        layer = frame.animation.w;
    }

    // Negative layers are the agreed signals for the HUD sheets.
    vec4 texel;
    if (layer < -5.5) {
        // A lightning bolt has no artwork at all. White, and the emissive flag
        // below takes it past 1.0 so bloom gives it its own halo - which is what
        // the reference gets from four nested shells blended additively.
        texel = vec4(1.0);
    } else if (layer < -2.5) {
        texel = texture(skinTexture, vec3(fragUv, 0.0));
    } else if (layer < -1.5) {
        texel = texture(fontTexture, vec3(fragUv, 0.0));
    } else if (layer < 0.0) {
        texel = texture(hudTexture, vec3(fragUv, 0.0));
    } else {
        texel = texture(blockTextures, vec3(fragUv, layer));
    }

    vec3 rgb = texel.rgb * fragColor.rgb;
    float alpha = texel.a * fragColor.a;

    // A creature skin is cutout by nature: the fleece shell is punched through
    // so the face underneath shows, so a hole has to be thrown away rather than
    // blended. Both shells write depth, so this needs no sorting.
    if (fragLayer < -2.5) {
        if (texel.a < 0.5) {
            discard;
        }
        alpha = fragColor.a;
    }

    if (worldLit) {
        // Cutout: a hole in the texture is thrown away outright rather than
        // blended. What survives still writes depth, so this needs no sorting -
        // unlike the translucent pass.
        if (texel.a < 0.5) {
            discard;
        }

        // Exact for every voxel face, and free of the artefacts a quad
        // derivative shows along a silhouette.
        vec3 normal = surfaceNormal();

        // A water surface is not flat. Only the horizontal faces get the
        // ripple: a waterfall's side face has no "up" for a wave to travel
        // along, and tilting it with an XZ pattern would be nonsense.
        //
        // **Everything water reflects is worked out from a snapped position**,
        // not from the fragment's own. One block face is one texture, so
        // dividing a block by the texture's width gives exactly the grid the
        // rest of the world is drawn on - and a mirror finish made of those
        // squares belongs in this game where a smooth one does not.
        bool water = isWaterLayer(fragLayer);
        vec3 shadePosition = fragWorldPosition;
        if (water && abs(normal.y) > 0.5) {
            float texels = frame.waterDetail.w;
            shadePosition.xz = (floor(shadePosition.xz * texels) + 0.5) / texels;
            // Calmed with distance, because far off a single pixel covers many
            // ripples and the honest average of them is a flatter mirror. Left
            // at full strength it is the same trap the cloud light march fell
            // into: detail finer than a pixel does not read as detail.
            float far = smoothstep(20.0, 110.0, distance(frame.eye.xyz, shadePosition));
            vec3 ripple = waterNormal(shadePosition.xz, frame.water.x, frame.water.y * mix(1.0, 0.25, far));
            // Mirrored wholesale when seen from underneath, so the underside of
            // a lake ripples the same way its top does.
            normal = normalize(ripple * sign(normal.y));
        }

        // Red is sky light, green is block light, blue is the face shade, and
        // ambient occlusion arrives separately. Only sky light answers to the
        // sun - modulating block light by it would put torches out at dusk and
        // leave a lit cave dark, since underground no face points at the sun.
        // Shading multiplies the result including the floor, or unlit caves come
        // out perfectly flat.
        Surface surface;
        surface.albedo = texel.rgb;
        surface.normal = normal;
        surface.skyLight = fragColor.r;
        surface.blockLight = fragColor.g;
        surface.faceShade = fragColor.b;
        surface.occlusion = fragOcclusion;
        surface.emissive = 0.0;
        surface.roughness = 0.85;
        surface.metallic = 0.0;
        if (fragLayer >= 0.0) {
            uint materialRow = materials.rows[int(fragLayer)];
            surface.roughness = float(materialRow & 0xffu) / 255.0;
            surface.metallic = float((materialRow >> 8) & 0xffu) / 255.0;
        }

        rgb = texel.rgb * diffuseLevel(surface, frame.sunDirection.xyz, frame.lighting.xyz, 1.0, 0.0);

        // **This is what makes water look wet.** The forward pass carries every
        // surface the deferred one cannot, and water is the whole reason it
        // exists - so without a specular term the one genuinely reflective
        // surface in the game was the only one that never caught the sun.
        // Gated by sky light so a flooded cave does not glint, and capped so a
        // near-mirror cannot overload bloom into a white blob.
        vec3 viewDirection = normalize(frame.eye.xyz - shadePosition);
        vec3 specular = min(specularLobe(surface, frame.sunDirection.xyz, viewDirection), vec3(4.0)) *
                        fragColor.r * frame.lighting.y * fragOcclusion;

        if (water) {
            // **The sky, in the surface.** A specular highlight alone only ever
            // put the sun in the water; everything else the water should be
            // returning - the blue overhead, the orange at sunset, the cloud
            // deck's own colour - was simply missing, and a lake at a distance
            // read as flat blue paint. Reflecting the same gradient the sky
            // pass writes is what turns it into water.
            bool submerged = frame.eye.w <= 0.0 && frame.fog.w > 0.0;
            vec3 reflected = reflect(-viewDirection, normal);
            // Never below the horizon. A ripple can tilt the ray down past it,
            // and there is nothing under there to return but the fog colour
            // stamped at full strength, which reads as a hole in the surface.
            reflected.y = max(reflected.y, 0.02);
            reflected = normalize(reflected);

            // **The bank, the trees and the hills, not just the sky.** A sky
            // gradient alone is what a swimming pool returns; a lake returns
            // whatever is standing at the far side of it, and that is most of
            // what makes one read as water at all.
            vec3 sky = skyRadiance(reflected);
            vec3 mirrored = sky;
            if (!submerged) {
                float confidence = 0.0;
                vec3 world = marchReflection(sceneCopy, push.modelViewProjection, frame.eye.xyz,
                                             shadePosition, reflected, confidence);
                mirrored = mix(sky, world, confidence);
            }

            float openToSky = smoothstep(0.0, 0.6, fragColor.r);
            vec3 mirror = mix(rgb, mirrored, openToSky);

            // **What is behind the surface, bent - and how much water is in the
            // way.** Both come out of the same copy, whose alpha is a distance.
            //
            // The alternative to reading it is what the hardware blend does:
            // take whatever is already in the framebuffer, straight, at one
            // fixed weight. That cannot bend, cannot know how deep the water is,
            // and cannot tell a puddle from a lake - which is why a bed ten
            // metres down used to be exactly as legible as one an inch down.
            vec2 screenUv = gl_FragCoord.xy / vec2(textureSize(sceneCopy, 0));
            float surfaceDistance = distance(frame.eye.xyz, fragWorldPosition);
            vec4 straight = texture(sceneCopy, screenUv);

            // **Sky behind the surface means the blend has to do it after all.**
            // The copy is taken before the sun, the moon and the clouds are
            // drawn, so compositing against its sky would erase them - and a
            // waterfall against an open sky is exactly that case. It also stops
            // the sky's parked distance being read as infinitely deep water.
            bool composited = !submerged && straight.a < kSkyDistance * 0.5;
            float thickness = 0.0;
            vec3 background = vec3(0.0);

            if (composited) {
                thickness = max(straight.a - surfaceDistance, 0.0);

                // Bent by the surface's own slope, and by how much water the ray
                // is crossing - a film bends nothing. Divided by distance
                // because the offset is measured on the screen and a far surface
                // covers fewer pixels.
                vec2 bend = normal.xz * frame.waterDetail.z * min(thickness, 2.5) /
                            max(surfaceDistance, 1.0);
                vec2 bentUv = clamp(screenUv + bend, vec2(0.001), vec2(0.999));
                vec4 bent = texture(sceneCopy, bentUv);
                // Rejected when the nudge lands on something in **front** of the
                // water: that is the bank, not the bed, and bending it drags the
                // shoreline out over the pond.
                if (bent.a > surfaceDistance) {
                    thickness = max(bent.a - surfaceDistance, 0.0);
                    background = bent.rgb;
                } else {
                    background = straight.rgb;
                }

                // The caustic net, on the bed rather than on the surface -
                // which is where it belongs, and it is free here because the
                // distance to the bed has already been read. Reconstructed
                // along this fragment's own view ray; the bend is far too small
                // to matter at this scale.
                vec3 ray = normalize(fragWorldPosition - frame.eye.xyz);
                vec3 bed = frame.eye.xyz + ray * (surfaceDistance + thickness);
                float texels = frame.waterDetail.w;
                vec2 lit = (floor(bed.xz * texels) + 0.5) / texels;
                float caustic = waterCaustics(lit, frame.water.x);
                background *= 1.0 + caustic * frame.waterDetail.y * 1.6 * fragColor.r *
                                        frame.lighting.y * smoothstep(3.0, 0.4, thickness);
            }

            // How much of it is returned. Physical, not a look: 2% straight
            // down, nearly all of it a few degrees off the horizon.
            float reflectance = clamp(waterFresnel(normal, viewDirection) * frame.water.z, 0.0, 1.0);
            rgb = mix(rgb, mirror, reflectance) + specular;

            // **Foam where the water runs out.** A thin film over the bank is
            // broken up rather than smoothly transparent, and it is the one cue
            // that says where a pond ends - a shoreline with no line on it reads
            // as terrain that happens to be blue.
            if (composited) {
                float shallow = 1.0 - smoothstep(0.0, kFoamDepth, thickness);
                float texels = frame.waterDetail.w;
                vec2 churn = (floor(fragWorldPosition.xz * texels) + 0.5) / texels;
                float broken = waterCaustics(churn * 1.7, frame.water.x * 1.3);
                float foam = clamp(shallow * (0.35 + broken) * frame.waterDetail.x, 0.0, 1.0);
                rgb = mix(rgb, vec3(0.92, 0.96, 0.98) * max(fragColor.r * frame.lighting.y, 0.25),
                          foam);

                float opacity = clamp(max(mix(1.0, fragColor.a, exp(-thickness * kWaterAbsorption)),
                                          max(reflectance, foam)),
                                      0.0, 1.0);
                // Composited here rather than by the blend, so alpha is spent on
                // nothing and the pipeline still writes a sensible value for the
                // block outline drawn over it.
                rgb = mix(background, rgb, opacity);
                alpha = 1.0;
            } else {
                // A glint gets its own floor, or the one thing bright enough to
                // matter is the thing the blend dims most.
                float glint = dot(specular, vec3(0.2126, 0.7152, 0.0722));
                alpha = clamp(max(mix(fragColor.a, 1.0, reflectance), glint), 0.0, 1.0);
            }
        } else {
            rgb += specular;

            // A surviving cutout pixel is fully present, never partly. Mip
            // levels average alpha, so distant foliage arrives with values like
            // 0.6 which pass the test and then blend with the sky behind -
            // which turned every distant tree pale grey. World transparency
            // comes from the vertex instead.
            float grazing = 1.0 - abs(dot(normal, viewDirection));
            alpha = mix(fragColor.a, 1.0, (1.0 - surface.roughness) * grazing * grazing);
        }
    }

    // A lit fuse strobes white. Layer -5 rather than -4, and tested first,
    // because the hurt test below would otherwise swallow it.
    //
    // Mixed rather than multiplied, unlike the hurt tint: a fuse is meant to
    // wash the animal out toward white, where being struck is meant to leave
    // the skin readable underneath.
    if (fragLayer < -5.5) {
        // Already white, and mixing it toward white again would only dim the
        // emission the flag below is about to multiply.
    } else if (fragLayer < -4.5) {
        rgb = mix(rgb, vec3(1.0), 0.5);
    } else if (fragLayer < -3.5) {
        // A struck creature flashes red. This rides on the layer rather than the
        // vertex colour because all four channels are spoken for - and the first
        // attempt, which forced block light to 1.0, did not tint anything: it
        // made the animal a light source, invisible by day and glaring at night.
        //
        // Multiplied, not mixed: mixing toward a flat red replaces the skin and
        // throws away every pixel of detail in it. Scaling the channels shifts
        // the hue while the texture still reads through.
        //
        // No additive term: a floor on red pins it to maximum everywhere and
        // the result saturates to flat red. Green and blue stay well above zero
        // because that is what makes a red *bright* rather than dark - crushing
        // them gives blood, keeping them gives a vivid scarlet with the skin
        // still legible underneath.
        rgb = clamp(rgb * vec3(2.9, 0.92, 0.86), 0.0, 1.0);
    }

    // The scene is written to a floating-point image, so this may legitimately
    // go above 1. Anything that does is what bloom picks up and what the tone
    // curve turns into a white core with a coloured fringe.
    if ((push.flags.x & kDrawFlagEmissive) != 0u) {
        // **A sky body's quad is mostly transparent, and a blended fragment
        // still writes depth.** So the clear surround stamps a rectangle into
        // the depth buffer and everything drawn after it - the clouds above all
        // - is rejected inside that rectangle. That is the invisible border
        // around the moon, and throwing the empty part away is the whole fix.
        if (alpha < 0.02) {
            discard;
        }
        rgb *= frame.lighting.w;
    }

    // Emission is read with the layer the geometry was **meshed** with, not the
    // redirected one: which frame of an animation is showing does not change
    // what the surface is made of, and fire's thirty-two frames would otherwise
    // each need their own row.
    if (fragLayer >= 0.0) {
        uint row = materials.rows[int(fragLayer)];
        float emissive = float((row >> 16) & 0xffu) / 255.0;
        if (emissive > 0.0) {
            // Weighted by how bright the texel already is, so a torch's flame
            // glows and the stick holding it does not. Never all the way to
            // zero: a redstone torch's head is dark red and should still read as
            // lit.
            float brightness = dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722));
            float mask = mix(0.35, 1.0, smoothstep(0.2, 0.7, brightness));
            rgb += texel.rgb * emissive * mask * kEmissionScale;
        }
    }

    // Two fogs, one ramp, and they measure distance differently on purpose.
    //
    // **World geometry is fogged on its radial distance from the eye**, because
    // that is the only measure that puts the fog wall the same distance away in
    // every direction. Depth along the view axis — `gl_Position.w`, which this
    // used before — is roughly 0.7 of the true distance at the corners of the
    // screen, so the corners stayed clear and chunks were seen loading in
    // exactly the place peripheral vision picks up movement.
    //
    // The sky keeps view depth, because radial distance needs a world-space
    // fragment position and the sky is drawn in a camera-local frame. That only
    // matters underwater, which is the one case that fogs the sky at all: a
    // start fraction of 0 means "fade everything from the eye outward", the
    // reference's `fog_start` of 0, and it is why the sun does not hang in the
    // middle of a submerged view.
    float fogDistance = (push.flags.x & kDrawFlagFogged) != 0u ? frame.fog.w : 0.0;
    // **A sky body sits at the far plane by definition**, so the distance fog
    // that dissolves the edge of the world would dissolve it too - it would
    // fade out as it set, which is the opposite of looking unreachably far
    // away. It still goes when the eye is under water or lava, where the fog
    // starts at the eye rather than at a distance.
    if ((push.flags.x & kDrawFlagSky) != 0u && frame.eye.w > 0.0) {
        fogDistance = 0.0;
    }
    if (fogDistance > 0.0) {
        float startFraction = frame.eye.w;
        float distance;
        bool apply;
        if (startFraction > 0.0) {
            distance = length(fragWorldPosition - frame.eye.xyz);
            apply = worldLit;
        } else {
            distance = fragViewDepth;
            apply = true;
        }

        if (apply) {
            float start = fogDistance * startFraction;
            float fogged = clamp((distance - start) / max(fogDistance - start, 0.001), 0.0, 1.0);
            rgb = mix(rgb, frame.fog.rgb, fogged);
            alpha = mix(alpha, 1.0, fogged);
        }
    }

    outColor = vec4(rgb, alpha);
}
