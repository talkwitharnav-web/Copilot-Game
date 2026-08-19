#include "item/SlotOps.hpp"

#include <algorithm>

namespace game::slots {
namespace {

/// Whether two stacks could *never* merge, however much room there were: two of
/// something that stacks to one, or two whose `damage` differs.
///
/// A destination that is merely **full** is not this. The reference leaves that
/// click alone - ten coal onto a full stack of coal does nothing - so "nothing
/// moved" on its own is not grounds to swap.
constexpr bool neverMerges(const ItemStack& slot, const ItemStack& cursor) {
    return maxStackFor(slot.item) == 1 || slot.damage != cursor.damage;
}

static_assert(neverMerges(ItemStack{ItemId::IronPickaxe, 1, 0},
                          ItemStack{ItemId::IronPickaxe, 1, 0}),
              "two tools can never merge, so clicking one onto the other must swap them");
static_assert(!neverMerges(ItemStack{ItemId::Coal, 64, 0}, ItemStack{ItemId::Coal, 10, 0}),
              "a stack that is merely full is left where it is, as the reference leaves it");

} // namespace

int merge(ItemStack& to, ItemStack& from, int limit) {
    const int moved = std::min({limit, from.count, roomFor(to, from)});
    if (moved <= 0) {
        return 0;
    }
    to.item = from.item;
    to.damage = from.damage;
    to.count += moved;
    from.count -= moved;
    if (from.count <= 0) {
        from = ItemStack{};
    }
    return moved;
}

int moveOne(ItemStack& to, ItemStack& from) {
    return merge(to, from, 1);
}

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
        if (merge(slot, cursor, cursor.count) > 0) {
            return;
        }
        // Nothing moved. Two tools, two stowboxes holding different contents,
        // two of anything that stacks to one: they can never merge, so the
        // reference swaps them and a click that visibly does nothing is read as
        // a broken inventory. A destination that is merely full stays put.
        if (!neverMerges(slot, cursor)) {
            return;
        }
    }
    std::swap(slot, cursor);
}

void rightClick(ItemStack& slot, ItemStack& cursor) {
    if (cursor.empty()) {
        if (slot.empty()) {
            return;
        }
        // Rounded up, so the smaller half is what stays behind. **`damage` has
        // to travel with it**: on a tool it is wear, and on a stowbox it names
        // which contents the box holds, so rebuilding the stack without it
        // repairs the tool or empties the box - and because both stack to one,
        // that was every right-click on either.
        const int taken = (slot.count + 1) / 2;
        cursor = ItemStack{slot.item, taken, slot.damage};
        slot.count -= taken;
        if (slot.count <= 0) {
            slot = ItemStack{};
        }
        return;
    }

    if (slot.empty() || slot.item == cursor.item) {
        // One into an empty slot always fits, so nothing moving means the slot
        // is full or the two can never merge - and only the second of those
        // swaps, exactly as the left button does.
        if (merge(slot, cursor, 1) > 0 || !neverMerges(slot, cursor)) {
            return;
        }
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
        if (roomFor(*target, cursor) > 0) {
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
        // `roomFor` rather than a comparison of items, because two stowboxes are
        // one item and two different sets of contents.
        if (target->empty() || roomFor(*target, from) <= 0) {
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
    // Asked of the cursor about itself: same item, same damage, so the only
    // thing `roomFor` can answer is how many more of exactly this it takes.
    if (cursor.empty() || roomFor(cursor, cursor) <= 0) {
        return;
    }

    std::vector<ItemStack*> matching;
    matching.reserve(sources.size());
    for (ItemStack* source : sources) {
        // Asked of the cursor because that is the side with the room, and it is
        // the same rule as everywhere else: two stowboxes holding different
        // contents must not be swept into one.
        if (!source->empty() && roomFor(cursor, *source) > 0) {
            matching.push_back(source);
        }
    }
    std::sort(matching.begin(), matching.end(),
              [](const ItemStack* a, const ItemStack* b) { return a->count < b->count; });

    for (ItemStack* source : matching) {
        // Nothing moved means the cursor is full, because every one of these had
        // room a moment ago.
        if (merge(cursor, *source, source->count) <= 0) {
            break;
        }
    }
}

} // namespace game::slots
