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
    /// The one kind that is about **collecting** rather than speed. Shears cut
    /// a vine no faster than a bare hand does; they are simply the only thing
    /// that hands one back.
    Shears,
};

/// Bare hands. Tiers rise from here, and a block can demand a minimum.
constexpr int kHandTier = 0;
constexpr int kWoodTier = 1;
constexpr int kStoneTier = 2;
constexpr int kIronTier = 3;
constexpr int kDiamondTier = 4;
/// Our name for the reference's dark alloy - see `BlockId::AncientDebris`.
constexpr int kEmberiteTier = 5;

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

/// **`harvestTool`, `harvestTier` and `breakSecondsGrounded` were removed from
/// this door on 2026-08-19 - a sweep with a control found all three had zero
/// callers, and two of them were a second answer to a question `MiningRow`
/// already owns.** Do not re-add one by reflex: `Tool.cpp` records the sweep,
/// the control behind it, and the one-line shape to restore if a translation
/// unit behind this door ever genuinely needs one.

/// Whether breaking it with `item` actually yields its drop.
bool yieldsDrop(BlockId block, ItemId item);
} // namespace game
