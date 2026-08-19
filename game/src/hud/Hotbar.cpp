#include "hud/Hotbar.hpp"

#include "hud/HudPrimitives.hpp"
#include "world/Projectile.hpp"

#include <glm/glm.hpp>

namespace game {
namespace {

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

/// The selected cell is drawn larger by the same ratio its sprite is larger, so
/// the frame overhangs its neighbours exactly as the artwork intends.
constexpr float kSelectedScale = kSelectedCell.tileSize.x / kNormalCell.tileSize.x;

// `kHotbarSelectedOverhang` is what the status bars clear, and it has to be the
// overhang the frame actually has. **The single edit that breaks this is
// remeasuring kSelectedCell from a redrawn sheet** without carrying the new
// ratio into the header - which puts the hearts back underneath the frame,
// where they were for the whole life of the selected-cell art.
static_assert(kHotbarSelectedOverhang == kHotbarSlotSize * (kSelectedScale - 1.0f) * 0.5f,
              "the overhang the status bars clear must be the overhang the selected frame draws");

/// Half-height of a block icon as a fraction of the cell's half-size, leaving
/// clear margin between the icon and the bevel.
constexpr float kIconScale = 0.56f;

// Our cell art is 21 texels across with a 14-texel interior, and the icon must
// stay inside that interior or it draws over the bevel and the slot stops
// reading as a frame. **The single edit that breaks this is raising kIconScale
// to match the container screens' 0.72** - which is the right number for a
// reference-proportioned slot and the wrong one for ours.
static_assert(kIconScale <= kNormalCell.interiorSize.x / kNormalCell.tileSize.x,
              "a hotbar icon must fit the cell's interior - see kNormalCell, whose bevel is thicker "
              "than the reference's");

/// How much of its size a fully drawn bow gives up. **The whole of the draw
/// animation**, because the icon stays centred: a fraction of itself is a limit
/// by construction, where a slide has to be clamped against the slot and still
/// ends up looking like the icon fell into a corner.
constexpr float kBowPullShrink = 0.24f;

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

// Nearer than the icon, so a count is never swallowed by the block behind it.
constexpr float kCountDepth = 0.00080f;
constexpr float kSelectedCountDepth = 0.00060f;

// **"Nearer than the icon" is a claim about a number the icon owns, and this is
// where it stops being taken on trust.** An icon does not sit at one depth: a
// block drawn as boxes reaches `hud::kIconDepthSpan` in front of the depth it
// is handed, and the count's own shadow reaches `hud::kFontShadowDepth` behind
// it, so the gap that matters is 5e-6 rather than the 5e-5 these two lines
// look like they leave.
//
// **This is downstream of five blocks changing shape.** Candles, bamboo, sea
// pickles, lecterns and dragon eggs were flat sprites until this session and
// spent none of the icon's depth budget; they are model-shaped now and spend
// all of it. Nothing would have reported that - the count would simply have
// come out behind a candle.
static_assert(hud::iconStaysBehindItsMarks(kIconDepth, kCountDepth),
              "an unselected hotbar icon now reaches in front of its own stack count and "
              "durability bar - widen the gap between kIconDepth and kCountDepth");
static_assert(hud::iconStaysBehindItsMarks(kSelectedIconDepth, kSelectedCountDepth),
              "the selected hotbar icon now reaches in front of its own stack count and "
              "durability bar - widen the gap between kSelectedIconDepth and kSelectedCountDepth");
static_assert(kSelectedIconDepth < kIconDepth && kSelectedCountDepth < kCountDepth,
              "the selected cell must sit nearer than its neighbours, or its oversized frame is "
              "clipped by the cells either side of it");

/// Places a sub-rectangle of a cell sprite at the right spot on screen.
void appendCellPiece(engine::MeshData& mesh, const CellSprite& cell, float cellCentreX, float cellCentreY,
                     float cellSize, const glm::vec2& localMin, const glm::vec2& localSize, float depth) {
    const glm::vec2 scale = glm::vec2{cellSize} / cell.tileSize;
    const glm::vec2 half = localSize * scale * 0.5f;
    const glm::vec2 centre = glm::vec2{cellCentreX, cellCentreY} +
                             (localMin + localSize * 0.5f - cell.tileSize * 0.5f) * scale;

    hud::appendSprite(mesh, centre.x, centre.y, half.x, half.y, depth, cell.tileMin + localMin, localSize,
                      hud::kSheetSize);
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

engine::MeshData makeHotbar(const Inventory& inventory, std::size_t selected, float bowDrawSeconds) {
    engine::MeshData mesh;

    // Cells butt up against each other so neighbouring borders merge into a
    // single divider, as they do in the artwork.
    const float totalWidth = kHotbarWidth;
    const float firstCentreX = -totalWidth * 0.5f + kHotbarSlotSize * 0.5f;

    for (std::size_t slot = 0; slot < kHotbarSlots; ++slot) {
        const float centreX = firstCentreX + static_cast<float>(slot) * kHotbarSlotSize;
        const bool isSelected = slot == selected;

        const CellSprite& cell = isSelected ? kSelectedCell : kNormalCell;
        const float cellSize = kHotbarSlotSize * (isSelected ? kSelectedScale : 1.0f);

        appendCellFrame(mesh, cell, centreX, kHotbarCentreY, cellSize,
                        isSelected ? kSelectedFrameDepth : kFrameDepth);

        // The interior is off-centre within the tile, because the bevel is
        // thicker on the top and left. Everything inside follows that centre
        // rather than the tile's, or it sits visibly high and to the left.
        const glm::vec2 scale = glm::vec2{cellSize} / cell.tileSize;
        const glm::vec2 interiorCentre = glm::vec2{centreX, kHotbarCentreY} +
                                         (cell.interiorMin + cell.interiorSize * 0.5f - cell.tileSize * 0.5f) * scale;
        const glm::vec2 interiorHalf = cell.interiorSize * scale * 0.5f;

        hud::appendQuad(mesh, interiorCentre.x, interiorCentre.y, interiorHalf.x, interiorHalf.y,
                        isSelected ? kSelectedTintDepth : kTintDepth, kInteriorTint,
                        static_cast<float>(TextureLayer::White), false);

        const ItemStack& stack = inventory.slot(slot);
        if (stack.empty()) {
            continue;
        }

        const float iconHalf = kHotbarSlotSize * 0.5f * kIconScale;
        const float iconDepth = isSelected ? kSelectedIconDepth : kIconDepth;
        if (isBlockItem(stack.item)) {
            hud::appendBlockIcon(mesh, blockForItem(stack.item), interiorCentre.x, interiorCentre.y, iconHalf,
                                 iconDepth);
        } else if (const int layer = itemTextureLayer(stack.item); layer >= 0) {
            // A bow being drawn shows one of three pictures instead of its own.
            // **Driven by the draw time, never used to decide the shot** - the
            // reference finishes the picture in half the time it finishes the
            // charge, so the two must not share a number.
            const bool pulling =
                isSelected && stack.item == ItemId::Bow && bowDrawSeconds >= 0.0f;
            const float sprite =
                pulling ? static_cast<float>(kBowPullingFirstSprite + bowPullStage(bowDrawSeconds))
                        : static_cast<float>(layer);

            // The three pictures are only the string's own travel. Shrinking
            // the icon on the charge curve is what fills the gaps between them:
            // it reads as the bow being drawn back **away from you**, and it is
            // the one way to say that without moving the icon.
            //
            // **Centred, deliberately.** Sliding it back along the arrow's own
            // axis was tried first and read as the bow slipping into the bottom
            // left corner of the slot - a slot is a frame, and anything that
            // leaves its middle looks dropped rather than pulled.
            const float pull = pulling ? bowCharge(bowDrawSeconds) : 0.0f;
            const float half = iconHalf * (1.0f - kBowPullShrink * pull);

            hud::appendQuad(mesh, interiorCentre.x, interiorCentre.y, half, half, iconDepth,
                            {1.0f, 1.0f, 1.0f, 1.0f}, sprite, true);
        } else {
            continue;
        }

        // The count and the durability bar, through **the one function that
        // places both**, so a stack reads the same in the bar as it does in the
        // inventory a keypress later. This used to be a private copy that put
        // the number bottom *left* at a flat 0.042 screen units - 41% larger
        // than the container screens' - and had no bar at all, so a pick's wear
        // was invisible in the only place you actually hold it.
        //
        // Measured against the **icon**, not the cell: our hotbar art has a
        // thicker bevel than the reference's, so the icon inside it is smaller
        // than a container slot's, and marks placed against the cell would drift
        // off the picture they annotate.
        hud::appendStackDecorations(mesh, stack, interiorCentre, iconHalf,
                                    isSelected ? kSelectedCountDepth : kCountDepth);
    }

    return mesh;
}

} // namespace game
