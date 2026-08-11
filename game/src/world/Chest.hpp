#pragma once

#include "item/Item.hpp"

#include <array>
#include <cstddef>

namespace game {

/// How many slots a chest holds. Three rows of nine, which is the reference's
/// and — not by accident — exactly the shape of the player's own storage, so
/// one panel layout serves both halves of the screen.
constexpr std::size_t kChestSlots = 27;

/// How many a hopper holds. It stores its five in the same `Chest` struct and
/// simply never touches the rest, which is what lets the whole container path -
/// saving, spilling on break, clicking, shift-clicking - serve both with no
/// second type.
constexpr std::size_t kHopperSlots = 5;

/// How often a hopper moves one item. Eight game ticks at the reference's
/// twenty a second, written as the seconds our frame loop actually counts in.
constexpr float kHopperTransferSeconds = 8.0f / 20.0f;

/// One chest's contents.
///
/// A **block entity**, for the same reason a furnace's is: which way a chest
/// opens is part of *which block it is* and lives in the id, but an inventory is
/// not and could never fit there.
struct Chest {
    std::array<ItemStack, kChestSlots> slots{};

    /// Nothing in it, so it can be forgotten rather than written to disk.
    bool empty() const {
        for (const ItemStack& slot : slots) {
            if (!slot.empty()) {
                return false;
            }
        }
        return true;
    }
};

} // namespace game
