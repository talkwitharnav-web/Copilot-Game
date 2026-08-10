#pragma once

#include "item/Item.hpp"

#include <array>
#include <cstddef>

namespace game {

/// How many slots a chest holds. Three rows of nine, which is the reference's
/// and — not by accident — exactly the shape of the player's own storage, so
/// one panel layout serves both halves of the screen.
constexpr std::size_t kChestSlots = 27;

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
