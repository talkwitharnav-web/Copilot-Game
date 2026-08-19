#include "hud/StatusBars.hpp"

#include "hud/Hotbar.hpp"
#include "hud/HudPrimitives.hpp"
#include "world/Fluid.hpp"

#include <algorithm>

namespace game::hud {
namespace {

/// The status strip on the sheet: ten 9x9 icons on a 10 px pitch, written by
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
///
/// **Append only.** The same order is written down in three places - this
/// enum, `New-StatusStrip` and `$statusFiles` in `tools/make-reference-hud.ps1`
/// - so inserting into the middle mistextures every icon after the insertion
/// and nothing warns. The gold pair is at the end for that reason rather than
/// beside the red hearts it belongs with.
enum StatusIcon {
    HeartEmpty = 0,
    HeartFull,
    HeartHalf,
    FoodEmpty,
    FoodFull,
    FoodHalf,
    AirFull,
    AirBursting,
    AbsorbFull,
    AbsorbHalf,
};

/// One icon's on-screen size.
///
/// **Derived from the hotbar, in the reference's own pixels**, rather than
/// picked to look right on its own. The reference draws a 182-wide bar, ten 9x9
/// icons on an 8 px pitch, so a row covers 81 of those 182 and the two rows
/// together span the whole bar with a gap down the middle. Anchoring to
/// `hud::kArtPixel` - which is that same 182 - is what keeps the ends of the
/// hearts on the ends of the hotbar at any future slot size.
///
/// The previous values were hand-picked for whole pixels at 720p and came out
/// under a third of this, which read as a small cluster floating in the middle
/// of the bar.
///
/// **This file used to define its own art pixel**, identical in derivation to
/// `hud::kArtPixel` and one of the two that were live at once. `UI.md` R1: one
/// art pixel, in `HudPrimitives.hpp`, anchored to the hotbar.
/// `assets/ui/ui-atlas.json`: every icon in this strip is 9x9 in the reference -
/// `hud/heart/*`, `hud/food_*` and `hud/air` alike - and the bar they sit over
/// is `hud/hotbar`, 182x22.
constexpr float kReferenceIconSize = 9.0f;
constexpr float kReferenceIconPitch = 8.0f;
constexpr float kReferenceRowPitch = 10.0f;

constexpr float kIconSize = kReferenceIconSize * kArtPixel;
constexpr float kIconHalf = kIconSize * 0.5f;
constexpr float kIconStep = kReferenceIconPitch * kArtPixel;

/// The reference's own selected-cell overhang, **measured rather than chosen**.
///
/// `assets/ui/ui-atlas.json`: `hud/hotbar` is 182x22 and `hud/hotbar_selection`
/// is 24x23 - the ring is one reference pixel taller than the bar it rings, and
/// that pixel is the whole reason a row stacked on `kHotbarTopY` can be eaten
/// by it. The reference puts all of it above the bar; our sheet scales the cell
/// about its centre instead, so ours is split top and bottom and this is a
/// **lower bound rather than a match**.
constexpr float kReferenceHotbarHeight = 22.0f;
constexpr float kReferenceSelectionHeight = 23.0f;
constexpr float kReferenceSelectionOverhang =
    (kReferenceSelectionHeight - kReferenceHotbarHeight) * kArtPixel;

/// Stacked upward from the hotbar's top edge, **clear of the selected cell's
/// oversized frame** rather than of the bar's nominal top.
///
/// One reference pixel above `kHotbarTopY` was one pixel above the wrong line:
/// the selected cell is drawn 25/21 the size of its neighbours and reaches
/// above the row, and it is drawn *nearer* than the hearts, so the leftmost
/// heart lost its bottom edge whenever slot 1 was selected. Nothing warned,
/// because both numbers were right about the thing each was measuring.
constexpr float kRowClearance = kHotbarSelectedOverhang + kArtPixel;
constexpr float kHealthRowY = kHotbarTopY - kRowClearance - kIconHalf;
constexpr float kFoodRowY = kHealthRowY;

/// The line above health and hunger, and **two rows share it** - bubbles on the
/// right, absorption's gold hearts on the left.
///
/// One constant because it is one line in the reference too: the row above the
/// hearts is where armour goes on the left and air on the right, and absorption
/// lands there because the reference draws its hearts as a continuation of the
/// same run of ten-to-a-row containers, so with health already filling its own
/// ten they wrap onto the line above. `UI.md` §6.3 has the armour row wanting
/// the same line whenever armour stops being inert.
constexpr float kUpperRowY = kHealthRowY - kReferenceRowPitch * kArtPixel;
constexpr float kAirRowY = kUpperRowY;
constexpr float kAbsorptionRowY = kUpperRowY;

/// The outermost icon of each row sits flush with the end of the hotbar, which
/// is what makes the two rows together measure the bar.
constexpr float kRowOuterX = kHotbarHalfWidth - kIconHalf;

// What this layout actually claims, checked rather than trusted. All of these
// are things a future change to `kHotbarSlotSize` or to the reference pixel
// numbers could quietly break, and none of them fails loudly on screen - they
// just look slightly wrong in a way nobody can name.
//
// Reduces to 91 - 9 - 9*8 > 0 in reference pixels: half a hotbar against one
// icon and nine gaps. Two measured numbers, not one derived from the other.
//
// **It guards both lines, because both are laid out the same way** - health
// against hunger below, absorption against air above, every one of them started
// at its own end of the bar and stepped inward by `kIconStep`. A full row on
// each side is the worst case for either line.
static_assert(kRowOuterX - static_cast<float>(kIconsPerRow - 1) * kIconStep - kIconHalf > 0.0f,
              "the two rows must leave a gap down the middle rather than growing into each other");

// **Independent of the layout below it**: both sides come from art, neither
// from `kRowClearance`. Our sheet's selected cell is 25 texels to an ordinary
// 21, and this says that ratio still overhangs the bar by at least as much as
// the reference's measured 23-against-22 does.
//
// The single edit that breaks it is bringing the sheet's selected cell down to
// 22 texels or fewer in `tools/make-hud-sheet.ps1` and `kHotbarSelectedOverhang`
// with it - at which point the clearance below is guarding against a frame that
// no longer reaches, and the next thing that *does* reach goes unnoticed.
static_assert(kHotbarSelectedOverhang >= kReferenceSelectionOverhang,
              "the selected cell must overhang the hotbar by at least the reference's own "
              "measured amount, or the row clearance is clearing nothing");

// **What this proves, and no more.** `kHealthRowY` is *defined* as
// `kHotbarTopY - kRowClearance - kIconHalf`, so any comparison of the row's
// position against the frame's line collapses to exactly the line below - this
// is a check on the **definition of `kRowClearance`**, not a proof that the
// hearts clear the frame, and there is no measurement of the row's position
// that the row is not itself derived from, so no such proof is available here.
//
// Written in the form it reduces to rather than laundered through
// `kHealthRowY`, because the laundered form reads like a layout proof and is
// not one. That was the old form's defect too - it compared
// `kHealthRowY + kIconHalf` against `kHotbarTopY`, reducing to
// `kRowClearance > 0`, and it passed happily while the hearts sat under the
// frame. Rewriting it against the frame's line only moved the circularity: it
// then reduced to `kArtPixel > 0`. **Eleven asserts in this project have
// already compared one side of a derivation against itself**; two of them were
// this one.
//
// The single edit that breaks it is dropping the `+ kArtPixel` from
// `kRowClearance`, leaving the hearts resting exactly on the frame's line.
static_assert(kRowClearance > kHotbarSelectedOverhang,
              "kRowClearance must keep the overhang term it exists for, and leave room past it");

// Reduces to `kReferenceIconSize < kReferenceRowPitch`, 9 < 10 - the icon's
// measured height against the reference's row spacing. Two independent numbers,
// so unlike the pair above this one is a real statement about the art.
static_assert(kAirRowY + kIconHalf < kHealthRowY - kIconHalf,
              "the air row must sit clear above the hunger row");
static_assert(kIconsPerRow * kTicksPerBubble * fluid::kTickSeconds == fluid::kAirSeconds,
              "a row of ten bubbles worth thirty ticks each is only the air clock if the two "
              "agree, and the bursting window is measured in those ticks");

/// Nearer than the hotbar's frame so nothing can swallow a heart.
///
/// Not a depth test: the UI pass has no depth attachment at all (`Renderer.cpp`,
/// `recordUiPass`). This is the key its stable sort runs on, far first, so a
/// smaller number here simply means "drawn later, on top".
constexpr float kIconDepth = 0.00098f;

void appendIcon(engine::MeshData& mesh, int icon, float centreX, float centreY) {
    appendSprite(mesh, centreX, centreY, kIconHalf, kIconHalf, kIconDepth,
                 glm::vec2{static_cast<float>(icon) * kIconPitch, kStripTop},
                 glm::vec2{kIconTexels, kIconTexels}, kSheetSize);
}

/// One row of ten icons drawn from a value counted in halves.
///
/// `firstX` is the centre of container 0 and `step` the distance to the next.
/// `maxValue` is what the row has room for, in the same halves as `value`, and
/// is what decides how many containers get drawn: twenty for a fixed bar,
/// **the pool rounded up to a whole heart** for absorption, which has no
/// maximum of its own and draws only what it is currently holding.
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
    const int containers = rowContainers(maxValue);
    // **`value` is clamped here because nothing upstream promises it was.**
    // `loadPlayer` returns health, food and saturation unbounded on purpose -
    // `Survival.hpp` owns those bounds and `Main.cpp` clamps against them - so
    // a corrupt save reaches this function with whatever is on disk, and a
    // health read back as NaN casts to `INT_MIN`.
    //
    // Unclamped, `value - i * 2` then **overflows on the second container** and
    // wraps positive, so the most corrupt value a save can carry drew *nine
    // full hearts* rather than none: signed overflow, and a bar that lies in
    // the direction that gets you killed. The row count was already defended
    // (`rowContainers`); the fill was not, which is the CLAUDE.md #14 shape
    // inside a single function. Found 2026-08-19 by a probe feeding both
    // integer extremes through this arithmetic.
    //
    // The ceiling is the row's own capacity rather than a survival maximum, so
    // the absorption row - which passes its own container count back in as a
    // maximum - stays correct without a second rule.
    const int capacity = containers * 2;
    const int clamped = value < 0 ? 0 : (value > capacity ? capacity : value);
    for (int i = 0; i < containers; ++i) {
        const float x = firstX + static_cast<float>(i) * step;
        // A hit shakes the hearts, which is how the reference tells you the
        // number moved rather than relying on you watching it.
        const float y = rowY + (i % 2 == 0 ? jitter : -jitter);

        appendIcon(mesh, emptyIcon, x, y);

        const int filled = clamped - i * 2;
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

    // **Absorption's gold hearts, on the line above the health row.**
    // "Absorption adds 4 additional health points per level to the player,
    // displayed as yellow hearts above the normal health bar ... the absorption
    // health points are depleted first" (`minecraft.wiki/w/Absorption`). They
    // are laid out exactly like the hearts below them - outermost icon flush
    // with the end of the hotbar, stepping inward - because the reference draws
    // them as a continuation of the same run: ten containers to a row, so the
    // eleventh heart onward wraps onto the line above and fills from the left.
    //
    // **An empty container under each one, and only under the ones that exist.**
    // The reference draws a socket for every heart in that run, absorption
    // included, which is why `absorbing_half` is opaque over the same four
    // columns as `half` and needs something behind it. What it does *not* draw
    // is a row of ten empty gold sockets for padding nobody has - so nothing at
    // all when the pool is empty, the same rule the bubble row follows.
    //
    // **`values.absorption` is the live balance**, and the row would be a lie
    // if it were read off `effects::absorptionPoints` instead: that is what a
    // grant is worth, and it stays at sixteen for the whole two minutes an
    // enchanted apple runs however much of the pool a creeper has taken.
    //
    // The hearts shake with the health row because they are part of the same
    // bar; `ceilToInt` matches `absorptionHearts` so a stray odd point cannot
    // be a container the row never fills.
    const int absorbHearts = absorptionHearts(values.absorption);
    if (absorbHearts > 0) {
        appendHalvesRow(mesh, ceilToInt(values.absorption), absorbHearts * 2, kAbsorptionRowY,
                        -kRowOuterX, kIconStep, HeartEmpty, AbsorbFull, AbsorbHalf, jitter);
    }

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
