#version 450

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

// Must match MeshPushConstants in PushConstants.hpp.
layout(push_constant) uniform Push {
    mat4 modelViewProjection;
} push;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = push.modelViewProjection * vec4(inPosition, 1.0);
    fragColor = inColor;
}
