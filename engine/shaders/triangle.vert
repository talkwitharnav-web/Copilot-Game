#version 450

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in float inLayer;

// Must match MeshPushConstants in PushConstants.hpp.
layout(push_constant) uniform Push {
    mat4 modelViewProjection;
    vec4 sunDirection;
    vec4 lighting;
    vec4 animation;
    vec4 fog;
} push;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUv;
// flat: the layer index must not be interpolated between corners, or triangles
// would sample a blend of two different textures across their surface.
layout(location = 2) flat out float fragLayer;
// Chunk geometry is drawn with an identity model matrix, so its input position
// is already world space. That is what lets the fragment shader recover a face
// normal without the vertex format carrying one.
layout(location = 3) out vec3 fragWorldPosition;
// Distance from the eye along the view axis. A perspective projection leaves it
// in `gl_Position.w`, so fog costs one varying and no extra maths.
layout(location = 4) out float fragViewDepth;

void main() {
    gl_Position = push.modelViewProjection * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragUv = inUv;
    fragLayer = inLayer;
    fragWorldPosition = inPosition;
    fragViewDepth = gl_Position.w;
}
