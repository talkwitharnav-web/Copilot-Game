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

static_assert(sizeof(MeshPushConstants) == 80,
              "Push constants must stay well under the 128 bytes Vulkan guarantees");

/// What the full-screen passes push. Must match `post_common.glsl`.
struct PostPushConstants {
    /// xy: one texel of the source in UV. z: the upsample filter radius.
    /// w: how much of a coarser bloom mip is blended into the finer one.
    glm::vec4 filterParams{0.0f, 0.0f, 0.0f, 0.0f};
    /// x: exposure. y: which tone mapping curve. z: bloom strength, 0 for none.
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
