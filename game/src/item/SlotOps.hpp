#pragma once

#include "item/Item.hpp"

#include <cstddef>
#include <vector>

namespace game {

/// What a click does to one slot, given what the cursor is carrying.
///
/// These are the rules the genre established, and they are worth following
/// exactly: anyone who has played one of these games already knows them, and
/// getting them subtly wrong is more jarring than not having them.
///
/// Free functions over two stacks rather than methods on the inventory, because
/// the crafting grid is not part of the inventory and obeys the same rules.
namespace slots {

/// Whole stacks. Picks up, puts down, merges what fits, or swaps.
void leftClick(ItemStack& slot, ItemStack& cursor);

/// Half on the way out, one at a time on the way in. An odd stack leaves the
/// smaller half behind, so 7 picks up 4.
void rightClick(ItemStack& slot, ItemStack& cursor);

/// Spreads `cursor` as evenly as it goes across every slot in `targets`,
/// leaving any remainder on the cursor.
///
/// `one` places a single item in each instead, which is what the right button
/// does. Slots holding a different item are skipped rather than overwritten.
void distribute(const std::vector<ItemStack*>& targets, ItemStack& cursor, bool one);

} // namespace slots
} // namespace game
