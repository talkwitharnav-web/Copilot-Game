#include "hud/Crosshair.hpp"

#include "hud/HudPrimitives.hpp"
#include "world/Block.hpp"

#include <glm/glm.hpp>

namespace game {
namespace {

// Units are relative to window height, so the crosshair is the same size on any
// display. The renderer corrects for aspect ratio.
//
// Long and thin on purpose: a reticle marks a point, so anything heavy enough to
// notice as a shape is covering the thing being aimed at. The border is only
// just wide enough to keep it visible against sand and sky.
constexpr float kArmLength = 0.021f;
constexpr float kArmThickness = 0.0013f;
constexpr float kBorder = 0.0006f;

/// Nearer than anything the world can draw, so the crosshair is never occluded.
constexpr float kInnerDepth = 0.0f;
constexpr float kBorderDepth = 0.0001f;

constexpr glm::vec4 kInnerColor{0.95f, 0.95f, 0.95f, 1.0f};
constexpr glm::vec4 kBorderColor{0.04f, 0.04f, 0.05f, 1.0f};

/// A bar of the reticle, always centred on the screen.
///
/// This file used to carry its own copy of `hud::appendQuad`, byte for byte
/// including the both-windings index list `UI.md` R3 requires - so the one rule
/// every quad in the HUD has to obey had two places to be got wrong. Nothing
/// was wrong with the copy; the cost was that it existed.
void appendBar(engine::MeshData& mesh, float halfWidth, float halfHeight, float depth, const glm::vec4& color) {
    hud::appendQuad(mesh, 0.0f, 0.0f, halfWidth, halfHeight, depth, color,
                    static_cast<float>(TextureLayer::White), false);
}

} // namespace

engine::MeshData makeCrosshair() {
    engine::MeshData mesh;

    // Border first, slightly larger and slightly further away.
    appendBar(mesh, kArmLength + kBorder, kArmThickness + kBorder, kBorderDepth, kBorderColor);
    appendBar(mesh, kArmThickness + kBorder, kArmLength + kBorder, kBorderDepth, kBorderColor);

    appendBar(mesh, kArmLength, kArmThickness, kInnerDepth, kInnerColor);
    appendBar(mesh, kArmThickness, kArmLength, kInnerDepth, kInnerColor);

    return mesh;
}

} // namespace game
