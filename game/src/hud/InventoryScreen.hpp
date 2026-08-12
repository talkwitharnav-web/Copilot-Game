#pragma once

#include "item/Inventory.hpp"
#include "world/Chest.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace game {

/// The inventory screen, laid out from the concept art in `reference/`.
///
/// Everything is built in the same screen space as the rest of the HUD:
/// coordinates relative to window **height** on both axes, Y growing downward.
namespace inventoryScreen {

/// Which panel is on screen.
///
/// Both are the same size and share their storage rows, hotbar and frame - the
/// crafting table's art is *derived* from the inventory's, so the only
/// differences are which sprite is drawn and where the crafting area sits. One
/// module rather than two keeps a single copy of the slot interaction, which is
/// by far the fiddliest part of either screen.
enum class Kind {
    Inventory,
    CraftingTable,
    Furnace,
    /// Two inputs and a result, like a furnace, but the result is a **preview**
    /// computed from what is in front of it rather than something the block is
    /// slowly making. It borrows the furnace's slot regions so the whole click,
    /// drag and shift-click path works unchanged.
    SmithingTable,
    /// Twenty-seven slots of storage above the player's own. The one screen
    /// with a region of its own rather than borrowing the crafting slots,
    /// because its contents are neither ingredients nor a result.
    Chest,
    /// Two chests standing shoulder to shoulder, shown as one fifty-four slot
    /// container. They stay two separate blocks holding twenty-seven each -
    /// only the screen joins them.
    DoubleChest,
    /// Five slots in one row. It is a container like a chest, only smaller, so
    /// it borrows the `Chest` region outright and the entire click, drag and
    /// shift-click path works with no hopper-specific branch anywhere.
    Hopper,
    /// One block in, and the cuts it offers laid out beside it. The results are
    /// **previews** like a smithing table's, so they use `CraftResult` - but
    /// there are three of them, and it is the only screen where that region's
    /// index means anything.
    Stonecutter,
};

/// Width and height of a screen's crafting grid.
///
/// A furnace has no grid: its three slots are input, fuel and output, which the
/// recipe matcher never sees. Nor has a smithing table.
constexpr int craftSize(Kind kind) {
    switch (kind) {
    case Kind::CraftingTable:
        return 3;
    case Kind::Furnace:
    case Kind::SmithingTable:
    case Kind::Chest:
    case Kind::DoubleChest:
    case Kind::Hopper:
    case Kind::Stonecutter:
        return 0;
    default:
        return 2;
    }
}

constexpr std::size_t craftSlotCount(Kind kind) {
    const auto size = static_cast<std::size_t>(craftSize(kind));
    return size * size;
}

/// How far along a furnace is, 0 to 1 each. Ignored by the other screens.
struct FurnaceProgress {
    float burn = 0.0f;
    float cook = 0.0f;
};

/// Slots the layout owns beyond the inventory grid itself.
enum class Region {
    Grid,
    Armour,
    Offhand,
    Craft,
    CraftResult,
    /// Furnace only. `Craft` index 0 doubles as its input and 1 as its fuel, so
    /// the shared click handling needs no furnace-specific branch.
    FurnaceInput,
    FurnaceFuel,
    /// A chest's own twenty-seven, indexed 0 to 26 across then down.
    Chest,
};

struct SlotHit {
    Region region;
    std::size_t index;
};

/// The five tabs across the top of the catalogue card.
///
/// The first four *are* the four item categories, in order, which is what lets
/// a tab be turned into a filter by a cast rather than by a table. Search is
/// not a category: it shows everything, and later filters by typed text.
enum class CatalogueTab : std::uint8_t {
    Construction,
    Equipment,
    Items,
    Nature,
    Search,
    Count,
};

static_assert(static_cast<int>(CatalogueTab::Search) == static_cast<int>(ItemCategory::Count),
              "the first four tabs must line up with the four categories");

/// What the catalogue card is showing.
///
/// `build` is otherwise a pure function of its arguments, and keeping it that
/// way is the most reusable fact about this module - so the screen's state
/// lives in one struct the caller owns rather than in file-scope statics.
struct CatalogueState {
    CatalogueTab tab = CatalogueTab::Construction;
    /// How many whole rows have been scrolled past. Rows rather than pixels,
    /// because the grid has no sub-row states and a fractional offset would
    /// only make the clipped last row ambiguous.
    int scrollRow = 0;
    /// What has been typed into the search field. **Only the Search tab reads
    /// it**, which is the reference's arrangement: the four category tabs are
    /// a browse, and searching is its own place.
    std::string query;
    /// Whether the caret is on this frame. Driven by the caller's clock rather
    /// than by a static in here, so `build` stays a pure function of what it is
    /// given - the same reason the scroll row lives here.
    bool caretVisible = true;
    /// Whether the field has been clicked into. Until it has, the caret is
    /// hidden, the hint is shown, and **the keyboard belongs to the game** - so
    /// selecting the Search tab does not silently stop `E` closing the screen.
    bool searchFocused = false;

    /// The recipe book: everything the player has been able to make at least
    /// once. It only ever grows, which is what the reference's own book does -
    /// a recipe you have seen stays visible after you spend the ingredients.
    std::unordered_set<ItemId> known;

    /// Whether `known` is consulted at all.
    ///
    /// **An empty book and no book are different things**, which is why this is
    /// a flag rather than "empty means everything": creative wants the whole
    /// catalogue because there it is a *source*, and a survival player who has
    /// made nothing yet wants an empty one.
    bool restrictToKnown = false;
};

/// Whether this screen shows the catalogue card beside the inventory.
///
/// A furnace does not: its own recipe list is a separate design and is
/// deliberately deferred. Nor does a smithing table - its one operation is in
/// front of you.
constexpr bool showsCatalogue(Kind kind) {
    return kind == Kind::Inventory || kind == Kind::CraftingTable;
}
/// Which tab, if any, sits under a point in screen space.
std::optional<CatalogueTab> tabAt(Kind kind, float x, float y);

/// Which catalogue entry sits under a point, as an index into the tab's
/// filtered list. Cells past the bottom of the card never answer.
///
/// Takes the scroll offset rather than returning a screen-relative cell,
/// because turning one into the other is the sort of arithmetic that ends up
/// copied to three call sites and wrong at one of them.
std::optional<std::size_t> catalogueCellAt(Kind kind, float x, float y, int scrollRow);

/// The furthest `scrollRow` that still shows something, for a list this long.
/// Zero when everything already fits.
int catalogueMaxScroll(std::size_t itemCount);

/// True anywhere over the catalogue's list - the cells, the gaps between them
/// and the empty space below the last one.
///
/// The reference makes this region the bin: left-clicking it while carrying a
/// stack destroys it. The tabs and the card's frame are deliberately outside,
/// so a misjudged click on a tab cannot cost you what you are holding.
bool insideCatalogueList(Kind kind, float x, float y);

/// The catalogue list's rectangle in screen units, top-left then bottom-right.
/// The renderer scissors to it, so the row the card cuts through is cut for
/// real rather than shrunk to fit.
std::pair<glm::vec2, glm::vec2> catalogueListBounds();

/// The items a tab lists, in declaration order.
///
/// `catalogue.query` is only read by the Search tab, and matches at the **start
/// of any word** of an item's name rather than anywhere in it - so "st" finds
/// Stone and Stone Stairs but not Sandstone, which is what makes a short query
/// useful in a list of seven hundred.
///
/// **Takes the whole state rather than a tab and a string.** Three callers read
/// this list - the drawing, the click that resolves a cell, and the scroll
/// limit - and if any of them built a different list the clicks would land on
/// the wrong item. One argument is one list.
std::vector<ItemId> catalogueItems(const CatalogueState& catalogue);

/// Which slot, if any, sits under a point in screen space.
std::optional<SlotHit> slotAt(Kind kind, float x, float y);

/// Whether a point is over the search field. Clicking it is what focuses it,
/// and clicking anywhere else is what lets it go.
bool insideSearchField(Kind kind, float x, float y);

/// True when the point is anywhere over either card, so clicks outside them can
/// be told apart from clicks that missed a slot.
bool insidePanel(Kind kind, float x, float y);

/// How many storage slots the open container offers. A double chest is two
/// blocks' worth shown as one list.
constexpr std::size_t chestSlotCount(Kind kind) {
    if (kind == Kind::DoubleChest) {
        return kChestSlots * 2;
    }
    if (kind == Kind::Hopper) {
        return kHopperSlots;
    }
    return kind == Kind::Chest ? kChestSlots : 0;
}

/// How many previewed results a screen shows. Only the stonecutter shows more
/// than one, and it is the only place `Region::CraftResult`'s index matters.
constexpr std::size_t resultSlotCount(Kind kind) {
    return kind == Kind::Stonecutter ? static_cast<std::size_t>(kStonecutterOptions) : 1u;
}

/// Every screen that shows a container, so the caller can ask one question
/// rather than list three kinds. **`isContainer` and `chestSlotCount` must
/// agree**: a kind that offers slots and is not named here would draw them and
/// then refuse every click.
constexpr bool isContainer(Kind kind) {
    return chestSlotCount(kind) > 0;
}

/// `heldStack` is what the cursor is carrying, drawn at (cursorX, cursorY).
///
/// In `creative` no entry is ever red: the catalogue is a source there, so
/// everything in it is one click away and "can you make this" is not a question
/// worth asking.
///
/// `chest` is only read by `Kind::Chest` and is null for every other screen.
///
/// `partner` is the second half of a double chest and supplies slots 27-53. It
/// is null everywhere else.
///
/// Catalogue entry icons go into `clipped` rather than the returned mesh,
/// because they are the one part of the screen that has to stop at an edge.
///
/// What the cursor carries, and the label under it, go into `top`, which is
/// drawn after `clipped`. A blended fragment still writes depth, so a stack
/// held over the catalogue was stamping a hole through the icons behind it
/// wherever its own artwork was transparent.
/// `craftResults` points at `resultSlotCount(kind)` previewed results. Every
/// screen but the stonecutter has exactly one.
engine::MeshData build(Kind kind, const Inventory& inventory, const ItemStack* craftSlots,
                       const ItemStack* craftResults, const ItemStack& heldStack, float cursorX, float cursorY,
                       float aspect, const CatalogueState& catalogue, const FurnaceProgress& progress,
                       bool creative, const Chest* chest, const Chest* partner, engine::MeshData& clipped,
                       engine::MeshData& top);

} // namespace inventoryScreen
} // namespace game
