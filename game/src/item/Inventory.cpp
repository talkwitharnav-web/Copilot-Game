#include "item/Inventory.hpp"

#include <algorithm>

namespace game {

int Inventory::add(ItemId item, int count) {
    if (item == ItemId::None || count <= 0) {
        return 0;
    }

    // Matching stacks first. Opening a fresh slot while a partial one exists is
    // how an inventory ends up holding the same thing four times over.
    for (ItemStack& stack : m_slots) {
        if (count <= 0) {
            break;
        }
        if (!stack.empty() && stack.item == item) {
            const int moved = std::min(count, stack.space());
            stack.count += moved;
            count -= moved;
        }
    }

    for (ItemStack& stack : m_slots) {
        if (count <= 0) {
            break;
        }
        if (stack.empty()) {
            stack.item = item;
            stack.count = std::min(count, maxStackFor(item));
            count -= stack.count;
        }
    }

    return count;
}

bool Inventory::hasRoomFor(ItemId item, int count) const {
    if (item == ItemId::None || count <= 0) {
        return true;
    }

    int room = 0;
    for (const ItemStack& stack : m_slots) {
        room += stack.empty() ? maxStackFor(item) : (stack.item == item ? stack.space() : 0);
        if (room >= count) {
            return true;
        }
    }
    return false;
}

void Inventory::consumeOne(std::size_t index) {
    ItemStack& stack = m_slots[index];
    if (stack.empty()) {
        return;
    }
    if (--stack.count <= 0) {
        stack = ItemStack{};
    }
}

int Inventory::count(ItemId item) const {
    int held = 0;
    for (const ItemStack& stack : m_slots) {
        if (!stack.empty() && stack.item == item) {
            held += stack.count;
        }
    }
    return held;
}

int Inventory::consume(ItemId item, int wanted) {
    int taken = 0;
    for (ItemStack& stack : m_slots) {
        if (taken >= wanted) {
            break;
        }
        if (stack.empty() || stack.item != item) {
            continue;
        }
        const int from = std::min(stack.count, wanted - taken);
        stack.count -= from;
        taken += from;
        if (stack.count <= 0) {
            stack = ItemStack{};
        }
    }
    return taken;
}

} // namespace game
