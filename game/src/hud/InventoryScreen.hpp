#pragma once

#include "item/Inventory.hpp"
#include "world/Chest.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <algorithm>
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
    /// **Not a screen.** It bounds the sweep below, which requires every `Kind`
    /// before it to state its own crafting grid - so a ninth screen fails the
    /// build instead of silently inheriting a 2x2 nothing draws.
    Count,
};

/// Width and height of a screen's crafting grid.
///
/// A furnace has no grid: its three slots are input, fuel and output, which the
/// recipe matcher never sees. Nor has a smithing table.
constexpr int craftSize(Kind kind) {
    switch (kind) {
    case Kind::Inventory:
        return 2;
    case Kind::CraftingTable:
        return 3;
    case Kind::Furnace:
    case Kind::SmithingTable:
    case Kind::Chest:
    case Kind::DoubleChest:
    case Kind::Hopper:
    case Kind::Stonecutter:
        return 0;
    case Kind::Count:
        break;
    }
    // **Not a grid size, and deliberately not a plausible one.** This used to be
    // `default: return 2`, which is the inventory's own grid - so a ninth `Kind`
    // would have quietly been given a 2x2 of writable slots that nothing draws
    // and nothing hit-tests, and it would have worked well enough to ship.
    // `CLAUDE.md` bug shape #10: a `default:` that returns a real value hides
    // every missing entry. The sweep below is what turns this into a build
    // failure, because MSVC's C4062 is off at /W4 and will not report the
    // missing case on its own.
    return -1;
}

/// Whether every `Kind` states its own grid width.
///
/// Walks the enum rather than the switch, so the two cannot agree by being the
/// same list - adding an enumerator without a `case` fails here, which is the
/// whole point of `Kind::Count` existing.
constexpr bool everyKindStatesItsGrid() {
    for (int i = 0; i < static_cast<int>(Kind::Count); ++i) {
        if (craftSize(static_cast<Kind>(i)) < 0) {
            return false;
        }
    }
    return true;
}

static_assert(everyKindStatesItsGrid(), "every Kind must state its own crafting grid width");
/// Negative control: the sweep above is only worth reading if an unstated
/// `Kind` still fails it.
static_assert(craftSize(Kind::Count) < 0,
              "an unstated Kind must answer the sentinel, or the sweep above proves nothing");

constexpr std::size_t craftSlotCount(Kind kind) {
    // `craftSize` answers -1 for a `Kind` that never stated its grid, and a
    // negative squared through `std::size_t` wraps to a plausible-looking 1.
    // Nothing real reaches it - `everyKindStatesItsGrid` proves that - but a
    // sentinel that turns into a slot count is not a sentinel.
    const int size = craftSize(kind);
    if (size <= 0) {
        return 0u;
    }
    return static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
}

/// How far along a furnace is, 0 to 1 each. Ignored by the other screens.
struct FurnaceProgress {
    float burn = 0.0f;
    float cook = 0.0f;
};

/// Slots the layout owns beyond the inventory grid itself.
///
/// **`Armour` is live as of 2026-08-19 and the three below it are not.**
/// `slotAt` returns `Armour` for the four cells down the left of the inventory
/// panel, `Main.cpp` resolves it to `Inventory::armourAt`, and `build` draws
/// what is worn there - which is what finally made `armourDefence`,
/// `armourToughness` and `survival::armourDamageTaken` reachable at all. Before
/// that the whole armour chain was correct, asserted and unusable, because
/// nothing in the game could put a helmet on.
///
/// `Offhand`, `FurnaceInput` and `FurnaceFuel` are still produced by nothing.
/// The two furnace ones are dead by design and say so below - the furnace
/// reuses `Craft`. The offhand is a placeholder for a screen that does not
/// exist yet. They are kept rather than deleted because the exhaustive `switch`
/// in `build` names them, so removing one is a compile error rather than a
/// silently dropped tooltip.
///
/// Negative claim about those three, checked 2026-08-19 11:29 and dated because
/// negative claims rot fastest: what makes it false for `Offhand` is a
/// second-hand slot existing at all, at which point the cell is already in the
/// panel art at centre (85, 70) - measured, not guessed - and wiring it is the
/// same four edits `Armour` needed.
///
/// **Re-measure it like this, because the obvious search gives a false clean.**
/// Grepping `SlotHit{Region::` finds no `Offhand` and never will - not even
/// after somebody wires one through a local, a ternary or an assignment,
/// because that pattern tests the *accessor* rather than the data. Use a
/// bare-name search over comment-stripped source, then reconcile every hit
/// against a form. In `InventoryScreen.cpp`:
///
///   | symbol         | hits | what they are                     |
///   |----------------|------|-----------------------------------|
///   | `Armour`       |    3 | 1 construction + 2 `case` labels  |
///   | `Offhand`      |    2 | 0 constructions + 2 `case` labels |
///   | `FurnaceInput` |    2 | 0 constructions + 2 `case` labels |
///   | `FurnaceFuel`  |    2 | 0 constructions + 2 `case` labels |
///
/// **`Armour` is the control, and it is the right control because it is the
/// same kind of symbol, in the same file, under the same search** - a produced
/// enumerator reads exactly one higher than an unproduced one, and that one is
/// the construction. Comparing against some neighbouring symbol instead would
/// be vacuous however true its number, which is the way a control most often
/// fails without looking like it failed.
///
/// **The proof is that no row has a residue.** Every hit is accounted for as a
/// construction or a label. If `Offhand` ever reads 3 while still showing two
/// labels, something now produces it and this paragraph is false.
enum class Region {
    Grid,
    Armour,
    Offhand,
    Craft,
    CraftResult,
    /// Furnace only. `Craft` index 0 doubles as its input and 1 as its fuel, so
    /// the shared click handling needs no furnace-specific branch - which is
    /// why neither of these is ever produced.
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

    /// Where the next character goes: a **byte offset into `query`**, from 0
    /// (before the first character) to `query.size()` (after the last).
    ///
    /// **Bytes, not characters - and here the two are the same thing by
    /// construction, not by luck.** `hud::fontAdvance` is a 128-entry table and
    /// `appendText` draws one cell per byte, so a byte the font cannot name is
    /// already undrawable; the field only ever receives what the window's typed
    /// text hands it. A byte index is therefore the *same* index the renderer
    /// measures with, which is the whole point - the caret's position and the
    /// text's width must be counted in one unit or the bar lands between the
    /// wrong pair of glyphs.
    ///
    /// If the field ever accepts anything above 127, `moveCaret` is the single
    /// place that has to learn about continuation bytes, and this comment is
    /// the thing that will be wrong first.
    std::size_t caret = 0;

    /// **The one owner of the caret's range**, and every edit below ends here.
    ///
    /// A caret that outlives its string is an out-of-bounds read on the very
    /// next keystroke, and `query` is a public member that code with no idea a
    /// caret exists can shorten. Idempotent on purpose, so calling it twice
    /// costs nothing and calling it once too often is never wrong.
    ///
    /// The renderer does **not** rely on this - it clamps again locally, at the
    /// point of use, because `build` takes this struct by const reference and
    /// cannot repair it. That is deliberate belt and braces: a missed call here
    /// can then only look momentarily odd for one frame, never crash.
    void clampCaret() { caret = std::min(caret, query.size()); }

    /// Replaces the whole field, caret and all.
    ///
    /// **This is the reconciliation point for everything that is not a
    /// keystroke** - closing the screen, switching tab, a reset. A rule that
    /// exists in only one of the two places that need it is this project's most
    /// expensive bug shape, so clearing the query has a named way to do it
    /// rather than a `query.clear()` that leaves the caret behind.
    void setQuery(std::string next) {
        query = std::move(next);
        caret = query.size();
    }

    /// A typed character, inserted **at the caret** rather than appended.
    void insertAtCaret(char c) {
        clampCaret();
        query.insert(caret, 1, c);
        ++caret;
    }

    /// Backspace: removes what is *before* the caret, and moves it back.
    void backspaceAtCaret() {
        clampCaret();
        if (caret > 0) {
            query.erase(caret - 1, 1);
            --caret;
        }
    }

    /// Delete: removes what is *under* the caret, and leaves it where it is.
    /// The two are different keys and they are not each other's mirror - this
    /// one does nothing at the end of the string, where Backspace does nothing
    /// at the start.
    void deleteAtCaret() {
        clampCaret();
        if (caret < query.size()) {
            query.erase(caret, 1);
        }
    }

    /// Left and Right, as a signed step.
    ///
    /// **Clamped at both ends rather than wrapping.** A caret that jumps from
    /// the end of the field to the start reads as a dropped keystroke, and the
    /// player presses the key again.
    void moveCaret(int delta) {
        clampCaret();
        const int moved = static_cast<int>(caret) + delta;
        caret = static_cast<std::size_t>(std::clamp(moved, 0, static_cast<int>(query.size())));
    }

    /// Home and End.
    void caretToStart() { caret = 0; }
    void caretToEnd() { caret = query.size(); }

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

/// How many armour cells the open screen shows.
///
/// **Only the inventory**, and that is the art rather than a policy: the panel
/// sprite every other screen uses is the inventory's with everything above the
/// storage rows painted out - `tools/make-hud-sheet.ps1`, "the top section is
/// cleared wholesale - armour column, character box, offhand". A crafting table
/// that answered four here would hit-test four cells that are not drawn.
///
/// **This and `armourSlotCentre` must agree**, the same pairing rule
/// `isContainer` and `chestSlotCount` carry: a screen that offers cells nothing
/// draws, or draws cells no click can reach, is the half-landed shape.
constexpr std::size_t armourSlotCount(Kind kind) {
    return kind == Kind::Inventory ? kArmourSlots : 0;
}

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

/// Every screen that shows a container, so the caller can ask one question
/// rather than list three kinds. **`isContainer` and `chestSlotCount` must
/// agree**: a kind that offers slots and is not named here would draw them and
/// then refuse every click.
constexpr bool isContainer(Kind kind) {
    return chestSlotCount(kind) > 0;
}

/// How many previewed results a screen shows.
///
/// **Zero for anything that is not a crafting surface**, which `slotAt` has
/// always agreed with - it returns before it ever tests a result rectangle on a
/// chest, a hopper or a double chest. This said 1 for all three, so the two
/// halves of the same question gave different answers, and any caller that
/// trusted this one would build a result slot no click could ever reach. The
/// stonecutter is the only screen that shows more than one.
constexpr std::size_t resultSlotCount(Kind kind) {
    if (kind == Kind::Stonecutter) {
        return static_cast<std::size_t>(kStonecutterOptions);
    }
    return isContainer(kind) ? 0u : 1u;
}

static_assert(resultSlotCount(Kind::Chest) == 0 && resultSlotCount(Kind::DoubleChest) == 0 &&
                  resultSlotCount(Kind::Hopper) == 0 && resultSlotCount(Kind::Inventory) == 1 &&
                  resultSlotCount(Kind::Stonecutter) == kStonecutterOptions,
              "a container has nothing to make - the single edit that breaks this is giving a new "
              "container kind storage slots without asking whether it also has a result");

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
/// drawn after `clipped`. The UI pass has no depth attachment (`Renderer.cpp`,
/// `recordUiPass`), so what actually decides the order is the renderer's
/// far-first stable sort with append order breaking ties - a stack held over
/// the catalogue was appended *before* the icons behind it and came out under
/// them wherever its own artwork was transparent.
/// `craftResults` points at `resultSlotCount(kind)` previewed results. Every
/// screen but the stonecutter has exactly one.
engine::MeshData build(Kind kind, const Inventory& inventory, const ItemStack* craftSlots,
                       const ItemStack* craftResults, const ItemStack& heldStack, float cursorX, float cursorY,
                       float aspect, const CatalogueState& catalogue, const FurnaceProgress& progress,
                       bool creative, const Chest* chest, const Chest* partner, engine::MeshData& clipped,
                       engine::MeshData& top);

} // namespace inventoryScreen
} // namespace game
