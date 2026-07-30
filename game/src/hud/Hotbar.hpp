#pragma once

#include "world/Block.hpp"

#include <engine/render/MeshData.hpp>

#include <array>
#include <cstddef>

namespace game {

constexpr std::size_t kHotbarSlots = 9;

/// The row of selectable blocks along the bottom of the screen.
///
/// Rebuilt whenever the selection changes rather than animated, because the
/// whole bar is a few dozen triangles and uploading it is cheaper than tracking
/// which slot moved. `BlockId::Air` leaves a slot empty.
engine::MeshData makeHotbar(const std::array<BlockId, kHotbarSlots>& slots, std::size_t selected);

} // namespace game
