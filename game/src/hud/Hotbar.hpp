#pragma once

#include "item/Inventory.hpp"

#include <engine/render/MeshData.hpp>

#include <cstddef>

namespace game {

/// The row of carried items along the bottom of the screen.
///
/// Rebuilt whenever the selection or its contents change rather than animated,
/// because the whole bar is a few dozen triangles and uploading it is cheaper
/// than tracking which slot moved. An empty stack leaves a slot bare.
engine::MeshData makeHotbar(const Inventory& inventory, std::size_t selected);

} // namespace game
