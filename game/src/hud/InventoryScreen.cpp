#include "hud/InventoryScreen.hpp"

#include "hud/HudPrimitives.hpp"
#include "world/Creature.hpp"

#include <algorithm>
#include <string>

namespace game::inventoryScreen {
namespace {

// Smaller is nearer, matching the rest of the HUD.
//
// **Every band is assigned here, before anything is drawn.** Two overlapping
// cards, tabs that sit above a card's edge, and cell backgrounds under icons
// under labels are exactly the arrangement that once hid the hotbar's selection
// highlight behind its own frame - a HUD element in the wrong order is
// invisible and nothing reports it.
constexpr float kDimDepth = 0.0044f;
constexpr float kTabDepth = 0.0042f;
constexpr float kPanelDepth = 0.0040f;
/// In front of its card, so a selected tab merges into it rather than sitting
/// behind it. That merge is the entire selection cue in the reference art.
constexpr float kSelectedTabDepth = 0.0038f;
// 0.0037 is free: a tab's icon is part of its sprite, not drawn over it.
constexpr float kCellDepth = 0.0036f;
constexpr float kLabelDepth = 0.0035f;
// 0.0034 is left free for the scrollbar.
constexpr float kCatalogueIconDepth = 0.0032f;
constexpr float kCatalogueCountDepth = 0.0031f;
constexpr float kIconDepth = 0.0030f;
constexpr float kCountDepth = 0.0026f;
constexpr float kHeldIconDepth = 0.0012f;
constexpr float kHeldCountDepth = 0.0008f;
// Nearest of everything: a label that anything can cover is not a label.
constexpr float kTooltipDepth = 0.0006f;

constexpr glm::vec4 kDim{0.0f, 0.0f, 0.0f, 0.55f};
constexpr glm::vec4 kCount{1.0f, 1.0f, 1.0f, 1.0f};
constexpr glm::vec4 kLabel{0.24f, 0.24f, 0.24f, 1.0f};
/// Unselected tabs and their icons are knocked back rather than recoloured.
constexpr glm::vec4 kUnselectedTint{0.72f, 0.72f, 0.72f, 1.0f};

/// Where each panel lives on the combined HUD sheet, in pixels. Both are the
/// same size; the crafting table's is generated from the inventory's by
/// `tools/make-hud-sheet.ps1`, which is why their cells match exactly.
constexpr glm::vec2 kPanelPixelSize{176.0f, 166.0f};
constexpr glm::vec2 kInventoryPanelMin{0.0f, 41.0f};
constexpr glm::vec2 kCraftingPanelMin{0.0f, 207.0f};
constexpr glm::vec2 kFurnacePanelMin{0.0f, 373.0f};

/// The catalogue card, and the tab strip. Both are written by
/// `tools/make-hud-sheet.ps1`, which reports the offsets it used - if they
/// disagree with these, the sheet has moved.
///
/// The strip holds five *complete* tabs, icon included, across two rows:
/// unselected on top, selected below. A 24-pixel pitch against a 22-pixel tab
/// leaves a two-pixel guard so mip generation cannot bleed one tab into its
/// neighbour.
constexpr glm::vec2 kBookPanelMin{0.0f, 555.0f};
constexpr glm::vec2 kBookPixelSize{146.0f, 166.0f};
constexpr glm::vec2 kTabSheetSize{22.0f, 25.0f};
constexpr float kTabSheetPitch = 24.0f;
constexpr float kTabSheetTop = 721.0f;
constexpr float kTabSheetRowPitch = 27.0f;

/// The catalogue reuses the inventory's own storage cell rather than carrying a
/// second copy of it. Slice 5 adds the red and selected variants beside it.
constexpr glm::vec2 kCellSheetMin{kInventoryPanelMin.x + 7.0f, kInventoryPanelMin.y + 83.0f};
constexpr glm::vec2 kCellSheetSize{18.0f, 18.0f};

/// The lit indicators, kept off the panel so they can be drawn over the spent
/// versions baked into it and cut off part way.
constexpr glm::vec2 kLitFlakeSheetMin{0.0f, 539.0f};
constexpr glm::vec2 kLitFlameSize{14.0f, 14.0f};
constexpr glm::vec2 kLitArrowSheetMin{16.0f, 540.0f};
constexpr glm::vec2 kLitArrowSize{21.0f, 15.0f};

/// Where those indicators sit on the furnace panel, in its own pixels.
constexpr glm::vec2 kFurnaceFlameArt{55.0f, 36.0f};
constexpr glm::vec2 kFurnaceArrowArt{79.0f, 36.0f};

/// How tall the panel is drawn, in screen units where the window is 2 tall.
constexpr float kPanelHeight = 1.30f;

/// One art pixel in screen units. Every position below is measured off the
/// artwork in its own pixels and scaled through this, so the layout cannot
/// drift away from the image it was taken from.
constexpr float kPixel = kPanelHeight / kPanelPixelSize.y;

/// Measured from the art: slot borders run 7, 25, 43 ... 169 across, the three
/// storage rows begin at y 83, and the separated hotbar row at y 141.
constexpr float kSlotPitchPixels = 18.0f;
constexpr float kFirstSlotCentreX = 16.0f;
constexpr float kStorageTopCentreY = 92.0f;
constexpr float kHotbarCentreY = 150.0f;

constexpr float kSlotHalf = kSlotPitchPixels * kPixel * 0.5f;
/// Eight art pixels, so the label scales with the panel rather than the window.
constexpr float kTooltipTextHeight = 8.0f * kPixel;

/// The two cards sit in one centred block: catalogue, a four-unit fold, then
/// the inventory. Measured off `reference/crafting-ui.avif` at 3x, where they
/// come out as 146 + 4 + 176 = 326.
constexpr float kCardGapPixels = 4.0f;
constexpr float kLayoutPixelWidth = kBookPixelSize.x + kCardGapPixels + kPanelPixelSize.x;

/// How far right the inventory card slides to make room, and where the
/// catalogue's own centre lands. Derived from the block rather than written
/// down, so changing either card's width moves both correctly.
constexpr float kInventoryShiftPixels = kLayoutPixelWidth * 0.5f - kPanelPixelSize.x * 0.5f;
constexpr float kBookCentrePixels = kBookPixelSize.x * 0.5f - kLayoutPixelWidth * 0.5f;

constexpr float kPanelOffsetX(Kind kind) {
    return showsCatalogue(kind) ? kInventoryShiftPixels * kPixel : 0.0f;
}

constexpr float kPanelHalfWidth = kPanelPixelSize.x * kPixel * 0.5f;
constexpr float kPanelHalfHeight = kPanelHeight * 0.5f;
constexpr float kBookHalfWidth = kBookPixelSize.x * kPixel * 0.5f;

/// Art pixel to screen space, for the inventory card. The card is centred
/// vertically; horizontally it is wherever the layout block puts it, and
/// **this is the only place that offset is applied** - every slot centre, hit
/// test and panel bound comes through here, so the two cannot drift apart.
constexpr glm::vec2 toScreen(Kind kind, float artX, float artY) {
    return {(artX - kPanelPixelSize.x * 0.5f) * kPixel + kPanelOffsetX(kind),
            (artY - kPanelPixelSize.y * 0.5f) * kPixel};
}

/// The same, in the catalogue card's own pixels. Both cards are 166 tall, so
/// only the horizontal half differs.
constexpr glm::vec2 toBook(float artX, float artY) {
    return {(artX - kBookPixelSize.x * 0.5f + kBookCentrePixels) * kPixel,
            (artY - kBookPixelSize.y * 0.5f) * kPixel};
}

/// The catalogue grid, in the card's own pixels: seven columns on the same
/// 18-unit pitch every other slot uses, centred in the card, below the tab-name
/// row. `kCatalogueBottom` is the card's inner edge, which the last row is
/// clipped against rather than hidden by.
constexpr int kCatalogueColumns = 7;
constexpr float kCatalogueLeft = (kBookPixelSize.x - kCatalogueColumns * kSlotPitchPixels) * 0.5f;
constexpr float kCatalogueTop = 26.0f;
constexpr float kCatalogueBottom = 160.0f;
constexpr float kCatalogueLabelY = 15.0f;

/// Tabs hang directly above the card, flush with its top edge.
///
/// Measured off the capture: the first four pack on a 25-unit pitch and Search
/// is pushed a further 21 units right, so the strip is deliberately not evenly
/// spaced. **The 3-unit gap between neighbouring tabs is the reference's own** -
/// it falls out of a 22-wide tab on a 25-unit pitch, and is not a chosen
/// margin. Search then ends at 143, three units short of the card's 146, which
/// is exactly the card's frame width.
constexpr float kTabPitch = 25.0f;
constexpr float kSearchExtraGap = 21.0f;

constexpr float tabLeft(CatalogueTab tab) {
    return static_cast<float>(tab) * kTabPitch + (tab == CatalogueTab::Search ? kSearchExtraGap : 0.0f);
}

constexpr glm::vec2 tabSheetMin(CatalogueTab tab, bool selected) {
    return {static_cast<float>(tab) * kTabSheetPitch, kTabSheetTop + (selected ? kTabSheetRowPitch : 0.0f)};
}

/// Slots 0-8 are the hotbar and are drawn on the separated bottom row; storage
/// fills the block above.
glm::vec2 gridSlotCentre(Kind kind, std::size_t index) {
    const auto column = static_cast<float>(index % kHotbarSlots);
    const float artX = kFirstSlotCentreX + column * kSlotPitchPixels;

    if (index < kHotbarSlots) {
        return toScreen(kind, artX, kHotbarCentreY);
    }
    const auto row = static_cast<float>(index / kHotbarSlots) - 1.0f;
    return toScreen(kind, artX, kStorageTopCentreY + row * kSlotPitchPixels);
}

/// Measured off the art the same way the storage grid was.
///
/// The inventory's own 2x2 sits top-right with its result beside it. The
/// crafting table's 3x3 sits left of centre with its result aligned to the
/// middle row, which is the layout the panel art was generated to.
struct Layout {
    glm::vec2 panelPixelMin;
    int craftSize;
    glm::vec2 craftFirstCentre;
    glm::vec2 craftResultCentre;
};

constexpr Layout layoutFor(Kind kind) {
    if (kind == Kind::CraftingTable) {
        return Layout{kCraftingPanelMin, 3, {37.0f, 25.0f}, {131.0f, 43.0f}};
    }
    if (kind == Kind::Furnace) {
        // Input above, fuel below. The two share the `Craft` region so the whole
        // click, drag and shift-click path works unchanged; only the positions
        // differ, which is exactly what this table is for.
        return Layout{kFurnacePanelMin, 0, {63.0f, 25.0f}, {123.0f, 43.0f}};
    }
    return Layout{kInventoryPanelMin, 2, {106.0f, 26.0f}, {162.0f, 36.0f}};
}

/// A furnace's two input slots are stacked rather than laid out in a grid, so
/// they are placed by hand instead of by pitch.
constexpr float kFurnaceFuelCentreY = 61.0f;

glm::vec2 craftSlotCentre(Kind kind, const Layout& layout, std::size_t index) {
    if (kind == Kind::Furnace) {
        return toScreen(kind, layout.craftFirstCentre.x,
                        index == 0 ? layout.craftFirstCentre.y : kFurnaceFuelCentreY);
    }
    const auto column = static_cast<float>(index % static_cast<std::size_t>(layout.craftSize));
    const auto row = static_cast<float>(index / static_cast<std::size_t>(layout.craftSize));
    return toScreen(kind, layout.craftFirstCentre.x + column * kSlotPitchPixels,
                    layout.craftFirstCentre.y + row * kSlotPitchPixels);
}

/// The furnace's own slot count, which is not a square grid.
constexpr std::size_t slotsFor(Kind kind) {
    return kind == Kind::Furnace ? 2u : craftSlotCount(kind);
}

glm::vec2 craftResultCentre(Kind kind, const Layout& layout) {
    return toScreen(kind, layout.craftResultCentre.x, layout.craftResultCentre.y);
}

bool within(float x, float y, const glm::vec2& centre, float half) {
    return x >= centre.x - half && x <= centre.x + half && y >= centre.y - half && y <= centre.y + half;
}

/// A catalogue cell's top-left corner and how much of it the card's inner edge
/// leaves visible.
///
/// The reference masks its list to a viewport and so clips the last row
/// **through** a cell rather than dropping it. There is no scissor rectangle
/// anywhere in this renderer, so the clip is arithmetic: a shorter quad with a
/// correspondingly shorter slice of the sprite.
struct CatalogueCell {
    glm::vec2 artTopLeft{};
    float visiblePixels = 0.0f;

    bool visible() const { return visiblePixels > 0.0f; }
    bool whole() const { return visiblePixels >= kSlotPitchPixels; }
};

CatalogueCell catalogueCell(std::size_t index) {
    const auto column = static_cast<float>(index % static_cast<std::size_t>(kCatalogueColumns));
    const auto row = static_cast<float>(index / static_cast<std::size_t>(kCatalogueColumns));
    const float top = kCatalogueTop + row * kSlotPitchPixels;
    CatalogueCell cell;
    cell.artTopLeft = {kCatalogueLeft + column * kSlotPitchPixels, top};
    cell.visiblePixels = std::min(kSlotPitchPixels, kCatalogueBottom - top);
    return cell;
}

/// How many cells the card has room for, counting a clipped last row.
std::size_t catalogueCapacity() {
    std::size_t count = 0;
    while (catalogueCell(count).visible()) {
        count += static_cast<std::size_t>(kCatalogueColumns);
    }
    return count;
}

glm::vec2 catalogueCellCentre(const CatalogueCell& cell) {
    return toBook(cell.artTopLeft.x + kSlotPitchPixels * 0.5f, cell.artTopLeft.y + kSlotPitchPixels * 0.5f);
}

constexpr const char* tabName(CatalogueTab tab) {
    return tab == CatalogueTab::Search ? "Search" : categoryName(static_cast<ItemCategory>(tab));
}

void appendCatalogue(engine::MeshData& mesh, const CatalogueState& state, const std::vector<ItemId>& shown) {
    hud::appendSprite(mesh, kBookCentrePixels * kPixel, 0.0f, kBookHalfWidth, kPanelHalfHeight, kPanelDepth,
                      kBookPanelMin, kBookPixelSize, hud::kSheetSize);

    // Each tab is one sprite carrying its own icon, so nothing is drawn over
    // it. The reference's tabs are built the same way, which is what lets the
    // proof atlas swap a whole tab in without this code knowing.
    for (int i = 0; i < static_cast<int>(CatalogueTab::Count); ++i) {
        const auto tab = static_cast<CatalogueTab>(i);
        const bool selected = tab == state.tab;
        const glm::vec2 centre = toBook(tabLeft(tab) + kTabSheetSize.x * 0.5f, -kTabSheetSize.y * 0.5f);
        hud::appendSprite(mesh, centre.x, centre.y, kTabSheetSize.x * kPixel * 0.5f,
                          kTabSheetSize.y * kPixel * 0.5f, selected ? kSelectedTabDepth : kTabDepth,
                          tabSheetMin(tab, selected), kTabSheetSize, hud::kSheetSize,
                          selected ? glm::vec4{1.0f} : kUnselectedTint);
    }

    const glm::vec2 label = toBook(kCatalogueLeft, kCatalogueLabelY);
    hud::appendText(mesh, tabName(state.tab), label.x, label.y, 8.0f * kPixel, kLabelDepth, kLabel);

    const std::size_t capacity = catalogueCapacity();
    for (std::size_t i = 0; i < capacity; ++i) {
        const CatalogueCell cell = catalogueCell(i);
        const float height = cell.visiblePixels;
        const glm::vec2 centre =
            toBook(cell.artTopLeft.x + kSlotPitchPixels * 0.5f, cell.artTopLeft.y + height * 0.5f);
        hud::appendSprite(mesh, centre.x, centre.y, kSlotPitchPixels * kPixel * 0.5f, height * kPixel * 0.5f,
                          kCellDepth, kCellSheetMin, {kCellSheetSize.x, height}, hud::kSheetSize);

        // A cell the card cuts through gets its background and nothing else. An
        // isometric icon is three quads on a cube, so a rectangle cannot clip
        // it, and half an item spilling over the frame looks worse than an
        // empty sliver saying "there is more below".
        if (cell.whole() && i < shown.size()) {
            hud::appendStack(mesh, ItemStack{shown[i], 1}, catalogueCellCentre(cell), kSlotHalf,
                             kCatalogueIconDepth, kCatalogueCountDepth);
        }
    }
}

} // namespace

std::vector<ItemId> catalogueItems(CatalogueTab tab) {
    std::vector<ItemId> shown;
    for (const ItemId item : allItems()) {
        if (tab == CatalogueTab::Search || categoryFor(item) == static_cast<ItemCategory>(tab)) {
            shown.push_back(item);
        }
    }
    return shown;
}

std::optional<CatalogueTab> tabAt(Kind kind, float x, float y) {
    if (!showsCatalogue(kind)) {
        return std::nullopt;
    }
    for (int i = 0; i < static_cast<int>(CatalogueTab::Count); ++i) {
        const auto tab = static_cast<CatalogueTab>(i);
        const glm::vec2 centre =
            toBook(tabLeft(tab) + kTabSheetSize.x * 0.5f, -kTabSheetSize.y * 0.5f);
        if (x >= centre.x - kTabSheetSize.x * kPixel * 0.5f && x <= centre.x + kTabSheetSize.x * kPixel * 0.5f &&
            y >= centre.y - kTabSheetSize.y * kPixel * 0.5f && y <= centre.y + kTabSheetSize.y * kPixel * 0.5f) {
            return tab;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> catalogueCellAt(Kind kind, float x, float y) {
    if (!showsCatalogue(kind)) {
        return std::nullopt;
    }
    const std::size_t capacity = catalogueCapacity();
    for (std::size_t i = 0; i < capacity; ++i) {
        const CatalogueCell cell = catalogueCell(i);
        // Only whole cells answer. A cell the card cuts in half has nowhere to
        // draw its icon either, so treating it as clickable would mean picking
        // something the player cannot see.
        if (cell.whole() && within(x, y, catalogueCellCentre(cell), kSlotHalf)) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<SlotHit> slotAt(Kind kind, float x, float y) {
    const Layout layout = layoutFor(kind);

    for (std::size_t i = 0; i < kInventorySlots; ++i) {
        if (within(x, y, gridSlotCentre(kind, i), kSlotHalf)) {
            return SlotHit{Region::Grid, i};
        }
    }
    for (std::size_t i = 0; i < slotsFor(kind); ++i) {
        if (within(x, y, craftSlotCentre(kind, layout, i), kSlotHalf)) {
            return SlotHit{Region::Craft, i};
        }
    }
    if (within(x, y, craftResultCentre(kind, layout), kSlotHalf)) {
        return SlotHit{Region::CraftResult, 0};
    }
    return std::nullopt;
}

bool insideCatalogueList(Kind kind, float x, float y) {
    if (!showsCatalogue(kind)) {
        return false;
    }
    const glm::vec2 topLeft = toBook(kCatalogueLeft, kCatalogueTop);
    const glm::vec2 bottomRight =
        toBook(kCatalogueLeft + kCatalogueColumns * kSlotPitchPixels, kCatalogueBottom);
    return x >= topLeft.x && x <= bottomRight.x && y >= topLeft.y && y <= bottomRight.y;
}

bool insidePanel(Kind kind, float x, float y) {
    if (y < -kPanelHalfHeight || y > kPanelHalfHeight) {
        // The tab strip hangs above the card and is still "inside" as far as
        // dropping a held stack is concerned.
        const float tabTop = -kPanelHalfHeight - kTabSheetSize.y * kPixel;
        if (!showsCatalogue(kind) || y < tabTop || y > -kPanelHalfHeight) {
            return false;
        }
    }
    const float offset = kPanelOffsetX(kind);
    if (x >= offset - kPanelHalfWidth && x <= offset + kPanelHalfWidth) {
        return true;
    }
    const float bookCentre = kBookCentrePixels * kPixel;
    return showsCatalogue(kind) && x >= bookCentre - kBookHalfWidth && x <= bookCentre + kBookHalfWidth;
}

engine::MeshData build(Kind kind, const Inventory& inventory, const ItemStack* craftSlots,
                       const ItemStack& craftResult, const ItemStack& heldStack, float cursorX, float cursorY,
                       float aspect, const CatalogueState& catalogue, const FurnaceProgress& progress) {
    engine::MeshData mesh;
    const Layout layout = layoutFor(kind);
    const float panelCentreX = kPanelOffsetX(kind);

    // Dims the world behind, so the panel reads as a layer over the game rather
    // than part of it.
    hud::appendQuad(mesh, 0.0f, 0.0f, aspect, 1.0f, kDimDepth, kDim, static_cast<float>(TextureLayer::White), false);

    // The whole panel is one sprite from the artwork. Rebuilding its frames,
    // bevels and icons out of primitives would mean redrawing by hand something
    // that already exists as a picture.
    hud::appendSprite(mesh, panelCentreX, 0.0f, kPanelHalfWidth, kPanelHalfHeight, kPanelDepth,
                      layout.panelPixelMin, kPanelPixelSize, hud::kSheetSize);

    std::vector<ItemId> catalogueList;
    if (showsCatalogue(kind)) {
        catalogueList = catalogueItems(catalogue.tab);
        appendCatalogue(mesh, catalogue, catalogueList);
    }

    for (std::size_t i = 0; i < kInventorySlots; ++i) {
        hud::appendStack(mesh, inventory.slot(i), gridSlotCentre(kind, i), kSlotHalf, kIconDepth, kCountDepth);
    }

    for (std::size_t i = 0; i < slotsFor(kind); ++i) {
        hud::appendStack(mesh, craftSlots[i], craftSlotCentre(kind, layout, i), kSlotHalf, kIconDepth,
                         kCountDepth);
    }
    hud::appendStack(mesh, craftResult, craftResultCentre(kind, layout), kSlotHalf, kIconDepth, kCountDepth);

    if (kind == Kind::Furnace) {
        // Both indicators are the lit sprite drawn over the spent one already in
        // the panel, cut down to how far along the furnace is. The flame burns
        // **downwards**, so it is clipped from the top; the arrow fills to the
        // right, so it is clipped from the right.
        if (progress.burn > 0.0f) {
            const float shown = std::min(progress.burn, 1.0f);
            const float hidden = kLitFlameSize.y * (1.0f - shown);
            const glm::vec2 pixelMin{kLitFlakeSheetMin.x, kLitFlakeSheetMin.y + hidden};
            const glm::vec2 pixelSize{kLitFlameSize.x, kLitFlameSize.y - hidden};
            const glm::vec2 topLeft = toScreen(kind, kFurnaceFlameArt.x, kFurnaceFlameArt.y + hidden);
            hud::appendSprite(mesh, topLeft.x + pixelSize.x * kPixel * 0.5f,
                              topLeft.y + pixelSize.y * kPixel * 0.5f, pixelSize.x * kPixel * 0.5f,
                              pixelSize.y * kPixel * 0.5f, kIconDepth, pixelMin, pixelSize, hud::kSheetSize);
        }
        if (progress.cook > 0.0f) {
            const float shown = std::min(progress.cook, 1.0f);
            const glm::vec2 pixelSize{kLitArrowSize.x * shown, kLitArrowSize.y};
            const glm::vec2 topLeft = toScreen(kind, kFurnaceArrowArt.x, kFurnaceArrowArt.y);
            hud::appendSprite(mesh, topLeft.x + pixelSize.x * kPixel * 0.5f,
                              topLeft.y + pixelSize.y * kPixel * 0.5f, pixelSize.x * kPixel * 0.5f,
                              pixelSize.y * kPixel * 0.5f, kIconDepth, kLitArrowSheetMin, pixelSize,
                              hud::kSheetSize);
        }
    }

    // Drawn last and nearest, so what the cursor carries is never behind a slot.
    hud::appendStack(mesh, heldStack, {cursorX, cursorY}, kSlotHalf, kHeldIconDepth, kHeldCountDepth);

    // Only with an empty cursor: while carrying a stack the pointer already has
    // something attached, and a label as well is noise over the slot you are
    // aiming at.
    if (heldStack.empty()) {
        const ItemStack* under = nullptr;
        ItemStack catalogueEntry;
        if (const std::optional<std::size_t> cell = catalogueCellAt(kind, cursorX, cursorY);
            cell.has_value() && *cell < catalogueList.size()) {
            catalogueEntry = ItemStack{catalogueList[*cell], 1};
            under = &catalogueEntry;
        } else if (const std::optional<SlotHit> hover = slotAt(kind, cursorX, cursorY); hover.has_value()) {
            switch (hover->region) {
            case Region::Grid:
                under = &inventory.slot(hover->index);
                break;
            case Region::Craft:
                under = &craftSlots[hover->index];
                break;
            case Region::CraftResult:
                under = &craftResult;
                break;
            default:
                break;
            }
        }
        if (under != nullptr && !under->empty()) {
            hud::appendTooltip(mesh,
                               isSpawnEgg(under->item) ? spawnEggName(under->item) : itemDisplayName(under->item),
                               cursorX, cursorY, aspect, kTooltipTextHeight, kTooltipDepth);
        }
    }

    return mesh;
}

} // namespace game::inventoryScreen
