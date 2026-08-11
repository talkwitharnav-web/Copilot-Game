#pragma once

#include "item/Item.hpp"

#include <array>
#include <cstddef>
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
    /// `any plank`, fails `oak plank`, and reports "not craftable" — wrongly.
    /// A matcher reassigns `any plank` to the spruce and succeeds. Greedy is
    /// correct for exactly as long as no ingredient names a *set*, so the day
    /// this array stops holding plain `ItemId`s, the check has to be rewritten.
    std::array<ItemId, kMaxCraftSlots> pattern{};
    int width = 0;
    int height = 0;
    bool shapeless = false;

    ItemStack result{};

    /// Which catalogue tab this recipe is listed under.
    ///
    /// Per **recipe**, not per item: one item can be produced by recipes that
    /// belong in different places. It defaults to the result item's own
    /// category, so the item table stays the one owner and a recipe only says
    /// anything when it disagrees.
    ItemCategory category = ItemCategory::Items;

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
/// `INTERFACE.md` §4.1: whether you *have* nine cobblestone has nothing to do
/// with where they go. `gridSize` still matters, because a 3x3 recipe is not
/// craftable while you are standing at the inventory's own 2x2.
///
/// Answered for the whole table at once rather than per item, because the
/// catalogue asks about 126 cells every frame a screen is open.
std::unordered_set<ItemId> craftableItems(const Inventory& inventory, int gridSize);

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
/// > **Named divergence: there is no twenty-second timer and no blaze powder.**
/// > The reference brews over 400 ticks and burns a fuel charge doing it; ours
/// > hands the potion over as soon as the two are in, exactly as the smithing
/// > table and the stonecutter already do. Adding the timer means a block entity
/// > that ticks and saves, which is the furnace's whole machinery over again.
ItemStack brewingResult(const ItemStack& bottle, const ItemStack& reagent);

/// Two damaged items of the same kind, combined into one.
///
/// The reference's own arithmetic: the durability left on both, **plus five per
/// cent of the item's maximum as a bonus**, capped at that maximum. It is what
/// a grindstone and an anvil both do, and the only part of either that does not
/// depend on enchantments or experience - neither of which exists here.
ItemStack repairResult(const ItemStack& left, const ItemStack& right);

} // namespace game
