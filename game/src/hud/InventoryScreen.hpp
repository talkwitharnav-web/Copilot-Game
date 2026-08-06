#pragma once

#include "item/Inventory.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
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
};

/// Width and height of a screen's crafting grid.
///
/// A furnace has no grid: its three slots are input, fuel and output, which the
/// recipe matcher never sees.
constexpr int craftSize(Kind kind) {
    switch (kind) {
    case Kind::CraftingTable:
        return 3;
    case Kind::Furnace:
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
/// The search text joins it in a later slice.
struct CatalogueState {
    CatalogueTab tab = CatalogueTab::Construction;
    /// How many whole rows have been scrolled past. Rows rather than pixels,
    /// because the grid has no sub-row states and a fractional offset would
    /// only make the clipped last row ambiguous.
    int scrollRow = 0;
};

/// Whether this screen shows the catalogue card beside the inventory.
///
/// A furnace does not: its own recipe list is a separate design and is
/// deliberately deferred.
constexpr bool showsCatalogue(Kind kind) {
    return kind != Kind::Furnace;
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
std::vector<ItemId> catalogueItems(CatalogueTab tab);

/// Which slot, if any, sits under a point in screen space.
std::optional<SlotHit> slotAt(Kind kind, float x, float y);

/// True when the point is anywhere over either card, so clicks outside them can
/// be told apart from clicks that missed a slot.
bool insidePanel(Kind kind, float x, float y);

/// `heldStack` is what the cursor is carrying, drawn at (cursorX, cursorY).
///
/// Catalogue entry icons go into `clipped` rather than the returned mesh,
/// because they are the one part of the screen that has to stop at an edge.
engine::MeshData build(Kind kind, const Inventory& inventory, const ItemStack* craftSlots,
                       const ItemStack& craftResult, const ItemStack& heldStack, float cursorX, float cursorY,
                       float aspect, const CatalogueState& catalogue, const FurnaceProgress& progress,
                       engine::MeshData& clipped);

} // namespace inventoryScreen
} // namespace game
