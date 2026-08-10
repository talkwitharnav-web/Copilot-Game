#pragma once

#include <engine/render/Vertex.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace game {

/// The six directions a voxel face can point, in the order the mesher's own
/// face table lists them.
enum class AxisFace : std::uint8_t {
    PosX,
    NegX,
    PosY,
    NegY,
    PosZ,
    NegZ,
    Count,
};

/// Fixed brightness by orientation, so surfaces stay readable even where the
/// light is flat. Multiplied with the computed lighting rather than replacing it.
///
/// **One owner.** `FallingBlock`, `ItemEntity` and `Creature` each wrote these
/// out by hand, and FallingBlock's had drifted on five of its six faces while
/// its comment claimed they matched - so a block that had just started falling
/// was shaded differently from the block it had been a moment earlier.
inline constexpr std::array<float, static_cast<std::size_t>(AxisFace::Count)> kFaceShades{
    0.72f, // +X
    0.72f, // -X
    1.00f, // +Y
    0.45f, // -Y
    0.86f, // +Z
    0.60f, // -Z
};

/// Which of the three-bit normal codes a vertex on that face carries.
inline constexpr std::array<std::uint32_t, static_cast<std::size_t>(AxisFace::Count)> kFaceNormalCodes{
    engine::kNormalPosX, engine::kNormalNegX, engine::kNormalPosY,
    engine::kNormalNegY, engine::kNormalPosZ, engine::kNormalNegZ,
};

constexpr float faceShade(AxisFace face) {
    return kFaceShades[static_cast<std::size_t>(face)];
}

constexpr std::uint32_t faceNormalCode(AxisFace face) {
    return kFaceNormalCodes[static_cast<std::size_t>(face)];
}

} // namespace game
