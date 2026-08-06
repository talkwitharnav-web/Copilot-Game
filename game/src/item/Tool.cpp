#include "item/Tool.hpp"

#include <algorithm>
#include <array>

namespace game {
namespace {

struct ToolRule {
    ItemId item;
    ToolProperties properties;
};

/// Stone is roughly twice the tool wood is, in both speed and how long it
/// lasts, which is what makes the upgrade worth making.
constexpr std::array<ToolRule, 10> kTools{{
    {ItemId::WoodenPickaxe, {ToolKind::Pickaxe, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenAxe, {ToolKind::Axe, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenShovel, {ToolKind::Shovel, kWoodTier, 2.0f, 60}},
    {ItemId::WoodenSword, {ToolKind::Sword, kWoodTier, 1.5f, 60}},
    {ItemId::WoodenHoe, {ToolKind::Hoe, kWoodTier, 1.5f, 60}},
    {ItemId::StonePickaxe, {ToolKind::Pickaxe, kStoneTier, 4.0f, 132}},
    {ItemId::StoneAxe, {ToolKind::Axe, kStoneTier, 4.0f, 132}},
    {ItemId::StoneShovel, {ToolKind::Shovel, kStoneTier, 4.0f, 132}},
    {ItemId::StoneSword, {ToolKind::Sword, kStoneTier, 2.0f, 132}},
    {ItemId::StoneHoe, {ToolKind::Hoe, kStoneTier, 2.0f, 132}},
}};

} // namespace

ToolProperties toolFor(ItemId item) {
    for (const ToolRule& rule : kTools) {
        if (rule.item == item) {
            return rule.properties;
        }
    }
    return ToolProperties{};
}

float blockHardness(BlockId block) {
    if (isWater(block) || block == BlockId::Air) {
        return 0.0f;
    }
    if (blockShape(block) == BlockShape::Cross) {
        // Plants and torches come away instantly, whatever you are holding.
        return 0.0f;
    }
    if (isStairs(block) || isSlab(block)) {
        return 1.5f;
    }
    if (isFurnace(block)) {
        return 3.5f;
    }
    switch (block) {
    case BlockId::Sand:
    case BlockId::Gravel:
    case BlockId::Snow:
        return 0.5f;
    case BlockId::Dirt:
    case BlockId::Grass:
        return 0.6f;
    case BlockId::Leaves:
        return 0.2f;
    case BlockId::Planks:
    case BlockId::PlanksFence:
    case BlockId::CraftingTable:
        return 2.0f;
    case BlockId::Log:
        return 2.0f;
    case BlockId::Stone:
        return 1.5f;
    case BlockId::Cobblestone:
        return 2.0f;
    case BlockId::Bricks:
        return 2.0f;
    case BlockId::Glowstone:
        return 0.3f;
    case BlockId::Glass:
        return 0.3f;
    case BlockId::Clay:
        return 0.6f;
    case BlockId::Sandstone:
        return 0.8f;
    case BlockId::Bookshelf:
        return 1.5f;
    case BlockId::Andesite:
    case BlockId::Diorite:
    case BlockId::Granite:
    case BlockId::StoneBricks:
        return 1.5f;
    case BlockId::SmoothStone:
    case BlockId::MossyCobblestone:
        return 2.0f;
    case BlockId::Obsidian:
        // Deliberately punishing. At a stone pickaxe's speed this is about
        // nineteen seconds, which is the point of the block.
        return 50.0f;
    case BlockId::PackedIce:
        return 0.5f;
    case BlockId::Terracotta:
        return 1.25f;
    case BlockId::CoalOre:
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::LapisOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
    case BlockId::Deepslate:
        return 3.0f;
    case BlockId::Bedrock:
        // The reference's own value for "never". Nothing here treats a block as
        // unbreakable, so an absurd hardness is what enforces it, and creative
        // still ignores it - which is correct, it is a builder's tool.
        return 3600.0f;
    default:
        return 1.0f;
    }
}

ToolKind harvestTool(BlockId block) {
    if (isStairs(block) || isSlab(block) || isFurnace(block)) {
        return ToolKind::Pickaxe;
    }
    switch (block) {
    case BlockId::Stone:
    case BlockId::Cobblestone:
    case BlockId::Bricks:
    case BlockId::Andesite:
    case BlockId::Diorite:
    case BlockId::Granite:
    case BlockId::SmoothStone:
    case BlockId::StoneBricks:
    case BlockId::MossyCobblestone:
    case BlockId::Obsidian:
    case BlockId::Sandstone:
        return ToolKind::Pickaxe;
    case BlockId::CoalOre:
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::LapisOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
    case BlockId::Deepslate:
    case BlockId::Terracotta:
    case BlockId::PackedIce:
        return ToolKind::Pickaxe;
    case BlockId::Log:
    case BlockId::Planks:
    case BlockId::PlanksFence:
    case BlockId::CraftingTable:
    case BlockId::Bookshelf:
        return ToolKind::Axe;
    case BlockId::Dirt:
    case BlockId::Grass:
    case BlockId::Sand:
    case BlockId::Gravel:
    case BlockId::Snow:
    case BlockId::Clay:
        return ToolKind::Shovel;
    default:
        return ToolKind::None;
    }
}

int harvestTier(BlockId block) {
    // Only stone-family blocks withhold their drop, which is what makes the
    // first pickaxe the thing that opens the game up. Wood and soil always give
    // something, or a fresh world would be unplayable.
    if (isStairs(block) || isSlab(block) || isFurnace(block)) {
        return kWoodTier;
    }
    switch (block) {
    case BlockId::Stone:
    case BlockId::Cobblestone:
    case BlockId::Bricks:
    case BlockId::Andesite:
    case BlockId::Diorite:
    case BlockId::Granite:
    case BlockId::SmoothStone:
    case BlockId::StoneBricks:
    case BlockId::MossyCobblestone:
    case BlockId::Sandstone:
        return kWoodTier;
    case BlockId::CoalOre:
    case BlockId::Deepslate:
    case BlockId::Terracotta:
        return kWoodTier;
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::LapisOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
    case BlockId::Obsidian:
        // The hardest thing a stone pickaxe can still take home. There is no
        // higher tier yet, so gating any of these above it would make them
        // unobtainable rather than aspirational.
        return kStoneTier;
    default:
        return kHandTier;
    }
}

float breakSeconds(BlockId block, ItemId item) {
    const float hardness = blockHardness(block);
    if (hardness <= 0.0f) {
        return 0.0f;
    }

    const ToolProperties tool = toolFor(item);
    const ToolKind wanted = harvestTool(block);
    // The right tool speeds it up; the wrong one is no worse than bare hands.
    const float speed = (wanted != ToolKind::None && tool.kind == wanted) ? tool.speed : 1.0f;

    // The reference's own pair, and the hardness table above is already its:
    // 1.5x with the kit a block demands and 5x without. The ratio is what makes
    // a tool a decision rather than a convenience - a block still comes away
    // bare-handed, it just takes long enough to be worth avoiding.
    const float penalty = tool.tier >= harvestTier(block) ? 1.5f : 5.0f;
    return hardness * penalty / std::max(0.1f, speed);
}

bool yieldsDrop(BlockId block, ItemId item) {
    return toolFor(item).tier >= harvestTier(block);
}

} // namespace game
