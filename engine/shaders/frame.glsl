// The per-frame uniform block, declared once.
//
// **Must match `FrameUniforms` in FrameUniforms.hpp, member for member and in
// order.** It lived in four shaders as four hand-copied declarations, three of
// which had already drifted to different lengths - legal, because a shorter
// declaration is still a prefix of the same buffer, and exactly the shape of
// bug this project keeps paying for. One owner now.
//
// **`shadowMatrices`' extent of 4 is part of that match and is the silent half
// of it.** It must equal `ShadowMap::kMaxCascades`. Editing the constant is
// caught - a `static_assert` sits beside it and stops the build - but editing
// the `[4]` below is caught by nothing whatsoever: both C++ asserts still pass,
// because the sizeof one is written in terms of that same constant. Shorten it
// and every member declared after it here - fog, eye, wind, all of them - reads
// from a different offset than the struct writes, with no diagnostic from any
// tool in this project. This comment is the only instrument on that direction,
// which is the whole reason it is here. Checked 2026-08-19 11:26.

layout(set = 0, binding = 4) uniform Frame {
    vec4 sunDirection;
    vec4 lighting;
    vec4 animation;
    vec4 fog;
    vec4 eye;
    mat4 inverseViewProjection;
    mat4 shadowMatrices[4];
    vec4 shadowTexelWorld;
    vec4 shadowParams;
    vec4 cloud;
    vec4 water;
    vec4 waterDetail;
    vec4 weather;
    vec4 wind;
    vec4 glow;
    vec4 volumetric;
} frame;
