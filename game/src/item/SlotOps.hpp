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

/// How many of `item` at that `damage` a stack could still take.
///
/// **One owner for "can this stack accept these items."** `Inventory::add`
/// compared `damage` and `Inventory::hasRoomFor` did not, so the shift-click
/// craft loop counted a stowbox holding one set of contents as room for a box
/// holding another - space that does not exist. Every place that used to test
/// `item` alone asks this instead, because that only ever worked by the
/// coincidence that nothing carrying a meaningful `damage` stacks past one.
constexpr int roomFor(const ItemStack& stack, ItemId item, int damage) {
    if (item == ItemId::None) {
        return 0;
    }
    // Capacity comes from what is arriving, because an empty slot has no item
    // of its own to ask - and a tool's capacity is one.
    if (stack.empty()) {
        return maxStackFor(item);
    }
    if (stack.item != item || stack.damage != damage) {
        return 0;
    }
    const int room = maxStackFor(item) - stack.count;
    return room > 0 ? room : 0;
}

/// The same rule asked of a whole stack that is arriving.
constexpr int roomFor(const ItemStack& stack, const ItemStack& incoming) {
    return roomFor(stack, incoming.item, incoming.damage);
}

static_assert(roomFor(ItemStack{}, ItemId::IronPickaxe, 0) == 1 &&
                  roomFor(ItemStack{}, ItemId::Bucket, 0) == 16,
              "an empty slot takes what the *arriving* item stacks to, which is why this asks "
              "the incoming item and not the slot. Writing `kMaxStack` here instead is the "
              "add/hasRoomFor disagreement this file exists to prevent, and asking it of Coal "
              "cannot catch it - Coal's cap is sixty-four either way");
static_assert(roomFor(ItemStack{ItemId::Coal, 1, 0}, ItemId::Coal, 0) ==
                  maxStackFor(ItemId::Coal) - 1,
              "a matching slot takes the rest of the stack");
static_assert(roomFor(ItemStack{ItemId::Coal, 1, 0}, ItemId::Coal, 1) == 0,
              "a difference in damage is a difference in kind, or a stowbox's contents merge away");
static_assert(roomFor(ItemStack{ItemId::IronPickaxe, 1, 0}, ItemId::IronPickaxe, 0) == 0,
              "two tools have no room for each other, which is why a click on one must swap");

/// Moves what fits from `from` into `to`, up to `limit`, and returns how many
/// actually moved. `to` may be empty, in which case it becomes `from`'s item at
/// `from`'s **damage** - a tool's wear, a stowbox's contents.
///
/// **The primitive every container-to-container move needs.** It was private to
/// this file for twenty milestones, so the hopper hand-rolled its own and built
/// `ItemStack{source.item, 1}` without the damage: a worn tool came out of one
/// end new, and a stowbox came out holding nothing. Nothing else here is
/// allowed to write a stack field by hand for the same reason.
///
/// **The count is the answer a click needs.** Two of anything that does not
/// stack have no room for each other, so moving nothing is not "the click was
/// handled" - it is what turns into a swap.
int merge(ItemStack& to, ItemStack& from, int limit);

/// One item across, which is exactly what a hopper moves per transfer. Returns
/// 1 if it went and 0 if there was no room, so the caller's eligibility test is
/// `roomFor(...) > 0` and its body is this line.
int moveOne(ItemStack& to, ItemStack& from);

/// Whole stacks. Picks up, puts down, merges what fits, or swaps.
///
/// Two of anything that does not stack - tools, stowboxes, armour, a full
/// bucket - are **swapped**, because nothing can move between them and a click
/// that visibly does nothing reads as a broken inventory.
void leftClick(ItemStack& slot, ItemStack& cursor);

/// Half on the way out, one at a time on the way in. An odd stack leaves the
/// smaller half behind, so 7 picks up 4. The half taken carries the stack's
/// `damage` with it, which is a tool's wear and a stowbox's contents.
///
/// Two that cannot stack swap, for the same reason the left button does.
void rightClick(ItemStack& slot, ItemStack& cursor);

/// Spreads `cursor` as evenly as it goes across every slot in `targets`,
/// leaving any remainder on the cursor.
///
/// `one` places a single item in each instead, which is what the right button
/// does. Slots holding a different item are skipped rather than overwritten.
void distribute(const std::vector<ItemStack*>& targets, ItemStack& cursor, bool one);

/// Sends a whole stack to wherever it fits among `targets`, topping up matching
/// stacks before opening an empty one. Returns what would not fit, left in
/// `from`.
///
/// This is shift-click. Which slots count as "the other place" is the caller's
/// decision, because it differs per region: the hotbar sends to storage, storage
/// sends to the hotbar, and a crafting grid sends to the whole inventory.
int quickMove(ItemStack& from, const std::vector<ItemStack*>& targets);

/// Pulls matching items out of `sources` onto `cursor` until it is full.
///
/// This is double-click. Smallest stacks go first, so the gesture consolidates
/// scattered leftovers rather than breaking up a full stack to do it.
void gather(ItemStack& cursor, const std::vector<ItemStack*>& sources);

} // namespace slots
} // namespace game
