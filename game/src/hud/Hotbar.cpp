#include "hud/Hotbar.hpp"

#include "hud/HudPrimitives.hpp"

#include <glm/glm.hpp>

namespace game {
namespace {

/// Pixel dimensions of assets/textures/hud.png.
constexpr glm::vec2 kSheetSize{185.0f, 41.0f};

/// One cell of the sprite sheet, and where its open interior sits inside it.
///
/// The frame is drawn as four edge strips rather than one quad, because the
/// artwork's interior is solid black and would hide the world behind it. Leaving
/// the middle out is what allows the slot to be see-through.
struct CellSprite {
    glm::vec2 tileMin;
    glm::vec2 tileSize;
    glm::vec2 interiorMin; // Relative to tileMin.
    glm::vec2 interiorSize;
};

// Measured from the sheet. The unselected cell is taken from a slot the
// selection frame does not overlap; the selected cell is the first slot, which
// the sheet already shows highlighted.
constexpr CellSprite kNormalCell{{42.0f, 18.0f}, {21.0f, 21.0f}, {4.0f, 4.0f}, {14.0f, 14.0f}};
constexpr CellSprite kSelectedCell{{0.0f, 16.0f}, {25.0f, 25.0f}, {6.0f, 6.0f}, {15.0f, 15.0f}};

/// On-screen size of one cell, relative to window height. The renderer corrects
/// for aspect ratio. Y spans -1 to 1 across the window, so this is a little over
/// 6% of screen height per cell.
constexpr float kSlotSize = 0.152f;

/// The selected cell is drawn larger by the same ratio its sprite is larger, so
/// the frame overhangs its neighbours exactly as the artwork intends.
constexpr float kSelectedScale = kSelectedCell.tileSize.x / kNormalCell.tileSize.x;

/// Positive Y is down in screen space, so this sits near the bottom edge.
constexpr float kBarCentreY = 0.888f;

/// Half-height of a block icon as a fraction of the cell's half-size, leaving
/// clear margin between the icon and the bevel.
constexpr float kIconScale = 0.56f;

/// Darkens whatever is behind a slot so the block icon stays readable against
/// bright sky or sand, without hiding the world.
constexpr glm::vec4 kInteriorTint{0.04f, 0.04f, 0.06f, 0.55f};

// Smaller is nearer. The selected cell gets a nearer band than the rest so its
// oversized frame draws over its neighbours instead of being clipped by them.
constexpr float kSelectedFrameDepth = 0.00075f;
constexpr float kSelectedTintDepth = 0.00070f;
constexpr float kSelectedIconDepth = 0.00065f;
constexpr float kFrameDepth = 0.00095f;
constexpr float kTintDepth = 0.00090f;
constexpr float kIconDepth = 0.00085f;

/// Places a sub-rectangle of a cell sprite at the right spot on screen.
void appendCellPiece(engine::MeshData& mesh, const CellSprite& cell, float cellCentreX, float cellCentreY,
                     float cellSize, const glm::vec2& localMin, const glm::vec2& localSize, float depth) {
    const glm::vec2 scale = glm::vec2{cellSize} / cell.tileSize;
    const glm::vec2 half = localSize * scale * 0.5f;
    const glm::vec2 centre = glm::vec2{cellCentreX, cellCentreY} +
                             (localMin + localSize * 0.5f - cell.tileSize * 0.5f) * scale;

    hud::appendSprite(mesh, centre.x, centre.y, half.x, half.y, depth, cell.tileMin + localMin, localSize,
                      kSheetSize);
}

void appendCellFrame(engine::MeshData& mesh, const CellSprite& cell, float centreX, float centreY, float cellSize,
                     float depth) {
    const glm::vec2 tile = cell.tileSize;
    const glm::vec2 innerMin = cell.interiorMin;
    const glm::vec2 innerMax = cell.interiorMin + cell.interiorSize;

    // Top and bottom span the full width; the sides fill only the gap between
    // them, so no pixel is drawn twice.
    appendCellPiece(mesh, cell, centreX, centreY, cellSize, {0.0f, 0.0f}, {tile.x, innerMin.y}, depth);
    appendCellPiece(mesh, cell, centreX, centreY, cellSize, {0.0f, innerMax.y}, {tile.x, tile.y - innerMax.y},
                    depth);
    appendCellPiece(mesh, cell, centreX, centreY, cellSize, {0.0f, innerMin.y}, {innerMin.x, cell.interiorSize.y},
                    depth);
    appendCellPiece(mesh, cell, centreX, centreY, cellSize, {innerMax.x, innerMin.y},
                    {tile.x - innerMax.x, cell.interiorSize.y}, depth);
}

} // namespace

engine::MeshData makeHotbar(const std::array<BlockId, kHotbarSlots>& slots, std::size_t selected) {
    engine::MeshData mesh;

    // Cells butt up against each other so neighbouring borders merge into a
    // single divider, as they do in the artwork.
    const float totalWidth = kHotbarSlots * kSlotSize;
    const float firstCentreX = -totalWidth * 0.5f + kSlotSize * 0.5f;

    for (std::size_t slot = 0; slot < kHotbarSlots; ++slot) {
        const float centreX = firstCentreX + static_cast<float>(slot) * kSlotSize;
        const bool isSelected = slot == selected;

        const CellSprite& cell = isSelected ? kSelectedCell : kNormalCell;
        const float cellSize = kSlotSize * (isSelected ? kSelectedScale : 1.0f);

        appendCellFrame(mesh, cell, centreX, kBarCentreY, cellSize,
                        isSelected ? kSelectedFrameDepth : kFrameDepth);

        // The interior is off-centre within the tile, because the bevel is
        // thicker on the top and left. Everything inside follows that centre
        // rather than the tile's, or it sits visibly high and to the left.
        const glm::vec2 scale = glm::vec2{cellSize} / cell.tileSize;
        const glm::vec2 interiorCentre = glm::vec2{centreX, kBarCentreY} +
                                         (cell.interiorMin + cell.interiorSize * 0.5f - cell.tileSize * 0.5f) * scale;
        const glm::vec2 interiorHalf = cell.interiorSize * scale * 0.5f;

        hud::appendQuad(mesh, interiorCentre.x, interiorCentre.y, interiorHalf.x, interiorHalf.y,
                        isSelected ? kSelectedTintDepth : kTintDepth, kInteriorTint,
                        static_cast<float>(TextureLayer::White), false);

        if (slots[slot] == BlockId::Air) {
            continue;
        }

        hud::appendBlockIcon(mesh, slots[slot], interiorCentre.x, interiorCentre.y,
                             kSlotSize * 0.5f * kIconScale, isSelected ? kSelectedIconDepth : kIconDepth);
    }

    return mesh;
}

} // namespace game
