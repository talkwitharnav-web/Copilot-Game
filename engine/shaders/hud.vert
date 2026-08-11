#version 450

// The interface's own vertex stage.
//
// **It exists because the HUD was going through `triangle.vert`**, which
// carries the water wave, the wind sway and three varyings the interface never
// reads - so every panel, slot, icon and glyph paid for a fluid-surface test
// and a gust term that can never fire, and the validation layers reported three
// vertex outputs with no matching fragment input on every single run. A warning
// nobody can act on is how a real one goes unread.
//
// Screen-space geometry has no world position, no normal and no fog, so this is
// the whole of what `hud.frag` actually consumes.

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
// Only three are read; the pipeline still describes all five, so they are
// declared in full rather than left to be guessed at.
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
// flat: the layer index selects a texture and must not be blended between
// corners, or a quad would sample a mix of two sheets across its surface.
layout(location = 2) flat out float fragLayer;

void main() {
    gl_Position = push.modelViewProjection * vec4(inPosition, 1.0);
    fragColor = inColor;
    fragUv = inUv;
    fragLayer = inLayer;
}
