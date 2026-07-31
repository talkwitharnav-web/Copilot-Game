#include "hud/InventoryScreen.hpp"

#include "hud/HudPrimitives.hpp"

#include <string>

namespace game::inventoryScreen {
namespace {

// Smaller is nearer, matching the rest of the HUD.
constexpr float kDimDepth = 0.0044f;
constexpr float kPanelDepth = 0.0040f;
constexpr float kIconDepth = 0.0030f;
constexpr float kCountDepth = 0.0026f;
constexpr float kHeldIconDepth = 0.0012f;
constexpr float kHeldCountDepth = 0.0008f;

constexpr glm::vec4 kDim{0.0f, 0.0f, 0.0f, 0.55f};
constexpr glm::vec4 kCount{1.0f, 1.0f, 1.0f, 1.0f};

/// Where the panel lives on the combined HUD sheet, in pixels.
constexpr glm::vec2 kSheetSize{185.0f, 207.0f};
constexpr glm::vec2 kPanelPixelMin{0.0f, 41.0f};
constexpr glm::vec2 kPanelPixelSize{176.0f, 166.0f};

/// How tall the panel is drawn, in screen units where the window is 2 tall.
constexpr float kPanelHeight = 1.30f;

/// One art pixel in screen units. Every position below is measured off the
/// artwork in its own pixels and scaled through this, so the layout cannot
/// drift away from the image it was taken from.
constexpr float kPixel = kPanelHeight / kPanelPixelSize.y;

constexpr float kPanelHalfWidth = kPanelPixelSize.x * kPixel * 0.5f;
constexpr float kPanelHalfHeight = kPanelHeight * 0.5f;

/// Measured from the art: slot borders run 7, 25, 43 ... 169 across, the three
/// storage rows begin at y 83, and the separated hotbar row at y 141.
constexpr float kSlotPitchPixels = 18.0f;
constexpr float kFirstSlotCentreX = 16.0f;
constexpr float kStorageTopCentreY = 92.0f;
constexpr float kHotbarCentreY = 150.0f;

constexpr float kSlotHalf = kSlotPitchPixels * kPixel * 0.5f;
constexpr float kIconHalf = kSlotHalf * 0.72f;
constexpr float kCountHeight = kSlotPitchPixels * kPixel * 0.42f;

/// Art pixel to screen space. The panel is centred, so the art's centre is the
/// origin.
constexpr glm::vec2 toScreen(float artX, float artY) {
    return {(artX - kPanelPixelSize.x * 0.5f) * kPixel, (artY - kPanelPixelSize.y * 0.5f) * kPixel};
}

/// Slots 0-8 are the hotbar and are drawn on the separated bottom row; storage
/// fills the block above.
glm::vec2 gridSlotCentre(std::size_t index) {
    const auto column = static_cast<float>(index % kHotbarSlots);
    const float artX = kFirstSlotCentreX + column * kSlotPitchPixels;

    if (index < kHotbarSlots) {
        return toScreen(artX, kHotbarCentreY);
    }
    const auto row = static_cast<float>(index / kHotbarSlots) - 1.0f;
    return toScreen(artX, kStorageTopCentreY + row * kSlotPitchPixels);
}

/// Measured off the art the same way the storage grid was: slot borders at
/// x 97 and 115, y 17 and 35, each 18 across, and the result slot's border at
/// (153, 27).
constexpr float kCraftFirstCentreX = 106.0f;
constexpr float kCraftFirstCentreY = 26.0f;
constexpr float kCraftResultCentreX = 162.0f;
constexpr float kCraftResultCentreY = 36.0f;

glm::vec2 craftSlotCentre(std::size_t index) {
    const auto column = static_cast<float>(index % kCraftSize);
    const auto row = static_cast<float>(index / kCraftSize);
    return toScreen(kCraftFirstCentreX + column * kSlotPitchPixels,
                    kCraftFirstCentreY + row * kSlotPitchPixels);
}

glm::vec2 craftResultCentre() {
    return toScreen(kCraftResultCentreX, kCraftResultCentreY);
}

bool within(float x, float y, const glm::vec2& centre, float half) {
    return x >= centre.x - half && x <= centre.x + half && y >= centre.y - half && y <= centre.y + half;
}

void appendStack(engine::MeshData& mesh, const ItemStack& stack, const glm::vec2& centre, float iconDepth,
                 float countDepth) {
    if (stack.empty()) {
        return;
    }
    if (isBlockItem(stack.item)) {
        hud::appendBlockIcon(mesh, blockForItem(stack.item), centre.x, centre.y, kIconHalf, iconDepth);
    } else if (const int layer = itemTextureLayer(stack.item); layer >= 0) {
        // Flat, because there is no block to build a little cube out of.
        hud::appendQuad(mesh, centre.x, centre.y, kIconHalf, kIconHalf, iconDepth, {1.0f, 1.0f, 1.0f, 1.0f},
                        static_cast<float>(layer), true);
    }

    if (stack.count > 1) {
        const std::string label = std::to_string(stack.count);
        hud::appendText(mesh, label, centre.x - kSlotHalf + 0.006f, centre.y + kSlotHalf - kCountHeight * 0.5f - 0.006f,
                        kCountHeight, countDepth, kCount);
    }
}

} // namespace

std::optional<SlotHit> slotAt(float x, float y, float) {
    for (std::size_t i = 0; i < kInventorySlots; ++i) {
        if (within(x, y, gridSlotCentre(i), kSlotHalf)) {
            return SlotHit{Region::Grid, i};
        }
    }
    for (std::size_t i = 0; i < kCraftSlots; ++i) {
        if (within(x, y, craftSlotCentre(i), kSlotHalf)) {
            return SlotHit{Region::Craft, i};
        }
    }
    if (within(x, y, craftResultCentre(), kSlotHalf)) {
        return SlotHit{Region::CraftResult, 0};
    }
    return std::nullopt;
}

bool insidePanel(float x, float y, float) {
    return x >= -kPanelHalfWidth && x <= kPanelHalfWidth && y >= -kPanelHalfHeight && y <= kPanelHalfHeight;
}

engine::MeshData build(const Inventory& inventory, const ItemStack* craftSlots, const ItemStack& craftResult,
                       const ItemStack& heldStack, float cursorX, float cursorY, float aspect) {
    engine::MeshData mesh;

    // Dims the world behind, so the panel reads as a layer over the game rather
    // than part of it.
    hud::appendQuad(mesh, 0.0f, 0.0f, aspect, 1.0f, kDimDepth, kDim, static_cast<float>(TextureLayer::White), false);

    // The whole panel is one sprite from the artwork. Rebuilding its frames,
    // bevels and icons out of primitives would mean redrawing by hand something
    // that already exists as a picture.
    hud::appendSprite(mesh, 0.0f, 0.0f, kPanelHalfWidth, kPanelHalfHeight, kPanelDepth, kPanelPixelMin,
                      kPanelPixelSize, kSheetSize);

    for (std::size_t i = 0; i < kInventorySlots; ++i) {
        appendStack(mesh, inventory.slot(i), gridSlotCentre(i), kIconDepth, kCountDepth);
    }

    for (std::size_t i = 0; i < kCraftSlots; ++i) {
        appendStack(mesh, craftSlots[i], craftSlotCentre(i), kIconDepth, kCountDepth);
    }
    appendStack(mesh, craftResult, craftResultCentre(), kIconDepth, kCountDepth);

    // Drawn last and nearest, so what the cursor carries is never behind a slot.
    appendStack(mesh, heldStack, {cursorX, cursorY}, kHeldIconDepth, kHeldCountDepth);

    return mesh;
}

} // namespace game::inventoryScreen
