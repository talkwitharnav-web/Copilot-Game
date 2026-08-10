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
constexpr std::array<SmeltRule, 22> kSmelting{{
    {itemForBlock(BlockId::Cobblestone), ItemStack{itemForBlock(BlockId::Stone), 1}},
    {itemForBlock(BlockId::Log), ItemStack{ItemId::Charcoal, 1}},
    {itemForBlock(BlockId::Sand), ItemStack{itemForBlock(BlockId::Glass), 1}},
    {itemForBlock(BlockId::Stone), ItemStack{itemForBlock(BlockId::SmoothStone), 1}},
    {ItemId::RawIron, ItemStack{ItemId::IronIngot, 1}},
    {ItemId::RawGold, ItemStack{ItemId::GoldIngot, 1}},
    {ItemId::RawCopper, ItemStack{ItemId::CopperIngot, 1}},
    {itemForBlock(BlockId::AncientDebris), ItemStack{ItemId::EmberiteScrap, 1}},
    {itemForBlock(BlockId::StoneBricks), ItemStack{itemForBlock(BlockId::CrackedStoneBricks), 1}},
    {itemForBlock(BlockId::Sandstone), ItemStack{itemForBlock(BlockId::SmoothSandstone), 1}},
    {itemForBlock(BlockId::WetSponge), ItemStack{itemForBlock(BlockId::Sponge), 1}},
    // The two raw foods that sit in the appended run rather than the first one.
    // Named rather than folded into `isRawFood`, because that run is not pairs
    // all the way through - the tropical fish is followed by the pufferfish.
    {ItemId::RawRabbit, ItemStack{ItemId::CookedRabbit, 1}},
    {ItemId::RawSalmon, ItemStack{ItemId::CookedSalmon, 1}},
    {itemForBlock(BlockId::Kelp), ItemStack{ItemId::DriedKelp, 1}},
    // The chains the new recipes need a first link for.
    {itemForBlock(BlockId::Clay), ItemStack{itemForBlock(BlockId::Terracotta), 1}},
    {ItemId::ClayBall, ItemStack{ItemId::Brick, 1}},
    {itemForBlock(BlockId::Cactus), ItemStack{static_cast<ItemId>(static_cast<int>(kFirstDye) + 13), 1}},
    {itemForBlock(BlockId::NetherQuartzOre), ItemStack{ItemId::Quartz, 1}},
    {itemForBlock(BlockId::QuartzBlock), ItemStack{itemForBlock(BlockId::SmoothQuartz), 1}},
    {itemForBlock(BlockId::Basalt), ItemStack{itemForBlock(BlockId::SmoothBasalt), 1}},
    {itemForBlock(BlockId::NetherBricks), ItemStack{itemForBlock(BlockId::CrackedNetherBricks), 1}},
    {itemForBlock(BlockId::Netherrack), ItemStack{ItemId::NetherBrickItem, 1}},
}};

struct FuelRule {
    ItemId item;
    float seconds;
};

/// The reference measures fuel in items smelted; at ten seconds each that makes
/// charcoal worth eight, wood one and a half, and a stick a half.
///
/// **Wood is answered by shape below rather than listed here**, because there
/// are now eleven planks, eleven fences and dozens of wooden stairs, slabs and
/// gates - and the reference burns every one of them.
constexpr std::array<FuelRule, 3> kFuels{{
    {ItemId::Coal, 80.0f},
    {ItemId::Charcoal, 80.0f},
    {ItemId::Stick, 5.0f},
}};

} // namespace

ItemStack smeltResult(ItemId input) {
    // Every raw food cooks into the item that follows it, so the pairing lives
    // in the `ItemId` order rather than in five more table rows that could
    // disagree with it.
    if (isRawFood(input)) {
        return ItemStack{cookedForm(input), 1};
    }
    // The sixteen dyed terracottas fire into their glazed forms. Both runs are
    // declared white-first in the same order, so this is one offset rather than
    // sixteen rows.
    if (isBlockItem(input)) {
        const BlockId block = blockForItem(input);
        if (block >= BlockId::WhiteTerracotta && block <= BlockId::BlackTerracotta) {
            return ItemStack{itemForBlock(static_cast<BlockId>(
                                 static_cast<int>(BlockId::WhiteGlazedTerracotta) +
                                 static_cast<int>(block) - static_cast<int>(BlockId::WhiteTerracotta))),
                             1};
        }
    }
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
    // Anything wooden burns. `isFlammable` already forwards a cut shape to the
    // planks it was made of, so a birch fence gate is fuel without being named.
    if (isBlockItem(item)) {
        const BlockId block = blockForItem(item);
        if (isLogBlock(block) || isPlanksBlock(block) ||
            (isShapedBlock(block) && isFlammable(block))) {
            return 15.0f;
        }
    }
    return 0.0f;
}

} // namespace game
