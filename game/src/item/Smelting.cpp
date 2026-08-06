#include "item/Smelting.hpp"

#include <array>

namespace game {
namespace {

struct SmeltRule {
    ItemId input;
    ItemStack output;
};

/// Ratios and burn times come from the reference recipe data - see
/// `CRAFTABLE.md`.
constexpr std::array<SmeltRule, 7> kSmelting{{
    {itemForBlock(BlockId::Cobblestone), ItemStack{itemForBlock(BlockId::Stone), 1}},
    {itemForBlock(BlockId::Log), ItemStack{ItemId::Charcoal, 1}},
    {itemForBlock(BlockId::Sand), ItemStack{itemForBlock(BlockId::Glass), 1}},
    {itemForBlock(BlockId::Stone), ItemStack{itemForBlock(BlockId::SmoothStone), 1}},
    {ItemId::RawIron, ItemStack{ItemId::IronIngot, 1}},
    {ItemId::RawGold, ItemStack{ItemId::GoldIngot, 1}},
    {ItemId::RawCopper, ItemStack{ItemId::CopperIngot, 1}},
}};

struct FuelRule {
    ItemId item;
    float seconds;
};

/// The reference measures fuel in items smelted; at ten seconds each that makes
/// charcoal worth eight, wood one and a half, and a stick a half.
constexpr std::array<FuelRule, 6> kFuels{{
    {ItemId::Coal, 80.0f},
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
