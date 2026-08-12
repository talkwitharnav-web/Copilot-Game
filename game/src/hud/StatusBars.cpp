#include "hud/StatusBars.hpp"

#include "hud/Hotbar.hpp"
#include "hud/HudPrimitives.hpp"
#include "world/Fluid.hpp"

#include <algorithm>

namespace game::hud {
namespace {

/// The status strip on the sheet: eight 9x9 icons on a 10 px pitch, written by
/// `New-StatusStrip` in `tools/make-hud-sheet.ps1` and overlaid with the
/// reference's own sprites by `tools/make-reference-hud.ps1`.
///
/// **This offset must follow the sheet**, and a wrong one does not fail - it
/// silently draws whatever happens to sit there, which for a strip at the very
/// bottom is the empty margin. `Main.cpp` checks the PNG header against
/// `kSheetSize`, which is what catches the whole sheet drifting.
constexpr float kStripTop = 1345.0f;
constexpr float kIconPitch = 10.0f;
constexpr float kIconTexels = 9.0f;

/// In the order `New-StatusStrip` writes them.
enum StatusIcon {
    HeartEmpty = 0,
    HeartFull,
    HeartHalf,
    FoodEmpty,
    FoodFull,
    FoodHalf,
    AirFull,
    AirBursting,
};

/// One icon's on-screen size.
///
/// **Derived from the hotbar, in the reference's own pixels**, rather than
/// picked to look right on its own. The reference draws a 182-wide bar, ten 9x9
/// icons on an 8 px pitch, so a row covers 81 of those 182 and the two rows
/// together span the whole bar with a gap down the middle. Anchoring to
/// `kHotbarWidth` is what keeps the ends of the hearts on the ends of the
/// hotbar at any future slot size.
///
/// The previous values were hand-picked for whole pixels at 720p and came out
/// under a third of this, which read as a small cluster floating in the middle
/// of the bar.
constexpr float kReferenceHotbarWidth = 182.0f;
constexpr float kReferenceIconSize = 9.0f;
constexpr float kReferenceIconPitch = 8.0f;
constexpr float kReferenceRowPitch = 10.0f;

constexpr float kPerReferencePixel = kHotbarWidth / kReferenceHotbarWidth;
constexpr float kIconSize = kReferenceIconSize * kPerReferencePixel;
constexpr float kIconHalf = kIconSize * 0.5f;
constexpr float kIconStep = kReferenceIconPitch * kPerReferencePixel;

/// Stacked upward from the hotbar's top edge, one reference pixel clear of it.
constexpr float kRowClearance = kPerReferencePixel;
constexpr float kHealthRowY = kHotbarTopY - kRowClearance - kIconHalf;
constexpr float kFoodRowY = kHealthRowY;
constexpr float kAirRowY = kHealthRowY - kReferenceRowPitch * kPerReferencePixel;

/// The outermost icon of each row sits flush with the end of the hotbar, which
/// is what makes the two rows together measure the bar.
constexpr float kRowOuterX = kHotbarHalfWidth - kIconHalf;

// What this layout actually claims, checked rather than trusted. All three are
// things a future change to `kHotbarSlotSize` or to the reference pixel numbers
// could quietly break, and none of them fails loudly on screen - they just look
// slightly wrong in a way nobody can name.
static_assert(kRowOuterX - static_cast<float>(kIconsPerRow - 1) * kIconStep - kIconHalf > 0.0f,
              "the two rows must leave a gap down the middle rather than growing into each other");
static_assert(kHealthRowY + kIconHalf < kHotbarTopY,
              "the status rows must sit clear above the hotbar, not overlap its frame");
static_assert(kAirRowY + kIconHalf < kHealthRowY - kIconHalf,
              "the air row must sit clear above the hunger row");
static_assert(kIconsPerRow * kTicksPerBubble * fluid::kTickSeconds == fluid::kAirSeconds,
              "a row of ten bubbles worth thirty ticks each is only the air clock if the two "
              "agree, and the bursting window is measured in those ticks");

/// Nearer than the hotbar's frame so nothing can swallow a heart, and far
/// enough from every other band that a blended fragment writing depth cannot
/// reach across into one of them.
constexpr float kIconDepth = 0.00098f;

void appendIcon(engine::MeshData& mesh, int icon, float centreX, float centreY) {
    appendSprite(mesh, centreX, centreY, kIconHalf, kIconHalf, kIconDepth,
                 glm::vec2{static_cast<float>(icon) * kIconPitch, kStripTop},
                 glm::vec2{kIconTexels, kIconTexels}, kSheetSize);
}

/// One row of ten icons drawn from a value counted in halves.
///
/// `firstX` is the centre of container 0 and `step` the distance to the next.
///
/// **Container 0 is the one that empties last.** So a row is laid out starting
/// from the end that should stay full longest: health starts at the hotbar's
/// left end and steps right, hunger starts at the right end and steps left, and
/// both therefore drain inward towards the middle - hearts right to left,
/// hunger left to right, which is the reference's arrangement.
///
/// Passing a direction and letting the row grow from the centre, which is what
/// this did before, gets that exactly backwards for both bars at once.
///
/// The **empty containers are drawn first and the full ones over them**, which
/// is the reference's own arrangement and the reason a half icon needs no third
/// sprite blended into a background.
void appendHalvesRow(engine::MeshData& mesh, int value, int maxValue, float rowY, float firstX,
                     float step, int emptyIcon, int fullIcon, int halfIcon, float jitter) {
    const int containers = std::max(1, maxValue / 2);
    for (int i = 0; i < containers && i < kIconsPerRow; ++i) {
        const float x = firstX + static_cast<float>(i) * step;
        // A hit shakes the hearts, which is how the reference tells you the
        // number moved rather than relying on you watching it.
        const float y = rowY + (i % 2 == 0 ? jitter : -jitter);

        appendIcon(mesh, emptyIcon, x, y);

        const int filled = value - i * 2;
        if (filled >= 2) {
            appendIcon(mesh, fullIcon, x, y);
        } else if (filled == 1) {
            appendIcon(mesh, halfIcon, x, y);
        }
    }
}

} // namespace

engine::MeshData makeStatusBars(const StatusValues& values) {
    engine::MeshData mesh;

    // Only the hearts shake, and only briefly. Driving it off the flash rather
    // than off a counter means it cannot get stuck on.
    const float jitter = values.hurtFlash > 0.0f ? 0.004f : 0.0f;

    appendHalvesRow(mesh, values.health, values.maxHealth, kHealthRowY, -kRowOuterX, kIconStep,
                    HeartEmpty, HeartFull, HeartHalf, jitter);
    appendHalvesRow(mesh, values.food, values.maxFood, kFoodRowY, kRowOuterX, -kIconStep, FoodEmpty,
                    FoodFull, FoodHalf, 0.0f);

    // **Bubbles only exist while you are holding your breath**, which is why a
    // full meter draws nothing at all rather than ten full bubbles.
    const AirRow air = airRow(values.airFraction);
    if (air.shown) {
        for (int i = 0; i < air.full + air.bursting; ++i) {
            // Laid out like the hunger row it sits above, so the two line up:
            // icon 0 at the outer end, stepping inward, so the bubble that
            // empties last is the one furthest from the middle.
            const float offset = kRowOuterX - static_cast<float>(i) * kIconStep;
            // **The bursting ones are drawn after the whole ones**, so what
            // pops is the bubble currently being lost rather than the last one
            // left. Painting the survivor bursting instead - which is what this
            // did - meant the row never showed a bubble going, only a permanent
            // warning sitting on the end of it.
            appendIcon(mesh, i < air.full ? AirFull : AirBursting, offset, kAirRowY);
        }
    }

    return mesh;
}

} // namespace game::hud
