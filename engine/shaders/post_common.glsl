// Shared by the two bloom passes, the tone mapper and the deferred pass.
//
// **All four include this file, as of 2026-08-19.** `deferred.frag` carried its
// own transcribed copy of this block until then, and it had already drifted -
// it documented `imageParams.w` as the debug view while this file and
// `PostPushConstants` both said nothing about `w`. These eight floats now exist
// as one C++ struct and exactly one GLSL declaration.
//
// Must match PostPushConstants in PushConstants.hpp, field for field. **That
// header is the owner and carries the full per-pass table**; the note below is
// the bloom chain's own view and is deliberately not the whole truth.

layout(push_constant) uniform Post {
    // xy: one texel of the source, in UV - `bloom_down` only.
    // z: the upsample filter radius, w: how much of a coarser mip an upsample
    // contributes - `bloom_up` only.
    //
    // **`x` means something else entirely in `tonemap.frag`**, which includes
    // this file and reads it as the anti-alias amount. `y` likewise carries the
    // contact-occlusion radius in `deferred.frag`. Four passes share one block
    // and no field has a single meaning.
    vec4 filterParams;
    // x: exposure. y: which tone mapping curve. z: bloom strength, 0 for none.
    // **w is the debug view**, read by `deferred.frag`. It is not spare.
    vec4 imageParams;
} post;
