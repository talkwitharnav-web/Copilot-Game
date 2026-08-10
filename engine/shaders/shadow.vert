#version 450

// The depth-only pass that fills the sun's shadow map. Position and enough to
// answer "is there a hole in the texture here", and nothing else - no lighting,
// no fog, no normals.

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
// Only two are read, but the pipeline still describes all five, so they are
// declared here in full rather than left to be guessed at.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in float inLayer;
layout(location = 4) in uint inSurface;

// Must match MeshPushConstants in PushConstants.hpp. The matrix is the *light's*
// view-projection for one cascade rather than the camera's.
layout(push_constant) uniform Push {
    mat4 modelViewProjection;
    uvec4 flags;
} push;

layout(location = 0) out vec2 fragUv;
layout(location = 1) flat out float fragLayer;

void main() {
    gl_Position = push.modelViewProjection * vec4(inPosition, 1.0);
    fragUv = inUv;
    fragLayer = inLayer;
}
