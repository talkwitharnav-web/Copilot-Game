#pragma once

#include "world/Block.hpp"

#include <cstdint>
#include <vector>

namespace game {

/// A thing that can sit in an inventory.
///
/// Deliberately not the same type as `BlockId`. Every placeable block has an
/// item form, but the reverse will not hold: a pickaxe is an item that is never
/// a block. Keeping them distinct now avoids unpicking it during crafting.
///
/// Block items **share the block's numbering**, so there is one list to
/// maintain rather than two that can silently drift apart. Anything that is not
/// a block starts above `kFirstToolItem`.
enum class ItemId : std::uint16_t {
    None = 0,

    /// Everything below this is a block item whose value equals its `BlockId`.
    kFirstToolItem = 256,

    Stick = kFirstToolItem,
    Charcoal,
    WoodenPickaxe,
    WoodenAxe,
    WoodenShovel,
    WoodenSword,
    WoodenHoe,
    StonePickaxe,
    StoneAxe,
    StoneShovel,
    StoneSword,
    StoneHoe,

    /// One spawn egg per creature, **in `CreatureKind` order and appended after
    /// every tool**. Both of those matter: the order is what lets the item, its
    /// sprite layer and the species it produces share one index, and appending
    /// keeps them outside `isTool`'s range test - inserted among the tools they
    /// would each become a one-slot wearing tool with mining power.
    ///
    /// Only the first is named. Thirty-six enumerators would be thirty-six
    /// chances to get the order wrong, and nothing needs to say `SpawnEggGoat`
    /// when `spawnEggFor(kind)` says it better.
    SpawnEggFirst,
};

/// The species offset an egg carries, or -1 if the item is not an egg.
constexpr int spawnEggIndex(ItemId item) {
    const int offset = static_cast<int>(item) - static_cast<int>(ItemId::SpawnEggFirst);
    return offset >= 0 && offset < kSpawnEggLayers ? offset : -1;
}

constexpr bool isSpawnEgg(ItemId item) {
    return spawnEggIndex(item) >= 0;
}

/// The egg for a species, by its `CreatureKind` index.
constexpr ItemId spawnEggForIndex(int kindIndex) {
    return static_cast<ItemId>(static_cast<int>(ItemId::SpawnEggFirst) + kindIndex);
}

/// Layer in the block texture array, for anything that is not a block.
///
/// Block items draw as a little cube built from their block textures; these
/// have nothing to build from and draw as a flat sprite instead. Negative means
/// this item has no sprite of its own.
constexpr int itemTextureLayer(ItemId item) {
    switch (item) {
    case ItemId::Stick:
        return static_cast<int>(TextureLayer::Stick);
    case ItemId::Charcoal:
        return static_cast<int>(TextureLayer::Charcoal);
    case ItemId::WoodenPickaxe:
        return static_cast<int>(TextureLayer::WoodenPickaxe);
    case ItemId::WoodenAxe:
        return static_cast<int>(TextureLayer::WoodenAxe);
    case ItemId::WoodenShovel:
        return static_cast<int>(TextureLayer::WoodenShovel);
    case ItemId::WoodenSword:
        return static_cast<int>(TextureLayer::WoodenSword);
    case ItemId::WoodenHoe:
        return static_cast<int>(TextureLayer::WoodenHoe);
    case ItemId::StonePickaxe:
        return static_cast<int>(TextureLayer::StonePickaxe);
    case ItemId::StoneAxe:
        return static_cast<int>(TextureLayer::StoneAxe);
    case ItemId::StoneShovel:
        return static_cast<int>(TextureLayer::StoneShovel);
    case ItemId::StoneSword:
        return static_cast<int>(TextureLayer::StoneSword);
    case ItemId::StoneHoe:
        return static_cast<int>(TextureLayer::StoneHoe);
    default:
        // Eggs are a contiguous run against a contiguous run of layers, so the
        // offset maps straight across rather than through thirty-six cases.
        if (const int egg = spawnEggIndex(item); egg >= 0) {
            return static_cast<int>(TextureLayer::SpawnEggFirst) + egg;
        }
        return -1;
    }
}

constexpr const char* itemName(ItemId item) {
    switch (item) {
    case ItemId::Stick:
        return "Stick";
    case ItemId::Charcoal:
        return "Charcoal";
    case ItemId::WoodenPickaxe:
        return "Wooden Pickaxe";
    case ItemId::WoodenAxe:
        return "Wooden Axe";
    case ItemId::WoodenShovel:
        return "Wooden Shovel";
    case ItemId::WoodenSword:
        return "Wooden Sword";
    case ItemId::WoodenHoe:
        return "Wooden Hoe";
    case ItemId::StonePickaxe:
        return "Stone Pickaxe";
    case ItemId::StoneAxe:
        return "Stone Axe";
    case ItemId::StoneShovel:
        return "Stone Shovel";
    case ItemId::StoneSword:
        return "Stone Sword";
    case ItemId::StoneHoe:
        return "Stone Hoe";
    default:
        // The species half of the name is added by the caller, which is the one
        // place that knows what a `CreatureKind` is called.
        return isSpawnEgg(item) ? "Spawn Egg" : "";
    }
}

constexpr ItemId itemForBlock(BlockId block) {
    return static_cast<ItemId>(block);
}

/// Which catalogue tab an item belongs under.
///
/// Bedrock's four names, which are better than the alternative's thirteen. The
/// Search tab is not a category — it shows everything — so it is a tab the UI
/// owns rather than a value an item can carry.
enum class ItemCategory : std::uint8_t {
    Construction,
    Equipment,
    Items,
    Nature,
    Count,
};

constexpr const char* categoryName(ItemCategory category) {
    switch (category) {
    case ItemCategory::Construction:
        return "Construction";
    case ItemCategory::Equipment:
        return "Equipment";
    case ItemCategory::Items:
        return "Items";
    case ItemCategory::Nature:
        return "Nature";
    default:
        return "";
    }
}

constexpr bool isBlockItem(ItemId item) {
    return item != ItemId::None && static_cast<std::uint16_t>(item) < static_cast<std::uint16_t>(ItemId::kFirstToolItem);
}

/// Only meaningful when `isBlockItem` holds.
constexpr BlockId blockForItem(ItemId item) {
    return static_cast<BlockId>(item);
}

/// What to call an item, whatever kind it is.
///
/// A block item is named by its block; casting one straight to a `BlockId` only
/// works for those, and silently reported every tool as "Air".
constexpr const char* itemDisplayName(ItemId item) {
    if (isBlockItem(item)) {
        return blockName(blockForItem(item));
    }
    const char* name = itemName(item);
    return name[0] != '\0' ? name : "Unknown";
}

/// How many of one item fit in a single slot.
constexpr int kMaxStack = 64;

/// Tools occupy the contiguous run from the first pickaxe to the last hoe.
constexpr bool isTool(ItemId item) {
    return item >= ItemId::WoodenPickaxe && item <= ItemId::StoneHoe;
}

/// **Tools never stack.** Two with different wear are not interchangeable, and
/// merging them would silently pick one damage value for both.
constexpr int maxStackFor(ItemId item) {
    return isTool(item) ? 1 : kMaxStack;
}

/// An item and how many of it. A count of zero means the slot is empty, and the
/// item is then meaningless.
struct ItemStack {
    ItemId item = ItemId::None;
    int count = 0;
    /// Wear on a tool. Meaningless for everything else, and the reason tools do
    /// not stack: two with different wear are not interchangeable.
    int damage = 0;

    constexpr bool empty() const { return count <= 0 || item == ItemId::None; }
    constexpr int space() const { return empty() ? kMaxStack : maxStackFor(item) - count; }
};

/// What a block turns into when broken.
///
/// Usually itself, but not always: stone yields cobblestone, and grass yields
/// plain dirt. Stairs and slabs collapse to their upright form so an inventory
/// does not fill with eight orientations of the same thing.
constexpr ItemId dropForBlock(BlockId block) {
    if (isStairs(block)) {
        return itemForBlock(BlockId::CobbleStairs0);
    }
    if (isSlab(block)) {
        return itemForBlock(BlockId::StoneSlab);
    }
    if (isWater(block)) {
        return ItemId::None;
    }
    // A furnace that happens to be alight is still just a furnace once broken.
    if (isFurnace(block)) {
        return itemForBlock(BlockId::Furnace);
    }
    switch (block) {
    case BlockId::Air:
        return ItemId::None;
    case BlockId::Stone:
        return itemForBlock(BlockId::Cobblestone);
    case BlockId::Grass:
        return itemForBlock(BlockId::Dirt);
    default:
        return itemForBlock(block);
    }
}

/// Whether a block is the one form of itself that belongs in a catalogue.
///
/// Stairs, the upper slab and a lit furnace are *placement states* of a single
/// item rather than items of their own, and `dropForBlock` already names which
/// state is the canonical one — so this asks it rather than listing them again.
/// A second stair or slab material therefore needs no change here.
constexpr bool isCanonicalBlockItem(BlockId block) {
    if (block == BlockId::Air || isWater(block)) {
        return false;
    }
    if (isStairs(block) || isSlab(block) || isFurnace(block)) {
        return dropForBlock(block) == itemForBlock(block);
    }
    return true;
}

/// Which tab an item sits under.
///
/// **This is ours, not measured** — the reference's own assignment lives in data
/// we do not have, and moving an entry is a one-line edit. A block added
/// without a case here lands in `Items`, which is visible in the catalogue
/// rather than silent.
constexpr ItemCategory categoryFor(ItemId item) {
    if (isTool(item)) {
        return ItemCategory::Equipment;
    }
    if (!isBlockItem(item)) {
        return ItemCategory::Items;
    }
    const BlockId block = blockForItem(item);
    if (isStairs(block) || isSlab(block) || isFurnace(block)) {
        return ItemCategory::Construction;
    }
    switch (block) {
    case BlockId::Stone:
    case BlockId::Cobblestone:
    case BlockId::Planks:
    case BlockId::Bricks:
    case BlockId::Glowstone:
    case BlockId::PlanksFence:
    case BlockId::CraftingTable:
    case BlockId::Torch:
        return ItemCategory::Construction;
    case BlockId::Dirt:
    case BlockId::Grass:
    case BlockId::Sand:
    case BlockId::Gravel:
    case BlockId::Snow:
    case BlockId::Log:
    case BlockId::Leaves:
    case BlockId::TallGrass:
        return ItemCategory::Nature;
    default:
        return ItemCategory::Items;
    }
}

/// Every item a catalogue can show, in declaration order.
///
/// Built from the block enum and the two contiguous runs rather than written
/// out, so adding a block or a species does not leave a second list behind to
/// forget. Declaration order is deliberate: neither edition sorts
/// alphabetically, and alphabetical would scatter the tool tiers.
inline const std::vector<ItemId>& allItems() {
    static const std::vector<ItemId> items = [] {
        std::vector<ItemId> all;
        for (int id = 0; id <= static_cast<int>(BlockId::Torch); ++id) {
            const auto block = static_cast<BlockId>(id);
            if (isCanonicalBlockItem(block)) {
                all.push_back(itemForBlock(block));
            }
        }
        for (int id = static_cast<int>(ItemId::kFirstToolItem);
             id < static_cast<int>(ItemId::SpawnEggFirst) + kSpawnEggLayers; ++id) {
            all.push_back(static_cast<ItemId>(id));
        }
        return all;
    }();
    return items;
}

} // namespace game
