// Shared by the two bloom passes and the tone mapper.
//
// Must match PostPushConstants in PushConstants.hpp, field for field.

layout(push_constant) uniform Post {
    // xy: one texel of the source, in UV. z: the upsample filter radius.
    // w: how much of a coarser mip an upsample contributes.
    vec4 filterParams;
    // x: exposure. y: which tone mapping curve. z: bloom strength, 0 for none.
    vec4 imageParams;
} post;
