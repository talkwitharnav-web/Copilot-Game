#include "item/SlotOps.hpp"

#include <algorithm>

namespace game::slots {
namespace {

/// Moves what fits from `from` into `to`, assuming they hold the same item.
void merge(ItemStack& to, ItemStack& from, int limit) {
    const int moved = std::min({limit, from.count, to.space()});
    if (moved <= 0) {
        return;
    }
    to.item = from.item;
    to.count += moved;
    from.count -= moved;
    if (from.count <= 0) {
        from = ItemStack{};
    }
}

} // namespace

void leftClick(ItemStack& slot, ItemStack& cursor) {
    if (cursor.empty()) {
        std::swap(slot, cursor);
        return;
    }
    if (slot.empty()) {
        std::swap(slot, cursor);
        return;
    }
    if (slot.item == cursor.item) {
        // Tops the slot up and keeps whatever did not fit, rather than refusing
        // the click outright.
        merge(slot, cursor, cursor.count);
        return;
    }
    std::swap(slot, cursor);
}

void rightClick(ItemStack& slot, ItemStack& cursor) {
    if (cursor.empty()) {
        if (slot.empty()) {
            return;
        }
        // Rounded up, so the smaller half is what stays behind.
        const int taken = (slot.count + 1) / 2;
        cursor = ItemStack{slot.item, taken};
        slot.count -= taken;
        if (slot.count <= 0) {
            slot = ItemStack{};
        }
        return;
    }

    if (slot.empty() || slot.item == cursor.item) {
        merge(slot, cursor, 1);
        return;
    }
    std::swap(slot, cursor);
}

void distribute(const std::vector<ItemStack*>& targets, ItemStack& cursor, bool one) {
    if (cursor.empty() || targets.empty()) {
        return;
    }

    // Only slots that can actually take the item count toward the share, or a
    // drag across occupied slots would silently shrink everyone else's portion.
    std::vector<ItemStack*> eligible;
    eligible.reserve(targets.size());
    for (ItemStack* target : targets) {
        if (target->empty() || (target->item == cursor.item && target->space() > 0)) {
            eligible.push_back(target);
        }
    }
    if (eligible.empty()) {
        return;
    }

    const int share = one ? 1 : std::max(1, cursor.count / static_cast<int>(eligible.size()));
    for (ItemStack* target : eligible) {
        if (cursor.empty()) {
            break;
        }
        merge(*target, cursor, share);
    }
}

} // namespace game::slots
