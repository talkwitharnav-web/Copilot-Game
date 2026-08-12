#pragma once

#include "world/Survival.hpp"

#include <engine/render/MeshData.hpp>

#include <algorithm>

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

/// Ten icons a row, the reference's count.
constexpr int kIconsPerRow = 10;

/// A bubble is worth thirty ticks of air, and the reference draws the one that
/// is going as *bursting* for the last two of them:
/// `full = ceil((air - 2) * 10 / 300)` whole bubbles against
/// `ceil(air * 10 / 300)` drawn at all, the difference being the bubble
/// currently being lost. So the pop belongs to the bubble on its way out, not
/// to whichever one happens to be last on the row.
constexpr float kTicksPerBubble = 30.0f;
constexpr float kBurstTicks = 2.0f;
constexpr float kBurstBubbles = kBurstTicks / kTicksPerBubble;

/// `std::ceil` cannot be used in a constant expression, and every number below
/// wants to be so the layout can be proved rather than trusted. Correct over
/// the range this file uses, negatives included, because a cast to `int`
/// truncates toward zero.
constexpr int ceilToInt(float value) {
    const int truncated = static_cast<int>(value);
    return value > static_cast<float>(truncated) ? truncated + 1 : truncated;
}

/// Exactly what the air row will draw, and whether it is drawn at all.
///
/// **The HUD's rebuild trigger reads this too, and that is the point of it
/// being a function rather than arithmetic inside the builder.** A transient
/// element has to ask for the frames it changes on, and it can only do that
/// honestly if "has anything moved" and "what is drawn" are the same question.
/// Comparing a bubble count instead left the row a second and a half late
/// arriving - `ceil(0.999 * 10)` is still ten - and stuck on screen after
/// surfacing, because the count reached ten while the row was still visible and
/// then never moved again.
struct AirRow {
    bool shown = false;
    int full = 0;
    int bursting = 0;

    constexpr bool operator==(const AirRow&) const = default;
};

constexpr AirRow airRow(float airFraction) {
    const float fraction = std::clamp(airFraction, 0.0f, 1.0f);
    const float bubbles = fraction * static_cast<float>(kIconsPerRow);
    const int drawn = std::clamp(ceilToInt(bubbles), 0, kIconsPerRow);

    AirRow row;
    // **Bubbles only exist while you are holding your breath**, which is the
    // reference's own rule - the meter shows while the air supply is under its
    // maximum - and is why a full breath draws nothing rather than ten bubbles.
    row.shown = fraction < 1.0f;
    row.full = std::clamp(ceilToInt(bubbles - kBurstBubbles), 0, drawn);
    row.bursting = drawn - row.full;
    return row;
}

// The formula at five known points, checked rather than trusted. A curve
// written from memory comes out inverted often enough that this project has a
// rule about it, and both sides here are compile-time so this can never rot.
static_assert(airRow(1.0f) == AirRow{false, 10, 0},
              "a full breath draws no row at all");
static_assert(airRow(0.5f) == AirRow{true, 5, 0},
              "half a breath is five whole bubbles and nothing bursting");
static_assert(airRow(0.1f) == AirRow{true, 1, 0},
              "one bubble left and it is whole - the pop is not the last survivor");
static_assert(airRow(1.55f / 15.0f) == AirRow{true, 1, 1},
              "a tenth of a second before a bubble is lost it is drawn bursting, over the ones that stay");
static_assert(airRow(0.0f) == AirRow{true, 0, 0},
              "out of air draws an empty row, still on screen");

/// The hearts, drumsticks and bubbles above the hotbar.
///
/// **Half-units, not whole ones.** Health and food are both counted in halves
/// in the reference, and drawing ten icons from twenty points is what makes a
/// single point of damage visible at all.
engine::MeshData makeStatusBars(const StatusValues& values);

} // namespace game::hud
