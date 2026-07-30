#version 450

// Face shading, interpolated across the triangle.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 2) flat in float fragLayer;

layout(set = 0, binding = 0) uniform sampler2DArray blockTextures;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texel = texture(blockTextures, vec3(fragUv, fragLayer));
    outColor = vec4(texel.rgb * fragColor, texel.a);
}
