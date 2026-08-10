#version 450

// These locations must match Vertex::attributeDescriptions() in Vertex.hpp.
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

void main() {
    vec3 position = inPosition;

    // **The water surface actually moves.** Only vertices the mesher marked as
    // sitting on a fluid's top are touched, so a waterfall's foot, the seabed
    // and the sides below the waterline all stay put and the column cannot open
    // a seam. Two vertices at the same world XZ get the same answer whatever
    // chunk or face they came from, which is what keeps it watertight.
    bool fluidTop = (inSurface & (1u << 11)) != 0u;
    if (fluidTop && frame.animation.x >= 0.0 && abs(inLayer - frame.animation.x) < 0.25) {
        float height = 0.0;
        vec2 slope = vec2(0.0);
        waterWave(position.xz, frame.water.x, height, slope);
        position.y += height * frame.water.y * kWaveDisplacement;
    }

    // **Grass bends with the wind, it does not wag.** The displacement is
    // always *along* the wind and never through zero: a blade that oscillated
    // about its rest position would read as a metronome. The per-position phase
    // is what stops a whole field moving in unison - and the gust term, which
    // is shared, is what makes it occasionally do exactly that.
    //
    // **The phase must turn slowly across space.** At better than a radian per
    // block, two corners of the same blade land on opposite parts of the cycle
    // and move in opposite directions, which shears the plant apart rather than
    // bending it. A fifth of a radian per block keeps one plant coherent while
    // still separating it from its neighbours a few blocks away.
    //
    // **The phase is a function of the column only.** It used to include the
    // vertex's height, which put the top of a three-block stalk a third of a
    // radian out of step with its own base - so the plant twisted along its
    // length instead of leaning. Everything in one column now shares a phase,
    // which is what makes a tall plant move as one thing.
    //
    // How far the vertex moves is `lean`: its height above its own root, in
    // blocks, so the base of a stalk holds still and the tip travels furthest.
    uint swayUnits = (inSurface >> 12) & 15u;
    if (swayUnits != 0u && frame.wind.z > 0.0) {
        float lean = min(float(swayUnits) * 0.5, 6.0);
        float phase = dot(position.xz, vec2(0.21, 0.29));
        float sway = 0.5 + 0.5 * sin(frame.wind.w * 2.1 + phase);
        sway += 0.25 * sin(frame.wind.w * 3.7 + phase * 1.9);
        position.xz += frame.wind.xy * frame.wind.z * sway * lean;
        // A little lift with the bend, or the tip visibly shortens as it leans.
        position.y += frame.wind.z * sway * 0.25 * lean;
    }

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
