#pragma once

#include "item/Item.hpp"
#include "world/Block.hpp"

#include <cstdint>

namespace game {

/// What a tool is for. A block names the kind that speeds it up, so the two
/// tables answer each other without either knowing the other's contents.
enum class ToolKind : std::uint8_t {
    None,
    Pickaxe,
    Axe,
    Shovel,
    Sword,
    Hoe,
};

/// Bare hands. Tiers rise from here, and a block can demand a minimum.
constexpr int kHandTier = 0;
constexpr int kWoodTier = 1;
constexpr int kStoneTier = 2;

struct ToolProperties {
    ToolKind kind = ToolKind::None;
    int tier = kHandTier;
    /// How much faster than bare hands this tool works on what it suits.
    float speed = 1.0f;
    /// Blocks it can break before it is used up. Zero means it never wears.
    int durability = 0;
};

ToolProperties toolFor(ItemId item);

/// Seconds to break with bare hands. The scale is arbitrary but the ordering is
/// not: dirt is quick, stone is slow, and bedrock-like things are unbreakable.
float blockHardness(BlockId block);

/// Which tool this block gives way to, and the tier it demands before it will
/// drop anything at all. **That demand is the whole progression** - stone mined
/// by hand yields nothing, so a pickaxe is the gate to everything past wood.
ToolKind harvestTool(BlockId block);
int harvestTier(BlockId block);

/// How long `item` takes to break `block`, in seconds.
float breakSeconds(BlockId block, ItemId item);

/// Whether breaking it with `item` actually yields its drop.
bool yieldsDrop(BlockId block, ItemId item);

} // namespace game
