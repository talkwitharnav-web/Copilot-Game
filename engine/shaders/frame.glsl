// The per-frame uniform block, declared once.
//
// **Must match `FrameUniforms` in FrameUniforms.hpp, member for member and in
// order.** It lived in four shaders as four hand-copied declarations, three of
// which had already drifted to different lengths - legal, because a shorter
// declaration is still a prefix of the same buffer, and exactly the shape of
// bug this project keeps paying for. One owner now.

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
