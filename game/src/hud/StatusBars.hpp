#pragma once

#include <engine/render/MeshData.hpp>

namespace game::hud {

/// What the survival bars are showing this frame.
///
/// A plain snapshot rather than a reference to the player, so the builder stays
/// a pure function of what it is given - the same property `InventoryScreen`'s
/// `build` has, and the reason either can be tested without a world.
struct StatusValues {
    int health = 20;
    int maxHealth = 20;
    int food = 20;
    int maxFood = 20;
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
