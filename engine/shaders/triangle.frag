#version 450

// Face shading, interpolated across the triangle.
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) flat in float fragLayer;
layout(location = 3) in vec3 fragWorldPosition;

layout(push_constant) uniform Push {
    mat4 modelViewProjection;
    vec4 sunDirection;
    vec4 lighting;
} push;

layout(set = 0, binding = 0) uniform sampler2DArray blockTextures;
// Separate from the block array because HUD sheets are a different size, and a
// texture array requires every layer to match.
layout(set = 0, binding = 1) uniform sampler2DArray hudTexture;
layout(set = 0, binding = 2) uniform sampler2DArray fontTexture;
// Creature skins: one unwrapped net per species, so a box face can carry the
// right pixels rather than the whole texture stretched over it.
layout(set = 0, binding = 3) uniform sampler2DArray skinTexture;

layout(location = 0) out vec4 outColor;

void main() {
    // Negative layers are the agreed signals for the HUD sheets.
    vec4 texel;
    if (fragLayer < -2.5) {
        texel = texture(skinTexture, vec3(fragUv, 0.0));
    } else if (fragLayer < -1.5) {
        texel = texture(fontTexture, vec3(fragUv, 0.0));
    } else if (fragLayer < 0.0) {
        texel = texture(hudTexture, vec3(fragUv, 0.0));
    } else {
        texel = texture(blockTextures, vec3(fragUv, fragLayer));
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

    if (push.lighting.z > 0.5) {
        // Cutout: a hole in the texture is thrown away outright rather than
        // blended. What survives still writes depth, so this needs no sorting -
        // unlike the translucent pass.
        if (texel.a < 0.5) {
            discard;
        }

        // A surviving cutout pixel is fully present, never partly. Mip levels
        // average alpha, so distant foliage arrives with values like 0.6 which
        // pass the test and then blend with the sky behind - which turned every
        // distant tree pale grey. World transparency comes from the vertex
        // instead, which is where water keeps its.
        alpha = fragColor.a;

        // Recovered from how world position changes across the triangle, rather
        // than carried per vertex. Every face here is flat, so this is exact,
        // and it keeps a normal out of the vertex format entirely.
        vec3 normal = normalize(cross(dFdx(fragWorldPosition), dFdy(fragWorldPosition)));
        float lambert = max(dot(normal, push.sunDirection.xyz), 0.0);
        float daylight = push.lighting.x + push.lighting.y * lambert;

        // Red is sky light, green is block light, blue is face shade times
        // ambient occlusion. Only sky light answers to the sun - modulating
        // block light by it would put torches out at dusk and leave a lit cave
        // dark, since underground no face points at the sun. Shading multiplies
        // the result including the floor, or unlit caves come out perfectly
        // flat.
        float lit = max(fragColor.g, fragColor.r * daylight);
        rgb = texel.rgb * fragColor.b * (push.lighting.w + (1.0 - push.lighting.w) * lit);
    }

    // A lit fuse strobes white. Layer -5 rather than -4, and tested first,
    // because the hurt test below would otherwise swallow it.
    //
    // Mixed rather than multiplied, unlike the hurt tint: a fuse is meant to
    // wash the animal out toward white, where being struck is meant to leave
    // the skin readable underneath.
    if (fragLayer < -4.5) {
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

    outColor = vec4(rgb, alpha);
}
