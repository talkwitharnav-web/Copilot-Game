#include "item/Smelting.hpp"

#include <array>

namespace game {
namespace {

struct SmeltRule {
    ItemId input;
    ItemStack output;
};

/// Ratios and burn times come from the reference recipe data - see
/// `CRAFTABLE.md`. Glass is absent because sand needs a transparent block we do
/// not have yet, not because the recipe is unknown.
constexpr std::array<SmeltRule, 2> kSmelting{{
    {itemForBlock(BlockId::Cobblestone), ItemStack{itemForBlock(BlockId::Stone), 1}},
    {itemForBlock(BlockId::Log), ItemStack{ItemId::Charcoal, 1}},
}};

struct FuelRule {
    ItemId item;
    float seconds;
};

/// The reference measures fuel in items smelted; at ten seconds each that makes
/// charcoal worth eight, wood one and a half, and a stick a half.
constexpr std::array<FuelRule, 5> kFuels{{
    {ItemId::Charcoal, 80.0f},
    {itemForBlock(BlockId::Log), 15.0f},
    {itemForBlock(BlockId::Planks), 15.0f},
    {itemForBlock(BlockId::PlanksFence), 15.0f},
    {ItemId::Stick, 5.0f},
}};

} // namespace

ItemStack smeltResult(ItemId input) {
    for (const SmeltRule& rule : kSmelting) {
        if (rule.input == input) {
            return rule.output;
        }
    }
    return ItemStack{};
}

float fuelBurnSeconds(ItemId item) {
    for (const FuelRule& rule : kFuels) {
        if (rule.item == item) {
            return rule.seconds;
        }
    }
    return 0.0f;
}

} // namespace game
