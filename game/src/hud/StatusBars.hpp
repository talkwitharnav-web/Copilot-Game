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
    /// Absorption points held **right now**, in the same half-heart unit as
    /// `health`.
    ///
    /// **This is `Player::absorption`, never `effects::absorptionPoints`.** The
    /// function returns what a *grant* is worth and the field is the live
    /// balance, so a row read off the function would show eight full gold
    /// hearts for the whole two minutes an enchanted apple runs, however much
    /// of the pool had already been spent. Same bug shape as the air row read
    /// off a bubble count: a value derived somewhere other than the one place
    /// that owns it.
    float absorption = 0.0f;
    /// Non-zero while the last hit is still flashing, which makes the hearts
    /// blink rather than merely shrink.
    float hurtFlash = 0.0f;
};

/// Ten icons a row, the reference's count.
constexpr int kIconsPerRow = 10;

/// How many containers a row draws, for a maximum counted in halves.
///
/// **A bar builder must not assume its input is already clamped, and this is
/// where that is proved rather than hoped** (2026-08-19). `WorldStore::loadPlayer`
/// returns health, food and saturation **unbounded**, deliberately -
/// `Survival.hpp` owns those bounds and `Main.cpp` clamps against them - so a
/// corrupt, truncated or hand-edited save reaches `makeStatusBars` carrying
/// whatever was on disk. The clamp was previously spelt half here and half in
/// the loop's own condition, which is safe and unprovable: nothing could state
/// the property, so nothing could keep it.
constexpr int rowContainers(int maxValue) {
    return std::clamp(maxValue / 2, 1, kIconsPerRow);
}

// A relation against the table that owns the maxima, plus three absolute
// anchors that no amount of coherent rescaling can satisfy. The first line is
// the useful one: it says the reference's ten containers and the survival
// table's twenty points are the same statement, so **raising `kMaxHealth`
// without deciding what a second row looks like fails the build** instead of
// silently drawing ten hearts for thirty points.
static_assert(rowContainers(survival::kMaxHealth) == kIconsPerRow &&
                  rowContainers(survival::kMaxFood) == kIconsPerRow &&
                  rowContainers(0) == 1 && rowContainers(-40) == 1 &&
                  rowContainers(4000) == kIconsPerRow,
              "a row is ten containers at the survival maxima and cannot be talked past them by "
              "a save carrying a nonsense maximum");

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

/// How many gold hearts the absorption row draws, and whether it draws at all.
///
/// **A function for the same reason `airRow` is one**: the HUD's rebuild
/// trigger has to compare what was last *drawn*, and it can only do that
/// honestly if "has the row moved" and "what does the row show" are the same
/// question. A pool compared as a float would dirty the HUD on every point of a
/// hit that changed no icon, and a pool compared as a heart count worked out at
/// the call site is the air row's bug in another outfit.
///
/// **Ceiling, not rounding**, which is the reference's own `ceil(absorption/2)`
/// - a single point left is half a heart still on screen, not nothing. Clamped
/// to a row because the icons wrap nowhere: our grants stop at Absorption IV's
/// sixteen points, which is eight of the ten, but a level from anywhere else
/// must not run off the end of the hotbar.
constexpr int absorptionHearts(float points) {
    return std::clamp(ceilToInt(points * 0.5f), 0, kIconsPerRow);
}

// The two grants that exist, measured through the function that owns what a
// grant is worth rather than against 4 and 16 written here a second time. Both
// sides are compile-time, so this can never rot.
//
// The single edit that fails it is truncating instead of ceiling in
// `absorptionHearts` - `absorptionHearts(1.0f)` then returns 0 and the last
// point of padding is invisible, which is the state a spent pool spends most of
// its life in.
static_assert(absorptionHearts(0.0f) == 0 && absorptionHearts(1.0f) == 1 &&
                  absorptionHearts(effects::absorptionPoints(
                      effects::running(effects::Effect::Absorption, 0))) == 2 &&
                  absorptionHearts(effects::absorptionPoints(
                      effects::running(effects::Effect::Absorption, 3))) == 8 &&
                  absorptionHearts(999.0f) == kIconsPerRow,
              "no pool draws no row, a golden apple draws two gold hearts and an enchanted one "
              "eight, an odd point left still draws half a heart, and nothing overruns the row");

/// The hearts, drumsticks, bubbles and absorption's gold hearts above the
/// hotbar.
///
/// **Half-units, not whole ones.** Health and food are both counted in halves
/// in the reference, and drawing ten icons from twenty points is what makes a
/// single point of damage visible at all.
engine::MeshData makeStatusBars(const StatusValues& values);

} // namespace game::hud
