#version 450

// Interpolated across the triangle's face by the GPU: each pixel receives a
// blend of the three corner colours weighted by how close it is to each.
layout(location = 0) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
