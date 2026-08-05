#include "world/Furnace.hpp"

#include "item/Smelting.hpp"

#include <algorithm>

namespace game {
namespace {

/// Whether what is in the input can actually become what is in the output right
/// now. Both halves matter: an unsmeltable input and a full output slot are the
/// same answer, and neither should light the fire.
bool canCook(const Furnace& furnace, ItemStack& wanted) {
    if (furnace.input.empty()) {
        return false;
    }
    wanted = smeltResult(furnace.input.item);
    if (wanted.empty()) {
        return false;
    }
    if (furnace.output.empty()) {
        return true;
    }
    return furnace.output.item == wanted.item && furnace.output.space() >= wanted.count;
}

} // namespace

float Furnace::cookFraction() const {
    return std::clamp(cookElapsed / kSmeltSeconds, 0.0f, 1.0f);
}

bool tickFurnace(Furnace& furnace, float deltaSeconds) {
    ItemStack wanted;
    const bool ready = canCook(furnace, wanted);

    if (furnace.burnRemaining > 0.0f) {
        furnace.burnRemaining = std::max(0.0f, furnace.burnRemaining - deltaSeconds);
    }

    // A fresh piece of fuel is only lit when there is work for it. Burning
    // through a stack of charcoal in an empty furnace would be the kind of
    // silent loss that is very hard to notice.
    if (furnace.burnRemaining <= 0.0f && ready && !furnace.fuel.empty()) {
        const float seconds = fuelBurnSeconds(furnace.fuel.item);
        if (seconds > 0.0f) {
            furnace.burnTotal = seconds;
            furnace.burnRemaining = seconds;
            if (--furnace.fuel.count <= 0) {
                furnace.fuel = ItemStack{};
            }
        }
    }

    if (furnace.burnRemaining > 0.0f && ready) {
        furnace.cookElapsed += deltaSeconds;
        if (furnace.cookElapsed >= kSmeltSeconds) {
            furnace.cookElapsed -= kSmeltSeconds;
            if (furnace.output.empty()) {
                furnace.output = wanted;
            } else {
                furnace.output.count += wanted.count;
            }
            if (--furnace.input.count <= 0) {
                furnace.input = ItemStack{};
            }
        }
    } else {
        // Progress slides back rather than snapping to zero, so pulling an item
        // out for a moment does not throw away all of its cooking.
        furnace.cookElapsed = std::max(0.0f, furnace.cookElapsed - deltaSeconds * 2.0f);
    }

    return furnace.burnRemaining > 0.0f;
}

} // namespace game
