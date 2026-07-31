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

layout(location = 0) out vec4 outColor;

void main() {
    // Negative layers are the agreed signals for the HUD sheets.
    vec4 texel;
    if (fragLayer < -1.5) {
        texel = texture(fontTexture, vec3(fragUv, 0.0));
    } else if (fragLayer < 0.0) {
        texel = texture(hudTexture, vec3(fragUv, 0.0));
    } else {
        texel = texture(blockTextures, vec3(fragUv, fragLayer));
    }

    vec3 rgb = texel.rgb * fragColor.rgb;

    if (push.lighting.z > 0.5) {
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

    outColor = vec4(rgb, texel.a * fragColor.a);
}
