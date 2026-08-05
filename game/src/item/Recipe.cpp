#include "item/Recipe.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace game {
namespace {

constexpr ItemId kNone = ItemId::None;

Recipe shaped(int width, int height, std::array<ItemId, kMaxCraftSlots> pattern, ItemId result, int count) {
    Recipe recipe;
    recipe.pattern = pattern;
    recipe.width = width;
    recipe.height = height;
    recipe.result = ItemStack{result, count};
    return recipe;
}

Recipe shapeless(std::array<ItemId, kMaxCraftSlots> ingredients, int used, ItemId result, int count) {
    Recipe recipe;
    recipe.pattern = ingredients;
    recipe.width = used;
    recipe.height = 1;
    recipe.shapeless = true;
    recipe.result = ItemStack{result, count};
    return recipe;
}

/// Every tool is its material over a stick or two, and every shape is the same
/// for both materials - so the table is generated rather than written out
/// twenty times.
Recipe tool(ItemId material, ItemId result, const char* pattern) {
    std::array<ItemId, kMaxCraftSlots> cells{};
    for (std::size_t i = 0; i < kMaxCraftSlots; ++i) {
        switch (pattern[i]) {
        case 'M':
            cells[i] = material;
            break;
        case 'S':
            cells[i] = ItemId::Stick;
            break;
        default:
            cells[i] = kNone;
            break;
        }
    }
    Recipe recipe;
    recipe.pattern = cells;
    recipe.width = 3;
    recipe.height = 3;
    recipe.result = ItemStack{result, 1};
    return recipe;
}

} // namespace

/// Every recipe in the game.
///
/// Shapes and yields are taken from the reference recipe data - see
/// `CRAFTABLE.md`, which records each one and where it came from.
const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> table = [] {
        std::vector<Recipe> all{
            shapeless({itemForBlock(BlockId::Log)}, 1, itemForBlock(BlockId::Planks), 4),
            shaped(1, 2, {itemForBlock(BlockId::Planks), itemForBlock(BlockId::Planks)}, ItemId::Stick, 4),
            shaped(2, 2,
                   {itemForBlock(BlockId::Planks), itemForBlock(BlockId::Planks), itemForBlock(BlockId::Planks),
                    itemForBlock(BlockId::Planks)},
                   itemForBlock(BlockId::CraftingTable), 1),
            // A ring of eight, hollow in the middle - which is why the pattern
            // has to be stored at 3x3 and cannot be trimmed to its filled cells.
            shaped(3, 3,
                   {itemForBlock(BlockId::Cobblestone), itemForBlock(BlockId::Cobblestone),
                    itemForBlock(BlockId::Cobblestone), itemForBlock(BlockId::Cobblestone), kNone,
                    itemForBlock(BlockId::Cobblestone), itemForBlock(BlockId::Cobblestone),
                    itemForBlock(BlockId::Cobblestone), itemForBlock(BlockId::Cobblestone)},
                   itemForBlock(BlockId::Furnace), 1),
            // Charcoal comes from smelting a log, so torches need no ore at all.
            shaped(1, 2, {ItemId::Charcoal, ItemId::Stick}, itemForBlock(BlockId::Torch), 4),
        };

        struct ToolShape {
            const char* pattern;
            ItemId wooden;
            ItemId stone;
        };
        constexpr std::array<ToolShape, 5> shapes{{
            {"MMM.S..S.", ItemId::WoodenPickaxe, ItemId::StonePickaxe},
            {"MM.MS..S.", ItemId::WoodenAxe, ItemId::StoneAxe},
            {".M..S..S.", ItemId::WoodenShovel, ItemId::StoneShovel},
            {".M..M..S.", ItemId::WoodenSword, ItemId::StoneSword},
            {"MM..S..S.", ItemId::WoodenHoe, ItemId::StoneHoe},
        }};
        for (const ToolShape& shape : shapes) {
            all.push_back(tool(itemForBlock(BlockId::Planks), shape.wooden, shape.pattern));
            all.push_back(tool(itemForBlock(BlockId::Cobblestone), shape.stone, shape.pattern));
        }

        // Derived here rather than at each recipe, so neither can be forgotten
        // on a new row. A shapeless recipe keeps its ingredient *count* in
        // `width`, so it fits the player's grid when it uses four or fewer -
        // comparing it against the pattern extents would be wrong.
        for (Recipe& recipe : all) {
            recipe.category = categoryFor(recipe.result.item);
            recipe.fitsInTwoByTwo =
                recipe.shapeless ? recipe.width <= 4 : recipe.width <= 2 && recipe.height <= 2;
        }
        return all;
    }();
    return table;
}

namespace {

/// Bounding box of the occupied cells, so a pattern can be compared where it
/// actually sits rather than where it was dropped.
struct Bounds {
    int minX = kMaxCraftSize;
    int minY = kMaxCraftSize;
    int maxX = -1;
    int maxY = -1;

    bool empty() const { return maxX < 0; }
    int width() const { return maxX - minX + 1; }
    int height() const { return maxY - minY + 1; }
};

Bounds occupiedBounds(const ItemStack* slots, int size) {
    Bounds bounds;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            if (slots[static_cast<std::size_t>(y * size + x)].empty()) {
                continue;
            }
            bounds.minX = std::min(bounds.minX, x);
            bounds.minY = std::min(bounds.minY, y);
            bounds.maxX = std::max(bounds.maxX, x);
            bounds.maxY = std::max(bounds.maxY, y);
        }
    }
    return bounds;
}

bool matchesShaped(const Recipe& recipe, const ItemStack* slots, int size, const Bounds& bounds) {
    if (bounds.width() != recipe.width || bounds.height() != recipe.height) {
        return false;
    }
    for (int y = 0; y < recipe.height; ++y) {
        for (int x = 0; x < recipe.width; ++x) {
            const ItemId wanted = recipe.pattern[static_cast<std::size_t>(y * recipe.width + x)];
            const ItemStack& have =
                slots[static_cast<std::size_t>((bounds.minY + y) * size + bounds.minX + x)];
            if (wanted == kNone) {
                if (!have.empty()) {
                    return false;
                }
            } else if (have.empty() || have.item != wanted) {
                return false;
            }
        }
    }
    return true;
}

bool matchesShapeless(const Recipe& recipe, const ItemStack* slots, int size) {
    // Tick off each ingredient against one filled slot. Counting rather than
    // comparing sets, so a recipe wanting two of something is not satisfied by
    // one.
    std::array<bool, kMaxCraftSlots> used{};
    int matched = 0;

    for (int i = 0; i < recipe.width; ++i) {
        bool found = false;
        for (int s = 0; s < size * size && !found; ++s) {
            const auto at = static_cast<std::size_t>(s);
            if (used[at] || slots[at].empty() || slots[at].item != recipe.pattern[static_cast<std::size_t>(i)]) {
                continue;
            }
            used[at] = true;
            found = true;
            ++matched;
        }
        if (!found) {
            return false;
        }
    }

    // Nothing may be left over, or a grid holding an extra item would still
    // craft and quietly eat it.
    int filled = 0;
    for (int s = 0; s < size * size; ++s) {
        if (!slots[static_cast<std::size_t>(s)].empty()) {
            ++filled;
        }
    }
    return filled == matched;
}

const Recipe* findMatch(const ItemStack* slots, int size) {
    const Bounds bounds = occupiedBounds(slots, size);
    if (bounds.empty()) {
        return nullptr;
    }

    for (const Recipe& recipe : recipes()) {
        const bool hit = recipe.shapeless ? matchesShapeless(recipe, slots, size)
                                          : matchesShaped(recipe, slots, size, bounds);
        if (hit) {
            return &recipe;
        }
    }
    return nullptr;
}

} // namespace

ItemStack craftResult(const ItemStack* slots, int size) {
    const Recipe* recipe = findMatch(slots, size);
    return recipe != nullptr ? recipe->result : ItemStack{};
}

void consumeIngredients(ItemStack* slots, int size) {
    if (findMatch(slots, size) == nullptr) {
        return;
    }
    // One of everything present, which is right for every recipe here: a cell
    // holding a stack contributes exactly one item per craft.
    for (int s = 0; s < size * size; ++s) {
        ItemStack& slot = slots[static_cast<std::size_t>(s)];
        if (slot.empty()) {
            continue;
        }
        if (--slot.count <= 0) {
            slot = ItemStack{};
        }
    }
}

} // namespace game
