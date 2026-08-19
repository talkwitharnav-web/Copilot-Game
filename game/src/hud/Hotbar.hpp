#pragma once

#include "item/Inventory.hpp"

#include <engine/render/MeshData.hpp>

#include <cstddef>

namespace game {

/// On-screen size of one hotbar cell, relative to window height. The renderer
/// corrects for aspect ratio. Y spans -1 to 1 across the window, so this is a
/// little over 6% of screen height per cell.
///
/// **Public because the status bars measure themselves against the bar they sit
/// on.** Hearts that do not line up with the hotbar's ends read as a mistake
/// however carefully they were sized on their own, so there is one owner of
/// this number and everything above the bar derives from it.
constexpr float kHotbarSlotSize = 0.152f;

/// Positive Y is down in screen space, so this sits near the bottom edge.
constexpr float kHotbarCentreY = 0.888f;

constexpr float kHotbarWidth = kHotbarSlotSize * static_cast<float>(kHotbarSlots);
constexpr float kHotbarHalfWidth = kHotbarWidth * 0.5f;

/// What anything stacked above the bar lines up against.
constexpr float kHotbarTopY = kHotbarCentreY - kHotbarSlotSize * 0.5f;

/// How far the selected cell's oversized frame reaches above `kHotbarTopY`.
///
/// The sheet's selected cell is 25 texels where an ordinary one is 21, and it is
/// drawn larger by that same ratio so the frame overhangs its neighbours exactly
/// as the artwork intends. That overhang is **above the bar as well as beside
/// it**, and the hearts sat 0.007 units inside it - drawn first, so the frame
/// covered the bottom of the leftmost heart whenever slot 1 was selected.
///
/// One owner, because two files place things against it: `Hotbar.cpp` draws the
/// frame and `StatusBars.cpp` has to clear it.
constexpr float kHotbarSelectedOverhang = kHotbarSlotSize * (25.0f / 21.0f - 1.0f) * 0.5f;

/// The row of carried items along the bottom of the screen.
///
/// Rebuilt whenever the selection or its contents change rather than animated,
/// because the whole bar is a few dozen triangles and uploading it is cheaper
/// than tracking which slot moved. An empty stack leaves a slot bare.
/// `bowDrawSeconds` is how long the selected bow has been held, or a negative
/// number when nothing is being drawn. It changes only the icon: the picture of
/// a bow at rest becomes one of a bow being pulled, which is the only feedback
/// the charge has.
engine::MeshData makeHotbar(const Inventory& inventory, std::size_t selected,
                            float bowDrawSeconds = -1.0f);

} // namespace game
