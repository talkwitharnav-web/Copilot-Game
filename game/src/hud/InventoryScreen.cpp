#include "hud/InventoryScreen.hpp"

#include "hud/HudPrimitives.hpp"
#include "item/Recipe.hpp"
#include "world/Creature.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>

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
/// The search field, front to back: its border, its recess, then the text and
/// caret on the ordinary label band. All four sit between the cell and the
/// icons, so nothing on the card can cover them.
constexpr float kSearchBorderDepth = 0.003570f;
constexpr float kSearchFieldDepth = 0.003560f;
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

// **Three bands hold an icon and its own marks, and each one has to clear the
// icon's reach rather than merely be a smaller number.** `appendBlockIcon`
// spends `hud::kIconDepthSpan` in front of whatever depth it is handed for any
// block drawn as boxes, and a count's shadow spends `hud::kFontShadowDepth`
// behind its own - so the real gap is smaller than these lines look.
//
// The catalogue's is the tight one at 6e-5, and it is tight on purpose: 0.0034
// is held free above it for the scrollbar. **Downstream of five blocks moving
// from flat sprites to models this session** - a flat sprite spends none of the
// icon budget and a model spends all of it, so this band went from unused to
// nearly spent without a line of it changing.
static_assert(hud::iconStaysBehindItsMarks(kCatalogueIconDepth, kCatalogueCountDepth),
              "a catalogue icon now reaches in front of its own count - the free 0.0034 band "
              "above is where to take the room from");
static_assert(hud::iconStaysBehindItsMarks(kIconDepth, kCountDepth),
              "a grid icon now reaches in front of its own stack count and durability bar");
static_assert(hud::iconStaysBehindItsMarks(kHeldIconDepth, kHeldCountDepth),
              "the icon on the cursor now reaches in front of its own stack count");
static_assert(kTooltipDepth < kHeldCountDepth,
              "the tooltip must stay nearer than the stack on the cursor - a label anything can "
              "cover is not a label");

/// **The hover highlight straddles the item it marks**, exactly as the
/// reference's two sprites do: `back` behind the icon so a full slot still
/// reads as full, `front` over the icon so the brightening lands on the picture
/// rather than only in the gap around it.
constexpr float kSlotHighlightBackDepth = 0.00305f;
constexpr float kSlotHighlightFrontDepth = 0.0020f;
static_assert(kSlotHighlightBackDepth < kCellDepth && kSlotHighlightBackDepth > kIconDepth,
              "the back half of the hover highlight has to land between the cell art and the "
              "icon - in front of the cell so it is visible at all, behind the icon so it never "
              "washes out the very item it is marking");
static_assert(kSlotHighlightFrontDepth < kCountDepth - hud::kDecorationSpan,
              "the front half of the hover highlight has to clear the stack count and durability "
              "bar, which reach kDecorationSpan in front of the depth they are handed - clearing "
              "kCountDepth alone would leave it behind the marks it is meant to lie over");
static_assert(kSlotHighlightFrontDepth > kHeldIconDepth,
              "the stack on the cursor has to cover the hover highlight, not the other way round");

constexpr glm::vec4 kDim{0.0f, 0.0f, 0.0f, 0.55f};
constexpr glm::vec4 kCount{1.0f, 1.0f, 1.0f, 1.0f};
constexpr glm::vec4 kLabel{0.24f, 0.24f, 0.24f, 1.0f};
/// The search field. A pale border round a black recess with white text on it
/// is the reference's own text box, and the contrast is the whole point: the
/// first cut drew grey text on a mid-grey slab and could barely be read.
constexpr glm::vec4 kSearchBorder{0.66f, 0.66f, 0.66f, 1.0f};
/// Brighter while it has the keyboard, which is the reference's own cue and the
/// only thing on the card that says where your typing is going.
constexpr glm::vec4 kSearchBorderLit{1.0f, 1.0f, 1.0f, 1.0f};
constexpr glm::vec4 kSearchBack{0.02f, 0.02f, 0.02f, 1.0f};
constexpr glm::vec4 kSearchText{1.0f, 1.0f, 1.0f, 1.0f};
/// What the field says when nothing has been typed.
constexpr glm::vec4 kSearchHint{0.42f, 0.42f, 0.42f, 1.0f};
/// Unselected tabs and their icons are knocked back rather than recoloured.
constexpr glm::vec4 kUnselectedTint{0.72f, 0.72f, 0.72f, 1.0f};

/// Where each panel lives on the combined HUD sheet, in pixels. Both are the
/// same size; the crafting table's is generated from the inventory's by
/// `tools/make-hud-sheet.ps1`, which is why their cells match exactly.
constexpr glm::vec2 kPanelPixelSize{176.0f, 166.0f};
constexpr glm::vec2 kInventoryPanelMin{0.0f, 41.0f};
constexpr glm::vec2 kCraftingPanelMin{0.0f, 207.0f};
constexpr glm::vec2 kFurnacePanelMin{0.0f, 373.0f};
constexpr glm::vec2 kSmithingPanelMin{0.0f, 793.0f};
constexpr glm::vec2 kChestPanelMin{0.0f, 959.0f};
constexpr glm::vec2 kDoubleChestPanelMin{0.0f, 1125.0f};
/// Appended after the status strip, which is why its offset is the largest
/// rather than following the chests it sits beside in this list.
constexpr glm::vec2 kHopperPanelMin{0.0f, 1355.0f};
constexpr glm::vec2 kStonecutterPanelMin{0.0f, 1521.0f};

// **A wrong sheet offset does not fail, it silently draws whatever sits there.**
// The stonecutter is the last thing on the sheet, so its bottom edge is the
// sheet's - which turns "every offset above me is still right" into one
// arithmetic statement the compiler can check. The single edit that breaks it
// is adding a strip to `tools/make-hud-sheet.ps1` without carrying the new
// height into `hud::kSheetSize`.
static_assert(kStonecutterPanelMin.y + kPanelPixelSize.y == hud::kSheetSize.y,
              "the stonecutter panel is the last thing on the sheet, so it must end exactly at the "
              "sheet's bottom edge");

/// The hopper's one row: five cells on the usual pitch, centred in the card, so
/// the run starts at 43 and the first centre lands on 52.
constexpr float kHopperFirstCentreX = 52.0f;
constexpr float kHopperCentreY = 43.0f;

/// The chest's own three rows, in the panel's pixels. The reference's chest GUI
/// puts them at 17, 35 and 53 - which is where our crafting area already sits,
/// so the panel is the inventory's with the top cleared and this stamped in.
constexpr float kChestFirstCentreY = 26.0f;

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
/// second copy of it - six times over, once per background state, written by
/// `tools/make-hud-sheet.ps1` from that same 18x18 tile so the bevels match to
/// the pixel. An 18-wide cell on a 20-pixel pitch leaves a two-pixel guard so
/// mip generation cannot bleed one state into its neighbour.
constexpr glm::vec2 kCellSheetSize{18.0f, 18.0f};
constexpr float kCellSheetPitch = 20.0f;
constexpr float kCellSheetTop = 773.0f;

/// What a catalogue cell's background says.
///
/// **Four, where `INTERFACE.md` S3.1 describes six.** Three of vanilla's six are
/// expandable groups, which are slice 9 and do not exist. The fourth is a
/// *selected recipe*, and that one was built and taken back out: nothing sets
/// it - slice 8 fills the grid on the click rather than remembering it - so it
/// was a highlight that meant nothing and stayed lit behind an item already
/// taken. What is left is the answer this slice exists to give, plus the cell
/// under the pointer.
///
/// Order is the order the strip is written in, and nothing else may reorder it.
enum class CellState : std::uint8_t {
    Craftable,
    CraftableHovered,
    Uncraftable,
    UncraftableHovered,
    Count,
};

constexpr glm::vec2 cellSheetMin(CellState state) {
    return {static_cast<float>(state) * kCellSheetPitch, kCellSheetTop};
}


/// The lit indicators, kept off the panel so they can be drawn over the spent
/// versions baked into it and cut off part way.
///
/// **The flame sat one row too high.** `ui-screens.json`
/// `ours.strips.indicators.parts.flame_lit` gives y 540, and measuring
/// `assets/textures/hud.png` agrees: rows 540-553 carry the flame and row 539
/// is fully transparent, as is row 554. A row of nothing at the top and a
/// missing row at the bottom is exactly what a furnace's flame burning down
/// looks like when it is nearly out, so the symptom hides in the animation.
/// The arrow beside it was already right.
constexpr glm::vec2 kLitFlakeSheetMin{0.0f, 540.0f};
constexpr glm::vec2 kLitFlameSize{14.0f, 14.0f};
constexpr glm::vec2 kLitArrowSheetMin{16.0f, 540.0f};
constexpr glm::vec2 kLitArrowSize{21.0f, 15.0f};

/// Where those indicators sit on the furnace panel, in its own pixels.
constexpr glm::vec2 kFurnaceFlameArt{55.0f, 36.0f};
constexpr glm::vec2 kFurnaceArrowArt{79.0f, 36.0f};

/// One art pixel in screen units - **`hud::kArtPixel`, and nothing local**.
///
/// Every position below is measured off the artwork in its own pixels and
/// scaled through this, so the layout cannot drift away from the image it was
/// taken from.
///
/// This file used to derive its own, from a panel height of 1.30 over 166 art
/// pixels, which came out 4.19% larger than the hotbar's. Both were live at
/// once: every container screen was drawn 4.19% bigger than the bar it
/// replaces, and because the hotbar is hidden while a screen is open, the eye
/// only ever sees one of them at a time. `UI.md` R1 settles it on the hotbar,
/// which is the thing that is on screen the rest of the time.
constexpr float kPixel = hud::kArtPixel;

/// The double chest carries three extra rows of nine, so its panel is 54 art
/// pixels taller and everything below those rows moves down by the same.
constexpr float kDoubleChestExtraPixels = 3.0f * 18.0f;

constexpr float panelPixelHeight(Kind kind) {
    return kPanelPixelSize.y + (kind == Kind::DoubleChest ? kDoubleChestExtraPixels : 0.0f);
}

/// How far the player's own rows are pushed down by a taller container above.
constexpr float storageShiftY(Kind kind) {
    return kind == Kind::DoubleChest ? kDoubleChestExtraPixels : 0.0f;
}

/// Measured from the art: slot borders run 7, 25, 43 ... 169 across, the three
/// storage rows begin at y 83, and the separated hotbar row at y 141.
constexpr float kSlotPitchPixels = 18.0f;
constexpr float kFirstSlotCentreX = 16.0f;
constexpr float kStorageTopCentreY = 92.0f;
constexpr float kHotbarCentreY = 150.0f;

/// **The armour column, measured off `assets/textures/hud.png` and not guessed
/// from the reference's coordinates.**
///
/// The four cells sit at art top-left (7, 7), (7, 25), (7, 43) and (7, 61) -
/// the same x as the storage grid, on the same 18-pixel pitch, starting 76
/// pixels above it. Found by a detector that scans the panel for the 18x18 cell
/// signature (dark border along the top and left, white highlight along the
/// bottom and right, interior grey), and **the control is that the same
/// detector reports the storage rows at centre y 92, 110 and 128 and the hotbar
/// at 150** - the three constants above, which it had never been told. It also
/// finds the 2x2 at (106, 26)/(124, 26)/(106, 44)/(124, 44) and the result at
/// (162, 36), so its answers are non-uniform and it is measuring the sheet
/// rather than echoing one number.
///
/// Our panel sits one pixel left of the reference's - `tools/make-hud-sheet.ps1`
/// says so explicitly - which is exactly why this is measured. Porting the
/// reference's x 8 would have put every armour cell one pixel out, in the
/// direction nothing would notice until a click near the edge missed.
constexpr float kArmourTopCentreY = 16.0f;
static_assert(kArmourTopCentreY + 3.0f * kSlotPitchPixels + kSlotPitchPixels * 0.5f <
                  kStorageTopCentreY - kSlotPitchPixels * 0.5f,
              "the armour column must clear the storage rows, or two regions answer for one "
              "point and which one wins is whichever loop slotAt happens to run first");

constexpr float kSlotHalf = kSlotPitchPixels * kPixel * 0.5f;

/// **The hover highlight, measured out of the reference rather than guessed.**
/// `gui/sprites/container/slot_highlight_back.png` and `slot_highlight_front.png`
/// are both 24x24 nine-slice with a border of 4, and all 320 border pixels are
/// fully transparent - the only ink in either is a 16x16 core of flat white,
/// alpha 96 on the back sheet and 32 on the front. **So the sprite carries no
/// shape at all**, and reproducing it needs no art: it is two plain quads over
/// the slot's interior. That is what let this be fixed in code without touching
/// `tools/make-hud-sheet.ps1`, which an earlier reading had assumed was needed.
constexpr float kSlotHighlightPixels = 16.0f;
static_assert(kSlotHighlightPixels == kSlotPitchPixels - 2.0f,
              "the highlight covers a slot's interior, which is the pitch less its one-pixel "
              "border on each side - if the pitch ever moves, this is the line that catches it");
constexpr float kSlotHighlightHalf = kSlotHighlightPixels * kPixel * 0.5f;
constexpr glm::vec4 kSlotHighlightBack{1.0f, 1.0f, 1.0f, 96.0f / 255.0f};
constexpr glm::vec4 kSlotHighlightFront{1.0f, 1.0f, 1.0f, 32.0f / 255.0f};
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

// The fold between the two cards is the one measurement the eye checks, and it
// is the *difference* of two derived centres rather than either of them - so
// both can be wrong by the same amount and still look plausible. **The single
// edit that breaks this is writing either card's centre down as a literal**
// instead of deriving it from the block.
static_assert(kInventoryShiftPixels - kPanelPixelSize.x * 0.5f -
                      (kBookCentrePixels + kBookPixelSize.x * 0.5f) ==
                  kCardGapPixels,
              "the catalogue and the inventory card must be exactly kCardGapPixels apart");

constexpr float kPanelOffsetX(Kind kind) {
    return showsCatalogue(kind) ? kInventoryShiftPixels * kPixel : 0.0f;
}

constexpr float kPanelHalfWidth = kPanelPixelSize.x * kPixel * 0.5f;
constexpr float panelHalfHeight(Kind kind) {
    return panelPixelHeight(kind) * kPixel * 0.5f;
}
constexpr float kBookHalfWidth = kBookPixelSize.x * kPixel * 0.5f;
/// The catalogue card never grows - only the inventory card has taller
/// variants - so it is measured from **its own** art rather than from the
/// inventory panel's height. The two happen to be the same 166 pixels tall,
/// which is precisely why reading the wrong one costs nothing until somebody
/// redraws one of them.
constexpr float kBookHalfHeight = kBookPixelSize.y * kPixel * 0.5f;

// The two cards are drawn as one centred block and share a vertical centre, so
// the day they stop being the same height is the day the fold between them
// stops lining up. **The single edit that breaks this is redrawing either card
// without the other**, in `tools/make-hud-sheet.ps1`.
static_assert(kBookPixelSize.y == kPanelPixelSize.y,
              "the catalogue and the inventory card are drawn as one block about a shared centre, "
              "so they must be the same height");

/// Art pixel to screen space, for the inventory card. The card is centred
/// vertically; horizontally it is wherever the layout block puts it, and
/// **this is the only place that offset is applied** - every slot centre, hit
/// test and panel bound comes through here, so the two cannot drift apart.
constexpr glm::vec2 toScreen(Kind kind, float artX, float artY) {
    return {(artX - kPanelPixelSize.x * 0.5f) * kPixel + kPanelOffsetX(kind),
            (artY - panelPixelHeight(kind) * 0.5f) * kPixel};
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

/// The search field, in the card's own pixels. It replaces the tab-name row, so
/// it is as wide as the grid below it. **One owner**, because the hit test that
/// focuses it and the geometry that draws it have to agree exactly.
constexpr float kSearchFieldWidth = kCatalogueColumns * kSlotPitchPixels;
constexpr float kSearchFieldHeight = 13.0f;
constexpr float kSearchBorderPixels = 1.0f;
constexpr float kSearchTextHeight = 8.0f;

/// How much room the text actually has: the field, less its border on both
/// sides, less the same two-pixel inset the text is drawn at, less one pixel
/// for the caret that follows it.
constexpr float kSearchTextInset = 2.0f;
constexpr float kSearchTextWidth =
    kSearchFieldWidth - 2.0f * (kSearchBorderPixels + kSearchTextInset) - 1.0f;

/// The caret at its far right still lands inside the field's inset box, and
/// lands exactly as far from the right border as the text starts from the left.
///
/// The caret is one art pixel wide, drawn centred half a pixel past its offset,
/// and its offset can reach `kSearchTextWidth` - so its right edge is
/// `inset + kSearchTextWidth + 1`. **The single edit that makes this fail is
/// dropping the `- 1.0f` above**, which is the caret's own column: the text
/// would then fill the box to the border and the caret would be drawn on top of
/// it, which is the state this field shipped in before it had one.
static_assert(kSearchBorderPixels + kSearchTextInset + kSearchTextWidth + 1.0f ==
                  kSearchFieldWidth - (kSearchBorderPixels + kSearchTextInset),
              "the caret's rightmost column must sit inside the field, mirroring the left inset");

/// What the field shows, and where the caret sits inside it.
///
/// **The visible window follows the caret, not the end of the string.** The
/// first version of this trimmed to the tail, which is right while you are
/// typing and wrong the moment an arrow key exists: walking the caret left
/// would have taken it out through the left-hand edge of the box, drawn over
/// the card, while the text under it never moved. A window that can only ever
/// show the end is not a text field, it is a log.
///
/// Front-trimmed rather than back-trimmed when there is a choice, because the
/// end of a query is the part you are still typing. Measured through
/// `hud::textWidth` throughout - the font is proportional, so a character count
/// is not a width, which is exactly why the caller's 22-character cap never
/// stopped this overflowing on its own.
struct SearchView {
    std::string_view text;
    /// From the field's text origin to the caret, in screen units.
    float caretOffset;
};

SearchView searchView(std::string_view query, std::size_t caret, float textHeight, float available) {
    // **The renderer does not trust `CatalogueState::caret`.** `build` takes
    // that struct by const reference and so cannot repair it, and `query` is a
    // public member that code with no idea a caret exists can shorten - so the
    // clamp happens again here, where a stale index would otherwise be an
    // out-of-bounds `substr` rather than a cosmetic slip.
    caret = std::min(caret, query.size());

    // Scroll right until the caret is inside the window. This is the half that
    // makes the trim follow the caret.
    std::size_t first = 0;
    while (first < caret && hud::textWidth(query.substr(first, caret - first), textHeight) > available) {
        ++first;
    }

    // Then show as much as fits from there. This cannot push the caret back out
    // of the right-hand edge: the text up to the caret already fits by the loop
    // above, so this one can never trim past it.
    std::size_t count = query.size() - first;
    while (count > 0 && hud::textWidth(query.substr(first, count), textHeight) > available) {
        --count;
    }

    return SearchView{query.substr(first, count),
                      hud::textWidth(query.substr(first, caret - first), textHeight)};
}

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
    const float shift = storageShiftY(kind);

    if (index < kHotbarSlots) {
        return toScreen(kind, artX, kHotbarCentreY + shift);
    }
    const auto row = static_cast<float>(index / kHotbarSlots) - 1.0f;
    return toScreen(kind, artX, kStorageTopCentreY + shift + row * kSlotPitchPixels);
}

/// Where one of the four worn pieces sits, in `ArmourSlot`'s own order - head,
/// chest, legs, feet, top to bottom, which is the order the art draws them in
/// and the order `Inventory::armourAt` indexes.
///
/// **No `storageShiftY`, deliberately.** That shift exists for the double
/// chest's three extra rows, and the double chest has no armour column at all -
/// `armourSlotCount` answers 0 for it. Adding the shift here would be a
/// derivation that only ever fires on a screen this never runs on, which is a
/// worse kind of dead code than none.
glm::vec2 armourSlotCentre(Kind kind, std::size_t index) {
    return toScreen(kind, kFirstSlotCentreX,
                    kArmourTopCentreY + static_cast<float>(index) * kSlotPitchPixels);
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
    if (kind == Kind::SmithingTable) {
        // Side by side rather than stacked: a tool goes in and the material
        // beside it, which is how the reference reads. The grindstone and the
        // brewing stand borrow the same two slots - all three ask the same
        // shape of question.
        return Layout{kSmithingPanelMin, 2, {45.0f, 43.0f}, {131.0f, 43.0f}};
    }
    if (kind == Kind::Chest || kind == Kind::DoubleChest) {
        return Layout{kind == Kind::Chest ? kChestPanelMin : kDoubleChestPanelMin, 0,
                      {16.0f, kChestFirstCentreY}, {0.0f, 0.0f}};
    }
    if (kind == Kind::Hopper) {
        return Layout{kHopperPanelMin, 0, {kHopperFirstCentreX, kHopperCentreY}, {0.0f, 0.0f}};
    }
    if (kind == Kind::Stonecutter) {
        // The input on the left, the first cut where the result normally sits;
        // the other two follow it on the usual pitch.
        return Layout{kStonecutterPanelMin, 0, {28.0f, 43.0f}, {94.0f, 43.0f}};
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
    // **A screen with no grid still has slots**, and dividing by its grid width
    // is an integer divide by zero — which on x86 is a hardware fault, not a
    // wrong answer. The stonecutter has one input and `craftSize` 0, so opening
    // it killed the process outright. A gridless screen lays its slots out in a
    // single row.
    const int width = layout.craftSize > 0 ? layout.craftSize : 1;
    const auto column = static_cast<float>(index % static_cast<std::size_t>(width));
    const auto row = static_cast<float>(index / static_cast<std::size_t>(width));
    return toScreen(kind, layout.craftFirstCentre.x + column * kSlotPitchPixels,
                    layout.craftFirstCentre.y + row * kSlotPitchPixels);
}

/// The furnace's and the smithing table's own slot counts, neither of which is
/// a square grid.
constexpr std::size_t slotsFor(Kind kind) {
    if (kind == Kind::Stonecutter) {
        return 1u;
    }
    return (kind == Kind::Furnace || kind == Kind::SmithingTable) ? 2u : craftSlotCount(kind);
}

/// Where the nth offered cut sits. Only the stonecutter has more than one
/// result, which is why every other screen asks for index 0 and gets the
/// layout's single position back.
glm::vec2 resultSlotCentre(Kind kind, const Layout& layout, std::size_t index) {
    return toScreen(kind, layout.craftResultCentre.x +
                              static_cast<float>(index) * kSlotPitchPixels,
                    layout.craftResultCentre.y);
}

/// Where one of a chest's twenty-seven slots sits, on the same nine-wide pitch
/// the storage rows below it use.
glm::vec2 chestSlotCentre(Kind kind, std::size_t index) {
    if (kind == Kind::Hopper) {
        return toScreen(kind, kHopperFirstCentreX + static_cast<float>(index) * kSlotPitchPixels,
                        kHopperCentreY);
    }
    const auto column = static_cast<float>(index % kHotbarSlots);
    const auto row = static_cast<float>(index / kHotbarSlots);
    return toScreen(kind, kFirstSlotCentreX + column * kSlotPitchPixels,
                    kChestFirstCentreY + row * kSlotPitchPixels);
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
/// **through** a cell rather than dropping it. The cell backgrounds do that
/// arithmetically - a shorter quad with a correspondingly shorter slice of the
/// sprite - because an isometric icon is three quads on a cube and no rectangle
/// can clip one. The icons themselves go into `Renderer::setClippedScreenMesh`,
/// which sets a real scissor.
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

/// How many rows the card shows *whole*. The clipped one is deliberately not
/// counted, so scrolling can always bring every entry up into a whole row -
/// which is exactly how the bucket arrived and could not be seen.
///
/// **It does draw, and it does answer a click**, which this comment used to
/// deny on both counts. `catalogueCapacity` counts every cell `catalogueCell`
/// calls visible, the clipped row included, and `catalogueCellAt` hit-tests
/// that row against the height the scissor leaves it. A reader who believed the
/// old wording would have "corrected" the capacity and deleted a working row of
/// icons; the note predates the scissor that made both true.
std::size_t catalogueWholeRows() {
    std::size_t rows = 0;
    while (catalogueCell(rows * static_cast<std::size_t>(kCatalogueColumns)).whole()) {
        ++rows;
    }
    return rows;
}

glm::vec2 catalogueCellCentre(const CatalogueCell& cell) {
    return toBook(cell.artTopLeft.x + kSlotPitchPixels * 0.5f, cell.artTopLeft.y + kSlotPitchPixels * 0.5f);
}

constexpr const char* tabName(CatalogueTab tab) {
    return tab == CatalogueTab::Search ? "Search" : categoryName(static_cast<ItemCategory>(tab));
}

void appendCatalogue(engine::MeshData& mesh, engine::MeshData& clipped, const CatalogueState& state,
                     const std::vector<ItemId>& shown, const std::unordered_set<ItemId>& craftable,
                     std::optional<std::size_t> hovered, bool everythingReachable) {
    hud::appendSprite(mesh, kBookCentrePixels * kPixel, 0.0f, kBookHalfWidth, kBookHalfHeight, kPanelDepth,
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
    if (state.tab == CatalogueTab::Search) {
        // The tab-name row becomes the field. Nothing else on the card has room
        // for one, and a heading reading "Search" over a search box says nothing
        // the tab strip has not already said.
        const glm::vec2 centre = toBook(kCatalogueLeft + kSearchFieldWidth * 0.5f, kCatalogueLabelY);
        const float white = static_cast<float>(TextureLayer::White);

        hud::appendQuad(mesh, centre.x, centre.y, kSearchFieldWidth * kPixel * 0.5f,
                        kSearchFieldHeight * kPixel * 0.5f, kSearchBorderDepth,
                        state.searchFocused ? kSearchBorderLit : kSearchBorder, white, false);
        hud::appendQuad(mesh, centre.x, centre.y,
                        (kSearchFieldWidth - 2.0f * kSearchBorderPixels) * kPixel * 0.5f,
                        (kSearchFieldHeight - 2.0f * kSearchBorderPixels) * kPixel * 0.5f, kSearchFieldDepth,
                        kSearchBack, white, false);

        const float textLeft = label.x + (kSearchBorderPixels + kSearchTextInset) * kPixel;
        const float textHeight = kSearchTextHeight * kPixel;

        // **What fits, and where the caret is inside it.** A text field
        // scrolls: while you are typing you have to see what you have just
        // typed, so the head slides out of sight and the tail stays - but the
        // moment arrow keys exist the window has to follow the *caret*, not the
        // end, or walking left runs the caret out through the border while the
        // text underneath it sits still.
        //
        // Nothing clipped this at all. The field is 122 art pixels of usable
        // room and the caller lets 22 characters in, which at 6 pixels apiece
        // is 132 - so a long query ran out through the border, across the
        // catalogue's own tab strip and off the card, and the caret went with
        // it. A cap in the caller is not a fix, because the glyphs are
        // proportional and 22 of them are not a fixed width.
        const SearchView view = searchView(state.query, state.caret, textHeight, kSearchTextWidth * kPixel);

        // The hint is what the field says when it is doing nothing. Clicking
        // into it is a statement that you are about to type, so it goes.
        const bool showHint = state.query.empty() && !state.searchFocused;
        if (showHint) {
            hud::appendText(mesh, "Search", textLeft, centre.y, textHeight, kLabelDepth, kSearchHint);
        } else if (!view.text.empty()) {
            hud::appendText(mesh, view.text, textLeft, centre.y, textHeight, kLabelDepth, kSearchText, true);
        }

        // A solid bar rather than an underscore, so it reads at this size, and
        // **placed from the same measurement that chose the window** - with the
        // field empty the caret belongs at the start, not after the hint
        // standing in for it, and mid-string it belongs between two glyphs
        // rather than after the last one drawn. Only while the field is
        // focused, and only on the frames the caller says it is on: a caret
        // that does not blink looks like part of the text.
        //
        // `caretOffset` is measured from the same `first` character
        // `view.text` starts at, so the two cannot disagree about where the
        // window begins. The single edit that breaks this is returning the
        // offset from the whole query instead of from the visible slice.
        if (state.searchFocused && state.caretVisible) {
            const float caretX = textLeft + view.caretOffset + kPixel * 0.5f;
            hud::appendQuad(mesh, caretX, centre.y, kPixel * 0.5f, textHeight * 0.5f, kLabelDepth,
                            kSearchText, white, false);
        }
    } else {
        hud::appendText(mesh, tabName(state.tab), label.x, label.y, 8.0f * kPixel, kLabelDepth, kLabel);
    }

    const std::size_t capacity = catalogueCapacity();
    const auto columns = static_cast<std::size_t>(kCatalogueColumns);
    const auto first = static_cast<std::size_t>(std::max(state.scrollRow, 0)) * columns;

    // Only rows that hold something are drawn. A row of empty cells below the
    // last entry reads as "the list goes on" when it does not, and the bin does
    // not need them - `insideCatalogueList` covers the whole rectangle whether
    // or not there is a cell under the pointer.
    const std::size_t remaining = first < shown.size() ? shown.size() - first : 0;
    const std::size_t cells = std::min(capacity, (remaining + columns - 1) / columns * columns);

    for (std::size_t i = 0; i < cells; ++i) {
        const CatalogueCell cell = catalogueCell(i);
        const float height = cell.visiblePixels;
        const glm::vec2 centre =
            toBook(cell.artTopLeft.x + kSlotPitchPixels * 0.5f, cell.artTopLeft.y + height * 0.5f);

        // An entry past the end of the list keeps the pale cell: it is the
        // neutral slot rather than a claim that nothing can be made there.
        CellState cellState = CellState::Craftable;
        if (first + i < shown.size()) {
            const ItemId item = shown[first + i];
            const bool affordable = everythingReachable || craftable.count(item) != 0;
            const bool under = hovered.has_value() && *hovered == first + i;
            if (affordable) {
                cellState = under ? CellState::CraftableHovered : CellState::Craftable;
            } else {
                cellState = under ? CellState::UncraftableHovered : CellState::Uncraftable;
            }
        }
        hud::appendSprite(mesh, centre.x, centre.y, kSlotPitchPixels * kPixel * 0.5f, height * kPixel * 0.5f,
                          kCellDepth, cellSheetMin(cellState), {kCellSheetSize.x, height}, hud::kSheetSize);

        // Full size, in the cell it belongs to. The row the card cuts through
        // is cut for real by the renderer's scissor rather than shrunk to fit,
        // so a half-visible entry looks like the reference's - sliced at the
        // frame - instead of like a smaller item.
        if (first + i < shown.size()) {
            hud::appendStack(clipped, ItemStack{shown[first + i], 1}, catalogueCellCentre(cell), kSlotHalf,
                             kCatalogueIconDepth, kCatalogueCountDepth);
        }
    }
}

} // namespace

std::vector<ItemId> catalogueItems(const CatalogueState& catalogue) {
    const CatalogueTab tab = catalogue.tab;
    const std::string_view query = catalogue.query;
    // Matching at the start of a word rather than anywhere in the name is what
    // keeps a two-letter query useful: "st" finds Stone and Stone Stairs and
    // leaves Sandstone out.
    const auto lower = [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    };
    std::string needle;
    if (tab == CatalogueTab::Search) {
        for (const char c : query) {
            needle.push_back(lower(c));
        }
    }
    const auto matches = [&](ItemId item) {
        if (needle.empty()) {
            return true;
        }
        const std::string_view name = displayNameOf(item);
        for (std::size_t start = 0; start + needle.size() <= name.size(); ++start) {
            if (start != 0 && name[start - 1] != ' ') {
                continue;
            }
            bool same = true;
            for (std::size_t i = 0; i < needle.size() && same; ++i) {
                same = lower(name[start + i]) == needle[i];
            }
            if (same) {
                return true;
            }
        }
        return false;
    };

    std::vector<ItemId> shown;
    for (const ItemId item : allItems()) {
        // The recipe book, when there is one. Applied before the tab and the
        // search so every tab is a view of the same book.
        if (catalogue.restrictToKnown && catalogue.known.count(item) == 0) {
            continue;
        }
        if (tab == CatalogueTab::Search) {
            if (matches(item)) {
                shown.push_back(item);
            }
        } else if (categoryFor(item) == static_cast<ItemCategory>(tab)) {
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

std::optional<std::size_t> catalogueCellAt(Kind kind, float x, float y, int scrollRow) {
    if (!showsCatalogue(kind)) {
        return std::nullopt;
    }
    const std::size_t capacity = catalogueCapacity();
    const auto first = static_cast<std::size_t>(std::max(scrollRow, 0)) *
                       static_cast<std::size_t>(kCatalogueColumns);
    for (std::size_t i = 0; i < capacity; ++i) {
        const CatalogueCell cell = catalogueCell(i);
        // Anything drawn can be clicked, including a cut-through cell - which
        // is hit-tested against the part of it the card actually shows, so the
        // target matches what the eye sees.
        const glm::vec2 centre = toBook(cell.artTopLeft.x + kSlotPitchPixels * 0.5f,
                                        cell.artTopLeft.y + cell.visiblePixels * 0.5f);
        const glm::vec2 half{kSlotHalf, kSlotHalf * (cell.visiblePixels / kSlotPitchPixels)};
        if (std::abs(x - centre.x) <= half.x && std::abs(y - centre.y) <= half.y) {
            return first + i;
        }
    }
    return std::nullopt;
}

int catalogueMaxScroll(std::size_t itemCount) {
    const auto columns = static_cast<std::size_t>(kCatalogueColumns);
    const std::size_t rowsNeeded = (itemCount + columns - 1) / columns;
    const std::size_t whole = catalogueWholeRows();
    return rowsNeeded > whole ? static_cast<int>(rowsNeeded - whole) : 0;
}

bool insideSearchField(Kind kind, float x, float y) {
    if (!showsCatalogue(kind)) {
        return false;
    }
    const glm::vec2 centre = toBook(kCatalogueLeft + kSearchFieldWidth * 0.5f, kCatalogueLabelY);
    return std::abs(x - centre.x) <= kSearchFieldWidth * kPixel * 0.5f &&
           std::abs(y - centre.y) <= kSearchFieldHeight * kPixel * 0.5f;
}

std::optional<SlotHit> slotAt(Kind kind, float x, float y) {
    const Layout layout = layoutFor(kind);

    for (std::size_t i = 0; i < kInventorySlots; ++i) {
        if (within(x, y, gridSlotCentre(kind, i), kSlotHalf)) {
            return SlotHit{Region::Grid, i};
        }
    }
    // **The four cells down the left, and the reason this was the last link in
    // the armour chain.** `Region::Armour` existed as an enumerator and as two
    // `case` labels, and was constructed by nothing anywhere in the repository -
    // so `Inventory::armourSet()` could only ever answer `kNoArmour`, and the
    // whole reduction curve, the durability charge and thirteen per-source
    // verdicts were correct, asserted and unreachable. `CLAUDE.md` bug shape
    // #15.
    //
    // Gated through `armourSlotCount` rather than on `kind` directly, so this
    // and `build` ask the same question of the same owner.
    for (std::size_t i = 0; i < armourSlotCount(kind); ++i) {
        if (within(x, y, armourSlotCentre(kind, i), kSlotHalf)) {
            return SlotHit{Region::Armour, i};
        }
    }
    if (isContainer(kind)) {
        for (std::size_t i = 0; i < chestSlotCount(kind); ++i) {
            if (within(x, y, chestSlotCentre(kind, i), kSlotHalf)) {
                return SlotHit{Region::Chest, i};
            }
        }
        // A container has no crafting slots and no result, so nothing below
        // applies.
        return std::nullopt;
    }
    for (std::size_t i = 0; i < slotsFor(kind); ++i) {
        if (within(x, y, craftSlotCentre(kind, layout, i), kSlotHalf)) {
            return SlotHit{Region::Craft, i};
        }
    }
    for (std::size_t i = 0; i < resultSlotCount(kind); ++i) {
        if (within(x, y, resultSlotCentre(kind, layout, i), kSlotHalf)) {
            return SlotHit{Region::CraftResult, i};
        }
    }
    return std::nullopt;
}

/// The screen centre of a slot `slotAt` has just reported, for the hover
/// highlight to sit on.
///
/// **Deliberately built from the same four centre functions `slotAt` tests**,
/// rather than from a second set of coordinates that happen to agree today. A
/// highlight that can drift from the region which produced it is exactly the
/// "one thing drawn in two places" fault this project has paid for repeatedly;
/// deriving both from one owner makes the drift impossible rather than merely
/// unlikely, and costs nothing here because the functions already exist.
std::optional<glm::vec2> hoveredSlotCentre(Kind kind, const Layout& layout, const SlotHit& hit) {
    switch (hit.region) {
    case Region::Grid:
        return gridSlotCentre(kind, hit.index);
    case Region::Chest:
        return chestSlotCentre(kind, hit.index);
    case Region::Craft:
        return craftSlotCentre(kind, layout, hit.index);
    case Region::CraftResult:
        return resultSlotCentre(kind, layout, hit.index);
    case Region::Armour:
        return armourSlotCentre(kind, hit.index);
    // **No `default:`**, for the same reason the tooltip switch gives further
    // down: these three have no geometry of their own today, and naming them is
    // what makes the compiler point at this switch the moment a fourth arrives.
    // A catch-all would silently leave the newcomer with no hover feedback,
    // which reads as the highlight being broken rather than as a case missed.
    case Region::Offhand:
    case Region::FurnaceInput:
    case Region::FurnaceFuel:
        break;
    }
    return std::nullopt;
}

bool insideCatalogueList(Kind kind, float x, float y) {
    if (!showsCatalogue(kind)) {
        return false;
    }
    const auto [topLeft, bottomRight] = catalogueListBounds();
    return x >= topLeft.x && x <= bottomRight.x && y >= topLeft.y && y <= bottomRight.y;
}

std::pair<glm::vec2, glm::vec2> catalogueListBounds() {
    return {toBook(kCatalogueLeft, kCatalogueTop),
            toBook(kCatalogueLeft + kCatalogueColumns * kSlotPitchPixels, kCatalogueBottom)};
}

bool insidePanel(Kind kind, float x, float y) {
    const float halfHeight = panelHalfHeight(kind);
    if (y < -halfHeight || y > halfHeight) {
        // The tab strip hangs above the card and is still "inside" as far as
        // dropping a held stack is concerned.
        const float tabTop = -halfHeight - kTabSheetSize.y * kPixel;
        if (!showsCatalogue(kind) || y < tabTop || y > -halfHeight) {
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
                       const ItemStack* craftResults, const ItemStack& heldStack, float cursorX, float cursorY,
                       float aspect, const CatalogueState& catalogue, const FurnaceProgress& progress,
                       bool creative, const Chest* chest, const Chest* partner, engine::MeshData& clipped,
                       engine::MeshData& top) {
    engine::MeshData mesh;
    const Layout layout = layoutFor(kind);
    const float panelCentreX = kPanelOffsetX(kind);

    // Dims the world behind, so the panel reads as a layer over the game rather
    // than part of it.
    hud::appendQuad(mesh, 0.0f, 0.0f, aspect, 1.0f, kDimDepth, kDim, static_cast<float>(TextureLayer::White), false);

    // The whole panel is one sprite from the artwork. Rebuilding its frames,
    // bevels and icons out of primitives would mean redrawing by hand something
    // that already exists as a picture.
    hud::appendSprite(mesh, panelCentreX, 0.0f, kPanelHalfWidth, panelHalfHeight(kind), kPanelDepth,
                      layout.panelPixelMin, {kPanelPixelSize.x, panelPixelHeight(kind)}, hud::kSheetSize);

    std::vector<ItemId> catalogueList;
    const std::optional<std::size_t> hoveredEntry =
        catalogueCellAt(kind, cursorX, cursorY, catalogue.scrollRow);
    if (showsCatalogue(kind)) {
        catalogueList = catalogueItems(catalogue);
        // **Nothing is red in creative**, because nothing there is out of
        // reach: the catalogue is a source and every entry is one click away.
        // So the check is not run at all rather than run and ignored.
        //
        // Otherwise it is asked once for the whole table rather than per cell,
        // and against the grid this screen actually has - a 3x3 recipe is not
        // craftable from the inventory's own 2x2, which is the reference's rule
        // and the whole reason `fitsInTwoByTwo` exists.
        const std::unordered_set<ItemId> affordable =
            creative ? std::unordered_set<ItemId>{} : craftableItems(inventory, craftSize(kind));
        appendCatalogue(mesh, clipped, catalogue, catalogueList, affordable, hoveredEntry, creative);
    }

    for (std::size_t i = 0; i < kInventorySlots; ++i) {
        hud::appendStack(mesh, inventory.slot(i), gridSlotCentre(kind, i), kSlotHalf, kIconDepth, kCountDepth);
    }

    // **What is worn, drawn where it is worn.** Without this the hit test would
    // equip in silence: the piece would leave your hand, reduce damage and wear
    // out, and the cell it went into would stay empty - which is
    // indistinguishable from the item having been destroyed, and is the reading
    // a playtester would file.
    for (std::size_t i = 0; i < armourSlotCount(kind); ++i) {
        hud::appendStack(mesh, inventory.armourAt(i), armourSlotCentre(kind, i), kSlotHalf, kIconDepth,
                         kCountDepth);
    }

    for (std::size_t i = 0; i < slotsFor(kind); ++i) {
        hud::appendStack(mesh, craftSlots[i], craftSlotCentre(kind, layout, i), kSlotHalf, kIconDepth,
                         kCountDepth);
    }
    for (std::size_t i = 0; i < resultSlotCount(kind); ++i) {
        hud::appendStack(mesh, craftResults[i], resultSlotCentre(kind, layout, i), kSlotHalf,
                         kIconDepth, kCountDepth);
    }

    if (chest != nullptr) {
        for (std::size_t i = 0; i < chestSlotCount(kind); ++i) {
            const Chest* half = i < kChestSlots ? chest : partner;
            if (half == nullptr) {
                continue;
            }
            hud::appendStack(mesh, half->slots[i % kChestSlots], chestSlotCentre(kind, i), kSlotHalf, kIconDepth,
                             kCountDepth);
        }
    }

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

    // **The hover highlight, on every slot rather than only the catalogue's.**
    // The user's ruling recorded in `INTERFACE.md` 3.1 is that transient
    // brightening on hover is wanted - "if i hover and it temporarily brightens
    // then THAT is fine, persistent brightness isn't" - and until now exactly
    // one family of cells had it, so every ordinary inventory, chest, craft and
    // result slot gave no feedback at all about which one you were pointing at.
    //
    // **Drawn whether or not a stack is on the cursor.** The tooltip below
    // deliberately stays away while carrying, because a label as well as an
    // attached stack is noise; the highlight is the opposite case - while
    // carrying is precisely when you need to see which slot you are about to
    // drop into, so it is the one piece of hover feedback that must survive.
    if (!hoveredEntry.has_value()) {
        if (const std::optional<SlotHit> hover = slotAt(kind, cursorX, cursorY); hover.has_value()) {
            if (const std::optional<glm::vec2> centre = hoveredSlotCentre(kind, layout, *hover);
                centre.has_value()) {
                hud::appendQuad(mesh, centre->x, centre->y, kSlotHighlightHalf, kSlotHighlightHalf,
                                kSlotHighlightBackDepth, kSlotHighlightBack, 0.0f, false);
                hud::appendQuad(mesh, centre->x, centre->y, kSlotHighlightHalf, kSlotHighlightHalf,
                                kSlotHighlightFrontDepth, kSlotHighlightFront, 0.0f, false);
            }
        }
    }

    // Its own layer, drawn after the clipped catalogue. The UI pass has no
    // depth attachment at all (`Renderer.cpp`, `recordUiPass`), so "nearest"
    // only decides the sort key and being appended *earlier* than the icons
    // behind it is what actually punched a hole through them.
    hud::appendStack(top, heldStack, {cursorX, cursorY}, kSlotHalf, kHeldIconDepth, kHeldCountDepth);

    // Only with an empty cursor: while carrying a stack the pointer already has
    // something attached, and a label as well is noise over the slot you are
    // aiming at.
    if (heldStack.empty()) {
        const ItemStack* under = nullptr;
        ItemStack catalogueEntry;
        if (hoveredEntry.has_value() && *hoveredEntry < catalogueList.size()) {
            catalogueEntry = ItemStack{catalogueList[*hoveredEntry], 1};
            under = &catalogueEntry;
        } else if (const std::optional<SlotHit> hover = slotAt(kind, cursorX, cursorY); hover.has_value()) {
            switch (hover->region) {
            case Region::Grid:
                under = &inventory.slot(hover->index);
                break;
            case Region::Chest:
                if (const Chest* half = hover->index < kChestSlots ? chest : partner; half != nullptr) {
                    under = &half->slots[hover->index % kChestSlots];
                }
                break;
            case Region::Craft:
                under = &craftSlots[hover->index];
                break;
            case Region::CraftResult:
                if (hover->index < resultSlotCount(kind)) {
                    under = &craftResults[hover->index];
                }
                break;
            case Region::Armour:
                if (hover->index < armourSlotCount(kind)) {
                    under = &inventory.armourAt(hover->index);
                }
                break;
            // **No `default:`.** The three regions below have no stack behind
            // them today, and naming them is what makes the compiler point at
            // this switch the moment a fourth arrives - a catch-all would
            // silently give the new one no tooltip, which reads as the tooltip
            // being broken rather than as a case being missed.
            case Region::Offhand:
            case Region::FurnaceInput:
            case Region::FurnaceFuel:
                break;
            }
        }
        if (under != nullptr && !under->empty()) {
            hud::appendTooltip(top,
                               displayNameOf(under->item),
                               cursorX, cursorY, aspect, kTooltipTextHeight, kTooltipDepth);
        }
    }

    return mesh;
}

} // namespace game::inventoryScreen
