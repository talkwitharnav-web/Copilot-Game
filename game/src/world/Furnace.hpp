#pragma once

#include "item/Item.hpp"

namespace game {

/// One furnace's contents and how far through its work it is.
///
/// This is a **block entity**: state belonging to a particular block that is not
/// part of *which block it is*. Water levels and stair facings live in the block
/// id precisely because they are its identity; an inventory is not, and 233
/// spare ids could not hold one anyway.
///
/// Whether a furnace is alight, by contrast, *is* in the id - `Furnace` and
/// `FurnaceLit` are different blocks - because the lit face and the light it
/// casts are properties of the block itself.
struct Furnace {
    ItemStack input;
    ItemStack fuel;
    ItemStack output;

    /// Seconds of fuel left, and what the piece currently burning was worth.
    /// The total is kept so the flame can be drawn as a fraction of its own
    /// fuel rather than of some fixed maximum.
    float burnRemaining = 0.0f;
    float burnTotal = 0.0f;

    /// Seconds the current item has spent cooking.
    float cookElapsed = 0.0f;

    bool burning() const { return burnRemaining > 0.0f; }

    /// How full the flame and the arrow should be drawn, each 0 to 1.
    float burnFraction() const { return burnTotal > 0.0f ? burnRemaining / burnTotal : 0.0f; }
    float cookFraction() const;

    /// Nothing in it and nothing happening, so it can be forgotten.
    bool idle() const {
        return input.empty() && fuel.empty() && output.empty() && burnRemaining <= 0.0f && cookElapsed <= 0.0f;
    }
};

/// Advances one furnace and reports whether it is alight afterwards, which is
/// what decides between the lit and unlit block.
///
/// Fuel is only consumed when there is something worth cooking, so a furnace
/// loaded with coal and nothing else sits cold rather than burning itself out.
bool tickFurnace(Furnace& furnace, float deltaSeconds);

} // namespace game
