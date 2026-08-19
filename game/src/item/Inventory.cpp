#include "item/Inventory.hpp"

#include <algorithm>

namespace game {

void Inventory::consumeOne(std::size_t index) {
    // **Checked, not trusted.** Every caller passes a slot index that came from
    // a hit test on the screen, and `std::array::operator[]` on a bad one is not
    // a crash but a quiet write past thirty-six stacks.
    if (index >= m_slots.size()) {
        return;
    }
    ItemStack& stack = m_slots[index];
    if (stack.empty()) {
        return;
    }
    if (--stack.count <= 0) {
        stack = ItemStack{};
    }
}

int Inventory::count(ItemId item, int damage) const {
    int held = 0;
    for (const ItemStack& stack : m_slots) {
        if (!stack.empty() && stack.item == item && matchesDamage(stack, damage)) {
            held += stack.count;
        }
    }
    return held;
}

int Inventory::consume(ItemId item, int wanted, int damage) {
    int taken = 0;
    for (ItemStack& stack : m_slots) {
        if (taken >= wanted) {
            break;
        }
        if (stack.empty() || stack.item != item || !matchesDamage(stack, damage)) {
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
