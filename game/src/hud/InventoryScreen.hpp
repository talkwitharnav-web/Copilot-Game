#pragma once

#include "item/Inventory.hpp"

#include <engine/render/MeshData.hpp>

#include <cstddef>
#include <optional>

namespace game {

/// The inventory screen, laid out from the concept art in `reference/`.
///
/// Everything is built in the same screen space as the rest of the HUD:
/// coordinates relative to window **height** on both axes, Y growing downward.
namespace inventoryScreen {

/// The player's own crafting grid, as drawn on the panel. A crafting table will
/// want 3x3; the recipe matcher already takes the size as an argument, so that
/// is a layout change rather than a logic one.
constexpr int kCraftSize = 2;
constexpr std::size_t kCraftSlots = kCraftSize * kCraftSize;

/// Slots the layout owns beyond the inventory grid itself.
enum class Region {
    Grid,
    Armour,
    Offhand,
    Craft,
    CraftResult,
};

struct SlotHit {
    Region region;
    std::size_t index;
};

/// Which slot, if any, sits under a point in screen space.
std::optional<SlotHit> slotAt(float x, float y, float aspect);

/// True when the point is anywhere over the panel, so clicks outside it can be
/// told apart from clicks that missed a slot.
bool insidePanel(float x, float y, float aspect);

/// `heldStack` is what the cursor is carrying, drawn at (cursorX, cursorY).
engine::MeshData build(const Inventory& inventory, const ItemStack* craftSlots, const ItemStack& craftResult,
                       const ItemStack& heldStack, float cursorX, float cursorY, float aspect);

} // namespace inventoryScreen
} // namespace game
