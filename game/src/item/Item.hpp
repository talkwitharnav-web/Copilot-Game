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

    /// Resource items, appended **after** the whole spawn egg run.
    ///
    /// Inserting them among the eggs would shift every egg's id, and item ids
    /// are written to disk in the player's inventory - a saved world would come
    /// back holding the wrong things.
    Coal = SpawnEggFirst + kSpawnEggLayers,
    RawIron,
    IronIngot,
    RawGold,
    GoldIngot,
    RawCopper,
    CopperIngot,
    Diamond,
    Emerald,
    LapisLazuli,
    Redstone,

    /// Appended after the resource run, and for the same reason it was appended
    /// after the eggs: an item id is written into the player's inventory on
    /// disk, so inserting anywhere earlier makes a saved world come back
    /// holding the wrong things.
    Bucket,
    WaterBucket,

    /// Spawn eggs for species added after the first thirty-six, continuing in
    /// `CreatureKind` order. A second run for exactly the reason above: growing
    /// the first would shift every resource and bucket id behind it.
    SpawnEggExtraFirst,

    /// **The single owner of where the item run ends.** `allItems()` reads it,
    /// and a stale one silently drops the newest item from the catalogue.
    kLastItem = SpawnEggExtraFirst + kExtraSpawnEggLayers - 1,
};

/// Every species has an egg, across both runs.
constexpr int kSpawnEggItems = kSpawnEggLayers + kExtraSpawnEggLayers;

/// How many items the resource run holds. One contiguous run, so a sprite layer
/// is arithmetic rather than a case per item. Taken from the sprite count so
/// the eleven is written down once.
constexpr int kResourceItems = kResourceSpriteCount;

static_assert(static_cast<int>(ItemId::Redstone) - static_cast<int>(ItemId::Coal) + 1 == kResourceItems,
              "kResourceItems must cover the whole run from Coal to Redstone");

/// The resource offset an item carries, or -1 if it is not one.
constexpr int resourceIndex(ItemId item) {
    const int offset = static_cast<int>(item) - static_cast<int>(ItemId::Coal);
    return offset >= 0 && offset < kResourceItems ? offset : -1;
}

/// The species offset an egg carries, or -1 if the item is not an egg. Two
/// runs, so the second continues the numbering the first left off at.
constexpr int spawnEggIndex(ItemId item) {
    const int offset = static_cast<int>(item) - static_cast<int>(ItemId::SpawnEggFirst);
    if (offset >= 0 && offset < kSpawnEggLayers) {
        return offset;
    }
    const int extra = static_cast<int>(item) - static_cast<int>(ItemId::SpawnEggExtraFirst);
    return extra >= 0 && extra < kExtraSpawnEggLayers ? kSpawnEggLayers + extra : -1;
}

constexpr bool isSpawnEgg(ItemId item) {
    return spawnEggIndex(item) >= 0;
}

/// The egg for a species, by its `CreatureKind` index.
constexpr ItemId spawnEggForIndex(int kindIndex) {
    return kindIndex < kSpawnEggLayers
               ? static_cast<ItemId>(static_cast<int>(ItemId::SpawnEggFirst) + kindIndex)
               : static_cast<ItemId>(static_cast<int>(ItemId::SpawnEggExtraFirst) + kindIndex -
                                     kSpawnEggLayers);
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
    case ItemId::Bucket:
        return kBucketSpritesFirst;
    case ItemId::WaterBucket:
        return kBucketSpritesFirst + 1;
    default:
        // Eggs are a contiguous run against a contiguous run of layers, so the
        // offset maps straight across rather than through thirty-six cases.
        if (const int egg = spawnEggIndex(item); egg >= 0) {
            return egg < kSpawnEggLayers
                       ? static_cast<int>(TextureLayer::SpawnEggFirst) + egg
                       : kExtraSpawnEggFirst + egg - kSpawnEggLayers;
        }
        if (const int resource = resourceIndex(item); resource >= 0) {
            return kResourceSpritesFirst + resource;
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
    case ItemId::Coal:
        return "Coal";
    case ItemId::RawIron:
        return "Raw Iron";
    case ItemId::IronIngot:
        return "Iron Ingot";
    case ItemId::RawGold:
        return "Raw Gold";
    case ItemId::GoldIngot:
        return "Gold Ingot";
    case ItemId::RawCopper:
        return "Raw Copper";
    case ItemId::CopperIngot:
        return "Copper Ingot";
    case ItemId::Diamond:
        return "Diamond";
    case ItemId::Emerald:
        return "Emerald";
    case ItemId::LapisLazuli:
        return "Lapis Lazuli";
    case ItemId::Redstone:
        return "Redstone";
    case ItemId::Bucket:
        return "Bucket";
    case ItemId::WaterBucket:
        return "Water Bucket";
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
///
/// A full bucket does not stack either, and an empty one stacks only to
/// sixteen - both the reference's, and the full one matters: a stack of water
/// buckets that emptied one at a time would need a count on each.
constexpr int maxStackFor(ItemId item) {
    if (isTool(item) || item == ItemId::WaterBucket) {
        return 1;
    }
    if (item == ItemId::Bucket) {
        return 16;
    }
    return kMaxStack;
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
    // The world's floor is not a souvenir.
    if (block == BlockId::Bedrock) {
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
    // Ores give up their resource, not themselves. Counts and products are the
    // reference's: one each for coal, diamond and emerald, one *raw* metal for
    // iron and gold, and several for copper, redstone and lapis.
    case BlockId::CoalOre:
        return ItemId::Coal;
    case BlockId::IronOre:
        return ItemId::RawIron;
    case BlockId::GoldOre:
        return ItemId::RawGold;
    case BlockId::CopperOre:
        return ItemId::RawCopper;
    case BlockId::RedstoneOre:
        return ItemId::Redstone;
    case BlockId::LapisOre:
        return ItemId::LapisLazuli;
    case BlockId::DiamondOre:
        return ItemId::Diamond;
    case BlockId::EmeraldOre:
        return ItemId::Emerald;
    default:
        return itemForBlock(block);
    }
}

/// How many a block yields. One unless the reference says otherwise.
///
/// The reference rolls a range - copper 2-5, redstone 4-5, lapis 4-9 - and we
/// take the middle of each rather than adding randomness a generator has no
/// need of. Mining the same vein twice should give the same haul.
constexpr int dropCountForBlock(BlockId block) {
    switch (block) {
    case BlockId::CopperOre:
        return 3;
    case BlockId::RedstoneOre:
        return 4;
    case BlockId::LapisOre:
        return 6;
    default:
        return 1;
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
    case BlockId::Andesite:
    case BlockId::Diorite:
    case BlockId::Granite:
    case BlockId::SmoothStone:
    case BlockId::StoneBricks:
    case BlockId::MossyCobblestone:
    case BlockId::Obsidian:
    case BlockId::Sandstone:
    case BlockId::Bookshelf:
    case BlockId::Glass:
    case BlockId::Terracotta:
        return ItemCategory::Construction;
    case BlockId::Dirt:
    case BlockId::Grass:
    case BlockId::Sand:
    case BlockId::Gravel:
    case BlockId::Snow:
    case BlockId::Log:
    case BlockId::Leaves:
    case BlockId::TallGrass:
    case BlockId::Clay:
    case BlockId::Dandelion:
    case BlockId::Poppy:
    case BlockId::DeadBush:
    case BlockId::CoalOre:
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::LapisOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
    case BlockId::Deepslate:
    case BlockId::Bedrock:
    case BlockId::PackedIce:
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
        for (int id = 0; id <= static_cast<int>(kLastBlock); ++id) {
            const auto block = static_cast<BlockId>(id);
            if (isCanonicalBlockItem(block)) {
                all.push_back(itemForBlock(block));
            }
        }
        for (int id = static_cast<int>(ItemId::kFirstToolItem);
             id < static_cast<int>(ItemId::SpawnEggFirst); ++id) {
            all.push_back(static_cast<ItemId>(id));
        }
        // Then **every** egg, in species order, across both runs. The ids are
        // deliberately split - the resources and buckets sit between them,
        // because an item id is written into a save and could not be moved - so
        // walking ids alone shows thirty-six eggs, thirteen unrelated items,
        // and then the other six. What order they *display* in is ours.
        for (int kind = 0; kind < kSpawnEggItems; ++kind) {
            all.push_back(spawnEggForIndex(kind));
        }
        for (int id = static_cast<int>(ItemId::SpawnEggFirst) + kSpawnEggLayers;
             id <= static_cast<int>(ItemId::kLastItem); ++id) {
            const auto item = static_cast<ItemId>(id);
            if (!isSpawnEgg(item)) {
                all.push_back(item);
            }
        }
        return all;
    }();
    return items;
}

} // namespace game
