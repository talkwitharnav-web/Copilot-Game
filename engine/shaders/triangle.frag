#version 450

// Face shading, interpolated across the triangle.
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) flat in float fragLayer;

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
    outColor = vec4(texel.rgb * fragColor.rgb, texel.a * fragColor.a);
}
