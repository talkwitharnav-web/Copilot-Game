#version 450

// The depth-only pass that fills the sun's shadow map. Position and enough to
// answer "is there a hole in the texture here", and nothing else - no lighting,
// no fog, no normals.

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
// `inPosition`, `inUv`, `inLayer` and `inSurface` are all read: `inLayer` tells
// shadow.frag which sheet to alpha-test against, and `inSurface` carries the
// wind-sway and fluid-top bits that move the vertex.
//
// **`inSurface` was unread here until 2026-08-19, and that was the bug, not a
// spare attribute.** The displacement lived only in `triangle.vert`, so grass
// and crops swayed while their shadows stayed at the plants' rest positions -
// 0.275 blocks of divergence for ordinary grass and 1.65 lateral for a tall
// stalk. An earlier note in this file called it "genuinely unread", which was
// true of the code and read as permission to leave it alone. It was not.
//
// `inColor` really is unread - a depth-only pass has no use for a vertex tint -
// but the pipeline still describes all five, so all five are declared here
// rather than left to be guessed at.
// These five are not declared here so much as transcribed. `Vertex.hpp` owns
// the layout - its `attributeDescriptions()` is the only table Vulkan reads,
// and nothing in the build compares it against these lines. Change one end and
// you must change the other by hand; the compiler will not say a word.
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

// `frame.glsl` and `waves.glsl` declare no sampler between them - only the
// frame uniform block at set 0 binding 4, which is already visible to the
// vertex stage. That matters here: this shader runs while the shadow map is
// the depth attachment, so it must not declare world-set binding 7.
#include "frame.glsl"
#include "waves.glsl"
#include "displace.glsl"

void main() {
    // **The same displacement the camera passes apply**, so a swaying plant
    // casts a swaying shadow. Shared rather than copied - see `displace.glsl`.
    vec3 position = displacedPosition(inPosition, inSurface, inLayer);

    gl_Position = push.modelViewProjection * vec4(position, 1.0);
    fragUv = inUv;
    fragLayer = inLayer;
}
