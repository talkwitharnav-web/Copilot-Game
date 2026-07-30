#version 450

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    // Vulkan clip space puts -1 at the top of the screen, unlike OpenGL.
    gl_Position = vec4(inPosition, 1.0);
    fragColor = inColor;
}
