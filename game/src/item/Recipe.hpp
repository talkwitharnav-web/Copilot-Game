#pragma once

#include "item/Item.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace game {

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

} // namespace game
