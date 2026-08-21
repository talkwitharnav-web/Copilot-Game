#pragma once

#include "item/Item.hpp"
#include "item/SeenItems.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <unordered_set>
#include <vector>

namespace game {

class Inventory;

/// The largest grid any recipe is matched against. The player's own grid is
/// 2x2; a crafting table will raise this to 3x3 without the matcher changing.
constexpr int kMaxCraftSize = 3;
constexpr std::size_t kMaxCraftSlots = kMaxCraftSize * kMaxCraftSize;

/// A recipe's ingredients, laid out as they must sit relative to each other.
///
/// **Shaped** recipes care about arrangement; **shapeless** ones only care about
/// which items are present. Both are the same struct because the difference is
/// one flag, and splitting them would mean two lists to search and two ways to
/// get the search wrong.
///
/// A shaped pattern is stored at its own size rather than padded to 3x3, so the
/// matcher can slide it around a larger grid. That is what lets a 1x2 stick
/// recipe be crafted in any column of a crafting table, exactly as the reference
/// data describes it.
struct Recipe {
    /// Rows of the pattern, top to bottom, each `width` long. `ItemId::None`
    /// means the cell must be empty.
    ///
    /// **Every ingredient is one concrete item, and the craftable check relies
    /// on that.** "Do I have the materials?" is bipartite matching in general,
    /// not a greedy count: given a recipe wanting `any plank` + `oak plank`
    /// while the player holds one oak and one spruce, greedy spends the oak on
    /// `any plank`, fails `oak plank`, and reports "not craftable" - wrongly.
    /// A matcher reassigns `any plank` to the spruce and succeeds. Greedy is
    /// correct for exactly as long as no ingredient names a *set*, so the day
    /// this array stops holding plain `ItemId`s, the check has to be rewritten.
    std::array<ItemId, kMaxCraftSlots> pattern{};
    int width = 0;
    int height = 0;
    bool shapeless = false;

    ItemStack result{};

    /// Whether the pattern fits the player's own 2x2 grid.
    ///
    /// The recipe book in the inventory shows only these; a crafting table
    /// shows everything. Precomputed once, because it is a fact about the
    /// pattern and a shapeless recipe stores its ingredient *count* in `width`,
    /// which is exactly the sort of thing a caller re-deriving it gets wrong.
    bool fitsInTwoByTwo = false;
};

/// Every recipe in the game, in declaration order.
///
/// Order is the display order: neither edition sorts alphabetically and neither
/// has a display-order field.
const std::vector<Recipe>& recipes();

/// The recipe a grid currently makes, or an empty stack for none.
///
/// `size` is the width and height of the square grid `slots` describes, so the
/// same call serves the inventory's 2x2 and a table's 3x3.
ItemStack craftResult(const ItemStack* slots, int size);

/// Removes one of each ingredient the matched recipe used.
///
/// Separate from `craftResult` because the result has to be shown before it is
/// taken: the player sees what a grid would make, then decides to take it.
void consumeIngredients(ItemStack* slots, int size);

/// Everything the inventory could make right now on a grid this wide.
///
/// **Shape is ignored entirely** - this is the other of the two questions in
/// `INTERFACE.md` section 4.1: whether you *have* nine cobblestone has nothing to
/// do with where they go. `gridSize` still matters, because a 3x3 recipe is not
/// craftable while you are standing at the inventory's own 2x2.
///
/// Answered for the whole table at once rather than per item, because the
/// catalogue asks about 126 cells every frame a screen is open.
std::unordered_set<ItemId> craftableItems(const Inventory& inventory, int gridSize);

/// Every result the player has been *shown how to make* on a grid this wide.
///
/// The recipe book's other question, and deliberately not the one
/// `craftableItems` answers: **meeting an ingredient unlocks a row for good,
/// while holding enough of one only decides what colour it is drawn.** Someone
/// who mined a log, spent every plank on a hut and has none left has still met
/// logs, and taking the plank recipe away again reads as the game forgetting
/// rather than as a rule. `SeenItems` is append-only by construction, which is
/// what makes that promise keepable.
///
/// **One ingredient is enough**, rather than all of them - a row appears the
/// moment any filled cell names something seen. That is what makes the book a
/// trail worth following, and it is exactly why the ingredients still missing
/// have to be drawn as missing; `previewFor` is what says which those are.
///
/// `gridSize` filters exactly as `craftableItems` does, because a 3x3 recipe is
/// not worth listing to a player standing at their own 2x2.
std::unordered_set<ItemId> recipesUnlockedBySeen(const SeenItems& seen, int gridSize);

/// One recipe resolved into the grid a screen actually draws, with the cells
/// the player cannot fill already marked.
///
/// **Laid out at the screen's width rather than the recipe's**, which is the
/// whole reason this exists instead of the caller reading `Recipe::pattern`
/// itself: a pattern is stored tight at its own size - the stick recipe is two
/// entries at `width = 1, height = 2` - so copying the array across puts those
/// two planks side by side and draws a recipe that does not exist.
struct RecipePreview {
    /// Row-major at the **screen's** grid width. `ItemId::None` is an empty cell.
    std::array<ItemId, kMaxCraftSlots> cells{};

    /// Parallel to `cells`: the inventory cannot supply this particular cell.
    ///
    /// Per cell rather than per ingredient, because multiplicity is something
    /// the player reads off the picture: three sticks wanted against two held
    /// marks exactly **one** cell, so counting the red ones says how many more
    /// are needed. `previewFor` spends the count map as it walks the cells,
    /// which is what makes that true.
    ///
    /// **Which cell of several carries the mark is chosen, not incidental**:
    /// cells the grid has already filled with the right item are settled first,
    /// so a shortfall lands on an *empty* cell wherever the recipe still has
    /// one. That is what the caller needs, because a mark on a cell holding a
    /// real stack is a mark it cannot draw without covering the player's own
    /// item. A marked cell is therefore either empty or holding the wrong
    /// thing, and both of those are honest to paint red.
    std::array<bool, kMaxCraftSlots> missing{};

    ItemStack result{};

    /// Nothing is missing, so laying this out and taking the result would work.
    ///
    /// **`previewFor` reads this itself**, and that is what decides which of
    /// several rows the player is shown, so it is load-bearing rather than
    /// merely a colour for the caller to draw with.
    bool craftable = false;
};

/// A recipe making `item` that fits `gridSize`, laid out top-left, with each
/// cell measured against everything the player can reach.
///
/// **The first row the player can actually satisfy, and only failing that the
/// first row that fits.** Declaration order is display order throughout this
/// project (`INTERFACE.md` section 4.6) - neither edition sorts recipes and
/// neither has a display-order field - so it survives as the tie-break, and a
/// player holding none of the ingredients still sees exactly the row the book
/// would have shown. What it cannot be is the *whole* rule, because
/// `craftableItems` colours the catalogue tile from **any** row producing the
/// item while this function draws one of them, so stopping at the first would
/// make two halves of one screen contradict each other.
///
/// **`inventory` is not everything the player owns**, which is the other half
/// of getting this right: the crafting grid and the cursor stack are separate
/// storage, so moving an ingredient into a cell takes it out of the bag and a
/// preview measured against the bag alone reddens as the player completes the
/// recipe. `gridSlots` and `gridSlotCount` are the cells already laid out and
/// `held` is the cursor stack; `gridSlots` may be null with a count of zero
/// where a caller has no grid to read. The grid answers two questions rather
/// than one - what the player owns, and which cells are already right - and the
/// second is what decides where `RecipePreview::missing` lands.
///
/// Empty when nothing makes `item` at this grid size - which includes the case
/// where the only recipe for it wants a crafting table and the player is at
/// their own 2x2.
std::optional<RecipePreview> previewFor(ItemId item, const Inventory& inventory,
                                        const ItemStack* gridSlots, std::size_t gridSlotCount,
                                        const ItemStack& held, int gridSize);

/// What a smithing table makes of a tool and a material, or nothing.
///
/// **Its own table, not a recipe**, which is what the reference does too: an
/// upgrade keeps the item it is given rather than consuming it into something
/// unrelated, so it can never be expressed as a grid pattern. Ours is the
/// diamond tools against an Emberite ingot; the reference also needs a template
/// item, which we have no source for.
ItemStack smithingResult(const ItemStack& base, const ItemStack& addition);

/// What a bottle and a reagent brew into, or nothing.
///
/// **Its own table rather than a `Recipe`**, for the same reason the smithing
/// upgrade is: brewing *changes* the thing on the left rather than consuming
/// two ingredients into a third, and no grid pattern can express that. The rows
/// are transcribed from Mojang's own shipped `brew_*.json` recipes.
///
/// > **Named divergence: there is no twenty-second timer and no fuel charge.**
/// > The reference brews over 400 ticks and burns a charge of cinder powder
/// > doing it; ours hands the potion over as soon as the two are in, exactly as
/// > the smithing table and the stonecutter already do. Adding the timer means a
/// > block entity that ticks and saves, which is the furnace's whole machinery
/// > over again.
ItemStack brewingResult(const ItemStack& bottle, const ItemStack& reagent);

/// Two damaged items of the same kind, combined into one.
///
/// The reference's own arithmetic: the durability left on both, **plus five per
/// cent of the item's maximum as a bonus**, capped at that maximum. It is what
/// a grindstone and an anvil both do, and the only part of either that does not
/// depend on enchantments or experience - neither of which exists here.
ItemStack repairResult(const ItemStack& left, const ItemStack& right);

} // namespace game
