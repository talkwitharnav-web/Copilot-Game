#include "hud/StatusBars.hpp"

#include "hud/HudPrimitives.hpp"

#include <algorithm>
#include <cmath>

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

/// One icon's on-screen half-size, as a fraction of window height. Chosen so a
/// nine-texel sprite lands on whole pixels at 720p, the same rule the font
/// follows - the sampler is nearest-neighbour, so any other ratio doubles some
/// rows and not others.
constexpr float kIconHalf = 0.0125f;
constexpr float kIconStep = kIconHalf * 2.0f;

/// Ten icons a row, centred on the same axis the hotbar uses.
constexpr int kIconsPerRow = 10;

/// Sits directly above the hotbar, which is centred at 0.888 and a little over
/// 0.15 tall. Positive Y is down.
constexpr float kHealthRowY = 0.784f;
constexpr float kFoodRowY = 0.784f;
constexpr float kAirRowY = 0.744f;

/// Nearer than the hotbar's frame so nothing can swallow a heart, and far
/// enough from every other band that a blended fragment writing depth cannot
/// reach across into one of them.
constexpr float kIconDepth = 0.00098f;

/// How far out from the centre each row starts. Health runs leftward from the
/// middle and food rightward, mirroring each other exactly as the reference
/// lays them out.
constexpr float kRowGap = 0.014f;

void appendIcon(engine::MeshData& mesh, int icon, float centreX, float centreY) {
    appendSprite(mesh, centreX, centreY, kIconHalf, kIconHalf, kIconDepth,
                 glm::vec2{static_cast<float>(icon) * kIconPitch, kStripTop},
                 glm::vec2{kIconTexels, kIconTexels}, kSheetSize);
}

/// One row of ten icons drawn from a value counted in halves.
///
/// `direction` is -1 for a row that grows leftward from the centre and +1 for
/// one that grows rightward. The **empty containers are drawn first and the
/// full ones over them**, which is the reference's own arrangement and the
/// reason a half icon needs no third sprite blended into a background.
void appendHalvesRow(engine::MeshData& mesh, int value, int maxValue, float rowY, float direction,
                     int emptyIcon, int fullIcon, int halfIcon, float jitter) {
    const int containers = std::max(1, maxValue / 2);
    for (int i = 0; i < containers && i < kIconsPerRow; ++i) {
        const float offset = kRowGap + (static_cast<float>(i) + 0.5f) * kIconStep;
        const float x = direction * offset;
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

    appendHalvesRow(mesh, values.health, values.maxHealth, kHealthRowY, -1.0f, HeartEmpty,
                    HeartFull, HeartHalf, jitter);
    appendHalvesRow(mesh, values.food, values.maxFood, kFoodRowY, 1.0f, FoodEmpty, FoodFull,
                    FoodHalf, 0.0f);

    // **Bubbles only exist while you are holding your breath**, which is why a
    // full meter draws nothing at all rather than ten full bubbles.
    if (values.airFraction < 1.0f) {
        const int bubbles =
            static_cast<int>(std::ceil(std::clamp(values.airFraction, 0.0f, 1.0f) *
                                       static_cast<float>(kIconsPerRow)));
        for (int i = 0; i < bubbles; ++i) {
            const float offset = kRowGap + (static_cast<float>(i) + 0.5f) * kIconStep;
            // The last one left is the one bursting, which is the only warning
            // that the next thing to happen is damage. The whole loop already
            // runs only while the meter is below full, so that is not asked
            // again here.
            const int icon = (i == bubbles - 1) ? AirBursting : AirFull;
            appendIcon(mesh, icon, offset, kAirRowY);
        }
    }

    return mesh;
}

} // namespace game::hud
