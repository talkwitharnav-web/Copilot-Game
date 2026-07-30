#include "hud/Hotbar.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace game {
namespace {

// Units are relative to window height; the renderer corrects for aspect ratio.
constexpr float kSlotSize = 0.085f;
constexpr float kSlotGap = 0.008f;
constexpr float kIconScale = 0.76f;
constexpr float kBorder = 0.006f;

/// Positive Y is down in screen space, so this sits near the bottom edge.
constexpr float kBarCentreY = 0.88f;

// Smaller is nearer. The crosshair lives nearer still, so it is never covered.
constexpr float kSelectionDepth = 0.0009f;
constexpr float kFrameDepth = 0.0008f;
constexpr float kSlotDepth = 0.0007f;
constexpr float kIconDepth = 0.0006f;

constexpr glm::vec3 kFrameColor{0.06f, 0.06f, 0.07f};
constexpr glm::vec3 kSlotColor{0.24f, 0.24f, 0.27f};
constexpr glm::vec3 kSelectionColor{0.96f, 0.96f, 0.98f};

void appendQuad(engine::MeshData& mesh, float centreX, float centreY, float halfWidth, float halfHeight, float depth,
                const glm::vec3& color, float layer, bool textured) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    const glm::vec2 offsets[4]{
        {-halfWidth, -halfHeight}, {halfWidth, -halfHeight}, {halfWidth, halfHeight}, {-halfWidth, halfHeight}};
    const glm::vec2 uvs[4]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (int corner = 0; corner < 4; ++corner) {
        const glm::vec2 uv = textured ? uvs[corner] : glm::vec2{0.5f, 0.5f};
        mesh.vertices.push_back(engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y, depth},
                                               {color.r, color.g, color.b},
                                               {uv.x, uv.y},
                                               layer});
    }

    // Both windings, for the same reason as the crosshair: screen-space geometry
    // skips the projection that decides which face is the front one.
    mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                             base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
}

} // namespace

engine::MeshData makeHotbar(const std::array<BlockId, kHotbarSlots>& slots, std::size_t selected) {
    engine::MeshData mesh;

    const float pitch = kSlotSize + kSlotGap;
    const float totalWidth = kHotbarSlots * kSlotSize + (kHotbarSlots - 1) * kSlotGap;
    const float firstCentreX = -totalWidth * 0.5f + kSlotSize * 0.5f;

    const float white = static_cast<float>(TextureLayer::White);

    // One frame behind the whole row rather than per slot, so the bar reads as a
    // single object.
    appendQuad(mesh, 0.0f, kBarCentreY, totalWidth * 0.5f + kBorder, kSlotSize * 0.5f + kBorder, kFrameDepth,
               kFrameColor, white, false);

    for (std::size_t slot = 0; slot < kHotbarSlots; ++slot) {
        const float centreX = firstCentreX + static_cast<float>(slot) * pitch;

        if (slot == selected) {
            appendQuad(mesh, centreX, kBarCentreY, kSlotSize * 0.5f + kBorder, kSlotSize * 0.5f + kBorder,
                       kSelectionDepth, kSelectionColor, white, false);
        }

        appendQuad(mesh, centreX, kBarCentreY, kSlotSize * 0.5f, kSlotSize * 0.5f, kSlotDepth, kSlotColor, white,
                   false);

        if (slots[slot] == BlockId::Air) {
            continue;
        }

        // The side face is the most recognisable view of a block: grass reads as
        // grass, and layered materials still show their structure.
        const float half = kSlotSize * 0.5f * kIconScale;
        appendQuad(mesh, centreX, kBarCentreY, half, half, kIconDepth, glm::vec3{1.0f},
                   blockTextureLayer(slots[slot], BlockFace::Side), true);
    }

    return mesh;
}

} // namespace game
