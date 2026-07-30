#version 450

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in float inLayer;

// Must match MeshPushConstants in PushConstants.hpp.
layout(push_constant) uniform Push {
    mat4 modelViewProjection;
} push;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragUv;
// flat: the layer index must not be interpolated between corners, or triangles
// would sample a blend of two different textures across their surface.
layout(location = 2) flat out float fragLayer;

void main() {
    gl_Position = push.modelViewProjection * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragUv = inUv;
    fragLayer = inLayer;
}
