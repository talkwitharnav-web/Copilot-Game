#pragma once

#include "item/Item.hpp"

namespace game {

/// Seconds one item spends in the fire. Every recipe takes the same time, which
/// is what the reference does and what lets the progress arrow be a single
/// fraction rather than a per-recipe one.
constexpr float kSmeltSeconds = 10.0f;

/// What `input` turns into, or an empty stack if it does not smelt.
ItemStack smeltResult(ItemId input);

/// How long one of `item` keeps a furnace alight, in seconds. Zero means it is
/// not fuel.
///
/// Kept separate from `smeltResult` because the two are unrelated questions: a
/// log is both an input and a fuel, and plenty of things are exactly one.
float fuelBurnSeconds(ItemId item);

} // namespace game
