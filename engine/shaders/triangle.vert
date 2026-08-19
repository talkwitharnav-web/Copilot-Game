#version 450

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
// These five are not declared here so much as transcribed. `Vertex.hpp` owns
// the layout - its `attributeDescriptions()` is the only table Vulkan reads,
// and nothing in the build compares it against these lines. Change one end and
// you must change the other by hand; the compiler will not say a word.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;
layout(location = 3) in float inLayer;
layout(location = 4) in uint inSurface;

// Must match MeshPushConstants in PushConstants.hpp.
layout(push_constant) uniform Push {
    mat4 modelViewProjection;
    uvec4 flags;
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
// Unpacked here and sent on as two varyings rather than one, because they need
// **different interpolation**: ambient occlusion is baked per corner and has to
// blend across the quad, which is the whole of smooth lighting, while the face
// normal is one fact about the whole surface and must not be blended at all.
layout(location = 5) flat out uint fragNormalCode;
layout(location = 6) out float fragOcclusion;

#include "frame.glsl"
#include "waves.glsl"
#include "displace.glsl"

void main() {
    // Wind sway and the water wave, shared with `shadow.vert` so a plant's
    // shadow leans with the plant instead of staying at its rest position.
    vec3 position = displacedPosition(inPosition, inSurface, inLayer);

    gl_Position = push.modelViewProjection * vec4(position, 1.0);
    fragColor = inColor;
    fragUv = inUv;
    fragLayer = inLayer;
    fragWorldPosition = position;
    fragViewDepth = gl_Position.w;
    fragNormalCode = inSurface & 7u;
    // Masked, not merely shifted: bit 11 above it is the fluid-top flag, and an
    // unmasked shift would fold it into the occlusion and light every water
    // surface at eight times white.
    fragOcclusion = float((inSurface >> 3) & 0xffu) / 255.0;
}
