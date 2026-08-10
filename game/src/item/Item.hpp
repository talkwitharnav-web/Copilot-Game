#pragma once

#include "world/Block.hpp"

#include <array>
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
    ///
    /// **Moved from 256 to 4096 on 2026-08-07**, when the block run reached 253
    /// and `BlockId` was widened to sixteen bits. The boundary is the real
    /// ceiling on how many blocks can exist, so widening the type without
    /// moving this would have bought nothing at all.
    kFirstToolItem = 4096,

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

    /// Three more tool tiers, appended **at the very end** for the same reason
    /// everything else here was: an item id is written into the player's saved
    /// inventory, so inserting these beside the wood and stone tools - which is
    /// where they belong conceptually - would have turned every spawn egg,
    /// resource and bucket in a saved world into something else.
    ///
    /// **`isTool` therefore spans two runs**, exactly as the spawn eggs do. It
    /// has to: that predicate is what stops a thing wearing out and having
    /// mining power, and a tool outside it silently becomes a stacking trinket.
    ///
    /// Order within the run is pickaxe, axe, shovel, sword, hoe by rising tier,
    /// and it matches the sprite run exactly so a tool's icon is arithmetic.
    IronPickaxe = SpawnEggExtraFirst + kExtraSpawnEggLayers,
    IronAxe,
    IronShovel,
    IronSword,
    IronHoe,
    DiamondPickaxe,
    DiamondAxe,
    DiamondShovel,
    DiamondSword,
    DiamondHoe,
    EmberitePickaxe,
    EmberiteAxe,
    EmberiteShovel,
    EmberiteSword,
    EmberiteHoe,

    /// What the deepest ore becomes on the way to a tool. Scrap is smelted from
    /// ancient debris; four of it and four gold make an ingot.
    EmberiteScrap,
    EmberiteIngot,

    /// Food, appended at the very end for the reason everything else here was:
    /// an item id is written into the player's saved inventory.
    ///
    /// **The apple comes first and the rest are raw/cooked pairs in order.**
    /// That layout is load-bearing twice over: the sprite run maps across by
    /// arithmetic, and `cookedForm` is `raw + 1` rather than a table that could
    /// disagree with this list.
    Apple,
    RawPorkchop,
    CookedPorkchop,
    RawBeef,
    CookedBeef,
    RawChicken,
    CookedChicken,
    RawMutton,
    CookedMutton,
    RawCod,
    CookedCod,

    /// Everything from here was appended on 2026-08-07 as **one table-driven
    /// run**, the same shape `kExtraBlocks` uses: a row carries the name, so a
    /// new item costs an enumerator and a row rather than a case in each of
    /// three switches. Their sprites are one contiguous run too, so the icon is
    /// arithmetic.
    ///
    /// The dyes are last and in the reference's colour order, white through
    /// black - the same order the wool, concrete and terracotta families use, so
    /// a dyeing recipe can be arithmetic rather than forty-eight rows.
    Bread,
    Cookie,
    MelonSlice,
    Carrot,
    Potato,
    BakedPotato,
    Beetroot,
    SweetBerries,
    GoldenApple,
    PumpkinPie,

    String,
    Feather,
    Leather,
    Bone,
    Gunpowder,
    Slimeball,
    InkSac,
    GlowInkSac,
    ClayBall,
    Brick,
    Flint,
    Wheat,
    WheatSeeds,
    Sugar,
    Paper,
    Book,
    GlassBottle,
    Bowl,
    Egg,
    RottenFlesh,
    SpiderEye,
    Honeycomb,
    HoneyBottle,

    WhiteDye,
    OrangeDye,
    MagentaDye,
    LightBlueDye,
    YellowDye,
    LimeDye,
    PinkDye,
    GrayDye,
    LightGrayDye,
    CyanDye,
    PurpleDye,
    BlueDye,
    BrownDye,
    GreenDye,
    RedDye,
    BlackDye,

    /// Appended after the dyes. A bucket that carries something does not stack,
    /// which is the reference's rule and the reason `stack` is a table column
    /// rather than a test on the id.
    LavaBucket,
    MilkBucket,
    FlintAndSteel,
    AmethystShard,
    Quartz,
    NetherBrickItem,
    GlowstoneDust,
    DriedKelp,
    MagmaCream,

    /// Another batch. **Coined names get ours**: *Blaze*, *Ghast* and *Ender*
    /// are Mojang's inventions, so the rod, the tear and the pearl are renamed
    /// the way *Emberite*, *Bramble* and *Voidmite* already were. Everything
    /// else here is ordinary English and keeps its name.
    GlowBerries,
    RawRabbit,
    CookedRabbit,
    RawSalmon,
    CookedSalmon,
    RawTropicalFish,
    RawPufferfish,
    BeetrootSeeds,
    MelonSeeds,
    PumpkinSeeds,
    BoneMeal,
    PrismarineShard,
    PrismarineCrystals,
    NautilusShell,
    HeartOfTheSea,
    Scute,
    PhantomMembrane,
    CinderRod,
    CinderPowder,
    DrifterTear,
    VoidPearl,
    VoidEye,
    ChorusFruit,
    PoppedChorusFruit,
    RabbitHide,
    RabbitFoot,
    EchoShard,
    WaterBottle,

    /// The first ranged weapon. The bow wears like a tool without being one -
    /// it mines nothing - so it carries its durability in `toolFor` and its
    /// stack of one in the table below, rather than being pushed into `isTool`
    /// where it would gain mining power and an axe's stripping behaviour.
    Bow,
    Arrow,

    /// The one tool that is about collecting rather than speed, and the pod it
    /// is not for. Cocoa beans are the reference's own brown dye, which ours
    /// had been standing a mushroom in for.
    Shears,
    CocoaBeans,

    /// **The single owner of where the item run ends.** `allItems()` reads it,
    /// and a stale one silently drops the newest item from the catalogue.
    kLastItem = CocoaBeans,
};

/// Everything about one of the appended items, in the one place that owns it.
/// Indexed by `item - ItemId::Bread`, so **this array's order must match the
/// enum run exactly**.
struct ExtraItemInfo {
    const char* name;
    /// Edible. There is no hunger yet, so this only decides the catalogue tab -
    /// but it is the honest question and M21 will want it anyway.
    bool food = false;
    int stack = 64;
};

constexpr std::array<ExtraItemInfo, 90> kExtraItems{{
    {.name = "Bread", .food = true},
    {.name = "Cookie", .food = true},
    {.name = "Melon Slice", .food = true},
    {.name = "Carrot", .food = true},
    {.name = "Potato", .food = true},
    {.name = "Baked Potato", .food = true},
    {.name = "Beetroot", .food = true},
    {.name = "Sweet Berries", .food = true},
    {.name = "Golden Apple", .food = true},
    {.name = "Pumpkin Pie", .food = true},

    {.name = "String"},
    {.name = "Feather"},
    {.name = "Leather"},
    {.name = "Bone"},
    {.name = "Gunpowder"},
    {.name = "Slimeball"},
    {.name = "Ink Sac"},
    {.name = "Glow Ink Sac"},
    {.name = "Clay Ball"},
    {.name = "Brick"},
    {.name = "Flint"},
    {.name = "Wheat"},
    {.name = "Wheat Seeds"},
    {.name = "Sugar"},
    {.name = "Paper"},
    {.name = "Book"},
    {.name = "Glass Bottle"},
    {.name = "Bowl"},
    {.name = "Egg", .stack = 16},
    {.name = "Rotten Flesh", .food = true},
    {.name = "Spider Eye", .food = true},
    {.name = "Honeycomb"},
    {.name = "Honey Bottle", .food = true, .stack = 16},

    {.name = "White Dye"},
    {.name = "Orange Dye"},
    {.name = "Magenta Dye"},
    {.name = "Light Blue Dye"},
    {.name = "Yellow Dye"},
    {.name = "Lime Dye"},
    {.name = "Pink Dye"},
    {.name = "Gray Dye"},
    {.name = "Light Gray Dye"},
    {.name = "Cyan Dye"},
    {.name = "Purple Dye"},
    {.name = "Blue Dye"},
    {.name = "Brown Dye"},
    {.name = "Green Dye"},
    {.name = "Red Dye"},
    {.name = "Black Dye"},

    {.name = "Lava Bucket", .stack = 1},
    {.name = "Milk Bucket", .stack = 1},
    {.name = "Flint and Steel", .stack = 1},
    {.name = "Amethyst Shard"},
    {.name = "Nether Quartz"},
    {.name = "Nether Brick"},
    {.name = "Glowstone Dust"},
    {.name = "Dried Kelp", .food = true},
    {.name = "Magma Cream"},

    {.name = "Glow Berries", .food = true},
    {.name = "Raw Rabbit", .food = true},
    {.name = "Cooked Rabbit", .food = true},
    {.name = "Raw Salmon", .food = true},
    {.name = "Cooked Salmon", .food = true},
    {.name = "Tropical Fish", .food = true},
    {.name = "Pufferfish", .food = true},
    {.name = "Beetroot Seeds"},
    {.name = "Melon Seeds"},
    {.name = "Pumpkin Seeds"},
    {.name = "Bone Meal"},
    {.name = "Prismarine Shard"},
    {.name = "Prismarine Crystals"},
    {.name = "Nautilus Shell"},
    {.name = "Heart of the Sea"},
    {.name = "Scute"},
    {.name = "Phantom Membrane"},
    {.name = "Cinder Rod"},
    {.name = "Cinder Powder"},
    {.name = "Drifter Tear"},
    {.name = "Void Pearl", .stack = 16},
    {.name = "Void Eye", .stack = 16},
    {.name = "Chorus Fruit", .food = true},
    {.name = "Popped Chorus Fruit"},
    {.name = "Rabbit Hide"},
    {.name = "Rabbit's Foot"},
    {.name = "Echo Shard"},
    {.name = "Water Bottle", .stack = 1},

    {.name = "Bow", .stack = 1},
    {.name = "Arrow"},
    {.name = "Shears", .stack = 1},
    {.name = "Cocoa Beans"},
}};

constexpr bool isExtraItem(ItemId item) {
    return item >= ItemId::Bread && item <= ItemId::kLastItem;
}

constexpr const ExtraItemInfo& extraItemInfo(ItemId item) {
    return kExtraItems[static_cast<std::size_t>(static_cast<int>(item) -
                                               static_cast<int>(ItemId::Bread))];
}

static_assert(kExtraItems.size() == static_cast<std::size_t>(static_cast<int>(ItemId::kLastItem) -
                                                             static_cast<int>(ItemId::Bread) + 1),
              "kExtraItems must have exactly one row per id in the appended run");

/// The first dye, so a colour family maps onto a dye by arithmetic.
constexpr ItemId kFirstDye = ItemId::WhiteDye;
constexpr int kDyeColours = 16;

/// Shots a bow has in it. Spent one per arrow **fired**, so a draw released
/// below the minimum charge costs nothing.
constexpr int kBowDurability = 385;

/// Where the block/item boundary used to sit, and what a value above it has to
/// be shifted by to still mean the same item.
///
/// Only the loaders use these. Block entities store `ItemStack`s as raw bytes,
/// so a chest saved before the boundary moved holds ids that are still perfectly
/// readable - they simply mean something else now, and shifting them back is
/// lossless because no id was ever reused.
constexpr int kLegacyFirstToolItem = 256;
constexpr int kItemIdShift = static_cast<int>(ItemId::kFirstToolItem) - kLegacyFirstToolItem;

constexpr ItemId upgradeLegacyItemId(ItemId stored) {
    const int raw = static_cast<int>(stored);
    return raw >= kLegacyFirstToolItem ? static_cast<ItemId>(raw + kItemIdShift) : stored;
}

/// The raw foods, as one contiguous run of pairs.
constexpr int kFoodItems = 11;

/// True for anything that can be eaten. Hunger arrives with M21; until then
/// this is what makes the catalogue and the smelting table agree on the set.
constexpr bool isFood(ItemId item) {
    if (item >= ItemId::Apple && item <= ItemId::CookedCod) {
        return true;
    }
    return isExtraItem(item) && extraItemInfo(item).food;
}

/// True for a raw food that a furnace turns into something better.
constexpr bool isRawFood(ItemId item) {
    if (item < ItemId::RawPorkchop || item > ItemId::CookedCod) {
        return false;
    }
    // Pairs run raw, cooked, raw, cooked from `RawPorkchop`.
    return ((static_cast<int>(item) - static_cast<int>(ItemId::RawPorkchop)) % 2) == 0;
}

/// What a raw food becomes in a furnace. Derived from the pairing above rather
/// than listed, so the two cannot drift apart.
constexpr ItemId cookedForm(ItemId raw) {
    return static_cast<ItemId>(static_cast<int>(raw) + 1);
}

/// The fifteen appended tools, as one contiguous run.
constexpr int kUpgradeToolItems = 15;

static_assert(static_cast<int>(ItemId::EmberiteHoe) - static_cast<int>(ItemId::IronPickaxe) + 1 ==
                  kUpgradeToolItems,
              "the appended tool run must be exactly fifteen long");
static_assert(kUpgradeToolItems == kUpgradeToolSprites,
              "every appended tool needs its sprite, in the same order");

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
    case ItemId::EmberiteScrap:
        return kEmberiteScrapSprite;
    case ItemId::EmberiteIngot:
        return kEmberiteIngotSprite;
    default:
        // The appended run is contiguous against a contiguous run of layers.
        if (isExtraItem(item)) {
            return kExtraItemSpritesFirst +
                   (static_cast<int>(item) - static_cast<int>(ItemId::Bread));
        }
        // The first food run, against its own contiguous layers. Bounded to
        // that run: `isFood` now answers for the appended foods too, and they
        // take their sprites from the run above instead.
        if (item >= ItemId::Apple && item <= ItemId::CookedCod) {
            return kFoodSpritesFirst + (static_cast<int>(item) - static_cast<int>(ItemId::Apple));
        }
        // The appended tools are contiguous against a contiguous run of layers,
        // in the same order, so this is arithmetic rather than fifteen cases.
        if (item >= ItemId::IronPickaxe && item <= ItemId::EmberiteHoe) {
            return kUpgradeToolSpritesFirst +
                   (static_cast<int>(item) - static_cast<int>(ItemId::IronPickaxe));
        }
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
    if (isExtraItem(item)) {
        return extraItemInfo(item).name;
    }
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
    case ItemId::IronPickaxe:
        return "Iron Pickaxe";
    case ItemId::IronAxe:
        return "Iron Axe";
    case ItemId::IronShovel:
        return "Iron Shovel";
    case ItemId::IronSword:
        return "Iron Sword";
    case ItemId::IronHoe:
        return "Iron Hoe";
    case ItemId::DiamondPickaxe:
        return "Diamond Pickaxe";
    case ItemId::DiamondAxe:
        return "Diamond Axe";
    case ItemId::DiamondShovel:
        return "Diamond Shovel";
    case ItemId::DiamondSword:
        return "Diamond Sword";
    case ItemId::DiamondHoe:
        return "Diamond Hoe";
    case ItemId::EmberitePickaxe:
        return "Emberite Pickaxe";
    case ItemId::EmberiteAxe:
        return "Emberite Axe";
    case ItemId::EmberiteShovel:
        return "Emberite Shovel";
    case ItemId::EmberiteSword:
        return "Emberite Sword";
    case ItemId::EmberiteHoe:
        return "Emberite Hoe";
    case ItemId::EmberiteScrap:
        return "Emberite Scrap";
    case ItemId::EmberiteIngot:
        return "Emberite Ingot";
    case ItemId::Apple:
        return "Apple";
    case ItemId::RawPorkchop:
        return "Raw Porkchop";
    case ItemId::CookedPorkchop:
        return "Cooked Porkchop";
    case ItemId::RawBeef:
        return "Raw Beef";
    case ItemId::CookedBeef:
        return "Steak";
    case ItemId::RawChicken:
        return "Raw Chicken";
    case ItemId::CookedChicken:
        return "Cooked Chicken";
    case ItemId::RawMutton:
        return "Raw Mutton";
    case ItemId::CookedMutton:
        return "Cooked Mutton";
    case ItemId::RawCod:
        return "Raw Cod";
    case ItemId::CookedCod:
        return "Cooked Cod";
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

/// Tools occupy two runs: the wood and stone pair written first, and the iron,
/// diamond and Emberite tiers appended at the end of the whole item list.
///
/// **Two runs rather than one widened one**, for the reason given at the enum:
/// item ids are in the player's save. Missing the second run here does not
/// fail - it makes fifteen tools stack, never wear out and mine like a fist.
constexpr bool isTool(ItemId item) {
    return (item >= ItemId::WoodenPickaxe && item <= ItemId::StoneHoe) ||
           (item >= ItemId::IronPickaxe && item <= ItemId::EmberiteHoe);
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
    if (isExtraItem(item)) {
        return extraItemInfo(item).stack;
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
    // Lit TNT that is broken hands back an ordinary charge rather than nothing.
    if (block == BlockId::TntPrimed) {
        return itemForBlock(BlockId::Tnt);
    }
    // The deepslate half of an ore yields exactly what the stone half does.
    if (isDeepslateOre(block)) {
        block = stoneOreFor(block);
    }
    // Rock breaks into its rubble, which is the one place a block does not drop
    // itself. Deepslate had been missing its half of that rule.
    if (block == BlockId::Deepslate) {
        return itemForBlock(BlockId::CobbledDeepslate);
    }
    // Every cut shape hands back its family's own canonical id, so an upside
    // down spruce stair drops a spruce stair rather than the cobblestone one
    // the old two-line version named outright.
    if (isShapedBlock(block)) {
        return itemForBlock(shapedCanonical(block));
    }
    // How deep the snow lies is state, not seven items - so every depth hands
    // back one layer, and only that one layer is a catalogue entry.
    if (isSnowLayer(block)) {
        return itemForBlock(BlockId::SnowLayerFirst);
    }
    // Which wall a ladder is fixed to is a placement state, like a chest's hinge.
    if (isLadder(block)) {
        return itemForBlock(BlockId::LadderNorth);
    }
    // Which sides a vine clings to is likewise state, not sixteen items - and
    // several vines sharing one cell come away together and give **one** vine,
    // which is what one id per combination already guarantees.
    if (isVine(block)) {
        return itemForBlock(vineWith(ConnectAll));
    }
    if (isCocoa(block)) {
        return ItemId::CocoaBeans;
    }
    // Fire is here because water sweeps it aside like a plant, and a plant is
    // the one thing on that path that *does* drop.
    if (isFluid(block) || block == BlockId::Fire) {
        return ItemId::None;
    }
    // The world's floor is not a souvenir.
    if (block == BlockId::Bedrock) {
        return ItemId::None;
    }
    // A furnace that happens to be alight is still just a furnace once broken,
    // and a smoker is its own block rather than a furnace with a hat.
    if (isSmoker(block)) {
        return itemForBlock(BlockId::Smoker);
    }
    if (isFurnace(block)) {
        return itemForBlock(BlockId::Furnace);
    }
    // Which way a chest opens is a placement state, not four different chests.
    if (isChest(block)) {
        return itemForBlock(BlockId::Chest);
    }
    // A hive is one block however it is turned, and however full it is. What
    // the honey inside is worth is not a drop - harvesting it is the bee work,
    // and until that lands a full hive simply keeps its honey when moved.
    if (isBeehive(block)) {
        return itemForBlock(BlockId::Beehive);
    }
    switch (block) {
    case BlockId::Air:
        return ItemId::None;
    case BlockId::Stone:
        return itemForBlock(BlockId::Cobblestone);
    case BlockId::Grass:
        return itemForBlock(BlockId::Dirt);
    // A clay bank breaks into the balls it is made of, which is what gives the
    // brick chain its first link.
    case BlockId::Clay:
        return ItemId::ClayBall;
    // Three more that give up something other than themselves, all the
    // reference's: a lamp shatters into dust, a crystal into shards, and a bush
    // hands over its fruit rather than the bush.
    case BlockId::Glowstone:
        return ItemId::GlowstoneDust;
    case BlockId::AmethystCluster:
        return ItemId::AmethystShard;
    case BlockId::SweetBerryBush:
        return ItemId::SweetBerries;
    // A melon breaks into slices rather than into itself, which is what makes
    // the nine-slice recipe worth having.
    case BlockId::Melon:
        return ItemId::MelonSlice;
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
    // A ripe pod is worth three beans; anything younger, one.
    if (isCocoa(block)) {
        return cocoaAge(block) == 2 ? 3 : 1;
    }
    switch (block) {
    case BlockId::CopperOre:
        return 3;
    case BlockId::RedstoneOre:
        return 4;
    case BlockId::LapisOre:
        return 6;
    case BlockId::Clay:
        return 4;
    case BlockId::Glowstone:
        return 3;
    case BlockId::AmethystCluster:
        return 4;
    case BlockId::SweetBerryBush:
        return 2;
    // The reference rolls three to seven; ours takes the middle, like every
    // other drop here.
    case BlockId::Melon:
        return 5;
    default:
        return 1;
    }
}

/// Whether a block is the one form of itself that belongs in a catalogue.
///
/// Stairs, the upper slab, a lit furnace, a chest's four facings and a beehive's
/// four facings times its two honey states are all *placement states* of a
/// single item rather than items of their own, and `dropForBlock` already names
/// which state is the canonical one — so this asks it rather than listing them
/// again. A second stair or slab material therefore needs no change here.
///
/// **Anything that spends block ids on orientation or contents must be named
/// here**, or the catalogue shows one row per id: the beehive's eight arrived
/// as eight separate entries until it was added.
constexpr bool isCanonicalBlockItem(BlockId block) {
    // Neither fluid, fire nor a lit charge is something you can hold. Lava is
    // reachable through its bucket and fire through flint and steel.
    if (block == BlockId::Air || isFluid(block) || block == BlockId::Fire ||
        block == BlockId::TntPrimed) {
        return false;
    }
    if (isShapedBlock(block) || isFurnace(block) || isChest(block) || isBeehive(block) ||
        isLadder(block) || isVine(block) || isCocoa(block) || isSnowLayer(block)) {
        return dropForBlock(block) == itemForBlock(block);
    }
    return true;
}

static_assert(isCanonicalBlockItem(BlockId::Tnt) && !isCanonicalBlockItem(BlockId::TntPrimed),
              "a lit charge is a state of TNT, not a second item");
static_assert(!isCanonicalBlockItem(BlockId::Lava0) && !isCanonicalBlockItem(BlockId::Fire),
              "neither lava nor fire is a holdable item");
static_assert(isCanonicalBlockItem(BlockId::SnowLayerFirst) &&
                  !isCanonicalBlockItem(BlockId::SnowLayerLast),
              "how deep snow lies is a state, not seven separate items");

static_assert(isCanonicalBlockItem(BlockId::Beehive),
              "the plain hive is the form that belongs in the catalogue");
static_assert(!isCanonicalBlockItem(BlockId::BeehiveEast) &&
                  !isCanonicalBlockItem(BlockId::BeehiveHoney) &&
                  !isCanonicalBlockItem(BlockId::BeehiveHoneyWest),
              "a hive's facing and its honey are placement states, not eight separate items");

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
    // The bow is not a tool by `isTool`'s definition - it mines nothing - but it
    // is equipment by anybody's, and so is what it fires.
    if (item == ItemId::Bow || item == ItemId::Arrow || item == ItemId::Shears) {
        return ItemCategory::Equipment;
    }
    // Food sits under Nature in the reference's taxonomy, alongside the plants
    // and soil it comes from.
    if (isFood(item)) {
        return ItemCategory::Nature;
    }
    if (!isBlockItem(item)) {
        return ItemCategory::Items;
    }
    const BlockId block = blockForItem(item);
    // The appended run carries its own tab, because it is no longer sorted by
    // one: a range test worked only while every natural block happened to sit
    // at the end of the enum.
    if (isExtraBlock(block)) {
        return extraBlockInfo(block).natural ? ItemCategory::Nature : ItemCategory::Construction;
    }
    if (isShapedBlock(block) || isFurnace(block) || isChest(block)) {
        return ItemCategory::Construction;
    }
    if (isLadder(block) || block == BlockId::IronBars) {
        return ItemCategory::Construction;
    }
    if (isVine(block) || isCocoa(block)) {
        return ItemCategory::Nature;
    }
    if (isBeehive(block)) {
        return ItemCategory::Nature;
    }
    if (isSnowLayer(block)) {
        return ItemCategory::Nature;
    }
    switch (block) {
    case BlockId::Stone:
    case BlockId::Cobblestone:
    case BlockId::Planks:
    case BlockId::Bricks:
    case BlockId::Glowstone:
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
    case BlockId::EmberiteBlock:
    case BlockId::SmithingTable:
    case BlockId::Prismarine:
    case BlockId::SeaLantern:
    case BlockId::Tnt:
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
    case BlockId::Ice:
    case BlockId::BlueIce:
    case BlockId::AncientDebris:
    case BlockId::CoarseDirt:
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
