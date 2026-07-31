#pragma once

#include "item/Item.hpp"

#include <array>
#include <cstddef>

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
    std::array<ItemId, kMaxCraftSlots> pattern{};
    int width = 0;
    int height = 0;
    bool shapeless = false;

    ItemStack result{};
};

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
