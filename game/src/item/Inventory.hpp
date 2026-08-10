#pragma once

#include "item/Item.hpp"

#include <array>
#include <cstddef>

namespace game {

/// Slots reachable from the hotbar, and the width of the storage grid.
constexpr std::size_t kHotbarSlots = 9;

/// Rows of storage above the hotbar.
constexpr std::size_t kStorageRows = 3;

/// The hotbar is the **first** nine slots, not the last, so the number keys map
/// straight onto slot indices.
constexpr std::size_t kInventorySlots = kHotbarSlots * (kStorageRows + 1);

/// What the player is carrying.
///
/// Fixed size and plain data: no allocation, cheap to copy, and it can be
/// written straight to the save file when persistence arrives.
class Inventory {
public:
    ItemStack& slot(std::size_t index) { return m_slots[index]; }
    const ItemStack& slot(std::size_t index) const { return m_slots[index]; }

    static constexpr std::size_t size() { return kInventorySlots; }

    /// Adds what it can, returning whatever did not fit.
    ///
    /// Tops up matching stacks before opening an empty slot, so picking things
    /// up does not scatter one item across several slots.
    int add(ItemId item, int count);

    /// True if `count` of `item` could be added without anything being lost.
    bool hasRoomFor(ItemId item, int count) const;

    /// Removes one from a slot, clearing it when the last is used.
    void consumeOne(std::size_t index);

    /// How many of an item are carried, across every slot.
    int count(ItemId item) const;

    /// Spends up to `wanted` of an item wherever it is held, returning how many
    /// were actually taken. Ammunition is spent from the whole inventory rather
    /// than from the selected slot, which is holding the bow.
    int consume(ItemId item, int wanted);

private:
    std::array<ItemStack, kInventorySlots> m_slots{};
};

} // namespace game
