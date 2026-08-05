#include "item/SlotOps.hpp"

#include <algorithm>

namespace game::slots {
namespace {

/// Moves what fits from `from` into `to`, assuming they hold the same item.
void merge(ItemStack& to, ItemStack& from, int limit) {
    // Capacity comes from what is being moved, because an empty destination has
    // no item of its own to ask - and a tool's capacity is one.
    const int capacity = maxStackFor(from.item);
    const int room = to.empty() ? capacity : capacity - to.count;
    const int moved = std::min({limit, from.count, room});
    if (moved <= 0) {
        return;
    }
    to.item = from.item;
    to.damage = from.damage;
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

int quickMove(ItemStack& from, const std::vector<ItemStack*>& targets) {
    if (from.empty()) {
        return 0;
    }

    // Matching stacks before empty ones, or moving into a half-full inventory
    // opens a second stack of something already carried.
    for (ItemStack* target : targets) {
        if (from.empty()) {
            break;
        }
        if (target->empty() || target->item != from.item) {
            continue;
        }
        merge(*target, from, from.count);
    }
    for (ItemStack* target : targets) {
        if (from.empty()) {
            break;
        }
        if (!target->empty()) {
            continue;
        }
        merge(*target, from, from.count);
    }
    return from.count;
}

void gather(ItemStack& cursor, const std::vector<ItemStack*>& sources) {
    if (cursor.empty() || cursor.space() <= 0) {
        return;
    }

    std::vector<ItemStack*> matching;
    matching.reserve(sources.size());
    for (ItemStack* source : sources) {
        if (!source->empty() && source->item == cursor.item) {
            matching.push_back(source);
        }
    }
    std::sort(matching.begin(), matching.end(),
              [](const ItemStack* a, const ItemStack* b) { return a->count < b->count; });

    for (ItemStack* source : matching) {
        if (cursor.space() <= 0) {
            break;
        }
        const int taken = std::min(source->count, cursor.space());
        cursor.count += taken;
        source->count -= taken;
        if (source->count <= 0) {
            *source = ItemStack{};
        }
    }
}

} // namespace game::slots
