#pragma once

#include "world/Block.hpp"
#include "world/Effects.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace game {

/// How many kinds of potion there are, and where the ones worth tipping an
/// arrow with begin.
///
/// **Declared before the enum**, because the three potion runs inside it are
/// sized by these, while the table that describes them cannot be written until
/// `ItemId` exists. The same arrangement the cut-shape family counts use.
constexpr int kPotionTypes = 41;
/// Water, mundane, thick and awkward carry no effect, so there is nothing to
/// tip an arrow with until after them.
constexpr int kFirstTippedPotion = 4;

/// The three collectible runs, declared here for the same reason.
constexpr int kPotterySherds = 23;
constexpr int kGoatHorns = 8;
constexpr int kMusicDiscs = 22;
/// One firework star per dye. Asserted against `kDyeColours` further down,
/// which cannot be declared this early.
constexpr int kFireworkStars = 16;

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

    /// The farm's own crop, and the one plant that grows on soul sand rather
    /// than on tilled ground.
    NetherWart,

    /// **Six materials times four pieces, in slot order helmet-chest-legs-boots
    /// and rising by material.** Both orders are load-bearing: the sprite run
    /// below matches them exactly, so a piece's icon, its defence and its
    /// durability are all one offset rather than twenty-four cases.
    LeatherHelmet,
    LeatherChestplate,
    LeatherLeggings,
    LeatherBoots,
    ChainmailHelmet,
    ChainmailChestplate,
    ChainmailLeggings,
    ChainmailBoots,
    IronHelmet,
    IronChestplate,
    IronLeggings,
    IronBoots,
    GoldenHelmet,
    GoldenChestplate,
    GoldenLeggings,
    GoldenBoots,
    DiamondHelmet,
    DiamondChestplate,
    DiamondLeggings,
    DiamondBoots,
    EmberiteHelmet,
    EmberiteChestplate,
    EmberiteLeggings,
    EmberiteBoots,
    /// The one head that belongs to no set.
    TurtleHelmet,
    Shield,

    Saddle,
    NameTag,
    Lead,
    Elytra,
    TotemOfUndying,
    Spyglass,
    Brush,
    Trident,
    Crossbow,
    FishingRod,
    Compass,
    Clock,
    EmptyMap,
    FilledMap,
    RecoveryCompass,
    FireworkRocket,
    BookAndQuill,
    WrittenBook,

    MushroomStew,
    BeetrootSoup,
    RabbitStew,
    SuspiciousStew,
    EnchantedGoldenApple,
    PoisonousPotato,
    GoldenCarrot,
    GlisteringMelonSlice,

    PowderSnowBucket,
    CodBucket,
    SalmonBucket,
    TropicalFishBucket,
    PufferfishBucket,
    AxolotlBucket,

    /// A ninth of an ingot. Two items rather than a divergence: the golden
    /// carrot and the glistering melon are built from nuggets in the reference,
    /// and standing whole ingots in for them made both absurdly expensive.
    IronNugget,
    GoldNugget,

    /// The two things you brew with that did not exist before. Magma cream, a
    /// phantom membrane and a rabbit's foot were already in the equipment run
    /// above and are **not** repeated here - three of Mojang's own brewing
    /// reagents were already on the shelf.
    ///
    /// **`BlazeRod`, `BlazePowder` and `GhastTear` used to sit here too, and
    /// they were a second copy of `CinderRod`, `CinderPowder` and
    /// `DrifterTear` above** - added by a later milestone that read this run's
    /// own comment about what was already on the shelf and missed that the
    /// naming policy twenty lines up had already renamed them. Brewing held the
    /// Mojang-named copies and two of the coined ids had no readers at all.
    /// Deleted on 2026-08-18; `upgradeLegacyItemId` carries a saved one across.
    FermentedSpiderEye,
    DragonBreath,

    /// **Every potion, three ways.** The reference stores one item id with a
    /// data value 0-46; ours spends an id per potion, because a recipe here
    /// holds concrete ids and has nowhere to put a data value - and because a
    /// catalogue of one entry called "Potion" would be useless.
    ///
    /// All three runs are in `kPotions` order, so a potion, the splash form of
    /// it and the arrow tipped with it are one subtraction apart.
    PotionFirst,
    SplashPotionFirst = PotionFirst + kPotionTypes,
    /// **Thirty-seven, not forty-one.** The four potions that do nothing -
    /// water, mundane, thick and awkward - have nothing to tip an arrow with,
    /// so the run starts at the first potion that carries an effect.
    TippedArrowFirst = SplashPotionFirst + kPotionTypes,

    /// The third form: thrown, and it leaves a cloud where it lands.
    LingeringPotionFirst = TippedArrowFirst + kPotionTypes - kFirstTippedPotion,

    /// **Twenty-three pottery sherds.** Decoration and nothing else: each is a
    /// picture pressed into fired clay, and the words are plain English for
    /// what the picture shows rather than anyone's coinage.
    PotterySherdFirst = LingeringPotionFirst + kPotionTypes,
    PotterySherdLast = PotterySherdFirst + kPotterySherds - 1,

    /// **Eight goat horns, and the only thing that differs is the note.** One
    /// picture between them, which is the reference's arrangement too - blowing
    /// one is the whole of what it does.
    GoatHornFirst,
    GoatHornLast = GoatHornFirst + kGoatHorns - 1,

    /// **Twenty-two music discs**, played by dropping one into a jukebox.
    ///
    /// **Named divergence: the titles are ours.** A track name is expressive
    /// work in a way that *stone* and *sheep* are not, so every one of these is
    /// a word we chose, and none of them is Mojang's.
    ///
    /// **This is the only disc run, and it took a duplicate to notice.** A
    /// second run of fifteen carrying Mojang's own track titles sat above the
    /// saddle for five milestones - `isMusicDisc` covered only this one, so the
    /// catalogue offered thirty-seven discs of which fifteen had no reader
    /// anywhere and could not be played. Deleted on 2026-08-18; grep an
    /// enumerator's name before adding it, which is the lesson `RedstoneLampLit`
    /// and the froglights had already taught.
    MusicDiscFirst,
    MusicDiscLast = MusicDiscFirst + kMusicDiscs - 1,

    /// **Sixteen firework stars, one per dye.** What a rocket bursts into is
    /// the star you built it from, so the colour has to be in the id the way
    /// every other dyed family here keeps it.
    FireworkStarFirst,
    FireworkStarLast = FireworkStarFirst + kFireworkStars - 1,

    /// **The single owner of where the item run ends.** `allItems()` reads it,
    /// and a stale one silently drops the newest item from the catalogue.
    kLastItem = FireworkStarLast,
};

/// One kind of potion: what it does, how hard, and for how long.
///
/// **Turtle master is why there are two effects rather than one.** It is the
/// only brew in the reference that lands a pair, and giving every row a second
/// slot costs nothing where a special case would have to be remembered at every
/// site that applies one.
struct PotionKind {
    effects::Effect effect = effects::Effect::None;
    int amplifier = 0;
    float seconds = 0.0f;
    effects::Effect second = effects::Effect::None;
    int secondAmplifier = 0;
};

/// Every potion, in the order all three item runs use.
///
/// Durations are the reference's own, in seconds: three minutes for most,
/// eight when extended with redstone, half of the base when strengthened with
/// glowstone. The four at the top carry nothing - they are what you get for
/// brewing something useless into water, and they exist because the brewing
/// tree needs somewhere for a wrong ingredient to go.
constexpr std::array<PotionKind, kPotionTypes> kPotions{{
    {},                                                         // Water bottle
    {},                                                         // Mundane
    {},                                                         // Thick
    {},                                                         // Awkward
    {effects::Effect::NightVision, 0, 180.0f},
    {effects::Effect::NightVision, 0, 480.0f},
    {effects::Effect::Invisibility, 0, 180.0f},
    {effects::Effect::Invisibility, 0, 480.0f},
    {effects::Effect::JumpBoost, 0, 180.0f},
    {effects::Effect::JumpBoost, 0, 480.0f},
    {effects::Effect::JumpBoost, 1, 90.0f},
    {effects::Effect::FireResistance, 0, 180.0f},
    {effects::Effect::FireResistance, 0, 480.0f},
    {effects::Effect::Speed, 0, 180.0f},
    {effects::Effect::Speed, 0, 480.0f},
    {effects::Effect::Speed, 1, 90.0f},
    {effects::Effect::Slowness, 0, 90.0f},
    {effects::Effect::Slowness, 0, 240.0f},
    // Slowness IV, and its aux value is out of sequence in the reference too:
    // it was bolted on after the numbering was already spent.
    {effects::Effect::Slowness, 3, 20.0f},
    {effects::Effect::WaterBreathing, 0, 180.0f},
    {effects::Effect::WaterBreathing, 0, 480.0f},
    {effects::Effect::InstantHealth, 0, 0.0f},
    {effects::Effect::InstantHealth, 1, 0.0f},
    {effects::Effect::InstantDamage, 0, 0.0f},
    {effects::Effect::InstantDamage, 1, 0.0f},
    {effects::Effect::Poison, 0, 45.0f},
    // Two minutes, which is Bedrock's own extended poison. Java gives 1:30.
    {effects::Effect::Poison, 0, 120.0f},
    {effects::Effect::Poison, 1, 22.5f},
    {effects::Effect::Regeneration, 0, 45.0f},
    {effects::Effect::Regeneration, 0, 120.0f},
    {effects::Effect::Regeneration, 1, 22.5f},
    {effects::Effect::Strength, 0, 180.0f},
    {effects::Effect::Strength, 0, 480.0f},
    {effects::Effect::Strength, 1, 90.0f},
    {effects::Effect::Weakness, 0, 90.0f},
    {effects::Effect::Weakness, 0, 240.0f},
    {effects::Effect::Slowness, 3, 20.0f, effects::Effect::Resistance, 2},
    {effects::Effect::Slowness, 3, 40.0f, effects::Effect::Resistance, 2},
    {effects::Effect::Slowness, 5, 20.0f, effects::Effect::Resistance, 3},
    {effects::Effect::SlowFalling, 0, 90.0f},
    {effects::Effect::SlowFalling, 0, 240.0f},
}};

/// What each potion is called when you drink it.
///
/// **Three arrays rather than one name built at run time**, because `itemName`
/// hands back a `const char*` and has nowhere to build a string. They are in
/// `kPotions` order and each is `static_assert`ed to the same length, which is
/// the only thing standing between a misordered row and a potion of swiftness
/// that poisons you.
constexpr std::array<const char*, kPotionTypes> kPotionNames{{
    "Bottle of Water",
    "Mundane Potion",
    "Thick Potion",
    "Awkward Potion",
    "Potion of Night Vision",
    "Potion of Night Vision (Extended)",
    "Potion of Invisibility",
    "Potion of Invisibility (Extended)",
    "Potion of Leaping",
    "Potion of Leaping (Extended)",
    "Potion of Leaping (Enhanced)",
    "Potion of Fire Resistance",
    "Potion of Fire Resistance (Extended)",
    "Potion of Swiftness",
    "Potion of Swiftness (Extended)",
    "Potion of Swiftness (Enhanced)",
    "Potion of Slowness",
    "Potion of Slowness (Extended)",
    "Potion of Slowness (Enhanced)",
    "Potion of Water Breathing",
    "Potion of Water Breathing (Extended)",
    "Potion of Healing",
    "Potion of Healing (Enhanced)",
    "Potion of Harming",
    "Potion of Harming (Enhanced)",
    "Potion of Poison",
    "Potion of Poison (Extended)",
    "Potion of Poison (Enhanced)",
    "Potion of Regeneration",
    "Potion of Regeneration (Extended)",
    "Potion of Regeneration (Enhanced)",
    "Potion of Strength",
    "Potion of Strength (Extended)",
    "Potion of Strength (Enhanced)",
    "Potion of Weakness",
    "Potion of Weakness (Extended)",
    "Potion of the Turtle Master",
    "Potion of the Turtle Master (Extended)",
    "Potion of the Turtle Master (Enhanced)",
    "Potion of Slow Falling",
    "Potion of Slow Falling (Extended)",
}};

constexpr std::array<const char*, kPotionTypes> kSplashPotionNames{{
    "Splash Bottle of Water",
    "Splash Mundane Potion",
    "Splash Thick Potion",
    "Splash Awkward Potion",
    "Splash Potion of Night Vision",
    "Splash Potion of Night Vision (Extended)",
    "Splash Potion of Invisibility",
    "Splash Potion of Invisibility (Extended)",
    "Splash Potion of Leaping",
    "Splash Potion of Leaping (Extended)",
    "Splash Potion of Leaping (Enhanced)",
    "Splash Potion of Fire Resistance",
    "Splash Potion of Fire Resistance (Extended)",
    "Splash Potion of Swiftness",
    "Splash Potion of Swiftness (Extended)",
    "Splash Potion of Swiftness (Enhanced)",
    "Splash Potion of Slowness",
    "Splash Potion of Slowness (Extended)",
    "Splash Potion of Slowness (Enhanced)",
    "Splash Potion of Water Breathing",
    "Splash Potion of Water Breathing (Extended)",
    "Splash Potion of Healing",
    "Splash Potion of Healing (Enhanced)",
    "Splash Potion of Harming",
    "Splash Potion of Harming (Enhanced)",
    "Splash Potion of Poison",
    "Splash Potion of Poison (Extended)",
    "Splash Potion of Poison (Enhanced)",
    "Splash Potion of Regeneration",
    "Splash Potion of Regeneration (Extended)",
    "Splash Potion of Regeneration (Enhanced)",
    "Splash Potion of Strength",
    "Splash Potion of Strength (Extended)",
    "Splash Potion of Strength (Enhanced)",
    "Splash Potion of Weakness",
    "Splash Potion of Weakness (Extended)",
    "Splash Potion of the Turtle Master",
    "Splash Potion of the Turtle Master (Extended)",
    "Splash Potion of the Turtle Master (Enhanced)",
    "Splash Potion of Slow Falling",
    "Splash Potion of Slow Falling (Extended)",
}};

/// The tipped arrows, which start at the first potion that carries an effect.
constexpr std::array<const char*, kPotionTypes - kFirstTippedPotion> kTippedArrowNames{{
    "Arrow of Night Vision",
    "Arrow of Night Vision (Extended)",
    "Arrow of Invisibility",
    "Arrow of Invisibility (Extended)",
    "Arrow of Leaping",
    "Arrow of Leaping (Extended)",
    "Arrow of Leaping (Enhanced)",
    "Arrow of Fire Resistance",
    "Arrow of Fire Resistance (Extended)",
    "Arrow of Swiftness",
    "Arrow of Swiftness (Extended)",
    "Arrow of Swiftness (Enhanced)",
    "Arrow of Slowness",
    "Arrow of Slowness (Extended)",
    "Arrow of Slowness (Enhanced)",
    "Arrow of Water Breathing",
    "Arrow of Water Breathing (Extended)",
    "Arrow of Healing",
    "Arrow of Healing (Enhanced)",
    "Arrow of Harming",
    "Arrow of Harming (Enhanced)",
    "Arrow of Poison",
    "Arrow of Poison (Extended)",
    "Arrow of Poison (Enhanced)",
    "Arrow of Regeneration",
    "Arrow of Regeneration (Extended)",
    "Arrow of Regeneration (Enhanced)",
    "Arrow of Strength",
    "Arrow of Strength (Extended)",
    "Arrow of Strength (Enhanced)",
    "Arrow of Weakness",
    "Arrow of Weakness (Extended)",
    "Arrow of the Turtle Master",
    "Arrow of the Turtle Master (Extended)",
    "Arrow of the Turtle Master (Enhanced)",
    "Arrow of Slow Falling",
    "Arrow of Slow Falling (Extended)",
}};

/// The third form. Same forty-one in the same order, thrown rather than drunk,
/// and what they leave behind is a cloud rather than a splash.
constexpr std::array<const char*, kPotionTypes> kLingeringPotionNames{{    "Lingering Bottle of Water",
    "Lingering Mundane Potion",
    "Lingering Thick Potion",
    "Lingering Awkward Potion",
    "Lingering Potion of Night Vision",
    "Lingering Potion of Night Vision (Extended)",
    "Lingering Potion of Invisibility",
    "Lingering Potion of Invisibility (Extended)",
    "Lingering Potion of Leaping",
    "Lingering Potion of Leaping (Extended)",
    "Lingering Potion of Leaping (Enhanced)",
    "Lingering Potion of Fire Resistance",
    "Lingering Potion of Fire Resistance (Extended)",
    "Lingering Potion of Swiftness",
    "Lingering Potion of Swiftness (Extended)",
    "Lingering Potion of Swiftness (Enhanced)",
    "Lingering Potion of Slowness",
    "Lingering Potion of Slowness (Extended)",
    "Lingering Potion of Slowness (Enhanced)",
    "Lingering Potion of Water Breathing",
    "Lingering Potion of Water Breathing (Extended)",
    "Lingering Potion of Healing",
    "Lingering Potion of Healing (Enhanced)",
    "Lingering Potion of Harming",
    "Lingering Potion of Harming (Enhanced)",
    "Lingering Potion of Poison",
    "Lingering Potion of Poison (Extended)",
    "Lingering Potion of Poison (Enhanced)",
    "Lingering Potion of Regeneration",
    "Lingering Potion of Regeneration (Extended)",
    "Lingering Potion of Regeneration (Enhanced)",
    "Lingering Potion of Strength",
    "Lingering Potion of Strength (Extended)",
    "Lingering Potion of Strength (Enhanced)",
    "Lingering Potion of Weakness",
    "Lingering Potion of Weakness (Extended)",
    "Lingering Potion of the Turtle Master",
    "Lingering Potion of the Turtle Master (Extended)",
    "Lingering Potion of the Turtle Master (Enhanced)",
    "Lingering Potion of Slow Falling",
    "Lingering Potion of Slow Falling (Extended)",
}};

/// The twenty-three sherds, in the order their pictures are staged.
///
/// Every one of these words is ordinary English for what is pressed into the
/// clay, which is why they can stay: a *sheaf* and a *mourner* are things,
/// not names anyone coined.
constexpr std::array<const char*, kPotterySherds> kPotterySherdNames{{
    "Angler Pottery Sherd",  "Archer Pottery Sherd",  "Arms Up Pottery Sherd",
    "Blade Pottery Sherd",   "Brewer Pottery Sherd",  "Burn Pottery Sherd",
    "Danger Pottery Sherd",  "Explorer Pottery Sherd", "Flow Pottery Sherd",
    "Friend Pottery Sherd",  "Gust Pottery Sherd",    "Heart Pottery Sherd",
    "Heartbreak Pottery Sherd", "Howl Pottery Sherd", "Miner Pottery Sherd",
    "Mourner Pottery Sherd", "Plenty Pottery Sherd",  "Prize Pottery Sherd",
    "Scrape Pottery Sherd",  "Sheaf Pottery Sherd",   "Shelter Pottery Sherd",
    "Skull Pottery Sherd",   "Snort Pottery Sherd",
}};

/// Eight horns, low to high. **Named for the note rather than for a mood**,
/// because the note is the only thing that differs and it is the thing you are
/// choosing between.
constexpr std::array<const char*, kGoatHorns> kGoatHornNames{{
    "Deep Goat Horn", "Low Goat Horn",   "Hollow Goat Horn", "Warm Goat Horn",
    "Long Goat Horn", "Clear Goat Horn", "Bright Goat Horn", "Sharp Goat Horn",
}};

/// Twenty-two discs. **Every title here is ours**, for the reason the id run
/// gives: a track name is expressive work in a way that *stone* is not.
constexpr std::array<const char*, kMusicDiscs> kMusicDiscNames{{
    "Music Disc: Amble",  "Music Disc: Beacon",   "Music Disc: Cinder",
    "Music Disc: Drift",  "Music Disc: Ember",    "Music Disc: Fathom",
    "Music Disc: Glimmer", "Music Disc: Hollow",  "Music Disc: Idle",
    "Music Disc: Jubilee", "Music Disc: Kindle",  "Music Disc: Lantern",
    "Music Disc: Murmur", "Music Disc: Nightfall", "Music Disc: Orbit",
    "Music Disc: Pilgrim", "Music Disc: Quarry",  "Music Disc: Ripple",
    "Music Disc: Summit", "Music Disc: Thicket",  "Music Disc: Undertow",
    "Music Disc: Vigil",
}};

/// The sixteen stars, in dye order, so a colour is one subtraction.
constexpr std::array<const char*, kFireworkStars> kFireworkStarNames{{
    "White Firework Star",  "Orange Firework Star", "Magenta Firework Star",
    "Light Blue Firework Star", "Yellow Firework Star", "Lime Firework Star",
    "Pink Firework Star",   "Gray Firework Star",   "Light Gray Firework Star",
    "Cyan Firework Star",   "Purple Firework Star", "Blue Firework Star",
    "Brown Firework Star",  "Green Firework Star",  "Red Firework Star",
    "Black Firework Star",
}};

constexpr bool isDrinkablePotion(ItemId item) {
    return item >= ItemId::PotionFirst &&
           item < static_cast<ItemId>(static_cast<int>(ItemId::PotionFirst) + kPotionTypes);
}

constexpr bool isSplashPotion(ItemId item) {
    return item >= ItemId::SplashPotionFirst &&
           item < static_cast<ItemId>(static_cast<int>(ItemId::SplashPotionFirst) + kPotionTypes);
}

constexpr bool isTippedArrow(ItemId item) {
    return item >= ItemId::TippedArrowFirst && item < ItemId::LingeringPotionFirst;
}

constexpr bool isLingeringPotion(ItemId item) {
    return item >= ItemId::LingeringPotionFirst && item < ItemId::PotterySherdFirst;
}

constexpr bool isPotterySherd(ItemId item) {
    return item >= ItemId::PotterySherdFirst && item <= ItemId::PotterySherdLast;
}

constexpr bool isGoatHorn(ItemId item) {
    return item >= ItemId::GoatHornFirst && item <= ItemId::GoatHornLast;
}

constexpr bool isMusicDisc(ItemId item) {
    return item >= ItemId::MusicDiscFirst && item <= ItemId::MusicDiscLast;
}

constexpr bool isFireworkStar(ItemId item) {
    return item >= ItemId::FireworkStarFirst && item <= ItemId::FireworkStarLast;
}

/// Which of the twenty-two this is, so a jukebox can keep it in one number.
constexpr int musicDiscIndex(ItemId item) {
    return static_cast<int>(item) - static_cast<int>(ItemId::MusicDiscFirst);
}

constexpr ItemId musicDiscAt(int index) {
    return static_cast<ItemId>(static_cast<int>(ItemId::MusicDiscFirst) + index);
}

/// The note a horn sounds, as a multiplier on the pitch. **Two octaves across
/// the eight**, low to high, so which horn you are holding is audible rather
/// than only readable.
constexpr float goatHornPitch(ItemId item) {
    const int step = static_cast<int>(item) - static_cast<int>(ItemId::GoatHornFirst);
    float pitch = 0.5f;
    for (int i = 0; i < step; ++i) {
        pitch *= 1.2f;
    }
    return pitch;
}

/// Every form that is a bottle rather than an arrow.
constexpr bool isPotion(ItemId item) {
    return isDrinkablePotion(item) || isSplashPotion(item) || isLingeringPotion(item);
}

/// Either form you throw.
constexpr bool isThrownPotion(ItemId item) {
    return isSplashPotion(item) || isLingeringPotion(item);
}

/// Which of the forty-one this is, whichever of the three forms it came in, or
/// **-1 for anything that is not a brew at all**.
///
/// The tipped-arrow arithmetic used to be the unguarded tail, so this ran for
/// every item in the game: a stick came back a large negative, and `potionKind`
/// fed that straight into `kPotions[...]`. Every caller happened to be guarded,
/// which is a property of today's callers rather than of this function.
constexpr int potionIndex(ItemId item) {
    if (isDrinkablePotion(item)) {
        return static_cast<int>(item) - static_cast<int>(ItemId::PotionFirst);
    }
    if (isSplashPotion(item)) {
        return static_cast<int>(item) - static_cast<int>(ItemId::SplashPotionFirst);
    }
    if (isLingeringPotion(item)) {
        return static_cast<int>(item) - static_cast<int>(ItemId::LingeringPotionFirst);
    }
    if (isTippedArrow(item)) {
        return static_cast<int>(item) - static_cast<int>(ItemId::TippedArrowFirst) +
               kFirstTippedPotion;
    }
    return -1;
}

/// **The bounds check lives here, not in the four callers.** An index off the
/// table gives back the empty row, which is what water already is - an effect of
/// `None` for no time, and every site that applies one already does nothing with
/// that.
constexpr PotionKind potionKind(ItemId item) {
    const int index = potionIndex(item);
    return index >= 0 && index < kPotionTypes ? kPotions[static_cast<std::size_t>(index)]
                                              : PotionKind{};
}

/// The drinkable form of whatever this is. **The single owner of stepping
/// between the three runs**, so a splash potion and the arrow tipped with it
/// cannot end up naming different brews.
constexpr ItemId potionAt(int index, int form) {
    if (form == 1) {
        return static_cast<ItemId>(static_cast<int>(ItemId::SplashPotionFirst) + index);
    }
    if (form == 2) {
        return static_cast<ItemId>(static_cast<int>(ItemId::TippedArrowFirst) + index -
                                   kFirstTippedPotion);
    }
    if (form == 3) {
        return static_cast<ItemId>(static_cast<int>(ItemId::LingeringPotionFirst) + index);
    }
    return static_cast<ItemId>(static_cast<int>(ItemId::PotionFirst) + index);
}

static_assert(potionIndex(potionAt(17, 0)) == 17 && potionIndex(potionAt(17, 1)) == 17 &&
                  potionIndex(potionAt(17, 2)) == 17,
              "a potion, its splash form and the arrow tipped with it must name one brew");
static_assert(isDrinkablePotion(potionAt(0, 0)) && isSplashPotion(potionAt(0, 1)) &&
                  isTippedArrow(potionAt(kFirstTippedPotion, 2)),
              "each form must fall inside its own run");
static_assert(isFireworkStar(ItemId::kLastItem) && !isMusicDisc(ItemId::kLastItem),
              "the item run must end on the last firework star");
static_assert(potionIndex(ItemId::Stick) == -1 && potionIndex(ItemId::MusicDiscFirst) == -1 &&
                  potionIndex(ItemId::kLastItem) == -1 &&
                  potionKind(ItemId::kLastItem).effect == effects::Effect::None,
              "nothing that is not a brew may produce an index into kPotions. Restoring the "
              "tipped-arrow arithmetic as an unguarded tail makes a stick a large negative and "
              "every disc, sherd and firework star a large *positive* - and it is the positive "
              "that reads off the end of kPotions. `< 0` here would prove neither");
static_assert(kSherdSprites == kPotterySherds && kMusicDiscSprites == kMusicDiscs,
              "the collectible sprite runs in Block.hpp must match the item runs here");
static_assert(kPotionSpriteTypes == kPotionTypes &&
                  kTippedArrowSprites == kPotionTypes - kFirstTippedPotion,
              "the potion sprite runs in Block.hpp must be as long as the potion table here");


/// Everything about one of the appended items, in the one place that owns it.
/// Indexed by `item - ItemId::Bread`, so **this array's order must match the
/// enum run exactly**.
struct ExtraItemInfo {
    const char* name;
    /// Edible. Decides the catalogue tab; `survival::foodValue` is what says
    /// how much hunger it restores, and the two must name the same set.
    bool food = false;
    int stack = 64;
};

constexpr std::array<ExtraItemInfo, 153> kExtraItems{{
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
    {.name = "Void Eye", .stack = 64},
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

    {.name = "Nether Wart"},

    {.name = "Leather Cap", .stack = 1},
    {.name = "Leather Tunic", .stack = 1},
    {.name = "Leather Trousers", .stack = 1},
    {.name = "Leather Boots", .stack = 1},
    {.name = "Chainmail Helmet", .stack = 1},
    {.name = "Chainmail Chestplate", .stack = 1},
    {.name = "Chainmail Leggings", .stack = 1},
    {.name = "Chainmail Boots", .stack = 1},
    {.name = "Iron Helmet", .stack = 1},
    {.name = "Iron Chestplate", .stack = 1},
    {.name = "Iron Leggings", .stack = 1},
    {.name = "Iron Boots", .stack = 1},
    {.name = "Golden Helmet", .stack = 1},
    {.name = "Golden Chestplate", .stack = 1},
    {.name = "Golden Leggings", .stack = 1},
    {.name = "Golden Boots", .stack = 1},
    {.name = "Diamond Helmet", .stack = 1},
    {.name = "Diamond Chestplate", .stack = 1},
    {.name = "Diamond Leggings", .stack = 1},
    {.name = "Diamond Boots", .stack = 1},
    {.name = "Emberite Helmet", .stack = 1},
    {.name = "Emberite Chestplate", .stack = 1},
    {.name = "Emberite Leggings", .stack = 1},
    {.name = "Emberite Boots", .stack = 1},
    {.name = "Turtle Shell", .stack = 1},
    {.name = "Shield", .stack = 1},

    {.name = "Saddle", .stack = 1},
    // Name tags stack to 64 in both editions - minecraft.wiki, *Name Tag*,
    // infobox "Stackable: Yes (64)", and the anvil section's "a stack of up to
    // 64 name tags can be renamed at once". Only the *named* ones fail to
    // stack together, which needs per-item text this codebase does not have.
    {.name = "Name Tag"},
    {.name = "Lead"},
    {.name = "Elytra", .stack = 1},
    {.name = "Totem of Undying", .stack = 1},
    {.name = "Spyglass", .stack = 1},
    {.name = "Brush", .stack = 1},
    {.name = "Trident", .stack = 1},
    {.name = "Crossbow", .stack = 1},
    {.name = "Fishing Rod", .stack = 1},
    {.name = "Compass", .stack = 64},
    {.name = "Clock", .stack = 64},
    {.name = "Empty Map"},
    {.name = "Map", .stack = 1},
    {.name = "Recovery Compass", .stack = 64},
    {.name = "Firework Rocket"},
    {.name = "Book and Quill", .stack = 1},
    {.name = "Written Book", .stack = 16},

    {.name = "Mushroom Stew", .food = true, .stack = 1},
    {.name = "Beetroot Soup", .food = true, .stack = 1},
    {.name = "Rabbit Stew", .food = true, .stack = 1},
    {.name = "Suspicious Stew", .food = true, .stack = 1},
    {.name = "Enchanted Golden Apple", .food = true},
    {.name = "Poisonous Potato", .food = true},
    {.name = "Golden Carrot", .food = true},
    {.name = "Glistering Melon Slice"},

    {.name = "Powder Snow Bucket", .stack = 1},
    {.name = "Bucket of Cod", .stack = 1},
    {.name = "Bucket of Salmon", .stack = 1},
    {.name = "Bucket of Tropical Fish", .stack = 1},
    {.name = "Bucket of Pufferfish", .stack = 1},
    {.name = "Bucket of Axolotl", .stack = 1},

    {.name = "Iron Nugget"},
    {.name = "Gold Nugget"},

    {.name = "Fermented Spider Eye"},
    {.name = "Dragon's Breath"},
}};

/// **The appended run stops at the last ingredient, not at `kLastItem`.** The
/// potions sit above it and carry their own tables - a row apiece here would be
/// a hundred and nineteen rows saying nothing that `kPotions` does not already
/// say, and the size assert below would have had to grow with every one.
constexpr bool isExtraItem(ItemId item) {
    return item >= ItemId::Bread && item <= ItemId::DragonBreath;
}

constexpr const ExtraItemInfo& extraItemInfo(ItemId item) {
    return kExtraItems[static_cast<std::size_t>(static_cast<int>(item) -
                                               static_cast<int>(ItemId::Bread))];
}

static_assert(kExtraItems.size() ==
                  static_cast<std::size_t>(static_cast<int>(ItemId::DragonBreath) -
                                           static_cast<int>(ItemId::Bread) + 1),
              "kExtraItems must have exactly one row per id in the appended run");

/// **The sprite counts, pinned against the enum they were counted from.**
///
/// `Block.hpp` states at `kExtraItemSprites` exactly how this goes wrong - a
/// count left too high "does not fail to compile and does not fail to draw. It
/// slides every icon above the gap onto the item fifteen slots below it, which
/// is a thing only eyes catch" - and then cannot assert it, because that header
/// has no `ItemId` to count. **This side can see both numbers, so it owes the
/// assert.** `Main.cpp` checks the atlas against these constants at startup,
/// which is a different drift: it would find the pictures and the counts
/// agreeing perfectly while both disagreed with the enum.
static_assert(kBrewingSprites == static_cast<int>(ItemId::DragonBreath) -
                                     static_cast<int>(ItemId::FermentedSpiderEye) + 1,
              "kBrewingSprites must cover exactly the ids itemTextureLayer answers from it");
static_assert(kExtraItemSprites == static_cast<int>(kExtraItems.size()) - kBrewingSprites,
              "kExtraItemSprites must be the appended run less the brewing ids caught ahead of "
              "it. Appending a row to kExtraItems without bumping the count in Block.hpp is "
              "what lands here");

/// The first dye, so a colour family maps onto a dye by arithmetic.
constexpr ItemId kFirstDye = ItemId::WhiteDye;
constexpr int kDyeColours = 16;
static_assert(kFireworkStars == kDyeColours, "a firework star per dye, and no more");

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

/// The second move: the two duplicate runs deleted on 2026-08-18.
///
/// Fifteen shadow music discs sat immediately before the saddle, and three
/// Mojang-named brewing reagents sat immediately after the gold nugget. Removing
/// eighteen enumerators slid every id above them down, and an item id is written
/// into a chest, a furnace and the player's inventory on disk.
///
/// **Both anchors are derived from ids that survived, not written down.** The
/// saddle now occupies the slot the first shadow disc used to, so it *is* the
/// old disc's number; and the fermented spider eye is fifteen discs plus two
/// rods below where the blaze rod was, so adding those fifteen back recovers it.
/// A pair of literals here would be two numbers nothing checks, going stale the
/// first time anything is inserted below - which is the same mistake in a
/// different costume.
constexpr int kLegacyShadowDiscs = 15;
constexpr int kLegacyShadowDiscFirst = static_cast<int>(ItemId::Saddle);
constexpr int kLegacyBlazeRod = static_cast<int>(ItemId::FermentedSpiderEye) + kLegacyShadowDiscs;
/// Eighteen ids gone in total: fifteen discs, two rods and a tear.
constexpr int kDuplicateRunItems = kLegacyShadowDiscs + 3;

/// **Stage one alone: the block/item boundary moving from 256 to 4096.**
///
/// Right for a file written *before* that move and for nothing else. In that
/// era block ids were eight bits, so "at or above the old boundary" and "is an
/// item" were the same statement.
///
/// **The upper bound is a courtesy to the shim below, and nothing else.** It
/// lets an id already in the current numbering fall through untouched, which is
/// what keeps the composed `upgradeLegacyItemId` behaving as it always has. It
/// is inert for real legacy data - that era never had four thousand items - and
/// it emphatically does **not** make the second stage safe on a newer file:
/// block items live *below* 4096, so the bound never sees them. Only the era
/// decides, and `ItemEra` in `WorldStore.cpp` is where that lives.
constexpr ItemId upgradeItemBoundary(ItemId stored) {
    const int raw = static_cast<int>(stored);
    const bool legacyItem = raw >= kLegacyFirstToolItem &&
                            raw < static_cast<int>(ItemId::kFirstToolItem);
    return legacyItem ? static_cast<ItemId>(raw + kItemIdShift) : stored;
}

/// **Stage two alone: the two duplicate runs deleted on 2026-08-18.**
///
/// Safe on any id from any file written since the boundary moved, **with no
/// guard at all**, because both deleted runs sat above `kFirstToolItem` - so
/// every block item takes the first branch and comes straight back out. That is
/// one fact about where the runs were, not a restatement of the migration, and
/// `WorldStore.cpp` asserts it separately so it cannot quietly stop being true.
constexpr ItemId upgradeDuplicateRuns(ItemId stored) {
    const int raw = static_cast<int>(stored);

    // Below the first shadow disc nothing moved, which is every block item.
    if (raw < kLegacyShadowDiscFirst) {
        return stored;
    }
    if (raw < kLegacyShadowDiscFirst + kLegacyShadowDiscs) {
        // A shadow disc, which no longer exists. It becomes the disc at the same
        // offset in the run that survived - twenty-two are ours and fifteen were
        // never playable, so handing back a real one is strictly better than
        // handing back a hole, and the jukebox can read it.
        return static_cast<ItemId>(static_cast<int>(ItemId::MusicDiscFirst) + raw -
                                   kLegacyShadowDiscFirst);
    }
    if (raw < kLegacyBlazeRod) {
        return static_cast<ItemId>(raw - kLegacyShadowDiscs);
    }
    // The three reagents that were a second copy of a coined id. They keep their
    // meaning and change their name, which is the whole point of the deletion.
    if (raw == kLegacyBlazeRod) {
        return ItemId::CinderRod;
    }
    if (raw == kLegacyBlazeRod + 1) {
        return ItemId::CinderPowder;
    }
    if (raw == kLegacyBlazeRod + 2) {
        return ItemId::FermentedSpiderEye;
    }
    if (raw == kLegacyBlazeRod + 3) {
        return ItemId::DrifterTear;
    }
    return static_cast<ItemId>(raw - kDuplicateRunItems);
}

/// Reads an id written by **any** older build and returns what it means now.
///
/// **Not idempotent, and it cannot be**: the second stage remaps a range that is
/// perfectly valid in the current numbering, so this is only ever safe on bytes
/// a version check has already declared old. That is the loader's contract, and
/// `WorldStore` keeps it by calling this only for a file whose version says so.
///
/// **The two stages are separately callable, and that is the fix.** An earlier
/// version of this comment claimed the first stage was bounded so the two
/// composed, and that a file needing only the second could be handed the whole
/// function. That was wrong and would have corrupted every chest: there are 580
/// appended blocks, so most block-item ids sit squarely inside `[256, 4096)`
/// and the first stage lifts them into the tool run. The upper bound catches
/// nothing there, because block items are *below* it.
///
/// So a caller that needs one stage now asks for that stage by name.
/// `upgradeDuplicateRuns` is safe on any post-boundary file with no guard at
/// all, which makes this function's only correct use the oldest files.
///
/// **That safety has a reason and a proof, and both live where they can be
/// checked.** The reason: both deleted runs sat above the block/item boundary,
/// so a stored block id takes `upgradeDuplicateRuns`'s first branch and comes
/// straight back out unchanged. The proof: `WorldStore.cpp` asserts that the
/// runs are above the boundary, and its `ItemEra::BeforeDuplicateRuns` rung
/// calls `upgradeDuplicateRuns(stored)` unguarded on the strength of it.
///
/// **This paragraph has said three different things in one session, so here is
/// the settled one.** It first called that rung's boundary test redundant -
/// right about the design, wrong about the call site, which at the time
/// composed both stages behind it. It was then corrected to call the test
/// load-bearing, which was true for exactly as long as the composition was.
/// The rung now asks for one stage by name and the test is gone, so both
/// readings are history. **No guard is missing and none should be added back:**
/// the edit that breaks this is the opposite one, writing `upgradeLegacyItemId`
/// into that rung again, and the assert beside it fails on precisely that.
constexpr ItemId upgradeLegacyItemId(ItemId stored) {
    return upgradeDuplicateRuns(upgradeItemBoundary(stored));
}

/// The oldest era's own number for the blaze rod - the middle era's number minus
/// the shift that put it there - so the composition can be asserted end to end
/// rather than one stage at a time.
constexpr int kOldestBlazeRod = kLegacyBlazeRod - kItemIdShift;

// --- Stage two, on its own. Every claim below is about the deleted runs, and
// none of them may be asked of the composed function: it would run the boundary
// shift over ids that are already in the current numbering.

static_assert(upgradeDuplicateRuns(static_cast<ItemId>(kLegacyBlazeRod)) == ItemId::CinderRod &&
                  upgradeDuplicateRuns(static_cast<ItemId>(kLegacyBlazeRod + 1)) ==
                      ItemId::CinderPowder &&
                  upgradeDuplicateRuns(static_cast<ItemId>(kLegacyBlazeRod + 3)) ==
                      ItemId::DrifterTear,
              "a saved blaze rod, blaze powder or ghast tear has to come back as the coined id "
              "that always meant the same thing");
static_assert(upgradeDuplicateRuns(static_cast<ItemId>(kLegacyBlazeRod + 2)) ==
                      ItemId::FermentedSpiderEye &&
                  upgradeDuplicateRuns(static_cast<ItemId>(kLegacyBlazeRod + 4)) ==
                      ItemId::DragonBreath,
              "the two reagents that survived must land on themselves, not on their neighbour");
static_assert(upgradeDuplicateRuns(static_cast<ItemId>(kLegacyShadowDiscFirst)) ==
                      ItemId::MusicDiscFirst &&
                  upgradeDuplicateRuns(static_cast<ItemId>(
                      kLegacyShadowDiscFirst + kLegacyShadowDiscs)) == ItemId::Saddle,
              "the first shadow disc becomes the first real one, and the id straight after the "
              "shadow run is the saddle it always was");

/// **The whole of finding #120 in one line.** Both ends of the block-item range
/// a post-boundary file can hold, put through the stage that file needs.
///
/// Fails on: writing `upgradeLegacyItemId` in place of `upgradeDuplicateRuns`
/// here - which is precisely what this function was for its first twelve hours,
/// and what a caller reaching for the obvious name still gets. Both ids move by
/// `kItemIdShift`, every stored block becomes a tool, and the file loads
/// cleanly.
static_assert(upgradeDuplicateRuns(static_cast<ItemId>(kLegacyFirstToolItem)) ==
                      static_cast<ItemId>(kLegacyFirstToolItem) &&
                  upgradeDuplicateRuns(static_cast<ItemId>(
                      static_cast<int>(ItemId::kFirstToolItem) - 1)) ==
                      static_cast<ItemId>(static_cast<int>(ItemId::kFirstToolItem) - 1),
              "stage two must leave every block item exactly where it is, at both ends of the "
              "range - a chest of stone that comes back a chest of tools is what asking the "
              "composed function for this costs");

// --- Stage one, and the composition.

static_assert(upgradeItemBoundary(static_cast<ItemId>(kLegacyFirstToolItem)) ==
                      ItemId::kFirstToolItem &&
                  upgradeItemBoundary(static_cast<ItemId>(kLegacyFirstToolItem - 1)) ==
                      static_cast<ItemId>(kLegacyFirstToolItem - 1),
              "the 256 boundary still shifts, and the last eight-bit block id still does not");
static_assert(upgradeLegacyItemId(static_cast<ItemId>(kOldestBlazeRod)) == ItemId::CinderRod &&
                  upgradeLegacyItemId(static_cast<ItemId>(kLegacyFirstToolItem - 1)) ==
                      static_cast<ItemId>(kLegacyFirstToolItem - 1),
              "the oldest files need both stages, in that order - swapping them, or dropping "
              "either, loses the rod; and an eight-bit block id passes through both untouched");

/// The raw foods, as one contiguous run of pairs.
constexpr int kFoodItems = 11;

/// True for anything that can be eaten, which is what makes the catalogue and
/// the smelting table agree on the set. **`survival::isEdible` answers the same
/// question from `foodValue`**, and the two are checked against each other by
/// the static assertions there.
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
    // The brewing round, all answered before the switch because each covers a
    // run of ids. **The ingredients have to be caught here rather than falling
    // into the appended-item arithmetic below**, because that run's layers are
    // followed by five more runs and growing it would slide every one of them.
    if (item >= ItemId::FermentedSpiderEye && item <= ItemId::DragonBreath) {
        return kBrewingSpritesFirst +
               (static_cast<int>(item) - static_cast<int>(ItemId::FermentedSpiderEye));
    }
    if (isDrinkablePotion(item)) {
        return kPotionSpritesFirst + potionIndex(item);
    }
    if (isSplashPotion(item)) {
        return kSplashPotionSpritesFirst + potionIndex(item);
    }
    if (isTippedArrow(item)) {
        return kTippedArrowSpritesFirst + potionIndex(item) - kFirstTippedPotion;
    }
    if (isLingeringPotion(item)) {
        return kLingeringPotionSpritesFirst + potionIndex(item);
    }
    // The collectibles. **All eight horns share one picture**, which is the
    // reference's arrangement too.
    if (isPotterySherd(item)) {
        return kSherdSpritesFirst +
               (static_cast<int>(item) - static_cast<int>(ItemId::PotterySherdFirst));
    }
    if (isGoatHorn(item)) {
        return kGoatHornSprite;
    }
    if (isMusicDisc(item)) {
        return kMusicDiscSpritesFirst + musicDiscIndex(item);
    }
    if (isFireworkStar(item)) {
        return kFireworkStarSpritesFirst +
               (static_cast<int>(item) - static_cast<int>(ItemId::FireworkStarFirst));
    }
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

/// The last appended item that still takes its picture from the extra-item run.
/// **Derived, never named** - it is defined as "the id below the brewing pair",
/// so appending an item below `GoldNugget` moves it here automatically and the
/// assert below then measures the new one.
constexpr ItemId kLastSpritedExtraItem =
    static_cast<ItemId>(static_cast<int>(ItemId::FermentedSpiderEye) - 1);

/// **The same two counts again, through the function that actually reads them.**
///
/// The pair beside `kExtraItems` is arithmetic on constants, and constants can
/// agree with each other while the reader adds a different base to them - the
/// shape that let eleven `static_assert`s pass while pointing at the wrong
/// texture. These evaluate the whole expression the renderer evaluates, base
/// included, and name the neighbouring run each one must stop short of. Append
/// an item and forget `kExtraItemSprites`, and the new last item resolves to
/// `kBeehiveSpritesFirst`: it would have drawn the beehive front, and instead
/// it fails the build.
static_assert(itemTextureLayer(ItemId::Bread) == kExtraItemSpritesFirst &&
                  itemTextureLayer(kLastSpritedExtraItem) == kBeehiveSpritesFirst - 1,
              "the appended items must fill their sprite run exactly and stop below the beehive");
static_assert(itemTextureLayer(ItemId::FermentedSpiderEye) == kBrewingSpritesFirst &&
                  itemTextureLayer(ItemId::DragonBreath) == kPotionSpritesFirst - 1,
              "the two brewing ids must fill their own run and stop below the potions");

constexpr const char* itemName(ItemId item) {
    if (isExtraItem(item)) {
        return extraItemInfo(item).name;
    }
    if (isPotterySherd(item)) {
        return kPotterySherdNames[static_cast<std::size_t>(
            static_cast<int>(item) - static_cast<int>(ItemId::PotterySherdFirst))];
    }
    if (isGoatHorn(item)) {
        return kGoatHornNames[static_cast<std::size_t>(static_cast<int>(item) -
                                                      static_cast<int>(ItemId::GoatHornFirst))];
    }
    if (isMusicDisc(item)) {
        return kMusicDiscNames[static_cast<std::size_t>(musicDiscIndex(item))];
    }
    if (isFireworkStar(item)) {
        return kFireworkStarNames[static_cast<std::size_t>(
            static_cast<int>(item) - static_cast<int>(ItemId::FireworkStarFirst))];
    }
    // All three potion runs, each from its own list. Answered before the switch
    // because each covers a run of ids rather than one.
    if (isDrinkablePotion(item)) {
        return kPotionNames[static_cast<std::size_t>(potionIndex(item))];
    }
    if (isSplashPotion(item)) {
        return kSplashPotionNames[static_cast<std::size_t>(potionIndex(item))];
    }
    if (isTippedArrow(item)) {
        return kTippedArrowNames[static_cast<std::size_t>(potionIndex(item) - kFirstTippedPotion)];
    }
    if (isLingeringPotion(item)) {
        return kLingeringPotionNames[static_cast<std::size_t>(potionIndex(item))];
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
/// Search tab is not a category - it shows everything - so it is a tab the UI
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

/// Which slot a piece of armour is worn in. The order is the run's own, so a
/// piece's slot is `(item - LeatherHelmet) % 4` and needs no table.
enum class ArmourSlot : std::uint8_t { Head = 0, Chest = 1, Legs = 2, Feet = 3, None = 4 };

/// Every wearable piece. One contiguous run plus the turtle shell, which
/// belongs to no set and so cannot be inside it.
constexpr bool isArmour(ItemId item) {
    return (item >= ItemId::LeatherHelmet && item <= ItemId::EmberiteBoots) ||
           item == ItemId::TurtleHelmet;
}

constexpr ArmourSlot armourSlot(ItemId item) {
    if (item == ItemId::TurtleHelmet) {
        return ArmourSlot::Head;
    }
    if (!isArmour(item)) {
        return ArmourSlot::None;
    }
    const int offset = static_cast<int>(item) - static_cast<int>(ItemId::LeatherHelmet);
    return static_cast<ArmourSlot>(offset % 4);
}

/// Which material row a piece belongs to, 0-5 rising leather to Emberite.
constexpr int armourMaterial(ItemId item) {
    if (item == ItemId::TurtleHelmet) {
        return 2; // as tough as iron, which is what the reference gives it
    }
    return (static_cast<int>(item) - static_cast<int>(ItemId::LeatherHelmet)) / 4;
}

/// **This is live: `armourDefence`, `armourToughness` and `armourSlot` all have
/// real callers outside this header as of 2026-08-19.** The comment that stood
/// here until then said the opposite - "nothing reads this ... a repo-wide grep
/// finds no caller of any of the four" - and it warned a reader not to believe
/// a diamond chestplate was doing anything. It is worth knowing why that was
/// the dangerous kind of wrong rather than a stale detail: it argued for
/// treating a load-bearing table as inert, and `CLAUDE.md` bug shape #16 ranks
/// a comment that could make someone delete live code first of all. **Negative
/// claims rot fastest**, which is why this one is dated and says below what
/// would make it false again.
///
/// Measured rather than read - a bare-name search over all 97 comment-stripped
/// `.cpp`/`.hpp` files under `game/src`:
///   - `armourDefence`   - `Inventory.hpp` 3, `Survival.hpp` 4
///   - `armourToughness` - `Inventory.hpp` 1, `Survival.hpp` 4
///   - `armourSlot`      - `Inventory.hpp` 8, `Main.cpp` 1
///   - `armourMaterial`  - **0 outside this header**, and that is still true.
///     It is not dead: `armourDefence` and `armourToughness` both call it two
///     lines down, so it is reached on every one of the counts above. A caller
///     sweep that only counts direct call sites reports it as unreachable, and
///     it is not - the value travels through a function rather than a name.
/// Controls, because a scan that returns nothing for everything has proved
/// nothing: `isArmour` came back with callers in two other files, and a
/// fabricated name came back with zero.
///
/// Defence points, **Bedrock's own table**, indexed material-major then slot.
/// Bedrock stopped being a flat 4% per point in 1.18.30 and now matches Java
/// exactly, toughness included - so these are the shared values, not a
/// Bedrock-only set.
///
/// **The three-step wiring plan this comment used to carry is two-thirds done**
/// (`GAPS.md` G2.4; `RESEARCH.md` section 3.4 carries the reduction formula):
///   1. **DONE.** Somewhere to wear it - `Inventory` has four armour slots
///      (`m_armour`), and the `player.dat` bump the old comment called "the
///      real cost" has happened: `SavedPlayer::armour` is format version 7,
///      with a documented migration that leaves the run empty for older worlds.
///   2. **DONE.** A consumer in `damagePlayer` (`world/Player.cpp`), sitting
///      exactly where the old comment said it had to - `survival::armourDamageTaken`
///      beside `effects::damageTakenScale` and after `chargeDamageWindow`,
///      because the invulnerability window compares the *raw* blow.
///   3. **CLOSED, and NOT by wiring the call** (2026-08-19, 17:10). This step
///      used to read "still open - `Creature.cpp` has `chargeDamageWindow` and
///      no call to `armourDamageTaken`". That framing was wrong, and acting on
///      it would have been a real defect: `Creature.cpp` now carries a dated
///      ruling that **there is no armour on a creature to read**. No creature
///      file names `ArmourSet` at all - every `armour*` hit in `Creature.hpp` is
///      prose about the villager *armourer* profession. So there is no defence
///      value to pass, and adding a defence column to feed the call would create
///      a second owner for a number nothing produces.
///      `damageCreature` is the single site that would take the call **if mob
///      equipment ever exists**; until then the absence is structural, not a gap.
/// **Falsified by**: a creature gaining wearable equipment - i.e. a creature
/// file naming `ArmourSet`, or `CreatureSpecies` growing an armour/defence
/// field. Deliberately phrased as *calls*, not *names*: a bare grep for
/// `armourDamageTaken` already matches this paragraph and the ruling that
/// replaced it, so the token alone proves nothing either way.
constexpr int armourDefence(ItemId item) {
    if (item == ItemId::TurtleHelmet) {
        return 2;
    }
    if (!isArmour(item)) {
        return 0;
    }
    constexpr int kPoints[6][4] = {
        {1, 3, 2, 1}, // leather
        {2, 5, 4, 1}, // chainmail
        {2, 6, 5, 2}, // iron
        {2, 5, 3, 1}, // gold
        {3, 8, 6, 3}, // diamond
        {3, 8, 6, 3}, // Emberite
    };
    return kPoints[armourMaterial(item)][static_cast<int>(armourSlot(item))];
}

/// Armour toughness, which is what makes diamond and Emberite hold up against
/// a *big* hit rather than merely a frequent one.
///
/// **Called, as of 2026-08-19** - `Survival.hpp` four times and `Inventory.hpp`
/// once; see `armourDefence` above for the measurement and its controls. This
/// line said "Also uncalled" until then, and it was the second copy of the same
/// rotted negative claim, which is the point: one wrong sentence about reach
/// gets pasted beside every function it plausibly describes.
constexpr float armourToughness(ItemId item) {
    if (!isArmour(item) || item == ItemId::TurtleHelmet) {
        return 0.0f;
    }
    const int material = armourMaterial(item);
    return material == 4 ? 2.0f : (material == 5 ? 3.0f : 0.0f);
}

/// **Tools never stack.** Two with different wear are not interchangeable, and
/// merging them would silently pick one damage value for both.
///
/// A full bucket does not stack either, and an empty one stacks only to
/// sixteen - both the reference's, and the full one matters: a stack of water
/// buckets that emptied one at a time would need a count on each.
///
/// > **CROSS-FILE: lowering any cap here can fail a `static_assert` in
/// > `world/Loot.hpp`, and that file's owner cannot see this edit coming.**
/// > `entriesSane()` there ends its message with *"more of one item than a
/// > single slot holds"*, and that clause is this function - every loot entry's
/// > count range is checked against `maxStackFor` of the item it names. So a
/// > cap lowered here fails the build in a file that did not change, with an
/// > error naming a loot table rather than an item cap.
/// >
/// > **Raising a cap is always safe; lowering one is not**, and the direction
/// > is the whole warning. The exposure is any item that appears in a loot
/// > table with a `max` above the new value.
/// >
/// > Recorded 2026-08-19 11:20. **Falsified by** `entriesSane()` losing its
/// > `maxStackFor` comparison, or by loot entries gaining their own cap - at
/// > which point delete this paragraph rather than leaving a stale coupling
/// > note, which is its own bug shape.
constexpr int maxStackFor(ItemId item) {
    if (isTool(item) || item == ItemId::WaterBucket) {
        return 1;
    }
    // The block items that do not stack to sixty-four. **A block item's cap
    // comes from nowhere else** - the table below only covers the appended run,
    // so a block that needs one has to be answered here or it silently stacks
    // to a full sixty-four.
    if (isBlockItem(item)) {
        const BlockId block = blockForItem(item);
        // A stowbox never stacks, and for exactly the tool's reason: its
        // `damage` names which contents it is carrying, and merging two would
        // pick one of them and lose the other outright.
        if (isStowbox(block)) {
            return 1;
        }
        // https://minecraft.wiki/w/Bed - "Stackable: No". A bed is two blocks
        // wide and carries a colour; the reference has never stacked one.
        if (isBed(block)) {
            return 1;
        }
        // https://minecraft.wiki/w/Sign and /w/Banner - both "Yes (16)", and a
        // hanging sign is a sign. Sixteen, not sixty-four: every one of these
        // will carry text or a pattern the moment it is placed.
        if (isSignLike(block)) {
            return 16;
        }
    }
    if (item == ItemId::Bucket) {
        return 16;
    }
    // A potion is a bottle, and the reference stacks neither the drinkable form
    // nor the thrown one. **A tipped arrow does stack**, because it is an arrow.
    if (isPotion(item)) {
        return 1;
    }
    // A horn and a disc are single things rather than a supply of them, and the
    // reference stacks neither. A sherd is a shard of pottery and stacks.
    if (isGoatHorn(item) || isMusicDisc(item)) {
        return 1;
    }
    if (isExtraItem(item)) {
        return extraItemInfo(item).stack;
    }
    return kMaxStack;
}

/// An item and how many of it. A count of zero means the slot is empty, and the
/// item is then meaningless.
///
/// > **There is deliberately no `space()` here.** It used to be one, and it was
/// > a damage-blind twin of `slots::roomFor`: it restated the cap as
/// > `kMaxStack` for an empty slot rather than asking the table that owns it -
/// > which it could not do, having no item to ask, and that is precisely why
/// > `roomFor` takes the *arriving* item - and it answered "how much room" for
/// > an incoming stack without ever looking at `damage`. Every caller survived
/// > on a `item == item` guard beside it rather than on the function. Ask
/// > `slots::roomFor(stack, incoming)` instead; it is the one owner of the
/// > question.
struct ItemStack {
    ItemId item = ItemId::None;
    int count = 0;
    /// Wear on a tool. Meaningless for everything else, and the reason tools do
    /// not stack: two with different wear are not interchangeable.
    int damage = 0;

    constexpr bool empty() const { return count <= 0 || item == ItemId::None; }
};

static_assert(maxStackFor(itemForBlock(BlockId::Stowbox)) == 1 &&
                  maxStackFor(ItemId::VoidEye) == 64 && maxStackFor(ItemId::Compass) == 64 &&
                  maxStackFor(ItemId::WrittenBook) == 16,
              "the stack caps the wiki states, asked through the one function that owns them");
static_assert(maxStackFor(itemForBlock(BlockId::BedRunFirst)) == 1 &&
                  maxStackFor(itemForBlock(BlockId::SignRunFirst)) == 16 &&
                  maxStackFor(itemForBlock(BlockId::HangingSignRunFirst)) == 16 &&
                  maxStackFor(itemForBlock(BlockId::BannerRunFirst)) == 16,
              "a block item's cap is this function's job too - deleting the isBed or isSignLike "
              "branch drops all four back to the sixty-four they silently were");

/// **The top of the block enum must fall all the way through to the default,
/// and `kLastBlock` is written rather than a name so this follows the enum.**
///
/// Every branch above is a bounded range, and this is what keeps them bounded.
/// `Block.hpp` now parks new families in **tail runs appended after the last
/// enumerator** rather than inside their own runs, because a run that grows
/// renumbers every id above it and those ids are already in saved chunks - the
/// forty waxed cut copper stair and slab ids landed exactly that way on
/// 2026-08-19. A tail id is therefore numerically **higher than every special
/// case here**, so a single range predicate written `id >= XRunFirst` with no
/// upper bound would sweep in the whole tail and cap the newest blocks in the
/// game at 1 or 16.
///
/// Checked and correct today: `isStowbox`, `isBed`, `isSign`, `isHangingSign`
/// and `isBanner` all close both ends, so a waxed cut copper slab stacks to 64
/// as the reference has it. This line is what says so tomorrow.
static_assert(maxStackFor(itemForBlock(kLastBlock)) == kMaxStack,
              "the last block id no longer stacks to sixty-four - either a range predicate in "
              "Block.hpp lost its upper bound and now swallows the tail runs, or a genuinely "
              "unstackable family was appended at the end and wants its own branch above");

/// Bring a stack down to its item's cap, and **say how much that destroyed**.
///
/// Returns the number of items thrown away, zero when the stack was already
/// legal. The count comes back so a caller can clamp and log in one line
/// without working the loss out itself, which would put `maxStackFor`'s
/// knowledge in a second place.
///
/// **This exists because the caps changed under worlds that already existed.**
/// Beds, signs, hanging signs and banners stacked to 64 here until 2026-08-18
/// and now stack to 1, 16, 16 and 16, which is what the wiki has always said.
/// `WorldStore::sanitiseStack` is where an old save meets the new cap, and
/// until this it shortened those stacks without a word.
///
/// **Destroying rather than spilling is deliberate.** The overflow has nowhere
/// to go: `sanitiseStack` is handed one `ItemStack&` and does not know which
/// container it came out of, and it runs for dropped-item entities as much as
/// for chests, so "move the rest to a free slot" is not something every caller
/// could do. A container that does have slots can still be full, so a spill
/// would need this path underneath it anyway. One mechanism, announced, beats
/// two where the second falls through to the first - and this project holds
/// nothing in a save precious, only losses it cannot explain.
constexpr int clampToStackLimit(ItemStack& stack) {
    if (stack.empty()) {
        return 0;
    }
    const int cap = maxStackFor(stack.item);
    if (stack.count <= cap) {
        return 0;
    }
    const int lost = stack.count - cap;
    stack.count = cap;
    return lost;
}

/// Fails on: assigning `stack.count = cap` before working out `lost` (which
/// reports nothing destroyed, every time, and hands the log a zero); and on
/// rebuilding the stack as `stack = {stack.item, cap}` instead of shortening
/// it, which passes the first two cases and empties every stowbox in the third.
constexpr bool stackClampReportsWhatItDestroyed() {
    ItemStack signs{itemForBlock(BlockId::SignRunFirst), 64};
    if (clampToStackLimit(signs) != 48 || signs.count != 16) {
        return false;
    }

    // A legal stack is untouched and reports nothing, so a non-zero return is
    // on its own the answer to "did this bite?".
    ItemStack bread{ItemId::Bread, 64};
    if (clampToStackLimit(bread) != 0 || bread.count != 64) {
        return false;
    }

    // **`damage` survives the clamp**, because a stowbox keeps which contents
    // it holds there. A clamp that rebuilt the stack rather than shortening it
    // would empty one on the very next load.
    ItemStack box{itemForBlock(BlockId::Stowbox), 4, 7};
    if (clampToStackLimit(box) != 3 || box.count != 1 || box.damage != 7) {
        return false;
    }

    return true;
}

static_assert(stackClampReportsWhatItDestroyed(),
              "clamping a stack to its cap must report the loss and keep the damage");

/// What a block turns into when broken.
///
/// Usually itself, but not always: stone yields cobblestone, and grass yields
/// plain dirt. Stairs and slabs collapse to their upright form so an inventory
/// does not fill with eight orientations of the same thing.
///
/// > **This answers the *identity* only, and it is no longer the whole story.**
/// > `dropsForBlock` in `item/BlockDrops.hpp` is what a break should ask: it
/// > adds counts that are ranges, several items from one block, chances rolled
/// > off the block's position, and the tool rows a tier gate cannot express.
/// > **It asks this function for everything it does not override**, which is
/// > what keeps the 640 cut shapes and the 119 candles answered in one place -
/// > so a new family rule belongs *here*, and only a rule this shape cannot
/// > carry belongs there.
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
    // ---- Redstone. ----
    // Every one of these spends most of its ids on a state, so each hands back
    // the one form that belongs in a catalogue - which is also what keeps seven
    // hundred new ids from arriving there as seven hundred entries.
    if (isRedstoneWire(block)) {
        return ItemId::Redstone;
    }
    if (isRedstoneTorch(block)) {
        return itemForBlock(BlockId::RedstoneTorch);
    }
    if (isLever(block)) {
        return itemForBlock(leverAt(LeverFloorX, false));
    }
    if (isRepeater(block)) {
        return itemForBlock(repeaterAt(FaceDirection::NegZ, 1, false, false));
    }
    if (isComparator(block)) {
        return itemForBlock(comparatorAt(FaceDirection::NegZ, false, false));
    }
    // **A piston head drops nothing at all.** It is a technical block, created
    // by an extending piston and never by a player - "It is not available in
    // the Creative inventory and does not drop anything when removed"
    // (minecraft.wiki, *Piston/Technical components*). It used to hand back a
    // whole piston, which is a free piston per extension the moment anything in
    // this game learns to extend one. The base is a separate block and pays for
    // itself below.
    if (isPistonHead(block)) {
        return ItemId::None;
    }
    if (isPiston(block)) {
        return itemForBlock(pistonAt(Facing6North, false, pistonSticky(block)));
    }
    if (isObserver(block)) {
        return itemForBlock(observerAt(Facing6North, false));
    }
    if (isDispenserLike(block)) {
        return itemForBlock(dispenserAt(Facing6North, isDropper(block)));
    }
    if (isDaylightDetector(block)) {
        return itemForBlock(daylightDetectorAt(0, false));
    }
    if (isLightningRod(block)) {
        return itemForBlock(lightningRodAt(Facing6Up, false));
    }
    if (isTripwireHook(block)) {
        return itemForBlock(tripwireHookAt(FaceDirection::NegZ, false, false));
    }
    // The string is what was strung up, and it is what comes back.
    if (isTripwire(block)) {
        return ItemId::String;
    }
    if (isRail(block)) {
        return itemForBlock(railAt(railFamily(block), 0, false));
    }
    if (isTarget(block)) {
        return itemForBlock(BlockId::Target);
    }
    if (isNoteBlock(block)) {
        return itemForBlock(BlockId::NoteBlock);
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
    // ---- The farm. ----
    // Tilled ground and a trodden path are dirt that has been worked, and both
    // come back as dirt - which is also what stops them being catalogue entries
    // twice over.
    if (isFarmland(block) || block == BlockId::DirtPath) {
        return itemForBlock(BlockId::Dirt);
    }
    // A crop hands back its seed until it is ripe, and its food once it is.
    // Age is the whole of the rule, so there is nothing else to keep in step.
    if (isCropBlock(block)) {
        const BlockId family = cropFamily(block);
        const bool ripe = cropAge(block) >= 7;
        if (family == BlockId::WheatCrop0) {
            return ripe ? ItemId::Wheat : ItemId::WheatSeeds;
        }
        if (family == BlockId::CarrotCrop0) {
            return ItemId::Carrot;
        }
        if (family == BlockId::PotatoCrop0) {
            return ItemId::Potato;
        }
        return ripe ? ItemId::Beetroot : ItemId::BeetrootSeeds;
    }
    // A stem is worth its own seeds whatever age it reached.
    if (isStemBlock(block)) {
        return stemGrowsMelon(block) ? ItemId::MelonSeeds : ItemId::PumpkinSeeds;
    }
    if (isNetherWart(block)) {
        return ItemId::NetherWart;
    }
    // Every facing hands back the one that faces north, the way a furnace does.
    if (isCarvedPumpkin(block)) {
        return itemForBlock(BlockId::CarvedPumpkinFirst);
    }
    if (isJackOLantern(block)) {
        return itemForBlock(BlockId::JackOLanternFirst);
    }
    // How full a composter is does not survive being broken - the reference
    // destroys the contents - so every level hands back the empty tub.
    if (isComposter(block)) {
        return itemForBlock(BlockId::Composter0);
    }
    // ---- Paired blocks in the fifth run. ----
    // A lit lamp, a lit bulb and the upper half of a two-block flower are all
    // *states* rather than items. Each hands back the one form that belongs in
    // a catalogue, which is also what stops it appearing there twice.
    if (block == BlockId::RedstoneLampLit) {
        return itemForBlock(BlockId::RedstoneLamp);
    }
    if (isCopperBulb(block) && isCopperBulbLit(block)) {
        return itemForBlock(static_cast<BlockId>(static_cast<int>(block) - 1));
    }
    if (isTallFlowerUpper(block)) {
        return itemForBlock(static_cast<BlockId>(static_cast<int>(block) - 1));
    }
    // Cave vines give up their berries and stay where they are, like a bush.
    if (block == BlockId::CaveVinesBerries) {
        return ItemId::GlowBerries;
    }
    // A stack of candles hands back its colour's candle; how many there were is
    // the count, not a different item.
    if (isCandle(block)) {
        return itemForBlock(
            static_cast<BlockId>(static_cast<int>(BlockId::Candle) + candleColour(block)));
    }
    // Both halves of a door give back one door, and which way it faces, which
    // way it is hinged and whether it is open are all placement states.
    if (isDoor(block)) {
        return itemForBlock(doorCanonical(doorFamily(block)));
    }
    if (isTrapdoor(block)) {
        return itemForBlock(trapdoorCanonical(trapdoorFamily(block)));
    }
    // Both halves of a bed give back one bed of that colour.
    if (isBed(block)) {
        return itemForBlock(bedCanonical(bedColour(block)));
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
    // and a smoker is its own block rather than a furnace with a hat. **Both
    // other cookers are asked before `isFurnace`**, which answers for all three.
    if (isSmoker(block)) {
        return itemForBlock(BlockId::Smoker);
    }
    if (isBlastFurnace(block)) {
        return itemForBlock(BlockId::BlastFurnace);
    }
    if (isFurnace(block)) {
        return itemForBlock(BlockId::Furnace);
    }
    // Which way a chest opens is a placement state, not four different chests.
    // **Each family hands back its own north-facing form**, which is what
    // widening `isChest` to cover the barrel and the trapped chest would
    // otherwise have broken - both would have dropped a plain chest.
    if (isTrappedChest(block)) {
        return itemForBlock(BlockId::TrappedChest);
    }
    if (isEnderChest(block)) {
        return itemForBlock(BlockId::EnderChest);
    }
    // A stowbox keeps its own colour, and there is nothing else in its id.
    if (isStowbox(block)) {
        return itemForBlock(block);
    }
    // How full a cauldron is does not survive being broken.
    if (isCauldron(block)) {
        return itemForBlock(BlockId::Cauldron);
    }
    // Nor does which way a hopper's spout pointed.
    if (isHopper(block)) {
        return itemForBlock(BlockId::Hopper);
    }
    if (block == BlockId::Barrel) {
        return itemForBlock(BlockId::Barrel);
    }
    if (isChest(block)) {
        return itemForBlock(BlockId::Chest);
    }
    // A nest is the FOUND half of the family and is not the hive's case: the
    // reference drops nothing at all for a nest broken without Silk Touch, and
    // Silk Touch is dormant here, so nothing is the whole answer. This must be
    // asked FIRST, because isBeehive is true of a nest - the predicate was
    // widened to cover the nest run because thirteen of its sixteen callers
    // want the same answer for both, and this is the one caller where the
    // wider answer is wrong. Returning None also keeps a nest out of the
    // creative catalogue, since isCanonicalBlockItem asks whether this equals
    // itemForBlock and None never does.
    if (isBeeNest(block)) {
        return ItemId::None;
    }
    // A hive is one block however it is turned, and however full it is.
    //
    // **Mining a full hive LOSES the honey**, and that is the trade rather than
    // an oversight: this function hands back an `ItemId` and nothing else, so
    // there is nowhere for a level to ride along, and the canonical `Beehive`
    // is level zero by construction - the run stores facing as `offset % 4` and
    // honey as `offset >= 4`, so the head of it is empty and north-facing. Break
    // a hive at level five and put it down again and it is empty. The way to be
    // paid for the honey is to shear it first: `Main.cpp`'s shear site drops
    // three honeycomb and resets the home through `beeHomeAtLevel(hive, 0)`.
    //
    // This used to say "until that lands" of the bee work. **It landed on
    // 2026-08-19**, and the same edit made the clause beside it wrong as well -
    // it claimed a moved hive "simply keeps its honey", which is the one thing
    // it cannot do. Falsifier for what replaced it: an `ItemStack` field that
    // survives a drop, at which point a level could ride in `damage` the way a
    // stowbox's contents already do.
    //
    // The reference drops nothing for a crafted hive either; that divergence
    // is deliberate, because a hive you crafted and placed would otherwise be
    // unrecoverable while Silk Touch is dormant, which is the worse trade.
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
    // **The two Nether ores were missing from this list outright**, so both fell
    // through to `default` and dropped themselves - an ore block that no recipe
    // can consume and that nothing else in the game can produce. Nether gold ore
    // pays 2-6 gold nuggets and nether quartz ore one quartz (minecraft.wiki,
    // *Nether Gold Ore* and *Nether Quartz Ore*, Bedrock drops). There is no
    // Silk Touch here (`BlockDrops.hpp`), so neither can ever be the block.
    case BlockId::NetherGoldOre:
        return ItemId::GoldNugget;
    case BlockId::NetherQuartzOre:
        return ItemId::Quartz;
    default:
        return itemForBlock(block);
    }
}

/// How many a block yields. One unless the reference says otherwise.
///
/// The reference rolls a range - copper 2-5, redstone 4-5, lapis 4-9 - and we
/// take the middle of each rather than adding randomness a generator has no
/// need of. Mining the same vein twice should give the same haul.
///
/// > **The midpoints are superseded.** `dropsForBlock` in `BlockDrops.hpp`
/// > carries the real ranges and keeps the "same cell, same haul" property by
/// > hashing the block's position rather than by flattening the range. This
/// > function survives for the call sites that have not migrated yet, and for
/// > the state-derived counts it owns outright - a candle stack and a cocoa pod,
/// > which are not ranges at all.
constexpr int dropCountForBlock(BlockId block) {
    // The deepslate half of an ore yields exactly what the stone half does -
    // and that has to be said *here* as well as in `dropForBlock`, or the two
    // disagree. It did: deepslate lapis paid one lapis where stone lapis paid
    // six, because only the identity was being collapsed.
    if (isDeepslateOre(block)) {
        block = stoneOreFor(block);
    }
    // A ripe pod is worth three beans; anything younger, one.
    if (isCocoa(block)) {
        return cocoaAge(block) == 2 ? 3 : 1;
    }
    // A stack of candles gives back every candle in it.
    if (isCandle(block)) {
        return candleCount(block);
    }
    // A ripe crop is worth a harvest; an unripe one gives back only what was
    // sown. Bedrock's own counts, taking the middle of each range for the same
    // reason the ores do - breaking the same field twice should pay the same.
    if (isCropBlock(block)) {
        if (cropAge(block) < 7) {
            return 1;
        }
        const BlockId family = cropFamily(block);
        // Carrots and potatoes roll 2-5; wheat gives one ear; beetroot 1-2.
        // Three is a flat stand-in inside the carrot/potato range rather than
        // its middle - `dropsForBlock` in `BlockDrops.hpp` rolls the real one,
        // and every live break already asks that rather than this.
        return (family == BlockId::CarrotCrop0 || family == BlockId::PotatoCrop0) ? 3 : 1;
    }
    switch (block) {
    case BlockId::CopperOre:
        return 3;
    case BlockId::RedstoneOre:
        return 4;
    case BlockId::LapisOre:
        return 6;
    // Two to six nuggets (minecraft.wiki, *Nether Gold Ore*), middled like
    // every other ore range here. Nether quartz is one and needs no row.
    case BlockId::NetherGoldOre:
        return 4;
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
/// which state is the canonical one - so this asks it rather than listing them
/// again. A second stair or slab material therefore needs no change here.
///
/// **Anything that spends block ids on orientation or contents must be named
/// here**, or the catalogue shows one row per id: the beehive's eight arrived
/// as eight separate entries until it was added.
///
/// **`isPistonHead` used to sit at the end of that disjunction and could never
/// be the term that decided.** `isRedstoneComponent` already contains every
/// piston head - measured over all 3285 ids, 12 of 12 already true, against a
/// control on the note block that came back 25 ids and 0 already true, so the
/// instrument can tell a real hand-patch from a redundant one and says the
/// note block is real. Deleting it is a provably zero-behaviour change; it is
/// worth doing because it sat immediately beside `isNoteBlock`, which *is* a
/// deliberate hand-patch, so a reader reasonably concluded that
/// `isRedstoneComponent` must exclude piston heads. Acting on that reading
/// means widening a family predicate that already contains the member -
/// `CLAUDE.md` bug shape #2 approached from the wrong end. The assert below is
/// what makes the deletion safe rather than merely tidy: narrow
/// `isRedstoneComponent` away from piston heads and the build stops, instead of
/// twelve catalogue rows quietly appearing.
constexpr bool isCanonicalBlockItem(BlockId block) {
    // Neither fluid, fire nor a lit charge is something you can hold. Lava is
    // reachable through its bucket and fire through flint and steel.
    if (block == BlockId::Air || isFluid(block) || block == BlockId::Fire ||
        block == BlockId::TntPrimed) {
        return false;
    }
    if (isShapedBlock(block) || isFurnace(block) || isChest(block) || isBeehive(block) ||
        isLadder(block) || isVine(block) || isCocoa(block) || isSnowLayer(block) ||
        isCarvedPumpkin(block) || isJackOLantern(block) || isComposter(block) ||
        isTallFlowerUpper(block) || isCopperBulb(block) || block == BlockId::RedstoneLampLit ||
        block == BlockId::CaveVinesBerries || isCandle(block) || isDoor(block) ||
        isTrapdoor(block) || isBed(block) || isCauldron(block) || isHopper(block) ||
        isRedstoneComponent(block) || isNoteBlock(block)) {
        return dropForBlock(block) == itemForBlock(block);
    }
    // A crop, a stem and tilled ground are all things the world grows or you
    // make in place - none of them is an item you carry, and each already hands
    // back the seed, the food or the dirt it came from.
    if (isCropBlock(block) || isStemBlock(block) || isNetherWart(block) || isFarmland(block) ||
        block == BlockId::DirtPath) {
        return false;
    }
    return true;
}

static_assert(isCanonicalBlockItem(BlockId::Tnt) && !isCanonicalBlockItem(BlockId::TntPrimed),
              "a lit charge is a state of TNT, not a second item");
/// **The deleted disjunct's replacement, and it is strictly stronger than the
/// line it stands in for.** Both ends of the piston head run must already be
/// `isRedstoneComponent`, which is what made `|| isPistonHead(block)` above
/// unable to decide anything. The note block clause is the control: it is *not*
/// a redstone component, so this cannot be an assert that says yes to whatever
/// it is handed - and it is also the reason `isNoteBlock` stays in the
/// disjunction while `isPistonHead` does not.
static_assert(isRedstoneComponent(BlockId::PistonHeadRunFirst) &&
                  isRedstoneComponent(BlockId::PistonHeadRunLast) &&
                  !isRedstoneComponent(BlockId::NoteBlock),
              "isRedstoneComponent must keep covering every piston head - isCanonicalBlockItem "
              "stopped naming them separately once this was true, so narrowing it here puts "
              "twelve orientation states back in the creative catalogue as items of their own");
static_assert(!isCanonicalBlockItem(BlockId::Lava0) && !isCanonicalBlockItem(BlockId::Fire),
              "neither lava nor fire is a holdable item");
static_assert(isCanonicalBlockItem(BlockId::SnowLayerFirst) &&
                  !isCanonicalBlockItem(BlockId::SnowLayerLast),
              "how deep snow lies is a state, not seven separate items");

static_assert(dropForBlock(BlockId::BlastFurnaceExtraFirst) ==
                  itemForBlock(BlockId::BlastFurnace),
              "every facing of a blast furnace hands back the one that faces north");
// Both halves of an ore's answer have to collapse deepslate, or they disagree
// about the same block - which they did, silently, for every deepslate vein.
static_assert(dropForBlock(BlockId::DeepslateLapisOre) == dropForBlock(BlockId::LapisOre) &&
                  dropCountForBlock(BlockId::DeepslateLapisOre) ==
                      dropCountForBlock(BlockId::LapisOre) &&
                  dropCountForBlock(BlockId::DeepslateCopperOre) ==
                      dropCountForBlock(BlockId::CopperOre),
              "the deepslate half of an ore must yield what the stone half does, count and all");
static_assert(!isCanonicalBlockItem(BlockId::BlastFurnaceExtraFirst) &&
                  !isCanonicalBlockItem(BlockId::BlastFurnaceExtraLast),
              "a blast furnace's facing and its fire are placement states, not eight items");
static_assert(!isCanonicalBlockItem(BlockId::CandleExtraFirst) &&
                  !isCanonicalBlockItem(BlockId::CandleExtraLast),
              "how many candles stand in a cell is a state, not a hundred and nineteen items");

static_assert(isCanonicalBlockItem(BlockId::Beehive),              "the plain hive is the form that belongs in the catalogue");
static_assert(!isCanonicalBlockItem(BlockId::BeehiveEast) &&
                  !isCanonicalBlockItem(BlockId::BeehiveHoney) &&
                  !isCanonicalBlockItem(BlockId::BeehiveHoneyWest),
              "a hive's facing and its honey are placement states, not eight separate items");

// A stowbox carries its contents in the stack's `damage`, so two of them
// merging would silently pick one and lose the other. **Both halves of that
// rule are asserted here**, because either one alone is a real item-loss bug.
static_assert(maxStackFor(itemForBlock(BlockId::Stowbox)) == 1 &&
                  maxStackFor(itemForBlock(BlockId::StowboxDyedLast)) == 1,
              "a stowbox must never stack; its damage names which contents it holds");
static_assert(isCanonicalBlockItem(BlockId::Stowbox) &&
                  isCanonicalBlockItem(BlockId::StowboxDyedFirst) &&
                  isCanonicalBlockItem(BlockId::StowboxDyedLast),
              "every stowbox colour is its own item, and drops itself");

/// Which tab an item sits under.
///
/// **This is ours, not measured** - the reference's own assignment lives in data
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
    // Everything else you wear or wield. Armour is a run; the rest are named,
    // because they share no range.
    if (isArmour(item) || item == ItemId::Shield || item == ItemId::Trident ||
        item == ItemId::Crossbow || item == ItemId::FishingRod || item == ItemId::Elytra) {
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
    // Redstone lands in Items, which is the tab Bedrock keeps it under - and it
    // is asked **before** the cut-shape test, because a button and a pressure
    // plate are cut shapes and would otherwise be filed under Construction with
    // the stairs while the lever beside them sat somewhere else.
    if (isRedstoneComponent(block)) {
        return ItemCategory::Items;
    }
    if (isShapedBlock(block) || isFurnace(block) || isChest(block)) {
        return ItemCategory::Construction;
    }
    if (isLadder(block) || block == BlockId::IronBars) {
        return ItemCategory::Construction;
    }
    // Doors and trapdoors sit outside every table run, so without this they
    // fall through to the switch's default and land under Items.
    if (isDoor(block) || isTrapdoor(block)) {
        return ItemCategory::Construction;
    }
    if (isBed(block)) {
        return ItemCategory::Items;
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
