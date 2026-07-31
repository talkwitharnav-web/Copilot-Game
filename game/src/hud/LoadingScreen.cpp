#include "hud/LoadingScreen.hpp"

#include "hud/HudPrimitives.hpp"

#include <algorithm>
#include <string>

namespace game::hud {
namespace {

// Smaller is nearer. Kept well under the depths world geometry occupies, so the
// panel hides the world assembling behind it.
constexpr float kPanelDepth = 0.0030f;
constexpr float kFrameDepth = 0.0025f;
constexpr float kTrackDepth = 0.0020f;
constexpr float kFillDepth = 0.0015f;
constexpr float kTextDepth = 0.0010f;

constexpr glm::vec4 kBackground{0.055f, 0.06f, 0.075f, 1.0f};
constexpr glm::vec4 kFrame{0.16f, 0.17f, 0.20f, 1.0f};
constexpr glm::vec4 kTrack{0.10f, 0.11f, 0.13f, 1.0f};
constexpr glm::vec4 kFill{0.45f, 0.68f, 0.38f, 1.0f};
constexpr glm::vec4 kText{0.82f, 0.84f, 0.86f, 1.0f};

constexpr float kBarHalfWidth = 0.40f;
constexpr float kBarHalfHeight = 0.020f;
constexpr float kFrameThickness = 0.005f;
constexpr float kCharHeight = 0.055f;

} // namespace

engine::MeshData makeLoadingScreen(float progress, float aspect) {
    engine::MeshData mesh;
    const float shown = std::clamp(progress, 0.0f, 1.0f);

    // Coordinates are relative to window height, so the half-width needed to
    // cover the screen is the aspect ratio.
    appendQuad(mesh, 0.0f, 0.0f, aspect, 1.0f, kPanelDepth, kBackground, kHudLayer, false);

    appendQuad(mesh, 0.0f, 0.0f, kBarHalfWidth + kFrameThickness, kBarHalfHeight + kFrameThickness, kFrameDepth,
               kFrame, kHudLayer, false);
    appendQuad(mesh, 0.0f, 0.0f, kBarHalfWidth, kBarHalfHeight, kTrackDepth, kTrack, kHudLayer, false);

    // Grown from the left edge rather than the centre.
    if (shown > 0.0f) {
        const float width = kBarHalfWidth * 2.0f * shown;
        appendQuad(mesh, -kBarHalfWidth + width * 0.5f, 0.0f, width * 0.5f, kBarHalfHeight, kFillDepth, kFill,
                   kHudLayer, false);
    }

    const std::string label = "Generating world  " + std::to_string(static_cast<int>(shown * 100.0f + 0.5f)) + "%";
    // Y grows downward, so this sits under the bar.
    appendText(mesh, label, -textWidth(label, kCharHeight) * 0.5f, kBarHalfHeight + 0.075f, kCharHeight, kTextDepth,
               kText);

    return mesh;
}

} // namespace game::hud
