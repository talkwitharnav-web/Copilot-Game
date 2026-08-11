#pragma once

#include "world/Survival.hpp"

#include <engine/render/MeshData.hpp>

namespace game::hud {

/// What the survival bars are showing this frame.
///
/// A plain snapshot rather than a reference to the player, so the builder stays
/// a pure function of what it is given - the same property `InventoryScreen`'s
/// `build` has, and the reason either can be tested without a world.
struct StatusValues {
    // **The survival table's own maxima, not a second copy of twenty.** Nothing
    // assigns these, so whatever is written here is what ships - and a bar
    // drawn against a stale maximum reports the wrong fraction of a full one.
    int health = survival::kMaxHealth;
    int maxHealth = survival::kMaxHealth;
    int food = survival::kMaxFood;
    int maxFood = survival::kMaxFood;
    /// Air left, as a fraction. Anything at or above 1 hides the bar entirely,
    /// which is the reference's behaviour: bubbles only appear once you are
    /// actually holding your breath.
    float airFraction = 1.0f;
    /// Non-zero while the last hit is still flashing, which makes the hearts
    /// blink rather than merely shrink.
    float hurtFlash = 0.0f;
};

/// The hearts, drumsticks and bubbles above the hotbar.
///
/// **Half-units, not whole ones.** Health and food are both counted in halves
/// in the reference, and drawing ten icons from twenty points is what makes a
/// single point of damage visible at all.
engine::MeshData makeStatusBars(const StatusValues& values);

} // namespace game::hud
