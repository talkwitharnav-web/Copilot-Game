#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace engine {

/// Data pushed straight into the command buffer alongside a draw.
///
/// Push constants are a small block that avoids the descriptor-set machinery
/// entirely, which makes them ideal for a per-draw matrix. The
/// `layout(push_constant)` block in the shader must match this exactly, field
/// for field.
///
/// **Only things that genuinely differ per draw belong here.** Vulkan
/// guarantees just 128 bytes, and a meaningful share of Windows devices report
/// exactly that, so everything constant across a frame lives in
/// `FrameUniforms` instead - it was 80 bytes of unchanging data re-pushed for
/// every one of the two hundred-odd draws in a frame.
struct MeshPushConstants {
    glm::mat4 modelViewProjection;
    /// x: a bitmask of the `kDrawFlag*` values below. y, z and w are spare.
    ///
    /// A bitmask rather than separate floats because both flags answer a yes/no
    /// question, and the shader used to recover them by comparing a float
    /// against 0.5.
    glm::uvec4 flags{0u, 0u, 0u, 0u};
};

/// **All five of these are re-spelled as bare decimal literals in
/// `triangle.frag`, and nothing in the build compares the two ends.** That file
/// declares `const uint kDrawFlagLit = 1u;` through
/// `const uint kDrawFlagBlended = 16u;` under a comment saying they must match
/// this header. This side is written as shifts, that side as the numbers the
/// shifts produce, so **inserting a flag anywhere but the end renumbers every
/// flag below it here and moves nothing there**: every sky draw would test as
/// emissive and every blended draw as sky, on a green build with no validation
/// error. Append new flags at the end, never in the middle - and when you must
/// insert one, edit `triangle.frag` in the same batch.
///
/// **The warning is sited here rather than there because this is the file whose
/// edit breaks it.** `triangle.frag` is the file that suffers, and it cannot
/// see a change made in this one. That is the same instrument the `kNormal*`
/// codes carry in `Vertex.hpp`, for the same reason.
///
/// Dated 2026-08-19, and **re-run the search rather than trusting this
/// paragraph** - a list rots, a search does not: `kDrawFlag` across
/// `engine/shaders`. Today it returns `triangle.frag` alone, 11 hits, five
/// definitions and five uses. A flag tested without naming the constant would
/// escape that search; there are none today, checked with `push.flags.x &`.
///
/// World geometry: apply the directional sun term and the cutout alpha test.
/// The sky, the block outline and every screen-space draw pass without it.
inline constexpr std::uint32_t kDrawFlagLit = 1u << 0;
/// Fade this draw into the distance fog. Screen-space geometry passes without
/// it, or the HUD would fade out along with the world.
inline constexpr std::uint32_t kDrawFlagFogged = 1u << 1;
/// Scale this draw by the frame's sky emission, so the sun can be brighter than
/// white and bloom around its edge instead of clipping to a flat disc.
inline constexpr std::uint32_t kDrawFlagEmissive = 1u << 2;

/// The sun and the moon. **Exempt from distance fog**: they are drawn at the
/// far plane by definition, so the fog that dissolves the edge of the world
/// would dissolve them with it. They still vanish underwater, where the fog
/// starts at the eye rather than at a distance.
inline constexpr std::uint32_t kDrawFlagSky = 1u << 3;

/// The blended pass: water and glass. **Exempt from the cutout alpha test**,
/// which is the whole point of it - a cutout throws away anything under half
/// alpha, and glass art is a frame at 0.78 around a panel at 0.43, so the test
/// would delete the panel and leave a rectangle of border. Only geometry that
/// is genuinely sorted behind everything opaque may carry this.
inline constexpr std::uint32_t kDrawFlagBlended = 1u << 4;

static_assert(sizeof(MeshPushConstants) == 80,
              "Push constants must stay well under the 128 bytes Vulkan guarantees");

/// What the full-screen passes push. Must match `post_common.glsl`, which since
/// 2026-08-19 is the **single** GLSL declaration of this block - `deferred.frag`
/// held a second transcribed copy until then and now `#include`s the shared file
/// like the other three. So these eight floats have one C++ end and one GLSL
/// end, and nothing in the build still compares two hand-written copies.
///
/// **Four passes share these eight floats and each reads a different subset, so
/// no field has one meaning.** The per-field comments below used to describe
/// the bloom chain alone, which is one consumer of four - the exact shape that
/// twice made a `FrameUniforms` slot get called spare while something was
/// already reading it. The complete map, from a sweep of every `post.` read in
/// `engine/shaders` (10 reads, 4 shaders, listed rather than counted):
///
/// | field | bloom_down | bloom_up | tonemap | deferred |
/// |---|---|---|---|---|
/// | `filterParams.x` | source texel U | - | **anti-alias amount** | - |
/// | `filterParams.y` | source texel V | - | - | **contact-occlusion radius** |
/// | `filterParams.z` | - | upsample radius | - | - |
/// | `filterParams.w` | - | mip blend weight | - | - |
/// | `imageParams.x`  | - | - | exposure | - |
/// | `imageParams.y`  | - | - | tone curve | - |
/// | `imageParams.z`  | - | - | bloom strength | - |
/// | `imageParams.w`  | - | - | - | **debug view** |
///
/// **There is no free slot.** Every one of the eight is read by something.
/// A new per-pass float needs a new field, and adding one means editing this
/// struct, `post_common.glsl` and `deferred.frag`'s own copy together.
///
/// **What would falsify this table:** any `post.filterParams` or
/// `post.imageParams` read in a shader not listed above, or a component reached
/// as `.r/.g/.b/.a`, `.s/.t/.p/.q` or `[0..3]` rather than `.x/.y/.z/.w` - all
/// three spellings were searched. Swizzles are what a name search misses.
/// Measured 2026-08-19; re-run it rather than trusting the date.
struct PostPushConstants {
    /// **Four meanings, one per pass** - see the table above. xy is one texel of
    /// the source in UV for `bloom_down` only; z is the upsample radius and w
    /// the mip blend weight for `bloom_up` only; x is the anti-alias amount for
    /// `tonemap`; y is the contact-occlusion radius for `deferred`.
    glm::vec4 filterParams{0.0f, 0.0f, 0.0f, 0.0f};
    /// x: exposure. y: which tone mapping curve. z: bloom strength, 0 for none.
    /// **w: which debug view**, read by `deferred.frag` and by nothing else -
    /// it is not spare, and writing anything but 0 into it from a pass other
    /// than the deferred one has no effect only because nothing else reads it.
    glm::vec4 imageParams{1.0f, 0.0f, 0.0f, 0.0f};
};

/// The curves `tonemap.frag` offers, in the order it tests for them.
enum class ToneMapper : unsigned {
    PbrNeutral = 0,
    Hable = 1,
    ReinhardLuminance = 2,
    Aces = 3,
    Count = 4,
};

} // namespace engine
