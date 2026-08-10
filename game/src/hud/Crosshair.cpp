#include "hud/Crosshair.hpp"

#include "world/Block.hpp"

#include <glm/glm.hpp>

#include <cstdint>

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

void appendQuad(engine::MeshData& mesh, float halfWidth, float halfHeight, float depth, const glm::vec4& color) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    const float layer = static_cast<float>(TextureLayer::White);

    const glm::vec2 corners[4]{
        {-halfWidth, -halfHeight}, {halfWidth, -halfHeight}, {halfWidth, halfHeight}, {-halfWidth, halfHeight}};

    for (const glm::vec2& corner : corners) {
        mesh.vertices.push_back(engine::Vertex{{corner.x, corner.y, depth},
                                               engine::packVertexColor(color.r, color.g, color.b, color.a),
                                               {0.5f, 0.5f},
                                               layer,
                                               engine::kVertexSurfaceDefault});
    }

    // Both windings. Backface culling is on, and screen-space geometry skips the
    // projection that establishes which way is front — rather than deriving that
    // (and silently rendering nothing if it is backwards), emit the quad twice.
    // Four extra triangles for the entire HUD is not worth reasoning about.
    mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                             base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
}

} // namespace

engine::MeshData makeCrosshair() {
    engine::MeshData mesh;

    // Border first, slightly larger and slightly further away.
    appendQuad(mesh, kArmLength + kBorder, kArmThickness + kBorder, kBorderDepth, kBorderColor);
    appendQuad(mesh, kArmThickness + kBorder, kArmLength + kBorder, kBorderDepth, kBorderColor);

    appendQuad(mesh, kArmLength, kArmThickness, kInnerDepth, kInnerColor);
    appendQuad(mesh, kArmThickness, kArmLength, kInnerDepth, kInnerColor);

    return mesh;
}

} // namespace game
