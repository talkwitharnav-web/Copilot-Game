#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace game {

/// How many materials each cut shape comes in.
///
/// **These have to be declared before the enum**, because the runs of ids they
/// size are inside it, while the tables that name the materials cannot be
/// written until `BlockId` exists.
///
/// **Nothing `static_assert`s one of these tables against its constant, and
/// nothing can.** Each is declared `std::array<ShapedFamily, kXFamilyCount>`, so
/// the declared size *is* the constant and any such assert compares an expression
/// with itself. A **short** initialiser therefore compiles silently and
/// zero-fills the tail with `BlockId::Air`. `everyFamilyTableIsFullyWritten`,
/// further down, is the guard that actually holds: it asserts the **last** row of
/// every one of these tables is written, that being the one row a short
/// initialiser always loses.
///
/// **Stairs and slabs each split in three.** `kStairRunFamilyCount` is **frozen**:
/// `StairsRunLast` is computed from it, and every enumerator above that is already
/// written into saved chunks, so raising it renumbers them. `kStairTailFamilyCount`
/// counts the families parked in the tail run at the very end of the enum.
/// `kStairFamilyCount` is the total, and is what the table and every family loop
/// use. **Add a material by raising the tail count, never the frozen one.**
constexpr int kStairRunFamilyCount = 52;
constexpr int kStairTailFamilyCount = 4;
constexpr int kStairFamilyCount = kStairRunFamilyCount + kStairTailFamilyCount;
constexpr int kSlabRunFamilyCount = 55;
constexpr int kSlabTailFamilyCount = 4;
constexpr int kSlabFamilyCount = kSlabRunFamilyCount + kSlabTailFamilyCount;
constexpr int kWallFamilyCount = 25;
constexpr int kFenceFamilyCount = 12;
constexpr int kGateFamilyCount = 11;
constexpr int kCarpetFamilyCount = 16;
constexpr int kPaneFamilyCount = 17;
/// Eleven woods plus iron. **Thirty-two ids each** - four facings times a hinge
/// side times open times which half - because the reference stores all four and
/// the hinge in particular genuinely cannot be derived: the same neighbourhood
/// can carry either hand, depending on what was there when it was placed.
constexpr int kDoorFamilyCount = 12;
/// The same woods and iron, at **sixteen ids each**: four facings times open
/// times top-or-bottom half. A trapdoor has no hinge to choose.
constexpr int kTrapdoorFamilyCount = 12;
/// Sixteen dyed beds. **Eight ids each** - four facings times head-or-foot -
/// because a bed is two blocks that have to know which end they are.
constexpr int kBedColours = 16;

/// Eleven woods and stone. **Twelve ids each** - six faces to hang it on times
/// pressed - because the reference lets a button go on a floor or a ceiling as
/// well as a wall, and a floor button is how half of all doorbells are built.
constexpr int kButtonFamilyCount = 12;
/// The same twelve, plus the two weighted plates that count what stands on them.
constexpr int kPressurePlateFamilyCount = 14;

/// One sign per wood, and one hanging sign per wood. **Eight ids each**: four
/// facings standing on the ground, then four for the wall-mounted form.
///
/// > **Named divergence: the reference gives a standing sign sixteen
/// > rotations.** Ours has four, because the box mesher is axis-aligned and a
/// > board turned 22.5 degrees is not something it can express - the same
/// > limitation that leaves the lever's handle sliding instead of tilting.
constexpr int kSignFamilyCount = 11;
/// Sixteen dyed banners, on the same eight states.
constexpr int kBannerFamilyCount = 16;
/// How many states a sign, a hanging sign or a banner spends on where it is and
/// which way it looks. The single owner of that eight, because three runs and
/// six functions all index by it.
constexpr int kSignStates = 8;

/// How many loot tables a *generated* chest can carry, and therefore how many
/// four-facing slices `LootChestRunFirst` reserves.
///
/// It lives here rather than in `Loot.hpp` because the block enum cannot see a
/// header that is built on top of it - and `Loot.hpp` `static_assert`s this
/// against `loot::TableId::Count`, so adding a table without widening the run
/// is a build failure rather than a chest that quietly rolls the wrong list.
///
/// **Re-verified in source on 2026-08-19, after this coupling was reported as
/// not existing.** That report swept `Loot.hpp` for the *type* `BlockId`, found
/// sixteen uses and none of them inside an assert, and read the result as
/// showing nothing was coupled. **The coupling does not run through a type. It
/// runs through this constant**, under its own name, so only a search for
/// `kLootChestTables` itself can find it. Falsified by `Loot.hpp` ceasing to
/// name `kLootChestTables` - run that search rather than trusting this
/// paragraph, and do not conclude from the absence of a *type* that no
/// constant crosses the seam.
constexpr int kLootChestTables = 11;

/// How many honey levels a hive holds, and how many of them needed new ids.
///
/// **Six states, `honey_level` `[0-5]`**, where this carried one bit until
/// 2026-08-19.
///
/// **A reader can check that here, in this tree**, and should: the resource
/// pack under `reference/minecraft-assets-26.2` states it twice over.
/// `blockstates/beehive.json` enumerates exactly 24 variants - four facings by
/// six levels - and `blockstates/bee_nest.json` enumerates the same 24. Both
/// send levels 0 to 4 to their `_empty` model and only level 5 to `_honey`,
/// which is separately why no new art was needed here.
///
/// That reading agrees with Mojang's `mojang-blocks.json`, where the property
/// has exactly two users, `beehive` and `bee_nest`. **That file is not vendored
/// here** - checked 2026-08-19, and a `mojang-blocks.json` appearing anywhere
/// under the repository root is what would make this sentence stale - but
/// nothing rests on it, because the two blockstate files above are primary and
/// are present. An earlier version of this comment said the number could not be
/// checked in the tree, which was wrong and is the kind of wrong that stops a
/// reader verifying something they easily could.
///
/// **A value list shared by many blocks is the storage domain and not one
/// block's range** - it tells you how wide the field is, and only a sole user
/// makes it authoritative for a particular block. Two users that are the same
/// thing in two guises is as close to sole as this gets, and it means the bee
/// nest needs the identical widening rather than a narrower one.
///
/// Levels 0 and 5 already had ids, so only the four in between are new.
constexpr int kBeehiveHoneyLevels = 6;
constexpr int kBeehiveFullHoney = kBeehiveHoneyLevels - 1;
constexpr int kBeehivePartialHoneyLevels = kBeehiveHoneyLevels - 2;

/// How wet farmland can be, and how many of those levels needed new ids.
///
/// **Eight states, `moisturized_amount` `[0-7]`**, where this carried two ids
/// until 2026-08-19.
///
/// **Sole user, so the published range is this block's range.** Mojang's
/// `mojang-blocks.json` lists `moisturized_amount` against `farmland` and
/// nothing else, which is what makes eight authoritative here rather than
/// merely the width of a shared field. That distinction is not pedantry: the
/// same file publishes `growth [0-7]` to thirteen blocks and `age [0-15]` to
/// eight, and reading either as one block's range is how a confident number
/// from a genuine primary source ends up one level too specific.
///
/// **The number that matters is not the drying time, it is when the picture
/// changes.** The reference draws the *wet* top on levels 1 to 7 and the dry
/// top only at 0, so a field that has lost water still looks watered for seven
/// of its eight steps and turns dry only on the last one. Two ids could not
/// express that at all: they made the dry texture appear halfway through, which
/// is the half a player actually sees and roughly seven times too early.
///
/// **The shape is copied, not invented.** It is exactly the beehive's above -
/// one id for level 0, a tail run for the levels in between, one id for the
/// full level - because farmland has the same problem the hive had: two
/// existing ids, already on disk, that must keep meaning what they meant.
/// `Farmland` stays level 0 and `FarmlandMoist` stays level 7, so every saved
/// world reads back correctly and no format bump is owed.
///
/// Levels 0 and 7 already had ids, so only the six in between are new.
constexpr int kFarmlandMoistureLevels = 8;
constexpr int kFarmlandFullMoisture = kFarmlandMoistureLevels - 1;
constexpr int kFarmlandPartialMoistureLevels = kFarmlandMoistureLevels - 2;

/// **Two bytes per block**, so a 32-cubed chunk is 64 KB of blocks.
///
/// It was one byte until 2026-08-07, and 253 of those 256 values were spoken
/// for. The ceiling was never really the number of block *types*: a third of
/// the run goes on *states* - eight ids for a stair's orientation, eight for a
/// furnace's facing and lit flag, eight for a water level - because an id is
/// the only storage a block has. The reference has about a thousand types and
/// twenty-eight thousand states, so a byte was never going to reach it.
///
/// The cost is real and was accepted: chunk memory for blocks doubles. What it
/// is **not** is a dead end - if that memory ever hurts, the fix is to pack the
/// storage behind `Chunk::at` (the reference keeps a short per-chunk palette and
/// stores a few bits per cell), and nothing outside `Chunk` would change.
enum class BlockId : std::uint16_t {
    Air = 0,
    Stone,
    Dirt,
    Grass,
    Sand,
    Cobblestone,
    Gravel,
    Snow,
    Planks,
    Bricks,
    Glowstone,
    Log,
    Leaves,
    /// Water carries its depth in the block id itself. Levels run 0 (a full
    /// source that never drains) to 7 (the thinnest film), and must stay
    /// contiguous and in order.
    Water0,
    Water1,
    Water2,
    Water3,
    Water4,
    Water5,
    Water6,
    Water7,
    /// Anything below here is not a full cube. Appended rather than inserted,
    /// because ids are what get written to disk.
    TallGrass,
    StoneSlab,
    /// Stairs spend eight contiguous ids on their orientation, the same way
    /// water spends eight on its level: two bits of facing plus one of half.
    /// Orientation is part of *which block this is*, so it belongs in the id
    /// rather than in a second per-block array that every chunk would carry.
    /// Must stay contiguous and in this order.
    CobbleStairs0,
    CobbleStairs1,
    CobbleStairs2,
    CobbleStairs3,
    CobbleStairs4,
    CobbleStairs5,
    CobbleStairs6,
    CobbleStairs7,
    /// The upper half of a cell. Two slabs meeting in one cell become a full
    /// block instead, so this is only ever a lone half.
    StoneSlabTop,
    PlanksFence,
    CraftingTable,
    /// Burning is not a property the furnace stores separately: it *is* a
    /// different block, the same way a water level is. That keeps the lit front
    /// texture and the light it gives off out of the block-entity data.
    Furnace,
    FurnaceLit,
    Torch,
    /// Everything below is appended, never inserted: ids are what get written to
    /// disk, so moving one silently rewrites every saved chunk.
    Andesite,
    Diorite,
    Granite,
    SmoothStone,
    StoneBricks,
    MossyCobblestone,
    Obsidian,
    Clay,
    Sandstone,
    Bookshelf,
    Glass,
    Dandelion,
    Poppy,
    DeadBush,
    CoalOre,
    IronOre,
    CopperOre,
    GoldOre,
    RedstoneOre,
    LapisOre,
    DiamondOre,
    EmeraldOre,
    Deepslate,
    Bedrock,
    Terracotta,
    PackedIce,
    /// A furnace stores which way its mouth points, the way stairs store their
    /// orientation. `Furnace` and `FurnaceLit` above are the north-facing pair
    /// and stay exactly where they are - ids are on disk, so the other three
    /// directions had to be appended rather than the run renumbered.
    FurnaceEast,
    FurnaceEastLit,
    FurnaceSouth,
    FurnaceSouthLit,
    FurnaceWest,
    FurnaceWestLit,
    /// Water with water directly above it. **It is full, and it spreads only
    /// downward** - which is the whole of why a waterfall is a column and not a
    /// widening cone. The reference encodes it as bit 0x8 of `liquid_depth`,
    /// where the level bits stop meaning anything; ours is one id for the same
    /// reason, since a falling cell is always at its highest level.
    WaterFalling,
    /// The ore the highest tier comes from, and the block that tier stacks into.
    ///
    /// **`Emberite` is our name for the reference's dark alloy.** *Nether* and
    /// *netherite* are coined and need ours; *ancient*, *debris*, *scrap* and
    /// *ingot* are ordinary English and stay. The reference hides its ore in
    /// another dimension, which we do not have, so ours is the deepest thing in
    /// the ground - which keeps what the material *means* while changing where
    /// it comes from.
    AncientDebris,
    EmberiteBlock,
    /// A furnace that cooks at twice the rate. **Eight ids in the same order as
    /// the furnace's** - four facings, unlit then lit - so the facing is
    /// arithmetic rather than a second switch.
    Smoker,
    SmokerLit,
    SmokerEast,
    SmokerEastLit,
    SmokerSouth,
    SmokerSouthLit,
    SmokerWest,
    SmokerWestLit,
    /// **No facing ids.** The reference keeps a smithing table's faces pointing
    /// the same way however it is placed, so which side you see is a fact about
    /// the block rather than a placement state - the same arrangement the
    /// crafting table uses.
    SmithingTable,
    /// Four facings and no lit state, so unlike a cooker the id carries only
    /// which way it opens. Its contents are a block entity, like a furnace's.
    Chest,
    ChestEast,
    ChestSouth,
    ChestWest,

    /// Appended at the end, like everything before them: a `BlockId` is written
    /// into save files, so inserting one anywhere earlier turns every block
    /// behind it into something else in a saved world.
    Prismarine,
    SeaLantern,
    CoarseDirt,

    /// **One ordered run, and the order is load-bearing.** `kExtraBlocks` below
    /// is indexed by the offset from `kFirstExtraBlock`, so every property of
    /// these - name, texture layer, whether it is a cross - comes from one table
    /// rather than from forty-eight cases in four different switches. Grouped by
    /// family so the range tests in `Mining.hpp` and `Explosion.hpp` can name a
    /// run's two ends instead of forty-eight ids.
    CobbledDeepslate,
    Ice,
    BlueIce,

    CoalBlock,
    IronBlock,
    GoldBlock,
    DiamondBlock,
    EmeraldBlock,
    LapisBlock,
    RedstoneBlock,
    CopperBlock,

    PolishedAndesite,
    PolishedDiorite,
    PolishedGranite,
    ChiseledStoneBricks,
    MossyStoneBricks,
    CrackedStoneBricks,
    PolishedDeepslate,
    DeepslateBricks,
    DeepslateTiles,

    SmoothSandstone,
    CutSandstone,
    ChiseledSandstone,

    TubeCoralBlock,
    BrainCoralBlock,
    BubbleCoralBlock,
    FireCoralBlock,
    HornCoralBlock,
    Sponge,
    WetSponge,
    DarkPrismarine,
    PrismarineBricks,

    SpruceLog,
    SpruceLeaves,
    SprucePlanks,
    BirchLog,
    BirchLeaves,
    BirchPlanks,

    /// Cross-shaped from here to the end of the run, which `blockShape` relies
    /// on rather than naming each one.
    Cornflower,
    OxeyeDaisy,
    AzureBluet,
    Allium,
    RedTulip,
    OrangeTulip,
    BrownMushroom,
    RedMushroom,
    Kelp,
    Seagrass,

    /// Everything from here on was appended on 2026-08-07. **The cross-shaped
    /// blocks above are no longer the tail of the run**, so the shape, cutout
    /// and catalogue rules that used to test `id >= kFirstCrossExtra` now read a
    /// flag on the row instead - which is what let solid blocks be appended
    /// after them without moving a single saved id.
    ///
    /// Colour families keep the reference's own order, white through black, so
    /// a dye's index is its wool's index and a recipe can be arithmetic.
    WhiteWool,
    OrangeWool,
    MagentaWool,
    LightBlueWool,
    YellowWool,
    LimeWool,
    PinkWool,
    GrayWool,
    LightGrayWool,
    CyanWool,
    PurpleWool,
    BlueWool,
    BrownWool,
    GreenWool,
    RedWool,
    BlackWool,

    WhiteConcrete,
    OrangeConcrete,
    MagentaConcrete,
    LightBlueConcrete,
    YellowConcrete,
    LimeConcrete,
    PinkConcrete,
    GrayConcrete,
    LightGrayConcrete,
    CyanConcrete,
    PurpleConcrete,
    BlueConcrete,
    BrownConcrete,
    GreenConcrete,
    RedConcrete,
    BlackConcrete,

    WhiteTerracotta,
    OrangeTerracotta,
    MagentaTerracotta,
    LightBlueTerracotta,
    YellowTerracotta,
    LimeTerracotta,
    PinkTerracotta,
    GrayTerracotta,
    LightGrayTerracotta,
    CyanTerracotta,
    PurpleTerracotta,
    BlueTerracotta,
    BrownTerracotta,
    GreenTerracotta,
    RedTerracotta,
    BlackTerracotta,

    /// The deepslate half of every ore. The generator swaps to these below the
    /// deepslate line, which is the reference's own arrangement and the reason
    /// deep mining looks different from shallow mining.
    DeepslateCoalOre,
    DeepslateIronOre,
    DeepslateCopperOre,
    DeepslateGoldOre,
    DeepslateRedstoneOre,
    DeepslateLapisOre,
    DeepslateDiamondOre,
    DeepslateEmeraldOre,

    JungleLog,
    JungleLeaves,
    JunglePlanks,
    AcaciaLog,
    AcaciaLeaves,
    AcaciaPlanks,
    DarkOakLog,
    DarkOakLeaves,
    DarkOakPlanks,
    CherryLog,
    CherryLeaves,
    CherryPlanks,

    StrippedOakLog,
    StrippedSpruceLog,
    StrippedBirchLog,
    StrippedJungleLog,
    StrippedAcaciaLog,
    StrippedDarkOakLog,

    Tuff,
    Calcite,
    DripstoneBlock,
    MossBlock,
    Mud,
    PackedMud,
    MudBricks,
    RootedDirt,
    AmethystBlock,
    SmoothBasalt,
    Basalt,
    MagmaBlock,
    HoneycombBlock,
    HoneyBlock,

    RedSandstone,
    CutRedSandstone,
    ChiseledRedSandstone,

    Pumpkin,
    Melon,
    HayBlock,
    NoteBlock,
    Jukebox,

    /// A second run of cross-shaped blocks. They sit at the end only because
    /// that is where they were appended; nothing depends on it any more.
    BlueOrchid,
    PinkTulip,
    WhiteTulip,
    LilyOfTheValley,
    OakSapling,
    SpruceSapling,
    BirchSapling,
    JungleSapling,
    AcaciaSapling,
    DarkOakSapling,
    Fern,
    SugarCane,
    Cobweb,

    /// **Outside the table-driven run**, because a hive has a front and the
    /// table has no column for one. Four facings with the honey empty, then the
    /// same four full - so the facing is `offset % 4` and the honey is
    /// `offset >= 4`, both arithmetic rather than a second switch.
    Beehive,
    BeehiveEast,
    BeehiveSouth,
    BeehiveWest,
    BeehiveHoney,
    BeehiveHoneyEast,
    BeehiveHoneySouth,
    BeehiveHoneyWest,

    /// Lava, encoded exactly the way water is - the level in the id, a separate
    /// falling form - so every piece of machinery that already understands a
    /// fluid asks one question instead of learning a second scheme.
    ///
    /// **Eight ids, only four of them reachable.** The reference's Overworld
    /// lava drops *two* levels per block rather than one, so 0, 2, 4 and 6 are
    /// what occur and the spread stops three blocks out. The odd levels are
    /// left in because the Nether uses a step of one; room for that costs
    /// nothing now and an id shift later.
    Lava0,
    Lava1,
    Lava2,
    Lava3,
    Lava4,
    Lava5,
    Lava6,
    Lava7,
    /// Lava with lava above it. Full, and spreads only downward.
    LavaFalling,

    /// Not a cube and not a plant. It has no collision at all, it burns
    /// whatever stands in it, and it dies without either fuel or a floor.
    Fire,

    /// Inert until lit.
    Tnt,
    /// Lit and counting down. **A separate id rather than a timer beside the
    /// block**, because an id is already saved, already meshes, and already
    /// tells the mesher to draw a different texture - a parallel table of
    /// burning positions would need all three building again.
    TntPrimed,

    /// A second table-driven run. It sits after the hive rather than beside the
    /// first run because **the first run is bounded by `kLastExtraBlock` and
    /// everything after it is already written to disk** - growing it in place
    /// would renumber every block above it and silently rewrite saved worlds.
    /// `isExtraBlock` spans both, exactly as `isCrossBlock` already spans two.
    Netherrack,
    SoulSand,
    SoulSoil,
    Blackstone,
    PolishedBlackstone,
    PolishedBlackstoneBricks,
    ChiseledPolishedBlackstone,
    CrackedPolishedBlackstoneBricks,
    GildedBlackstone,
    NetherBricks,
    RedNetherBricks,
    CrackedNetherBricks,
    ChiseledNetherBricks,
    NetherGoldOre,
    NetherQuartzOre,
    QuartzBlock,
    SmoothQuartz,
    ChiseledQuartz,
    QuartzBricks,
    EndStone,
    EndStoneBricks,
    PurpurBlock,
    Podzol,
    Mycelium,
    DriedKelpBlock,
    SlimeBlock,
    Sculk,
    BuddingAmethyst,
    PolishedTuff,
    TuffBricks,
    ChiseledTuff,
    PolishedBasalt,
    RawIronBlock,
    RawGoldBlock,
    RawCopperBlock,
    ExposedCopper,
    WeatheredCopper,
    OxidizedCopper,
    CutCopper,
    ExposedCutCopper,
    WeatheredCutCopper,
    OxidizedCutCopper,
    ChiseledCopper,
    ReinforcedDeepslate,
    ChiseledDeepslate,
    CrackedDeepslateBricks,
    CrackedDeepslateTiles,
    SmoothRedSandstone,
    NetherWartBlock,
    WhiteConcretePowder,
    OrangeConcretePowder,
    MagentaConcretePowder,
    LightBlueConcretePowder,
    YellowConcretePowder,
    LimeConcretePowder,
    PinkConcretePowder,
    GrayConcretePowder,
    LightGrayConcretePowder,
    CyanConcretePowder,
    PurpleConcretePowder,
    BlueConcretePowder,
    BrownConcretePowder,
    GreenConcretePowder,
    RedConcretePowder,
    BlackConcretePowder,

    /// The second run continues here. Its cross-shaped plants are kept
    /// contiguous and first so `isCrossBlock` stays three ranges rather than
    /// gaining seventeen names.
    Bamboo,
    SweetBerryBush,
    GlowLichen,
    PointedDripstone,
    SeaPickle,
    NetherSprouts,
    CrimsonRoots,
    WarpedRoots,
    CrimsonFungus,
    WarpedFungus,
    TwistingVines,
    WeepingVines,
    HangingRoots,
    SporeBlossom,
    AmethystCluster,
    LargeFern,
    LilyPad,

    /// Full cubes from here.
    Cactus,
    WhiteGlazedTerracotta,
    OrangeGlazedTerracotta,
    MagentaGlazedTerracotta,
    LightBlueGlazedTerracotta,
    YellowGlazedTerracotta,
    LimeGlazedTerracotta,
    PinkGlazedTerracotta,
    GrayGlazedTerracotta,
    LightGrayGlazedTerracotta,
    CyanGlazedTerracotta,
    PurpleGlazedTerracotta,
    BlueGlazedTerracotta,
    BrownGlazedTerracotta,
    GreenGlazedTerracotta,
    RedGlazedTerracotta,
    BlackGlazedTerracotta,
    Shroomlight,
    OchreFroglight,
    VerdantFroglight,
    PearlescentFroglight,
    CrimsonNylium,
    WarpedNylium,
    CrimsonStem,
    WarpedStem,
    CrimsonPlanks,
    WarpedPlanks,
    WarpedWartBlock,
    MangroveLog,
    MangrovePlanks,
    MangroveLeaves,
    MuddyMangroveRoots,
    BambooBlock,
    BambooPlanks,
    BambooMosaic,
    BoneBlock,
    QuartzPillar,
    PurpurPillar,
    Target,
    SnowBlock,
    SculkCatalyst,
    Azalea,
    FloweringAzalea,

    /// The five logs an axe used to do nothing to. Appended rather than filed
    /// beside the other stripped logs, because a table row carries its sprite
    /// index by hand and inserting one slides every index after it.
    StrippedCherryLog,
    StrippedMangroveLog,
    StrippedCrimsonStem,
    StrippedWarpedStem,
    StrippedBambooBlock,

    /// **Five runs of cut shapes, one per material, and not one of them needs a
    /// texture of its own** - a stair is drawn with the block it was cut from,
    /// so `shapedParent` answers the texture, the tool, the hardness and the
    /// blast resistance all at once and the tables below carry only a parent
    /// and a name.
    ///
    /// The runs are sized by the family counts above and their members are
    /// **reached by arithmetic, never by name**. Naming six hundred and forty
    /// enumerators would be six hundred and forty chances to put one in the
    /// wrong order, and the order is the whole mapping.
    ///
    /// Cobblestone stairs, the stone slab and the oak fence already existed and
    /// are already written into saved worlds, so each is **family 0 of its own
    /// shape** and the new run holds the families after it. That is the one
    /// branch in `stairsAt` and friends, and it is why the run lengths below
    /// subtract one family.
    ///
    /// **Stairs and slabs subtract from the *frozen* count, not the total.** The
    /// waxed copper families live in the tail runs at the end of this enum and so
    /// add nothing here. These two lines used to read `kStairFamilyCount` and
    /// `kSlabFamilyCount`; left that way, every family added anywhere would have
    /// lengthened this run and slid every enumerator below it, renumbering ids
    /// already written into saved chunks.
    StairsRunFirst,
    StairsRunLast = StairsRunFirst + (kStairRunFamilyCount - 1) * 8 - 1,
    SlabRunFirst,
    SlabRunLast = SlabRunFirst + (kSlabRunFamilyCount - 1) * 2 - 1,
    WallRunFirst,
    WallRunLast = WallRunFirst + kWallFamilyCount - 1,
    FenceRunFirst,
    FenceRunLast = FenceRunFirst + (kFenceFamilyCount - 1) - 1,
    /// Four facings, closed then open, so both halves of a gate's state are one
    /// offset - the same arrangement the furnace and the hive use.
    GateRunFirst,
    GateRunLast = GateRunFirst + kGateFamilyCount * 8 - 1,

    /// A **third** table-driven run, at the very end for the reason the second
    /// one exists at all: everything before it is already written to disk, and
    /// growing a run in place renumbers every block above it.
    WhiteStainedGlass,
    OrangeStainedGlass,
    MagentaStainedGlass,
    LightBlueStainedGlass,
    YellowStainedGlass,
    LimeStainedGlass,
    PinkStainedGlass,
    GrayStainedGlass,
    LightGrayStainedGlass,
    CyanStainedGlass,
    PurpleStainedGlass,
    BlueStainedGlass,
    BrownStainedGlass,
    GreenStainedGlass,
    RedStainedGlass,
    BlackStainedGlass,
    IronBars,
    Lantern,
    SoulLantern,
    SoulTorch,
    RedstoneTorch,
    EndRod,

    /// A ladder is the first block you climb rather than stand on, and it keeps
    /// which wall it is fixed to in its id - four of them, in `FaceDirection`'s
    /// own compass order.
    LadderNorth,
    LadderEast,
    LadderSouth,
    LadderWest,

    /// Two more cut families, on exactly the same arrangement as the stairs:
    /// a carpet is a wool, a pane is a glass, and neither needs a texture.
    CarpetRunFirst,
    CarpetRunLast = CarpetRunFirst + kCarpetFamilyCount - 1,
    PaneRunFirst,
    PaneRunLast = PaneRunFirst + kPaneFamilyCount - 1,

    /// Sixteen vines: **one id per combination of the four sides it clings to**,
    /// which is Bedrock's own `vine_direction_bits` and its own count. Java
    /// spends a fifth bit on the ceiling piece; Bedrock derives that from
    /// whether anything is above, and so do we - a stored bit would be a second
    /// copy of a fact the world already holds.
    ///
    /// The bits are `ConnectionBits`, so a vine's sides read the same way a
    /// fence's arms do.
    VineFirst,
    VineLast = VineFirst + 15,

    /// Cocoa on the side of a jungle log: **four facings times three ages**, so
    /// the facing is the offset within a triple and the age is which triple.
    CocoaFirst,
    CocoaLast = CocoaFirst + 11,

    /// Snow as it settles: **seven depths, each two texels deeper than the**
    /// **last**, with the eighth being the full `Snow` cube that already
    /// existed. Depth is the offset, so it is arithmetic rather than a table,
    /// and every one of them samples the snow texture that was already loaded -
    /// so this family costs no texture, no layer constant and no staging row.
    SnowLayerFirst,
    SnowLayerLast = SnowLayerFirst + 6,

    /// **A fourth table-driven run**, and it exists for the same reason the
    /// second and third do: everything before it is already written into saved
    /// worlds, so widening a run in place renumbers every block above it.
    ///
    /// This one is the farm. Crops keep their age *in the id* exactly the way
    /// water keeps its level and stairs keep their orientation - a growth stage
    /// is which block this is, not a number beside it, so it costs no
    /// per-chunk array and it saves and meshes for free.
    ///
    /// Farmland carries all eight of the reference's moisture levels, and these
    /// two are its ends: `Farmland` is 0 and `FarmlandMoist` is 7. The six in
    /// between are `FarmlandMoistureRunFirst..Last` at the tail of this enum,
    /// appended there so that neither of these two numbers moves and every
    /// saved world keeps reading correctly. Ask `farmlandMoisture`, never the
    /// id: **only level 0 draws the dry top**, so "is it `FarmlandMoist`" is
    /// the wrong question and is silently wrong for the six new levels.
    ///
    /// An earlier comment here said farmland was "two ids rather than a
    /// moisture counter" because "a cell that is one of two things needs no
    /// second table". The reasoning was sound; the premise was not, and the
    /// cost was a dry texture appearing about seven times too early.
    Farmland,
    FarmlandMoist,
    DirtPath,

    /// Eight ages each, so `age = id - WheatCrop0`. Carrots and potatoes have
    /// eight ages but only four pictures - the reference maps 0-1, 2-3, 4-6, 7
    /// - and that mapping lives in `cropTextureStage`, not in the id.
    WheatCrop0,
    WheatCropLast = WheatCrop0 + 7,
    CarrotCrop0,
    CarrotCropLast = CarrotCrop0 + 7,
    PotatoCrop0,
    PotatoCropLast = PotatoCrop0 + 7,
    /// **Bedrock stores beetroot as `growth` 0-7 like every other crop**, not
    /// Java's `age` 0-3. Four pictures over eight ages, same as the carrot.
    BeetrootCrop0,
    BeetrootCropLast = BeetrootCrop0 + 7,

    /// A stem grows through eight ages and then *attaches* to whichever side it
    /// put its fruit on. The attached form is four more ids rather than a flag,
    /// so the direction is arithmetic and the mesher needs no extra state.
    MelonStem0,
    MelonStemLast = MelonStem0 + 7,
    MelonStemAttachedFirst,
    MelonStemAttachedLast = MelonStemAttachedFirst + 3,
    PumpkinStem0,
    PumpkinStemLast = PumpkinStem0 + 7,
    PumpkinStemAttachedFirst,
    PumpkinStemAttachedLast = PumpkinStemAttachedFirst + 3,

    /// Four ages, three pictures - the reference draws age 2 and 3 the same.
    NetherWart0,
    NetherWartLast = NetherWart0 + 3,

    /// Carved and lit, four facings each, in `FaceDirection`'s compass order
    /// like the furnace. Only the first of each four is a catalogue entry,
    /// because the other three drop it rather than themselves.
    CarvedPumpkinFirst,
    CarvedPumpkinLast = CarvedPumpkinFirst + 3,
    JackOLanternFirst,
    JackOLanternLast = JackOLanternFirst + 3,

    /// Nine fill levels. Level 8 is the reference's separate "ready" state -
    /// not merely full - and it is what you take the bone meal out of.
    Composter0,
    ComposterLast = Composter0 + 8,

    /// **A fifth table-driven run**, appended for the reason all the others
    /// were: everything before it is already on disk.
    ///
    /// Almost all of it is a plain cube wanting a name and a picture, which is
    /// exactly what the table is for - so a hundred and sixteen blocks arrive
    /// here without touching a single switch.
    ///
    /// Bark blocks first: a log wears its end grain on two faces, a **wood**
    /// block wears bark on all six. They need no new art at all, because they
    /// point at the log side their own family already staged.
    OakWood,
    SpruceWood,
    BirchWood,
    JungleWood,
    AcaciaWood,
    DarkOakWood,
    CherryWood,
    MangroveWood,
    CrimsonHyphae,
    WarpedHyphae,
    StrippedOakWood,
    StrippedSpruceWood,
    StrippedBirchWood,
    StrippedJungleWood,
    StrippedAcaciaWood,
    StrippedDarkOakWood,
    StrippedCherryWood,
    StrippedMangroveWood,
    StrippedCrimsonHyphae,
    StrippedWarpedHyphae,

    BrownMushroomBlock,
    RedMushroomBlock,
    MushroomStem,

    DeadTubeCoralBlock,
    DeadBrainCoralBlock,
    DeadBubbleCoralBlock,
    DeadFireCoralBlock,
    DeadHornCoralBlock,

    /// Waxed copper. **The same pictures as the unwaxed run** - wax is not
    /// visible, it is a promise that the block will not change - so these cost
    /// nine ids and no art whatsoever.
    WaxedCopperBlock,
    WaxedExposedCopper,
    WaxedWeatheredCopper,
    WaxedOxidizedCopper,
    WaxedCutCopper,
    WaxedExposedCutCopper,
    WaxedWeatheredCutCopper,
    WaxedOxidizedCutCopper,
    WaxedChiseledCopper,

    CopperGrate,
    ExposedCopperGrate,
    WeatheredCopperGrate,
    OxidizedCopperGrate,
    WaxedCopperGrate,
    WaxedExposedCopperGrate,
    WaxedWeatheredCopperGrate,
    WaxedOxidizedCopperGrate,

    /// Four oxidation stages, unlit then lit. **The light falls with the
    /// oxidation** - 15, 12, 8, 4 - which is not a formula and so is a table.
    CopperBulb,
    CopperBulbLit,
    ExposedCopperBulb,
    ExposedCopperBulbLit,
    WeatheredCopperBulb,
    WeatheredCopperBulbLit,
    OxidizedCopperBulb,
    OxidizedCopperBulbLit,

    CryingObsidian,
    PowderSnow,
    SuspiciousSand,
    SuspiciousGravel,
    AzaleaLeaves,
    FloweringAzaleaLeaves,
    RedstoneLamp,
    RedstoneLampLit,
    Lodestone,
    EnchantingTable,
    ChiseledBookshelf,
    CartographyTable,
    FletchingTable,
    Barrel,
    BlastFurnace,
    Loom,
    Stonecutter,
    Grindstone,
    Lectern,
    Bell,
    Cauldron,
    BrewingStand,
    Anvil,
    ChippedAnvil,
    DamagedAnvil,
    Scaffolding,
    FlowerPot,

    SculkVein,
    SculkSensor,
    SculkShrieker,
    SmallAmethystBud,
    MediumAmethystBud,
    LargeAmethystBud,
    BigDripleaf,
    SmallDripleaf,
    CaveVines,
    CaveVinesBerries,
    MossCarpet,
    ChorusPlant,
    ChorusFlower,

    /// The five corals as plants, alive and dead, then the same ten as fans.
    /// Kept in two contiguous fives so the dead form of a coral is one offset
    /// from the live one.
    TubeCoral,
    BrainCoral,
    BubbleCoral,
    FireCoral,
    HornCoral,
    DeadTubeCoral,
    DeadBrainCoral,
    DeadBubbleCoral,
    DeadFireCoral,
    DeadHornCoral,
    TubeCoralFan,
    BrainCoralFan,
    BubbleCoralFan,
    FireCoralFan,
    HornCoralFan,
    DeadTubeCoralFan,
    DeadBrainCoralFan,
    DeadBubbleCoralFan,
    DeadFireCoralFan,
    DeadHornCoralFan,

    /// The two-block flowers. **Bottom then top for each**, so a half is one
    /// offset and the pair can be placed and broken together.
    SunflowerLower,
    SunflowerUpper,
    LilacLower,
    LilacUpper,
    RoseBushLower,
    RoseBushUpper,
    PeonyLower,
    PeonyUpper,
    WitherRose,

    Campfire,
    SoulCampfire,
    RespawnAnchor,

    /// **A sixth run**, and the last of this batch. Seventeen candles - plain
    /// and the sixteen dyes, declared white-first like every other colour
    /// family so a colour is one offset here too - and a few blocks that had no
    /// home in any earlier group.
    ///
    /// **Named divergence: a candle is one candle and is always lit.** The
    /// reference stacks up to four in a cell and tracks whether they are burning,
    /// which is four counts times two states times seventeen colours; that is a
    /// hundred and thirty-six ids for a decoration, and it can be widened later
    /// without moving anything because this run is at the end.
    Candle,
    WhiteCandle,
    OrangeCandle,
    MagentaCandle,
    LightBlueCandle,
    YellowCandle,
    LimeCandle,
    PinkCandle,
    GrayCandle,
    LightGrayCandle,
    CyanCandle,
    PurpleCandle,
    BlueCandle,
    BrownCandle,
    GreenCandle,
    RedCandle,
    BlackCandle,

    TintedGlass,
    Beacon,
    Conduit,
    DragonEgg,
    EndPortalFrame,
    MonsterSpawner,

    /// Four facings, like the chest it is a twin of. **A trapped chest pairs
    /// only with another trapped chest** - the pairing test is already "the
    /// neighbour is the same id", so that falls out for free.
    TrappedChest,
    TrappedChestEast,
    TrappedChestSouth,
    TrappedChestWest,

    /// The blast furnace's other seven states. **The north-facing unlit one is
    /// `BlastFurnace` above and stays exactly where it is** - it is already in
    /// saved worlds and it is the catalogue entry - so the rest are appended
    /// here and reached through one branch, which is the same arrangement the
    /// legacy stair and slab families use.
    ///
    /// Order is east, south, west, then the same four lit.
    BlastFurnaceExtraFirst,
    BlastFurnaceExtraLast = BlastFurnaceExtraFirst + 6,

    /// The candles' other hundred and nineteen states.
    ///
    /// **Outside every table run on purpose.** A row per id would be a hundred
    /// and nineteen lines carrying nothing but a name and a layer that are both
    /// already derivable from the colour - so these are answered by predicate
    /// instead, exactly as the beehive, ladder, vine and cocoa families are.
    ///
    /// Seven states per colour, because the eighth - one candle, lit - is the
    /// `Candle..BlackCandle` run above and is already on disk. Within a colour:
    /// lit twos, threes and fours, then unlit ones, twos, threes and fours.
    CandleExtraFirst,
    CandleExtraLast = CandleExtraFirst + 17 * 7 - 1,

    /// Doors. **Thirty-two states each**, packed so that every one of the four
    /// facts about a door is a bit or two of the offset:
    /// `half<<4 | open<<3 | hinge<<2 | facing`.
    ///
    /// Reached by arithmetic, never by name - three hundred and eighty-four
    /// enumerators would be as many chances to put one in the wrong order, and
    /// the order is the whole mapping.
    DoorRunFirst,
    DoorRunLast = DoorRunFirst + kDoorFamilyCount * 32 - 1,

    /// Trapdoors, on the same arrangement one bit narrower:
    /// `top<<3 | open<<2 | facing`.
    TrapdoorRunFirst,
    TrapdoorRunLast = TrapdoorRunFirst + kTrapdoorFamilyCount * 16 - 1,

    /// Beds, `head<<2 | facing`. The facing is the direction the **head** lies
    /// from the foot, so one value orients both blocks.
    BedRunFirst,
    BedRunLast = BedRunFirst + kBedColours * 8 - 1,

    /// Four facings, like the chest. Its twenty-seven slots are **the player's,
    /// not the block's** - every ender chest in the world is a window onto the
    /// same ones.
    EnderChest,
    EnderChestEast,
    EnderChestSouth,
    EnderChestWest,

    /// The cauldron's other six fill levels. The empty one is `Cauldron` in run
    /// five and stays there; these are levels one to six, and **six is full**,
    /// which is the reference's own range rather than a third of it.
    CauldronExtraFirst,
    CauldronExtraLast = CauldronExtraFirst + 5,

    /// The hopper, and which way its spout points. Five states rather than six:
    /// a spout may point down or at any of the four sides, but **never up** -
    /// the reference has no such state and nor does the placement rule that
    /// derives one from the face you clicked.
    Hopper,
    HopperNorth,
    HopperSouth,
    HopperEast,
    HopperWest,

    /// Our name for the shulker box, since *Shulker* is Mojang's coinage. A
    /// container that **keeps what is inside it when broken**: the contents
    /// travel on the item, not with the place it stood.
    ///
    /// Plain first, then the sixteen dyes in the usual order, so the colour is
    /// one subtraction here as it is for every other dyed family.
    Stowbox,
    StowboxDyedFirst,
    StowboxDyedLast = StowboxDyedFirst + 15,

    /// **Redstone.** Everything from here down carries or answers a signal.
    ///
    /// **422 of the 679 ids in these runs are unreachable in play**, because
    /// there is no signal engine and that is a settled decision - the block
    /// comment above `isRedstoneWire` counts them family by family and says
    /// what would make them live. The states are packed into the ids on purpose
    /// so that engine can be added later without moving one of them.
    ///
    /// Wire keeps its strength in the id the way water keeps its level, which
    /// is what the reference does too: Bedrock gives `redstone_wire` a single
    /// `redstone_signal` state of 0-15 and **derives the shape from the
    /// neighbours at draw time**, where Java stores a direction per side and
    /// pays 1296 states for it. Sixteen ids and a shape function is the whole
    /// of it.
    RedstoneWireFirst,
    RedstoneWireLast = RedstoneWireFirst + 15,

    /// The torch off, and on or off against any of the four walls. The lit
    /// floor torch stays `RedstoneTorch` up in run three because it is already
    /// on disk - the project's own "a legacy id is family zero" rule.
    RedstoneTorchOff,
    RedstoneTorchWallFirst,
    RedstoneTorchWallLast = RedstoneTorchWallFirst + 3,
    RedstoneTorchOffWallFirst,
    RedstoneTorchOffWallLast = RedstoneTorchOffWallFirst + 3,

    /// `on<<3 | attachment`, where the attachment is floor-across, floor-along,
    /// ceiling-across, ceiling-along, then the four walls. Eight positions is
    /// the reference's own count: a lever on the floor still has to say which
    /// way the handle throws, which a wall lever does not.
    LeverRunFirst,
    LeverRunLast = LeverRunFirst + 15,

    /// Twelve materials at twelve states each - `pressed<<3 | attachment`, over
    /// floor, ceiling and the four walls.
    ButtonRunFirst,
    ButtonRunLast = ButtonRunFirst + kButtonFamilyCount * 12 - 1,

    /// Fourteen materials at sixteen states each. Wood and stone only ever use
    /// 0 and 15, but the two weighted plates genuinely count entities, so one
    /// uniform run of sixteen costs nothing and keeps the arithmetic single.
    PressurePlateRunFirst,
    PressurePlateRunLast = PressurePlateRunFirst + kPressurePlateFamilyCount * 16 - 1,

    /// `locked<<5 | powered<<4 | delay<<2 | facing`. The facing is the way the
    /// signal *leaves*, which is the reference's own convention read the useful
    /// way round.
    RepeaterRunFirst,
    RepeaterRunLast = RepeaterRunFirst + 63,

    /// `subtract<<3 | powered<<2 | facing`.
    ComparatorRunFirst,
    ComparatorRunLast = ComparatorRunFirst + 15,

    /// `sticky*12 + extended*6 + facing`, six facings including up and down.
    ///
    /// **Multiplication, not a bit-pack.** This said `sticky<<4 | extended<<3 |
    /// facing`, which needs 32 ids and would put a sticky piston at offset 16 -
    /// the run is 24 long and `pistonSticky` reads `>= 12`. Six is not a power
    /// of two, so anything with an up-and-down facing has to multiply; the
    /// shift notation is right only where the facing is horizontal, as it is
    /// for the repeater, comparator and tripwire hook above and below.
    PistonRunFirst,
    PistonRunLast = PistonRunFirst + 23,
    /// The head an extended piston pushes out in front of itself.
    PistonHeadRunFirst,
    PistonHeadRunLast = PistonHeadRunFirst + 11,

    /// `powered*6 + facing` - multiplication, for the same reason the piston is.
    /// The facing is the **watching** face; the pulse leaves from the opposite
    /// side.
    ObserverRunFirst,
    ObserverRunLast = ObserverRunFirst + 11,

    /// Six facings each. Which way they act, not which way they were placed.
    DispenserRunFirst,
    DispenserRunLast = DispenserRunFirst + 5,
    DropperRunFirst,
    DropperRunLast = DropperRunFirst + 5,

    /// `inverted<<4 | level`.
    DaylightDetectorRunFirst,
    DaylightDetectorRunLast = DaylightDetectorRunFirst + 31,

    /// The other fifteen strengths a struck target holds. Strength zero is
    /// `Target` in run five and stays there.
    TargetRunFirst,
    TargetRunLast = TargetRunFirst + 14,

    /// The other twenty-four pitches. Note zero is `NoteBlock` in run five.
    /// **The instrument is not stored** - it is read off the block underneath,
    /// exactly as the reference does.
    NoteBlockRunFirst,
    NoteBlockRunLast = NoteBlockRunFirst + 23,

    /// `powered*6 + facing`, six facings - multiplication, like the piston and
    /// the observer and unlike the four-facing runs.
    LightningRodRunFirst,
    LightningRodRunLast = LightningRodRunFirst + 11,

    /// `powered<<3 | attached<<2 | facing`.
    TripwireHookRunFirst,
    TripwireHookRunLast = TripwireHookRunFirst + 15,
    /// The string itself: `powered<<1 | attached`.
    TripwireRunFirst,
    TripwireRunLast = TripwireRunFirst + 3,

    /// Ten shapes for the plain rail - two flat, four sloped, four curved - and
    /// six shapes times powered for the three that cannot curve.
    RailRunFirst,
    RailRunLast = RailRunFirst + 9,
    PoweredRailRunFirst,
    PoweredRailRunLast = PoweredRailRunFirst + 11,
    DetectorRailRunFirst,
    DetectorRailRunLast = DetectorRailRunFirst + 11,
    ActivatorRailRunFirst,
    ActivatorRailRunLast = ActivatorRailRunFirst + 11,

    /// **Signs, hanging signs and banners.** Each spends `kSignStates` ids on
    /// where it is: **four** rotations standing on the ground, then four facings
    /// for the form that hangs off a wall - eight, not the twenty this comment
    /// claimed. The reference has sixteen standing rotations and ours has four,
    /// which is a named simplification and the reason `kSignStates` is a
    /// constant rather than a literal; the comment never followed it down when
    /// the count was cut, so anyone sizing an array off it read four times the
    /// truth.
    SignRunFirst,
    SignRunLast = SignRunFirst + kSignFamilyCount * kSignStates - 1,
    HangingSignRunFirst,
    HangingSignRunLast = HangingSignRunFirst + kSignFamilyCount * kSignStates - 1,
    BannerRunFirst,
    BannerRunLast = BannerRunFirst + kBannerFamilyCount * kSignStates - 1,

    /// **A chest nobody has opened yet, and which loot table it will roll.**
    /// `table * 4 + facing`, the four facings in `Chest`'s own compass order so
    /// `chestFacing` stays arithmetic here too.
    ///
    /// It is drawn, mined, blasted and dropped as the plain chest it will
    /// become; the id carries exactly one extra fact, that the loot inside has
    /// not been rolled. **That fact is worth a block-id family because a block
    /// is the one thing that persists for free.** Rolling converts the id to
    /// the plain chest, and `setBlock` flags the chunk modified, so the chunk
    /// is saved and thereafter *loaded* rather than regenerated. Without that,
    /// `open -> empty -> save -> reload` re-rolls forever: `saveChests` drops a
    /// chest that is empty and an unmodified chunk is regenerated, so the loot
    /// comes back and the whole thing is an infinite duplication exploit.
    ///
    /// Which *table* has to live in the id as well, because a break and an
    /// explosion know a block position and nothing else - re-solving the
    /// village plan to ask which building this was is both expensive and
    /// fragile, since it would re-roll every existing chest the day the plot
    /// weights are retuned.
    LootChestRunFirst,
    LootChestRunLast = LootChestRunFirst + kLootChestTables * 4 - 1,

    /// **The four honey levels a hive passes through on its way to full**, four
    /// facings each, appended at the tail rather than spliced into the hive run.
    ///
    /// The reference stores `honey_level` 0-5 and this family carried one bit,
    /// so a hive could be empty or full and never *filling*. That is why the
    /// shear path could not fire and honeycomb was unobtainable: there was
    /// nowhere to record five sixths of the states.
    ///
    /// **Appended, never inserted, and that is the whole reason this shape was
    /// chosen.** Levels 0 and 5 keep the exact ids they have always had, so
    /// every saved world keeps its meaning and **no chunk format bump is owed**.
    /// Splicing sixteen ids into the middle would have renumbered every block
    /// declared after the hive - roughly two thousand eight hundred of them -
    /// and, worse than merely needing a bump, would have re-read a stored full
    /// hive as a level-1 one, which is a silent change of meaning rather than a
    /// discard.
    ///
    /// **Nothing here needs new art.** The reference paints
    /// `beehive_front_honey` at level 5 alone and the plain front at every level
    /// below it, so these sixteen draw exactly as an empty hive does and
    /// `kBeehiveSprites` stays four wide. That is what lets this land inside one
    /// file instead of needing the three-file sprite protocol.
    BeehiveHoneyRunFirst,
    BeehiveHoneyRunLast = BeehiveHoneyRunFirst + kBeehivePartialHoneyLevels * 4 - 1,

    /// **The bee nest: the same four facings at the same six honey levels, and
    /// the naturally generated half of the pair.** A hive is crafted, a nest is
    /// found on a tree - and that difference is the whole reason the nest has to
    /// exist rather than being a hive wearing a different picture, because
    /// nothing generated can be crafted, and nothing crafted generates.
    ///
    /// **One clean run of twenty-four, level-major**, because unlike the hive
    /// this family has no history to preserve: not one id, save or caller
    /// existed before today. `BeeNestRunFirst + level * 4 + facing` is therefore
    /// the whole of its arithmetic, where the hive needs three pieces.
    ///
    /// **Appended at the tail like the hive run, so again no chunk format bump
    /// is owed** - every id a saved world can already contain keeps its number,
    /// and a world saved before today simply has no nests in it, which is what a
    /// world whose generator does not place them should have.
    ///
    /// **`honey_level` is `[0-5]` here for the same measured reason it is on the
    /// hive**, and the same two sources state it: `blockstates/bee_nest.json` in
    /// `reference/minecraft-assets-26.2` enumerates twenty-four variants, four
    /// facings by six levels, and Mojang's `mojang-blocks.json` gives the
    /// property exactly two users, `beehive` and `bee_nest`. **Two users is what
    /// makes six authoritative for both** rather than merely the width of a
    /// shared field.
    ///
    /// **The art is the hive's for now, and that is a stated placeholder rather
    /// than an oversight.** `models/block/bee_nest_empty.json` names five
    /// distinct textures where the hive names four - the nest has its own top
    /// *and* its own bottom, where the hive shares one `beehive_end` between
    /// them - and all five `bee_nest_*.png` exist upstream. None is staged, and
    /// staging is a three-file protocol reaching `make-reference-blocks.ps1` and
    /// `Main.cpp`'s sprite list, neither of which is editable from here. See
    /// `kBeeNestFrontSprite`, where the placeholder is one alias per face and an
    /// assert states what closing it looks like.
    BeeNestRunFirst,
    BeeNestRunLast = BeeNestRunFirst + kBeehiveHoneyLevels * 4 - 1,

    /// Farmland moisture 1 to 6. Levels 0 and 7 are `Farmland` and
    /// `FarmlandMoist` up in extra run 4, and they stay exactly where they are:
    /// `Farmland` is `kFirstExtraBlock4`, so inserting six ids beside it would
    /// have moved the head of that run and every id behind it - thousands of
    /// them, all on disk in saved chunks - to buy contiguity that one
    /// subtraction in `farmlandMoisture` provides for nothing.
    ///
    /// **This is the beehive's layout, deliberately.** Same reason, same shape,
    /// same round-trip assert: two ids that already exist and must keep their
    /// numbers, so the new middle goes at the tail and one accessor hides the
    /// join. Anything that wants a level asks `farmlandMoisture`; anything that
    /// wants an id asks `farmlandAtMoisture`; nothing else does the arithmetic.
    FarmlandMoistureRunFirst,
    FarmlandMoistureRunLast = FarmlandMoistureRunFirst + kFarmlandPartialMoistureLevels - 1,

    /// **The stair and slab tail runs, and why they are here rather than in
    /// `StairsRun` and `SlabRun` a thousand ids below.**
    ///
    /// A stair family owns eight consecutive ids and a slab family two, allocated
    /// purely by the family's *position* in `kStairFamilies` and `kSlabFamilies`.
    /// So four waxed stair families plus four waxed slab families are not eight
    /// ids, they are **forty** - and because `StairsRunLast` is computed from the
    /// family count while `SlabRunFirst` is merely the enumerator after it,
    /// lengthening the stair run slides the whole slab run and everything above
    /// it. All of those are already in saved chunks, so that edit renumbers a
    /// player's world silently rather than costing a few ids.
    ///
    /// Parked at the tail instead, they cost one branch each in `isStairs`,
    /// `isSlab`, `stairOffset`, `stairFamily`, `stairsAt`, `slabFamily`,
    /// `isUpperHalf` and `slabAt`, and **no id already on disk changes value**.
    /// This is the layout of the farmland moisture run directly above and of the
    /// beehive before it, and of `family == 0` - cobblestone stairs and the stone
    /// slab have sat outside their own runs from the start for this same reason.
    /// One convention, now used four times, rather than a second mechanism.
    ///
    /// **The growth rule that keeps it cheap: a tail run may only be extended
    /// while it is the last one.** Growing a tail that has another behind it
    /// slides that one, which is the exact harm this layout exists to avoid. A
    /// future shape therefore appends a *new* run here rather than lengthening
    /// either of these two.
    WaxedCutCopperStairsRunFirst,
    WaxedCutCopperStairsRunLast =
        WaxedCutCopperStairsRunFirst + kStairTailFamilyCount * 8 - 1,
    WaxedCutCopperSlabRunFirst,
    WaxedCutCopperSlabRunLast = WaxedCutCopperSlabRunFirst + kSlabTailFamilyCount * 2 - 1,
};

/// The highest id in use. Anything that walks every block reads this rather
/// than naming whichever block happens to be last, which is how the catalogue
/// silently stopped one short of the newest one.
///
/// **This line moves every time a new run is appended above, and forgetting it is
/// exactly the failure the paragraph above describes.** It named
/// `FarmlandMoistureRunLast` until the waxed copper tail runs were added.
constexpr BlockId kLastBlock = BlockId::WaxedCutCopperSlabRunLast;

/// How many block ids exist, for anything that wants an array with one slot per
/// block. Derived, so it cannot fall behind the enum the way a literal 256 did.
///
/// **WARNING - GROWING THIS CONSTANT CAN BREAK FILES THAT NEVER MENTION IT, and
/// the warning sits here rather than in a ledger because the person about to
/// break them is the person editing the enum above.** Several files walk every
/// id in a *strided* `constexpr` sweep, because MSVC gives a `constexpr`
/// evaluation a step budget that one loop over every block exceeds. Each such
/// sweep carries a pass count and a stride, and is correct only while
/// `passes * stride >= kBlockIdCount`.
///
/// **The constraint, phrased to stay true however the tree moves:** every
/// strided sweep anywhere must satisfy that inequality against this constant,
/// and *growing this constant is the only edit that can falsify one*. The
/// failure is loud but misdirected - the sweep's own assert fires, in a file the
/// author of the new id never opened, naming neither the id nor this constant.
///
/// **Find them by convention rather than from a list, because a list rots and a
/// search does not:** they are named `*SweepPasses` and `*SweepStride`. Verified
/// 2026-08-19 that `Copper.hpp` carries `kCopperSweepPasses` and
/// `kCopperSweepStride` under exactly that assert, and that it in turn names
/// `kBoxFaceSweepPasses` in `ChunkMesher.cpp` as sharing its arithmetic.
///
/// **Headroom is NOT uniform, and that is the trap.** Two designs exist here. A
/// *fixed* stride with a *fixed* pass count has finite spare, and one outside
/// this file has already been raised after a single feature's ids consumed most
/// of it - so a batch this file absorbs in silence can still exhaust another's.
/// A stride *derived* from this count, as `kModelSweepStride` below is, has no
/// capacity limit at all: its passes times its stride cover the count by
/// construction, for any count.
///
/// **But the derived form trades one limit for another rather than removing
/// both, and the second has no assert.** A fixed stride holds the steps per pass
/// constant however many ids arrive; a derived stride grows them, and a
/// `constexpr` evaluation that exceeds MSVC's step budget fails with a message
/// naming neither the sweep nor the count. Debug is the binding case, because
/// `_ITERATOR_DEBUG_LEVEL=2` bounds-checks every `std::array` subscript and
/// multiplies the step count several times over, so a sweep can pass Release and
/// fail Debug. Derive the stride when the count may grow a lot; keep it fixed
/// when the per-pass work is heavy. Neither choice is free.
///
/// **Falsified by** any sweep whose `passes * stride` no longer exceeds this
/// count, and by any new `*Sweep*` constant the search above does not reach - so
/// re-run the search rather than trusting this paragraph.
constexpr std::size_t kBlockIdCount = static_cast<std::size_t>(kLastBlock) + 1;

/// Block items share `BlockId`'s numbering below `ItemId::kFirstToolItem`, so
/// that boundary is the real ceiling rather than the width of the type.
static_assert(static_cast<int>(kLastBlock) < 4096,
              "block ids must stay below ItemId::kFirstToolItem, which is 4096");

/// Where the table-driven run starts, and where it ends. It is no longer the
/// tail of the enum, so the end is named rather than assumed to be `kLastBlock`.
constexpr BlockId kFirstExtraBlock = BlockId::CobbledDeepslate;
constexpr BlockId kLastExtraBlock = BlockId::Cobweb;

/// The second table-driven run, appended after the hive. Two runs rather than
/// one widened run because everything between them is already written into
/// saved worlds; `extraBlockInfo` maps both onto one table.
constexpr BlockId kFirstExtraBlock2 = BlockId::Netherrack;
constexpr BlockId kLastExtraBlock2 = BlockId::StrippedBambooBlock;

/// And a third, for the same reason there is a second.
constexpr BlockId kFirstExtraBlock3 = BlockId::WhiteStainedGlass;
constexpr BlockId kLastExtraBlock3 = BlockId::EndRod;

/// And a fourth: the farm.
constexpr BlockId kFirstExtraBlock4 = BlockId::Farmland;
constexpr BlockId kLastExtraBlock4 = BlockId::ComposterLast;

/// And a fifth: the decorative batch, which is almost entirely plain cubes.
constexpr BlockId kFirstExtraBlock5 = BlockId::OakWood;
constexpr BlockId kLastExtraBlock5 = BlockId::RespawnAnchor;

/// And a sixth: the candles and the last few oddments.
constexpr BlockId kFirstExtraBlock6 = BlockId::Candle;
constexpr BlockId kLastExtraBlock6 = BlockId::BlastFurnaceExtraLast;

/// And a seventh. It is separate from the sixth only because the doors,
/// trapdoors and beds sit between them, and none of those is a table block.
constexpr BlockId kFirstExtraBlock7 = BlockId::EnderChest;
constexpr BlockId kLastExtraBlock7 = BlockId::StowboxDyedLast;

/// How many rows the first run occupies, which is where the second run's rows
/// begin in the table.
constexpr int kExtraRun1Count =
    static_cast<int>(kLastExtraBlock) - static_cast<int>(kFirstExtraBlock) + 1;
constexpr int kExtraRun2Count =
    static_cast<int>(kLastExtraBlock2) - static_cast<int>(kFirstExtraBlock2) + 1;
constexpr int kExtraRun3Count =
    static_cast<int>(kLastExtraBlock3) - static_cast<int>(kFirstExtraBlock3) + 1;
constexpr int kExtraRun4Count =
    static_cast<int>(kLastExtraBlock4) - static_cast<int>(kFirstExtraBlock4) + 1;
constexpr int kExtraRun5Count =
    static_cast<int>(kLastExtraBlock5) - static_cast<int>(kFirstExtraBlock5) + 1;
constexpr int kExtraRun6Count =
    static_cast<int>(kLastExtraBlock6) - static_cast<int>(kFirstExtraBlock6) + 1;
constexpr int kExtraRun7Count =
    static_cast<int>(kLastExtraBlock7) - static_cast<int>(kFirstExtraBlock7) + 1;

/// A candle, plain or dyed. Seventeen contiguous ids, plain first.
constexpr bool isCandle(BlockId id) {
    return (id >= BlockId::Candle && id <= BlockId::BlackCandle) ||
           (id >= BlockId::CandleExtraFirst && id <= BlockId::CandleExtraLast);
}

/// Which colour, 0 plain then white through black - the same order every dyed
/// family in the game uses, so a candle's dye is one offset here too.
constexpr int candleColour(BlockId id) {
    if (id <= BlockId::BlackCandle) {
        return static_cast<int>(id) - static_cast<int>(BlockId::Candle);
    }
    return (static_cast<int>(id) - static_cast<int>(BlockId::CandleExtraFirst)) / 7;
}

/// Where in a colour's seven appended states this one sits, or -1 for the
/// original single lit candle.
constexpr int candleVariant(BlockId id) {
    if (id <= BlockId::BlackCandle) {
        return -1;
    }
    return (static_cast<int>(id) - static_cast<int>(BlockId::CandleExtraFirst)) % 7;
}

constexpr bool isCandleLit(BlockId id) { return candleVariant(id) < 3; }

/// How many candles stand in the cell, one to four.
constexpr int candleCount(BlockId id) {
    const int variant = candleVariant(id);
    if (variant < 0) {
        return 1;
    }
    return variant < 3 ? variant + 2 : variant - 2;
}

constexpr BlockId candleAt(int colour, int count, bool lit) {
    const int clamped = count < 1 ? 1 : (count > 4 ? 4 : count);
    if (lit && clamped == 1) {
        return static_cast<BlockId>(static_cast<int>(BlockId::Candle) + colour);
    }
    const int variant = lit ? clamped - 2 : clamped + 2;
    return static_cast<BlockId>(static_cast<int>(BlockId::CandleExtraFirst) + colour * 7 + variant);
}

// The mapping has to survive a round trip in both directions, or a candle lit
// with a flint and steel would come back a different colour or a different
// count. Cheaper to prove than to find out.
static_assert(candleAt(0, 1, true) == BlockId::Candle);
static_assert(candleAt(16, 1, true) == BlockId::BlackCandle);
static_assert(candleColour(candleAt(5, 3, false)) == 5);
static_assert(candleCount(candleAt(5, 3, false)) == 3);
static_assert(!isCandleLit(candleAt(5, 3, false)));
static_assert(candleColour(candleAt(12, 4, true)) == 12);
static_assert(candleCount(candleAt(12, 4, true)) == 4);
static_assert(isCandleLit(candleAt(12, 4, true)));
static_assert(candleCount(BlockId::Candle) == 1 && isCandleLit(BlockId::Candle));

/// The five corals, in the one order every family of them uses: tube, brain,
/// bubble, fire, horn, live before dead. Everything about coral is one offset
/// from these, which is why there is no per-species table anywhere.
constexpr bool isCoralPlant(BlockId id) {
    return id >= BlockId::TubeCoral && id <= BlockId::DeadHornCoral;
}

constexpr bool isCoralFan(BlockId id) {
    return id >= BlockId::TubeCoralFan && id <= BlockId::DeadHornCoralFan;
}

constexpr bool isCoralBlock(BlockId id) {
    return (id >= BlockId::TubeCoralBlock && id <= BlockId::HornCoralBlock) ||
           (id >= BlockId::DeadTubeCoralBlock && id <= BlockId::DeadHornCoralBlock);
}

/// Whether this coral is already dead, in any of its three forms.
constexpr bool isDeadCoral(BlockId id) {
    return (id >= BlockId::DeadTubeCoralBlock && id <= BlockId::DeadHornCoralBlock) ||
           (id >= BlockId::DeadTubeCoral && id <= BlockId::DeadHornCoral) ||
           (id >= BlockId::DeadTubeCoralFan && id <= BlockId::DeadHornCoralFan);
}

/// Cross-shaped, and not a plant.
///
/// **The one owner of `BlockShape::Cross` no longer meaning "a plant".** The
/// shape began as the two crossed quads a flower is drawn with and is now the
/// general answer for anything drawn that way, so four separate rules keyed on
/// it - what burns, what water washes away, what a player builds into and what
/// a distant chunk may leave out - were all applied to stone and crystal. Each
/// of those now asks a list; this is the list of what the shape gets wrong, so
/// there is one place to add the next one.
///
/// The reference calls every one of these solid, waterloggable and
/// non-flammable: an amethyst geode, a dripstone cave's stalactites, a coral
/// reef with its sea pickles, a conduit and the end's chorus forest.
constexpr bool isMineralGrowth(BlockId id) {
    return id == BlockId::AmethystCluster || id == BlockId::SmallAmethystBud ||
           id == BlockId::MediumAmethystBud || id == BlockId::LargeAmethystBud ||
           id == BlockId::PointedDripstone || id == BlockId::Conduit ||
           id == BlockId::SeaPickle || id == BlockId::ChorusPlant ||
           id == BlockId::ChorusFlower || isCoralPlant(id) || isCoralFan(id);
}

/// What a live coral becomes when it is left out of water. **One offset per
/// family**, so the rule is arithmetic rather than fifteen cases - which is
/// also what stops a new coral species needing an edit here.
constexpr BlockId deadCoralFor(BlockId id) {
    if (id >= BlockId::TubeCoralBlock && id <= BlockId::HornCoralBlock) {
        return static_cast<BlockId>(static_cast<int>(BlockId::DeadTubeCoralBlock) +
                                    static_cast<int>(id) -
                                    static_cast<int>(BlockId::TubeCoralBlock));
    }
    if (id >= BlockId::TubeCoral && id <= BlockId::HornCoral) {
        return static_cast<BlockId>(static_cast<int>(BlockId::DeadTubeCoral) +
                                    static_cast<int>(id) - static_cast<int>(BlockId::TubeCoral));
    }
    if (id >= BlockId::TubeCoralFan && id <= BlockId::HornCoralFan) {
        return static_cast<BlockId>(static_cast<int>(BlockId::DeadTubeCoralFan) +
                                    static_cast<int>(id) - static_cast<int>(BlockId::TubeCoralFan));
    }
    return id;
}

/// The two-block flowers, and which half this is. Bottom then top for each, so
/// the pair is one offset apart.
constexpr bool isTallFlower(BlockId id) {
    return id >= BlockId::SunflowerLower && id <= BlockId::PeonyUpper;
}

constexpr bool isTallFlowerUpper(BlockId id) {
    return isTallFlower(id) &&
           ((static_cast<int>(id) - static_cast<int>(BlockId::SunflowerLower)) % 2) == 1;
}

/// A copper bulb, and whether it is lit. Four oxidation stages, unlit then lit.
constexpr bool isCopperBulb(BlockId id) {
    return id >= BlockId::CopperBulb && id <= BlockId::OxidizedCopperBulbLit;
}

constexpr bool isCopperBulbLit(BlockId id) {
    return isCopperBulb(id) &&
           ((static_cast<int>(id) - static_cast<int>(BlockId::CopperBulb)) % 2) == 1;
}

/// The six moisture levels that needed new ids - 1 to 6. Levels 0 and 7 are
/// not in here, which is the whole point: they are the two ids that already
/// existed and could not move.
constexpr bool isFarmlandMoistureRun(BlockId id) {
    return id >= BlockId::FarmlandMoistureRunFirst && id <= BlockId::FarmlandMoistureRunLast;
}

/// Farmland at any moisture. **Eight ids for one block now, not two**, so the
/// mesher, the crop rules and the hoe can all still ask one question.
///
/// **Widening a family predicate kills every early-out already sitting behind
/// it** - bug shape #2, the one that turned eight `case` labels into dead code
/// when `isFurnace` grew to cover smokers. So every caller was read and
/// counted, not estimated - see the measurement note below, which states its
/// method because a bare count is unfalsifiable.
///
/// **Every caller wants the wider answer - measured, not assumed.** On
/// 2026-08-19 a bare-name sweep over `game\src` found 20 occurrences: this
/// definition, four more in this file (two asserts, the round-trip checker,
/// `blockShape`), and **15 real callers across 8 files** - `Sounds.hpp`,
/// `Item.hpp` twice, `Mining.hpp` twice, `Explosion.hpp`, `Farming.hpp` three
/// times, `Material.hpp`, `World.cpp` four times, `Main.cpp`. Every one asks a
/// question the six new levels answer the same way as `FarmlandMoist`, and a
/// 15-column dump of all 3,315 ids confirms it rather than my reading of it:
/// name, shape, solidity, support, three texture layers, material family,
/// sound, blast resistance and drop are identical across all eight. Controls
/// for that sweep: the live twin `isCropBlock` scored 16 and an invented
/// `isMudland` scored 0, so the instrument separates present from absent.
///
/// Most callers get the widening twice over: `blockShape` answers `Tilled`
/// through this predicate, and `isSolid` and `needsSupportBelow` answer through
/// `blockShape`, so solidity, collision and support all follow from this one
/// line rather than from six new rows.
///
/// **The narrower questions do not go through here at all**, and they are why
/// this change is not finished in this file. `World.cpp` gates both its
/// moisture step and its hydration scoring on `isFarmland`, which now widens
/// correctly - and then compares `== BlockId::FarmlandMoist` *inside* the gate,
/// at six sites. Those are equality tests against level 7, so the six new
/// levels score as *dry while looking wet*, and the drying step still falls
/// from full to nothing in one tick instead of walking down eight. **Only the
/// override is narrower than the gate that admits it**, which is the same shape
/// as the per-face model bug fixed earlier tonight. Filed, not edited - that
/// file has another owner.
constexpr bool isFarmland(BlockId id) {
    return id == BlockId::Farmland || id == BlockId::FarmlandMoist || isFarmlandMoistureRun(id);
}

// The ends are outside the run and the run is exactly the middle. Stated as an
// assert because "the six in between" is the kind of claim that reads as
// obviously true and is off by one.
static_assert(!isFarmlandMoistureRun(BlockId::Farmland) &&
                  !isFarmlandMoistureRun(BlockId::FarmlandMoist) &&
                  isFarmland(BlockId::FarmlandMoistureRunFirst) &&
                  isFarmland(BlockId::FarmlandMoistureRunLast),
              "the moisture run must hold the middle levels and neither end");
static_assert(static_cast<int>(BlockId::FarmlandMoistureRunLast) -
                      static_cast<int>(BlockId::FarmlandMoistureRunFirst) + 1 ==
                  kFarmlandMoistureLevels - 2,
              "the run must be exactly the levels that are not Farmland or FarmlandMoist");

/// How wet this farmland is, 0 to 7. Anything that is not farmland reads 0,
/// which is the same answer plain dirt would deserve.
constexpr int farmlandMoisture(BlockId id) {
    if (isFarmlandMoistureRun(id)) {
        return static_cast<int>(id) - static_cast<int>(BlockId::FarmlandMoistureRunFirst) + 1;
    }
    return id == BlockId::FarmlandMoist ? kFarmlandFullMoisture : 0;
}

/// The farmland id for a moisture level, clamped. The inverse of
/// `farmlandMoisture`, and the only place that knows the run is not contiguous
/// with its two ends.
constexpr BlockId farmlandAtMoisture(int moisture) {
    const int clamped =
        moisture < 0 ? 0 : (moisture > kFarmlandFullMoisture ? kFarmlandFullMoisture : moisture);
    if (clamped == 0) {
        return BlockId::Farmland;
    }
    if (clamped == kFarmlandFullMoisture) {
        return BlockId::FarmlandMoist;
    }
    return static_cast<BlockId>(static_cast<int>(BlockId::FarmlandMoistureRunFirst) + clamped - 1);
}

/// Drives whatever it is handed over all eight levels and requires every one to
/// come back as itself, as farmland, and as a distinct id. Taking the builder
/// as a parameter is what lets the wrong builder be named below and rejected,
/// rather than the assert merely agreeing with whatever the file happens to do.
constexpr bool farmlandRoundTrips(BlockId (*make)(int)) {
    for (int level = 0; level < kFarmlandMoistureLevels; ++level) {
        const BlockId id = make(level);
        if (!isFarmland(id) || farmlandMoisture(id) != level) {
            return false;
        }
        for (int other = 0; other < level; ++other) {
            if (make(other) == id) {
                return false;
            }
        }
    }
    return true;
}

/// **This is what the file did until 2026-08-19**, kept so it can be refused.
/// Two ids collapse levels 1 to 6 onto level 7, so the round trip loses the
/// level - and a reader who assumes "wet is wet" gets exactly this.
constexpr BlockId farmlandAtMoistureTwoIdsOnly(int moisture) {
    return moisture <= 0 ? BlockId::Farmland : BlockId::FarmlandMoist;
}

static_assert(farmlandRoundTrips(farmlandAtMoisture),
              "every moisture level must survive a trip through an id and back");
static_assert(!farmlandRoundTrips(farmlandAtMoistureTwoIdsOnly),
              "the two-id scheme must fail this - if it passes, the checker is not "
              "reading the level and proves nothing");
static_assert(farmlandAtMoisture(0) == BlockId::Farmland &&
                  farmlandAtMoisture(kFarmlandFullMoisture) == BlockId::FarmlandMoist,
              "the two ids that were already on disk must keep their meanings");
static_assert(farmlandAtMoisture(-3) == BlockId::Farmland &&
                  farmlandAtMoisture(99) == BlockId::FarmlandMoist,
              "clamping, so a caller that has just decremented past zero cannot walk "
              "out of the run and into whatever id sits beside it");

/// Anything that grows through ages on farmland. **Stems are deliberately not
/// in here** - they grow the same way but they fruit rather than being
/// harvested, so every caller that means "a crop I can break for food" would
/// have to exclude them again.
constexpr bool isCropBlock(BlockId id) {
    return id >= BlockId::WheatCrop0 && id <= BlockId::BeetrootCropLast;
}

/// Which crop family a block belongs to, as the id of that family's age 0.
/// One subtraction away from the age, which is why nothing else stores one.
constexpr BlockId cropFamily(BlockId id) {
    if (id >= BlockId::WheatCrop0 && id <= BlockId::WheatCropLast) return BlockId::WheatCrop0;
    if (id >= BlockId::CarrotCrop0 && id <= BlockId::CarrotCropLast) return BlockId::CarrotCrop0;
    if (id >= BlockId::PotatoCrop0 && id <= BlockId::PotatoCropLast) return BlockId::PotatoCrop0;
    return BlockId::BeetrootCrop0;
}

constexpr int cropAge(BlockId id) {
    return static_cast<int>(id) - static_cast<int>(cropFamily(id));
}

constexpr BlockId cropAt(BlockId family, int age) {
    return static_cast<BlockId>(static_cast<int>(family) + (age < 0 ? 0 : (age > 7 ? 7 : age)));
}

/// Ages 0-7 of either stem, before it has fruited.
constexpr bool isGrowingStem(BlockId id) {
    return (id >= BlockId::MelonStem0 && id <= BlockId::MelonStemLast) ||
           (id >= BlockId::PumpkinStem0 && id <= BlockId::PumpkinStemLast);
}

/// A stem that has already put a fruit down and is pointing at it.
constexpr bool isAttachedStem(BlockId id) {
    return (id >= BlockId::MelonStemAttachedFirst && id <= BlockId::MelonStemAttachedLast) ||
           (id >= BlockId::PumpkinStemAttachedFirst && id <= BlockId::PumpkinStemAttachedLast);
}

constexpr bool isStemBlock(BlockId id) { return isGrowingStem(id) || isAttachedStem(id); }

/// True for the melon half of either run, false for the pumpkin half.
constexpr bool stemGrowsMelon(BlockId id) {
    return (id >= BlockId::MelonStem0 && id <= BlockId::MelonStemLast) ||
           (id >= BlockId::MelonStemAttachedFirst && id <= BlockId::MelonStemAttachedLast);
}

constexpr int stemAge(BlockId id) {
    if (id >= BlockId::PumpkinStem0 && id <= BlockId::PumpkinStemLast) {
        return static_cast<int>(id) - static_cast<int>(BlockId::PumpkinStem0);
    }
    if (id >= BlockId::MelonStem0 && id <= BlockId::MelonStemLast) {
        return static_cast<int>(id) - static_cast<int>(BlockId::MelonStem0);
    }
    return 7;
}

constexpr bool isNetherWart(BlockId id) {
    return id >= BlockId::NetherWart0 && id <= BlockId::NetherWartLast;
}

constexpr int netherWartAge(BlockId id) {
    return static_cast<int>(id) - static_cast<int>(BlockId::NetherWart0);
}

constexpr bool isCarvedPumpkin(BlockId id) {
    return id >= BlockId::CarvedPumpkinFirst && id <= BlockId::CarvedPumpkinLast;
}

constexpr bool isJackOLantern(BlockId id) {
    return id >= BlockId::JackOLanternFirst && id <= BlockId::JackOLanternLast;
}

constexpr bool isComposter(BlockId id) {
    return id >= BlockId::Composter0 && id <= BlockId::ComposterLast;
}

constexpr int composterLevel(BlockId id) {
    return static_cast<int>(id) - static_cast<int>(BlockId::Composter0);
}

constexpr BlockId composterAt(int level) {
    return static_cast<BlockId>(static_cast<int>(BlockId::Composter0) +
                                (level < 0 ? 0 : (level > 8 ? 8 : level)));
}

/// **A bee's home, crafted or found**: four facings at six honey levels, across
/// the three runs they were declared in.
///
/// **This name says hive and means hive-or-nest, and that is deliberate.** The
/// reference's two blocks are the same block in almost every respect a caller
/// can ask about - both are wood, both burn, both face four ways, both fill with
/// honey one level at a time, both are sheared for honeycomb, both go in the
/// Nature tab. So the nest joining the family here is what stops `CLAUDE.md` bug
/// shape #14, a rule that is correct in only one of the two places that need it.
///
/// **Widening a family predicate kills every early-out already sitting behind
/// it** - bug shape #2, which is how `isFurnace` growing to cover smokers turned
/// eight `case` labels into dead code. So every caller was read, not just the new
/// one, and **counted rather than estimated**: an earlier draft of this comment
/// said sixteen and it was wrong, which is the one kind of mistake nothing in
/// this project catches.
///
/// **Thirteen consumers ask this, across seven files** - `Sounds.hpp`,
/// `Item.hpp`, `Mining.hpp`, `Explosion.hpp`, `Material.hpp`, `Main.cpp` and
/// this one. Measured 2026-08-19 with comments and string literals blanked,
/// excluding the definition, the asserts, and the three derivations inside this
/// file that are this predicate rather than users of it - `isCraftedBeehive`,
/// `beehiveHasHoney` and `beehiveRoundTrips`.
///
/// **Nine of the thirteen want the wider answer and got it for free**: the
/// material family, the dig sound, flammability, blast resistance, the mining
/// tool, the hardness, the Nature tab, the catalogue test, and
/// `blockFacingDirection` - which works because `beehiveFacing` was widened in
/// the same edit rather than a later one.
///
/// **Two are hive-only and are split immediately below their old answer**:
/// `blockName` returned "Beehive" for everything this accepted, and
/// `blockTextureLayer` reached straight for hive art.
///
/// **Two more are hive-only and live in files this cannot reach, so they are
/// filed rather than fixed**: `Item.hpp`'s drop canonicalises any home to a
/// beehive item, where the reference drops nothing from a nest without silk
/// touch; and `Main.cpp` rebuilds a sheared or placed home through
/// `beehiveAt(beehiveFacing(...), ...)`, which throws the kind away. **The
/// second is what `beeHomeAtLevel` exists to replace**, and the control beside
/// it is that exact call, kept so it can be rejected rather than described.
///
/// **Three ranges, because levels 0 and 5 of the hive predate the other four.**
/// Splicing the missing levels into the first run would have renumbered every
/// block declared after it, which changes what a saved chunk *means* rather than
/// merely what format it is in.
constexpr bool isBeehive(BlockId id) {
    return (id >= BlockId::Beehive && id <= BlockId::BeehiveHoneyWest) ||
           (id >= BlockId::BeehiveHoneyRunFirst && id <= BlockId::BeehiveHoneyRunLast) ||
           (id >= BlockId::BeeNestRunFirst && id <= BlockId::BeeNestRunLast);
}

/// The found half of the family: a nest, not a hive.
constexpr bool isBeeNest(BlockId id) {
    return id >= BlockId::BeeNestRunFirst && id <= BlockId::BeeNestRunLast;
}

/// The crafted half. **Written as "a home that is not a nest" rather than as its
/// own pair of ranges**, so the day a fourth run is appended there is one place
/// to change and not two that can disagree about which blocks exist.
constexpr bool isCraftedBeehive(BlockId id) { return isBeehive(id) && !isBeeNest(id); }

// Every home is exactly one of the two, and nothing outside is either. The
// second line is the one that would catch a range typed one id wide.
static_assert(isCraftedBeehive(BlockId::Beehive) && !isBeeNest(BlockId::Beehive) &&
                  isBeeNest(BlockId::BeeNestRunFirst) &&
                  !isCraftedBeehive(BlockId::BeeNestRunFirst) &&
                  isBeeNest(BlockId::BeeNestRunLast) && isBeehive(BlockId::BeeNestRunLast),
              "a bee's home must be a nest or a hive and never both or neither");
static_assert(!isBeeNest(BlockId::BeehiveHoneyRunLast) && !isBeeNest(BlockId::Lava0) &&
                  !isBeeNest(BlockId::Stone),
              "the nest range must not reach back over the id declared before it");

// **The two runs must meet exactly.** Written as arithmetic rather than as two
// range tests, because a gap leaves ids that are neither and an overlap leaves
// ids that are both, and each of those reads as a plausible block from every
// caller's side.
static_assert(static_cast<int>(BlockId::BeeNestRunFirst) ==
                  static_cast<int>(BlockId::BeehiveHoneyRunLast) + 1,
              "the nest run must start where the hive's partial run ends");
static_assert(static_cast<int>(BlockId::BeeNestRunLast) -
                      static_cast<int>(BlockId::BeeNestRunFirst) + 1 ==
                  kBeehiveHoneyLevels * 4,
              "the nest run must be four facings wide at every one of the six honey levels");

/// Settled snow, one to seven layers deep.
constexpr bool isSnowLayer(BlockId id) {
    return id >= BlockId::SnowLayerFirst && id <= BlockId::SnowLayerLast;
}

/// How many layers deep, counting from one. Eight would be a solid block, which
/// is `BlockId::Snow` - so seven is as deep as this family goes.
///
/// **That boundary became reachable by accumulation and is currently a one-way
/// door.** Snowy Plains publishes a snow accumulation of up to 1.0 blocks, which
/// is exactly eight layers, so `snowLayerAt` really does write `BlockId::Snow`
/// onto a column during snowfall. Melting tests `isSnowLayer`, and the solid
/// block is not one - so a drift that stops at seven melts away a layer at a
/// time while a drift that reaches the brim stays put after the sun comes out.
///
/// **The fix is an eighth id in this run, not a melting `BlockId::Snow`.** The
/// reference treats eight layers as a state of the layer block - Silk Touch on
/// eight "drops a snow block instead" is phrased as an exception precisely
/// because the block is not one - and teaching the crafted, mined, solid snow
/// block to melt would make every one of them vanish in sunlight, which is a
/// different and much louder bug. Blocked on the id itself: a new enumerator
/// moves `kBlockIdCount` and wants rows in tables this file does not own.
constexpr int snowLayerDepth(BlockId id) {
    return isSnowLayer(id) ? static_cast<int>(id) - static_cast<int>(BlockId::SnowLayerFirst) + 1
                           : 0;
}

/// The id for a depth of one to seven. Anything deeper is the solid cube.
constexpr BlockId snowLayerAt(int depth) {
    return depth >= 8 ? BlockId::Snow
                      : static_cast<BlockId>(static_cast<int>(BlockId::SnowLayerFirst) +
                                             (depth < 1 ? 0 : depth - 1));
}

/// How full this hive is, `0` to `5`, and `0` for anything that is not a hive.
///
/// **The visible difference is only at 5**, which is exactly what made one bit
/// look sufficient for twenty milestones: the reference paints honey on the
/// front at level 5 and an empty front at every level below it. That reading was
/// right about *appearance* and wrong about *simulation*. A hive fills one level
/// at a time, so a single bit can say empty or full but can never say
/// **filling** - and the shear that gates on full could therefore never fire,
/// which is why honeycomb was unobtainable with a correct shear path.
constexpr int beehiveHoneyLevel(BlockId id) {
    // **The nest is asked first and answers in one line, because its run is a
    // whole `[0-5]` with nothing spliced out of it.** The `+ 1` two lines below
    // is the hive's history showing: its run holds levels 1 to 4 only, so the
    // same expression on this run would report every nest one level too full and
    // an empty one as level 1. Reading them in one branch is exactly the shape
    // that widened a predicate and forgot the offset.
    if (id >= BlockId::BeeNestRunFirst && id <= BlockId::BeeNestRunLast) {
        return (static_cast<int>(id) - static_cast<int>(BlockId::BeeNestRunFirst)) / 4;
    }
    if (id >= BlockId::BeehiveHoneyRunFirst && id <= BlockId::BeehiveHoneyRunLast) {
        return (static_cast<int>(id) - static_cast<int>(BlockId::BeehiveHoneyRunFirst)) / 4 + 1;
    }
    if (id >= BlockId::BeehiveHoney && id <= BlockId::BeehiveHoneyWest) {
        return kBeehiveFullHoney;
    }
    return 0;
}

/// Whether this hive is full, and so worth shearing.
///
/// **Unchanged in meaning by the widening.** It answered "level 5" when 0 and 5
/// were the only levels, and it answers "level 5" now, so every caller outside
/// this file - the shear in `Main.cpp`, the front picture in
/// `blockTextureLayer`, the drop in `Mining.hpp` - keeps its exact behaviour and
/// none of them had to be touched.
///
/// **The family test is not optional.** This was `id >= BeehiveHoney` with no
/// upper bound, which is `true` for every one of the two-and-a-half thousand ids
/// declared after the hive run - lava, every log, every slab, the lot. Both
/// callers happened to sit behind an `isBeehive` already, so it never showed;
/// the third one would have found honey in a staircase.
constexpr bool beehiveHasHoney(BlockId id) {
    return isBeehive(id) && beehiveHoneyLevel(id) == kBeehiveFullHoney;
}

// **Remove the `isBeehive` above and this fails.** `Lava0` is simply the next
// id declared after the hive run, so it is the cheapest witness there is -
// and `LootChestRunLast` is the same witness for the *second* run, being the id
// declared immediately before it.
static_assert(!beehiveHasHoney(BlockId::Lava0) && !beehiveHasHoney(BlockId::Stone));
static_assert(!isBeehive(BlockId::LootChestRunLast) && !isBeehive(BlockId::Lava0) &&
                  beehiveHoneyLevel(BlockId::Stone) == 0,
              "the partial-honey run must not spill into the id declared before it");

/// Every cross-shaped plant, across both of the runs they were appended in.
///
/// **One owner.** The geometry, the cutout test and the catalogue tab all have
/// to give the same answer and they live in three different files, so they ask
/// this rather than each carrying its own list. It replaced a
/// `id >= kFirstCrossExtra` range test, which quietly required plants to be the
/// last thing in the enum forever.
///
/// **Three of its members are no longer *drawn* on two diagonals.** A candle,
/// bamboo and a sea pickle each have a model of their own in the reference and
/// `blockShape` answers `Model` for them before it asks this - which leaves
/// this the owner of the other three questions, all of which still want the
/// plant's answer.
constexpr bool isCrossBlock(BlockId id) {
    return id == BlockId::TallGrass || id == BlockId::Dandelion ||
           id == BlockId::Poppy || id == BlockId::DeadBush ||
           (id >= BlockId::Cornflower && id <= BlockId::Seagrass) ||
           (id >= BlockId::BlueOrchid && id <= BlockId::Cobweb) ||
           (id >= BlockId::Bamboo && id <= BlockId::LargeFern) ||
           // Everything that grows on tilled ground. Taking the plant shape
           // brings four correct behaviours with it and costs no rules of its
           // own: no collision, washed away by a flow, dying when what is under
           // it goes, and drawn as two crossed cutout blades.
           isCropBlock(id) || isStemBlock(id) || isNetherWart(id) ||
           // The fifth run's plants. Coral and its fans, the two-block flowers,
           // the amethyst buds, the dripleaves and the cave vines all want the
           // same four behaviours for the same reason.
           isCoralPlant(id) || isCoralFan(id) || isTallFlower(id) ||
           id == BlockId::WitherRose ||
           (id >= BlockId::SmallAmethystBud && id <= BlockId::LargeAmethystBud) ||
           id == BlockId::BigDripleaf || id == BlockId::SmallDripleaf ||
           id == BlockId::CaveVines || id == BlockId::CaveVinesBerries ||
           id == BlockId::ChorusPlant || id == BlockId::ChorusFlower ||
           isCandle(id) ||
           id == BlockId::Conduit;
}

/// The flowers alone, which is a narrower question than `isCrossBlock` and the
/// one a bee asks. Mushrooms, saplings, ferns, sugar cane and grass are all
/// cross-shaped and none of them is a flower.
///
/// **Two runs, both half-open on names rather than on the run's end**, because
/// each run continues past the last flower in it - so this cannot be written as
/// "everything from here on" the way `isCrossBlock` can.
///
/// **It also has to reach past those two runs, and did not.** The four
/// two-block flowers and the wither rose were added later and outside them, and
/// bees ignored a whole sunflower field - which is the only thing that asks, so
/// nothing else moved when they were left out. Only the *lower* halves count:
/// the upper half is not placed, broken or supported in its own right anywhere
/// else in this file, so making it a flower here would be the one rule that
/// treats it as a plant of its own.
///
/// The wither rose is a flower in the reference and bees do pollinate it, at the
/// cost of the wither they take from standing on it. That cost is the creature
/// code's business, not this predicate's.
constexpr bool isFlower(BlockId id) {
    return id == BlockId::Dandelion || id == BlockId::Poppy ||
           (id >= BlockId::Cornflower && id <= BlockId::OrangeTulip) ||
           (id >= BlockId::BlueOrchid && id <= BlockId::LilyOfTheValley) ||
           id == BlockId::WitherRose || (isTallFlower(id) && !isTallFlowerUpper(id)) ||
           // "A flowering azalea is a variant of azalea that counts as a flower
           // for all purposes, including pollination by bees, except for
           // crafting dyes." Dye is not a question this predicate is asked, so
           // the exception costs nothing. The plain azalea beside it is **not** a
           // flower and stays out - which is the whole reason this is a named id
           // and not `isAzalea`.
           id == BlockId::FloweringAzalea;
}

static_assert(isFlower(BlockId::Dandelion) && isFlower(BlockId::Poppy) &&
                  isFlower(BlockId::Allium) && isFlower(BlockId::LilyOfTheValley),
              "every flower must answer yes");
static_assert(!isFlower(BlockId::BrownMushroom) && !isFlower(BlockId::TallGrass) &&
                  !isFlower(BlockId::OakSapling) && !isFlower(BlockId::Fern) &&
                  !isFlower(BlockId::SugarCane) && !isFlower(BlockId::Kelp),
              "a cross-shaped plant is not automatically a flower");
// **Drop either clause added above and one of these fails.** The second is
// written as a pair so it also fires if the halves ever stop alternating.
static_assert(isFlower(BlockId::WitherRose) && isFlower(BlockId::SunflowerLower) &&
                  isFlower(BlockId::PeonyLower),
              "the late-added flowers are still flowers");
static_assert(isFlower(BlockId::SunflowerLower) != isFlower(BlockId::SunflowerUpper),
              "a tall flower is one plant, and its lower half is the one every other rule names");
// **The pair, so the exception cannot be widened into `isAzalea` by accident.**
// Delete the `FloweringAzalea` clause and the first half fails; add the plain
// azalea to it and the second does.
static_assert(isFlower(BlockId::FloweringAzalea) && !isFlower(BlockId::Azalea),
              "the flowering one counts as a flower for pollination; the plain one does not");

/// Leaves of every wood type: alpha-tested like a plant, but a full cube.
constexpr bool isLeafBlock(BlockId id) {
    return id == BlockId::Leaves || id == BlockId::SpruceLeaves || id == BlockId::BirchLeaves ||
           id == BlockId::JungleLeaves || id == BlockId::AcaciaLeaves ||
           id == BlockId::DarkOakLeaves || id == BlockId::CherryLeaves ||
           id == BlockId::MangroveLeaves || id == BlockId::AzaleaLeaves ||
           id == BlockId::FloweringAzaleaLeaves;
}

/// Every log that has already been stripped. Named rather than written as a
/// range: the five added later sit at the tail of the enum, because a table row
/// carries its sprite index by hand.
constexpr bool isStrippedLog(BlockId id) {
    return (id >= BlockId::StrippedOakLog && id <= BlockId::StrippedDarkOakLog) ||
           id == BlockId::StrippedCherryLog || id == BlockId::StrippedMangroveLog ||
           id == BlockId::StrippedCrimsonStem || id == BlockId::StrippedWarpedStem ||
           id == BlockId::StrippedBambooBlock;
}

/// The bark blocks - eight woods and two hyphae - which wear their side texture
/// on all six faces where a log wears end grain on two.
///
/// **Their stripped forms are the ten ids declared straight after them, in the
/// same order**, which is what lets `strippedFor` answer them with one addition
/// instead of ten `case` labels. `kBarkRunLength` is that distance, and the
/// assert beside `strippedFor` is what holds the two runs together.
constexpr int kBarkRunLength = 10;

constexpr bool isBarkBlock(BlockId id) {
    return id >= BlockId::OakWood && id <= BlockId::WarpedHyphae;
}

constexpr bool isStrippedBarkBlock(BlockId id) {
    return id >= BlockId::StrippedOakWood && id <= BlockId::StrippedWarpedHyphae;
}

static_assert(static_cast<int>(BlockId::WarpedHyphae) - static_cast<int>(BlockId::OakWood) + 1 ==
                  kBarkRunLength,
              "ten bark blocks");
// **Insert one enumerator between the two runs and this fails**, which is the
// single edit that would otherwise turn an axe on oak wood into stripped spruce.
static_assert(static_cast<int>(BlockId::StrippedOakWood) - static_cast<int>(BlockId::OakWood) ==
                  kBarkRunLength,
              "the stripped run must start exactly where the plain run ends");
static_assert(static_cast<int>(BlockId::StrippedWarpedHyphae) -
                  static_cast<int>(BlockId::WarpedHyphae) == kBarkRunLength,
              "and the two runs must stay the same length and order");

/// Every log, stripped or not - the blocks whose end grain differs from their
/// bark, and the set an axe is for.
constexpr bool isLogBlock(BlockId id) {
    return id == BlockId::Log || id == BlockId::SpruceLog || id == BlockId::BirchLog ||
           id == BlockId::JungleLog || id == BlockId::AcaciaLog || id == BlockId::DarkOakLog ||
           id == BlockId::CherryLog || id == BlockId::MangroveLog ||
           id == BlockId::CrimsonStem || id == BlockId::WarpedStem ||
           id == BlockId::BambooBlock || isStrippedLog(id);
}

/// Which way a side face points.
///
/// `Unknown` is for callers with no direction to give - an item icon has no
/// place in the world, and most blocks look the same all the way round anyway.
enum class FaceDirection : std::int8_t {
    Unknown = -1,
    PosX = 0,
    NegX = 1,
    PosZ = 2,
    NegZ = 3,
};

/// A quarter turn about the vertical. `Unknown` stays unknown, which is what
/// keeps every faceless block's icon exactly as it was.
///
/// **Declared here, beside the enum it turns**, rather than beside its first
/// caller - the door leaf needs it three hundred lines earlier than the icon
/// code does.
constexpr FaceDirection quarterTurn(FaceDirection direction) {
    switch (direction) {
    case FaceDirection::PosX:
        return FaceDirection::PosZ;
    case FaceDirection::PosZ:
        return FaceDirection::NegX;
    case FaceDirection::NegX:
        return FaceDirection::NegZ;
    case FaceDirection::NegZ:
        return FaceDirection::PosX;
    default:
        return FaceDirection::Unknown;
    }
}

constexpr FaceDirection oppositeDirection(FaceDirection direction) {
    return quarterTurn(quarterTurn(direction));
}

/// The two pumpkins, named by the way they face.
///
/// **Written because there was no way to say it.** Both runs store their four
/// facings in `FaceDirection`'s own order, so `blockFacing` reads one out with a
/// bare cast - and placement, having no constructor to call, had to copy that
/// arithmetic out of the table that owns it. That is a second derivation of the
/// same fact, one file away from the first, which is how a family drifts.
/// Declared here rather than beside `isCarvedPumpkin` only because
/// `FaceDirection` does not exist yet up there.
///
/// `Unknown` becomes `NegZ`, matching every other faceless placement in the
/// file: a block put down by something with no direction to give faces the
/// player's default.
constexpr BlockId carvedPumpkinAt(FaceDirection facing) {
    const int index = facing == FaceDirection::Unknown ? static_cast<int>(FaceDirection::NegZ)
                                                      : static_cast<int>(facing);
    return static_cast<BlockId>(static_cast<int>(BlockId::CarvedPumpkinFirst) + index);
}

constexpr BlockId jackOLanternAt(FaceDirection facing) {
    const int index = facing == FaceDirection::Unknown ? static_cast<int>(FaceDirection::NegZ)
                                                      : static_cast<int>(facing);
    return static_cast<BlockId>(static_cast<int>(BlockId::JackOLanternFirst) + index);
}

/// Which sides a neighbour-aware block reaches toward.
enum ConnectionBits : std::uint8_t {
    ConnectNorth = 1, // -Z
    ConnectEast = 2,  // +X
    ConnectSouth = 4, // +Z
    ConnectWest = 8,  // -X
    ConnectAll = 15,
};

// ---------------------------------------------------------------------------
// Redstone
//
// A signal is a number from 0 to 15. Sources make it, wire carries it and loses
// one per block, and a machine does something whenever what it sees is above
// zero - strength never changes *what* a machine does, only how far the wire
// carrying it reaches.
//
// Everything here is Bedrock's arrangement, which is markedly simpler than
// Java's and is the one worth copying: no quasi-connectivity, no scheduled-tick
// ordering to expose, and wire that stores a strength rather than a shape.
// ---------------------------------------------------------------------------

/// The strongest a signal can be, and the length of the longest wire run.
constexpr int kMaxSignal = 15;

/// Six faces, as an index. **The order is ours and is used by every six-way
/// redstone block**, so a piston, an observer, a dispenser and a lightning rod
/// all pack their facing the same way.
///
/// Down first because that is where a hopper, a dropper and a piston most often
/// point, and it makes `facing6 & 1` mean "vertical, pointing up".
enum Facing6 : std::uint8_t {
    Facing6Down = 0,
    Facing6Up = 1,
    Facing6North = 2, // -Z
    Facing6South = 3, // +Z
    Facing6West = 4,  // -X
    Facing6East = 5,
};

/// **Block.hpp must never gain a glm include** - that is a documented build
/// break - so a direction hands back its three components one at a time.
constexpr int facing6Dx(int facing) { return facing == Facing6West ? -1 : (facing == Facing6East ? 1 : 0); }
constexpr int facing6Dy(int facing) { return facing == Facing6Down ? -1 : (facing == Facing6Up ? 1 : 0); }
constexpr int facing6Dz(int facing) {
    return facing == Facing6North ? -1 : (facing == Facing6South ? 1 : 0);
}

constexpr int oppositeFacing6(int facing) { return facing ^ 1; }
/// The same six directions written as a `FaceDirection`, or `Unknown` for the
/// two that have no horizontal sense.
constexpr FaceDirection facing6AsDirection(int facing) {
    switch (facing) {
    case Facing6East:
        return FaceDirection::PosX;
    case Facing6West:
        return FaceDirection::NegX;
    case Facing6South:
        return FaceDirection::PosZ;
    case Facing6North:
        return FaceDirection::NegZ;
    default:
        return FaceDirection::Unknown;
    }
}

constexpr int directionAsFacing6(FaceDirection direction) {
    switch (direction) {
    case FaceDirection::PosX:
        return Facing6East;
    case FaceDirection::NegX:
        return Facing6West;
    case FaceDirection::PosZ:
        return Facing6South;
    default:
        return Facing6North;
    }
}
/// Whether a set of three step functions describes these six directions.
///
/// **Takes the three as parameters so a deliberately wrong set can be run
/// through the same body** - a check that cannot fail is not evidence.
constexpr bool facing6StepsAgree(int (*dx)(int), int (*dy)(int), int (*dz)(int)) {
    for (int f = 0; f < 6; ++f) {
        const int x = dx(f);
        const int y = dy(f);
        const int z = dz(f);
        if (x * x + y * y + z * z != 1) {
            return false; // exactly one axis, exactly one cell
        }
        const int back = oppositeFacing6(f);
        if (dx(back) != -x || dy(back) != -y || dz(back) != -z) {
            return false; // `facing ^ 1` must really be the reverse step
        }
        // And the horizontal four must step the way the live conversion names
        // them, so the two tables cannot drift apart.
        switch (facing6AsDirection(f)) {
        case FaceDirection::PosX:
            if (x != 1) { return false; }
            break;
        case FaceDirection::NegX:
            if (x != -1) { return false; }
            break;
        case FaceDirection::PosZ:
            if (z != 1) { return false; }
            break;
        case FaceDirection::NegZ:
            if (z != -1) { return false; }
            break;
        default:
            if (y == 0) {
                return false; // the two with no horizontal sense are the vertical pair
            }
            break;
        }
    }
    return true;
}

// **Nothing calls these three today.** Checked 2026-08-19 by a bare-name sweep
// over 141 files with comments, strings and char literals stripped: one site
// each, their own definitions. What would make that stale is the first machine
// that moves something - a piston that pushes, a dispenser that dispenses, an
// observer that pulses - because **no consumer anywhere reads a six-way facing
// back out of a block yet**, which is why these are unused rather than
// redundant. They stay because they are the only place in the tree that turns a
// `Facing6` into a step, and the alternative is that the first caller writes the
// ternary inline: a value derived somewhere other than the table that owns it.
//
// **What a dead table must not do is rot quietly.** `Facing6`'s order is ours by
// its own comment and could be reordered by someone who checks the live users
// and finds these three invisible, so they are pinned here to the two live
// neighbours that would move with them. After this, reordering the enum breaks a
// build rather than a piston.
static_assert(facing6StepsAgree(facing6Dx, facing6Dy, facing6Dz),
              "the six-way steps disagree with the order of Facing6 or with facing6AsDirection");

// **CONTROL: the same three with X and Z swapped.** That is the specific mistake
// available here - `Facing6` lists north and south before west and east, so
// reading the enum's order straight into the steps produces exactly this, and it
// survives both the unit-step test and the opposite test above.
static_assert(!facing6StepsAgree(facing6Dz, facing6Dy, facing6Dx),
              "the step check must reject an axis swap, or it is proving only that three "
              "functions return small numbers");


// ---------------------------------------------------------------------------
// **Redstone components, and the 422 ids of them that nothing can reach.**
//
// Every state accessor from here down to the rails is correct, is tested, and
// is **dormant by design**. `wireSignal`, `redstoneTorchLit`, `leverOn`,
// `buttonPressed`, `pressurePlateSignal`, `repeaterDelay`, `repeaterPowered`,
// `repeaterLocked`, `comparatorPowered`, `comparatorSubtracts`,
// `pistonExtended`, `observerPowered`, `daylightDetectorSignal`,
// `targetSignal`, `lightningRodPowered`, `tripwireHookAttached`,
// `tripwireHookPowered`, `tripwireAttached`, `tripwirePowered` and
// `railPowered` each decode a field packed into the id, and for most of them
// nothing in the game ever writes anything but the default value.
//
// **The cause is a settled decision, not an oversight**: there is no redstone
// signal engine and there is not going to be one until it is asked for -
// `DECISIONS.md`, "No redstone signal engine, and it is a decision rather than
// a gap". What a component owes is its own state, its own picture and its own
// sound, and a hand supplies the change. Six toggles are live: a lever flips, a
// button springs back on a timer, a repeater steps its delay, a comparator
// swaps mode, a daylight detector inverts and a note block retunes. Nothing
// else moves.
//
// **Measured rather than estimated.** 679 ids answer `isRedstoneComponent`, and
// 422 of them cannot be produced by placement, by a toggle, by worldgen or by a
// drop:
//
// * **210 pressure plates** - fourteen materials times the fifteen pressed
//   strengths. Nothing detects what stands on one, so only `signal == 0` is
//   ever written, and the two weighted plates never count anything.
// * **48 repeaters** powered or locked, **30 daylight detectors** showing any
//   level above zero, **15 wire strengths**, **15 struck target strengths**,
//   **12 extended pistons**, **12 piston heads** (the whole run - an extending
//   piston is the only thing that makes one, and it drops nothing so it is not
//   in the catalogue), **12 attached or powered tripwire hooks**, **8 powered
//   comparators**, **6 powered observers**, **6 powered lightning rods**,
//   **5 unlit redstone torches**, **4 tripwires** (the whole run - a tripwire
//   gives back string, and string is not a block item), **1 lit redstone lamp**.
// * **38 rails**, of which 18 are the powered halves. **The other 20 are a
//   different divergence** and not this one: nothing solves track shape, so
//   placement lays straight flat track and the four slopes and four curves are
//   unreachable for reasons that have nothing to do with signals.
//
// **What would make them live is one system**: a propagation pass that walks
// power from the sources through wire and repeaters and writes the result back
// with `setBlock`. Every accessor below is its read half and every `...At`
// builder is its write half, so that pass needs no new ids and no new state -
// which is exactly why none of this is deleted. An accessor here that looks
// unused is not broken and must not be "fixed" away.
// ---------------------------------------------------------------------------

/// Redstone wire, one id per strength.
constexpr bool isRedstoneWire(BlockId id) {
    return id >= BlockId::RedstoneWireFirst && id <= BlockId::RedstoneWireLast;
}

constexpr int wireSignal(BlockId id) {
    return static_cast<int>(id) - static_cast<int>(BlockId::RedstoneWireFirst);
}

constexpr BlockId wireWithSignal(int signal) {
    const int clamped = signal < 0 ? 0 : (signal > kMaxSignal ? kMaxSignal : signal);
    return static_cast<BlockId>(static_cast<int>(BlockId::RedstoneWireFirst) + clamped);
}

/// A redstone torch in any of its ten states.
///
/// **Three ranges rather than one**, because the lit floor torch is
/// `BlockId::RedstoneTorch` from the third table run and is already written
/// into saved chunks. Moving it to sit beside the other nine would have made
/// every existing world come back with something else in its place.
constexpr bool isRedstoneTorch(BlockId id) {
    return id == BlockId::RedstoneTorch || id == BlockId::RedstoneTorchOff ||
           (id >= BlockId::RedstoneTorchWallFirst && id <= BlockId::RedstoneTorchOffWallLast);
}

constexpr bool redstoneTorchLit(BlockId id) {
    return id == BlockId::RedstoneTorch ||
           (id >= BlockId::RedstoneTorchWallFirst && id <= BlockId::RedstoneTorchWallLast);
}

/// Which wall it hangs on, or `Unknown` when it stands on the floor.
constexpr FaceDirection redstoneTorchWall(BlockId id) {
    if (id >= BlockId::RedstoneTorchWallFirst && id <= BlockId::RedstoneTorchWallLast) {
        return static_cast<FaceDirection>(static_cast<int>(id) -
                                          static_cast<int>(BlockId::RedstoneTorchWallFirst));
    }
    if (id >= BlockId::RedstoneTorchOffWallFirst && id <= BlockId::RedstoneTorchOffWallLast) {
        return static_cast<FaceDirection>(static_cast<int>(id) -
                                          static_cast<int>(BlockId::RedstoneTorchOffWallFirst));
    }
    return FaceDirection::Unknown;
}

/// The single owner of putting a torch's two facts back together. Writing
/// `lit ? RedstoneTorch : RedstoneTorchOff` at a call site is what turns a wall
/// torch into a floor one the moment it is toggled.
constexpr BlockId redstoneTorchAt(FaceDirection wall, bool lit) {
    if (wall == FaceDirection::Unknown) {
        return lit ? BlockId::RedstoneTorch : BlockId::RedstoneTorchOff;
    }
    const int base = static_cast<int>(lit ? BlockId::RedstoneTorchWallFirst
                                          : BlockId::RedstoneTorchOffWallFirst);
    return static_cast<BlockId>(base + static_cast<int>(wall));
}

/// A lever, in any of its sixteen states.
constexpr bool isLever(BlockId id) {
    return id >= BlockId::LeverRunFirst && id <= BlockId::LeverRunLast;
}

/// Where a lever or button hangs, and for the floor and ceiling which way round.
enum LeverMount : std::uint8_t {
    /// On the floor, handle throwing along X.
    LeverFloorX = 0,
    /// On the floor, handle throwing along Z.
    LeverFloorZ = 1,
    LeverCeilingX = 2,
    LeverCeilingZ = 3,
    /// Against the wall to the +X, -X, +Z, -Z - the `FaceDirection` order,
    /// offset by four so the two families share one field.
    LeverWallFirst = 4,
};

constexpr int leverMount(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::LeverRunFirst)) & 7;
}

constexpr bool leverOn(BlockId id) {
    return ((static_cast<int>(id) - static_cast<int>(BlockId::LeverRunFirst)) & 8) != 0;
}

constexpr BlockId leverAt(int mount, bool on) {
    return static_cast<BlockId>(static_cast<int>(BlockId::LeverRunFirst) + (on ? 8 : 0) + mount);
}

/// A button of any material.
constexpr bool isButton(BlockId id) {
    return id >= BlockId::ButtonRunFirst && id <= BlockId::ButtonRunLast;
}

constexpr int buttonFamily(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::ButtonRunFirst)) / 12;
}

/// Floor, ceiling, then the four walls in `FaceDirection` order.
constexpr int buttonMount(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::ButtonRunFirst)) % 6;
}

constexpr bool buttonPressed(BlockId id) {
    return ((static_cast<int>(id) - static_cast<int>(BlockId::ButtonRunFirst)) % 12) >= 6;
}

constexpr BlockId buttonAt(int family, int mount, bool pressed) {
    return static_cast<BlockId>(static_cast<int>(BlockId::ButtonRunFirst) + family * 12 +
                                (pressed ? 6 : 0) + mount);
}

/// A pressure plate of any material.
constexpr bool isPressurePlate(BlockId id) {
    return id >= BlockId::PressurePlateRunFirst && id <= BlockId::PressurePlateRunLast;
}

constexpr int pressurePlateFamily(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::PressurePlateRunFirst)) / 16;
}

constexpr int pressurePlateSignal(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::PressurePlateRunFirst)) % 16;
}

constexpr BlockId pressurePlateAt(int family, int signal) {
    const int clamped = signal < 0 ? 0 : (signal > kMaxSignal ? kMaxSignal : signal);
    return static_cast<BlockId>(static_cast<int>(BlockId::PressurePlateRunFirst) + family * 16 +
                                clamped);
}

/// The two plates that count what stands on them rather than answering yes.
/// They are the last two families, so this is one comparison.
constexpr bool isWeightedPlate(BlockId id) {
    return isPressurePlate(id) && pressurePlateFamily(id) >= kPressurePlateFamilyCount - 2;
}

/// Only wood answers to a dropped item or a loose arrow; stone wants something
/// that walks. Wood is families 0-10, stone is 11.
constexpr bool plateAnswersToItems(BlockId id) {
    return isPressurePlate(id) && pressurePlateFamily(id) != 11;
}

constexpr bool isRepeater(BlockId id) {
    return id >= BlockId::RepeaterRunFirst && id <= BlockId::RepeaterRunLast;
}

/// Which way the signal **leaves**. The reference stores the opposite and every
/// description of it has to say so twice; storing the useful end means the
/// block in front is `facing` and the block behind is `opposite`.
constexpr FaceDirection repeaterFacing(BlockId id) {
    return static_cast<FaceDirection>((static_cast<int>(id) -
                                       static_cast<int>(BlockId::RepeaterRunFirst)) &
                                      3);
}

/// One to four, as shown on the block. The stored value is one less.
constexpr int repeaterDelay(BlockId id) {
    return (((static_cast<int>(id) - static_cast<int>(BlockId::RepeaterRunFirst)) >> 2) & 3) + 1;
}

constexpr bool repeaterPowered(BlockId id) {
    return (((static_cast<int>(id) - static_cast<int>(BlockId::RepeaterRunFirst)) >> 4) & 1) != 0;
}

constexpr bool repeaterLocked(BlockId id) {
    return (((static_cast<int>(id) - static_cast<int>(BlockId::RepeaterRunFirst)) >> 5) & 1) != 0;
}

constexpr BlockId repeaterAt(FaceDirection facing, int delay, bool powered, bool locked) {
    return static_cast<BlockId>(static_cast<int>(BlockId::RepeaterRunFirst) +
                                (locked ? 32 : 0) + (powered ? 16 : 0) + ((delay - 1) << 2) +
                                static_cast<int>(facing));
}

constexpr bool isComparator(BlockId id) {
    return id >= BlockId::ComparatorRunFirst && id <= BlockId::ComparatorRunLast;
}

constexpr FaceDirection comparatorFacing(BlockId id) {
    return static_cast<FaceDirection>((static_cast<int>(id) -
                                       static_cast<int>(BlockId::ComparatorRunFirst)) &
                                      3);
}

constexpr bool comparatorPowered(BlockId id) {
    return (((static_cast<int>(id) - static_cast<int>(BlockId::ComparatorRunFirst)) >> 2) & 1) != 0;
}

/// Subtract mode - the front torch stands up and lit. The other mode compares.
constexpr bool comparatorSubtracts(BlockId id) {
    return (((static_cast<int>(id) - static_cast<int>(BlockId::ComparatorRunFirst)) >> 3) & 1) != 0;
}

constexpr BlockId comparatorAt(FaceDirection facing, bool powered, bool subtract) {
    return static_cast<BlockId>(static_cast<int>(BlockId::ComparatorRunFirst) +
                                (subtract ? 8 : 0) + (powered ? 4 : 0) +
                                static_cast<int>(facing));
}

constexpr bool isPiston(BlockId id) {
    return id >= BlockId::PistonRunFirst && id <= BlockId::PistonRunLast;
}

constexpr int pistonFacing(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::PistonRunFirst)) % 6;
}

constexpr bool pistonExtended(BlockId id) {
    return (((static_cast<int>(id) - static_cast<int>(BlockId::PistonRunFirst)) / 6) & 1) != 0;
}

constexpr bool pistonSticky(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::PistonRunFirst)) >= 12;
}

constexpr BlockId pistonAt(int facing, bool extended, bool sticky) {
    return static_cast<BlockId>(static_cast<int>(BlockId::PistonRunFirst) + (sticky ? 12 : 0) +
                                (extended ? 6 : 0) + facing);
}

constexpr bool isPistonHead(BlockId id) {
    return id >= BlockId::PistonHeadRunFirst && id <= BlockId::PistonHeadRunLast;
}

constexpr int pistonHeadFacing(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::PistonHeadRunFirst)) % 6;
}

constexpr bool pistonHeadSticky(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::PistonHeadRunFirst)) >= 6;
}

constexpr BlockId pistonHeadAt(int facing, bool sticky) {
    return static_cast<BlockId>(static_cast<int>(BlockId::PistonHeadRunFirst) + (sticky ? 6 : 0) +
                                facing);
}

constexpr bool isObserver(BlockId id) {
    return id >= BlockId::ObserverRunFirst && id <= BlockId::ObserverRunLast;
}

/// The face it **watches**. Its pulse comes out of the opposite side, which is
/// the one convention here that is worth restating every time it is used.
constexpr int observerFacing(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::ObserverRunFirst)) % 6;
}

constexpr bool observerPowered(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::ObserverRunFirst)) >= 6;
}

constexpr BlockId observerAt(int facing, bool powered) {
    return static_cast<BlockId>(static_cast<int>(BlockId::ObserverRunFirst) + (powered ? 6 : 0) +
                                facing);
}

constexpr bool isDispenser(BlockId id) {
    return id >= BlockId::DispenserRunFirst && id <= BlockId::DispenserRunLast;
}

constexpr bool isDropper(BlockId id) {
    return id >= BlockId::DropperRunFirst && id <= BlockId::DropperRunLast;
}

/// Either of the two nine-slot machines that fire on a rising edge. They differ
/// only in what they do with the item, which is why almost everything about
/// them - the screen, the block entity, the facing, the spill on break - is one
/// code path asking this.
constexpr bool isDispenserLike(BlockId id) { return isDispenser(id) || isDropper(id); }

constexpr int dispenserFacing(BlockId id) {
    return isDropper(id) ? static_cast<int>(id) - static_cast<int>(BlockId::DropperRunFirst)
                         : static_cast<int>(id) - static_cast<int>(BlockId::DispenserRunFirst);
}

constexpr BlockId dispenserAt(int facing, bool dropper) {
    return static_cast<BlockId>(static_cast<int>(dropper ? BlockId::DropperRunFirst
                                                         : BlockId::DispenserRunFirst) +
                                facing);
}

constexpr bool isDaylightDetector(BlockId id) {
    return id >= BlockId::DaylightDetectorRunFirst && id <= BlockId::DaylightDetectorRunLast;
}

constexpr int daylightDetectorSignal(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::DaylightDetectorRunFirst)) & 15;
}

constexpr bool daylightDetectorInverted(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::DaylightDetectorRunFirst)) >= 16;
}

constexpr BlockId daylightDetectorAt(int signal, bool inverted) {
    const int clamped = signal < 0 ? 0 : (signal > kMaxSignal ? kMaxSignal : signal);
    return static_cast<BlockId>(static_cast<int>(BlockId::DaylightDetectorRunFirst) +
                                (inverted ? 16 : 0) + clamped);
}

/// A target block, struck or quiet. **Two ranges** - strength zero is the
/// decorative `Target` that already sits in the fifth table run and on disk.
constexpr bool isTarget(BlockId id) {
    return id == BlockId::Target ||
           (id >= BlockId::TargetRunFirst && id <= BlockId::TargetRunLast);
}

constexpr int targetSignal(BlockId id) {
    return id == BlockId::Target
               ? 0
               : static_cast<int>(id) - static_cast<int>(BlockId::TargetRunFirst) + 1;
}

constexpr BlockId targetAt(int signal) {
    const int clamped = signal < 1 ? 0 : (signal > kMaxSignal ? kMaxSignal : signal);
    return clamped == 0
               ? BlockId::Target
               : static_cast<BlockId>(static_cast<int>(BlockId::TargetRunFirst) + clamped - 1);
}

/// A note block at any of its twenty-five pitches. **The instrument is not in
/// here** - it is read off whatever the block is standing on, so retuning a
/// note block never has to know what it is sitting above.
constexpr bool isNoteBlock(BlockId id) {
    return id == BlockId::NoteBlock ||
           (id >= BlockId::NoteBlockRunFirst && id <= BlockId::NoteBlockRunLast);
}

constexpr int noteBlockPitch(BlockId id) {
    return id == BlockId::NoteBlock
               ? 0
               : static_cast<int>(id) - static_cast<int>(BlockId::NoteBlockRunFirst) + 1;
}

constexpr BlockId noteBlockAt(int pitch) {
    const int wrapped = ((pitch % 25) + 25) % 25;
    return wrapped == 0
               ? BlockId::NoteBlock
               : static_cast<BlockId>(static_cast<int>(BlockId::NoteBlockRunFirst) + wrapped - 1);
}

constexpr bool isLightningRod(BlockId id) {
    return id >= BlockId::LightningRodRunFirst && id <= BlockId::LightningRodRunLast;
}

/// **Which way the rod points**, as a `Facing6` - away from whatever holds it,
/// so a rod stood on the ground points up. Same convention as
/// `tripwireHookFacing` below: neither of them stores its support.
///
/// **Its only writer disagrees with itself, and the vertical half is
/// inverted.** The placement site stores `oppositeDirection(wall)` for a rod on
/// a wall, which is away from the support and right; for a vertical one it
/// stores `Facing6Up` when `into.y > 0`, and `into` runs from the new cell *at*
/// the block that was clicked, so that is the ceiling case. A rod stood on the
/// ground points down into the floor and one under a ceiling points up into it.
/// The fix belongs to the writer, and nothing here can assert across to it -
/// which is why this comment names the convention instead.
constexpr int lightningRodFacing(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::LightningRodRunFirst)) % 6;
}

constexpr bool lightningRodPowered(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::LightningRodRunFirst)) >= 6;
}

constexpr BlockId lightningRodAt(int facing, bool powered) {
    return static_cast<BlockId>(static_cast<int>(BlockId::LightningRodRunFirst) +
                                (powered ? 6 : 0) + facing);
}

constexpr bool isTripwireHook(BlockId id) {
    return id >= BlockId::TripwireHookRunFirst && id <= BlockId::TripwireHookRunLast;
}

/// **The direction the wire runs**, away from the wall the hook is screwed to -
/// so the wall is `oppositeDirection` of this, not this.
///
/// An earlier note here had it the other way round. The single writer settles
/// it: the placement site stores `oppositeDirection(wall)`, and `wall` is read
/// off `into = target.block - placeCell`, which runs from the new cell *at* the
/// block that was clicked. `wall` therefore already points at the support, and
/// what is stored points away from it.
constexpr FaceDirection tripwireHookFacing(BlockId id) {
    return static_cast<FaceDirection>((static_cast<int>(id) -
                                       static_cast<int>(BlockId::TripwireHookRunFirst)) &
                                      3);
}

constexpr bool tripwireHookAttached(BlockId id) {
    return (((static_cast<int>(id) - static_cast<int>(BlockId::TripwireHookRunFirst)) >> 2) & 1) !=
           0;
}

constexpr bool tripwireHookPowered(BlockId id) {
    return (((static_cast<int>(id) - static_cast<int>(BlockId::TripwireHookRunFirst)) >> 3) & 1) !=
           0;
}

constexpr BlockId tripwireHookAt(FaceDirection facing, bool attached, bool powered) {
    return static_cast<BlockId>(static_cast<int>(BlockId::TripwireHookRunFirst) +
                                (powered ? 8 : 0) + (attached ? 4 : 0) + static_cast<int>(facing));
}

constexpr bool isTripwire(BlockId id) {
    return id >= BlockId::TripwireRunFirst && id <= BlockId::TripwireRunLast;
}

constexpr bool tripwireAttached(BlockId id) {
    return ((static_cast<int>(id) - static_cast<int>(BlockId::TripwireRunFirst)) & 1) != 0;
}

constexpr bool tripwirePowered(BlockId id) {
    return ((static_cast<int>(id) - static_cast<int>(BlockId::TripwireRunFirst)) & 2) != 0;
}

constexpr BlockId tripwireAt(bool attached, bool powered) {
    return static_cast<BlockId>(static_cast<int>(BlockId::TripwireRunFirst) + (powered ? 2 : 0) +
                                (attached ? 1 : 0));
}

// **The enum comments for these runs are now checked rather than trusted.**
// Three of them claimed a bit-packed `sticky<<4 | extended<<3 | facing` for runs
// that are plainly base-six arithmetic: a shift layout needs 32 ids for a piston
// run that is 24 long, and it puts a sticky piston at offset 16 where
// `pistonSticky` looks for 12. Rewrite any of the three as shifts and the first
// three of these fail - the last piston lands past `PistonRunLast`, and an
// extended plain piston collides with a retracted sticky one.
static_assert(pistonAt(5, true, true) == BlockId::PistonRunLast,
              "twenty-four ids is two by two by six, not a five-bit pack");
static_assert(pistonSticky(pistonAt(0, false, true)) && !pistonExtended(pistonAt(0, false, true)) &&
                  pistonFacing(pistonAt(4, true, false)) == 4 &&
                  pistonExtended(pistonAt(4, true, false)) &&
                  !pistonSticky(pistonAt(4, true, false)),
              "the three fields must survive a round trip on both sides of the boundary");
static_assert(observerAt(5, true) == BlockId::ObserverRunLast &&
                  lightningRodAt(5, true) == BlockId::LightningRodRunLast,
              "twelve ids is two by six");
// And the counterweight, so the correction is not over-applied: the four-facing
// runs beside them **are** genuine bit-packs and their comments stay as written.
static_assert(tripwireHookAt(FaceDirection::NegZ, true, true) == BlockId::TripwireHookRunLast,
              "sixteen ids over four facings is a real two-bit pack");

constexpr bool isRedstoneLamp(BlockId id) {
    return id == BlockId::RedstoneLamp || id == BlockId::RedstoneLampLit;
}

/// Which of the four rail families this is, or -1 for anything else. Ordering
/// matters and is the same everywhere: plain, powered, detector, activator.
constexpr int railFamily(BlockId id) {
    if (id >= BlockId::RailRunFirst && id <= BlockId::RailRunLast) {
        return 0;
    }
    if (id >= BlockId::PoweredRailRunFirst && id <= BlockId::PoweredRailRunLast) {
        return 1;
    }
    if (id >= BlockId::DetectorRailRunFirst && id <= BlockId::DetectorRailRunLast) {
        return 2;
    }
    if (id >= BlockId::ActivatorRailRunFirst && id <= BlockId::ActivatorRailRunLast) {
        return 3;
    }
    return -1;
}

constexpr bool isRail(BlockId id) { return railFamily(id) >= 0; }

/// Which of the first ids in the four runs a family starts at.
constexpr BlockId railRunFirst(int family) {
    switch (family) {
    case 1:
        return BlockId::PoweredRailRunFirst;
    case 2:
        return BlockId::DetectorRailRunFirst;
    case 3:
        return BlockId::ActivatorRailRunFirst;
    default:
        return BlockId::RailRunFirst;
    }
}

/// Track shape. 0/1 are the two flat runs, 2-5 the four slopes, 6-9 the four
/// curves - **and only the plain rail may curve**, which is why the other three
/// families are six shapes wide rather than ten.
constexpr int railShape(BlockId id) {
    const int family = railFamily(id);
    const int offset = static_cast<int>(id) - static_cast<int>(railRunFirst(family));
    return family == 0 ? offset : offset % 6;
}

constexpr bool railPowered(BlockId id) {
    const int family = railFamily(id);
    if (family <= 0) {
        return false;
    }
    return (static_cast<int>(id) - static_cast<int>(railRunFirst(family))) >= 6;
}

constexpr BlockId railAt(int family, int shape, bool powered) {
    if (family == 0) {
        return static_cast<BlockId>(static_cast<int>(BlockId::RailRunFirst) + shape);
    }
    const int flat = shape > 5 ? 0 : shape;
    return static_cast<BlockId>(static_cast<int>(railRunFirst(family)) + (powered ? 6 : 0) + flat);
}

/// Whether a rail shape climbs, and toward which of the four horizontal sides.
/// Shapes two to five are the slopes, in `FaceDirection` order.
constexpr bool railSlopes(int shape) { return shape >= 2 && shape <= 5; }
constexpr FaceDirection railSlopeToward(int shape) {
    return railSlopes(shape) ? static_cast<FaceDirection>(shape - 2) : FaceDirection::Unknown;
}

/// Whether the track runs east-west rather than north-south.
///
/// **A slope is a straight run seen from the side**, so ascending east and
/// ascending west lie along X exactly as the flat east-west run does, and the
/// direction is asked for rather than written out - `railSlopeToward` owns the
/// slope numbering and this must not become a second copy of it.
constexpr bool railRunsAlongX(int shape) {
    if (shape == 1) {
        return true;
    }
    const FaceDirection climb = railSlopeToward(shape);
    return climb == FaceDirection::PosX || climb == FaceDirection::NegX;
}

/// Quarter turns of the rail sprite in its own cell.
///
/// **The reference states this as a `y` rotation per shape** in
/// `blockstates/rail.json`: north-south takes none and east-west takes 90, and
/// the four corners take 0, 90, 180 and 270 for south-east, south-west,
/// north-west and north-east in that order. `Main.cpp` numbers the curves 6, 7,
/// 8, 9 in exactly that order, so the curve turn is the offset and no table is
/// needed - but that also means this and the solver have to keep agreeing, which
/// is what the 180-degree assert below is for.
///
/// **Without this every straight rail was pixel-identical.** Shapes 0 and 1
/// differ only by this turn, so a player could not tell which way any rail ran.
constexpr int railUvTurns(int shape) {
    if (shape >= 6 && shape <= 9) {
        return shape - 6;
    }
    return railRunsAlongX(shape) ? 1 : 0;
}

/// **The plane a rail is drawn on, and the height a slope climbs to.**
/// `rail_flat.json` is a zero-thickness element at y = 1, and
/// `template_rail_raised_ne.json` is that same plane at y = 9 turned 45 degrees
/// about X with `rescale: true`, which carries its two edges to y = 1 and
/// y = 17. So a slope leaves the floor of its own cell one texel up and arrives
/// one texel above the ceiling - which is exactly where the flat rail in the
/// cell it climbs to is drawn.
constexpr float kRailPlaneY = 1.0f / 16.0f;
constexpr float kRailSlopeTopY = 17.0f / 16.0f;
static_assert(kRailSlopeTopY - kRailPlaneY == 1.0f,
              "a slope has to rise exactly one cell, or its top edge cannot meet the rail it "
              "climbs to and a ramp shows a step at every join");

/// The height of one corner of a rail's quad, for a corner at `cornerX` and
/// `cornerZ` in {0, 1}. **The raised edge is the one the shape climbs toward**,
/// read out of the direction rather than written out once per shape.
constexpr float railCornerY(int shape, int cornerX, int cornerZ) {
    if (!railSlopes(shape)) {
        return kRailPlaneY;
    }
    const int facing = directionAsFacing6(railSlopeToward(shape));
    const int dx = facing6Dx(facing);
    const int dz = facing6Dz(facing);
    const bool high = (dx > 0 && cornerX == 1) || (dx < 0 && cornerX == 0) ||
                      (dz > 0 && cornerZ == 1) || (dz < 0 && cornerZ == 0);
    return high ? kRailSlopeTopY : kRailPlaneY;
}

/// Whether a slope raises exactly the two corners on the side it climbs toward,
/// and raises them to `expectedTop`.
///
/// **The "which corner is high" test here is deliberately a different
/// expression from the one in `railCornerY`** - a sign of a dot product against
/// the climb direction, rather than a chain of four comparisons. An assert that
/// restates the function it checks proves nothing, and eleven asserts in this
/// file once did exactly that and passed while pointing at the wrong texture.
///
/// **Parameterised on the top height so a wrong one can be shown failing.**
constexpr bool railSlopeRisesTo(int shape, float expectedTop) {
    if (!railSlopes(shape)) {
        return false;
    }
    const int facing = directionAsFacing6(railSlopeToward(shape));
    int high = 0;
    for (int cornerX = 0; cornerX <= 1; ++cornerX) {
        for (int cornerZ = 0; cornerZ <= 1; ++cornerZ) {
            const bool toward = (2 * cornerX - 1) * facing6Dx(facing) +
                                    (2 * cornerZ - 1) * facing6Dz(facing) >
                                0;
            const float y = railCornerY(shape, cornerX, cornerZ);
            if (toward) {
                if (y != expectedTop) {
                    return false;
                }
                ++high;
            } else if (y != kRailPlaneY) {
                return false;
            }
        }
    }
    return high == 2;
}
static_assert(railSlopeRisesTo(2, kRailSlopeTopY) && railSlopeRisesTo(3, kRailSlopeTopY) &&
                  railSlopeRisesTo(4, kRailSlopeTopY) && railSlopeRisesTo(5, kRailSlopeTopY),
              "all four slopes raise the two corners on the side they climb toward, to the height "
              "of the rail in the cell above");
static_assert(!railSlopeRisesTo(2, kRailPlaneY + 0.5f),
              "the height check has to be able to say no - a half-height ramp must fail it, or the "
              "four asserts above would pass against any geometry at all");
static_assert(!railSlopeRisesTo(0, kRailSlopeTopY) && !railSlopeRisesTo(1, kRailSlopeTopY),
              "a flat run is not a slope, so nothing may raise its corners");

static_assert(railUvTurns(0) != railUvTurns(1),
              "north-south and east-west shared a turn, which is exactly why every straight rail "
              "was pixel-identical and a player could not tell which way one ran");
static_assert(railUvTurns(2) == railUvTurns(1) && railUvTurns(3) == railUvTurns(1),
              "a slope along X turns like the east-west run it belongs to");
static_assert(railUvTurns(4) == railUvTurns(0) && railUvTurns(5) == railUvTurns(0),
              "and a slope along Z like the north-south run");
/// **Opposite corners must sit half a turn apart, whichever way a quarter turn
/// goes.** South-east against north-west, and south-west against north-east.
/// This holds under either sign convention, so it survives the one thing about
/// the turn direction that cannot be settled without looking at the screen.
static_assert(((railUvTurns(8) - railUvTurns(6)) & 3) == 2 &&
                  ((railUvTurns(9) - railUvTurns(7)) & 3) == 2,
              "the four curves are not a quarter turn apart in corner order, so this and the "
              "solver in Main.cpp have stopped agreeing about which curve is which");

/// Every shape names a turn a quad can actually take.
///
/// **`bias` exists only so the sweep can be shown failing.** The obvious
/// control - sweeping one shape past the end - does not work here and the
/// compiler said so: shape 10 falls through to the straight-run branch and
/// answers 0, which is a perfectly legal quarter, so the check passed and
/// proved nothing. Offsetting the answer instead drives the same loop over the
/// same reads and does put a turn out of range.
constexpr bool railTurnsAreQuarters(int bias, int lo, int hi) {
    for (int shape = lo; shape < hi; ++shape) {
        const int turns = railUvTurns(shape) + bias;
        if (turns < 0 || turns > 3) {
            return false;
        }
    }
    return true;
}
static_assert(railTurnsAreQuarters(0, 0, 10), "a rail asked for a turn that is not a quarter");
static_assert(!railTurnsAreQuarters(1, 0, 10),
              "one step of bias has to push the north-east curve's three out of range - if it does "
              "not, the sweep is not reading the turns at all");

/// Everything that carries or answers a signal, in one question.
///
/// **Its own predicate rather than a widening of an existing one.** The
/// temptation is to route redstone through `isOpaque`, and the two disagree on
/// about fifteen blocks - glass, slabs, leaves, the redstone block itself - so
/// sharing them produces exactly the class of bug that compiles, validates and
/// is only ever found by playing.
///
/// **The note block and the hopper are missing and must not simply be added.**
/// Both answer a signal in the reference, so the omission is real, but this
/// predicate has five callers across four files that use it to pick mining
/// behaviour, placement and item handling - widening a family predicate and
/// killing the early-outs already sitting behind it is this project's second
/// recorded failure shape. `Item.hpp` already patches the note block back in by
/// hand at its own call site, which is the duplication this should eventually
/// collapse; doing so means reading all five callers, not editing this line.
constexpr bool isRedstoneComponent(BlockId id) {
    return isRedstoneWire(id) || isRedstoneTorch(id) || isLever(id) || isButton(id) ||
           isPressurePlate(id) || isRepeater(id) || isComparator(id) || isPiston(id) ||
           isPistonHead(id) || isObserver(id) || isDispenserLike(id) || isDaylightDetector(id) ||
           isTarget(id) || isLightningRod(id) || isTripwireHook(id) || isTripwire(id) ||
           isRedstoneLamp(id) || isRail(id) || id == BlockId::RedstoneBlock;
}

/// Every torch. **A box standing in its cell, not two crossed sheets** — which
/// is what the reference draws and what stops the crosshair claiming the whole
/// cell one stands in.
///
/// **Declared here rather than three hundred lines up, beside the other
/// cross-shaped plants, because it has to be able to ask `isRedstoneTorch`.**
/// Writing the redstone torch's three id ranges out a second time would be the
/// project's own commonest bug — a value derived somewhere other than the one
/// place that owns it — and every one of this function's eleven callers sits
/// well below here.
constexpr bool isTorchBlock(BlockId id) {
    return id == BlockId::Torch || id == BlockId::SoulTorch || isRedstoneTorch(id);
}

/// A torch hanging off a wall rather than standing on the floor. Only the
/// redstone torch has one today; the plain and soul torches are floor-only.
constexpr bool isWallTorch(BlockId id) {
    return isTorchBlock(id) && redstoneTorchWall(id) != FaceDirection::Unknown;
}

// ---------------------------------------------------------------------------
// Signs, hanging signs and banners
//
// Three families, one shape of state. Each stands on the ground at any of
// **four** rotations or hangs off a wall at one of four, which is `kSignStates`
// = 8 and is what every accessor below actually arithmetic-decodes.
//
// The reference has sixteen standing rotations and this header claimed them
// long after ours was cut to four - the same wrong number appeared in the enum
// comment and above `signState`, all three describing a block whose ids had
// been half the size for milestones. Sixteen is the one to restore if a sign
// ever needs to face a diagonal path: it is a change to `kSignStates`, four
// more `signFacing` cases and a save-format bump, and nothing else here.
// ---------------------------------------------------------------------------

constexpr bool isSign(BlockId id) {
    return id >= BlockId::SignRunFirst && id <= BlockId::SignRunLast;
}

constexpr bool isHangingSign(BlockId id) {
    return id >= BlockId::HangingSignRunFirst && id <= BlockId::HangingSignRunLast;
}

constexpr bool isBanner(BlockId id) {
    return id >= BlockId::BannerRunFirst && id <= BlockId::BannerRunLast;
}

/// True for all three, which is how every rule they share is asked.
constexpr bool isSignLike(BlockId id) {
    return isSign(id) || isHangingSign(id) || isBanner(id);
}

/// 0 a sign, 1 a hanging sign, 2 a banner.
constexpr int signKind(BlockId id) {
    return isSign(id) ? 0 : (isHangingSign(id) ? 1 : 2);
}

constexpr BlockId signRunFirst(int kind) {
    switch (kind) {
    case 1:
        return BlockId::HangingSignRunFirst;
    case 2:
        return BlockId::BannerRunFirst;
    default:
        return BlockId::SignRunFirst;
    }
}

constexpr int signFamily(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(signRunFirst(signKind(id)))) / kSignStates;
}

/// Zero to three standing, four to seven against a wall - which is what
/// `signOnWall` reads and what `signAt` writes. **It said "zero to fifteen,
/// sixteen to nineteen" and nothing in the code ever did**: `kSignStates` is 8,
/// so a wall sign is `state >= 4`, and a reader who trusted the comment would
/// have taken every wall sign for a standing one.
constexpr int signState(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(signRunFirst(signKind(id)))) % kSignStates;
}

constexpr bool signOnWall(BlockId id) { return signState(id) >= 4; }

/// Which way its face looks, standing or hung. One accessor for both, because
/// every reader wants the same answer and only the geometry differs.
constexpr FaceDirection signFacing(BlockId id) {
    return static_cast<FaceDirection>(signState(id) & 3);
}

constexpr BlockId signAt(int kind, int family, FaceDirection facing, bool onWall) {
    return static_cast<BlockId>(static_cast<int>(signRunFirst(kind)) + family * kSignStates +
                                (onWall ? 4 : 0) + static_cast<int>(facing));
}

constexpr bool isSmoker(BlockId id) {
    return id >= BlockId::Smoker && id <= BlockId::SmokerWestLit;
}

/// A blast furnace, in any of its eight states. **Two ranges**, because the
/// north-facing unlit one was placed in the table run before the other seven
/// existed and is already on disk.
constexpr bool isBlastFurnace(BlockId id) {
    return id == BlockId::BlastFurnace ||
           (id >= BlockId::BlastFurnaceExtraFirst && id <= BlockId::BlastFurnaceExtraLast);
}

/// Where in the appended seven a state sits: east, south, west, then lit.
constexpr int blastFurnaceStep(BlockId id) {
    return id == BlockId::BlastFurnace
               ? -1
               : static_cast<int>(id) - static_cast<int>(BlockId::BlastFurnaceExtraFirst);
}

constexpr bool isBlastFurnaceLit(BlockId id) { return blastFurnaceStep(id) >= 3; }

constexpr FaceDirection blastFurnaceFacing(BlockId id) {
    const int step = blastFurnaceStep(id);
    switch (step < 0 ? 0 : (step >= 3 ? step - 3 : step + 1)) {
    case 1:
        return FaceDirection::PosX;
    case 2:
        return FaceDirection::PosZ;
    case 3:
        return FaceDirection::NegX;
    default:
        return FaceDirection::NegZ;
    }
}

constexpr BlockId blastFurnaceAt(FaceDirection facing, bool lit) {
    int quarter = 0;
    switch (facing) {
    case FaceDirection::PosX:
        quarter = 1;
        break;
    case FaceDirection::PosZ:
        quarter = 2;
        break;
    case FaceDirection::NegX:
        quarter = 3;
        break;
    default:
        break;
    }
    if (!lit && quarter == 0) {
        return BlockId::BlastFurnace;
    }
    const int step = lit ? 3 + quarter : quarter - 1;
    return static_cast<BlockId>(static_cast<int>(BlockId::BlastFurnaceExtraFirst) + step);
}

constexpr bool isTrappedChest(BlockId id) {
    return id >= BlockId::TrappedChest && id <= BlockId::TrappedChestWest;
}

constexpr bool isEnderChest(BlockId id) {
    return id >= BlockId::EnderChest && id <= BlockId::EnderChestWest;
}

/// A cauldron and how full it is, nought to six. Two ranges, because the empty
/// one was placed in the table run before the other six existed.
constexpr bool isCauldron(BlockId id) {
    return id == BlockId::Cauldron ||
           (id >= BlockId::CauldronExtraFirst && id <= BlockId::CauldronExtraLast);
}

/// How full, 0 to 6. **A level and nothing else - the reference also records
/// *which liquid*, and we do not.**
///
/// Mojang's `metadata/vanilladata_modules/mojang-blocks.json` gives
/// `cauldron_liquid` the domain `[water, lava, powder_snow]` with **exactly one
/// user, `minecraft:cauldron`** - one user, so by the count-the-users rule that
/// is the block's own range and not a shared storage field. Two consequences,
/// both real and both waiting on ids rather than on logic: a lava cauldron
/// cannot exist, so dripstone can only ever drip water (a lava drip fills an
/// *empty* cauldron completely, which is unrepresentable while "empty" and
/// "which liquid" are the same fact), and the powder-snow bucket has no source,
/// that being Bedrock's obtain path for it.
constexpr int cauldronLevel(BlockId id) {
    return id == BlockId::Cauldron
               ? 0
               : 1 + static_cast<int>(id) - static_cast<int>(BlockId::CauldronExtraFirst);
}

constexpr BlockId cauldronAt(int level) {
    const int clamped = level < 0 ? 0 : (level > 6 ? 6 : level);
    return clamped == 0 ? BlockId::Cauldron
                        : static_cast<BlockId>(static_cast<int>(BlockId::CauldronExtraFirst) +
                                               clamped - 1);
}

static_assert(cauldronLevel(cauldronAt(0)) == 0 && cauldronLevel(cauldronAt(6)) == 6 &&
                  cauldronLevel(cauldronAt(3)) == 3,
              "a cauldron's level has to survive a round trip through its id");

/// Every block that opens the twenty-seven slot panel. **The barrel and the
/// trapped chest are chests as far as everything downstream is concerned** -
/// the same screen, the same block entity, the same spill on break - which is
/// why widening this one predicate is the whole of what they cost.
///
/// Mojang's own interface does exactly this: `barrel_panel`, `shulker_box_panel`
/// and `ender_chest_panel` are all aliases of `small_chest_panel`.
/// A stowbox, plain or dyed.
constexpr bool isStowbox(BlockId id) {
    return id == BlockId::Stowbox ||
           (id >= BlockId::StowboxDyedFirst && id <= BlockId::StowboxDyedLast);
}

/// A chest placed by worldgen whose contents have not been rolled yet.
///
/// Everything downstream treats it as the plain chest it will become - one
/// widened `isChest` below buys it the screen, the hardness, the axe, the drop
/// and the material family in one edit. **The one thing it must not inherit is
/// pairing**; see `chestPairs`.
constexpr bool isLootChest(BlockId id) {
    return id >= BlockId::LootChestRunFirst && id <= BlockId::LootChestRunLast;
}

/// Which loot table an unrolled chest carries, as a plain index.
///
/// Meaningful only where `isLootChest` holds. It is a `loot::TableId` value,
/// which this header deliberately cannot name: `Loot.hpp` is built on top of
/// `Block.hpp` and naming it here would be a cycle. `Loot.hpp` asserts the two
/// counts agree.
constexpr int lootChestTable(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::LootChestRunFirst)) / 4;
}

constexpr bool isChest(BlockId id) {
    return (id >= BlockId::Chest && id <= BlockId::ChestWest) || id == BlockId::Barrel ||
           isTrappedChest(id) || isEnderChest(id) || isStowbox(id) || isLootChest(id);
}

/// Whether two of these standing side by side join into a fifty-four slot
/// container. **Neither a barrel nor an ender chest ever does** - the reference
/// says so outright, and without this the pairing walk would happily join two
/// of either, because its only test is that the neighbour is the same id.
///
/// **Nor does an unrolled loot chest**, and that one is not cosmetic: a
/// generated chest joined to the one a player set down beside it would open a
/// fifty-four slot screen where half the slots belong to a block that has not
/// been rolled, and the roll would then land in a container the player is
/// already looking at.
constexpr bool chestPairs(BlockId id) {
    return id != BlockId::Barrel && !isEnderChest(id) && !isStowbox(id) && !isLootChest(id);
}

/// Which way this chest's lid faces, and the chest that faces that way. Four
/// ids in the order of `FaceDirection`'s own compass, so both are arithmetic.
constexpr FaceDirection chestFacing(BlockId id) {
    // A barrel opens upward and a stowbox opens whichever way it was set down,
    // so neither has a lid facing to give; answering with a fixed side keeps
    // `chestJoinsAlongX` total without inventing a front.
    if (id == BlockId::Barrel || isStowbox(id)) {
        return FaceDirection::NegZ;
    }
    const int base = isTrappedChest(id)  ? static_cast<int>(BlockId::TrappedChest)
                     : isEnderChest(id)  ? static_cast<int>(BlockId::EnderChest)
                     : isLootChest(id)   ? static_cast<int>(BlockId::LootChestRunFirst) +
                                               lootChestTable(id) * 4
                                         : static_cast<int>(BlockId::Chest);
    switch (static_cast<int>(id) - base) {
    case 1:
        return FaceDirection::PosX;
    case 2:
        return FaceDirection::PosZ;
    case 3:
        return FaceDirection::NegX;
    default:
        return FaceDirection::NegZ;
    }
}

/// The plain chest facing a given way. Trapped chests get their own accessor
/// rather than a defaulted parameter, because every caller does know which
/// family it is placing.
constexpr BlockId chestFacing(FaceDirection facing) {
    switch (facing) {
    case FaceDirection::PosX:
        return BlockId::ChestEast;
    case FaceDirection::PosZ:
        return BlockId::ChestSouth;
    case FaceDirection::NegX:
        return BlockId::ChestWest;
    default:
        return BlockId::Chest;
    }
}

/// The unrolled chest for one loot table, facing a given way.
///
/// **The facing offset is read off `chestFacing` rather than written out
/// again**, so the two families can never disagree about which of the four ids
/// means east - that is the one derivation a second copy of the compass would
/// silently break.
constexpr BlockId lootChestAt(int table, FaceDirection facing) {
    const int offset = static_cast<int>(chestFacing(facing)) - static_cast<int>(BlockId::Chest);
    return static_cast<BlockId>(static_cast<int>(BlockId::LootChestRunFirst) + table * 4 + offset);
}

/// The plain chest an unrolled one becomes once its loot has been rolled - the
/// same facing, so a chest never turns round the first time it is opened.
///
/// Handing this to `setBlock` is what makes the roll stick, and it is the whole
/// of the duplication fix: the chunk is flagged modified, saved, and from then
/// on loaded rather than regenerated, so the block that said "unrolled" is gone
/// from the world for good.
constexpr BlockId plainChestFor(BlockId id) {
    return chestFacing(chestFacing(id));
}

static_assert(isChest(BlockId::LootChestRunFirst) && isChest(BlockId::LootChestRunLast) &&
                  !chestPairs(BlockId::LootChestRunFirst),
              "an unrolled chest is a chest everywhere except the pairing walk");
static_assert(lootChestAt(0, FaceDirection::NegZ) == BlockId::LootChestRunFirst &&
                  lootChestAt(kLootChestTables - 1, FaceDirection::NegX) ==
                      BlockId::LootChestRunLast,
              "the loot-chest run must be exactly kLootChestTables slices of four");
static_assert(chestFacing(lootChestAt(0, FaceDirection::PosX)) == FaceDirection::PosX &&
                  chestFacing(lootChestAt(kLootChestTables - 1, FaceDirection::PosZ)) ==
                      FaceDirection::PosZ &&
                  chestFacing(lootChestAt(kLootChestTables - 1, FaceDirection::NegX)) ==
                      FaceDirection::NegX,
              "an unrolled chest must report the facing it was built with, in every slice");
static_assert(lootChestTable(lootChestAt(kLootChestTables - 1, FaceDirection::PosZ)) ==
                  kLootChestTables - 1,
              "the table an unrolled chest carries must survive the round trip through its id");
static_assert(plainChestFor(lootChestAt(kLootChestTables - 1, FaceDirection::PosZ)) ==
                      BlockId::ChestSouth &&
                  plainChestFor(lootChestAt(0, FaceDirection::NegZ)) == BlockId::Chest,
              "rolling a chest must leave it facing exactly the way it already faced");

constexpr BlockId trappedChestFacing(FaceDirection facing) {
    switch (facing) {
    case FaceDirection::PosX:
        return BlockId::TrappedChestEast;
    case FaceDirection::PosZ:
        return BlockId::TrappedChestSouth;
    case FaceDirection::NegX:
        return BlockId::TrappedChestWest;
    default:
        return BlockId::TrappedChest;
    }
}

constexpr BlockId enderChestFacing(FaceDirection facing) {
    switch (facing) {
    case FaceDirection::PosX:
        return BlockId::EnderChestEast;
    case FaceDirection::PosZ:
        return BlockId::EnderChestSouth;
    case FaceDirection::NegX:
        return BlockId::EnderChestWest;
    default:
        return BlockId::EnderChest;
    }
}

/// A hopper, whichever way its spout points.
constexpr bool isHopper(BlockId id) {
    return id >= BlockId::Hopper && id <= BlockId::HopperWest;
}

/// Which way this hopper's spout points sideways, or `Unknown` when it pours
/// straight down.
///
/// **`FaceDirection` has no vertical**, deliberately - it names the four
/// compass faces a chest, gate or door can front. Rather than widen it for one
/// block, a hopper's fifth state is the absence of a sideways one, which is
/// exactly what `Unknown` already means everywhere else.
constexpr FaceDirection hopperSideSpout(BlockId id) {
    switch (static_cast<int>(id) - static_cast<int>(BlockId::Hopper)) {
    case 1:
        return FaceDirection::NegZ;
    case 2:
        return FaceDirection::PosZ;
    case 3:
        return FaceDirection::PosX;
    case 4:
        return FaceDirection::NegX;
    default:
        return FaceDirection::Unknown;
    }
}

/// The hopper pouring a given way. **Anything that is not one of the four
/// compass directions folds to down**, which is what makes a hopper placed on
/// a floor behave: the face you clicked is up, there is no upward state in the
/// reference either, and pointing it down is what it does instead of refusing.
constexpr BlockId hopperWithSideSpout(FaceDirection spout) {
    switch (spout) {
    case FaceDirection::NegZ:
        return BlockId::HopperNorth;
    case FaceDirection::PosZ:
        return BlockId::HopperSouth;
    case FaceDirection::PosX:
        return BlockId::HopperEast;
    case FaceDirection::NegX:
        return BlockId::HopperWest;
    default:
        return BlockId::Hopper;
    }
}

static_assert(hopperSideSpout(hopperWithSideSpout(FaceDirection::PosX)) == FaceDirection::PosX &&
                  hopperSideSpout(hopperWithSideSpout(FaceDirection::NegZ)) ==
                      FaceDirection::NegZ &&
                  hopperSideSpout(hopperWithSideSpout(FaceDirection::Unknown)) ==
                      FaceDirection::Unknown,
              "a hopper's spout has to survive a round trip through its id");

/// Which axis two chests join along - across the latch, never through it, so
/// they stand shoulder to shoulder rather than nose to tail. A bool rather
/// than a vector because this header deliberately knows nothing about glm.
constexpr bool chestJoinsAlongX(BlockId id) {
    const FaceDirection facing = chestFacing(id);
    return facing == FaceDirection::PosZ || facing == FaceDirection::NegZ;
}

/// One door or trapdoor material: what it is called, and the two pictures a
/// door needs. A trapdoor uses `lowerLayer` alone.
struct OpeningFamily {
    const char* name;
    int lowerLayer;
    int upperLayer;
    /// Iron answers to nothing but a signal in the reference. We have no
    /// redstone, so ours opens by hand and this only decides the tool and the
    /// hardness - a door nobody could open would be worse than a divergence.
    bool metal = false;
};

/// Where the door and trapdoor pictures begin in the table's sprite run.
constexpr int kDoorFirstLayer = 585;
constexpr int kTrapdoorFirstLayer = kDoorFirstLayer + kDoorFamilyCount * 2;

/// **Declared in `kWoods` order** - oak, spruce, birch, jungle, acacia, dark
/// oak, cherry, mangrove, crimson, warped, bamboo, then iron. That is not
/// cosmetic: the recipe loop walks the woods and indexes straight into these,
/// so a different order here would quietly give a cherry door a mangrove recipe.
constexpr std::array<OpeningFamily, kDoorFamilyCount> kDoorFamilies{{
    {"Oak Door", kDoorFirstLayer + 0, kDoorFirstLayer + 1},
    {"Spruce Door", kDoorFirstLayer + 2, kDoorFirstLayer + 3},
    {"Birch Door", kDoorFirstLayer + 4, kDoorFirstLayer + 5},
    {"Jungle Door", kDoorFirstLayer + 6, kDoorFirstLayer + 7},
    {"Acacia Door", kDoorFirstLayer + 8, kDoorFirstLayer + 9},
    {"Dark Oak Door", kDoorFirstLayer + 10, kDoorFirstLayer + 11},
    {"Cherry Door", kDoorFirstLayer + 12, kDoorFirstLayer + 13},
    {"Mangrove Door", kDoorFirstLayer + 14, kDoorFirstLayer + 15},
    {"Crimson Door", kDoorFirstLayer + 16, kDoorFirstLayer + 17},
    {"Warped Door", kDoorFirstLayer + 18, kDoorFirstLayer + 19},
    {"Bamboo Door", kDoorFirstLayer + 20, kDoorFirstLayer + 21},
    {"Iron Door", kDoorFirstLayer + 22, kDoorFirstLayer + 23, true},
}};

constexpr std::array<OpeningFamily, kTrapdoorFamilyCount> kTrapdoorFamilies{{
    {"Oak Trapdoor", kTrapdoorFirstLayer + 0, -1},
    {"Spruce Trapdoor", kTrapdoorFirstLayer + 1, -1},
    {"Birch Trapdoor", kTrapdoorFirstLayer + 2, -1},
    {"Jungle Trapdoor", kTrapdoorFirstLayer + 3, -1},
    {"Acacia Trapdoor", kTrapdoorFirstLayer + 4, -1},
    {"Dark Oak Trapdoor", kTrapdoorFirstLayer + 5, -1},
    {"Cherry Trapdoor", kTrapdoorFirstLayer + 6, -1},
    {"Mangrove Trapdoor", kTrapdoorFirstLayer + 7, -1},
    {"Crimson Trapdoor", kTrapdoorFirstLayer + 8, -1},
    {"Warped Trapdoor", kTrapdoorFirstLayer + 9, -1},
    {"Bamboo Trapdoor", kTrapdoorFirstLayer + 10, -1},
    {"Iron Trapdoor", kTrapdoorFirstLayer + 11, -1, true},
}};

constexpr bool isDoor(BlockId id) {
    return id >= BlockId::DoorRunFirst && id <= BlockId::DoorRunLast;
}

constexpr bool isTrapdoor(BlockId id) {
    return id >= BlockId::TrapdoorRunFirst && id <= BlockId::TrapdoorRunLast;
}

constexpr int doorOffset(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::DoorRunFirst)) % 32;
}

constexpr int doorFamily(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::DoorRunFirst)) / 32;
}

constexpr FaceDirection doorFacing(BlockId id) {
    return static_cast<FaceDirection>(doorOffset(id) & 3);
}

constexpr bool doorHingeRight(BlockId id) { return (doorOffset(id) & 4) != 0; }
constexpr bool doorOpen(BlockId id) { return (doorOffset(id) & 8) != 0; }
constexpr bool doorIsUpper(BlockId id) { return (doorOffset(id) & 16) != 0; }

constexpr BlockId doorAt(int family, FaceDirection facing, bool hingeRight, bool open, bool upper) {
    const int offset = (upper ? 16 : 0) | (open ? 8 : 0) | (hingeRight ? 4 : 0) |
                       (static_cast<int>(facing) & 3);
    return static_cast<BlockId>(static_cast<int>(BlockId::DoorRunFirst) + family * 32 + offset);
}

constexpr int trapdoorOffset(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::TrapdoorRunFirst)) % 16;
}

constexpr int trapdoorFamily(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::TrapdoorRunFirst)) / 16;
}

constexpr FaceDirection trapdoorFacing(BlockId id) {
    return static_cast<FaceDirection>(trapdoorOffset(id) & 3);
}

constexpr bool trapdoorOpen(BlockId id) { return (trapdoorOffset(id) & 4) != 0; }
constexpr bool trapdoorIsTop(BlockId id) { return (trapdoorOffset(id) & 8) != 0; }

constexpr BlockId trapdoorAt(int family, FaceDirection facing, bool open, bool top) {
    const int offset = (top ? 8 : 0) | (open ? 4 : 0) | (static_cast<int>(facing) & 3);
    return static_cast<BlockId>(static_cast<int>(BlockId::TrapdoorRunFirst) + family * 16 + offset);
}

/// The form of each that belongs in a catalogue: shut, bottom half, left hinge,
/// facing north.
constexpr BlockId doorCanonical(int family) {
    return doorAt(family, FaceDirection::NegZ, false, false, false);
}

constexpr BlockId trapdoorCanonical(int family) {
    return trapdoorAt(family, FaceDirection::NegZ, false, false);
}

// Every one of the five facts about a door has to survive a round trip, or
// opening one would silently move it, turn it round or swap which half is which.
static_assert(doorFamily(doorAt(7, FaceDirection::PosX, true, true, true)) == 7);
static_assert(doorFacing(doorAt(7, FaceDirection::PosX, true, true, true)) == FaceDirection::PosX);
static_assert(doorHingeRight(doorAt(7, FaceDirection::PosX, true, true, true)));
static_assert(doorOpen(doorAt(7, FaceDirection::PosX, true, true, true)));
static_assert(doorIsUpper(doorAt(7, FaceDirection::PosX, true, true, true)));
static_assert(!doorOpen(doorAt(0, FaceDirection::NegZ, false, false, false)));
static_assert(isDoor(doorAt(kDoorFamilyCount - 1, FaceDirection::NegX, true, true, true)));
static_assert(trapdoorFamily(trapdoorAt(3, FaceDirection::PosZ, true, true)) == 3);
static_assert(trapdoorOpen(trapdoorAt(3, FaceDirection::PosZ, true, true)));
static_assert(trapdoorIsTop(trapdoorAt(3, FaceDirection::PosZ, true, true)));
static_assert(isTrapdoor(trapdoorAt(kTrapdoorFamilyCount - 1, FaceDirection::NegX, true, true)));

/// One dyed bed: its name and the four pictures its two halves need.
struct BedFamily {
    const char* name;
    int footTop;
    int footSide;
    int headTop;
    int headSide;
};

constexpr int kBedFirstLayer = kTrapdoorFirstLayer + kTrapdoorFamilyCount;

/// Four pictures per colour, in the order the staging loop writes them.
constexpr BedFamily bedFamilyAt(int colour) {
    constexpr const char* kNames[kBedColours] = {
        "White Bed",     "Orange Bed", "Magenta Bed", "Light Blue Bed",
        "Yellow Bed",    "Lime Bed",   "Pink Bed",    "Gray Bed",
        "Light Gray Bed", "Cyan Bed",  "Purple Bed",  "Blue Bed",
        "Brown Bed",     "Green Bed",  "Red Bed",     "Black Bed",
    };
    const int base = kBedFirstLayer + colour * 4;
    return BedFamily{kNames[colour], base, base + 1, base + 2, base + 3};
}

constexpr bool isBed(BlockId id) {
    return id >= BlockId::BedRunFirst && id <= BlockId::BedRunLast;
}

constexpr int bedOffset(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::BedRunFirst)) % 8;
}

constexpr int bedColour(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::BedRunFirst)) / 8;
}

/// Which way the head lies from the foot. Both halves carry the same value, so
/// either one can find the other.
constexpr FaceDirection bedFacing(BlockId id) {
    return static_cast<FaceDirection>(bedOffset(id) & 3);
}

constexpr bool bedIsHead(BlockId id) { return (bedOffset(id) & 4) != 0; }

constexpr BlockId bedAt(int colour, FaceDirection facing, bool head) {
    const int offset = (head ? 4 : 0) | (static_cast<int>(facing) & 3);
    return static_cast<BlockId>(static_cast<int>(BlockId::BedRunFirst) + colour * 8 + offset);
}

/// The form that belongs in a catalogue: the foot, facing north.
constexpr BlockId bedCanonical(int colour) {
    return bedAt(colour, FaceDirection::NegZ, false);
}

static_assert(bedColour(bedAt(9, FaceDirection::PosZ, true)) == 9);
static_assert(bedFacing(bedAt(9, FaceDirection::PosZ, true)) == FaceDirection::PosZ);
static_assert(bedIsHead(bedAt(9, FaceDirection::PosZ, true)));
static_assert(!bedIsHead(bedAt(9, FaceDirection::PosZ, false)));
static_assert(isBed(bedAt(kBedColours - 1, FaceDirection::NegX, true)));

/// Which half of a double chest a block is, seen by someone facing its front.
///
/// **Derived from world position every time it is asked, never stored** - the
/// same rule the pairing itself follows. That is why a double chest needs no
/// save data, survives a reload, and cannot have its two halves disagree about
/// which is which: both run the same calculation and get the same answer.
enum class ChestHalf : std::uint8_t {
    Single = 0,
    Left,
    Right,
};

/// The offset a facing contributes inside any one honey level's four ids.
///
/// **One table, read in both directions.** `beehiveFacing` is this function's
/// inverse, and the two used to be written out separately - the shape that
/// mirrored every model box for four milestones because a derivation was applied
/// to one of a pair and not the other. The round-trip below pins them together.
constexpr int beehiveFacingStep(FaceDirection facing) {
    switch (facing) {
    case FaceDirection::PosX:
        return 1;
    case FaceDirection::PosZ:
        return 2;
    case FaceDirection::NegX:
        return 3;
    default:
        return 0;
    }
}

constexpr FaceDirection beehiveFacing(BlockId id) {
    // **Which run, then one subtraction** - rather than a subtraction per run,
    // which is how a third run gets added to the family test and missed here.
    const bool partial =
        id >= BlockId::BeehiveHoneyRunFirst && id <= BlockId::BeehiveHoneyRunLast;
    const int base = isBeeNest(id) ? static_cast<int>(BlockId::BeeNestRunFirst)
                     : partial     ? static_cast<int>(BlockId::BeehiveHoneyRunFirst)
                                   : static_cast<int>(BlockId::Beehive);
    const int offset = static_cast<int>(id) - base;
    switch (offset % 4) {
    case 1:
        return FaceDirection::PosX;
    case 2:
        return FaceDirection::PosZ;
    case 3:
        return FaceDirection::NegX;
    default:
        return FaceDirection::NegZ;
    }
}

/// The hive facing this way at this honey level. **The one place the two halves
/// of a hive's identity are combined**, so filling one can never quietly turn it
/// round - which is exactly what a plain `honey ? A : B` would do.
///
/// Levels 0 and 5 come out of the original run and the four in between out of
/// the appended one, which is why this is arithmetic in three pieces rather than
/// one. Out-of-range levels clamp rather than wrapping into another block.
constexpr BlockId beehiveAtLevel(FaceDirection facing, int level) {
    const int step = beehiveFacingStep(facing);
    const int clamped = level < 0 ? 0 : (level > kBeehiveFullHoney ? kBeehiveFullHoney : level);
    if (clamped == 0) {
        return static_cast<BlockId>(static_cast<int>(BlockId::Beehive) + step);
    }
    if (clamped == kBeehiveFullHoney) {
        return static_cast<BlockId>(static_cast<int>(BlockId::BeehiveHoney) + step);
    }
    return static_cast<BlockId>(static_cast<int>(BlockId::BeehiveHoneyRunFirst) +
                                (clamped - 1) * 4 + step);
}

/// Empty or full, for the callers that only ever meant those two.
///
/// **Kept as a `bool` deliberately, rather than widening this one to an `int`
/// in place.** `Main.cpp` places a hive with `beehiveAt(front,
/// beehiveHasHoney(placing))` and resets a sheared one with `beehiveAt(facing,
/// false)`. Had the parameter become an `int`, both would still compile and the
/// first would silently mean *level 1* - a hive one sixth full - where it has
/// always meant full. A separate name is what makes that mistake unwritable.
constexpr BlockId beehiveAt(FaceDirection facing, bool honey) {
    return beehiveAtLevel(facing, honey ? kBeehiveFullHoney : 0);
}

/// Every facing at every level, made and then read back.
///
/// Takes the constructor as a parameter so the same body can be run against a
/// deliberately wrong one; a check that cannot fail is not evidence.
constexpr bool beehiveRoundTrips(BlockId (*make)(FaceDirection, int)) {
    const FaceDirection facings[4] = {FaceDirection::PosX, FaceDirection::NegX,
                                      FaceDirection::PosZ, FaceDirection::NegZ};
    for (int level = 0; level <= kBeehiveFullHoney; ++level) {
        for (const FaceDirection facing : facings) {
            const BlockId id = make(facing, level);
            if (!isBeehive(id) || beehiveHoneyLevel(id) != level ||
                beehiveFacing(id) != facing || beehiveFacingStep(beehiveFacing(id)) != beehiveFacingStep(facing)) {
                return false;
            }
        }
    }
    return true;
}

static_assert(beehiveRoundTrips(beehiveAtLevel),
              "a hive must read back the facing and the honey level it was built with");

/// **CONTROL.** The layout mistake this run invites: partial levels laid out
/// facing-major, four levels per facing, instead of level-major. It is a
/// one-character-looking change that produces a plausible id for every input.
constexpr BlockId beehiveAtLevelFacingMajor(FaceDirection facing, int level) {
    const int step = beehiveFacingStep(facing);
    const int clamped = level < 0 ? 0 : (level > kBeehiveFullHoney ? kBeehiveFullHoney : level);
    if (clamped == 0) {
        return static_cast<BlockId>(static_cast<int>(BlockId::Beehive) + step);
    }
    if (clamped == kBeehiveFullHoney) {
        return static_cast<BlockId>(static_cast<int>(BlockId::BeehiveHoney) + step);
    }
    return static_cast<BlockId>(static_cast<int>(BlockId::BeehiveHoneyRunFirst) +
                                (clamped - 1) + step * kBeehivePartialHoneyLevels);
}

static_assert(!beehiveRoundTrips(beehiveAtLevelFacingMajor),
              "the round trip must reject a wrongly ordered run, or it proves nothing");

// The ids the `bool` form produces are the ids it has always produced. This is
// what lets every saved world and every existing caller keep their meaning
// without a chunk format bump.
static_assert(beehiveAt(FaceDirection::NegZ, true) == BlockId::BeehiveHoney &&
                  beehiveAt(FaceDirection::PosX, true) == BlockId::BeehiveHoneyEast &&
                  beehiveAt(FaceDirection::PosZ, true) == BlockId::BeehiveHoneySouth &&
                  beehiveAt(FaceDirection::NegX, true) == BlockId::BeehiveHoneyWest &&
                  beehiveAt(FaceDirection::NegZ, false) == BlockId::Beehive,
              "widening honey_level must not move the two levels that already had ids");

// **The trap, written down as a compiled fact.** Widening `beehiveAt`'s `bool`
// to an `int` in place would leave every existing call site compiling while
// `true` quietly became level 1. These two ids differ, so that mistake cannot
// hide here.
static_assert(beehiveAtLevel(FaceDirection::NegZ, 1) != beehiveAt(FaceDirection::NegZ, true),
              "level 1 and a full hive must be different blocks");

static_assert(beehiveAtLevel(FaceDirection::NegZ, 99) == BlockId::BeehiveHoney &&
                  beehiveAtLevel(FaceDirection::NegZ, -3) == BlockId::Beehive,
              "an out-of-range honey level must clamp rather than land on another block");

static_assert(beehiveFacing(beehiveAt(FaceDirection::PosZ, true)) == FaceDirection::PosZ);
static_assert(beehiveHasHoney(beehiveAt(FaceDirection::PosZ, true)));
static_assert(!beehiveHasHoney(beehiveAt(FaceDirection::NegX, false)));

/// The nest facing this way at this honey level.
///
/// **One run, so one line** - `first + level * 4 + facing`, which is the shape
/// the hive would have had if it had been declared in one piece. Out-of-range
/// levels clamp rather than wrapping into another block, exactly as the hive's
/// does, because both are reached from a generator and a bee that count.
constexpr BlockId beeNestAtLevel(FaceDirection facing, int level) {
    const int clamped = level < 0 ? 0 : (level > kBeehiveFullHoney ? kBeehiveFullHoney : level);
    return static_cast<BlockId>(static_cast<int>(BlockId::BeeNestRunFirst) + clamped * 4 +
                                beehiveFacingStep(facing));
}

// **The same checker the hive uses, run against the nest's constructor.** It
// works unchanged because `isBeehive`, `beehiveHoneyLevel` and `beehiveFacing`
// all answer for a nest - so if any one of those three had been widened and the
// others not, this line is where it fails rather than in play.
static_assert(beehiveRoundTrips(beeNestAtLevel),
              "a nest must read back the facing and the honey level it was built with");

/// **CONTROL.** The nest run laid out facing-major - six levels per facing
/// instead of four facings per level. Every input still produces an id inside
/// the run, which is what makes it worth rejecting explicitly.
constexpr BlockId beeNestAtLevelFacingMajor(FaceDirection facing, int level) {
    const int clamped = level < 0 ? 0 : (level > kBeehiveFullHoney ? kBeehiveFullHoney : level);
    return static_cast<BlockId>(static_cast<int>(BlockId::BeeNestRunFirst) + clamped +
                                beehiveFacingStep(facing) * kBeehiveHoneyLevels);
}

static_assert(!beehiveRoundTrips(beeNestAtLevelFacingMajor),
              "the round trip must reject a wrongly ordered nest run, or it proves nothing");

/// **The same kind of home this one already is, at a new honey level.**
///
/// **This exists to close one specific live trap.** `Main.cpp` resets a sheared
/// home with `beehiveAt(beehiveFacing(hive), false)` - correct for the hive it
/// was written against, and it turns a nest into a hive, because a bare facing
/// carries no record of which of the two it came from. Nothing about that
/// mistake looks wrong: it compiles, it draws, it faces the right way, and the
/// only symptom is that a naturally generated block quietly becomes a crafted
/// one the first time it is sheared. **Every caller that already holds the block
/// should ask this**, and the control below is that exact wrong call, kept so it
/// can be rejected rather than described.
///
/// **The three-argument form is for the one caller that must keep the kind and
/// change the facing**, which is placement: the block in your hand records which
/// of the two it is and what honey it carries, and the camera decides which way
/// it ends up pointing. Without it `Main.cpp` has to write this function's body
/// out with one term substituted, which is the duplication the helper exists to
/// stop and would leave two places to find if a third kind of home is ever
/// appended. The two-argument form is now that one, asked for the facing it
/// already has, so there is a single expression of the kind rule.
///
/// **The forward is what keeps the old proof valid, so do not undo it.**
/// `beeHomeKeepsItsKind` can only ever hand a block its own facing, so it
/// cannot reach the facing axis at all - but because the two-argument form is
/// now nothing but a call to the three-argument one, a three-argument form that
/// forgot the kind would make the two-argument form forget it too, and that
/// assert would fail. The old proof covers the new overload **by construction**.
/// Writing the two-argument body out again beside it - which looks like
/// harmless inlining - would silently sever that and leave the kind rule in two
/// places, which is the exact thing this pair of functions exists to prevent.
/// The facing axis is unreachable that way and has a sweep of its own below,
/// with one control per axis: one that turns correctly and forgets the kind,
/// one that keeps the kind and ignores the new facing.
constexpr BlockId beeHomeAtLevel(BlockId home, FaceDirection facing, int level) {
    return isBeeNest(home) ? beeNestAtLevel(facing, level) : beehiveAtLevel(facing, level);
}

constexpr BlockId beeHomeAtLevel(BlockId home, int level) {
    return beeHomeAtLevel(home, beehiveFacing(home), level);
}

/// Both kinds, every facing, every level moved to every other level.
///
/// **The parameter is a concrete function-pointer type deliberately, and it has
/// to stay one.** `beeHomeAtLevel` is overloaded, so naming it here without
/// parentheses hands over an overload set rather than a function. A fixed target
/// type resolves that by *exact* match, and because the two overloads differ in
/// arity rather than in a parameter type, only one of them can ever match: a
/// function type's parameter count is part of the type and no conversion reaches
/// across it. So there is nothing to disambiguate and no cast is wanted here.
///
/// Rewrite this to take its callable by template deduction and the very same
/// call becomes a hard error, because deduction cannot choose from an overload
/// set. `beeHomeTurnsWithoutChangingKind` below is the same shape for the same
/// reason, as is `facingTwinMatchesTheAxes` elsewhere in this file.
constexpr bool beeHomeKeepsItsKind(BlockId (*move)(BlockId, int)) {
    const FaceDirection facings[4] = {FaceDirection::PosX, FaceDirection::NegX,
                                      FaceDirection::PosZ, FaceDirection::NegZ};
    for (int from = 0; from <= kBeehiveFullHoney; ++from) {
        for (int to = 0; to <= kBeehiveFullHoney; ++to) {
            for (const FaceDirection facing : facings) {
                const BlockId movedHive = move(beehiveAtLevel(facing, from), to);
                const BlockId movedNest = move(beeNestAtLevel(facing, from), to);
                if (isBeeNest(movedHive) || !isBeeNest(movedNest)) {
                    return false;
                }
                if (beehiveHoneyLevel(movedHive) != to || beehiveHoneyLevel(movedNest) != to) {
                    return false;
                }
                if (beehiveFacing(movedHive) != facing || beehiveFacing(movedNest) != facing) {
                    return false;
                }
            }
        }
    }
    return true;
}

static_assert(beeHomeKeepsItsKind(beeHomeAtLevel),
              "filling or emptying a bee's home must not change which kind of home it is");

/// **CONTROL - the call the game used to make, and the reason a two-argument
/// form exists here at all.** A facing and a level with the kind thrown away on
/// the way through, so a sheared nest came back as a beehive.
///
/// **The game no longer makes it.** Checked 2026-08-19 against `Main.cpp`, which
/// now goes through `beeHomeAtLevel` at both the shear site and the honey-filling
/// site. What would make this stale again is either of those sites reverting to
/// a bare `beehiveAtLevel(beehiveFacing(...), ...)`.
///
/// It stays compiled regardless, and must: it is the negative half that proves
/// the assert above is capable of failing, and a positive assert alone would sit
/// there passing on a function that forgot the kind. **Nothing may call it.**
constexpr BlockId beeHomeAtLevelForgettingKind(BlockId home, int level) {
    return beehiveAtLevel(beehiveFacing(home), level);
}

static_assert(!beeHomeKeepsItsKind(beeHomeAtLevelForgettingKind),
              "the kind check must reject a rebuild that goes through a bare facing, or it is "
              "agreeing with the bug rather than catching it");

/// **Both kinds, every facing turned to every other facing, at every level.**
/// The two-argument sweep above cannot ask this, because the facing it uses is
/// the one the block already had - so a form that ignored its facing argument
/// entirely would sail through it.
constexpr bool beeHomeTurnsWithoutChangingKind(BlockId (*turn)(BlockId, FaceDirection, int)) {
    const FaceDirection facings[4] = {FaceDirection::PosX, FaceDirection::NegX,
                                      FaceDirection::PosZ, FaceDirection::NegZ};
    for (const FaceDirection from : facings) {
        for (const FaceDirection to : facings) {
            for (int level = 0; level <= kBeehiveFullHoney; ++level) {
                const BlockId turnedHive = turn(beehiveAtLevel(from, kBeehiveFullHoney), to, level);
                const BlockId turnedNest = turn(beeNestAtLevel(from, kBeehiveFullHoney), to, level);
                if (isBeeNest(turnedHive) || !isBeeNest(turnedNest)) {
                    return false;
                }
                if (beehiveFacing(turnedHive) != to || beehiveFacing(turnedNest) != to) {
                    return false;
                }
                if (beehiveHoneyLevel(turnedHive) != level ||
                    beehiveHoneyLevel(turnedNest) != level) {
                    return false;
                }
            }
        }
    }
    return true;
}

static_assert(beeHomeTurnsWithoutChangingKind(beeHomeAtLevel),
              "placing a bee's home must take the facing from the camera and the kind from the "
              "block in hand");

/// **CONTROL, and it is the plausible simplification**: drop the new facing on
/// the floor and forward to the two-argument form, which is what "the overload
/// is just a convenience" reasoning produces.
constexpr BlockId beeHomeAtLevelIgnoringTheNewFacing(BlockId home, FaceDirection, int level) {
    return beeHomeAtLevel(home, level);
}

static_assert(!beeHomeTurnsWithoutChangingKind(beeHomeAtLevelIgnoringTheNewFacing),
              "the turn check must reject a form that keeps the old facing, or it is only "
              "re-testing what the two-argument sweep already covers");

/// **CONTROL, and it is the original bug wearing the new signature**: turn the
/// block correctly and rebuild it as a hive whatever it was. This is the shear
/// mistake all over again - a nest quietly becoming a crafted beehive - and
/// without it the kind clause of the sweep above is decorative, because nothing
/// proves it can fire.
constexpr BlockId beeHomeAtLevelTurningButForgettingKind(BlockId, FaceDirection facing, int level) {
    return beehiveAtLevel(facing, level);
}

static_assert(!beeHomeTurnsWithoutChangingKind(beeHomeAtLevelTurningButForgettingKind),
              "the turn check must reject a form that rebuilds every home as a hive, or its kind "
              "clause has never been shown to fire");

// A nest and a hive at the same facing and the same level are different blocks -
// the fact every line above rests on, and the one a shared run would have lost.
static_assert(beeNestAtLevel(FaceDirection::PosX, 3) != beehiveAtLevel(FaceDirection::PosX, 3) &&
                  beeNestAtLevel(FaceDirection::NegZ, 0) != BlockId::Beehive,
              "a nest must never share an id with the hive of the same facing and level");

/// Which half of a pair a chest is, given where its partner stands.
///
/// `dx`/`dz` are the partner's offset, as two ints because this header knows
/// nothing about glm. Left means the viewer's left looking at the front, which
/// is `cross(frontNormal, up)` - so a partner standing *there* puts this chest
/// on the right of the pair. The asserts below are the whole specification, and
/// they are what stops the two halves being swapped by a later edit.
constexpr ChestHalf chestHalfFor(FaceDirection facing, int dx, int dz) {
    int leftX = 0;
    int leftZ = 0;
    switch (facing) {
    case FaceDirection::PosX:
        leftZ = 1;
        break;
    case FaceDirection::NegX:
        leftZ = -1;
        break;
    case FaceDirection::PosZ:
        leftX = -1;
        break;
    default:
        leftX = 1;
        break;
    }
    return (dx == leftX && dz == leftZ) ? ChestHalf::Right : ChestHalf::Left;
}

static_assert(chestHalfFor(FaceDirection::NegZ, 1, 0) == ChestHalf::Right);
static_assert(chestHalfFor(FaceDirection::NegZ, -1, 0) == ChestHalf::Left);
static_assert(chestHalfFor(FaceDirection::PosZ, -1, 0) == ChestHalf::Right);
static_assert(chestHalfFor(FaceDirection::PosZ, 1, 0) == ChestHalf::Left);
static_assert(chestHalfFor(FaceDirection::PosX, 0, 1) == ChestHalf::Right);
static_assert(chestHalfFor(FaceDirection::NegX, 0, -1) == ChestHalf::Right);

/// **Every cooker, all three families.** The block entity, the screen, opening
/// it, breaking it and spilling its contents all ask this and none of them care
/// which kind it is - which is the whole reason a smoker cost no new screen and
/// a blast furnace cost none either.
constexpr bool isFurnace(BlockId id) {
    return id == BlockId::Furnace || id == BlockId::FurnaceLit ||
           (id >= BlockId::FurnaceEast && id <= BlockId::FurnaceWestLit) || isSmoker(id) ||
           isBlastFurnace(id);
}

constexpr bool isFurnaceLit(BlockId id) {
    if (isBlastFurnace(id)) {
        return isBlastFurnaceLit(id);
    }
    if (isSmoker(id)) {
        // Unlit then lit, in pairs, so the low bit of the offset is the state.
        return ((static_cast<int>(id) - static_cast<int>(BlockId::Smoker)) & 1) != 0;
    }
    return id == BlockId::FurnaceLit || id == BlockId::FurnaceEastLit ||
           id == BlockId::FurnaceSouthLit || id == BlockId::FurnaceWestLit;
}

/// Which way this cooker's mouth points.
constexpr FaceDirection furnaceFacing(BlockId id) {
    if (isBlastFurnace(id)) {
        return blastFurnaceFacing(id);
    }
    if (isSmoker(id)) {
        switch ((static_cast<int>(id) - static_cast<int>(BlockId::Smoker)) / 2) {
        case 1:
            return FaceDirection::PosX;
        case 2:
            return FaceDirection::PosZ;
        case 3:
            return FaceDirection::NegX;
        default:
            return FaceDirection::NegZ;
        }
    }
    switch (id) {
    case BlockId::FurnaceEast:
    case BlockId::FurnaceEastLit:
        return FaceDirection::PosX;
    case BlockId::FurnaceSouth:
    case BlockId::FurnaceSouthLit:
        return FaceDirection::PosZ;
    case BlockId::FurnaceWest:
    case BlockId::FurnaceWestLit:
        return FaceDirection::NegX;
    default:
        return FaceDirection::NegZ;
    }
}

/// The cooker that faces this way, is lit or not, and belongs to this family.
///
/// **The one place the three halves of a cooker's identity are combined**, so
/// lighting one can never quietly turn it round or turn it into the other kind.
/// `smoker` is deliberately not defaulted: a default here would mean "I have
/// nothing to tell you", and every caller does.
constexpr BlockId furnaceFacing(FaceDirection facing, bool lit, bool smoker) {
    if (smoker) {
        int step = 0;
        switch (facing) {
        case FaceDirection::PosX:
            step = 1;
            break;
        case FaceDirection::PosZ:
            step = 2;
            break;
        case FaceDirection::NegX:
            step = 3;
            break;
        default:
            break;
        }
        return static_cast<BlockId>(static_cast<int>(BlockId::Smoker) + step * 2 + (lit ? 1 : 0));
    }
    switch (facing) {
    case FaceDirection::PosX:
        return lit ? BlockId::FurnaceEastLit : BlockId::FurnaceEast;
    case FaceDirection::PosZ:
        return lit ? BlockId::FurnaceSouthLit : BlockId::FurnaceSouth;
    case FaceDirection::NegX:
        return lit ? BlockId::FurnaceWestLit : BlockId::FurnaceWest;
    default:
        return lit ? BlockId::FurnaceLit : BlockId::Furnace;
    }
}

/// How much faster than a furnace this cooker works. The reference's smoker
/// cooks in five seconds instead of ten **and burns its fuel twice as fast**, so
/// the items per fuel are unchanged - which is exactly one multiplier on the
/// whole tick rather than two separate rates to keep in step. A blast furnace
/// is the same doubling, for ore rather than food.
constexpr float cookSpeed(BlockId id) {
    return (isSmoker(id) || isBlastFurnace(id)) ? 2.0f : 1.0f;
}

/// The cooker of `like`'s own family, facing this way and in this state.
///
/// **One owner for all three families.** `furnaceFacing`'s `smoker` bool was
/// fine while there were two, and would have silently turned a blast furnace
/// into a plain one the moment there were three - the exact shape of bug that
/// the lit-furnace facing already caused once.
constexpr BlockId cookerAt(BlockId like, FaceDirection facing, bool lit) {
    if (isBlastFurnace(like)) {
        return blastFurnaceAt(facing, lit);
    }
    return furnaceFacing(facing, lit, isSmoker(like));
}

/// Opens a screen when it is right-clicked, rather than being placed against.
///
/// **This is the one gate every screen is behind**, so a block whose screen is
/// written, wired and shipped is still unreachable until its id is named here -
/// which is exactly what happened to the brewing stand, and to all forty-one
/// brews behind it, for a whole milestone. The list is hand-maintained by
/// necessity, so anything added to the right-click branches has to be added
/// here in the same edit.
constexpr bool isInteractive(BlockId id) {
    return id == BlockId::CraftingTable || id == BlockId::SmithingTable || isFurnace(id) ||
           isChest(id) || isHopper(id) || id == BlockId::Grindstone || id == BlockId::Anvil ||
           id == BlockId::Stonecutter || id == BlockId::BrewingStand ||
           id == BlockId::ChippedAnvil || id == BlockId::DamagedAnvil;
}

/// Highest flowing level. Water at this depth cannot spread any further, which
/// is what stops a single source flooding the world.
///
/// **Seven, not fifteen, and the primary source appears to say fifteen.**
/// Mojang's own `metadata/vanilladata_modules/mojang-blocks.json` publishes
/// `liquid_depth` with domain `[0..15]`, so anyone checking this constant
/// against the reference meets a flat contradiction and is one edit away from
/// doubling every flow in the game. It resolves by counting the property's
/// users: `liquid_depth` has **four** - water, flowing_water, lava,
/// flowing_lava - so `[0..15]` is the field's storage domain rather than any
/// one block's range, and the sixteen values are eight spread levels in the low
/// three bits plus a falling flag above them. Seven is the top *spread* level
/// and it is right. `Fluid.hpp` carries the long form at `fluidHeight`, beside
/// the `/9.0f` scale that depends on it.
constexpr int kMaxWaterLevel = 7;

constexpr bool isWater(BlockId id) {
    return (id >= BlockId::Water0 && id <= BlockId::Water7) || id == BlockId::WaterFalling;
}

/// Water that has water directly above it, so it may only continue downward.
constexpr bool isFallingWater(BlockId id) {
    return id == BlockId::WaterFalling;
}

/// 0 for a source, rising as the flow thins out. Falling water is full, which
/// is why a column that lands spreads the full seven blocks rather than six.
constexpr int waterLevel(BlockId id) {
    if (id == BlockId::WaterFalling) {
        return 0;
    }
    return isWater(id) ? static_cast<int>(id) - static_cast<int>(BlockId::Water0) : kMaxWaterLevel + 1;
}

/// Level 0 is the **source**, so this must never be handed the level of a
/// falling cell - which also reads 0 and would come back as a source that never
/// drains. Only ever called with a level derived from `neighbour + 1`.
constexpr BlockId waterAtLevel(int level) {
    const int clamped = level < 0 ? 0 : (level > kMaxWaterLevel ? kMaxWaterLevel : level);
    return static_cast<BlockId>(static_cast<int>(BlockId::Water0) + clamped);
}

/// A source never drains. Everything else needs a supply each time it is
/// re-evaluated, which is what makes flow recede when you cut it off.
constexpr bool isWaterSource(BlockId id) {
    return id == BlockId::Water0;
}

/// Lava's half of the same encoding. Deliberately written as a parallel set
/// rather than one generic fluid accessor taking a base id: the two differ in
/// spread step, spread delay and what they do on contact, so the places that
/// genuinely mean "either fluid" ask `isFluid` and everything else names one.
///
/// **Seven for the same reason water's is** - `liquid_depth`'s `[0..15]` is a
/// four-user storage domain, not lava's range. See `kMaxWaterLevel`.
constexpr int kMaxLavaLevel = 7;

/// How many levels lava loses per block it spreads. Two is the reference's
/// Overworld figure and is the whole reason lava reaches three blocks where
/// water reaches seven.
constexpr int kLavaSpreadStep = 2;

constexpr bool isLava(BlockId id) {
    return (id >= BlockId::Lava0 && id <= BlockId::Lava7) || id == BlockId::LavaFalling;
}

constexpr bool isFallingLava(BlockId id) {
    return id == BlockId::LavaFalling;
}

constexpr int lavaLevel(BlockId id) {
    if (id == BlockId::LavaFalling) {
        return 0;
    }
    return isLava(id) ? static_cast<int>(id) - static_cast<int>(BlockId::Lava0) : kMaxLavaLevel + 1;
}

constexpr BlockId lavaAtLevel(int level) {
    const int clamped = level < 0 ? 0 : (level > kMaxLavaLevel ? kMaxLavaLevel : level);
    return static_cast<BlockId>(static_cast<int>(BlockId::Lava0) + clamped);
}

constexpr bool isLavaSource(BlockId id) {
    return id == BlockId::Lava0;
}

/// Either fluid. **This is the predicate for "can something move through, see
/// through or be washed away by this cell"** - the questions where the two are
/// interchangeable. Anything about swimming, drowning or fish stays on
/// `isWater`, because lava answers those differently or not at all.
constexpr bool isFluid(BlockId id) {
    return isWater(id) || isLava(id);
}

/// The level of whichever fluid this is, so the mesher can drop a surface
/// without asking which one twice.
constexpr int fluidLevel(BlockId id) {
    return isLava(id) ? lavaLevel(id) : waterLevel(id);
}

constexpr bool isFluidSource(BlockId id) {
    return isWaterSource(id) || isLavaSource(id);
}

static_assert(lavaLevel(BlockId::Lava0) == 0 && lavaLevel(BlockId::Lava6) == 6);
static_assert(lavaAtLevel(2) == BlockId::Lava2);
static_assert(isLava(BlockId::LavaFalling) && lavaLevel(BlockId::LavaFalling) == 0);
static_assert(!isLavaSource(BlockId::LavaFalling), "a falling cell is full but is not a source");
static_assert(isFluid(BlockId::Water3) && isFluid(BlockId::Lava4));
static_assert(!isFluid(BlockId::Fire), "fire is not a fluid, however much it spreads like one");

/// Drawn in the *opaque* pass, but with its fully transparent pixels thrown
/// away by the shader. Not the same thing as translucent: nothing is blended,
/// depth is still written, and so no sorting is needed.
/// Plain glass and the sixteen coloured ones.
///
/// **One owner for a range that was written out inline**, so a new colour cannot
/// be added to the cutout list and forgotten by everything else that has to know
/// a block is glass.
constexpr bool isGlassBlock(BlockId id) {
    return id == BlockId::Glass || id == BlockId::TintedGlass ||
           (id >= BlockId::WhiteStainedGlass && id <= BlockId::BlackStainedGlass);
}

/// The eight copper grates, which are a lattice and nothing else.
constexpr bool isCopperGrate(BlockId id) {
    return id >= BlockId::CopperGrate && id <= BlockId::WaxedOxidizedCopperGrate;
}

/// The anvil and its two damaged forms, which share one shape and differ only
/// in the picture on top.
constexpr bool isAnvil(BlockId id) {
    return id >= BlockId::Anvil && id <= BlockId::DamagedAnvil;
}

constexpr bool isCutout(BlockId id) {    // The cactus art carries the reference model's inset as transparency: a
    // one-texel border on the end caps and a column down each side.
    //
    // The coloured glass is here rather than in the blended pass on purpose:
    // ours has no sorted transparency, and a cutout at least draws in the right
    // order. What it costs is that stained glass is see-through only where its
    // art is, which for the reference's own textures is the border alone.
    return isLeafBlock(id) || isCrossBlock(id) || isGlassBlock(id) || id == BlockId::Fire ||
           isDoor(id) || isTrapdoor(id) ||
           // Everything drawn as a model. **Their art is transparent where the
           // model is hollow** - a cauldron's rim, a bell's frame, the gap
           // between a grindstone's legs - so without this the empty texels
           // draw as opaque black instead of being thrown away.
           isTorchBlock(id) || isCauldron(id) || isComposter(id) || id == BlockId::Bell ||
           id == BlockId::Grindstone || id == BlockId::Stonecutter ||
           id == BlockId::BrewingStand ||
           id == BlockId::Azalea || id == BlockId::FloweringAzalea || id == BlockId::Cactus ||
           id == BlockId::LilyPad || id == BlockId::IronBars || id == BlockId::EndRod ||
           id == BlockId::Lantern || id == BlockId::SoulLantern ||
           // Seventeen blocks whose art has real holes in it while they were
           // drawn as solid cubes: a grate is a lattice, an anvil and a
           // campfire and the sculk pair are shorter than their cell, and
           // scaffolding and the enchanting table are open frames. Without
           // this their empty texels draw as opaque black *and* they cull the
           // faces of everything they touch. Found by a table sweep, not by
           // looking - the same shape as the cactus.
           isCopperGrate(id) || isAnvil(id) ||
           id == BlockId::EnchantingTable || id == BlockId::Scaffolding ||
           id == BlockId::SculkSensor || id == BlockId::SculkShrieker ||
           id == BlockId::Campfire || id == BlockId::SoulCampfire ||
           id == BlockId::MonsterSpawner || id == BlockId::EndPortalFrame ||
           isHopper(id) || id == BlockId::FlowerPot ||
           (id >= BlockId::LadderNorth && id <= BlockId::PaneRunLast) ||
           (id >= BlockId::VineFirst && id <= BlockId::CocoaLast);
}

/// Which way a stair's low step faces. The tall half sits on the opposite side.
enum class Facing : std::uint8_t {
    North = 0, // -Z
    East = 1,  // +X
    South = 2, // +Z
    West = 3,  // -X
};

/// One material a cut shape comes in.
///
/// **The parent is the whole point.** A stair, slab, wall, fence or gate has no
/// texture, no hardness, no tool and no blast resistance of its own - it borrows
/// every one of them from the block it was cut out of, through `shapedParent`.
/// So six hundred and forty blocks arrived without a single new image and
/// without a single new row in any of the four tables that would otherwise have
/// needed one each.
struct ShapedFamily {
    BlockId parent;
    /// Written out in full rather than composed from the parent's name, because
    /// `blockName` hands back a pointer and has nowhere to build a string.
    const char* name;
};

/// Every material stairs come in, **cobblestone first** because its eight ids
/// predate the run and are already in saved worlds.
///
/// **Adding a family here is not a cheap row, and this is the place that has to
/// say so.** The run's length is computed from `kStairFamilyCount`, and
/// `SlabRunFirst` follows `StairsRunLast` implicitly, so one more stair family
/// lengthens the run by eight and **shifts every enumerator above it** - the
/// slabs, the walls, the fences, the gates, the whole third table-driven run and
/// every id after them. Those ids are written into saved chunks. The enum states
/// this beside the runs themselves, which is exactly where someone adding a row
/// *here* will not be looking, and it is why the third run was appended at the
/// very end rather than grown in place.
///
/// So a family added here must be **paired with a `kChunkFormatVersion` bump**
/// and coordinated with whoever else is bumping it, never landed incidentally.
/// The alternative the file already has precedent for is a run appended at the
/// tail with one branch in `stairsAt` and friends - which is what cobblestone
/// already is, read from the other end. `kSlabFamilies` below has the identical
/// property and the identical cost.
///
/// **And when a family is added here, its ids and its row are the same act -
/// which is itself the reason to add a family here rather than write its ids out
/// by hand.** `Recipe.cpp` derives the waxed copper recipes through `waxedForm`
/// and needs no edit at all when the ids appear, but an id written as a bare
/// enumerator *without* a row here has no parent, so no recipe is generated and
/// nothing anywhere reports it. The failure then looks like a `Recipe.cpp` fault
/// and is not one. Adding the row is what makes the ids exist at all, so the
/// table route cannot fail that way - the hand-written route is the only one
/// that can, and that is a third argument against it.
constexpr std::array<ShapedFamily, kStairFamilyCount> kStairFamilies{{
    {BlockId::Cobblestone, "Cobblestone Stairs"},
    {BlockId::Stone, "Stone Stairs"},
    {BlockId::MossyCobblestone, "Mossy Cobblestone Stairs"},
    {BlockId::StoneBricks, "Stone Brick Stairs"},
    {BlockId::MossyStoneBricks, "Mossy Stone Brick Stairs"},
    {BlockId::Granite, "Granite Stairs"},
    {BlockId::PolishedGranite, "Polished Granite Stairs"},
    {BlockId::Diorite, "Diorite Stairs"},
    {BlockId::PolishedDiorite, "Polished Diorite Stairs"},
    {BlockId::Andesite, "Andesite Stairs"},
    {BlockId::PolishedAndesite, "Polished Andesite Stairs"},
    {BlockId::CobbledDeepslate, "Cobbled Deepslate Stairs"},
    {BlockId::PolishedDeepslate, "Polished Deepslate Stairs"},
    {BlockId::DeepslateBricks, "Deepslate Brick Stairs"},
    {BlockId::DeepslateTiles, "Deepslate Tile Stairs"},
    {BlockId::Bricks, "Brick Stairs"},
    {BlockId::MudBricks, "Mud Brick Stairs"},
    {BlockId::Sandstone, "Sandstone Stairs"},
    {BlockId::SmoothSandstone, "Smooth Sandstone Stairs"},
    {BlockId::RedSandstone, "Red Sandstone Stairs"},
    {BlockId::SmoothRedSandstone, "Smooth Red Sandstone Stairs"},
    {BlockId::Prismarine, "Prismarine Stairs"},
    {BlockId::PrismarineBricks, "Prismarine Brick Stairs"},
    {BlockId::DarkPrismarine, "Dark Prismarine Stairs"},
    {BlockId::PurpurBlock, "Purpur Stairs"},
    {BlockId::QuartzBlock, "Quartz Stairs"},
    {BlockId::SmoothQuartz, "Smooth Quartz Stairs"},
    {BlockId::NetherBricks, "Nether Brick Stairs"},
    {BlockId::RedNetherBricks, "Red Nether Brick Stairs"},
    {BlockId::Blackstone, "Blackstone Stairs"},
    {BlockId::PolishedBlackstone, "Polished Blackstone Stairs"},
    {BlockId::PolishedBlackstoneBricks, "Polished Blackstone Brick Stairs"},
    {BlockId::EndStoneBricks, "End Stone Brick Stairs"},
    {BlockId::Tuff, "Tuff Stairs"},
    {BlockId::PolishedTuff, "Polished Tuff Stairs"},
    {BlockId::TuffBricks, "Tuff Brick Stairs"},
    {BlockId::CutCopper, "Cut Copper Stairs"},
    {BlockId::ExposedCutCopper, "Exposed Cut Copper Stairs"},
    {BlockId::WeatheredCutCopper, "Weathered Cut Copper Stairs"},
    {BlockId::OxidizedCutCopper, "Oxidized Cut Copper Stairs"},
    {BlockId::Planks, "Oak Stairs"},
    {BlockId::SprucePlanks, "Spruce Stairs"},
    {BlockId::BirchPlanks, "Birch Stairs"},
    {BlockId::JunglePlanks, "Jungle Stairs"},
    {BlockId::AcaciaPlanks, "Acacia Stairs"},
    {BlockId::DarkOakPlanks, "Dark Oak Stairs"},
    {BlockId::CherryPlanks, "Cherry Stairs"},
    {BlockId::MangrovePlanks, "Mangrove Stairs"},
    {BlockId::CrimsonPlanks, "Crimson Stairs"},
    {BlockId::WarpedPlanks, "Warped Stairs"},
    {BlockId::BambooPlanks, "Bamboo Stairs"},
    {BlockId::BambooMosaic, "Bamboo Mosaic Stairs"},
    // **These four sit in the tail run, not in `StairsRun`**, and that is the whole
    // reason waxed copper cost nothing. A family's eight ids are allocated by its
    // position, so appending inside the run would have lengthened `StairsRunLast`,
    // slid `SlabRunFirst` and every enumerator above it, and renumbered blocks
    // already written into saved chunks. `stairsAt` and its siblings carry one extra
    // branch instead - the same trick `family == 0` has always used for cobblestone.
    {BlockId::WaxedCutCopper, "Waxed Cut Copper Stairs"},
    {BlockId::WaxedExposedCutCopper, "Waxed Exposed Cut Copper Stairs"},
    {BlockId::WaxedWeatheredCutCopper, "Waxed Weathered Cut Copper Stairs"},
    {BlockId::WaxedOxidizedCutCopper, "Waxed Oxidized Cut Copper Stairs"},
}};

/// Every material slabs come in, **stone first** for the same reason - the two
/// stone-slab ids predate the run.
///
/// **The same disk cost as `kStairFamilies` above, and for the same reason.**
/// `SlabRunLast` is computed from `kSlabFamilyCount`, so one more slab family
/// lengthens the run by two and shifts every enumerator above it into different
/// numbers than the ones in saved chunks. Read that comment before adding a row
/// here; a family is never just a row.
constexpr std::array<ShapedFamily, kSlabFamilyCount> kSlabFamilies{{
    {BlockId::Stone, "Stone Slab"},
    {BlockId::Cobblestone, "Cobblestone Slab"},
    {BlockId::MossyCobblestone, "Mossy Cobblestone Slab"},
    {BlockId::StoneBricks, "Stone Brick Slab"},
    {BlockId::MossyStoneBricks, "Mossy Stone Brick Slab"},
    {BlockId::Granite, "Granite Slab"},
    {BlockId::PolishedGranite, "Polished Granite Slab"},
    {BlockId::Diorite, "Diorite Slab"},
    {BlockId::PolishedDiorite, "Polished Diorite Slab"},
    {BlockId::Andesite, "Andesite Slab"},
    {BlockId::PolishedAndesite, "Polished Andesite Slab"},
    {BlockId::CobbledDeepslate, "Cobbled Deepslate Slab"},
    {BlockId::PolishedDeepslate, "Polished Deepslate Slab"},
    {BlockId::DeepslateBricks, "Deepslate Brick Slab"},
    {BlockId::DeepslateTiles, "Deepslate Tile Slab"},
    {BlockId::Bricks, "Brick Slab"},
    {BlockId::MudBricks, "Mud Brick Slab"},
    {BlockId::Sandstone, "Sandstone Slab"},
    {BlockId::SmoothSandstone, "Smooth Sandstone Slab"},
    {BlockId::RedSandstone, "Red Sandstone Slab"},
    {BlockId::SmoothRedSandstone, "Smooth Red Sandstone Slab"},
    {BlockId::Prismarine, "Prismarine Slab"},
    {BlockId::PrismarineBricks, "Prismarine Brick Slab"},
    {BlockId::DarkPrismarine, "Dark Prismarine Slab"},
    {BlockId::PurpurBlock, "Purpur Slab"},
    {BlockId::QuartzBlock, "Quartz Slab"},
    {BlockId::SmoothQuartz, "Smooth Quartz Slab"},
    {BlockId::NetherBricks, "Nether Brick Slab"},
    {BlockId::RedNetherBricks, "Red Nether Brick Slab"},
    {BlockId::Blackstone, "Blackstone Slab"},
    {BlockId::PolishedBlackstone, "Polished Blackstone Slab"},
    {BlockId::PolishedBlackstoneBricks, "Polished Blackstone Brick Slab"},
    {BlockId::EndStoneBricks, "End Stone Brick Slab"},
    {BlockId::Tuff, "Tuff Slab"},
    {BlockId::PolishedTuff, "Polished Tuff Slab"},
    {BlockId::TuffBricks, "Tuff Brick Slab"},
    {BlockId::CutCopper, "Cut Copper Slab"},
    {BlockId::ExposedCutCopper, "Exposed Cut Copper Slab"},
    {BlockId::WeatheredCutCopper, "Weathered Cut Copper Slab"},
    {BlockId::OxidizedCutCopper, "Oxidized Cut Copper Slab"},
    {BlockId::Planks, "Oak Slab"},
    {BlockId::SprucePlanks, "Spruce Slab"},
    {BlockId::BirchPlanks, "Birch Slab"},
    {BlockId::JunglePlanks, "Jungle Slab"},
    {BlockId::AcaciaPlanks, "Acacia Slab"},
    {BlockId::DarkOakPlanks, "Dark Oak Slab"},
    {BlockId::CherryPlanks, "Cherry Slab"},
    {BlockId::MangrovePlanks, "Mangrove Slab"},
    {BlockId::CrimsonPlanks, "Crimson Slab"},
    {BlockId::WarpedPlanks, "Warped Slab"},
    {BlockId::BambooPlanks, "Bamboo Slab"},
    {BlockId::BambooMosaic, "Bamboo Mosaic Slab"},
    // The three the reference cuts into halves but never into steps.
    {BlockId::SmoothStone, "Smooth Stone Slab"},
    {BlockId::CutSandstone, "Cut Sandstone Slab"},
    {BlockId::CutRedSandstone, "Cut Red Sandstone Slab"},
    // The four waxed cut coppers, in the tail run rather than in `SlabRun` - see the
    // matching note in `kStairFamilies`. Two ids each, allocated by position, so
    // adding them inside the run would have renumbered every enumerator above it.
    {BlockId::WaxedCutCopper, "Waxed Cut Copper Slab"},
    {BlockId::WaxedExposedCutCopper, "Waxed Exposed Cut Copper Slab"},
    {BlockId::WaxedWeatheredCutCopper, "Waxed Weathered Cut Copper Slab"},
    {BlockId::WaxedOxidizedCutCopper, "Waxed Oxidized Cut Copper Slab"},
}};

/// Every material a wall comes in. **Rock only** - the reference has no wooden
/// wall, because a fence already is one.
constexpr std::array<ShapedFamily, kWallFamilyCount> kWallFamilies{{
    {BlockId::Cobblestone, "Cobblestone Wall"},
    {BlockId::MossyCobblestone, "Mossy Cobblestone Wall"},
    {BlockId::StoneBricks, "Stone Brick Wall"},
    {BlockId::MossyStoneBricks, "Mossy Stone Brick Wall"},
    {BlockId::Granite, "Granite Wall"},
    {BlockId::Diorite, "Diorite Wall"},
    {BlockId::Andesite, "Andesite Wall"},
    {BlockId::CobbledDeepslate, "Cobbled Deepslate Wall"},
    {BlockId::PolishedDeepslate, "Polished Deepslate Wall"},
    {BlockId::DeepslateBricks, "Deepslate Brick Wall"},
    {BlockId::DeepslateTiles, "Deepslate Tile Wall"},
    {BlockId::Bricks, "Brick Wall"},
    {BlockId::MudBricks, "Mud Brick Wall"},
    {BlockId::Sandstone, "Sandstone Wall"},
    {BlockId::RedSandstone, "Red Sandstone Wall"},
    {BlockId::Prismarine, "Prismarine Wall"},
    {BlockId::NetherBricks, "Nether Brick Wall"},
    {BlockId::RedNetherBricks, "Red Nether Brick Wall"},
    {BlockId::Blackstone, "Blackstone Wall"},
    {BlockId::PolishedBlackstone, "Polished Blackstone Wall"},
    {BlockId::PolishedBlackstoneBricks, "Polished Blackstone Brick Wall"},
    {BlockId::EndStoneBricks, "End Stone Brick Wall"},
    {BlockId::Tuff, "Tuff Wall"},
    {BlockId::PolishedTuff, "Polished Tuff Wall"},
    {BlockId::TuffBricks, "Tuff Brick Wall"},
}};

/// Every fence, **oak first** because its id predates the run. Nether brick is
/// the one fence the reference makes out of rock, and the reference refuses to
/// join it to the wooden ones.
///
/// **Ours joins them, and this table cannot stop it.** An earlier note here
/// claimed we got the refusal "for free, because connection is a shape test
/// rather than a material one" - which is exactly backwards: a shape test is
/// precisely the thing that *cannot* express a material distinction, and every
/// fence in this table carries `BlockShape::Fence`, so `shapeReaches` joins the
/// nether brick one to all eleven wooden ones. Closing it means handing
/// `shapeReaches` the asking block's id rather than only its shape, which
/// changes `connectionBits` and both of its callers, so it is not a change this
/// table can make on its own.
constexpr std::array<ShapedFamily, kFenceFamilyCount> kFenceFamilies{{
    {BlockId::Planks, "Oak Fence"},
    {BlockId::SprucePlanks, "Spruce Fence"},
    {BlockId::BirchPlanks, "Birch Fence"},
    {BlockId::JunglePlanks, "Jungle Fence"},
    {BlockId::AcaciaPlanks, "Acacia Fence"},
    {BlockId::DarkOakPlanks, "Dark Oak Fence"},
    {BlockId::CherryPlanks, "Cherry Fence"},
    {BlockId::MangrovePlanks, "Mangrove Fence"},
    {BlockId::CrimsonPlanks, "Crimson Fence"},
    {BlockId::WarpedPlanks, "Warped Fence"},
    {BlockId::BambooPlanks, "Bamboo Fence"},
    {BlockId::NetherBricks, "Nether Brick Fence"},
}};

/// Every gate. The reference has no nether brick gate, so this is the fence
/// list one short.
constexpr std::array<ShapedFamily, kGateFamilyCount> kGateFamilies{{
    {BlockId::Planks, "Oak Fence Gate"},
    {BlockId::SprucePlanks, "Spruce Fence Gate"},
    {BlockId::BirchPlanks, "Birch Fence Gate"},
    {BlockId::JunglePlanks, "Jungle Fence Gate"},
    {BlockId::AcaciaPlanks, "Acacia Fence Gate"},
    {BlockId::DarkOakPlanks, "Dark Oak Fence Gate"},
    {BlockId::CherryPlanks, "Cherry Fence Gate"},
    {BlockId::MangrovePlanks, "Mangrove Fence Gate"},
    {BlockId::CrimsonPlanks, "Crimson Fence Gate"},
    {BlockId::WarpedPlanks, "Warped Fence Gate"},
    {BlockId::BambooPlanks, "Bamboo Fence Gate"},
}};

/// A carpet is its wool laid flat, in the reference's own colour order.
constexpr std::array<ShapedFamily, kCarpetFamilyCount> kCarpetFamilies{{
    {BlockId::WhiteWool, "White Carpet"},
    {BlockId::OrangeWool, "Orange Carpet"},
    {BlockId::MagentaWool, "Magenta Carpet"},
    {BlockId::LightBlueWool, "Light Blue Carpet"},
    {BlockId::YellowWool, "Yellow Carpet"},
    {BlockId::LimeWool, "Lime Carpet"},
    {BlockId::PinkWool, "Pink Carpet"},
    {BlockId::GrayWool, "Gray Carpet"},
    {BlockId::LightGrayWool, "Light Gray Carpet"},
    {BlockId::CyanWool, "Cyan Carpet"},
    {BlockId::PurpleWool, "Purple Carpet"},
    {BlockId::BlueWool, "Blue Carpet"},
    {BlockId::BrownWool, "Brown Carpet"},
    {BlockId::GreenWool, "Green Carpet"},
    {BlockId::RedWool, "Red Carpet"},
    {BlockId::BlackWool, "Black Carpet"},
}};

/// A pane is its glass on edge. Plain glass first, then the sixteen colours in
/// the same order everything else uses.
constexpr std::array<ShapedFamily, kPaneFamilyCount> kPaneFamilies{{
    {BlockId::Glass, "Glass Pane"},
    {BlockId::WhiteStainedGlass, "White Stained Glass Pane"},
    {BlockId::OrangeStainedGlass, "Orange Stained Glass Pane"},
    {BlockId::MagentaStainedGlass, "Magenta Stained Glass Pane"},
    {BlockId::LightBlueStainedGlass, "Light Blue Stained Glass Pane"},
    {BlockId::YellowStainedGlass, "Yellow Stained Glass Pane"},
    {BlockId::LimeStainedGlass, "Lime Stained Glass Pane"},
    {BlockId::PinkStainedGlass, "Pink Stained Glass Pane"},
    {BlockId::GrayStainedGlass, "Gray Stained Glass Pane"},
    {BlockId::LightGrayStainedGlass, "Light Gray Stained Glass Pane"},
    {BlockId::CyanStainedGlass, "Cyan Stained Glass Pane"},
    {BlockId::PurpleStainedGlass, "Purple Stained Glass Pane"},
    {BlockId::BlueStainedGlass, "Blue Stained Glass Pane"},
    {BlockId::BrownStainedGlass, "Brown Stained Glass Pane"},
    {BlockId::GreenStainedGlass, "Green Stained Glass Pane"},
    {BlockId::RedStainedGlass, "Red Stained Glass Pane"},
    {BlockId::BlackStainedGlass, "Black Stained Glass Pane"},
}};

constexpr bool isStairs(BlockId id) {
    return (id >= BlockId::CobbleStairs0 && id <= BlockId::CobbleStairs7) ||
           (id >= BlockId::StairsRunFirst && id <= BlockId::StairsRunLast) ||
           (id >= BlockId::WaxedCutCopperStairsRunFirst &&
            id <= BlockId::WaxedCutCopperStairsRunLast);
}

constexpr bool isSlab(BlockId id) {
    return id == BlockId::StoneSlab || id == BlockId::StoneSlabTop ||
           (id >= BlockId::SlabRunFirst && id <= BlockId::SlabRunLast) ||
           (id >= BlockId::WaxedCutCopperSlabRunFirst &&
            id <= BlockId::WaxedCutCopperSlabRunLast);
}

constexpr bool isWall(BlockId id) {
    return id >= BlockId::WallRunFirst && id <= BlockId::WallRunLast;
}

constexpr bool isFence(BlockId id) {
    return id == BlockId::PlanksFence ||
           (id >= BlockId::FenceRunFirst && id <= BlockId::FenceRunLast);
}

constexpr bool isFenceGate(BlockId id) {
    return id >= BlockId::GateRunFirst && id <= BlockId::GateRunLast;
}

constexpr bool isCarpet(BlockId id) {
    return id >= BlockId::CarpetRunFirst && id <= BlockId::CarpetRunLast;
}

/// Glass panes only. **Iron bars are the same shape and deliberately not in
/// this family**: they have their own texture rather than a parent's, so they
/// answer `blockShape` directly and nothing else about them is shared.
constexpr bool isPane(BlockId id) {
    return id >= BlockId::PaneRunFirst && id <= BlockId::PaneRunLast;
}

constexpr int carpetFamily(BlockId id) {
    return static_cast<int>(id) - static_cast<int>(BlockId::CarpetRunFirst);
}

constexpr BlockId carpetAt(int family) {
    return static_cast<BlockId>(static_cast<int>(BlockId::CarpetRunFirst) + family);
}

constexpr int paneFamily(BlockId id) {
    return static_cast<int>(id) - static_cast<int>(BlockId::PaneRunFirst);
}

constexpr BlockId paneAt(int family) {
    return static_cast<BlockId>(static_cast<int>(BlockId::PaneRunFirst) + family);
}

/// The glass whose art is partly clear *everywhere* rather than clear in
/// places: the sixteen stained ones, the tinted one, and the sixteen stained
/// panes that share their texture.
///
/// **This is the line between a cutout and a blend, and it is a fact about the
/// artwork rather than a taste.** Plain glass is a solid frame around a fully
/// clear middle - 191 of its 256 texels have no alpha at all - so throwing the
/// clear ones away draws it exactly right. These have no fully clear texel
/// anywhere and no fully opaque one either, so a cutout can only round them to
/// one or the other, and rounding up is what made every stained pane a solid
/// cube and tinted glass in particular a black block.
///
/// **The panes are in here because they are the same picture.** A pane is a
/// `ShapedFamily` whose parent is the block, so `shapedParent` hands it the
/// parent's texture layer - and once that art keeps its real alpha, a pane left
/// on the cutout path would have its 0.40 centre panel discarded and draw as a
/// frame with a hole in it. It has to travel with the block or not at all.
/// **Declared here rather than beside `isGlassBlock` for that reason alone**:
/// it needs `isPane` and `kPaneFamilies`, which are above.
constexpr bool isBlendedGlass(BlockId id) {
    if (isPane(id)) {
        return kPaneFamilies[static_cast<std::size_t>(paneFamily(id))].parent != BlockId::Glass;
    }
    return id == BlockId::TintedGlass ||
           (id >= BlockId::WhiteStainedGlass && id <= BlockId::BlackStainedGlass);
}

static_assert(isBlendedGlass(BlockId::TintedGlass) && !isBlendedGlass(BlockId::Glass),
              "plain glass has real holes and stays a cutout; tinted glass has none and cannot");
static_assert(isBlendedGlass(paneAt(1)) && !isBlendedGlass(paneAt(0)),
              "a stained pane wears the blended block's own art; a plain glass pane does not");

constexpr bool isLadder(BlockId id) {
    return id >= BlockId::LadderNorth && id <= BlockId::LadderWest;
}

constexpr bool isVine(BlockId id) {
    return id >= BlockId::VineFirst && id <= BlockId::VineLast;
}

/// Which sides this vine clings to, as a `ConnectionBits` mask.
constexpr std::uint8_t vineSides(BlockId id) {
    return static_cast<std::uint8_t>(static_cast<int>(id) - static_cast<int>(BlockId::VineFirst));
}

constexpr BlockId vineWith(std::uint8_t sides) {
    return static_cast<BlockId>(static_cast<int>(BlockId::VineFirst) + (sides & ConnectAll));
}

/// Everything you can climb. **One owner**, because the player's movement, the
/// support rules and the catalogue all have to agree, and a ladder and a vine
/// differ in nothing that matters to any of them.
///
/// **It was a ladder and a vine and nothing else, twenty ids in the whole
/// game**, which left every block whose *purpose* is vertical movement outside
/// it. Scaffolding is "a climbable semi-solid block" in the reference's own
/// opening sentence and is climbed by holding jump inside it; twisting vines
/// are "climbable upward-growing vegetation" and weeping vines "climbable,
/// downwards-growing" - between them the nether's only ladder substitute; and
/// cave vines are the lush caves' equivalent. None of them is a ladder or a
/// vine by shape, which is why a shape test could never have reached them.
constexpr bool isClimbable(BlockId id) {
    return isLadder(id) || isVine(id) || id == BlockId::Scaffolding ||
           id == BlockId::TwistingVines || id == BlockId::WeepingVines ||
           id == BlockId::CaveVines || id == BlockId::CaveVinesBerries;
}

// **Both halves of each pair, because this file's standing failure is a rule
// applied to one of two.** Delete `CaveVinesBerries` and the second fails while
// the first still passes, which is exactly the shape that has cost the most.
static_assert(isClimbable(BlockId::Scaffolding) && isClimbable(BlockId::TwistingVines) &&
                  isClimbable(BlockId::WeepingVines),
              "the three blocks whose whole purpose is climbing");
static_assert(isClimbable(BlockId::CaveVines) == isClimbable(BlockId::CaveVinesBerries),
              "a cave vine is climbable whether or not it is fruiting");
static_assert(!isClimbable(BlockId::Stone) && !isClimbable(BlockId::TallGrass),
              "and nothing else has quietly joined them");

constexpr bool isCocoa(BlockId id) {
    return id >= BlockId::CocoaFirst && id <= BlockId::CocoaLast;
}

/// 0, 1 or 2. Two is ripe and worth three beans; anything less gives one.
constexpr int cocoaAge(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::CocoaFirst)) / 4;
}

/// Which way the pod points **away** from the log it hangs on.
constexpr FaceDirection cocoaFacing(BlockId id) {
    switch ((static_cast<int>(id) - static_cast<int>(BlockId::CocoaFirst)) % 4) {
    case 1:
        return FaceDirection::PosX;
    case 2:
        return FaceDirection::PosZ;
    case 3:
        return FaceDirection::NegX;
    default:
        return FaceDirection::NegZ;
    }
}

constexpr BlockId cocoaAt(FaceDirection facing, int age) {
    int step = 0;
    switch (facing) {
    case FaceDirection::PosX:
        step = 1;
        break;
    case FaceDirection::PosZ:
        step = 2;
        break;
    case FaceDirection::NegX:
        step = 3;
        break;
    default:
        break;
    }
    const int clamped = age < 0 ? 0 : (age > 2 ? 2 : age);
    return static_cast<BlockId>(static_cast<int>(BlockId::CocoaFirst) + clamped * 4 + step);
}

static_assert(cocoaAge(cocoaAt(FaceDirection::PosZ, 2)) == 2 &&
                  cocoaFacing(cocoaAt(FaceDirection::PosZ, 2)) == FaceDirection::PosZ,
              "a pod's facing and its ripeness are one offset apart");
static_assert(vineSides(vineWith(ConnectNorth | ConnectWest)) == (ConnectNorth | ConnectWest));

/// Which wall a ladder is fixed to - the face it is **fastened against**, so a
/// ladder on the north wall of a room faces south into it.
constexpr FaceDirection ladderFacing(BlockId id) {
    switch (static_cast<int>(id) - static_cast<int>(BlockId::LadderNorth)) {
    case 1:
        return FaceDirection::PosX;
    case 2:
        return FaceDirection::PosZ;
    case 3:
        return FaceDirection::NegX;
    default:
        return FaceDirection::NegZ;
    }
}

constexpr BlockId ladderFacing(FaceDirection facing) {
    switch (facing) {
    case FaceDirection::PosX:
        return BlockId::LadderEast;
    case FaceDirection::PosZ:
        return BlockId::LadderSouth;
    case FaceDirection::NegX:
        return BlockId::LadderWest;
    default:
        return BlockId::LadderNorth;
    }
}

/// Which of the eight ids in its family a stair is: two bits of facing and one
/// of half, exactly as the single cobblestone run always encoded it.
///
/// **Three ranges, not two** - family 0 below the run, the run, and the tail run
/// at the end of the enum. The tail test has to be explicit and cannot be left to
/// fall through: a tail id is far *above* `StairsRunFirst`, so the run's own
/// arithmetic accepts it happily and returns a plausible, wrong offset.
constexpr int stairOffset(BlockId id) {
    if (id <= BlockId::CobbleStairs7) {
        return static_cast<int>(id) - static_cast<int>(BlockId::CobbleStairs0);
    }
    if (id >= BlockId::WaxedCutCopperStairsRunFirst &&
        id <= BlockId::WaxedCutCopperStairsRunLast) {
        return (static_cast<int>(id) -
                static_cast<int>(BlockId::WaxedCutCopperStairsRunFirst)) % 8;
    }
    return (static_cast<int>(id) - static_cast<int>(BlockId::StairsRunFirst)) % 8;
}

/// The run is numbered from one because cobblestone holds family 0; the tail
/// picks up at `kStairRunFamilyCount`, which is where its rows sit in
/// `kStairFamilies`.
constexpr int stairFamily(BlockId id) {
    if (id <= BlockId::CobbleStairs7) {
        return 0;
    }
    if (id >= BlockId::WaxedCutCopperStairsRunFirst &&
        id <= BlockId::WaxedCutCopperStairsRunLast) {
        return kStairRunFamilyCount + (static_cast<int>(id) -
                                       static_cast<int>(BlockId::WaxedCutCopperStairsRunFirst)) / 8;
    }
    return 1 + (static_cast<int>(id) - static_cast<int>(BlockId::StairsRunFirst)) / 8;
}

constexpr Facing stairFacing(BlockId id) { return static_cast<Facing>(stairOffset(id) & 3); }

/// Upside-down stairs, with the full half on top.
constexpr bool stairIsTop(BlockId id) { return (stairOffset(id) & 4) != 0; }

constexpr BlockId stairsAt(int family, Facing facing, bool top) {
    const int offset = static_cast<int>(facing) + (top ? 4 : 0);
    if (family == 0) {
        return static_cast<BlockId>(static_cast<int>(BlockId::CobbleStairs0) + offset);
    }
    if (family >= kStairRunFamilyCount) {
        return static_cast<BlockId>(static_cast<int>(BlockId::WaxedCutCopperStairsRunFirst) +
                                    (family - kStairRunFamilyCount) * 8 + offset);
    }
    return static_cast<BlockId>(static_cast<int>(BlockId::StairsRunFirst) + (family - 1) * 8 +
                                offset);
}

constexpr int slabFamily(BlockId id) {
    if (id <= BlockId::StoneSlabTop) {
        return 0;
    }
    if (id >= BlockId::WaxedCutCopperSlabRunFirst && id <= BlockId::WaxedCutCopperSlabRunLast) {
        return kSlabRunFamilyCount + (static_cast<int>(id) -
                                      static_cast<int>(BlockId::WaxedCutCopperSlabRunFirst)) / 2;
    }
    return 1 + (static_cast<int>(id) - static_cast<int>(BlockId::SlabRunFirst)) / 2;
}

/// Whether a half block occupies the upper half of its cell.
constexpr bool isUpperHalf(BlockId id) {
    if (id == BlockId::StoneSlabTop) {
        return true;
    }
    // **This test has to come before the range check below, not after it.** A
    // tail slab sits above `SlabRunLast`, so the guard would reject it and every
    // waxed slab would report itself a bottom half - wrong, and quietly.
    if (id >= BlockId::WaxedCutCopperSlabRunFirst && id <= BlockId::WaxedCutCopperSlabRunLast) {
        return ((static_cast<int>(id) -
                 static_cast<int>(BlockId::WaxedCutCopperSlabRunFirst)) & 1) != 0;
    }
    if (id < BlockId::SlabRunFirst || id > BlockId::SlabRunLast) {
        return false;
    }
    return ((static_cast<int>(id) - static_cast<int>(BlockId::SlabRunFirst)) & 1) != 0;
}

constexpr BlockId slabAt(int family, bool top) {
    if (family == 0) {
        return top ? BlockId::StoneSlabTop : BlockId::StoneSlab;
    }
    if (family >= kSlabRunFamilyCount) {
        return static_cast<BlockId>(static_cast<int>(BlockId::WaxedCutCopperSlabRunFirst) +
                                    (family - kSlabRunFamilyCount) * 2 + (top ? 1 : 0));
    }
    return static_cast<BlockId>(static_cast<int>(BlockId::SlabRunFirst) + (family - 1) * 2 +
                                (top ? 1 : 0));
}

/// **`isUpperHalf` as it stood before the tail run existed, kept only so the
/// assert below can reject it.** It returns `false` for anything outside
/// `SlabRun`, and every waxed slab is outside `SlabRun` - so under yesterday's
/// rule all eight of them were bottom halves and no waxed slab could be placed on
/// the upper half of a cell. Nothing calls this but the three asserts under it;
/// it exists because an assert that only shows the new rule working cannot show
/// the new rule was *needed*. Delete it only if the tail run itself goes away.
constexpr bool isUpperHalfBeforeTheTailRun(BlockId id) {
    if (id == BlockId::StoneSlabTop) {
        return true;
    }
    if (id < BlockId::SlabRunFirst || id > BlockId::SlabRunLast) {
        return false;
    }
    return ((static_cast<int>(id) - static_cast<int>(BlockId::SlabRunFirst)) & 1) != 0;
}

static_assert(!isUpperHalfBeforeTheTailRun(slabAt(kSlabFamilyCount - 1, true)),
              "negative control: the pre-tail rule must FAIL on a tail slab, or the assert "
              "below it proves nothing");
static_assert(isUpperHalf(slabAt(kSlabFamilyCount - 1, true)),
              "and the rule carrying the tail branch must get the same slab right");
static_assert(isUpperHalfBeforeTheTailRun(slabAt(kSlabRunFamilyCount - 1, true)) ==
                  isUpperHalf(slabAt(kSlabRunFamilyCount - 1, true)),
              "and the two differ in exactly one variable: on an in-run slab they agree, so "
              "the disagreement above is the tail branch and nothing else");

/// The round trips. Each runs the whole chain a real reader runs - a family index
/// through `stairsAt` to an id and back through `stairFamily` - rather than
/// comparing one half of the tail arithmetic against the other half, which would
/// pass while pointing at the wrong block.
static_assert(stairFamily(stairsAt(kStairRunFamilyCount, Facing::North, false)) ==
                  kStairRunFamilyCount,
              "the first tail stair family round-trips");
static_assert(stairFamily(stairsAt(kStairFamilyCount - 1, Facing::West, true)) ==
                  kStairFamilyCount - 1,
              "and so does the last");
static_assert(stairFacing(stairsAt(kStairFamilyCount - 1, Facing::West, true)) == Facing::West,
              "facing survives the tail arithmetic");
static_assert(stairIsTop(stairsAt(kStairFamilyCount - 1, Facing::West, true)),
              "and so does the half");
static_assert(!stairIsTop(stairsAt(kStairFamilyCount - 1, Facing::West, false)),
              "not vacuous: the same call with top=false disagrees");
static_assert(isStairs(stairsAt(kStairRunFamilyCount, Facing::North, false)) &&
                  isStairs(stairsAt(kStairFamilyCount - 1, Facing::West, true)),
              "isStairs accepts both ends of the tail run");
static_assert(slabFamily(slabAt(kSlabRunFamilyCount, false)) == kSlabRunFamilyCount,
              "the first tail slab family round-trips");
static_assert(slabFamily(slabAt(kSlabFamilyCount - 1, true)) == kSlabFamilyCount - 1,
              "and so does the last");
static_assert(isSlab(slabAt(kSlabFamilyCount - 1, true)) && !isUpperHalf(slabAt(kSlabFamilyCount - 1, false)),
              "isSlab accepts the tail, and the bottom half is still a bottom half");

/// The two asserts that catch a tail run sized wrong. Both tie a **reader's**
/// arithmetic to the enum's own extent, so a family count that disagreed with the
/// table, or an offset off by one, moves one side and not the other.
static_assert(static_cast<int>(stairsAt(kStairFamilyCount - 1, Facing::West, true)) ==
                  static_cast<int>(BlockId::WaxedCutCopperStairsRunLast),
              "the highest stair family's top-west variant is exactly the last id in the run");
static_assert(static_cast<int>(slabAt(kSlabFamilyCount - 1, true)) ==
                  static_cast<int>(BlockId::WaxedCutCopperSlabRunLast),
              "and the highest slab family's top half is exactly the last id in its run");

/// The chain through the family table, which is what `blockName` and
/// `shapedParent` actually read. A tail family that landed on the wrong row would
/// name a block correctly in the world and wrongly in the inventory.
static_assert(kStairFamilies[static_cast<std::size_t>(stairFamily(
                  stairsAt(kStairFamilyCount - 1, Facing::South, false)))]
                      .parent == BlockId::WaxedOxidizedCutCopper,
              "the last tail stair family reaches its own row in kStairFamilies");
static_assert(kSlabFamilies[static_cast<std::size_t>(
                  slabFamily(slabAt(kSlabRunFamilyCount, false)))]
                      .parent == BlockId::WaxedCutCopper,
              "and the first tail slab family reaches its own row in kSlabFamilies");

/// Why the tail exists at all, stated as something the compiler checks.
static_assert(static_cast<int>(BlockId::SlabRunFirst) ==
                  static_cast<int>(BlockId::StairsRunLast) + 1,
              "the slab run begins immediately after the stair run, which is precisely why "
              "lengthening the stair run in place would renumber it and everything above it");
static_assert(static_cast<int>(BlockId::WaxedCutCopperStairsRunFirst) >
                  static_cast<int>(BlockId::SlabRunLast),
              "and the tail runs sit above every earlier run, which is what makes them free");
static_assert(BlockId::WaxedCutCopperStairsRunLast <= kLastBlock &&
                  BlockId::WaxedCutCopperSlabRunLast <= kLastBlock,
              "kLastBlock must reach the newest run. Forgetting to move it is the failure its "
              "own comment records: every strided sweep then stops short and every array sized "
              "by kBlockIdCount is indexed out of bounds by the new ids");

constexpr int wallFamily(BlockId id) {
    return static_cast<int>(id) - static_cast<int>(BlockId::WallRunFirst);
}

constexpr BlockId wallAt(int family) {
    return static_cast<BlockId>(static_cast<int>(BlockId::WallRunFirst) + family);
}

constexpr int fenceFamily(BlockId id) {
    return id == BlockId::PlanksFence
               ? 0
               : 1 + static_cast<int>(id) - static_cast<int>(BlockId::FenceRunFirst);
}

constexpr BlockId fenceAt(int family) {
    return family == 0 ? BlockId::PlanksFence
                       : static_cast<BlockId>(static_cast<int>(BlockId::FenceRunFirst) + family - 1);
}

constexpr int gateOffset(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::GateRunFirst)) % 8;
}

constexpr int gateFamily(BlockId id) {
    return (static_cast<int>(id) - static_cast<int>(BlockId::GateRunFirst)) / 8;
}

/// Which way a gate's leaves face when it is shut. The gate itself spans the
/// perpendicular axis, which is what `gateBoxes` needs.
constexpr FaceDirection gateFacing(BlockId id) {
    switch (gateOffset(id) & 3) {
    case 1:
        return FaceDirection::PosX;
    case 2:
        return FaceDirection::PosZ;
    case 3:
        return FaceDirection::NegX;
    default:
        return FaceDirection::NegZ;
    }
}

constexpr bool gateIsOpen(BlockId id) { return (gateOffset(id) & 4) != 0; }

/// **The one place a gate's facing and its state are combined**, so swinging one
/// open can never quietly turn it round - which is exactly what a plain
/// `open ? A : B` would do.
constexpr BlockId gateAt(int family, FaceDirection facing, bool open) {
    int step = 0;
    switch (facing) {
    case FaceDirection::PosX:
        step = 1;
        break;
    case FaceDirection::PosZ:
        step = 2;
        break;
    case FaceDirection::NegX:
        step = 3;
        break;
    default:
        break;
    }
    return static_cast<BlockId>(static_cast<int>(BlockId::GateRunFirst) + family * 8 + step +
                                (open ? 4 : 0));
}

/// Every material a button comes in. **Declared in `kWoods` order with stone
/// last**, exactly as the door and trapdoor tables are, because the recipe loop
/// walks the woods and indexes straight across all three.
constexpr std::array<ShapedFamily, kButtonFamilyCount> kButtonFamilies{{
    {BlockId::Planks, "Oak Button"},
    {BlockId::SprucePlanks, "Spruce Button"},
    {BlockId::BirchPlanks, "Birch Button"},
    {BlockId::JunglePlanks, "Jungle Button"},
    {BlockId::AcaciaPlanks, "Acacia Button"},
    {BlockId::DarkOakPlanks, "Dark Oak Button"},
    {BlockId::CherryPlanks, "Cherry Button"},
    {BlockId::MangrovePlanks, "Mangrove Button"},
    {BlockId::CrimsonPlanks, "Crimson Button"},
    {BlockId::WarpedPlanks, "Warped Button"},
    {BlockId::BambooPlanks, "Bamboo Button"},
    {BlockId::Stone, "Stone Button"},
}};

/// The same twelve and then the two weighted plates, which are cut from gold
/// and iron and so borrow their pictures the same way everything else here does.
constexpr std::array<ShapedFamily, kPressurePlateFamilyCount> kPressurePlateFamilies{{
    {BlockId::Planks, "Oak Pressure Plate"},
    {BlockId::SprucePlanks, "Spruce Pressure Plate"},
    {BlockId::BirchPlanks, "Birch Pressure Plate"},
    {BlockId::JunglePlanks, "Jungle Pressure Plate"},
    {BlockId::AcaciaPlanks, "Acacia Pressure Plate"},
    {BlockId::DarkOakPlanks, "Dark Oak Pressure Plate"},
    {BlockId::CherryPlanks, "Cherry Pressure Plate"},
    {BlockId::MangrovePlanks, "Mangrove Pressure Plate"},
    {BlockId::CrimsonPlanks, "Crimson Pressure Plate"},
    {BlockId::WarpedPlanks, "Warped Pressure Plate"},
    {BlockId::BambooPlanks, "Bamboo Pressure Plate"},
    {BlockId::Stone, "Stone Pressure Plate"},
    {BlockId::GoldBlock, "Light Weighted Pressure Plate"},
    {BlockId::IronBlock, "Heavy Weighted Pressure Plate"},
}};

static_assert(kPressurePlateFamilies[kPressurePlateFamilyCount - 2].parent == BlockId::GoldBlock &&
                  kPressurePlateFamilies[kPressurePlateFamilyCount - 1].parent ==
                      BlockId::IronBlock,
              "isWeightedPlate names the last two families; keep gold and iron there");
static_assert(kPressurePlateFamilies[11].parent == BlockId::Stone,
              "plateAnswersToItems names family 11 as the stone one");

/// A sign is a board of its own planks on a post of the same.
///
/// > **Named divergence: a sign wears its wood's planks, not the reference's
/// > own sign texture.** Those ship at 32x32 - every wood's does - and this
/// > game's texture array is 16x16 throughout. The honest options were a second
/// > array at twice the size or a downscale, and a downscale is exactly the
/// > thing `make-reference-blocks.ps1` refuses to do. Planks are what a sign is
/// > made of, and they cost no art at all.
constexpr std::array<ShapedFamily, kSignFamilyCount> kSignFamilies{{
    {BlockId::Planks, "Oak Sign"},
    {BlockId::SprucePlanks, "Spruce Sign"},
    {BlockId::BirchPlanks, "Birch Sign"},
    {BlockId::JunglePlanks, "Jungle Sign"},
    {BlockId::AcaciaPlanks, "Acacia Sign"},
    {BlockId::DarkOakPlanks, "Dark Oak Sign"},
    {BlockId::CherryPlanks, "Cherry Sign"},
    {BlockId::MangrovePlanks, "Mangrove Sign"},
    {BlockId::CrimsonPlanks, "Crimson Sign"},
    {BlockId::WarpedPlanks, "Warped Sign"},
    {BlockId::BambooPlanks, "Bamboo Sign"},
}};

constexpr std::array<ShapedFamily, kSignFamilyCount> kHangingSignFamilies{{
    {BlockId::Planks, "Oak Hanging Sign"},
    {BlockId::SprucePlanks, "Spruce Hanging Sign"},
    {BlockId::BirchPlanks, "Birch Hanging Sign"},
    {BlockId::JunglePlanks, "Jungle Hanging Sign"},
    {BlockId::AcaciaPlanks, "Acacia Hanging Sign"},
    {BlockId::DarkOakPlanks, "Dark Oak Hanging Sign"},
    {BlockId::CherryPlanks, "Cherry Hanging Sign"},
    {BlockId::MangrovePlanks, "Mangrove Hanging Sign"},
    {BlockId::CrimsonPlanks, "Crimson Hanging Sign"},
    {BlockId::WarpedPlanks, "Warped Hanging Sign"},
    {BlockId::BambooPlanks, "Bamboo Hanging Sign"},
}};

/// A banner is a sheet of its own wool on a post.
///
/// > **Named divergence, and a bigger one: a banner carries no pattern.** The
/// > reference has no block model for one at all - it is drawn by a block-entity
/// > renderer that composites up to six tinted masks out of
/// > `textures/entity/banner/`. Patterns need the loom, a per-banner payload and
/// > a compositing pass; the cloth is what a banner *is*, and it is what this
/// > ships.
constexpr std::array<ShapedFamily, kBannerFamilyCount> kBannerFamilies{{
    {BlockId::WhiteWool, "White Banner"},
    {BlockId::OrangeWool, "Orange Banner"},
    {BlockId::MagentaWool, "Magenta Banner"},
    {BlockId::LightBlueWool, "Light Blue Banner"},
    {BlockId::YellowWool, "Yellow Banner"},
    {BlockId::LimeWool, "Lime Banner"},
    {BlockId::PinkWool, "Pink Banner"},
    {BlockId::GrayWool, "Gray Banner"},
    {BlockId::LightGrayWool, "Light Gray Banner"},
    {BlockId::CyanWool, "Cyan Banner"},
    {BlockId::PurpleWool, "Purple Banner"},
    {BlockId::BlueWool, "Blue Banner"},
    {BlockId::BrownWool, "Brown Banner"},
    {BlockId::GreenWool, "Green Banner"},
    {BlockId::RedWool, "Red Banner"},
    {BlockId::BlackWool, "Black Banner"},
}};

/// The block a cut shape is made of, or the block itself for everything else.
///
/// **This is the single reason six hundred and forty blocks needed no entry in
/// any texture, tool, hardness or blast table.** Every one of those questions is
/// forwarded to the parent, and a parent is never itself a cut shape, so the
/// forwarding is one step deep by construction.
constexpr BlockId shapedParent(BlockId id) {
    if (isStairs(id)) {
        return kStairFamilies[static_cast<std::size_t>(stairFamily(id))].parent;
    }
    if (isSlab(id)) {
        return kSlabFamilies[static_cast<std::size_t>(slabFamily(id))].parent;
    }
    if (isWall(id)) {
        return kWallFamilies[static_cast<std::size_t>(wallFamily(id))].parent;
    }
    if (isFence(id)) {
        return kFenceFamilies[static_cast<std::size_t>(fenceFamily(id))].parent;
    }
    if (isFenceGate(id)) {
        return kGateFamilies[static_cast<std::size_t>(gateFamily(id))].parent;
    }
    if (isCarpet(id)) {
        return kCarpetFamilies[static_cast<std::size_t>(carpetFamily(id))].parent;
    }
    if (isPane(id)) {
        return kPaneFamilies[static_cast<std::size_t>(paneFamily(id))].parent;
    }
    if (isButton(id)) {
        return kButtonFamilies[static_cast<std::size_t>(buttonFamily(id))].parent;
    }
    if (isPressurePlate(id)) {
        return kPressurePlateFamilies[static_cast<std::size_t>(pressurePlateFamily(id))].parent;
    }
    if (isSign(id)) {
        return kSignFamilies[static_cast<std::size_t>(signFamily(id))].parent;
    }
    if (isHangingSign(id)) {
        return kHangingSignFamilies[static_cast<std::size_t>(signFamily(id))].parent;
    }
    if (isBanner(id)) {
        return kBannerFamilies[static_cast<std::size_t>(signFamily(id))].parent;
    }
    return id;
}

/// How many cuts a stonecutter offers for one block, at most. Stairs, two slabs
/// and a wall, which is every shaped family a stone can belong to.
constexpr int kStonecutterOptions = 3;

/// One cut of `parent`, or `Air` past the last one it has.
///
/// **A reverse lookup over the family tables rather than a table of its own.**
/// Which cuts a block has is already stated by the families naming it as their
/// parent, and a second list could only fall out of step with the first - a new
/// stair material would silently offer nothing here.
constexpr BlockId stonecutterOption(BlockId parent, int option) {
    if (parent == BlockId::Air || shapedParent(parent) != parent) {
        return BlockId::Air;
    }
    if (option == 0) {
        for (int family = 0; family < kStairFamilyCount; ++family) {
            if (kStairFamilies[static_cast<std::size_t>(family)].parent == parent) {
                return stairsAt(family, Facing::North, false);
            }
        }
        return BlockId::Air;
    }
    if (option == 1) {
        for (int family = 0; family < kSlabFamilyCount; ++family) {
            if (kSlabFamilies[static_cast<std::size_t>(family)].parent == parent) {
                return slabAt(family, false);
            }
        }
        return BlockId::Air;
    }
    for (int family = 0; family < kWallFamilyCount; ++family) {
        if (kWallFamilies[static_cast<std::size_t>(family)].parent == parent) {
            return wallAt(family);
        }
    }
    return BlockId::Air;
}

/// How many of a cut one block yields. The reference's own: two slabs, one of
/// everything else.
constexpr int stonecutterYield(int option) {
    return option == 1 ? 2 : 1;
}

static_assert(stonecutterOption(BlockId::Stone, 0) != BlockId::Air &&
                  stonecutterOption(BlockId::Stone, 1) != BlockId::Air,
              "stone must cut into stairs and slabs, or the stonecutter offers nothing");
static_assert(stonecutterOption(stonecutterOption(BlockId::Stone, 0), 0) == BlockId::Air,
              "a cut block cannot be cut again");

/// True for anything cut from another block.
constexpr bool isShapedBlock(BlockId id) {
    return isStairs(id) || isSlab(id) || isWall(id) || isFence(id) || isFenceGate(id) ||
           isCarpet(id) || isPane(id) || isButton(id) || isPressurePlate(id) || isSignLike(id);
}

/// The id an item of this family turns back into: the one a recipe produces and
/// the one that is dropped, whichever placement state was broken.
constexpr BlockId shapedCanonical(BlockId id) {
    if (isStairs(id)) {
        return stairsAt(stairFamily(id), Facing::North, false);
    }
    if (isSlab(id)) {
        return slabAt(slabFamily(id), false);
    }
    if (isFenceGate(id)) {
        return gateAt(gateFamily(id), FaceDirection::NegZ, false);
    }
    // A button and a plate spend most of their ids on where they hang and
    // whether they are down; only the quiet floor-mounted one is an item.
    if (isButton(id)) {
        return buttonAt(buttonFamily(id), 0, false);
    }
    if (isPressurePlate(id)) {
        return pressurePlateAt(pressurePlateFamily(id), 0);
    }
    // Which way a sign points is placement, not an item: every rotation and
    // every wall it could be nailed to gives back the one that stands facing
    // south.
    if (isSignLike(id)) {
        return signAt(signKind(id), signFamily(id), FaceDirection::NegZ, false);
    }
    return id;
}

static_assert(stairFamily(BlockId::CobbleStairs5) == 0 && stairFacing(BlockId::CobbleStairs5) == Facing::East &&
                  stairIsTop(BlockId::CobbleStairs5),
              "the legacy cobblestone run must still decode exactly as it did");
static_assert(stairsAt(0, Facing::East, true) == BlockId::CobbleStairs5);
static_assert(stairsAt(3, Facing::South, true) != stairsAt(3, Facing::South, false));
static_assert(stairFamily(stairsAt(17, Facing::West, true)) == 17 &&
              stairFacing(stairsAt(17, Facing::West, true)) == Facing::West &&
              stairIsTop(stairsAt(17, Facing::West, true)));
static_assert(slabAt(0, true) == BlockId::StoneSlabTop && slabAt(0, false) == BlockId::StoneSlab);
static_assert(slabFamily(slabAt(40, true)) == 40 && isUpperHalf(slabAt(40, true)) &&
              !isUpperHalf(slabAt(40, false)));
static_assert(fenceAt(0) == BlockId::PlanksFence && fenceFamily(fenceAt(7)) == 7);
static_assert(wallFamily(wallAt(11)) == 11);
static_assert(gateFamily(gateAt(6, FaceDirection::PosZ, true)) == 6 &&
              gateFacing(gateAt(6, FaceDirection::PosZ, true)) == FaceDirection::PosZ &&
              gateIsOpen(gateAt(6, FaceDirection::PosZ, true)) &&
              !gateIsOpen(gateAt(6, FaceDirection::PosZ, false)));
static_assert(shapedParent(BlockId::CobbleStairs0) == BlockId::Cobblestone &&
                  shapedParent(BlockId::StoneSlab) == BlockId::Stone &&
                  shapedParent(BlockId::PlanksFence) == BlockId::Planks &&
                  shapedParent(BlockId::Stone) == BlockId::Stone,
              "the three legacy shapes must map onto the materials they always were");
static_assert(!isShapedBlock(shapedParent(BlockId::GateRunLast)),
              "a parent must never itself be a cut shape, or the forwarding recurses");
static_assert(shapedParent(carpetAt(9)) == BlockId::CyanWool &&
                  shapedParent(paneAt(0)) == BlockId::Glass &&
                  shapedParent(paneAt(16)) == BlockId::BlackStainedGlass,
              "a carpet is its wool and a pane is its glass, colour for colour");

/// What geometry a block actually occupies.
///
/// Everything before M17b was a unit cube, and both meshing and collision
/// assumed it. A shape is the one place that assumption is now written down.
enum class BlockShape : std::uint8_t {
    /// Occupies nothing: air, and anything you walk straight through.
    Empty,
    Full,
    /// Two quads on the diagonals, drawn from both sides. Plants.
    Cross,
    /// Bottom half of the cube.
    Slab,
    /// A half block with a quarter step on top of it.
    Stairs,
    /// A post that grows arms toward whatever it touches.
    Fence,
    /// The same idea in stone: a fatter post and stubbier arms. Kept apart from
    /// `Fence` rather than folded into it, because the two have different box
    /// tables and neither connects to the other.
    Wall,
    /// A sheet two texels thick that grows toward its neighbours. Glass panes
    /// and iron bars.
    Pane,
    /// An assembly of small boxes, each carrying **its own rectangle of the
    /// texture and optionally its own layer**. A lantern, an end rod, a torch,
    /// a bell in its frame, a cauldron, a composter, a grindstone, a
    /// stonecutter and a brewing stand.
    ///
    /// Everything else here is *cut out of a cube*, so where a box sits is also
    /// where it samples from. These are not, which is the whole distinction —
    /// and it is also what stops them occluding their neighbours' faces, since
    /// only a `Full` shape does that. Left as a full cube, a cauldron culled
    /// the faces of everything touching it and you could see through the world
    /// around it.
    Model,
    /// A rung ladder flat against one wall, with no collision at all - what it
    /// gives you is a way *up*, not a floor.
    Ladder,
    /// The same, on as many of the four walls as it clings to at once, plus a
    /// sheet under the ceiling when there is one.
    Vine,
    /// A pod hanging off the side of a log, growing through three sizes.
    Cocoa,
    /// A gate in a fence. Its facing says which way the leaves point when it is
    /// shut, and the open form has a gap you walk through.
    Gate,
    /// A slightly shrunken cube lifted just clear of the floor. Lit TNT, which
    /// the reference turns into an entity the moment it is struck.
    Hovering,
    /// A door leaf: a thin slab standing against one wall of its cell, which
    /// swings a quarter turn to the wall beside it when opened.
    Door,
    /// The same leaf lying flat, in the top or bottom of its cell, which swings
    /// up to stand against one wall.
    Trapdoor,
    /// A full-width plate nine sixteenths tall. A bed, which you stand on and
    /// sleep in and which is a sixteenth under a slab's height.
    Bed,
    /// A full-width block one texel short of the ceiling. Tilled ground and a
    /// trodden path, which the reference sinks by a sixteenth - that step is
    /// what makes a field read as worked rather than painted, and it is also
    /// why a torch will not stand on one.
    Tilled,
    /// A full-width plate lying on the floor of its cell. The lily pad, which
    /// **rests on** the water rather than standing in it - the one support in
    /// the game that is not solid.
    Flat,
    /// A small nub on whichever face it was stuck to, and a sixteenth shallower
    /// once it is pushed in. Cut out of its material like a stair is, so where
    /// the box sits is also where it samples from.
    Button,
    /// A fourteen-wide plate a texel thick, half that when something stands on
    /// it. Cut out of its material for the same reason.
    Plate,
    /// A board on a post, a board on a wall, a board hung off a ceiling, or a
    /// sheet of cloth. Cut out of its material like the two above, which is why
    /// a sign wears its own planks and a banner its own wool.
    Sign,
};

constexpr BlockShape blockShape(BlockId id) {
    if (id == BlockId::Air || isFluid(id)) {
        return BlockShape::Empty;
    }
    // Redstone first, because several of these are cut out of a material whose
    // own shape would otherwise answer for them, and because the wire has to be
    // reached before anything asks whether it is a plant.
    if (isButton(id)) {
        return BlockShape::Button;
    }
    if (isPressurePlate(id)) {
        return BlockShape::Plate;
    }
    if (isSignLike(id)) {
        return BlockShape::Sign;
    }
    // Wire, rails and a tripwire are all a sheet lying on the floor; only their
    // heights differ, and `flatHeight` owns those.
    if (isRedstoneWire(id) || isRail(id) || isTripwire(id)) {
        return BlockShape::Flat;
    }
    if (isLever(id) || isRepeater(id) || isComparator(id) || isPistonHead(id) ||
        isDaylightDetector(id) || isLightningRod(id) || isTripwireHook(id)) {
        return BlockShape::Model;
    }
    // An observer, a dispenser, a dropper, a target, a note block and a piston
    // are all ordinary cubes; only which picture goes on which face differs.
    if (isObserver(id) || isDispenserLike(id) || isPiston(id)) {
        return BlockShape::Full;
    }
    // Fire takes the plant shape without being a plant: two crossed blades, no
    // collision, swept away by water and needing something under it. All four
    // of those fall out of `Cross` for free, which is why it is not `Empty`.
    if (id == BlockId::Fire) {
        return BlockShape::Cross;
    }
    // **Three families `isCrossBlock` still claims, drawn from their own
    // models.** It stays their owner for cutout, sound, mining and the
    // catalogue - all four want the plant's answer - but the reference draws
    // none of them on two diagonals: a candle is one to four two-texel sticks
    // (`template_candle` and its three siblings), bamboo is a two-texel stalk
    // (`bamboo1_age0.json`) and a sea pickle is a four-texel lump
    // (`sea_pickle.json`). Drawn as crosses they filled the whole cell, which
    // is 138 of the disagreements `check-models.ps1` reports.
    //
    // Answered here rather than by deleting them from `isCrossBlock`, whose
    // other three readers are right as they are - and the four predicates that
    // did key on the shape name them outright now, the way the torch already
    // does after the same move.
    if (isCandle(id) || id == BlockId::Bamboo || id == BlockId::SeaPickle) {
        return BlockShape::Model;
    }
    if (isCrossBlock(id)) {
        return BlockShape::Cross;
    }
    // Two that were cubes with a picture on top. A lectern is a plinth, a post
    // and a reading desk (`lectern.json`) and a dragon egg is six stacked
    // slabs (`dragon_egg.json`); neither fills its cell, and as `Full` both
    // culled the faces of everything they touched.
    if (id == BlockId::Lectern || id == BlockId::DragonEgg) {
        return BlockShape::Model;
    }
    if (isSnowLayer(id)) {
        return BlockShape::Flat;
    }
    if (isFarmland(id) || id == BlockId::DirtPath) {
        return BlockShape::Tilled;
    }
    if (isDoor(id)) {
        return BlockShape::Door;
    }
    if (isTrapdoor(id)) {
        return BlockShape::Trapdoor;
    }
    if (isBed(id)) {
        return BlockShape::Bed;
    }
    if (isSlab(id)) {
        return BlockShape::Slab;
    }
    if (isStairs(id)) {
        return BlockShape::Stairs;
    }
    if (isFence(id)) {
        return BlockShape::Fence;
    }
    if (isWall(id)) {
        return BlockShape::Wall;
    }
    if (isPane(id) || id == BlockId::IronBars) {
        return BlockShape::Pane;
    }
    if (id == BlockId::Lantern || id == BlockId::SoulLantern || id == BlockId::EndRod) {
        return BlockShape::Model;
    }
    // Everything that used to fall through to `Full` and come out as a cube
    // wearing a lid, plus the brewing stand, which was drawn as a flower.
    if (isTorchBlock(id) || isCauldron(id) || isComposter(id) || id == BlockId::Bell ||
        id == BlockId::Grindstone || id == BlockId::Stonecutter ||
        id == BlockId::BrewingStand) {
        return BlockShape::Model;
    }
    // Ten more that are shorter than their cell or not a box at all. Marking
    // them cutout stopped their art drawing as opaque black and immediately
    // showed the real fault underneath: a cube's worth of nothing above a
    // thirteen-texel frame reads as a gap. The reference's own
    // `models/block/*.json` gives every one of these box for box.
    if (isAnvil(id) || id == BlockId::EndPortalFrame || id == BlockId::EnchantingTable ||
        id == BlockId::SculkSensor || id == BlockId::SculkShrieker ||
        id == BlockId::Campfire || id == BlockId::SoulCampfire || id == BlockId::Scaffolding ||
        id == BlockId::FlowerPot || isHopper(id)) {
        return BlockShape::Model;
    }
    if (isLadder(id)) {
        return BlockShape::Ladder;
    }
    if (isVine(id)) {
        return BlockShape::Vine;
    }
    if (isCocoa(id)) {
        return BlockShape::Cocoa;
    }
    if (isCarpet(id)) {
        return BlockShape::Flat;
    }
    // Two more that lie on the floor rather than standing in the cell. Named
    // rather than folded into `isCarpet`, which is a material family the
    // recipes read - widening it would give both of these a wool's recipe.
    if (id == BlockId::MossCarpet || id == BlockId::SculkVein) {
        return BlockShape::Flat;
    }
    if (isFenceGate(id)) {
        return BlockShape::Gate;
    }
    // A charge that has been struck comes off the floor. Ours stays a block
    // where the reference makes it an entity, so the detachment has to be in
    // the shape or nothing shows it.
    if (id == BlockId::TntPrimed) {
        return BlockShape::Hovering;
    }
    // A pad lies on the water. **Answered last on purpose**, not before the
    // cross test as an older comment claimed: it used to be a cross block, and
    // standing it up in the cell is what made it fight the water. Reaching here
    // at all depends on `isCrossBlock` not naming it, which it does not.
    if (id == BlockId::LilyPad) {
        return BlockShape::Flat;
    }
    return BlockShape::Full;
}

/// Axis-aligned box in block-local space, each axis running 0 to 1.
struct BlockBox {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};

/// Whether the mesher has to build this block box by box rather than merging it
/// into the greedy mask.
///
/// **One owner for a list that used to be written out twice** - once to skip
/// these in the greedy pass and once to accept them in the shape pass - so a new
/// shape could be added to one and forgotten in the other, which draws the block
/// twice or not at all. `Empty` covers air and the fluids, which the greedy pass
/// handles itself.
constexpr bool usesShapePass(BlockShape shape) {
    return shape != BlockShape::Empty && shape != BlockShape::Full;
}

/// Every box a block occupies.
///
/// **The capacity is not the widest *cut-out* shape, it is the widest `postModel`
/// answer**, and getting that wrong wrote off the end of this array. It held nine
/// - a fence's post plus two rails toward each of four neighbours, which really
/// is the widest fence - while `drawnBoxes` copies `postModel` into it bounded by
/// the *model's* count, and a cauldron's model is ten boxes. Six ids overran a
/// stack object on the mesher's hot path, on a worker thread, every time one was
/// meshed. The `static_assert` under `ModelBoxes` is what stops it returning: the
/// two capacities are now tied, so growing a model to eleven boxes fails to
/// compile instead of scribbling. Truncating the loop instead was the other
/// one-line fix and is the wrong one - a cauldron would then draw and crack with
/// a box missing, silently.
struct BlockBoxes {
    BlockBox boxes[10]{};
    int count = 0;
};

// Shared by both fence shapes. The post matches the reference exactly, and the
// rails sit where its model puts them: 6-9 and 12-15 in sixteenths.
constexpr float kFencePostLow = 0.375f;
constexpr float kFencePostHigh = 0.625f;
constexpr float kFenceArmLow = 0.4375f;
constexpr float kFenceArmHigh = 0.5625f;
constexpr float kFenceLowRailBottom = 0.375f;
constexpr float kFenceLowRailTop = 0.5625f;
constexpr float kFenceHighRailBottom = 0.75f;
constexpr float kFenceHighRailTop = 0.9375f;

/// Post plus a solid full-height arm toward each connected side.
///
/// **Collision only.** What matters underfoot is that a line of fences is a
/// barrier; modelling the gap between the rails would let you squeeze through
/// it. `fenceRailBoxes` is what gets drawn.
///
/// **The reference is 1.5 blocks tall here and this is 1.0**, so a fence can be
/// jumped. Raising these to 1.5 on its own does nothing: `overlapsSolid` walks
/// cells from `floor(min.y)` upward, so a player whose feet are at the top of
/// the fence never looks at the cell the fence is in and passes straight over
/// the extra half block. The broad phase has to scan one cell lower first, and
/// that lives in `Collision.hpp`, not here. The same applies to `wallBoxes` and
/// `gateBoxes` below.
constexpr BlockBoxes fenceBoxes(std::uint8_t connections) {
    BlockBoxes result;
    result.boxes[result.count++] = {kFencePostLow, 0.0f, kFencePostLow, kFencePostHigh, 1.0f, kFencePostHigh};

    if ((connections & ConnectNorth) != 0) {
        result.boxes[result.count++] = {kFenceArmLow, 0.0f, 0.0f, kFenceArmHigh, 1.0f, kFencePostLow};
    }
    if ((connections & ConnectSouth) != 0) {
        result.boxes[result.count++] = {kFenceArmLow, 0.0f, kFencePostHigh, kFenceArmHigh, 1.0f, 1.0f};
    }
    if ((connections & ConnectWest) != 0) {
        result.boxes[result.count++] = {0.0f, 0.0f, kFenceArmLow, kFencePostLow, 1.0f, kFenceArmHigh};
    }
    if ((connections & ConnectEast) != 0) {
        result.boxes[result.count++] = {kFencePostHigh, 0.0f, kFenceArmLow, 1.0f, 1.0f, kFenceArmHigh};
    }
    return result;
}

/// Post plus two thin rails toward each connected side.
///
/// **Drawing only.** A fence you can see through is most of what makes it read
/// as a fence rather than as a thin wall, and this is the one place drawn
/// geometry and collision geometry deliberately disagree.
constexpr BlockBoxes fenceRailBoxes(std::uint8_t connections) {
    BlockBoxes result;
    result.boxes[result.count++] = {kFencePostLow, 0.0f, kFencePostLow, kFencePostHigh, 1.0f, kFencePostHigh};

    const auto addRails = [&result](float minX, float minZ, float maxX, float maxZ) {
        result.boxes[result.count++] = {minX, kFenceLowRailBottom, minZ, maxX, kFenceLowRailTop, maxZ};
        result.boxes[result.count++] = {minX, kFenceHighRailBottom, minZ, maxX, kFenceHighRailTop, maxZ};
    };

    if ((connections & ConnectNorth) != 0) {
        addRails(kFenceArmLow, 0.0f, kFenceArmHigh, kFencePostLow);
    }
    if ((connections & ConnectSouth) != 0) {
        addRails(kFenceArmLow, kFencePostHigh, kFenceArmHigh, 1.0f);
    }
    if ((connections & ConnectWest) != 0) {
        addRails(0.0f, kFenceArmLow, kFencePostLow, kFenceArmHigh);
    }
    if ((connections & ConnectEast) != 0) {
        addRails(kFencePostHigh, kFenceArmLow, 1.0f, kFenceArmHigh);
    }
    return result;
}

/// Lit TNT's box. A texel of clearance underneath is enough to read as "come
/// off the floor" without the charge looking like it is flying.
constexpr float kHoverLift = 0.0625f;
constexpr float kHoverInset = 0.03125f;

// The reference's wall model: a fat 4-12 post the full height of the cell, and
// 5-11 arms that stop two texels short of the top so a wall reads as lower than
// the fence it is made of stone to replace.
constexpr float kWallPostLow = 0.25f;
constexpr float kWallPostHigh = 0.75f;
constexpr float kWallArmLow = 0.3125f;
constexpr float kWallArmHigh = 0.6875f;
constexpr float kWallArmTop = 0.875f;

/// Post plus an arm toward each connected side. Unlike a fence there is no
/// separate drawn form: a wall is solid all the way up, so what you see and what
/// you bump into are the same boxes.
constexpr BlockBoxes wallBoxes(std::uint8_t connections) {
    BlockBoxes result;
    result.boxes[result.count++] = {kWallPostLow, 0.0f,  kWallPostLow,
                                    kWallPostHigh, 1.0f, kWallPostHigh};

    if ((connections & ConnectNorth) != 0) {
        result.boxes[result.count++] = {kWallArmLow, 0.0f, 0.0f, kWallArmHigh, kWallArmTop, kWallPostLow};
    }
    if ((connections & ConnectSouth) != 0) {
        result.boxes[result.count++] = {kWallArmLow, 0.0f, kWallPostHigh, kWallArmHigh, kWallArmTop, 1.0f};
    }
    if ((connections & ConnectWest) != 0) {
        result.boxes[result.count++] = {0.0f, 0.0f, kWallArmLow, kWallPostLow, kWallArmTop, kWallArmHigh};
    }
    if ((connections & ConnectEast) != 0) {
        result.boxes[result.count++] = {kWallPostHigh, 0.0f, kWallArmLow, 1.0f, kWallArmTop, kWallArmHigh};
    }
    return result;
}

/// A gate's posts and, while it is shut, the two rails between them.
///
/// **The posts stay put when it opens** - only the leaves swing away. That is
/// what leaves a doorway you can walk through while the gate still visibly
/// belongs to the fence line it sits in, and it is why an open gate needs no
/// separate "no collision" case: the gap between the posts is 0.75 blocks and
/// the player is 0.6 wide.
constexpr BlockBoxes gateBoxes(FaceDirection facing, bool open) {
    const bool alongX = facing == FaceDirection::PosZ || facing == FaceDirection::NegZ;

    // Written once in the gate's own frame - `a` runs the way the gate spans,
    // `b` across it - and swapped into world axes at the end, so the two
    // orientations cannot drift apart.
    constexpr float kPostEdge = 0.125f;
    constexpr float kPostBase = 0.3125f;
    BlockBox local[4]{};
    int count = 0;
    local[count++] = {0.0f, kPostBase, kFenceArmLow, kPostEdge, 1.0f, kFenceArmHigh};
    local[count++] = {1.0f - kPostEdge, kPostBase, kFenceArmLow, 1.0f, 1.0f, kFenceArmHigh};
    if (!open) {
        local[count++] = {kPostEdge, kFenceLowRailBottom, kFenceArmLow,
                          1.0f - kPostEdge, kFenceLowRailTop, kFenceArmHigh};
        local[count++] = {kPostEdge, kFenceHighRailBottom, kFenceArmLow,
                          1.0f - kPostEdge, kFenceHighRailTop, kFenceArmHigh};
    }

    BlockBoxes result;
    for (int i = 0; i < count; ++i) {
        const BlockBox& b = local[i];
        result.boxes[result.count++] =
            alongX ? BlockBox{b.minX, b.minY, b.minZ, b.maxX, b.maxY, b.maxZ}
                   : BlockBox{b.minZ, b.minY, b.minX, b.maxZ, b.maxY, b.maxX};
    }
    return result;
}

/// The lily pad's plate, the reference model's 1.5 of sixteen.
constexpr float kFlatHeight = 0.09375f;

/// How thick a door leaf is. The reference's three sixteenths, and the single
/// owner of it - the collision box, the selection box and the mesher all read
/// this rather than writing 0.1875 out three times.
constexpr float kDoorThickness = 3.0f / 16.0f;

/// Which wall a door leaf stands against, as a box.
///
/// A shut door fills the side it faces. Opening it swings the leaf a quarter
/// turn - **which way depends on the hinge**, and that is the whole reason the
/// hinge has to be stored rather than derived: the same neighbourhood can carry
/// either hand, depending on what stood there when the door was hung.
constexpr BlockBoxes doorLeafBoxes(FaceDirection facing, bool hingeRight, bool open) {
    FaceDirection side = facing;
    if (open) {
        side = hingeRight ? quarterTurn(facing) : quarterTurn(quarterTurn(quarterTurn(facing)));
    }
    BlockBoxes result;
    switch (side) {
    case FaceDirection::PosX:
        result.boxes[0] = {1.0f - kDoorThickness, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
        break;
    case FaceDirection::NegX:
        result.boxes[0] = {0.0f, 0.0f, 0.0f, kDoorThickness, 1.0f, 1.0f};
        break;
    case FaceDirection::PosZ:
        result.boxes[0] = {0.0f, 0.0f, 1.0f - kDoorThickness, 1.0f, 1.0f, 1.0f};
        break;
    default:
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, kDoorThickness};
        break;
    }
    result.count = 1;
    return result;
}

/// A trapdoor lies flat when shut and stands against a wall when open.
constexpr BlockBoxes trapdoorLeafBoxes(FaceDirection facing, bool open, bool top) {
    BlockBoxes result;
    if (!open) {
        result.boxes[0] = top ? BlockBox{0.0f, 1.0f - kDoorThickness, 0.0f, 1.0f, 1.0f, 1.0f}
                              : BlockBox{0.0f, 0.0f, 0.0f, 1.0f, kDoorThickness, 1.0f};
        result.count = 1;
        return result;
    }
    // Open, it stands against the wall it is hinged to, which is the one it
    // faces - so the shut door's own box is exactly the shape wanted.
    return doorLeafBoxes(facing, false, false);
}

/// How far short of the ceiling tilled ground stops. The reference's own one
/// texel, and the single owner of it - the mesher, the collision box and the
/// selection box all read this rather than writing 0.9375 out three times.
constexpr float kTilledHeight = 15.0f / 16.0f;

/// How tall a bed lies. The reference's nine sixteenths.
constexpr float kBedHeight = 9.0f / 16.0f;

/// How tall a flat block lies. A carpet is a single texel; the lily pad is the
/// reference's one and a half; settled snow is two texels per layer, which is
/// the reference's own step.
constexpr float flatHeight(BlockId id) {
    if (isSnowLayer(id)) {
        return static_cast<float>(snowLayerDepth(id)) * 2.0f / 16.0f;
    }
    // The reference's own `redstone_dust_dot` sits at y = 0.25 of a texel - a
    // quarter of one, not a quarter of a block. A rail is a texel up and a
    // tripwire a texel and a half.
    if (isRedstoneWire(id)) {
        return 0.25f / 16.0f;
    }
    if (isRail(id)) {
        return 1.0f / 16.0f;
    }
    if (isTripwire(id)) {
        return 1.5f / 16.0f;
    }
    // Moss carpet parents `block/carpet` in the reference, so it is a carpet's
    // single texel - but `isCarpet` covers only the sixteen dyed wools and this
    // is not one of them.
    if (id == BlockId::MossCarpet) {
        return 1.0f / 16.0f;
    }
    // `sculk_vein.json` is a flat quad at 0.1, not a box: it is paint on the
    // surface it grows over.
    if (id == BlockId::SculkVein) {
        return 0.1f / 16.0f;
    }
    return isCarpet(id) ? 1.0f / 16.0f : kFlatHeight;
}

// **`kFlatHeight` is the lily pad's one and a half texels, and two blocks were
// silently taking it.** Moss carpet parents `block/carpet` in the reference and
// is one texel like every other carpet; `isCarpet` covers only the sixteen dyed
// wools, so the moss one fell past it and stood half a texel proud of any carpet
// beside it. A sculk vein is a flat quad at 0.1 of a texel - it is paint on a
// surface, not a slab - and at a texel and a half it lifted a player walking
// over it. Both are named beside the carpet clause above.
static_assert(flatHeight(BlockId::MossCarpet) == flatHeight(BlockId::CarpetRunFirst),
              "a moss carpet is a carpet; drop its clause and this fails");
static_assert(flatHeight(BlockId::SculkVein) < flatHeight(BlockId::LilyPad),
              "a vein is paint on a surface, not the pad's slab");

// A pane is the reference's: two texels thick down the middle of the cell, with
// arms reaching the wall on each connected side.
constexpr float kPaneLow = 0.4375f;
constexpr float kPaneHigh = 0.5625f;

constexpr BlockBoxes paneBoxes(std::uint8_t connections) {
    BlockBoxes result;
    result.boxes[result.count++] = {kPaneLow, 0.0f, kPaneLow, kPaneHigh, 1.0f, kPaneHigh};

    if ((connections & ConnectNorth) != 0) {
        result.boxes[result.count++] = {kPaneLow, 0.0f, 0.0f, kPaneHigh, 1.0f, kPaneLow};
    }
    if ((connections & ConnectSouth) != 0) {
        result.boxes[result.count++] = {kPaneLow, 0.0f, kPaneHigh, kPaneHigh, 1.0f, 1.0f};
    }
    if ((connections & ConnectWest) != 0) {
        result.boxes[result.count++] = {0.0f, 0.0f, kPaneLow, kPaneLow, 1.0f, kPaneHigh};
    }
    if ((connections & ConnectEast) != 0) {
        result.boxes[result.count++] = {kPaneHigh, 0.0f, kPaneLow, 1.0f, 1.0f, kPaneHigh};
    }
    return result;
}

/// A lantern's box, and an end rod's. Both are the reference's own shapes: the
/// lantern is its six-texel body, 5-11 across and 0-7 tall, with the cap and
/// handle standing outside what you can bump into or aim at; the rod is 6-10
/// across and the full height.
constexpr BlockBoxes postBoxes(BlockId id) {
    BlockBoxes result;
    constexpr float t = 1.0f / 16.0f;
    if (isTorchBlock(id)) {
        // The reference's own two-by-ten stick. What this replaces is the plant
        // hitbox, which was 12 texels across and 13 tall - so aiming anywhere
        // near a torch claimed the whole cell.
        result.boxes[0] = {7 * t, 0.0f, 7 * t, 9 * t, 10 * t, 9 * t};
        result.count = 1;
        return result;
    }
    if (isCauldron(id) || isComposter(id)) {
        // Walked into as a solid cube even though it is drawn hollow, which is
        // the reference's own collision shape: you cannot stand inside one.
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
        result.count = 1;
        return result;
    }
    if (id == BlockId::Bell) {
        result.boxes[0] = {0.0f, 0.0f, 4 * t, 1.0f, 1.0f, 12 * t};
        result.count = 1;
        return result;
    }
    if (id == BlockId::Grindstone) {
        result.boxes[0] = {2 * t, 0.0f, 2 * t, 14 * t, 1.0f, 14 * t};
        result.count = 1;
        return result;
    }
    if (id == BlockId::Stonecutter) {
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, 9 * t, 1.0f};
        result.count = 1;
        return result;
    }
    // The ten that stopped being cubes on 2026-08-10. Each is a single solid
    // box the height the reference gives it, because what you bump into and
    // what you aim at want the outline of the thing, not every plinth of it -
    // the anvil, the campfire and the scaffold all reduce to one.
    if (isAnvil(id)) {
        // **Inset on one axis only.** `template_anvil`'s "Anvil top" is
        // `from [3,10,0] to [13,16,16]` - the striking surface runs the *whole*
        // cell in Z and is narrow in X, which is why a real anvil looks like a
        // bar rather than a plinth. Both axes were inset to 2..14 here, so the
        // overhang drew four texels outside its own cage at each end: you could
        // see the horn and not aim at it, and the crack overlay stopped short of
        // what was breaking. Measured over every id by a cage-versus-drawing
        // sweep, which is the only way this shows up - nothing warns.
        result.boxes[0] = {2 * t, 0.0f, 0.0f, 14 * t, 1.0f, 1.0f};
        result.count = 1;
        return result;
    }
    if (id == BlockId::EndPortalFrame || id == BlockId::EnchantingTable ||
        id == BlockId::SculkSensor || id == BlockId::SculkShrieker ||
        id == BlockId::Campfire || id == BlockId::SoulCampfire) {
        const float top = id == BlockId::EndPortalFrame    ? 13 * t
                          : id == BlockId::EnchantingTable ? 12 * t
                          : id == BlockId::SculkShrieker   ? 15 * t
                          : id == BlockId::SculkSensor     ? 8 * t
                                                           : 7 * t;
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, top, 1.0f};
        result.count = 1;
        return result;
    }
    if (id == BlockId::FlowerPot) {
        result.boxes[0] = {5 * t, 0.0f, 5 * t, 11 * t, 6 * t, 11 * t};
        result.count = 1;
        return result;
    }
    if (id == BlockId::Scaffolding || isHopper(id)) {
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
        result.count = 1;
        return result;
    }
    if (id == BlockId::BrewingStand) {
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, 2 * t, 1.0f};
        result.boxes[1] = {7 * t, 0.0f, 7 * t, 9 * t, 14 * t, 9 * t};
        result.count = 2;
        return result;
    }
    const bool rod = id == BlockId::EndRod;
    const float half = rod ? 0.125f : 0.1875f;
    result.boxes[0] = {0.5f - half, 0.0f, 0.5f - half, 0.5f + half, rod ? 1.0f : 0.4375f, 0.5f + half};
    result.count = 1;
    return result;
}

/// A drawn box that carries **its own rectangle of the texture**.
///
/// Every other shape here is *cut out of a cube*, so where a box sits in its
/// cell is also where it samples from - which is exactly right for a stair, a
/// slab or a fence and exactly wrong for a lantern, whose six-texel body is
/// painted in the corner of its sheet rather than in the middle. A model needs
/// the two said separately.
struct ModelBox {
    BlockBox box;
    /// Rectangle the four side faces sample.
    float uMin, vMin, uMax, vMax;
    /// Rectangle the top and bottom faces sample. **A net paints a lid
    /// somewhere other than its walls**, so a model needs the two said
    /// separately - reusing the side rect is what put the lantern's glass on
    /// its lid and squashed the cap's plate into a two-texel sliver.
    float topUMin, topVMin, topUMax, topVMax;
    /// Which texture layer this box samples, or **-1 to take the block's own**.
    ///
    /// A bell is a wooden frame round a gold bell and a grindstone is a stone
    /// wheel between two posts: parts of one block painted from different
    /// images. Without this a model could pick its rectangle but not its
    /// picture, and every post came out gold.
    float sideLayer = -1.0f;
    float lidLayer = -1.0f;
    /// Quarter turns applied to the **lid**, clockwise.
    ///
    /// The reference rotates a whole block model to face a direction; ours has
    /// no rotation, so a lid that is not symmetric needs to say which way round
    /// it goes. A bed is the case that needs it: its pillow is painted along
    /// one edge of the head half's top texture, and without a turn it lands
    /// against the join for three of the four facings instead of at the end of
    /// the bed.
    ///
    /// **Anything whose lid marks a world direction has to set this**, and for
    /// twenty milestones the bed was the only one that did - so a repeater and
    /// a comparator, which carry their torch sockets painted along one axis,
    /// wore them sideways or backwards on three facings out of four. Every
    /// horizontal user derives the count from `lidTurnsFacing` rather than
    /// writing four cases out again.
    ///
    /// It also serves the reference's per-face `"rotation"`, which is the same
    /// quarter turn of a rectangle against its quad: `template_campfire`'s logs
    /// carry `"rotation": 90` and `"rotation": 180` on their `up` faces, and
    /// without them a sixteen-texel strip is squeezed across a four-texel log.
    unsigned char lidTurns = 0;
    /// One side face whose automatic mirroring is to be undone.
    ///
    /// The mesher flips U on half the faces so a texture reads the same way
    /// from every side, which is what a word or a furnace front wants. A bed's
    /// side marks a *world* direction - red mattress at the join, white pillow
    /// at the head - so the flip puts the pillow at the wrong end on exactly
    /// one of the two long sides.
    FaceDirection unmirror = FaceDirection::Unknown;
    /// Row of `kBoxFloorFaces` this box's **underside** takes, or -1 for a box
    /// whose underside is its lid.
    ///
    /// Every field above says "the lid" and means both of them: the four
    /// rectangles, the two layers and `lidTurns` are all chosen by axis, and an
    /// axis is true for +Y and -Y alike. So a flower pot's soil wore dirt
    /// underneath, a campfire's crossbars wore bark where the embers are and a
    /// candle stood on its own wick. **-1 is "the lid's answer", not "ask the
    /// block"** - which is why adding this moved no box until a row was named.
    signed char floorOverride = -1;
    /// Row of `kBoxWallFaces` this box's **four walls** take, or -1 for a box
    /// whose walls are all alike.
    ///
    /// `sideLayer` and one rectangle serve +X, -X, +Z and -Z, and the reference
    /// routinely gives them four different rectangles and two different
    /// textures - a sea pickle is four quarters of one strip and a campfire log
    /// is bark outward and embers inward. `unmirror` was the only per-wall knob
    /// there has ever been, and it flips one wall's U.
    signed char wallOverride = -1;
};

struct ModelBoxes {
    ModelBox boxes[10]{};
    int count = 0;
};

/// `drawnBoxes` pours a `ModelBoxes` into a `BlockBoxes` one box at a time,
/// bounded by the model's own count, so the receiver must be at least as deep as
/// the source. It was one shorter and six cauldron ids wrote past the end.
///
/// **Asserted against the two arrays rather than against the numbers 9 and 10**,
/// so it still holds after either one moves - a `static_assert` that repeats a
/// literal proves only that someone typed the literal twice.
static_assert(sizeof(BlockBoxes{}.boxes) / sizeof(BlockBox) >=
                  sizeof(ModelBoxes{}.boxes) / sizeof(ModelBox),
              "a BlockBoxes must hold every box a ModelBoxes can, or drawnBoxes writes off the "
              "end of it - see the note on BlockBoxes");

/// Quarter turns that put a lid painted for **north** onto a block that faces
/// `facing`, and the single owner of that mapping.
///
/// It is the reference's own blockstate `y` rotation read as a count: every
/// horizontal block it turns is written `facing=north` with no rotation,
/// `east` with `"y": 90`, `south` with `180` and `west` with `270`. A repeater
/// and a comparator store the *opposite* of the reference's `facing` - ours is
/// the end the signal leaves by, theirs the end it arrives at - so their
/// blockstates read `south` where ours reads `NegZ`, and the two conventions
/// cancel to exactly this table. Checked against the geometry rather than
/// against itself, box for box, in the asserts under `postModel`.
///
/// **Written once because it was written once too few times.** The bed had it
/// and the redstone bench did not, which is this project's recorded failure
/// shape five: a derivation applied to one of a pair and not the other.
constexpr unsigned char lidTurnsFacing(FaceDirection facing) {
    return facing == FaceDirection::NegZ   ? 0
           : facing == FaceDirection::PosX ? 1
           : facing == FaceDirection::PosZ ? 2
                                           : 3;
}

/// Texture layers a model box borrows from a *different* face of its own block.
///
/// **Written as numbers here and cross-checked against the table below with
/// `static_assert`s**, because `blockTextureLayer` is defined three thousand
/// lines further down and a model cannot call it. The asserts are what stop
/// this being a second copy that quietly rots: change a row and the build
/// fails rather than the bell turning gold.
///
/// ⚠️ **`ExtraBlockInfo::layer` is an offset into the appended run, not a layer
/// index.** `blockTextureLayer` adds `kTableSpritesFirst` to it. Writing the
/// row's number here on its own pointed every one of these at a nether plant -
/// a lily pad for the brewing stand's rod, weeping vines for the bell - and the
/// asserts still passed, because they compared the *offset* against the offset
/// and never once checked the sum. So the base is stated once, asserted once,
/// and every constant below is derived from it.
constexpr float kTableLayerBase = 215.0f;
constexpr float kComposterTopLayer = kTableLayerBase + 395.0f;
constexpr float kComposterSideLayer = kTableLayerBase + 396.0f;
constexpr float kWaterLayer = 12.0f;
constexpr float kComposterReadyLayer = kTableLayerBase + 397.0f;
constexpr float kStonecutterTopLayer = kTableLayerBase + 472.0f;
constexpr float kGrindstoneRoundLayer = kTableLayerBase + 474.0f;
constexpr float kBellSideLayer = kTableLayerBase + 477.0f;
/// The lectern's own side art, named so the plinth's top ring can wear it. Its
/// lid would otherwise take the block's *top* layer, which is the reading
/// desk's picture, and paint a book on the floor of the thing.
constexpr float kLecternSideLayer = kTableLayerBase + 475.0f;
constexpr float kBellTopLayer = kTableLayerBase + 478.0f;
constexpr float kCauldronSideLayer = kTableLayerBase + 479.0f;
constexpr float kBrewingStandBaseLayer = kTableLayerBase + 481.0f;
constexpr float kBrewingStandRodLayer = kTableLayerBase + 482.0f;
/// The lit redstone torch, which is an ordinary table row from the third run.
/// The unlit one is a staged layer of its own, because there was no row to put
/// it in without moving every id behind it.
constexpr float kRedstoneTorchLayer = kTableLayerBase + 357.0f;

/// **The redstone run, appended after every existing layer.** Same rule as the
/// moon and the saw blade: anything inserted in the middle silently slides every
/// layer behind it and mistextures the lot.
///
/// **Stated as a literal and asserted where the run that computes it is
/// declared**, for exactly the reason `kTableLayerBase` above is: `postModel`
/// needs these numbers and is defined two thousand lines before anything could
/// work them out. The assert is what stops the literal being a second copy that
/// quietly rots.
///
/// **It went 1157 → 1142 on 2026-08-18**, when fifteen duplicate music discs
/// left `kExtraItemSprites` below. Three literals in this header sit downstream
/// of that run - this one, the saw blade and the compost - and all three are
/// pinned to a derived sprite index, so all three failed to compile rather than
/// silently mistexturing. **That is what these asserts are for and it is the
/// only reason the shift was not a bug**; the two run *lengths* themselves have
/// no such guard, and could not have one here.
constexpr int kRedstoneSpritesFirst = 1142;

/// Sixteen copies of the reference's own dust art, **tinted at staging time by
/// the strength they stand for** - the reference tints them at draw time, and we
/// have no per-block tint, so the colour is baked into sixteen layers instead.
/// One subtraction turns a wire's id into its picture.
constexpr int kWireFirstSprite = kRedstoneSpritesFirst;
constexpr int kWireSprites = 16;

constexpr int kRedstoneTorchOffSprite = kWireFirstSprite + kWireSprites;
constexpr int kLeverSprite = kRedstoneTorchOffSprite + 1;
constexpr int kRepeaterSprite = kLeverSprite + 1;
constexpr int kRepeaterOnSprite = kRepeaterSprite + 1;
constexpr int kComparatorSprite = kRepeaterOnSprite + 1;
constexpr int kComparatorOnSprite = kComparatorSprite + 1;
/// The bench both a repeater and a comparator stand on. A second copy of smooth
/// stone rather than a lookup into the table run, so a model box can name it as
/// a plain number the way every other one does.
constexpr int kRedstoneSlabSprite = kComparatorOnSprite + 1;
constexpr int kObserverFrontSprite = kRedstoneSlabSprite + 1;
constexpr int kObserverBackSprite = kObserverFrontSprite + 1;
constexpr int kObserverBackOnSprite = kObserverBackSprite + 1;
constexpr int kObserverSideSprite = kObserverBackOnSprite + 1;
constexpr int kObserverTopSprite = kObserverSideSprite + 1;
constexpr int kPistonTopSprite = kObserverTopSprite + 1;
constexpr int kPistonTopStickySprite = kPistonTopSprite + 1;
constexpr int kPistonSideSprite = kPistonTopStickySprite + 1;
constexpr int kPistonBottomSprite = kPistonSideSprite + 1;
constexpr int kPistonInnerSprite = kPistonBottomSprite + 1;
constexpr int kDispenserFrontSprite = kPistonInnerSprite + 1;
constexpr int kDispenserFrontVerticalSprite = kDispenserFrontSprite + 1;
constexpr int kDropperFrontSprite = kDispenserFrontVerticalSprite + 1;
constexpr int kDropperFrontVerticalSprite = kDropperFrontSprite + 1;
/// The sides and back a dispenser and a dropper share with a furnace. Staged
/// again here rather than reached for in the first sixty-seven, because those
/// are `TextureLayer` enumerators and a model wants a plain number.
constexpr int kMachineSideSprite = kDropperFrontVerticalSprite + 1;
constexpr int kMachineTopSprite = kMachineSideSprite + 1;
constexpr int kDaylightSideSprite = kMachineTopSprite + 1;
constexpr int kDaylightTopSprite = kDaylightSideSprite + 1;
constexpr int kDaylightInvertedTopSprite = kDaylightTopSprite + 1;
constexpr int kLightningRodSprite = kDaylightInvertedTopSprite + 1;
constexpr int kLightningRodOnSprite = kLightningRodSprite + 1;
constexpr int kTripwireHookSprite = kLightningRodOnSprite + 1;
constexpr int kTripwireSprite = kTripwireHookSprite + 1;
/// The four rail families, quiet then live. The plain rail has no live form and
/// spends its second slot on the corner piece instead.
constexpr int kRailFirstSprite = kTripwireSprite + 1;
constexpr int kRailSprites = 8;
constexpr int kRedstoneLampOnSprite = kRailFirstSprite + kRailSprites;
constexpr int kRedstoneSprites = kRedstoneLampOnSprite + 1 - kRedstoneSpritesFirst;

/// **The brewing run, appended after the redstone one.** Two ingredients, then
/// every potion three ways - drunk, thrown and on the end of an arrow.
///
/// The forty-one potion pictures are the reference's own bottle with its
/// overlay **tinted at staging time by the effect's colour**, exactly as the
/// redstone wire is: the reference tints at draw time and nothing here can.
///
/// **Two, not five.** It was five until 2026-08-18, when `Item.hpp` deleted
/// `BlazeRod`, `BlazePowder` and `GhastTear` - a second copy of `CinderRod`,
/// `CinderPowder` and `DrifterTear`, which had been sitting in the appended run
/// below with sprites of their own all along. What is left is the fermented
/// spider eye and the dragon's breath, and this number **is** the length of
/// that run: `itemTextureLayer` answers `FermentedSpiderEye..DragonBreath` from
/// here by subtraction. Nothing in this header can check that, because the item
/// enum is in a header this one cannot see - so the assert lives on the far
/// side, beside the ones that already pin the sherd, disc and potion runs.
constexpr int kBrewingSpritesFirst = kRedstoneSpritesFirst + kRedstoneSprites;
constexpr int kBrewingSprites = 2;
constexpr int kPotionSpritesFirst = kBrewingSpritesFirst + kBrewingSprites;
/// Stated here and `static_assert`ed against `kPotionTypes` in `Item.hpp`,
/// which is where the potions themselves are described and which this header
/// cannot see.
constexpr int kPotionSpriteTypes = 41;
constexpr int kSplashPotionSpritesFirst = kPotionSpritesFirst + kPotionSpriteTypes;
constexpr int kTippedArrowSpritesFirst = kSplashPotionSpritesFirst + kPotionSpriteTypes;
constexpr int kTippedArrowSprites = 37;
constexpr int kLingeringPotionSpritesFirst = kTippedArrowSpritesFirst + kTippedArrowSprites;
constexpr int kPotionSpritesEnd = kLingeringPotionSpritesFirst + kPotionSpriteTypes;

/// **Collectibles**: the twenty-three pottery sherds, the goat horn and the
/// twenty-two music discs.
///
/// One sprite for all eight horns, because the reference draws them from one
/// picture too - what differs between them is the note, not the horn.
constexpr int kSherdSpritesFirst = kPotionSpritesEnd;
constexpr int kSherdSprites = 23;
constexpr int kGoatHornSprite = kSherdSpritesFirst + kSherdSprites;
constexpr int kMusicDiscSpritesFirst = kGoatHornSprite + 1;
constexpr int kMusicDiscSprites = 22;
constexpr int kCollectibleSpritesEnd = kMusicDiscSpritesFirst + kMusicDiscSprites;

/// The sixteen firework stars, one tint apiece.
constexpr int kFireworkStarSpritesFirst = kCollectibleSpritesEnd;
constexpr int kFireworkStarSprites = 16;
constexpr int kFireworkStarSpritesEnd = kFireworkStarSpritesFirst + kFireworkStarSprites;

/// The ten breaking stages, drawn over whatever is being mined.
///
/// **Appended at the very end on purpose.** Several runs above end in a
/// constant another run starts from, and one of them - `kRedstoneSpritesFirst`
/// - is a hard literal held in place by a `static_assert`; inserting anywhere
/// but here slides every layer after the insertion point and re-textures a few
/// hundred blocks at once.
constexpr int kDestroyStageFirst = kFireworkStarSpritesEnd;
constexpr int kDestroyStages = 10;
constexpr int kDestroyStagesEnd = kDestroyStageFirst + kDestroyStages;

/// Which of the ten pictures a break that far along shows. Clamped at both
/// ends: progress reaches exactly 1 on the frame the block goes, and an
/// eleventh stage would sample whatever layer follows this run.
constexpr int destroyStageLayer(float progress) {
    const int stage = static_cast<int>(progress * static_cast<float>(kDestroyStages));
    return kDestroyStageFirst + (stage < 0 ? 0 : (stage >= kDestroyStages ? kDestroyStages - 1 : stage));
}
/// Four more that a model box needs by name rather than by face, because these
/// blocks paint a *lid* from their side image: an anvil's plinths, a campfire's
/// logs, a scaffold's posts and a hopper's funnel are all the same picture all
/// the way round.
constexpr float kAnvilBodyLayer = kTableLayerBase + 483.0f;
constexpr float kScaffoldSideLayer = kTableLayerBase + 487.0f;
constexpr float kCampfireLogLayer = kTableLayerBase + 534.0f;
/// The same log with embers burning in it, which is the only picture in the
/// game that carries a campfire's heat. It is the row each campfire's
/// `topLayer` names, and the assert under the model tables holds these two
/// literals against those rows so they cannot drift apart.
///
/// **The soul campfire's was wrong, and the fix had to land outside this file.**
/// Layer 536 held `soul_campfire_fire.png`, the animated flame sheet, and was
/// read here as the ember log - so the blue variant wore a frame of its own fire
/// where its ordinary twin wears a lit plank. It was corrected by **replacing
/// that name in place** with `soul_campfire_log_lit.png` in
/// `tools/make-reference-blocks.ps1` and in `Main.cpp`'s sprite list, which is
/// why the number below did not move and nothing after it slid. The flame sheet
/// had no other reader - the campfire model is five solid boxes with no flame
/// quad - so nothing was lost. **Keep this slot a replacement**: inserting a
/// name ahead of it gives several hundred blocks their neighbour's texture.
constexpr float kCampfireLitLogLayer = kTableLayerBase + 535.0f;
constexpr float kSoulCampfireLitLogLayer = kTableLayerBase + 536.0f;
constexpr float kHopperSideLayer = kTableLayerBase + 687.0f;
/// The two plain materials a bell's frame is made of. The reference builds it
/// from `block/stone` posts and a `block/dark_oak_planks` bar, and paints the
/// bell itself from an entity texture we do not have - so the gold comes off
/// `bell_side`/`bell_top` and the frame off the two ordinary layers.
constexpr float kStoneLayer = 0.0f;
constexpr float kDirtLayer = 1.0f;
constexpr float kPlanksLayer = 9.0f;
constexpr float kLogSideLayer = 13.0f;
/// The saw blade, which is the last layer of all.
constexpr float kStonecutterSawLayer = 1139.0f;
/// The compost inside a composter. **The tub's own top texture is a rim with a
/// transparent middle**, and that middle is exactly the rectangle the contents
/// plate samples - so a composter filled to anything below level eight drew
/// nothing at all and read as broken. The reference has this image; it simply
/// had not been staged.
constexpr float kComposterCompostLayer = 1141.0f;

/// The reference hangs its ladder on a **zero-thickness plane 0.8 texels off
/// the wall**. Ours is drawn as a box, so it is a half-texel plate held a texel
/// clear instead: close enough to read as flush, and far enough that it shares
/// no plane with the wall behind it - which is the only thing that would
/// flicker. A vine's sheets use the same two numbers.
constexpr float kLadderGap = 1.0f / 16.0f;
constexpr float kLadderThickness = 1.0f / 32.0f;

/// A ladder's rungs, flat against the wall it is fixed to.
constexpr BlockBoxes ladderBoxes(FaceDirection wall) {
    constexpr float lo = kLadderGap;
    constexpr float hi = kLadderGap + kLadderThickness;
    BlockBoxes result;
    switch (wall) {
    case FaceDirection::PosX:
        result.boxes[0] = {1.0f - hi, 0.0f, 0.0f, 1.0f - lo, 1.0f, 1.0f};
        break;
    case FaceDirection::NegX:
        result.boxes[0] = {lo, 0.0f, 0.0f, hi, 1.0f, 1.0f};
        break;
    case FaceDirection::PosZ:
        result.boxes[0] = {0.0f, 0.0f, 1.0f - hi, 1.0f, 1.0f, 1.0f - lo};
        break;
    default:
        result.boxes[0] = {0.0f, 0.0f, lo, 1.0f, 1.0f, hi};
        break;
    }
    result.count = 1;
    return result;
}

/// A vine's sheets: one against every side it clings to, and one under the
/// ceiling when `roof` says there is something above to hang from.
///
/// **The ceiling sheet is derived rather than stored**, which is Bedrock's own
/// arrangement - four direction bits and no fifth for the roof - and it is why
/// sixteen ids cover the block completely where Java needs thirty-two.
constexpr BlockBoxes vineBoxes(std::uint8_t sides, bool roof) {
    constexpr float lo = kLadderGap;
    constexpr float hi = kLadderGap + kLadderThickness;
    BlockBoxes result;
    if ((sides & ConnectNorth) != 0) {
        result.boxes[result.count++] = {0.0f, 0.0f, lo, 1.0f, 1.0f, hi};
    }
    if ((sides & ConnectSouth) != 0) {
        result.boxes[result.count++] = {0.0f, 0.0f, 1.0f - hi, 1.0f, 1.0f, 1.0f - lo};
    }
    if ((sides & ConnectWest) != 0) {
        result.boxes[result.count++] = {lo, 0.0f, 0.0f, hi, 1.0f, 1.0f};
    }
    if ((sides & ConnectEast) != 0) {
        result.boxes[result.count++] = {1.0f - hi, 0.0f, 0.0f, 1.0f - lo, 1.0f, 1.0f};
    }
    if (roof) {
        result.boxes[result.count++] = {0.0f, 1.0f - hi, 0.0f, 1.0f, 1.0f - lo, 1.0f};
    }
    return result;
}

/// A cocoa pod, hanging off the log on the `facing` side of its cell.
///
/// Each of the three ages has its own size, and the one thing they share is
/// that **the top of the pod sits a quarter of a block below the top of the
/// log** - so a row of pods lines up however ripe each one is. The reference's
/// ripe pod is 8 by 9 by 8 spanning z 7 to 15, which is a texel of daylight
/// between the pod and the bark.
constexpr BlockBoxes cocoaBoxes(FaceDirection facing, int age) {
    constexpr float t = 1.0f / 16.0f;
    const float size = (age == 0 ? 4.0f : age == 1 ? 6.0f : 8.0f) * t;
    const float top = 12.0f * t;
    const float bottom = top - (age == 0 ? 5.0f : age == 1 ? 7.0f : 9.0f) * t;
    const float half = size * 0.5f;
    const float near = 1.0f - t - size;
    const float far = 1.0f - t;

    BlockBoxes result;
    switch (facing) {
    case FaceDirection::PosX:
        result.boxes[0] = {near, bottom, 0.5f - half, far, top, 0.5f + half};
        break;
    case FaceDirection::NegX:
        result.boxes[0] = {1.0f - far, bottom, 0.5f - half, 1.0f - near, top, 0.5f + half};
        break;
    case FaceDirection::PosZ:
        result.boxes[0] = {0.5f - half, bottom, near, 0.5f + half, top, far};
        break;
    default:
        result.boxes[0] = {0.5f - half, bottom, 1.0f - far, 0.5f + half, top, 1.0f - near};
        break;
    }
    result.count = 1;
    return result;
}

/// A button's box, read straight out of the reference's `button.json`.
///
/// The reference authors one model lying on the floor - `5,0,6` to `11,2,10` -
/// and lets the blockstate turn it onto a wall or a ceiling. We have no model
/// rotation, so the turn is done here: the **six-texel run is always
/// perpendicular to the face it is stuck to**, the four-texel one is the other
/// horizontal, and the two-texel thickness stands off the face. A pressed
/// button is `1.02` thick rather than `1`, which is the reference's own number
/// and not a rounding of it.
constexpr BlockBoxes buttonBoxes(int mount, bool pressed) {
    constexpr float t = 1.0f / 16.0f;
    const float thick = (pressed ? 1.02f : 2.0f) * t;
    BlockBoxes result;
    result.count = 1;
    switch (mount) {
    case 0: // Floor.
        result.boxes[0] = {5 * t, 0.0f, 6 * t, 11 * t, thick, 10 * t};
        break;
    case 1: // Ceiling.
        result.boxes[0] = {5 * t, 1.0f - thick, 6 * t, 11 * t, 1.0f, 10 * t};
        break;
    case 2 + static_cast<int>(FaceDirection::PosX):
        result.boxes[0] = {1.0f - thick, 6 * t, 5 * t, 1.0f, 10 * t, 11 * t};
        break;
    case 2 + static_cast<int>(FaceDirection::NegX):
        result.boxes[0] = {0.0f, 6 * t, 5 * t, thick, 10 * t, 11 * t};
        break;
    case 2 + static_cast<int>(FaceDirection::PosZ):
        result.boxes[0] = {5 * t, 6 * t, 1.0f - thick, 11 * t, 10 * t, 1.0f};
        break;
    default:
        result.boxes[0] = {5 * t, 6 * t, 0.0f, 11 * t, 10 * t, thick};
        break;
    }
    return result;
}

/// A pressure plate's box: the reference's `1,0,1` to `15,1,15`, and half a
/// texel thick once something is standing on it.
constexpr BlockBoxes plateBoxes(bool pressed) {
    constexpr float t = 1.0f / 16.0f;
    BlockBoxes result;
    result.count = 1;
    result.boxes[0] = {t, 0.0f, t, 15 * t, (pressed ? 0.5f : 1.0f) * t, 15 * t};
    return result;
}

/// A sign, a hanging sign or a banner, from the reference's own models.
///
/// `template_sign_rot_0` is a post `7.33,0,7.33` to `8.67,9.33,8.67` under a
/// board `0,9.33,7.33` to `16,17.33,8.67`; `template_wall_sign` is one board at
/// `0,4.33,0.33` to `16,12.33,1.67`. **The standing board's top reaches
/// outside its own cell in the reference and is clipped to it here**, because
/// nothing else in this game draws past a cell wall - a `Sign` box takes its
/// texture from where it sits in the cell and the block sampler repeats, so a
/// board poking out of the top would wrap the sheet round rather than extend
/// it. Recorded as a named divergence in `tools/check-models.ps1`.
///
/// **A hanging sign is two texels thick, not the post's one and a third**, and
/// the three of them differ above the board: `template_hanging_sign_rot_0`
/// hangs it from two chains reaching the ceiling, `template_attached_hanging_
/// sign_rot_0` swaps those for one plane straight up to it, and
/// `template_wall_hanging_sign` adds the bar at `0,14,6` to `16,16,10` whose
/// ends cull into the blocks either side - which is what a wall-hung one hangs
/// from, so it is **not** pressed flat against the wall the way a wall sign is.
/// All three share the board at `1,0,7` to `15,10,9`.
///
/// A banner has no reference model at all - it is a block entity there - so its
/// post and cloth are ours, and that is recorded at `kBannerFamilies`.
constexpr BlockBoxes signBoxes(int kind, FaceDirection facing, bool onWall) {
    constexpr float t = 1.0f / 16.0f;
    constexpr float postLo = 7.33333f / 16.0f;
    constexpr float postHi = 8.66667f / 16.0f;
    const bool alongX = facing == FaceDirection::PosZ || facing == FaceDirection::NegZ;
    BlockBoxes result;

    // A flat piece facing the way the sign does: `lo` to `hi` across, `faceLo`
    // to `faceHi` in height, `thickLo` to `thickHi` through the thickness.
    const auto plate = [&](float lo, float hi, float faceLo, float faceHi, float thickLo,
                           float thickHi) {
        return alongX ? BlockBox{lo, faceLo, thickLo, hi, faceHi, thickHi}
                      : BlockBox{thickLo, faceLo, lo, thickHi, faceHi, hi};
    };
    // The standing sign's own board, as thick as the post under it.
    const auto board = [&](float lo, float hi, float faceLo, float faceHi) {
        return plate(lo, hi, faceLo, faceHi, postLo, postHi);
    };

    if (onWall && kind == 1) {
        // Hung off the side of a block: the same board as the ceiling one, on
        // two chains up to a bar that spans the whole cell and buries its ends
        // in the blocks either side (`template_wall_hanging_sign`).
        result.boxes[0] = plate(1 * t, 15 * t, 0.0f, 10 * t, 7 * t, 9 * t);
        result.boxes[1] = plate(3 * t, 4 * t, 10 * t, 14 * t, 7 * t, 9 * t);
        result.boxes[2] = plate(12 * t, 13 * t, 10 * t, 14 * t, 7 * t, 9 * t);
        result.boxes[3] = plate(0.0f, 1.0f, 14 * t, 1.0f, 6 * t, 10 * t);
        result.count = 4;
        return result;
    }

    if (onWall) {
        // Against the wall behind it, a texel and a third clear of it.
        constexpr float near = 0.33333f / 16.0f;
        constexpr float far = 1.66667f / 16.0f;
        constexpr float lo = 0.0f;
        constexpr float hi = 1.0f;
        constexpr float bottom = 4.33333f / 16.0f;
        constexpr float top = 12.33333f / 16.0f;
        switch (facing) {
        case FaceDirection::PosX:
            result.boxes[0] = {near, bottom, lo, far, top, hi};
            break;
        case FaceDirection::NegX:
            result.boxes[0] = {1.0f - far, bottom, lo, 1.0f - near, top, hi};
            break;
        case FaceDirection::PosZ:
            result.boxes[0] = {lo, bottom, near, hi, top, far};
            break;
        default:
            result.boxes[0] = {lo, bottom, 1.0f - far, hi, top, 1.0f - near};
            break;
        }
        result.count = 1;
        return result;
    }

    if (kind == 1) {
        // Hung from the ceiling: the board low in the cell, two chains above it.
        result.boxes[0] = plate(1 * t, 15 * t, 0.0f, 10 * t, 7 * t, 9 * t);
        result.boxes[1] = plate(3 * t, 4 * t, 10 * t, 1.0f, 7 * t, 9 * t);
        result.boxes[2] = plate(12 * t, 13 * t, 10 * t, 1.0f, 7 * t, 9 * t);
        result.count = 3;
        return result;
    }

    if (kind == 2) {
        // A banner: a post the height of the cell with the cloth hung across it.
        result.boxes[0] = {postLo, 0.0f, postLo, postHi, 1.0f, postHi};
        result.boxes[1] = board(1 * t, 15 * t, 3 * t, 1.0f);
        result.count = 2;
        return result;
    }

    result.boxes[0] = {postLo, 0.0f, postLo, postHi, 9.33333f / 16.0f, postHi};
    result.boxes[1] = board(0.0f, 1.0f, 9.33333f / 16.0f, 1.0f);
    result.count = 2;
    return result;
}

// ---------------------------------------------------------------------------
// **A box's floor is not its lid, and its four walls are not one wall.**
//
// `ModelBox` states one rectangle and one layer for "the lid" and one of each
// for "the side", and the mesher chose between them by *axis*. An axis is true
// for +Y and -Y alike, so every box's underside wore its lid's rectangle, its
// lid's layer and its lid's quarter turn: a flower pot stood on dirt, a
// campfire's crossbars had embers on top and bark underneath the wrong way
// round, a candle stood on its own wick and a repeater's belly showed the
// arrows off its lid, turned with the block. One axis over, `sideLayer` and one
// rectangle serve +X, -X, +Z and -Z together, and the reference routinely gives
// those four faces four different rectangles and two different textures - a sea
// pickle is four quarters of one strip, a campfire log is bark outward and
// embers inward, a flower pot's thin walls show a six-texel rect on a one-texel
// end.
//
// The fallback underneath was per-face correct the whole time:
// `blockTextureLayer(block, face.facing, face.direction)` tells Top from Bottom
// and names all four walls separately. **Only the override was coarser than the
// thing it overrode** - this project's recorded failure shape ten, stood on its
// head.
//
// **Said as one byte apiece rather than as four rects and four layers.**
// `ModelBoxes` is 684 bytes returned *by value* from `postModel` inside the
// mesher's per-cell loop, and inline per-face fields would have taken
// `ModelBox` from 68 bytes to 128 and that copy to 1284 - to serve about thirty
// boxes out of thirteen hundred. Two `signed char` indices into the tables
// below fit in the two bytes of tail padding `ModelBox` already carried, so
// neither `sizeof` moved: 68 and 684 before and after, measured rather than
// assumed.
//
// **-1 means "the answer this face gets today", not "ask the block"**, which is
// what makes "no box changed until its own data was edited" a property of the
// encoding rather than something that had to be measured afterwards.
//
// **And the accessors below are the point, more than the fields are.** A box is
// drawn in four places - the mesher, the slot picture, the dropped entity and
// the thrown one - and a field alone leaves each of them deciding for itself,
// which is exactly the shape that produced the original bug. `boxFaceLayer`,
// `boxFaceRect` and `boxFaceTurns` take the face and answer for it, so "the
// drop asked and the icon did not" stops being a thing anyone can write.

/// `world/FaceShading.hpp` owns this. Declaring it opaquely - a scoped enum
/// with a fixed underlying type is a complete type - is how the accessors below
/// can be keyed on the same face name the mesher already uses without this
/// header reaching the renderer's vertex format, which `FaceShading.hpp`
/// includes and which no gameplay header may carry.
///
/// The cost is that the *enumerators* are out of reach here, so `boxFaceRole`
/// reads them as the integers they are. `ChunkMesher.cpp` sees both headers and
/// carries the proof that this reading agrees with `FaceGeometry.hpp`'s own
/// `faceAxis` and `faceOutwardNormal` derivations, face by face, with a wrong
/// reading shown failing.
enum class AxisFace : std::uint8_t;

/// Which of a box's overridable faces an `AxisFace` names: **0 the lid, 1 the
/// floor, then 2, 3, 4, 5 for the four walls +X, -X, +Z, -Z**.
///
/// The wall order is `FaceDirection`'s own - `PosX`, `NegX`, `PosZ`, `NegZ` -
/// so `boxWallSlotDirection` is a cast rather than a table, and a slot cannot
/// drift from the direction it means without that cast being wrong too.
constexpr int boxFaceRole(AxisFace face) {
    const int index = static_cast<int>(face);
    return index == 2 ? 0 : index == 3 ? 1 : index < 2 ? 2 + index : index;
}

constexpr bool boxFaceIsLidOrFloor(AxisFace face) { return boxFaceRole(face) <= 1; }

constexpr bool boxFaceIsFloor(AxisFace face) { return boxFaceRole(face) == 1; }

/// -1 for a lid or a floor; otherwise 0, 1, 2, 3 indexing `BoxWallFaces::face`.
constexpr int boxWallSlot(AxisFace face) {
    const int role = boxFaceRole(face);
    return role <= 1 ? -1 : role - 2;
}

/// The direction wall slot `slot` faces. The identity that makes the order
/// above safe rather than merely written down.
constexpr FaceDirection boxWallSlotDirection(int slot) {
    return static_cast<FaceDirection>(slot);
}

/// A face rectangle in the block sheet's own 0-1 coordinates, which is what
/// both `ModelBox` rect pairs already hold.
struct FaceRect {
    float uMin = 0.0f;
    float vMin = 0.0f;
    float uMax = 1.0f;
    float vMax = 1.0f;
};

/// "This face keeps whatever it is given today" - the lid's answer for a floor,
/// the side's for a wall. The default, and the reason adding the fields moved
/// nothing.
constexpr float kBoxFaceSame = -1.0f;
/// "This face has no layer of its own": fall through to the block, the way an
/// unset `lidLayer` already does. Only meaningful for `BoxFaceOverride::layer`.
constexpr float kBoxFaceOwnBlock = -2.0f;
/// `kBoxFaceSame` for a quarter-turn count, which is not a float.
constexpr signed char kBoxTurnSame = -1;

/// One face's disagreement with the rest of its box. Every field defaults to
/// "no disagreement", so a row states only what it changes.
struct BoxFaceOverride {
    float uMin = kBoxFaceSame;
    float vMin = kBoxFaceSame;
    float uMax = kBoxFaceSame;
    float vMax = kBoxFaceSame;
    float layer = kBoxFaceSame;
    signed char turns = kBoxTurnSame;
};

/// The four walls of one box, in `boxWallSlot` order: **+X, -X, +Z, -Z**.
struct BoxWallFaces {
    BoxFaceOverride face[4];
};

/// A sixteenth, which is the unit every rectangle below is quoted in because it
/// is the unit the reference's model JSON states them in.
constexpr float kBoxFaceTexel = 1.0f / 16.0f;

/// Row of `kBoxFloorFaces` - see each row's comment for the model it is read
/// from. Every one of these is a `down` face whose reference JSON disagrees
/// with the `up` face beside it.
constexpr signed char kBoxFloorCandle = 0;
constexpr signed char kBoxFloorBamboo = 1;
constexpr signed char kBoxFloorSeaPickle = 2;
constexpr signed char kBoxFloorTorch = 3;
constexpr signed char kBoxFloorPotNorthWall = 4;
constexpr signed char kBoxFloorPotSouthWall = 5;
constexpr signed char kBoxFloorPotSoil = 6;
constexpr signed char kBoxFloorRedstoneBench = 7;
constexpr signed char kBoxFloorDaylightBench = 8;
constexpr signed char kBoxFloorLecternPlinth = 9;
constexpr signed char kBoxFloorLecternDesk = 10;
constexpr signed char kBoxFloorCampfireBar = 11;
constexpr signed char kBoxFloorSoulCampfireBar = 12;
constexpr signed char kBoxFloorCampfirePlank = 13;
constexpr signed char kBoxFloorAnvilTop = 14;

constexpr BoxFaceOverride kBoxFloorFaces[]{
    // kBoxFloorCandle - `template_candle` #0 `down [0,14,2,16]` against its
    // `up [0,6,2,8]`. The wick is on top; the flat cut end is underneath.
    {.uMin = 0.0f, .vMin = 14 * kBoxFaceTexel, .uMax = 2 * kBoxFaceTexel, .vMax = 1.0f},
    // kBoxFloorBamboo - `bamboo1_age0` #0 `down [13,4,15,6]`, `up [13,0,15,2]`.
    {.uMin = 13 * kBoxFaceTexel,
     .vMin = 4 * kBoxFaceTexel,
     .uMax = 15 * kBoxFaceTexel,
     .vMax = 6 * kBoxFaceTexel},
    // kBoxFloorSeaPickle - `sea_pickle` #0 `down [8,1,12,5]`, `up [4,1,8,5]`.
    {.uMin = 8 * kBoxFaceTexel,
     .vMin = 1 * kBoxFaceTexel,
     .uMax = 12 * kBoxFaceTexel,
     .vMax = 5 * kBoxFaceTexel},
    // kBoxFloorTorch - `template_torch` #0 `down [7,13,9,15]`, `up [7,6,9,8]`.
    // A floor torch's underside is against the ground, but a wall torch stands
    // three texels clear of it and this is the face you see under it.
    {.uMin = 7 * kBoxFaceTexel,
     .vMin = 13 * kBoxFaceTexel,
     .uMax = 9 * kBoxFaceTexel,
     .vMax = 15 * kBoxFaceTexel},
    // kBoxFloorPotNorthWall - `flower_pot` #2 `down [6,10,10,11]` against its
    // `up [6,5,10,6]`. Seen from below the pot's near and far walls swap.
    {.uMin = 6 * kBoxFaceTexel,
     .vMin = 10 * kBoxFaceTexel,
     .uMax = 10 * kBoxFaceTexel,
     .vMax = 11 * kBoxFaceTexel},
    // kBoxFloorPotSouthWall - `flower_pot` #3 `down [6,5,10,6]`, `up
    // [6,10,10,11]`. The other half of the same swap.
    {.uMin = 6 * kBoxFaceTexel,
     .vMin = 5 * kBoxFaceTexel,
     .uMax = 10 * kBoxFaceTexel,
     .vMax = 6 * kBoxFaceTexel},
    // kBoxFloorPotSoil - `flower_pot` #4 `down [6,12,10,16]` on `#flowerpot`
    // where `up [6,6,10,10]` is `#dirt`. **The soil's underside is the pot**,
    // which is what `kBoxFaceOwnBlock` is for: the pot's own art is what every
    // other box of it already falls back to.
    {.uMin = 6 * kBoxFaceTexel,
     .vMin = 12 * kBoxFaceTexel,
     .uMax = 10 * kBoxFaceTexel,
     .vMax = 1.0f,
     .layer = kBoxFaceOwnBlock},
    // kBoxFloorRedstoneBench - `repeater_1tick` and `comparator` #0 `down
    // [0,0,16,16]` on `#slab`, which both models resolve to `smooth_stone` -
    // the same picture their four walls wear. **And `turns` 0**: the lid turns
    // with the block because one sprite carries the painted torch sockets along
    // an axis, and the belly has no sockets and no rotation in the model.
    {.layer = static_cast<float>(kRedstoneSlabSprite), .turns = 0},
    // kBoxFloorDaylightBench - `template_daylight_detector` #0 `down
    // [0,0,16,16]` on `#side`, not the glass `#top`.
    {.layer = static_cast<float>(kDaylightSideSprite)},
    // kBoxFloorLecternPlinth - `lectern` #0 `down [0,0,16,16]` on `#bottom`,
    // which that model states as `block/oak_planks`. **`.turns = 0` is load
    // bearing**: the box above now sets `lidTurns = 2` for the `up` face, which
    // this model turns half round and the `down` face beside it does not.
    {.layer = kPlanksLayer, .turns = 0},
    // kBoxFloorLecternDesk - `lectern` #2 `down [0,0,16,13]` on `#bottom`
    // against an `up` of `#top`. The underside of the sloping desk is plain
    // planks and thirteen texels deep, not sixteen. Same split as the plinth:
    // `up` is turned half round, `down` is not.
    {.uMin = 0.0f,
     .vMin = 0.0f,
     .uMax = 1.0f,
     .vMax = 13 * kBoxFaceTexel,
     .layer = kPlanksLayer,
     .turns = 0},
    // kBoxFloorCampfireBar - `template_campfire` #1 and #3 `down [0,4,16,8]` on
    // `#lit_log`, no rotation, against an `up` of `#log` turned half round.
    // **The embers are on the underside of the upper logs**, which is the half
    // of the block a player standing beside it actually sees lit.
    {.uMin = 0.0f,
     .vMin = 4 * kBoxFaceTexel,
     .uMax = 1.0f,
     .vMax = 8 * kBoxFaceTexel,
     .layer = kCampfireLitLogLayer,
     .turns = 0},
    // kBoxFloorSoulCampfireBar - the same row against the soul fire's logs.
    {.uMin = 0.0f,
     .vMin = 4 * kBoxFaceTexel,
     .uMax = 1.0f,
     .vMax = 8 * kBoxFaceTexel,
     .layer = kSoulCampfireLitLogLayer,
     .turns = 0},
    // kBoxFloorCampfirePlank - `template_campfire` #4 `down [0,8,16,14]` turned
    // a quarter on `#log`, where `up` is the same rectangle on `#lit_log`. Same
    // rectangle, same turn, other picture - so this row states only the layer.
    {.layer = kCampfireLogLayer},
    // kBoxFloorAnvilTop - `template_anvil` #3 `down` on `#body` where `up` is
    // `#top`. This box states neither layer, so both fell through to the block
    // - and `blockTextureLayer(Anvil, Bottom)` answers `anvil_top`, measured.
    // The underside of the overhang is the one face of an anvil that showed the
    // striking surface from below.
    {.layer = kAnvilBodyLayer},
};

constexpr int kBoxFloorFaceCount =
    static_cast<int>(sizeof(kBoxFloorFaces) / sizeof(kBoxFloorFaces[0]));

/// Row of `kBoxWallFaces`, each holding four faces in +X, -X, +Z, -Z order.
constexpr signed char kBoxWallsSeaPickle = 0;
constexpr signed char kBoxWallsPotWestWall = 1;
constexpr signed char kBoxWallsPotEastWall = 2;
constexpr signed char kBoxWallsLecternDesk = 3;
constexpr signed char kBoxWallsCampfireWestLog = 4;
constexpr signed char kBoxWallsSoulCampfireWestLog = 5;
constexpr signed char kBoxWallsCampfireEastLog = 6;
constexpr signed char kBoxWallsSoulCampfireEastLog = 7;
constexpr signed char kBoxWallsCampfireBar = 8;
constexpr signed char kBoxWallsCampfirePlank = 9;

constexpr BoxWallFaces kBoxWallFaces[]{
    // kBoxWallsSeaPickle - `sea_pickle` #0 gives its four walls four quarters
    // of one strip: `east [12,5,16,11]`, `west [8,5,12,11]`, `south
    // [0,5,4,11]`, `north [4,5,8,11]`. The box already carries `north`, so -Z
    // states nothing.
    {{{.uMin = 12 * kBoxFaceTexel,
       .vMin = 5 * kBoxFaceTexel,
       .uMax = 1.0f,
       .vMax = 11 * kBoxFaceTexel},
      {.uMin = 8 * kBoxFaceTexel,
       .vMin = 5 * kBoxFaceTexel,
       .uMax = 12 * kBoxFaceTexel,
       .vMax = 11 * kBoxFaceTexel},
      {.uMin = 0.0f,
       .vMin = 5 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 11 * kBoxFaceTexel},
      {}}},
    // kBoxWallsPotWestWall - `flower_pot` #0. Its two wide faces are the
    // six-texel `[5,10,11,16]` the box already carries; its two *ends* are one
    // texel wide - `north [10,10,11,16]`, `south [5,10,6,16]` - and were
    // wearing the wide rectangle squeezed onto a one-texel face.
    {{{},
      {},
      {.uMin = 5 * kBoxFaceTexel,
       .vMin = 10 * kBoxFaceTexel,
       .uMax = 6 * kBoxFaceTexel,
       .vMax = 1.0f},
      {.uMin = 10 * kBoxFaceTexel,
       .vMin = 10 * kBoxFaceTexel,
       .uMax = 11 * kBoxFaceTexel,
       .vMax = 1.0f}}},
    // kBoxWallsPotEastWall - `flower_pot` #1, the same two ends the other way
    // round: `north [5,10,6,16]`, `south [10,10,11,16]`.
    {{{},
      {},
      {.uMin = 10 * kBoxFaceTexel,
       .vMin = 10 * kBoxFaceTexel,
       .uMax = 11 * kBoxFaceTexel,
       .vMax = 1.0f},
      {.uMin = 5 * kBoxFaceTexel,
       .vMin = 10 * kBoxFaceTexel,
       .uMax = 6 * kBoxFaceTexel,
       .vMax = 1.0f}}},
    // kBoxWallsLecternDesk - `lectern` #2: `south [0,4,16,8]` is what the box
    // carries, `east` and `west` are thirteen texels of it - `[0,4,13,8]`, the
    // desk being thirteen deep - and `north [0,0,16,4]` is a different row of
    // the sheet entirely, that being the lip a book rests against.
    {{{.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 13 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 13 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel},
      {},
      {.uMin = 0.0f, .vMin = 0.0f, .uMax = 1.0f, .vMax = 4 * kBoxFaceTexel}}},
    // kBoxWallsCampfireWestLog - `template_campfire` #0, the log at x 1-5. Its
    // +X face looks in at the fire and the model paints it `#lit_log
    // [0,1,16,5]`; its two ends are the four-by-four cut grain at `[0,4,4,8]`,
    // not the sixteen-texel strip the box carries.
    {{{.uMin = 0.0f,
       .vMin = 1 * kBoxFaceTexel,
       .uMax = 1.0f,
       .vMax = 5 * kBoxFaceTexel,
       .layer = kCampfireLitLogLayer},
      {},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel}}},
    // kBoxWallsSoulCampfireWestLog - the same against soul fire's embers.
    {{{.uMin = 0.0f,
       .vMin = 1 * kBoxFaceTexel,
       .uMax = 1.0f,
       .vMax = 5 * kBoxFaceTexel,
       .layer = kSoulCampfireLitLogLayer},
      {},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel}}},
    // kBoxWallsCampfireEastLog - `template_campfire` #2, the log at x 11-15,
    // whose *-X* face is the one looking in at the fire.
    {{{},
      {.uMin = 0.0f,
       .vMin = 1 * kBoxFaceTexel,
       .uMax = 1.0f,
       .vMax = 5 * kBoxFaceTexel,
       .layer = kCampfireLitLogLayer},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel}}},
    // kBoxWallsSoulCampfireEastLog - as above for the soul fire.
    {{{},
      {.uMin = 0.0f,
       .vMin = 1 * kBoxFaceTexel,
       .uMax = 1.0f,
       .vMax = 5 * kBoxFaceTexel,
       .layer = kSoulCampfireLitLogLayer},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel}}},
    // kBoxWallsCampfireBar - `template_campfire` #1 and #3, the upper pair. Ours
    // carries `#lit_log` on all four walls because the two long ones want it;
    // the model's `east` and `west` are the plain `#log` cut grain at
    // `[0,4,4,8]`. Both variants share this row: the ends are never lit.
    {{{.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel,
       .layer = kCampfireLogLayer},
      {.uMin = 0.0f,
       .vMin = 4 * kBoxFaceTexel,
       .uMax = 4 * kBoxFaceTexel,
       .vMax = 8 * kBoxFaceTexel,
       .layer = kCampfireLogLayer},
      {},
      {}}},
    // kBoxWallsCampfirePlank - `template_campfire` #4: `north [0,15,6,16]` is
    // what the box carries and `south` is the other end of the same row,
    // `[10,15,16,16]`.
    {{{},
      {},
      {.uMin = 10 * kBoxFaceTexel,
       .vMin = 15 * kBoxFaceTexel,
       .uMax = 1.0f,
       .vMax = 1.0f},
      {}}},
};

constexpr int kBoxWallFaceCount =
    static_cast<int>(sizeof(kBoxWallFaces) / sizeof(kBoxWallFaces[0]));

/// The row this face reads, or `nullptr` for a face that has nothing to say -
/// which is every face of every box that names no row, and the lid of every box
/// that does.
constexpr const BoxFaceOverride* boxFaceOverride(const ModelBox& box, AxisFace face) {
    if (boxFaceIsFloor(face)) {
        return box.floorOverride < 0 ? nullptr : &kBoxFloorFaces[box.floorOverride];
    }
    const int slot = boxWallSlot(face);
    if (slot < 0 || box.wallOverride < 0) {
        return nullptr;
    }
    return &kBoxWallFaces[box.wallOverride].face[slot];
}

/// **The layer this face of this box wants**, or a negative number meaning the
/// block's own answer for that face - which is the same contract `sideLayer`
/// and `lidLayer` already had, so nothing downstream of this changes.
constexpr float boxFaceLayer(const ModelBox& box, AxisFace face) {
    const float own = boxFaceIsLidOrFloor(face) ? box.lidLayer : box.sideLayer;
    const BoxFaceOverride* said = boxFaceOverride(box, face);
    if (said == nullptr) {
        return own;
    }
    if (said->layer >= 0.0f) {
        return said->layer;
    }
    // Threshold, never equality: these are sentinels carried in a float.
    if (said->layer < -1.5f) {
        return -1.0f;
    }
    return own;
}

/// **The rectangle this face of this box wants.**
constexpr FaceRect boxFaceRect(const ModelBox& box, AxisFace face) {
    const FaceRect own = boxFaceIsLidOrFloor(face)
                             ? FaceRect{box.topUMin, box.topVMin, box.topUMax, box.topVMax}
                             : FaceRect{box.uMin, box.vMin, box.uMax, box.vMax};
    const BoxFaceOverride* said = boxFaceOverride(box, face);
    if (said == nullptr || said->uMin < 0.0f) {
        return own;
    }
    return {said->uMin, said->vMin, said->uMax, said->vMax};
}

/// **The quarter turns this face of this box wants**, clockwise, the way
/// `lidTurns` counts them.
///
/// `lidTurns` was gated on the same axis test as the rectangle and the layer,
/// so splitting those two and not this one would have put the right picture on
/// a repeater's belly at the wrong angle - a half fix that looks whole. A wall
/// has never had a turn of its own and the reference gives several of them one;
/// no shipping box states one yet, and the two that want it are recorded
/// against this row rather than guessed at.
constexpr unsigned char boxFaceTurns(const ModelBox& box, AxisFace face) {
    const unsigned char own = boxFaceIsLidOrFloor(face) ? box.lidTurns : 0;
    const BoxFaceOverride* said = boxFaceOverride(box, face);
    if (said == nullptr || said->turns < 0) {
        return own;
    }
    return static_cast<unsigned char>(said->turns);
}

// Each named row against a constant known from somewhere other than the table,
// so a constant that drifts from the row it names fails here rather than
// quietly painting the wrong thing. `kPlanksLayer` is separately asserted
// against `TextureLayer::Planks`.
static_assert(kBoxFloorFaceCount == 15);
static_assert(kBoxWallFaceCount == 10);
static_assert(kBoxFloorFaces[kBoxFloorLecternPlinth].layer == kPlanksLayer);
static_assert(kBoxFloorFaces[kBoxFloorLecternDesk].layer == kPlanksLayer);
static_assert(kBoxFloorFaces[kBoxFloorPotSoil].layer == kBoxFaceOwnBlock);
static_assert(kBoxFloorFaces[kBoxFloorCampfireBar].layer == kCampfireLitLogLayer);
static_assert(kBoxFloorFaces[kBoxFloorSoulCampfireBar].layer == kSoulCampfireLitLogLayer);
static_assert(kBoxFloorFaces[kBoxFloorCampfirePlank].layer == kCampfireLogLayer);
static_assert(kBoxFloorFaces[kBoxFloorCandle].vMax == 1.0f);
static_assert(kBoxWallFaces[kBoxWallsCampfireWestLog].face[0].layer == kCampfireLitLogLayer);
static_assert(kBoxWallFaces[kBoxWallsCampfireEastLog].face[1].layer == kCampfireLitLogLayer);
static_assert(kBoxWallFaces[kBoxWallsCampfireBar].face[0].layer == kCampfireLogLayer);

/// The reference's own `template_lantern`, `end_rod` and `cocoa_stage2`
/// models, in sixteenths, face rectangles and all - and, since the redstone
/// round, the lever, the repeater, the comparator, the piston head, the
/// daylight detector, the lightning rod and the tripwire hook.
///
/// The lantern's handle is the reference's two quads crossed through the middle
/// of the cell. Ours are a texel thick and **cross at a right angle where the
/// reference turns them 45 degrees**, because the box mesher has no rotation -
/// the silhouette is the same from the four cardinal directions and reads as a
/// loop from every other, which is all a three-texel handle has to do. The
/// hole in the middle of each is the artwork's own transparency, which works
/// because a lantern is a cutout block.
///
/// The cocoa's stem quad is left out: it is a zero-thickness plane and there is
/// nothing here to make one from.
constexpr ModelBoxes postModel(BlockId id) {
    constexpr float t = 1.0f / 16.0f;
    ModelBoxes result;
    // ---- Redstone. Every box below is the reference's own model JSON. ----
    //
    // **Named divergence, and it covers this whole family: none of these tilts.**
    // The reference turns a lever's handle 45 degrees, leans a wall torch back
    // 22.5 and hangs a tripwire hook at 45, and the box mesher has no rotation
    // at all. Where a tilt is what reads as state - a lever thrown one way or
    // the other - the box slides instead, which is legible from every angle a
    // player will look at it from.
    if (isLever(id)) {
        // `lever.json`: base `5,-0.02,4` to `11,2.98,12` in cobblestone, handle
        // `7,1,7` to `9,11,9`. The base deliberately pokes two hundredths of a
        // texel into whatever it is screwed to, which is the reference stopping
        // two faces sharing a plane - the same rule the lantern's handle taught
        // us. Ours is smooth stone rather than cobblestone, because a model box
        // names its layer as a number and the redstone run has a stone in it.
        const int mount = leverMount(id);
        const bool on = leverOn(id);
        const float lean = on ? 2 * t : -2 * t;
        const bool ceiling = mount == LeverCeilingX || mount == LeverCeilingZ;
        const bool floorOrCeiling = mount <= LeverCeilingZ;
        // The throw runs along X for the "X" mounts and along Z for the others;
        // a wall lever always throws up and down.
        const bool alongX = mount == LeverFloorX || mount == LeverCeilingX;
        if (floorOrCeiling) {
            const float baseLo = ceiling ? 13 * t : 0.0f;
            const float baseHi = ceiling ? 1.0f : 3 * t;
            result.boxes[0] = {alongX ? BlockBox{5 * t, baseLo, 4 * t, 11 * t, baseHi, 12 * t}
                                      : BlockBox{4 * t, baseLo, 5 * t, 12 * t, baseHi, 11 * t},
                               0.0f, 13 * t, 1.0f, 1.0f,
                               0.0f, 0.0f, 1.0f, 1.0f,
                               static_cast<float>(kRedstoneSlabSprite),
                               static_cast<float>(kRedstoneSlabSprite)};
            const float lo = ceiling ? 5 * t : t;
            const float hi = ceiling ? 15 * t : 11 * t;
            const float slide = ceiling ? -lean : lean;
            result.boxes[1] = {alongX ? BlockBox{7 * t + slide, lo, 7 * t, 9 * t + slide, hi, 9 * t}
                                      : BlockBox{7 * t, lo, 7 * t + slide, 9 * t, hi, 9 * t + slide},
                               7 * t, 6 * t, 9 * t, 1.0f,
                               7 * t, 6 * t, 9 * t, 8 * t};
        } else {
            const FaceDirection wall = static_cast<FaceDirection>(mount - LeverWallFirst);
            const float near = 0.0f;
            const float far = 3 * t;
            // Standing against the wall: six texels across it, eight up it.
            switch (wall) {
            case FaceDirection::PosX:
                result.boxes[0] = {1.0f - far, 4 * t, 5 * t, 1.0f - near, 12 * t, 11 * t,
                                   0.0f, 13 * t, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                   static_cast<float>(kRedstoneSlabSprite),
                                   static_cast<float>(kRedstoneSlabSprite)};
                result.boxes[1] = {1.0f - 11 * t, 7 * t + lean, 7 * t,
                                   1.0f - t,      9 * t + lean, 9 * t,
                                   7 * t, 6 * t, 9 * t, 1.0f, 7 * t, 6 * t, 9 * t, 8 * t};
                break;
            case FaceDirection::NegX:
                result.boxes[0] = {near, 4 * t, 5 * t, far, 12 * t, 11 * t,
                                   0.0f, 13 * t, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                   static_cast<float>(kRedstoneSlabSprite),
                                   static_cast<float>(kRedstoneSlabSprite)};
                result.boxes[1] = {t, 7 * t + lean, 7 * t, 11 * t, 9 * t + lean, 9 * t,
                                   7 * t, 6 * t, 9 * t, 1.0f, 7 * t, 6 * t, 9 * t, 8 * t};
                break;
            case FaceDirection::PosZ:
                result.boxes[0] = {5 * t, 4 * t, 1.0f - far, 11 * t, 12 * t, 1.0f - near,
                                   0.0f, 13 * t, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                   static_cast<float>(kRedstoneSlabSprite),
                                   static_cast<float>(kRedstoneSlabSprite)};
                result.boxes[1] = {7 * t, 7 * t + lean, 1.0f - 11 * t,
                                   9 * t, 9 * t + lean, 1.0f - t,
                                   7 * t, 6 * t, 9 * t, 1.0f, 7 * t, 6 * t, 9 * t, 8 * t};
                break;
            default:
                result.boxes[0] = {5 * t, 4 * t, near, 11 * t, 12 * t, far,
                                   0.0f, 13 * t, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,
                                   static_cast<float>(kRedstoneSlabSprite),
                                   static_cast<float>(kRedstoneSlabSprite)};
                result.boxes[1] = {7 * t, 7 * t + lean, t, 9 * t, 9 * t + lean, 11 * t,
                                   7 * t, 6 * t, 9 * t, 1.0f, 7 * t, 6 * t, 9 * t, 8 * t};
                break;
            }
        }
        result.count = 2;
        return result;
    }
    // A torch on a wall. The reference's `template_redstone_torch_wall` is the
    // floor stick leaned back 22.5 degrees out of the wall; ours stands upright
    // three texels clear of it, raised so the flame sits at the same height.
    if (isWallTorch(id)) {
        const float layer = redstoneTorchLit(id) ? kRedstoneTorchLayer
                                                 : static_cast<float>(kRedstoneTorchOffSprite);
        switch (redstoneTorchWall(id)) {
        case FaceDirection::PosX:
            result.boxes[0] = {12 * t, 3 * t, 7 * t, 14 * t, 13 * t, 9 * t};
            break;
        case FaceDirection::NegX:
            result.boxes[0] = {2 * t, 3 * t, 7 * t, 4 * t, 13 * t, 9 * t};
            break;
        case FaceDirection::PosZ:
            result.boxes[0] = {7 * t, 3 * t, 12 * t, 9 * t, 13 * t, 14 * t};
            break;
        default:
            result.boxes[0] = {7 * t, 3 * t, 2 * t, 9 * t, 13 * t, 4 * t};
            break;
        }
        result.boxes[0].uMin = 7 * t;
        result.boxes[0].vMin = 6 * t;
        result.boxes[0].uMax = 9 * t;
        result.boxes[0].vMax = 1.0f;
        result.boxes[0].topUMin = 7 * t;
        result.boxes[0].topVMin = 6 * t;
        result.boxes[0].topUMax = 9 * t;
        result.boxes[0].topVMax = 8 * t;
        result.boxes[0].sideLayer = layer;
        result.boxes[0].lidLayer = layer;
        // A wall torch stands clear of the floor, so the cut end underneath it
        // is one a player sees - and it is `[7,13,9,15]`, not the flame square.
        result.boxes[0].floorOverride = kBoxFloorTorch;
        result.count = 1;
        return result;
    }
    if (isRepeater(id) || isComparator(id)) {
        // Both stand on the same bench: `0,0,0` to `16,2,16`, smooth stone all
        // round with the block's own picture on top.
        const bool repeater = isRepeater(id);
        const float top = repeater
                              ? static_cast<float>(repeaterPowered(id) ? kRepeaterOnSprite
                                                                      : kRepeaterSprite)
                              : static_cast<float>(comparatorPowered(id) || comparatorSubtracts(id)
                                                       ? kComparatorOnSprite
                                                       : kComparatorSprite);
        // A torch two texels wide, standing on the bench. `along` measures from
        // the **output** end, which is the end the facing points at.
        const FaceDirection facing = repeater ? repeaterFacing(id) : comparatorFacing(id);
        // **The lid turns with the block, exactly as a bed's does.** There is
        // one sprite per state and not four, and `repeater.png` carries the
        // painted torch sockets along one axis - so an unturned lid ran them
        // across the real 3-D torches on an east/west repeater and end-for-end
        // backwards on a south-facing one. Three facings out of four were
        // wrong, on the two blocks a player orients more often than any other.
        result.boxes[0] = {{0.0f, 0.0f, 0.0f, 1.0f, 2 * t, 1.0f},
                           0.0f, 14 * t, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f,
                           static_cast<float>(kRedstoneSlabSprite), top, lidTurnsFacing(facing)};
        // And the belly is smooth stone, unturned. It used to be the lid: the
        // arrows off the top of a repeater, painted on its underside and turned
        // with the block along with them.
        result.boxes[0].floorOverride = kBoxFloorRedstoneBench;
        result.count = 1;
        const auto torch = [&](float along, float across, float height, bool lit) {
            const float layer = lit ? kRedstoneTorchLayer
                                    : static_cast<float>(kRedstoneTorchOffSprite);
            const float a0 = along;
            const float a1 = along + 2 * t;
            const float c0 = across;
            const float c1 = across + 2 * t;
            BlockBox box{};
            switch (facing) {
            case FaceDirection::NegZ:
                box = {c0, 2 * t, a0, c1, 2 * t + height, a1};
                break;
            case FaceDirection::PosZ:
                box = {1.0f - c1, 2 * t, 1.0f - a1, 1.0f - c0, 2 * t + height, 1.0f - a0};
                break;
            case FaceDirection::NegX:
                box = {a0, 2 * t, 1.0f - c1, a1, 2 * t + height, 1.0f - c0};
                break;
            default:
                box = {1.0f - a1, 2 * t, c0, 1.0f - a0, 2 * t + height, c1};
                break;
            }
            result.boxes[result.count++] = {box,
                                            7 * t, 6 * t, 9 * t, 11 * t,
                                            7 * t, 6 * t, 9 * t, 8 * t,
                                            layer, layer};
        };
        if (repeater) {
            // The fixed torch sits two texels in from the output end; the one
            // that moves is the delay, and it steps two texels per setting.
            torch(2 * t, 7 * t, 5 * t, repeaterPowered(id));
            torch(static_cast<float>(4 + 2 * repeaterDelay(id)) * t, 7 * t, 5 * t,
                  repeaterPowered(id));
        } else {
            // A pair at the **back**, five tall, and a single one at the output
            // end three tall - which is the comparator's only asymmetry, and is
            // the same in both modes. `comparator.json` states all three:
            // `[4,2,11]-[6,7,13]`, `[10,2,11]-[12,7,13]` and `[7,2,2]-[9,5,4]`,
            // and `comparator_on` lights the pair while `comparator_subtract`
            // lights the single one. The mode is told by which torch is lit,
            // never by its height. An older comment here had the two ends the
            // wrong way round; the boxes were always right.
            torch(11 * t, 4 * t, 5 * t, comparatorPowered(id));
            torch(11 * t, 10 * t, 5 * t, comparatorPowered(id));
            torch(2 * t, 7 * t, 3 * t, comparatorSubtracts(id));
        }
        return result;
    }
    if (isPistonHead(id)) {
        // `template_piston_head`: a plate `0,0,0` to `16,16,4` and an arm
        // `6,6,4` to `10,10,20`. **The reference's arm runs four texels outside
        // its own cell**, back into the piston that pushed it; ours stops at the
        // cell wall, which is the short head the reference also ships.
        const int facing = pistonHeadFacing(id);
        const float plate = static_cast<float>(pistonHeadSticky(id) ? kPistonTopStickySprite
                                                                    : kPistonTopSprite);
        const float side = static_cast<float>(kPistonSideSprite);
        const auto along = [&](float lo, float hi, float half) {
            switch (facing) {
            case Facing6Up:
                return BlockBox{0.5f - half, 1.0f - hi, 0.5f - half, 0.5f + half, 1.0f - lo,
                                0.5f + half};
            case Facing6Down:
                return BlockBox{0.5f - half, lo, 0.5f - half, 0.5f + half, hi, 0.5f + half};
            case Facing6South:
                return BlockBox{0.5f - half, 0.5f - half, 1.0f - hi, 0.5f + half, 0.5f + half,
                                1.0f - lo};
            case Facing6North:
                return BlockBox{0.5f - half, 0.5f - half, lo, 0.5f + half, 0.5f + half, hi};
            case Facing6East:
                return BlockBox{1.0f - hi, 0.5f - half, 0.5f - half, 1.0f - lo, 0.5f + half,
                                0.5f + half};
            default:
                return BlockBox{lo, 0.5f - half, 0.5f - half, hi, 0.5f + half, 0.5f + half};
            }
        };
        result.boxes[0] = {along(0.0f, 4 * t, 0.5f), 0.0f, 0.0f, 1.0f, 4 * t,
                           0.0f, 0.0f, 1.0f, 1.0f, side, plate};
        result.boxes[1] = {along(4 * t, 1.0f, 2 * t), 0.0f, 0.0f, 1.0f, 12 * t,
                           0.0f, 0.0f, 4 * t, 4 * t, side, side};
        result.count = 2;
        return result;
    }
    if (isDaylightDetector(id)) {
        // `template_daylight_detector`: `0,0,0` to `16,6,16`, sides sampling
        // rows 10 to 16 of the side texture.
        result.boxes[0] = {{0.0f, 0.0f, 0.0f, 1.0f, 6 * t, 1.0f},
                           0.0f, 10 * t, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f,
                           static_cast<float>(kDaylightSideSprite),
                           static_cast<float>(daylightDetectorInverted(id)
                                                  ? kDaylightInvertedTopSprite
                                                  : kDaylightTopSprite)};
        // `down` is `#side` over the whole sheet, not the glass. The underside
        // of a daylight detector was a second sensor face.
        result.boxes[0].floorOverride = kBoxFloorDaylightBench;
        result.count = 1;
        return result;
    }
    if (isLightningRod(id)) {
        // `template_lightning_rod`: head `6,12,6` to `10,16,10` sampling the
        // top-left 4x4 of the sheet, shaft `7,0,7` to `9,12,9` sampling a
        // two-texel column of rows 4 to 16. **Every one of its eleven faces
        // needs an explicit rectangle** - this model is cut out of a corner of
        // its texture rather than out of a cube, which is exactly the case
        // `ModelBox` exists for.
        const float layer = static_cast<float>(lightningRodPowered(id) ? kLightningRodOnSprite
                                                                       : kLightningRodSprite);
        const int facing = lightningRodFacing(id);
        // The rod stands along whichever axis it was stuck to, tip outward.
        const auto rod = [&](float lo, float hi, float half) {
            switch (facing) {
            case Facing6Down:
                return BlockBox{0.5f - half, 1.0f - hi, 0.5f - half, 0.5f + half, 1.0f - lo,
                                0.5f + half};
            case Facing6North:
                return BlockBox{0.5f - half, 0.5f - half, 1.0f - hi, 0.5f + half, 0.5f + half,
                                1.0f - lo};
            case Facing6South:
                return BlockBox{0.5f - half, 0.5f - half, lo, 0.5f + half, 0.5f + half, hi};
            case Facing6West:
                return BlockBox{1.0f - hi, 0.5f - half, 0.5f - half, 1.0f - lo, 0.5f + half,
                                0.5f + half};
            case Facing6East:
                return BlockBox{lo, 0.5f - half, 0.5f - half, hi, 0.5f + half, 0.5f + half};
            default:
                return BlockBox{0.5f - half, lo, 0.5f - half, 0.5f + half, hi, 0.5f + half};
            }
        };
        result.boxes[0] = {rod(0.0f, 12 * t, t), 0.0f, 4 * t, 2 * t, 1.0f,
                           0.0f, 4 * t, 2 * t, 6 * t, layer, layer};
        result.boxes[1] = {rod(12 * t, 1.0f, 2 * t), 0.0f, 0.0f, 4 * t, 4 * t,
                           0.0f, 0.0f, 4 * t, 4 * t, layer, layer};
        result.count = 2;
        return result;
    }
    if (isTripwireHook(id)) {
        // `tripwire_hook.json`, reduced to the two boxes that read: the post
        // against the wall, `6,1,14` to `10,9,16`, and the hook plate in front
        // of it. The reference's four one-texel slivers round the hook are left
        // out, as the redstone torch's glow shell is.
        const float layer = static_cast<float>(kTripwireHookSprite);
        const FaceDirection wall = tripwireHookFacing(id);
        const bool tripped = tripwireHookPowered(id);
        const float hookY = tripped ? 4 * t : 5 * t;
        const auto place = [&](float lo, float hi, float halfX, float y0, float y1) {
            switch (wall) {
            case FaceDirection::PosX:
                return BlockBox{1.0f - hi, y0, 0.5f - halfX, 1.0f - lo, y1, 0.5f + halfX};
            case FaceDirection::NegX:
                return BlockBox{lo, y0, 0.5f - halfX, hi, y1, 0.5f + halfX};
            case FaceDirection::PosZ:
                return BlockBox{0.5f - halfX, y0, 1.0f - hi, 0.5f + halfX, y1, 1.0f - lo};
            default:
                return BlockBox{0.5f - halfX, y0, lo, 0.5f + halfX, y1, hi};
            }
        };
        result.boxes[0] = {place(0.0f, 2 * t, 2 * t, t, 9 * t),
                           6 * t, 7 * t, 10 * t, 15 * t,
                           6 * t, 0.0f, 10 * t, 2 * t, layer, layer};
        result.boxes[1] = {place(2 * t, 6 * t, 2 * t, hookY, hookY + t),
                           5 * t, 3 * t, 11 * t, 9 * t,
                           5 * t, 3 * t, 11 * t, 9 * t, layer, layer};
        result.count = 2;
        return result;
    }
    // A mattress on legs, which is what the side texture is drawn for: rows 0-6
    // are transparent, rows 7-13 are the mattress side and its frame, and rows
    // 13-16 hold three 3x3 leg faces packed side by side. Sampling from row 0
    // therefore drew mostly nothing and no legs at all.
    //
    // The lid needs turning as well. The top face's u runs along +X and its v
    // along +Z, so an unturned head texture puts the pillow at -Z whichever way
    // the bed lies, and three of the four facings had the pillow against the
    // join in the middle instead of at the end.
    if (isBed(id)) {
        const FaceDirection lie = bedFacing(id);
        const unsigned char turns = lidTurnsFacing(lie);
        // The long side a quarter turn to the left of the head, which is the
        // one the automatic mirror gets backwards.
        const FaceDirection unmirror = lie == FaceDirection::NegZ   ? FaceDirection::NegX
                                       : lie == FaceDirection::PosX ? FaceDirection::NegZ
                                       : lie == FaceDirection::PosZ ? FaceDirection::PosX
                                                                    : FaceDirection::PosZ;
        // The top is one whole face, not a net: the reference's
        // `template_bed_head` samples `up` at uv 0,0-16,16 over the whole cell,
        // and the head texture's rows 0-7 are the pillow, row 8 the shadow it
        // casts and rows 9-15 the blanket. Cropping to the pillow stretched it
        // over the entire head half and left the blanket nowhere, so the red
        // began at the join instead of covering the middle of the bed.
        result.boxes[0] = {{0.0f, 3 * t, 0.0f, 1.0f, kBedHeight, 1.0f},
                           0.0f, 7 * t, 1.0f, 13 * t,
                           0.0f, 0.0f, 1.0f, 1.0f, -1.0f, -1.0f, turns, unmirror};
        // Two legs per half, at the end of the bed that faces away from the
        // other half - so the pair meets in the middle and the whole bed stands
        // on four.
        //
        // The two halves keep their leg faces at opposite ends of the net: the
        // foot's are u 0-9 and the head's u 7-16, because we stage one side
        // image per half and the reference's are mirrors of each other. Reading
        // the foot's corner for both left the head's legs sampling empty
        // texture, so a bed stood on two.
        const float legU = bedIsHead(id) ? 13 * t : 0.0f;
        const bool alongX = lie == FaceDirection::PosX || lie == FaceDirection::NegX;
        const bool positive = lie == FaceDirection::PosX || lie == FaceDirection::PosZ;
        const bool outerHigh = bedIsHead(id) == positive;
        const float outerMin = outerHigh ? 13 * t : 0.0f;
        const float outerMax = outerHigh ? 1.0f : 3 * t;
        for (int side = 0; side < 2; ++side) {
            const float acrossMin = side == 0 ? 0.0f : 13 * t;
            const float acrossMax = side == 0 ? 3 * t : 1.0f;
            const BlockBox leg =
                alongX ? BlockBox{outerMin, 0.0f, acrossMin, outerMax, 3 * t, acrossMax}
                       : BlockBox{acrossMin, 0.0f, outerMin, acrossMax, 3 * t, outerMax};
            result.boxes[1 + side] = {leg,
                                      legU, 13 * t, legU + 3 * t, 1.0f,
                                      legU, 13 * t, legU + 3 * t, 1.0f};
        }
        result.count = 3;
        return result;
    }
    // Four walls round a plug of soil, six texels tall - the reference's own
    // `flower_pot`. It had been a `Cross`, so a pot drew as a full-height
    // crossed sheet like a flower: ten texels of block that is not there.
    if (id == BlockId::FlowerPot) {
        // **The lid rect is each wall's own footprint, not the pot's.**
        // `flower_pot.json` states `up` uv `[5,5,6,11]` for the x 5-6 wall and
        // `[10,5,11,11]` for the x 10-11 wall - one texel wide each, exactly
        // like the z walls below, which had theirs read off the model all
        // along. Handing the X pair the whole six-by-six interior instead
        // squashed the pot's inside colour into their one-texel rims, so a pot
        // seen from above was smeared left and right and right front and back.
        // `check-models.ps1` cannot see this: it compares union extents and
        // never looks at a `uv`.
        result.boxes[0] = {{5 * t, 0.0f, 5 * t, 6 * t, 6 * t, 11 * t},
                           5 * t, 10 * t, 11 * t, 1.0f,
                           5 * t, 5 * t, 6 * t, 11 * t};
        result.boxes[1] = {{10 * t, 0.0f, 5 * t, 11 * t, 6 * t, 11 * t},
                           5 * t, 10 * t, 11 * t, 1.0f,
                           10 * t, 5 * t, 11 * t, 11 * t};
        result.boxes[2] = {{6 * t, 0.0f, 5 * t, 10 * t, 6 * t, 6 * t},
                           6 * t, 10 * t, 10 * t, 1.0f,
                           6 * t, 5 * t, 10 * t, 6 * t};
        result.boxes[3] = {{6 * t, 0.0f, 10 * t, 10 * t, 6 * t, 11 * t},
                           6 * t, 10 * t, 10 * t, 1.0f,
                           6 * t, 10 * t, 10 * t, 11 * t};
        // The soil in it, which is the one part painted from another block.
        result.boxes[4] = {{6 * t, 0.0f, 6 * t, 10 * t, 4 * t, 10 * t},
                           6 * t, 12 * t, 10 * t, 1.0f,
                           6 * t, 6 * t, 10 * t, 10 * t, -1.0f, kDirtLayer};
        // **Five undersides and four wall ends the axis test could not tell
        // apart.** The soil's `down` is the pot, not the dirt on top of it; the
        // two z walls swap rectangles when seen from below; and the x walls'
        // one-texel ends were wearing the six-texel rectangle off their long
        // faces, stretched sideways.
        result.boxes[0].wallOverride = kBoxWallsPotWestWall;
        result.boxes[1].wallOverride = kBoxWallsPotEastWall;
        result.boxes[2].floorOverride = kBoxFloorPotNorthWall;
        result.boxes[3].floorOverride = kBoxFloorPotSouthWall;
        result.boxes[4].floorOverride = kBoxFloorPotSoil;
        result.count = 5;
        return result;
    }
    // ---- Five that were drawn as something they are not. ----
    //
    // A candle, bamboo and a sea pickle were `BlockShape::Cross`, so each
    // filled its whole cell with two crossed sheets of its own art; a lectern
    // and a dragon egg were plain cubes. Between them they are 141 of the rows
    // `check-models.ps1` reported the day it learnt to read a `uv`.

    // `template_candle` and its two-, three- and four-candle siblings: the same
    // two-by-two stick every time, and the only things that change with the
    // count are where each one stands and how tall it is. **That is the
    // table** - x, z and height in sixteenths - because four near-identical
    // branches is how the same number ends up written four times.
    //
    // The flames are left out for the reason the cocoa's stem quad is: each is
    // a pair of zero-thickness planes turned 45 degrees and the box mesher has
    // neither. Ours burn in the artwork instead - `make-reference-blocks.ps1`
    // stages the *lit* picture.
    if (isCandle(id)) {
        constexpr float sticks[4][4][3] = {{{7, 7, 6}},
                                           {{5, 7, 5}, {9, 6, 6}},
                                           {{7, 9, 3}, {5, 7, 5}, {8, 6, 6}},
                                           {{6, 8, 3}, {9, 8, 5}, {5, 5, 5}, {8, 5, 6}}};
        const int count = candleCount(id);
        for (int i = 0; i < count; ++i) {
            const float x = sticks[count - 1][i][0];
            const float z = sticks[count - 1][i][1];
            const float tall = sticks[count - 1][i][2];
            // Sides start at the sheet's row 8 and are as tall as the stick;
            // the lid is the wick square above them, and the cut end it stands
            // on is `[0,14,2,16]` - which is what `floorOverride` is for, and
            // which this comment used to say a `ModelBox` had no room for.
            result.boxes[i] = {{x * t, 0.0f, z * t, (x + 2) * t, tall * t, (z + 2) * t},
                               0.0f, 8 * t, 2 * t, (8 + tall) * t,
                               0.0f, 6 * t, 2 * t, 8 * t};
            result.boxes[i].floorOverride = kBoxFloorCandle;
        }
        result.count = count;
        return result;
    }
    // `bamboo1_age0.json`: a two-texel stalk the full height of the cell, whose
    // sides read the leftmost two columns of the sheet and whose cut end is the
    // little square at `[13,0,15,2]`. A cross drew the whole sheet across the
    // whole cell, which is a solid wall of bamboo colour.
    if (id == BlockId::Bamboo) {
        result.boxes[0] = {{7 * t, 0.0f, 7 * t, 9 * t, 1.0f, 9 * t},
                           0.0f, 0.0f, 2 * t, 1.0f,
                           13 * t, 0.0f, 15 * t, 2 * t};
        result.boxes[0].floorOverride = kBoxFloorBamboo;
        result.count = 1;
        return result;
    }
    // `sea_pickle.json`: one four-texel lump six texels tall. The reference
    // paints each side from a different quarter of the sheet and adds two
    // turned tendrils above it; ours leaves the tendrils, which is the same
    // no-tilt divergence the lever's family is named for. The four quarters
    // themselves are stated - each wall its own, and the cut end underneath.
    if (id == BlockId::SeaPickle) {
        result.boxes[0] = {{6 * t, 0.0f, 6 * t, 10 * t, 6 * t, 10 * t},
                           4 * t, 5 * t, 8 * t, 11 * t,
                           4 * t, 1 * t, 8 * t, 5 * t};
        result.boxes[0].floorOverride = kBoxFloorSeaPickle;
        result.boxes[0].wallOverride = kBoxWallsSeaPickle;
        result.count = 1;
        return result;
    }
    // `dragon_egg.json`, all six of its stacked slabs and every rectangle it
    // states. Each one sits *on* the one below, so the pairs of faces that meet
    // are a lid against a floor and back-face culling settles them - the same
    // arrangement as the end rod's base and stick.
    if (id == BlockId::DragonEgg) {
        result.boxes[0] = {{6 * t, 15 * t, 6 * t, 10 * t, 1.0f, 10 * t},
                           6 * t, 0.0f, 10 * t, 1 * t,
                           6 * t, 6 * t, 10 * t, 10 * t};
        result.boxes[1] = {{5 * t, 14 * t, 5 * t, 11 * t, 15 * t, 11 * t},
                           5 * t, 1 * t, 11 * t, 2 * t,
                           5 * t, 5 * t, 11 * t, 11 * t};
        result.boxes[2] = {{4 * t, 13 * t, 4 * t, 12 * t, 14 * t, 12 * t},
                           4 * t, 2 * t, 12 * t, 3 * t,
                           4 * t, 4 * t, 12 * t, 12 * t};
        result.boxes[3] = {{3 * t, 0.0f, 3 * t, 13 * t, 13 * t, 13 * t},
                           3 * t, 3 * t, 13 * t, 1.0f,
                           3 * t, 3 * t, 13 * t, 13 * t};
        result.boxes[4] = {{2 * t, 1 * t, 2 * t, 14 * t, 11 * t, 14 * t},
                           2 * t, 5 * t, 14 * t, 15 * t,
                           2 * t, 2 * t, 14 * t, 14 * t};
        result.boxes[5] = {{1 * t, 3 * t, 1 * t, 15 * t, 8 * t, 15 * t},
                           1 * t, 8 * t, 15 * t, 13 * t,
                           1 * t, 1 * t, 15 * t, 15 * t};
        result.count = 6;
        return result;
    }
    // `lectern.json`: a full-width plinth, an eight-texel post and the reading
    // desk across the top of it.
    //
    // **The desk does not tilt.** The reference swings it back 22.5 degrees
    // about x, which is the whole of the 2.45 texels `check-models.ps1` still
    // reports and is recorded there as a named divergence; every number below
    // is the model's own, untilted.
    //
    // Two of its four textures are staged but never loaded - `lectern_base`
    // under the plinth and `lectern_front` on the post's face - so those boxes
    // take the block's own side art. **The staging is not what is missing**:
    // `make-reference-blocks.ps1` carries both rows, and did before this
    // comment was corrected on 2026-08-19; what is missing is the pair of
    // entries in `Main.cpp`'s sprite list and the two layer constants here that
    // would index them. An earlier version of this comment said "not staged",
    // which would have sent a reader to add rows that already exist and to
    // conclude the job was done. The rectangles are still the reference's: the
    // plinth's `north` is the bottom two rows of the sheet and the post's
    // `west` is the block of it below the desk art.
    if (id == BlockId::Lectern) {
        result.boxes[0] = {{0.0f, 0.0f, 0.0f, 1.0f, 2 * t, 1.0f},
                           0.0f, 14 * t, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f, -1.0f, kLecternSideLayer};
        // Neither of the post's lids is ever seen - the plinth is under it and
        // the desk over it - and the reference states neither, so its side rect
        // stands in for both.
        result.boxes[1] = {{4 * t, 2 * t, 4 * t, 12 * t, 15 * t, 12 * t},
                           2 * t, 8 * t, 15 * t, 1.0f,
                           2 * t, 8 * t, 15 * t, 1.0f};
        result.boxes[2] = {{0.0f, 12 * t, 3 * t, 1.0f, 1.0f, 1.0f},
                           0.0f, 4 * t, 1.0f, 8 * t,
                           0.0f, 1 * t, 1.0f, 14 * t};
        // Both undersides are `#bottom`, which this model states as oak planks
        // - the plinth's whole sheet and the desk's top thirteen rows. The desk
        // also states three different wall rectangles: the lip a book leans
        // against is a different row of the sheet from its two flanks.
        result.boxes[0].floorOverride = kBoxFloorLecternPlinth;
        result.boxes[2].floorOverride = kBoxFloorLecternDesk;
        result.boxes[2].wallOverride = kBoxWallsLecternDesk;
        // `lectern.json` puts `"rotation": 180` on both `up` faces and on
        // neither `down`. Half a turn is its own inverse, so this is the one
        // rotation that does not first need our clockwise to be proved equal to
        // the reference's - which is why the model's `90`s on its side faces
        // are left to the finding that owns them rather than guessed at here.
        result.boxes[0].lidTurns = 2;
        result.boxes[2].lidTurns = 2;
        result.count = 3;
        return result;
    }
    if (isCocoa(id)) {
        result.boxes[0] = {cocoaBoxes(cocoaFacing(id), cocoaAge(id)).boxes[0],
                           8 * t, 4 * t, 16 * t, 13 * t,
                           8 * t, 4 * t, 16 * t, 13 * t};
        result.count = 1;
        return result;
    }
    // The reference's `template_anvil`, box for box: a 12-wide plinth, two
    // narrowing waists and a top that overhangs the lot. Only the top's lid is
    // painted from `anvil_top`; everything else is the body picture, which is
    // why the three plinths name their own lid layer.
    if (isAnvil(id)) {
        constexpr float body = kAnvilBodyLayer;
        result.boxes[0] = {{2 * t, 0.0f, 2 * t, 14 * t, 4 * t, 14 * t},
                           2 * t, 12 * t, 14 * t, 1.0f,
                           2 * t, 2 * t, 14 * t, 14 * t, body, body};
        result.boxes[1] = {{4 * t, 4 * t, 3 * t, 12 * t, 5 * t, 13 * t},
                           4 * t, 11 * t, 12 * t, 12 * t,
                           4 * t, 3 * t, 12 * t, 13 * t, body, body};
        result.boxes[2] = {{6 * t, 5 * t, 4 * t, 10 * t, 10 * t, 12 * t},
                           6 * t, 6 * t, 10 * t, 11 * t,
                           6 * t, 4 * t, 10 * t, 12 * t, body, body};
        result.boxes[3] = {{3 * t, 10 * t, 0.0f, 13 * t, 1.0f, 1.0f},
                           3 * t, 0.0f, 13 * t, 6 * t,
                           3 * t, 0.0f, 13 * t, 1.0f};
        // The overhang's underside is the body, and it is the one face here
        // that named no layer at all: blockTextureLayer(Anvil, Bottom) answers
        // anvil_top, so the striking surface showed from underneath.
        result.boxes[3].floorOverride = kBoxFloorAnvilTop;
        // `template_anvil.json` turns three of its lids half round - #0 on both
        // `up` and `down`, #1 on the only one it states, #3 on both. #2 is the
        // waist and states neither face, so it keeps its default. Because these
        // are all `180`, and half a turn is its own inverse, none of them
        // depends on our clockwise matching the reference's; the `90`s and
        // `270`s this same model puts on its *side* faces do, and are left to
        // the finding that owns them.
        result.boxes[0].lidTurns = 2;
        result.boxes[1].lidTurns = 2;
        result.boxes[3].lidTurns = 2;
        result.count = 4;
        return result;
    }
    // Three that are simply shorter than their cell. Their side art is painted
    // in the *lower* rows, so the rectangle starts where the block does - the
    // whole reason a full-height cube left a gap once the empty texels above
    // stopped drawing as black.
    if (id == BlockId::EndPortalFrame || id == BlockId::EnchantingTable ||
        id == BlockId::SculkSensor) {
        const float top = id == BlockId::EndPortalFrame  ? 13 * t
                          : id == BlockId::EnchantingTable ? 12 * t
                                                           : 8 * t;
        result.boxes[0] = {{0.0f, 0.0f, 0.0f, 1.0f, top, 1.0f},
                           0.0f, 1.0f - top, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f};
        result.count = 1;
        return result;
    }
    // A half-height base with a narrower drum standing on it.
    if (id == BlockId::SculkShrieker) {
        result.boxes[0] = {{0.0f, 0.0f, 0.0f, 1.0f, 8 * t, 1.0f},
                           0.0f, 8 * t, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f};
        result.boxes[1] = {{1 * t, 8 * t, 1 * t, 15 * t, 15 * t, 15 * t},
                           1 * t, 1 * t, 15 * t, 8 * t,
                           1 * t, 1 * t, 15 * t, 15 * t};
        result.count = 2;
        return result;
    }
    // The reference's `template_campfire`, **five solid pieces and not four**:
    // two logs on the floor along Z, two crossbars above them along X, and the
    // ember plank down the middle that had been left out. The crossbars only
    // cover z 1-5 and z 11-15, so without the plank there was a six-texel hole
    // straight through the middle of every campfire - and since the plank's lid
    // is the only face in the whole model painted from the ember picture, a
    // block that emits light 15 had no embers anywhere on it. The fire itself
    // is a pair of animated quads there and is still left out.
    if (id == BlockId::Campfire || id == BlockId::SoulCampfire) {
        constexpr float log = kCampfireLogLayer;
        const float lit = id == BlockId::Campfire ? kCampfireLitLogLayer
                                                  : kSoulCampfireLitLogLayer;
        // `[1,0,0]-[5,4,16]` and `[11,0,0]-[15,4,16]`, sides `[0,0,16,4]`, lid
        // the same strip **a quarter turn round** - `"rotation": 90` in the
        // model, because sixteen texels of picture have to run along the log's
        // sixteen-texel length rather than across its four-texel width. Which
        // way round the quarter turn goes is not observable in this artwork:
        // bark and embers are noise, not a direction. The reference's outward
        // faces are `#log` and its inward ones `#lit_log`, which one `sideLayer`
        // could not say until `wallOverride` existed.
        result.boxes[0] = {{1 * t, 0.0f, 0.0f, 5 * t, 4 * t, 1.0f},
                           0.0f, 0.0f, 1.0f, 4 * t,
                           0.0f, 0.0f, 1.0f, 4 * t, log, log, 1};
        result.boxes[1] = {{11 * t, 0.0f, 0.0f, 15 * t, 4 * t, 1.0f},
                           0.0f, 0.0f, 1.0f, 4 * t,
                           0.0f, 0.0f, 1.0f, 4 * t, log, log, 1};
        // `[0,3,1]-[16,7,5]` and `[0,3,11]-[16,7,15]`. **Their rectangle used
        // to be rows 4-8, which is three-quarters empty** - `campfire_log.png`
        // keeps a four-by-four end grain there and nothing else - so both upper
        // logs were very nearly transparent from the side and from above, which
        // is failure shape four with the sibling twist of shape one: the two
        // floor logs beside them had theirs read off the model correctly. The
        // model says rows 0-4 on both long faces, and names `#lit_log` for
        // both, these being the pieces the flame sits between. The lid is the
        // same strip turned half round, which needs no sign argument.
        result.boxes[2] = {{0.0f, 3 * t, 1 * t, 1.0f, 7 * t, 5 * t},
                           0.0f, 0.0f, 1.0f, 4 * t,
                           0.0f, 0.0f, 1.0f, 4 * t, lit, log, 2};
        result.boxes[3] = {{0.0f, 3 * t, 11 * t, 1.0f, 7 * t, 15 * t},
                           0.0f, 0.0f, 1.0f, 4 * t,
                           0.0f, 0.0f, 1.0f, 4 * t, lit, log, 2};
        // The ember plank, `[5,0,0]-[11,1,16]`: `north` is `[0,15,6,16]`, a
        // single row of bark for a piece one texel tall, and `up` is
        // `[0,8,16,14]` on `#lit_log` - the ember bed - a quarter turn round
        // for the same reason the floor logs are.
        result.boxes[4] = {{5 * t, 0.0f, 0.0f, 11 * t, 1 * t, 1.0f},
                           0.0f, 15 * t, 6 * t, 1.0f,
                           0.0f, 8 * t, 1.0f, 14 * t, log, lit, 1};
        // **Where the embers actually are.** The upper pair's `down` is
        // `#lit_log [0,4,16,8]` unturned - the lit face is *underneath* them,
        // over the fire - while ours took the lid's bark, turned half round.
        // The plank's `down` is bark where its lid is embers. And the ends of
        // all five are the four-texel cut grain, never the sixteen-texel strip
        // along their length; the two floor logs' inward faces are lit and
        // their outward ones are not, which is what one `sideLayer` for four
        // walls could not say.
        const bool soul = id == BlockId::SoulCampfire;
        result.boxes[0].wallOverride =
            soul ? kBoxWallsSoulCampfireWestLog : kBoxWallsCampfireWestLog;
        result.boxes[1].wallOverride =
            soul ? kBoxWallsSoulCampfireEastLog : kBoxWallsCampfireEastLog;
        result.boxes[2].floorOverride = soul ? kBoxFloorSoulCampfireBar : kBoxFloorCampfireBar;
        result.boxes[3].floorOverride = result.boxes[2].floorOverride;
        result.boxes[2].wallOverride = kBoxWallsCampfireBar;
        result.boxes[3].wallOverride = kBoxWallsCampfireBar;
        result.boxes[4].floorOverride = kBoxFloorCampfirePlank;
        result.boxes[4].wallOverride = kBoxWallsCampfirePlank;
        result.count = 5;
        return result;
    }
    // Four corner posts under a thin deck. The posts stop a hundredth short of
    // the deck rather than reaching it, because two boxes that share a plane
    // put two quads on it.
    if (id == BlockId::Scaffolding) {
        constexpr float side = kScaffoldSideLayer;
        constexpr float deck = 15.99f * t;
        result.boxes[0] = {{0.0f, deck, 0.0f, 1.0f, 1.0f, 1.0f},
                           0.0f, 0.0f, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f};
        for (int i = 0; i < 4; ++i) {
            const float x = (i & 1) != 0 ? 14 * t : 0.0f;
            const float z = (i & 2) != 0 ? 14 * t : 0.0f;
            result.boxes[1 + i] = {{x, 0.0f, z, x + 2 * t, deck, z + 2 * t},
                                   0.0f, 0.0f, 2 * t, 1.0f,
                                   x, z, x + 2 * t, z + 2 * t, side, side};
        }
        // **The four top rails, which had been left out entirely.**
        // `scaffolding_stable.json` has nine elements and we drew five: the
        // deck, the four posts, and then `[2,14,0]-[14,16,2]` with its three
        // rotations closing the gap between post and post at the top. Without
        // them a scaffold was four bare legs with two texels of daylight
        // between them at head height, which is the one part of it a player
        // climbing the tower is looking straight at.
        //
        // `check-models.ps1` cannot see this: the rails lie inside the union
        // the deck and posts already describe, and it compares union extents
        // only. That is the second of this pair - the flower pot's `uv` is the
        // other - and the reason both needed the JSON read rather than the
        // check trusted.
        //
        // They stop at `deck` for the same reason the posts do. Every outward
        // face is rows 0-2 of `scaffolding_side` across the rail's twelve
        // texels, which is the model's own auto-uv on all four of them; the
        // inward faces are the band below, and one `sideLayer` cannot carry
        // both, so the face a player sees wins.
        for (int i = 0; i < 4; ++i) {
            const bool alongX = i < 2;
            const float edge = (i & 1) != 0 ? 14 * t : 0.0f;
            const float x0 = alongX ? 2 * t : edge;
            const float z0 = alongX ? edge : 2 * t;
            const float x1 = alongX ? 14 * t : edge + 2 * t;
            const float z1 = alongX ? edge + 2 * t : 14 * t;
            result.boxes[5 + i] = {{x0, 14 * t, z0, x1, deck, z1},
                                   2 * t, 0.0f, 14 * t, 2 * t,
                                   x0, z0, x1, z1, side, side};
        }
        result.count = 9;
        return result;
    }
    // A rim, four walls round it, then the funnel and its spout. The reference
    // moves the spout to whichever side the hopper faces; ours keeps it under
    // the middle for all five, which is a named simplification.
    if (isHopper(id)) {
        constexpr float side = kHopperSideLayer;
        result.boxes[0] = {{0.0f, 10 * t, 0.0f, 1.0f, 11 * t, 1.0f},
                           0.0f, 5 * t, 1.0f, 6 * t,
                           0.0f, 0.0f, 1.0f, 1.0f, side, side};
        result.boxes[1] = {{0.0f, 11 * t, 0.0f, 2 * t, 1.0f, 1.0f},
                           0.0f, 0.0f, 2 * t, 5 * t,
                           0.0f, 0.0f, 2 * t, 1.0f};
        result.boxes[2] = {{14 * t, 11 * t, 0.0f, 1.0f, 1.0f, 1.0f},
                           14 * t, 0.0f, 1.0f, 5 * t,
                           14 * t, 0.0f, 1.0f, 1.0f};
        result.boxes[3] = {{2 * t, 11 * t, 0.0f, 14 * t, 1.0f, 2 * t},
                           2 * t, 0.0f, 14 * t, 5 * t,
                           2 * t, 0.0f, 14 * t, 2 * t};
        result.boxes[4] = {{2 * t, 11 * t, 14 * t, 14 * t, 1.0f, 1.0f},
                           2 * t, 0.0f, 14 * t, 5 * t,
                           2 * t, 14 * t, 14 * t, 1.0f};
        result.boxes[5] = {{4 * t, 4 * t, 4 * t, 12 * t, 10 * t, 12 * t},
                           4 * t, 6 * t, 12 * t, 12 * t,
                           4 * t, 4 * t, 12 * t, 12 * t, side, side};
        result.boxes[6] = {{6 * t, 0.0f, 6 * t, 10 * t, 4 * t, 10 * t},
                           6 * t, 12 * t, 10 * t, 1.0f,
                           6 * t, 6 * t, 10 * t, 10 * t, side, side};
        result.count = 7;
        return result;
    }
    // The reference's `template_torch`: one 2x10x2 stick, its sides sampling
    // the lower ten rows of the image and its lid the two rows the flame sits
    // on. Two crossed sheets is what a *plant* is; a torch is a stick.
    if (isTorchBlock(id)) {
        result.boxes[0] = {{7 * t, 0.0f, 7 * t, 9 * t, 10 * t, 9 * t},
                           7 * t, 6 * t, 9 * t, 16 * t,
                           7 * t, 6 * t, 9 * t, 8 * t};
        result.boxes[0].floorOverride = kBoxFloorTorch;
        result.count = 1;
        return result;
    }
    // A hollow tub: four walls and a floor. **Drawn hollow is the whole point**
    // - as a full cube its lid showed the rim texture's transparent middle with
    // nothing behind it, and being opaque it culled the faces of everything it
    // touched, so a small window opened through the world around it.
    //
    // The two share a branch and differ under the floor. `composter.json` has a
    // genuine solid bottom, `[0,0,0]-[16,2,16]`. **`cauldron.json` has no bottom
    // at all**: eight small elements at y 0-3 make four L-shaped feet with the
    // middle open, and the tub's floor is a separate plate at `[2,3,2]-[14,4,14]`
    // painted `#inside`. Ours drew a legless cauldron sitting flat on the
    // ground, which is the one thing about the block's outline you can see from
    // across a room.
    if (isCauldron(id) || isComposter(id)) {
        const bool tub = isCauldron(id);
        const float floorTop = tub ? 3 * t : 2 * t;
        const float wall = 2 * t;
        const float inner = tub ? kCauldronSideLayer : kComposterSideLayer;
        // The rim is always the rim. A composter's own top layer changes to the
        // bone-meal picture at level eight, and that one is painted only in its
        // middle twelve texels - so the four walls' top faces, which sample the
        // outer ring, drew nothing at all on a composter that was ready.
        const float rim = tub ? -1.0f : kComposterTopLayer;
        if (tub) {
            // **Each L is drawn as the four-by-four square it fits inside**, not
            // as its two elements. Eight feet plus four walls plus the floor
            // plate plus what is in it is thirteen boxes and `ModelBoxes` holds
            // ten; the square is one 2x2x3 nub per corner heavier than the
            // reference and opens the middle, which is the part that shows. Say
            // so here rather than let the next reader measure it and think it a
            // mistake.
            for (int i = 0; i < 4; ++i) {
                const float x = (i & 1) != 0 ? 12 * t : 0.0f;
                const float z = (i & 2) != 0 ? 12 * t : 0.0f;
                result.boxes[result.count++] = {{x, 0.0f, z, x + 4 * t, floorTop, z + 4 * t},
                                                0.0f, 1.0f - floorTop, 4 * t, 1.0f,
                                                x, z, x + 4 * t, z + 4 * t};
            }
            // `[2,3,2]-[14,4,14]`, `#inside` on both faces - the floor you see
            // when the cauldron is empty, and the ceiling you see through the
            // open feet from below.
            result.boxes[result.count++] = {{wall, floorTop, wall, 1.0f - wall, floorTop + t,
                                             1.0f - wall},
                                            wall, 1.0f - floorTop - t, 1.0f - wall, 1.0f - floorTop,
                                            wall, wall, 1.0f - wall, 1.0f - wall, inner, inner};
        } else {
            result.boxes[result.count++] = {{0.0f, 0.0f, 0.0f, 1.0f, floorTop, 1.0f},
                                            0.0f, 1.0f - floorTop, 1.0f, 1.0f,
                                            0.0f, 0.0f, 1.0f, 1.0f, -1.0f, inner};
        }
        result.boxes[result.count++] = {{0.0f, floorTop, 0.0f, 1.0f, 1.0f, wall},
                                        0.0f, 0.0f, 1.0f, 1.0f - floorTop,
                                        0.0f, 0.0f, 1.0f, wall, -1.0f, rim};
        result.boxes[result.count++] = {{0.0f, floorTop, 1.0f - wall, 1.0f, 1.0f, 1.0f},
                                        0.0f, 0.0f, 1.0f, 1.0f - floorTop,
                                        0.0f, 1.0f - wall, 1.0f, 1.0f, -1.0f, rim};
        result.boxes[result.count++] = {{0.0f, floorTop, wall, wall, 1.0f, 1.0f - wall},
                                        0.0f, 0.0f, 1.0f, 1.0f - floorTop,
                                        0.0f, wall, wall, 1.0f - wall, -1.0f, rim};
        result.boxes[result.count++] = {{1.0f - wall, floorTop, wall, 1.0f, 1.0f, 1.0f - wall},
                                        0.0f, 0.0f, 1.0f, 1.0f - floorTop,
                                        1.0f - wall, wall, 1.0f, 1.0f - wall, -1.0f, rim};
        // What is in it, which is the only reason nine composter levels and
        // seven cauldron levels are separate blocks at all.
        const int level = tub ? cauldronLevel(id) : composterLevel(id);
        if (level > 0) {
            const float fill = tub ? floorTop + static_cast<float>(level) * 2.0f * t
                                   : floorTop + static_cast<float>(level) * 1.5f * t;
            // A full cauldron is water; a ready composter is bone meal and
            // everything below it is compost, which has a texture of its own -
            // the tub's top is a rim with a transparent middle and drew nothing.
            const float surface = tub ? kWaterLayer
                                      : (level >= 8 ? kComposterReadyLayer : kComposterCompostLayer);
            result.boxes[result.count++] = {{wall, fill - 0.5f * t, wall, 1.0f - wall, fill,
                                             1.0f - wall},
                                            wall, 1.0f - fill, 1.0f - wall, 1.0f - fill + 0.5f * t,
                                            wall, wall, 1.0f - wall, 1.0f - wall,
                                            surface, surface};
        }
        return result;
    }
    // Two posts and a crossbar, which is the whole of the reference's
    // `bell_floor`: `block/stone` posts at x 0-2 and 14-16, and a
    // `block/dark_oak_planks` bar at y 13-15. **The bell itself is not in that
    // file** - the reference draws it as a block entity - so its geometry is
    // read off the art instead, and `bell_side.png` states it plainly: a
    // six-wide body seven texels tall painted at u 1-7, and an eight-wide lip
    // two texels tall at u 0-8 under it. A model box has to be able to name its
    // own layer for any of this; before it could, every post came out gold.
    if (id == BlockId::Bell) {
        constexpr float post = kStoneLayer;
        constexpr float bar = kPlanksLayer;
        result.boxes[0] = {{0.0f, 0.0f, 6 * t, 2 * t, 1.0f, 10 * t},
                           0.0f, 1 * t, 2 * t, 1.0f,
                           0.0f, 0.0f, 2 * t, 4 * t, post, post};
        result.boxes[1] = {{14 * t, 0.0f, 6 * t, 1.0f, 1.0f, 10 * t},
                           0.0f, 1 * t, 2 * t, 1.0f,
                           0.0f, 0.0f, 2 * t, 4 * t, post, post};
        result.boxes[2] = {{2 * t, 13 * t, 7 * t, 14 * t, 15 * t, 9 * t},
                           2 * t, 3 * t, 14 * t, 5 * t,
                           2 * t, 3 * t, 14 * t, 5 * t, bar, bar};
        // The body hangs from the underside of the bar, and reaches a hair into
        // it so the two never share a plane. Sampling from u 0 was drawing the
        // transparent column beside the artwork as a stripe down the bell.
        result.boxes[3] = {{5 * t, 6 * t, 5 * t, 11 * t, 13 * t + 0.004f, 11 * t},
                           1 * t, 0.0f, 7 * t, 7 * t,
                           1 * t, 1 * t, 7 * t, 7 * t, kBellSideLayer, kBellTopLayer};
        // The flared lip: eight across, two deep, sitting under the body.
        result.boxes[4] = {{4 * t, 4 * t, 4 * t, 12 * t, 6 * t, 12 * t},
                           0.0f, 7 * t, 8 * t, 9 * t,
                           0.0f, 0.0f, 8 * t, 8 * t, kBellSideLayer, kBellTopLayer};
        result.count = 5;
        return result;
    }
    // A wheel between two legs, which is what a grindstone is and what a full
    // cube could never be. The reference's own geometry: log legs at x 2-4 and
    // 12-14, stone pivots above them, and a wheel filling x 4-12 all the way to
    // the top. Every box here matches `grindstone.json` exactly.
    //
    // **Two of the pictures do not, and they are two different problems.** An
    // earlier version of this comment ran them together and said both needed
    // the same redesign; only one of them does.
    //
    // *The wheel* is the architectural one. `ModelBox` carries one `sideLayer`
    // for all four walls, and the reference gives the wheel different textures
    // on different walls: north and south are `#round` (`grindstone_round`, the
    // 8x12 disc we use) but east and west are `#side` (`grindstone_side`,
    // 12x12), so the two ends of the wheel wear the disc stretched sideways.
    // That wants **a per-face layer on a model box**, which is an owner-level
    // change to `ModelBox`, `ChunkMesher.cpp`, `HudPrimitives.cpp` and
    // `ItemEntity.cpp` together, not a number to correct here.
    //
    // *The pivots* are much smaller: they want no per-face anything, only
    // `grindstone_pivot.png`, which is simply not staged, so they borrow
    // `grindstone_side`. **What blocks it is where the layer would go, not the
    // texture.** The reference's file order would put it at 475, immediately
    // after `grindstone_round` - and 183 literals in this header sit at or
    // above 475 (113 `.layer`, 50 `.topLayer`, 11 `kTableLayerBase + N` and 9
    // named constants). Only the last 3 are pinned by an assert; the other 180
    // are checked by nothing but a reader's eyes, which is how several hundred
    // blocks would come to wear their neighbour's texture with a clean build.
    //
    // **Append it instead, and the cost collapses to three.** `kTableSprites`
    // is `maxTableLayer() + 1` - derived by walking the table - so a row given
    // the *next free* layer moves nothing inside the table and nothing derived
    // from it. Only the three absolute atlas literals move, by one each, and
    // every one of them fails to compile if it does not:
    //   `kStonecutterSawLayer`, `kComposterCompostLayer`, `kRedstoneSpritesFirst`.
    // The atlas is an unordered bag; nothing requires it to follow the
    // reference's file order, and `kComposterCompostLayer` above is this exact
    // precedent - a table texture that arrived late and was appended rather
    // than inserted. **Needs one row appended to the staging script and to
    // `Main.cpp`'s sprite list in the same change**, which is why it is not
    // done here: this header cannot land half of it safely.
    if (id == BlockId::Grindstone) {
        constexpr float wheel = kGrindstoneRoundLayer;
        constexpr float pivot = kGrindstoneRoundLayer - 1.0f; // grindstone_side
        constexpr float leg = kLogSideLayer;
        result.boxes[0] = {{2 * t, 0.0f, 6 * t, 4 * t, 7 * t, 10 * t},
                           2 * t, 9 * t, 4 * t, 16 * t,
                           2 * t, 6 * t, 4 * t, 10 * t, leg, leg};
        result.boxes[1] = {{12 * t, 0.0f, 6 * t, 14 * t, 7 * t, 10 * t},
                           12 * t, 9 * t, 14 * t, 16 * t,
                           12 * t, 6 * t, 14 * t, 10 * t, leg, leg};
        result.boxes[2] = {{2 * t, 7 * t, 5 * t, 4 * t, 13 * t, 11 * t},
                           0.0f, 0.0f, 2 * t, 6 * t,
                           0.0f, 0.0f, 2 * t, 6 * t, pivot, pivot};
        result.boxes[3] = {{12 * t, 7 * t, 5 * t, 14 * t, 13 * t, 11 * t},
                           0.0f, 0.0f, 2 * t, 6 * t,
                           0.0f, 0.0f, 2 * t, 6 * t, pivot, pivot};
        result.boxes[4] = {{4 * t, 4 * t, 2 * t, 12 * t, 1.0f, 14 * t},
                           0.0f, 0.0f, 8 * t, 12 * t,
                           0.0f, 0.0f, 8 * t, 12 * t, wheel, wheel};
        result.count = 5;
        return result;
    }
    // A bench with a saw standing out of it. The blade has its own texture -
    // borrowing the bench top drew a grey plate with the bench's slot on it.
    if (id == BlockId::Stonecutter) {
        constexpr float saw = kStonecutterSawLayer;
        result.boxes[0] = {{0.0f, 0.0f, 0.0f, 1.0f, 9 * t, 1.0f},
                           0.0f, 7 * t, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f};
        result.boxes[1] = {{1 * t, 9 * t, 7.5f * t, 15 * t, 1.0f, 8.5f * t},
                           1 * t, 9 * t, 15 * t, 1.0f,
                           1 * t, 9 * t, 15 * t, 10 * t, saw, saw};
        result.count = 2;
        return result;
    }
    // A rod standing in three separate base plates - `brewing_stand.json` and
    // all four of its elements. It was drawn as two crossed sheets, like a
    // flower, and then as one solid plate.
    //
    // **The three "blades" that used to sit at y 11-12 were invented**, measured
    // out of rows 2-4 of the picture because nothing else sampled them, while
    // the model that states that geometry sat in the reference folder unopened.
    // Rows 0-8 of `brewing_stand.png` are the *bottle arms*, and
    // `brewing_stand_bottle0..2.json` name them exactly: one flat quad each,
    // `[8,0,8]-[16,16,8]` turned 0, 120 and 240 degrees, drawn **only when that
    // slot holds a bottle**. They are full height, not one texel; they are
    // sheets, not boxes; and they are conditional, not permanent. Three wrong
    // answers from one guess, which is failure shape four exactly.
    //
    // They are left out rather than corrected, because we have no bottle-in-slot
    // state to switch them on with - the stand's contents live in the block
    // entity, not the id. Adding them would mean drawing all three always, which
    // is further from the reference than drawing none. The union is unchanged
    // either way: the rod already reaches y 14.
    if (id == BlockId::BrewingStand) {
        constexpr float base = kBrewingStandBaseLayer;
        constexpr float rod = kBrewingStandRodLayer;
        result.boxes[0] = {{7 * t, 0.0f, 7 * t, 9 * t, 14 * t, 9 * t},
                           7 * t, 2 * t, 9 * t, 16 * t,
                           7 * t, 7 * t, 9 * t, 9 * t, rod, rod};
        result.boxes[1] = {{9 * t, 0.0f, 5 * t, 15 * t, 2 * t, 11 * t},
                           9 * t, 14 * t, 15 * t, 16 * t,
                           9 * t, 5 * t, 15 * t, 11 * t, base, base};
        result.boxes[2] = {{1 * t, 0.0f, 1 * t, 7 * t, 2 * t, 7 * t},
                           1 * t, 14 * t, 7 * t, 16 * t,
                           1 * t, 1 * t, 7 * t, 7 * t, base, base};
        result.boxes[3] = {{1 * t, 0.0f, 9 * t, 7 * t, 2 * t, 15 * t},
                           1 * t, 14 * t, 7 * t, 16 * t,
                           1 * t, 9 * t, 7 * t, 15 * t, base, base};
        result.count = 4;
        return result;
    }
    if (id == BlockId::EndRod) {
        result.boxes[0] = {{6 * t, 0.0f, 6 * t, 10 * t, 1 * t, 10 * t},
                           2 * t, 6 * t, 6 * t, 7 * t,
                           2 * t, 2 * t, 6 * t, 6 * t};
        result.boxes[1] = {{7 * t, 1 * t, 7 * t, 9 * t, 16 * t, 9 * t},
                           0.0f, 0.0f, 2 * t, 15 * t,
                           2 * t, 0.0f, 4 * t, 2 * t};
        result.count = 2;
        return result;
    }
    result.boxes[0] = {{5 * t, 0.0f, 5 * t, 11 * t, 7 * t, 11 * t},
                       0.0f, 2 * t, 6 * t, 9 * t,
                       0.0f, 9 * t, 6 * t, 15 * t};
    result.boxes[1] = {{6 * t, 7 * t, 6 * t, 10 * t, 9 * t, 10 * t},
                       1 * t, 0.0f, 5 * t, 2 * t,
                       1 * t, 10 * t, 5 * t, 14 * t};
    result.boxes[2] = {{6.5f * t, 9 * t, 7.5f * t, 9.5f * t, 11 * t, 8.5f * t},
                       11 * t, 1 * t, 14 * t, 3 * t,
                       11 * t, 1 * t, 14 * t, 2 * t};
    // A hair shorter than the one it crosses, because two quads that share a
    // top plane put two coincident faces on it and flicker where they meet.
    result.boxes[3] = {{7.5f * t, 9 * t + 0.004f, 6.5f * t, 8.5f * t, 11 * t - 0.004f, 9.5f * t},
                       11 * t, 10 * t, 14 * t, 12 * t,
                       11 * t, 10 * t, 14 * t, 11 * t};
    result.count = 4;
    return result;
}

/// How many boxes a `ModelBoxes` can hold, read off the array rather than
/// written down beside it. Every bound below is this, so widening the array is
/// one edit and the checks follow it.
constexpr int kModelBoxCapacity = static_cast<int>(sizeof(ModelBoxes{}.boxes) / sizeof(ModelBox));

/// Whether every model in `[lo, hi)` fits in `bound` boxes.
///
/// **`count` and `boxes` are fields of the same struct**, so a `postModel`
/// return whose count runs past the array is malformed whoever asked for it -
/// the mesher, the slot picture, the dropped item or the thrown one. That is
/// why this sweeps every id rather than only the ones that take the model path
/// today: the next caller is the one that finds the hole.
///
/// **Parameterised on the bound so the checker can be shown failing.** A guard
/// that hard-codes the right number agrees with whatever the data does; this
/// one is handed a candidate and refuses the wrong one two lines below. Eleven
/// asserts in this file once compared one side of a derivation against itself
/// and passed while pointing at the wrong texture.
constexpr bool everyModelFitsIn(int bound, int lo, int hi) {
    for (int i = lo; i < hi; ++i) {
        const int count = postModel(static_cast<BlockId>(i)).count;
        if (count < 0 || count > bound) {
            return false;
        }
    }
    return true;
}

/// **The sweep runs in eighths because a `constexpr` evaluation has a step
/// budget** and `postModel` is a nine-hundred-line switch returning 684 bytes.
/// The eighths are derived from the id count, so adding ids widens them rather
/// than leaving a tail unswept - and the last one is clamped to the count so no
/// id is read twice and none is missed.
constexpr int kModelSweepStride = static_cast<int>(kBlockIdCount + 7) / 8;
static_assert(kModelSweepStride * 8 >= static_cast<int>(kBlockIdCount),
              "eight strides have to cover every id, or the sweep below has a blind tail");
static_assert(everyModelFitsIn(kModelBoxCapacity, 0 * kModelSweepStride, 1 * kModelSweepStride), "");
static_assert(everyModelFitsIn(kModelBoxCapacity, 1 * kModelSweepStride, 2 * kModelSweepStride), "");
static_assert(everyModelFitsIn(kModelBoxCapacity, 2 * kModelSweepStride, 3 * kModelSweepStride), "");
static_assert(everyModelFitsIn(kModelBoxCapacity, 3 * kModelSweepStride, 4 * kModelSweepStride), "");
static_assert(everyModelFitsIn(kModelBoxCapacity, 4 * kModelSweepStride, 5 * kModelSweepStride), "");
static_assert(everyModelFitsIn(kModelBoxCapacity, 5 * kModelSweepStride, 6 * kModelSweepStride), "");
static_assert(everyModelFitsIn(kModelBoxCapacity, 6 * kModelSweepStride, 7 * kModelSweepStride), "");
static_assert(everyModelFitsIn(kModelBoxCapacity, 7 * kModelSweepStride, static_cast<int>(kBlockIdCount)),
              "some block's model has more boxes than the array it is copied into");

/// **The margin is exactly zero, and that is the reason the sweep above is
/// worth its compile time.** A filled cauldron is the fattest model in the file
/// at ten boxes against a capacity of ten, so the next box added to any model -
/// not just to a cauldron - is an overflow rather than a squeeze. Measured: six
/// ids at capacity, all of them cauldron fill levels, worst count 10 over 499
/// ids that take the model path.
static_assert(postModel(BlockId::CauldronExtraFirst).count == kModelBoxCapacity,
              "if this stops being tight the negative control below stops proving anything, and "
              "the comment above is then wrong about the headroom");

/// **The control, and it differs from its positive twin in one number.** Same
/// function, same single id, one less capacity - so a pass here would mean the
/// sweep cannot fail at all rather than that the data is good.
static_assert(everyModelFitsIn(kModelBoxCapacity, static_cast<int>(BlockId::CauldronExtraFirst),
                               static_cast<int>(BlockId::CauldronExtraFirst) + 1),
              "");
static_assert(!everyModelFitsIn(kModelBoxCapacity - 1, static_cast<int>(BlockId::CauldronExtraFirst),
                                static_cast<int>(BlockId::CauldronExtraFirst) + 1),
              "the capacity check has to be able to say no, or the eight asserts above are decoration");

/// Whether a block's lid turn agrees with its own geometry, checked by turning
/// one and comparing it against the other.
///
/// **This is deliberately not an assert about `lidTurnsFacing`.** Restating a
/// `?:` chain proves nothing - eleven asserts in this file once compared one
/// side of a derivation against itself and passed while pointing at the wrong
/// texture. So instead: take the block a quarter turn round, turn its front
/// torch's box back by the number of quarter turns the *lid* claims, and
/// require the unturned block's torch. Geometry and lid then have to move
/// together or the build stops.
///
/// A y:90 rotation carries `(x, z)` to `(1 - z, x)`, so undoing one carries
/// `(x, z)` back to `(z, 1 - x)`.
constexpr BlockBox unturnBox(BlockBox box, int turns) {
    for (int i = 0; i < turns; ++i) {
        const BlockBox in = box;
        box.minX = in.minZ;
        box.maxX = in.maxZ;
        box.minZ = 1.0f - in.maxX;
        box.maxZ = 1.0f - in.minX;
    }
    return box;
}

constexpr bool lidTurnMatchesGeometry(BlockId flat, BlockId turned) {
    const ModelBoxes a = postModel(flat);
    const ModelBoxes b = postModel(turned);
    if (a.count != b.count || a.count < 2 || a.boxes[0].lidTurns != 0) {
        return false;
    }
    const BlockBox back = unturnBox(b.boxes[1].box, b.boxes[0].lidTurns);
    return back.minX == a.boxes[1].box.minX && back.maxX == a.boxes[1].box.maxX &&
           back.minY == a.boxes[1].box.minY && back.maxY == a.boxes[1].box.maxY &&
           back.minZ == a.boxes[1].box.minZ && back.maxZ == a.boxes[1].box.maxZ;
}

// **Delete `lidTurnsFacing(facing)` from the repeater/comparator bench - which
// is precisely the state this file shipped in - and all five of these fail.**
// So does turning the chain the wrong way round, or renumbering `FaceDirection`
// without revisiting it.
static_assert(lidTurnMatchesGeometry(repeaterAt(FaceDirection::NegZ, 1, false, false),
                                     repeaterAt(FaceDirection::PosX, 1, false, false)));
static_assert(lidTurnMatchesGeometry(repeaterAt(FaceDirection::NegZ, 3, true, false),
                                     repeaterAt(FaceDirection::PosZ, 3, true, false)));
static_assert(lidTurnMatchesGeometry(repeaterAt(FaceDirection::NegZ, 1, false, false),
                                     repeaterAt(FaceDirection::NegX, 1, false, false)));
static_assert(lidTurnMatchesGeometry(comparatorAt(FaceDirection::NegZ, false, false),
                                     comparatorAt(FaceDirection::PosX, false, false)));
static_assert(lidTurnMatchesGeometry(comparatorAt(FaceDirection::NegZ, true, true),
                                     comparatorAt(FaceDirection::NegX, true, true)));

/// The one box a model-shaped block is bumped into and aimed at: **the union of
/// everything it draws**, worked out from `postModel` rather than written down
/// again beside it.
///
/// The older models here each carry a hand-written silhouette in `postBoxes`
/// that is deliberately simpler than their geometry - one box for an anvil's
/// four, one for a campfire's logs. The redstone family has no such shorthand
/// to write, and a second copy of a lever's or a lightning rod's extents is
/// exactly the shape of mistake this project keeps paying for.
constexpr BlockBoxes modelSilhouette(BlockId id) {
    const ModelBoxes model = postModel(id);
    BlockBoxes result;
    if (model.count == 0) {
        return result;
    }
    BlockBox box = model.boxes[0].box;
    for (int i = 1; i < model.count; ++i) {
        const BlockBox& next = model.boxes[i].box;
        box.minX = next.minX < box.minX ? next.minX : box.minX;
        box.minY = next.minY < box.minY ? next.minY : box.minY;
        box.minZ = next.minZ < box.minZ ? next.minZ : box.minZ;
        box.maxX = next.maxX > box.maxX ? next.maxX : box.maxX;
        box.maxY = next.maxY > box.maxY ? next.maxY : box.maxY;
        box.maxZ = next.maxZ > box.maxZ ? next.maxZ : box.maxZ;
    }
    result.boxes[0] = box;
    result.count = 1;
    return result;
}

/// A cactus is one texel narrower than its cell on both horizontal axes and one
/// short of full height. **Read off `cactus.json`, not guessed**: that model
/// draws the top and bottom from the full cube and then insets the sides to
/// `[0,0,1]-[16,16,15]` and `[1,0,0]-[15,16,16]`, and the reference's collision
/// box follows the same inset. It is what lets you stand on the corner of a
/// cactus without being pricked.
constexpr float kCactusInset = 1.0f / 16.0f;
constexpr float kCactusHeight = 15.0f / 16.0f;

/// How far you sink into soul sand. `RESEARCH.md`: "**full-block support
/// shape**, but a **7/8 collision shape** - everything can be placed on it, but
/// you sink in". Only the collision half is here; `isSolid` and `isOpaque` stay
/// full, which is what keeps it placeable and keeps the mesher culling against
/// it.
constexpr float kSoulSandHeight = 7.0f / 8.0f;

/// The `Model` blocks the reference walks straight through.
///
/// **One owner for a question three functions ask.** `collisionBoxes` must give
/// them nothing, `selectionBoxes` must still give them something to aim at, and
/// `isSolid` must say no - and those three living apart is exactly how this
/// broke. `BlockShape` is the single owner of *how a block is drawn*, and it is
/// read for two further questions it was never asked: what you collide with,
/// and whether a cell is ground. So moving candles, the sea pickle and bamboo
/// from `Cross` to `Model` - a change made purely so they would be *drawn* from
/// their model - silently handed all three a collision box and made them
/// standable. A lit candle became a step you climbed and a sea pickle became
/// sea floor a creature could spawn on.
///
/// **Bamboo is deliberately not here.** The reference does give bamboo a thin
/// collision box, so the box it gained is correct and only its `isSolid` answer
/// is new - and a bamboo stalk you can stand on is the reference's behaviour
/// too. It is the one of the three the reshape got right.
constexpr bool isWalkedThroughModel(BlockId id) {
    return isTorchBlock(id) || isCandle(id) || id == BlockId::SeaPickle;
}

/// The full cubes the reference draws whole and collides with not at all.
///
/// **The `Full`-shaped twin of `isWalkedThroughModel`, and it has to be a second
/// predicate rather than a widening of that one.** That one is read inside
/// `collisionBoxes`' `case BlockShape::Model`, so a `Full` block added to it
/// keeps its collision box - the single thing the change exists to remove - and
/// is then handed to `modelSilhouette`, which powder snow has no model to answer
/// from. Both halves fail quietly rather than loudly: the block still blocks,
/// and its outline comes back empty. So the two predicates sit side by side and
/// each is read where its own shape is decided.
///
/// **Powder snow is drawn as an ordinary cube and stops nothing.** You fall into
/// the cell, and that is what makes the freezing clock reachable at all - the
/// clock and the sink-and-climb overlay in the mover are dead code for as long
/// as this answers as a solid cube, because the player stands on the lid and is
/// never inside it. It keeps a selection box for the reason a torch does: a
/// block you cannot aim at is a block you cannot break.
///
/// **The leather-boot exception is deliberately not here.** The reference works
/// powder snow's collision out per entity per tick, which no block table can
/// express and which only this one block wants; that rule belongs to the mover
/// and is written there. This names the base shape the mover starts from.
constexpr bool isWalkedThroughFullCube(BlockId id) {
    return id == BlockId::PowderSnow;
}

/// The single source of truth for a block's extent. Meshing, collision and the
/// targeting outline all read this, so none of them can disagree about where a
/// block actually is.
///
/// **`BlockShape::Cross` falls through to no box at all, which is right for a
/// flower and wrong for five ids.** The reference counts an amethyst cluster,
/// its three buds and pointed dripstone as solid, and all five are drawn as
/// crosses here, so a stalagmite is currently walked through. The fix is a box
/// for those five rather than a `Cross` case - but the number is not in the
/// reference's own model, which is `block/cross` and names no box, and the two
/// candidate sources disagree, so it has to be measured before it is written.
/// Guessing a rectangle instead of reading the model that names it is this
/// project's twelfth recorded failure shape.
constexpr BlockBoxes collisionBoxes(BlockId id) {
    BlockBoxes result;
    // **Drawn as a cube and collided with as nothing at all** - the limit of the
    // pair below rather than a member of it, so it is answered first and on its
    // own. `BlockBoxes` default-initialises `count` to zero, so the empty result
    // is the whole answer.
    if (isWalkedThroughFullCube(id)) {
        return result;
    }
    // **The two the reference draws as a cube and collides with as something
    // smaller.** Neither is expressible as a `BlockShape`, because the shape is
    // what the mesher reads and both of these really are full cubes to it - so
    // they are named here, ahead of the switch, rather than given a shape that
    // would change how they are drawn.
    if (id == BlockId::Cactus) {
        result.boxes[0] = {kCactusInset,        0.0f,          kCactusInset,
                           1.0f - kCactusInset, kCactusHeight, 1.0f - kCactusInset};
        result.count = 1;
        return result;
    }
    if (id == BlockId::SoulSand) {
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, kSoulSandHeight, 1.0f};
        result.count = 1;
        return result;
    }
    switch (blockShape(id)) {
    case BlockShape::Full:
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
        result.count = 1;
        break;
    case BlockShape::Slab:
        result.boxes[0] = isUpperHalf(id) ? BlockBox{0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f}
                                          : BlockBox{0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f};
        result.count = 1;
        break;
    case BlockShape::Tilled:
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, kTilledHeight, 1.0f};
        result.count = 1;
        break;
    case BlockShape::Door:
        return doorLeafBoxes(doorFacing(id), doorHingeRight(id), doorOpen(id));
    case BlockShape::Trapdoor:
        return trapdoorLeafBoxes(trapdoorFacing(id), trapdoorOpen(id), trapdoorIsTop(id));
    case BlockShape::Bed:
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, kBedHeight, 1.0f};
        result.count = 1;
        break;
    case BlockShape::Stairs: {
        const bool top = stairIsTop(id);
        // The solid half, then the quarter step sitting on it.
        result.boxes[0] = top ? BlockBox{0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f}
                              : BlockBox{0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f};

        const float stepLow = top ? 0.0f : 0.5f;
        const float stepHigh = top ? 0.5f : 1.0f;
        switch (stairFacing(id)) {
        case Facing::North:
            result.boxes[1] = {0.0f, stepLow, 0.5f, 1.0f, stepHigh, 1.0f};
            break;
        case Facing::South:
            result.boxes[1] = {0.0f, stepLow, 0.0f, 1.0f, stepHigh, 0.5f};
            break;
        case Facing::East:
            result.boxes[1] = {0.0f, stepLow, 0.0f, 0.5f, stepHigh, 1.0f};
            break;
        case Facing::West:
            result.boxes[1] = {0.5f, stepLow, 0.0f, 1.0f, stepHigh, 1.0f};
            break;
        }
        result.count = 2;
        break;
    }
    case BlockShape::Fence:
        // Every arm, regardless of neighbours: what is drawn follows the
        // neighbours, but collision cannot see them from an id alone, and a
        // post on its own would let you squeeze past.
        return fenceBoxes(ConnectAll);
    case BlockShape::Wall:
        return wallBoxes(ConnectAll);
    case BlockShape::Pane:
        return paneBoxes(ConnectAll);
    case BlockShape::Model:
        // **A lever and a tripwire hook are walked straight through.** The
        // reference gives neither a collision box; a floor lever was a ledge
        // you tripped on and a wall lever narrowed a corridor. They still have
        // a selection box - see `selectionBoxes`, which would otherwise let you
        // aim straight past a lever at the wall behind it, exactly as it does
        // for a ladder. Every other component in the branch below - repeater,
        // comparator, daylight detector, lightning rod, piston head - does have
        // one in the reference, so the branch is right for those.
        if (isLever(id) || isTripwireHook(id) || isWalkedThroughModel(id)) {
            break;
        }
        // A redstone model is bumped into and aimed at as **the union of what
        // it draws**, worked out from `postModel` rather than written down a
        // second time. The older models above keep their hand-written
        // silhouettes, which are deliberately simpler than their geometry - one
        // box for an anvil's four, one for a campfire's logs.
        //
        // The five that stopped being crosses and cubes are here for the same
        // reason: none has a shorthand worth writing, and the union is the
        // right answer for each. A dragon egg's comes out as the reference's
        // own `1,0,1` to `15,16,15`, and a lectern's is the whole cell, which
        // is what it collided as while it was a cube.
        //
        // **The lectern has now been filed twice as a bug and is not one**, so
        // the numbers are here rather than left to be rediscovered a third
        // time: `lectern.json` element 0 is the plinth `[0,0,0]-[16,2,16]`, the
        // full footprint, and element 2 is the desk
        // `[0.0125,12,3]-[15.9875,16,16]`, which reaches the ceiling and both X
        // walls. The union of those *is* the cell. A cage-versus-drawing sweep
        // over every id agrees - the lectern draws nothing outside what you can
        // aim at. Tightening it means expressing the post's waist as a second
        // box, which is the general one-box-per-model divergence and not
        // anything specific to a lectern.
        if (isRedstoneComponent(id) || isWallTorch(id) || isCandle(id) ||
            id == BlockId::Bamboo || id == BlockId::SeaPickle || id == BlockId::DragonEgg ||
            id == BlockId::Lectern) {
            return modelSilhouette(id);
        }
        return postBoxes(id);
    case BlockShape::Ladder:
        // **No collision at all.** A ladder you bump into is a ladder you
        // cannot get onto, and climbing is what it is for - the reference gives
        // it none either. Same for a vine.
        break;
    case BlockShape::Vine:
        break;
    case BlockShape::Cocoa:
        return cocoaBoxes(cocoaFacing(id), cocoaAge(id));
    case BlockShape::Gate:
        return gateBoxes(gateFacing(id), gateIsOpen(id));
    case BlockShape::Hovering:
        // A texel of daylight underneath and half a texel in from each side.
        // The top stays flush so a charge under a ceiling still culls that face.
        result.boxes[0] = {kHoverInset,        kHoverLift, kHoverInset,
                           1.0f - kHoverInset, 1.0f,       1.0f - kHoverInset};
        result.count = 1;
        break;
    case BlockShape::Flat:
        // Wire, a rail and a tripwire lie on the floor and are walked straight
        // over - they are the flat shapes with no collision at all, where a
        // carpet, a snow layer and a lily pad are all stood on.
        if (isRedstoneWire(id) || isRail(id) || isTripwire(id)) {
            break;
        }
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, flatHeight(id), 1.0f};
        result.count = 1;
        break;
    case BlockShape::Button:
    case BlockShape::Plate:
    case BlockShape::Sign:
        // None of these is bumped into. A button you could walk into would stop
        // you reaching a door, a plate has to be **stood on** so the floor under
        // it is what holds you up, and the reference lets you walk through a
        // sign and a banner alike.
        break;
    default:
        break;
    }
    return result;
}

/// Height a shape reaches, as a fraction of the block. Stairs reach the top of
/// their step, which is what the targeting outline needs to enclose.
constexpr float shapeHeight(BlockShape shape) {
    if (shape == BlockShape::Slab) {
        return 0.5f;
    }
    return shape == BlockShape::Tilled ? kTilledHeight : 1.0f;
}

/// The boxes a block that collides with **nothing** is nonetheless drawn as.
///
/// A button, a pressure plate, redstone wire, a rail and a tripwire are all
/// walked straight through, so `collisionBoxes` answers empty for every one of
/// them - and the mesher starts from that. Without one owner for this, the
/// mesher and the targeting outline each grow their own copy, and the two
/// disagree the first time a number changes.
constexpr BlockBoxes uncollidableDrawnBoxes(BlockId id) {
    BlockBoxes result;
    if (isButton(id)) {
        return buttonBoxes(buttonMount(id), buttonPressed(id));
    }
    if (isPressurePlate(id)) {
        return plateBoxes(pressurePlateSignal(id) > 0);
    }
    if (isSignLike(id)) {
        return signBoxes(signKind(id), signFacing(id), signOnWall(id));
    }
    result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, flatHeight(id), 1.0f};
    result.count = 1;
    return result;
}

/// Whether `uncollidableDrawnBoxes` is the right question for this block.
constexpr bool drawsWithoutColliding(BlockId id) {
    return isButton(id) || isPressurePlate(id) || isRedstoneWire(id) || isRail(id) ||
           isTripwire(id) || isSignLike(id);
}

/// What the crosshair can pick out.
///
/// Usually the collision shape, but not always: a plant is walked straight
/// through and still has to be breakable, so it collides with nothing and
/// selects as a slim column.
constexpr BlockBoxes selectionBoxes(BlockId id) {
    if (blockShape(id) == BlockShape::Cross) {
        BlockBoxes result;
        // Slimmer and shorter than the blades themselves, matching the
        // reference's 2-14 by 0-13 hitbox: aiming at a plant should not mean
        // aiming at the whole cell it stands in.
        result.boxes[0] = {0.125f, 0.0f, 0.125f, 0.875f, 0.8125f, 0.875f};
        result.count = 1;
        return result;
    }
    // A ladder collides with nothing, so falling through to the collision boxes
    // would leave it unbreakable - you would aim straight past it at the wall.
    if (blockShape(id) == BlockShape::Ladder) {
        return ladderBoxes(ladderFacing(id));
    }
    if (blockShape(id) == BlockShape::Vine) {
        // No roof sheet: an outline has no neighbours to ask, and a vine you
        // can see is one you can aim at through its sides.
        return vineBoxes(vineSides(id) == 0 ? ConnectAll : vineSides(id), false);
    }
    // The five that collide with nothing and would otherwise be unbreakable:
    // you would aim straight through them at whatever they are stuck to.
    if (drawsWithoutColliding(id)) {
        return uncollidableDrawnBoxes(id);
    }
    // The same reason as the ladder above, and the same reason the two of them
    // were given no collision: a lever you cannot aim at is a lever you cannot
    // flip or break. A torch, a candle and a sea pickle are here for the
    // identical reason - take their collision away without this line and they
    // become unbreakable scenery.
    if (isLever(id) || isTripwireHook(id) || isWalkedThroughModel(id)) {
        return modelSilhouette(id);
    }
    // Taking powder snow's collision away would otherwise take its outline with
    // it, because this line falls through to `collisionBoxes` - the very trap
    // the ladder and the lever above are written to avoid. It is aimed at as
    // the whole cube it is drawn as.
    if (isWalkedThroughFullCube(id)) {
        BlockBoxes result;
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
        result.count = 1;
        return result;
    }
    return collisionBoxes(id);
}

// **Remove either half of the lever change and one of these fails.** They are
// written as the pair the ladder already proved: nothing to bump into, and
// still something to aim at.
static_assert(collisionBoxes(leverAt(LeverFloorX, false)).count == 0 &&
                  selectionBoxes(leverAt(LeverFloorX, false)).count > 0,
              "a lever is walked through and still aimed at");
static_assert(collisionBoxes(BlockId::TripwireHookRunFirst).count == 0 &&
                  selectionBoxes(BlockId::TripwireHookRunFirst).count > 0,
              "and so is a tripwire hook");
static_assert(collisionBoxes(BlockId::RepeaterRunFirst).count > 0,
              "while a repeater, which the reference does give a box, still has one");

/// Blocks movement. Water does not - you sink into it, and neither does a plant
/// you walk straight through.
///
/// **It is not the same question as "has a collision box", and `Flat` is where
/// the two part company.** A rail, a redstone line, tripwire string, a button, a
/// pressure plate and a sign are all `drawsWithoutColliding` - `collisionBoxes`
/// hands back nothing for them - and every one of them still answers yes here.
///
/// That is deliberate, because none of the fourteen callers is asking about
/// collision. They ask *is there something there*: is the cell under these feet
/// ground, may a creature spawn on it, does water pool on it, is a bed's support
/// present, is the block at eye level suffocating. Answering "no" for a rail
/// would open a hole under every minecart track that a spawn check, a fluid and
/// a fire could all fall through, for no gain - the physics never reads this,
/// it reads `collisionBoxes`, which is already right.
///
/// **So the two are allowed to disagree, and the assert below records that they
/// do** rather than leaving the next reader to discover it as a bug.
///
/// **Powder snow is the one block here that is knowingly wrong, and it cannot
/// be fixed from a per-id predicate.** It answers yes to this, to `isOpaque`
/// and to one full `[0..1]` collision box - byte for byte a stone - where the
/// reference makes it conditionally solid: entities fall in and sink, except
/// rabbit, fox, endermite and silverfish, which walk on it
/// (`minecraft:can_stand_on_powder_snow`), and except anything wearing leather
/// boots. Mojang's `mojang-blocks.json` gives `minecraft:powder_snow` **no
/// state properties at all**, so unlike the beehive's `honey_level` there is no
/// field to widen and no second id to add - the condition is entity-side, which
/// makes this `Player.cpp` and the creature code rather than a line here. The
/// naive edit, dropping it to zero collision boxes, is *also* wrong: the player
/// would fall straight through instead of sinking, and the carpet-on-top rule
/// that stops entities falling through would have nothing to hold.
constexpr bool isSolid(BlockId id) {
    // A torch, a candle and a sea pickle are drawn as boxes and walked straight
    // through, which is the one place a `Model` is not solid. Named through
    // `isWalkedThroughModel` rather than given a shape of their own, because
    // everything else about them - the box, their own texture rectangle, not
    // occluding a neighbour - is exactly what a model already is, and because
    // `collisionBoxes` and `selectionBoxes` have to answer from the same list.
    if (isWalkedThroughModel(id)) {
        return false;
    }
    // And powder snow, for the same three-answers-at-once reason but coming
    // from the other shape: a `Full` cube that is not ground. Nothing may rest
    // on it and no creature may spawn on a lid the player falls through.
    if (isWalkedThroughFullCube(id)) {
        return false;
    }
    const BlockShape shape = blockShape(id);
    return shape == BlockShape::Full || shape == BlockShape::Slab || shape == BlockShape::Stairs ||
           shape == BlockShape::Fence || shape == BlockShape::Wall || shape == BlockShape::Gate ||
           shape == BlockShape::Pane || shape == BlockShape::Model ||
           shape == BlockShape::Hovering || shape == BlockShape::Flat ||
           shape == BlockShape::Tilled || shape == BlockShape::Door ||
           shape == BlockShape::Trapdoor || shape == BlockShape::Bed;
}

// A struck charge is off the floor but still something you bump into and still
// something you can stand on, and only the lit one moves.
static_assert(blockShape(BlockId::TntPrimed) == BlockShape::Hovering);
static_assert(blockShape(BlockId::Tnt) == BlockShape::Full);
static_assert(isSolid(BlockId::TntPrimed) && isSolid(BlockId::Tnt));
static_assert(collisionBoxes(BlockId::TntPrimed).count == 1 &&
              collisionBoxes(BlockId::TntPrimed).boxes[0].minY > 0.0f &&
              collisionBoxes(BlockId::TntPrimed).boxes[0].maxY == 1.0f);
static_assert(collisionBoxes(BlockId::Tnt).boxes[0].minY == 0.0f);

// **The three walked-through models, written as the trio every one of them has
// to satisfy at once.** Nothing to bump into, something to aim at, and not
// ground - the last is the half that has no assert of its own anywhere else,
// and it is the half that let a creature spawn on a sea pickle. Drop any single
// clause of `isWalkedThroughModel` and one of these fails.
static_assert(collisionBoxes(BlockId::Torch).count == 0 &&
                  selectionBoxes(BlockId::Torch).count > 0 && !isSolid(BlockId::Torch),
              "a torch is walked through, aimed at, and is not ground");
static_assert(collisionBoxes(BlockId::Candle).count == 0 &&
                  selectionBoxes(BlockId::Candle).count > 0 && !isSolid(BlockId::Candle),
              "and so is a candle");
static_assert(collisionBoxes(BlockId::SeaPickle).count == 0 &&
                  selectionBoxes(BlockId::SeaPickle).count > 0 && !isSolid(BlockId::SeaPickle),
              "and so is a sea pickle");
// **Powder snow, the same trio arriving from the other shape.** Nothing to bump
// into, something to aim at, and not ground - and it is the third clause the
// freezing clock waits on, because a solid lid means the player stands above
// the cell and is never inside it.
//
// **The assert below is what stops the box coming back, and both consequences of
// its coming back live outside this file and are SILENT.** Reported by the
// suffocation and freezing owner on 2026-08-19, and consistent with what I
// checked on the freezing side: a box here puts the player on top of the cell
// rather than inside it, so the freeze clock - which waits on the body occupying
// a cell holding this block - becomes unreachable. And a body that does end up
// inside a cell that now has a box is a body inside a solid block, which hands
// the question to the suffocation rule and makes that rule the only thing
// between the player and a fast death. **A green build reports neither**, which
// is why this is written down rather than left to the assert alone. Falsified by
// the freeze clock ceasing to test cell occupancy.
static_assert(collisionBoxes(BlockId::PowderSnow).count == 0 &&
                  selectionBoxes(BlockId::PowderSnow).count > 0 &&
                  !isSolid(BlockId::PowderSnow),
              "powder snow is fallen into, aimed at, and is not ground");
// The control, and it differs from the line above in exactly one thing: stone is
// a `Full` cube that `isWalkedThroughFullCube` does not name. It keeps both
// halves, so a predicate widened to answer true for every full cube - or one
// wired into only two of the three readers - cannot satisfy both asserts at once.
static_assert(collisionBoxes(BlockId::Stone).count > 0 && isSolid(BlockId::Stone),
              "a full cube outside isWalkedThroughFullCube keeps its box and its footing");
// The absolute anchor, without which all three above would still pass with
// `isWalkedThroughModel` widened to every `Model` block: bamboo is the one of
// the three reshaped ids the reference *does* give a box and *does* let you
// stand on, and a lightning rod is an ordinary component that keeps both.
static_assert(blockShape(BlockId::Bamboo) == BlockShape::Model &&
                  collisionBoxes(BlockId::Bamboo).count > 0 && isSolid(BlockId::Bamboo),
              "bamboo keeps the box and the footing the reference gives it");
static_assert(collisionBoxes(BlockId::LightningRodRunFirst).count > 0 &&
                  isSolid(BlockId::LightningRodRunFirst),
              "and an ordinary Model component is untouched");

// **Written against the full cube each of them still is to the mesher.** Both
// sit ahead of the shape switch, so the pair that proves it has to name the
// shape as well as the box: give either a shape of its own and the first half
// fails, delete either early return and the second does. Stone is the anchor,
// because a rule that only ever compares a block against itself proves nothing.
static_assert(blockShape(BlockId::Cactus) == BlockShape::Full &&
                  collisionBoxes(BlockId::Cactus).boxes[0].maxY == kCactusHeight &&
                  collisionBoxes(BlockId::Cactus).boxes[0].minX == kCactusInset,
              "a cactus is drawn as a cube and collided with one texel slimmer and shorter");
static_assert(blockShape(BlockId::SoulSand) == BlockShape::Full &&
                  collisionBoxes(BlockId::SoulSand).boxes[0].maxY == kSoulSandHeight &&
                  collisionBoxes(BlockId::Stone).boxes[0].maxY == 1.0f,
              "you sink into soul sand, and the stone beside it is still full height");

// **The one place `isSolid` and "has a collision box" deliberately disagree**,
// written down so it reads as a decision rather than a bug. Make `isSolid` drop
// `Flat` and the first line fails; give a rail a collision box and the second
// does. Either is a real change with fourteen callers behind it, and neither
// should happen by accident.
static_assert(isSolid(railRunFirst(0)) && isSolid(BlockId::RedstoneWireFirst),
              "a rail is ground for everything that asks whether a cell is occupied");
static_assert(collisionBoxes(railRunFirst(0)).count == 0 &&
                  collisionBoxes(BlockId::RedstoneWireFirst).count == 0,
              "and it is walked straight over, which is the physics' question, not this one");

/// Hides whatever is behind it. Kept separate from `isSolid` because water is
/// neither solid nor invisible, and conflating the two is how you end up either
/// walking on water or unable to see the seabed.
constexpr bool isOpaque(BlockId id) {
    return blockShape(id) == BlockShape::Full && !isCutout(id);
}

/// Whether a fence, wall or pane grows an arm toward `neighbour`.
///
/// **One owner.** The mesher had this rule written inside it, so what you saw
/// and what you bumped into were free to disagree - and they did: a lone pane
/// was drawn as a two-texel post and collided with as a full cross.
constexpr bool shapeReaches(BlockShape shape, BlockId neighbour) {
    const BlockShape other = blockShape(neighbour);
    return isOpaque(neighbour) || other == shape ||
           (shape != BlockShape::Pane && other == BlockShape::Gate) ||
           // **A wall and a pane reach each other.** The reference's wall page
           // lists glass panes and iron bars beside walls and fence gates, and
           // nothing above catches it: a pane is cutout rather than opaque and
           // carries its own shape, so a window set into a wall left both sides
           // stubbed. Unlike the nether brick fence noted at `kFenceFamilies`
           // this one *is* expressible here, because it turns on shape rather
           // than on what the two blocks are made of.
           (shape == BlockShape::Wall && other == BlockShape::Pane) ||
           (shape == BlockShape::Pane && other == BlockShape::Wall);
}

// **Both directions, and the pair that must stay apart.** A one-sided rule is
// how a wall would grow an arm toward a pane that grew none back. The fence
// line is the anchor rather than a restatement: `BlockShape::Wall`'s own note
// says a fence and a wall never connect, and this is what notices if widening
// the pane rule quietly takes that with it.
static_assert(shapeReaches(BlockShape::Wall, paneAt(0)) &&
                  shapeReaches(BlockShape::Pane, wallAt(0)),
              "a wall and a pane reach each other, both ways round");
static_assert(!shapeReaches(BlockShape::Fence, wallAt(0)) &&
                  !shapeReaches(BlockShape::Wall, fenceAt(0)),
              "a fence and a wall still do not");

constexpr std::uint8_t connectionBits(BlockShape shape, BlockId north, BlockId south, BlockId west,
                                      BlockId east) {
    std::uint8_t bits = 0;
    if (shapeReaches(shape, north)) {
        bits |= ConnectNorth;
    }
    if (shapeReaches(shape, south)) {
        bits |= ConnectSouth;
    }
    if (shapeReaches(shape, west)) {
        bits |= ConnectWest;
    }
    if (shapeReaches(shape, east)) {
        bits |= ConnectEast;
    }
    return bits;
}

/// Whether a shape's extent depends on what stands beside it. The three that do
/// cannot be answered from an id alone, which is why there is a second pair of
/// box functions that take the answer.
constexpr bool connectsToNeighbours(BlockShape shape) {
    return shape == BlockShape::Fence || shape == BlockShape::Wall || shape == BlockShape::Pane;
}

constexpr BlockBoxes collisionBoxesWith(BlockId id, std::uint8_t connections) {
    switch (blockShape(id)) {
    case BlockShape::Fence:
        return fenceBoxes(connections);
    case BlockShape::Wall:
        return wallBoxes(connections);
    case BlockShape::Pane:
        return paneBoxes(connections);
    default:
        return collisionBoxes(id);
    }
}

constexpr BlockBoxes selectionBoxesWith(BlockId id, std::uint8_t connections) {
    return connectsToNeighbours(blockShape(id)) ? collisionBoxesWith(id, connections) : selectionBoxes(id);
}

/// **The same question once the cell overhead is known** - the third fact
/// `drawnBoxes` has always been handed and this answer never was.
///
/// A vine grows a ceiling panel when the block above it is opaque, so its drawn
/// extent and its *aimable* extent disagreed and the crack overlay sat on a
/// panel the crosshair could not reach. `vineBoxes` owns that shape, and this
/// clause mirrors `drawnBoxes`' vine clause exactly - the same function with the
/// same two arguments - so the pair cannot drift. Everything else defers to the
/// two-argument form rather than restating any part of it.
///
/// **The context-free `selectionBoxes(BlockId)` overload stays and this does not
/// replace it**: it is read by `static_assert`s *and* by a function body in this
/// file, so it is load-bearing code rather than test scaffolding.
///
/// Added for `Collision.hpp`, whose `worldSelectionBoxes` already holds the
/// world and the cell and can therefore supply the argument.
///
/// **Stated as a constraint rather than as the state of the world, so it cannot
/// rot**: while this overload exists, a caller holding `opaqueAbove` should
/// prefer it, and the vine roof must not be written a third time. Falsified by
/// a third caller of `vineBoxes` appearing - run that search and expect exactly
/// two, this one and `drawnBoxes`, rather than trusting this paragraph.
constexpr BlockBoxes selectionBoxesWith(BlockId id, std::uint8_t connections, bool opaqueAbove) {
    if (blockShape(id) == BlockShape::Vine) {
        return vineBoxes(vineSides(id), opaqueAbove);
    }
    return selectionBoxesWith(id, connections);
}

/// **The argument has to change the answer, or it is decoration.** `roof` adds
/// exactly one box inside `vineBoxes`, so this holds whatever sides the id
/// carries, and it fails the moment the vine clause above is dropped.
static_assert(selectionBoxesWith(BlockId::Vine, 0, true).count ==
                  selectionBoxesWith(BlockId::Vine, 0, false).count + 1,
              "opaqueAbove must reach vineBoxes and add the ceiling panel");
/// **The compiled negative control, and the more valuable half.** The
/// two-argument form answers as though nothing were overhead - which is exactly
/// the defect this overload exists to remove. Pinning the old behaviour here
/// proves the fix changes something rather than merely looking as though it
/// does, which is the failure eleven asserts in this file once passed under.
static_assert(selectionBoxesWith(BlockId::Vine, 0).count ==
                  selectionBoxesWith(BlockId::Vine, 0, false).count,
              "the two-argument form must still answer as though nothing were overhead");
/// And the third fact must stay inert everywhere else, or it has leaked into
/// blocks that have no roof rule at all.
static_assert(selectionBoxesWith(BlockId::Stone, 0, true).count ==
                  selectionBoxesWith(BlockId::Stone, 0, false).count,
              "opaqueAbove must change nothing for a block that is not a vine");

/// **Exactly the boxes the mesher draws**, and the single owner of that answer.
///
/// Not `collisionBoxes`, not `selectionBoxes`, not `modelSilhouette`, and the
/// difference is the point: a fence's drawn rails are not the posts you bump
/// into, a plant's pick column is not its blades, and a silhouette is one box
/// round a lantern that is drawn as two. Every one of those three is
/// deliberately a different shape, so **anything that wants to draw something
/// else onto a block - a crack overlay, say - has to ask this one**, or it
/// decorates a shape that is not there and the decoration hangs in mid-air.
///
/// `connections` comes from `connectionBits` and `opaqueAbove` is the cell
/// overhead; both are facts about the world that no id can carry on its own.
///
/// **Two families are not boxes at all, and they are the limit of this
/// answer.** A `Cross` plant is two crossed quads, so it returns nothing here
/// and its callers handle it. A **rail** is one plane - `rail_flat.json` is a
/// zero-thickness element - and an *ascending* rail is that plane tilted 45
/// degrees, which an axis-aligned box cannot express at any count. The 28
/// sloped rail ids therefore get their flat box here while the mesher draws a
/// ramp, so a crack overlay on one sits under the track rather than on it.
/// That is stated rather than fixed: widening the box to the ramp's extent
/// would put a full-cell-tall crack on a thin rail, which is wrong in a louder
/// way, and the tilted quad the overlay actually wants is not a `BlockBoxes`.
/// The 18 flat rail ids are unaffected - their plane and their box coincide.
///
/// **The crack overlay no longer asks this about a rail.** `BlockOutline`'s
/// crack builder now grows the tilted plane itself from `railShape` and
/// `railCornerY` - the same owners the mesher reads - exactly as it already did
/// for a plant's blades. So the qualification above stands as an honest
/// statement about *this* answer, but it no longer has that consumer behind it,
/// and the overlay is no longer evidence for widening the box to the ramp.
constexpr BlockBoxes drawnBoxes(BlockId id, std::uint8_t connections, bool opaqueAbove) {
    const BlockShape shape = blockShape(id);
    if (shape == BlockShape::Model || shape == BlockShape::Cocoa || shape == BlockShape::Bed) {
        const ModelBoxes model = postModel(id);
        BlockBoxes boxes{};
        for (int i = 0; i < model.count; ++i) {
            boxes.boxes[boxes.count++] = model.boxes[i].box;
        }
        return boxes;
    }
    if (shape == BlockShape::Vine) {
        return vineBoxes(vineSides(id), opaqueAbove);
    }
    if (shape == BlockShape::Ladder) {
        return ladderBoxes(ladderFacing(id));
    }
    if (drawsWithoutColliding(id)) {
        return uncollidableDrawnBoxes(id);
    }
    if (shape == BlockShape::Fence) {
        return fenceRailBoxes(connections);
    }
    if (shape == BlockShape::Wall) {
        return wallBoxes(connections);
    }
    if (shape == BlockShape::Pane) {
        return paneBoxes(connections);
    }
    return collisionBoxes(id);
}

static_assert(drawnBoxes(BlockId::Stone, 0, false).count == 1,
              "an ordinary cube is one box, or every crack on one is drawn six times over");
static_assert(drawnBoxes(BlockId::Poppy, 0, false).count == 0,
              "a plant is two crossed quads and has no box answer at all - a caller that forgets "
              "that draws a cage round every flower instead of cracking its blades");

/// Whether everything this block *draws* sits inside what you can *aim at*.
///
/// **The two are separate answers to "what is this block", which is exactly the
/// shape that lets one of them be right on its own.** A cage narrower than the
/// drawing means visible geometry the crosshair passes straight through and a
/// crack overlay that stops short of what is breaking, and nothing in the build
/// notices - it needs a sweep comparing the two unions corner for corner.
///
/// Deliberately *not* asserted for every id: a lantern's hanging chain and a
/// stonecutter's paper-thin saw blade are outside the reference's own hitbox
/// too, so a blanket rule here would be wrong against the reference rather than
/// right. It pins the ones that were read off a model and then inset by hand.
constexpr bool cageHoldsTheDrawing(BlockId id) {
    const BlockBoxes cage = selectionBoxes(id);
    const BlockBoxes drawn = drawnBoxes(id, 0, false);
    if (cage.count == 0) {
        return drawn.count == 0;
    }
    for (int d = 0; d < drawn.count; ++d) {
        bool inside = false;
        for (int c = 0; c < cage.count; ++c) {
            if (drawn.boxes[d].minX >= cage.boxes[c].minX - 1e-4f &&
                drawn.boxes[d].minY >= cage.boxes[c].minY - 1e-4f &&
                drawn.boxes[d].minZ >= cage.boxes[c].minZ - 1e-4f &&
                drawn.boxes[d].maxX <= cage.boxes[c].maxX + 1e-4f &&
                drawn.boxes[d].maxY <= cage.boxes[c].maxY + 1e-4f &&
                drawn.boxes[d].maxZ <= cage.boxes[c].maxZ + 1e-4f) {
                inside = true;
            }
        }
        if (!inside) {
            return false;
        }
    }
    return true;
}

static_assert(cageHoldsTheDrawing(BlockId::Anvil) && cageHoldsTheDrawing(BlockId::ChippedAnvil) &&
                  cageHoldsTheDrawing(BlockId::DamagedAnvil),
              "an anvil's top runs the whole cell in Z - template_anvil `from [3,10,0] to "
              "[13,16,16]` - so a cage inset on both horizontal axes leaves the horn outside it");
// **The negative control, so this proves something.** Shrink the anvil's cage
// back to the inset-on-both-axes box it used to be and the predicate must
// reject it; a containment test that passes whatever it is handed is the
// eleven-asserts-pointing-at-the-wrong-texture failure written down again.
constexpr BlockBoxes anvilCageInsetBothWays() {
    constexpr float t = 1.0f / 16.0f;
    BlockBoxes boxes{};
    boxes.boxes[0] = {2 * t, 0.0f, 2 * t, 14 * t, 1.0f, 14 * t};
    boxes.count = 1;
    return boxes;
}
constexpr bool drawingEscapes(const BlockBoxes& cage, BlockId id) {
    const BlockBoxes drawn = drawnBoxes(id, 0, false);
    for (int d = 0; d < drawn.count; ++d) {
        if (drawn.boxes[d].minZ < cage.boxes[0].minZ - 1e-4f ||
            drawn.boxes[d].maxZ > cage.boxes[0].maxZ + 1e-4f) {
            return true;
        }
    }
    return false;
}
static_assert(drawingEscapes(anvilCageInsetBothWays(), BlockId::Anvil),
              "the cage this replaced really did leave the anvil's top outside it, so the assert "
              "above is measuring something rather than agreeing with itself");
static_assert(!drawingEscapes(selectionBoxes(BlockId::Anvil), BlockId::Anvil),
              "and the one in the file now does not");

/// Whether the inventory and a dropped item draw this block as the flat picture
/// its texture actually is rather than as a little cube. **Shape, not a list**,
/// so a new plant or a new pane is right the day it is added.
constexpr bool usesFlatIcon(BlockShape shape) {
    return shape == BlockShape::Cross || shape == BlockShape::Flat || shape == BlockShape::Ladder ||
           shape == BlockShape::Vine || shape == BlockShape::Pane;
}

/// Whether a slot picture and a dropped item are built from `postModel` rather
/// than from boxes cut out of a cube.
constexpr bool usesModelIcon(BlockShape shape) {
    return shape == BlockShape::Model || shape == BlockShape::Cocoa || shape == BlockShape::Bed;
}

/// **Blocks whose slot picture is flat while the block itself is a model.**
///
/// The reference keys a held item's picture on `models/item/*.json`, which is a
/// separate file from `models/block/*.json` and is allowed to disagree with it.
/// A torch in the world is a post with a flame on top; a torch in the hotbar is
/// the flat `block/torch` sprite. We had only one answer for both, because
/// `usesFlatIcon` and `usesModelIcon` are keyed on `BlockShape` and shape also
/// drives how the block is drawn in the world - where a torch really is a
/// model. **So this cannot be a widening of `usesFlatIcon`**; it is the
/// per-id half that shape genuinely cannot carry.
///
/// **Five families, 44 ids, and no new art.** Measured by intersecting every id
/// we send down the model-icon path against every reference item model that has
/// a sibling block model and a `layer0` of `block/*`: 84 such names in the
/// reference, 69 distinct names on our model path, 5 in both - torch,
/// soul torch, redstone torch, lever and tripwire hook. `layer0` names the
/// block texture we already stage, so the sprite exists today.
///
/// **The other 51 are deliberately not here.** Those name a `layer0` of
/// `item/*` - an image that is not staged and is not a block texture - so they
/// need `tools/make-reference-blocks.ps1` and the loader before they can mean
/// anything, and both are other files.
///
/// **Three of those 51 are measured and player-visible, so they are named
/// rather than left in a count.** A slot picture is 16 texels square; the
/// footprint a `postModel` icon actually fills is the union extent of its
/// boxes, wider horizontal axis by height. Candle is 2.0 x 6.0, bamboo
/// 2.0 x 16.0 and sea pickle 4.0 x 6.0 - two to four texels of sixteen - so all
/// three read as a smear in the hotbar *and* diverge from the reference, which
/// draws each flat (`item/candle`, `item/bamboo`, `item/sea_pickle`, all
/// `item/generated` or `item/handheld`). The controls in the same measurement
/// fill their slot and are right to stay models: lectern 16.0 x 16.0 over three
/// boxes, dragon egg 14.0 x 16.0 over six. **A model that is correct and
/// illegible is still a defect**, and these three are the head of the queue the
/// moment `item/*` sprites reach the block atlas.
///
/// Controls run with the intersection: lectern, dragon egg, cauldron and bell
/// all have block models and no flat item sprite, and all four score no.
constexpr bool iconIsFlatSprite(BlockId id) {
    return id == BlockId::Torch || id == BlockId::SoulTorch || isRedstoneTorch(id) ||
           isLever(id) || isTripwireHook(id);
}

/// Whether this block's slot picture and dropped item are the flat sprite.
///
/// **One owner, because the hotbar, the floor and the thrown item have to
/// agree.** Adding the fact above as a bare predicate would still leave every
/// consumer combining it with `usesFlatIcon` for itself, which is exactly the
/// shape that let the drop hand one side layer to all four walls while the
/// mesher had it right. Callers ask this and the pair below; they do not
/// combine anything.
constexpr bool iconIsFlat(BlockId id) {
    return usesFlatIcon(blockShape(id)) || iconIsFlatSprite(id);
}

/// Whether this block's slot picture and dropped item are built from
/// `postModel`. The exclusion is the whole point: a torch is a model in the
/// world and a sprite in the slot, so the icon path must stop claiming it.
constexpr bool iconIsModel(BlockId id) {
    return usesModelIcon(blockShape(id)) && !iconIsFlatSprite(id);
}

/// **Flat and model are mutually exclusive, swept rather than argued.** If both
/// could answer yes for one id, whichever consumer tested first would win and
/// the hotbar and the floor could disagree about the same block.
constexpr bool iconPathIsUnambiguous(int lo, int hi) {
    for (int i = lo; i < hi; ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (iconIsFlat(id) && iconIsModel(id)) {
            return false;
        }
    }
    return true;
}
static_assert(iconPathIsUnambiguous(0, static_cast<int>(kBlockIdCount)),
              "some block claims both the flat icon path and the model icon path");

/// **The control, and it is a rule that really is violated** rather than a
/// second true thing. Before this pair existed the two questions were asked of
/// `blockShape` alone, and a torch answered yes to the model path while the
/// reference draws it flat - so requiring the *old* pair to be exclusive of the
/// new flat answer has to fail, on all 44 ids.
constexpr bool oldIconPathAgreedWithReference(int lo, int hi) {
    for (int i = lo; i < hi; ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (usesModelIcon(blockShape(id)) && iconIsFlatSprite(id)) {
            return false;
        }
    }
    return true;
}
static_assert(!oldIconPathAgreedWithReference(0, static_cast<int>(kBlockIdCount)),
              "the defect this pair fixes has to be present in the code it replaces, or nothing "
              "changed and these accessors are decoration");

/// A block that is flat by shape is flat whatever the sprite list says, and one
/// that is neither stays neither - so the addition cannot have moved a block
/// that was already right.
static_assert(iconIsFlat(BlockId::Poppy) && !iconIsModel(BlockId::Poppy), "");
static_assert(!iconIsFlat(BlockId::Stone) && !iconIsModel(BlockId::Stone), "");
static_assert(iconIsModel(BlockId::Lectern) && !iconIsFlat(BlockId::Lectern), "");
static_assert(iconIsFlat(BlockId::Torch) && !iconIsModel(BlockId::Torch),
              "a torch is a sprite in the slot and a model in the world, which is the whole "
              "reason this is keyed on the id rather than on the shape");
static_assert(usesModelIcon(blockShape(BlockId::Torch)),
              "and the world path is untouched - widening the shape test would have flattened the "
              "torch standing in front of you as well as the one in your hand");

/// The boxes an icon and a dropped item draw this block as, for every shape
/// that is neither flat nor a model.
///
/// **One owner, because the hotbar and the floor have to agree.** The slot
/// picture had this switch written inside it and a dropped block had no
/// equivalent at all - it was one cube wearing the block's side texture, so a
/// dropped bell was a gold brick and a dropped fence was a plank.
///
/// **Two arms rather than four** for the connecting shapes, matching the
/// reference's own `*_inventory` models: what you are holding is a section of
/// fence, not a crossroads.
constexpr BlockBoxes iconBoxes(BlockId id) {
    const BlockShape shape = blockShape(id);
    BlockBoxes result;
    switch (shape) {
    case BlockShape::Fence:
        return fenceRailBoxes(ConnectWest | ConnectEast);
    case BlockShape::Wall:
        return wallBoxes(ConnectWest | ConnectEast);
    case BlockShape::Gate:
        return gateBoxes(gateFacing(id), false);
    case BlockShape::Stairs:
    case BlockShape::Door:
    case BlockShape::Trapdoor:
        // Drawn as the real thing rather than as one cube, which is what made a
        // stair's slot picture indistinguishable from plain cobblestone.
        return collisionBoxes(id);
    case BlockShape::Button: {
        // **The reference has a model just for this**: `button_inventory.json`
        // lifts the nub into the middle of the cell, `5,6,6` to `11,10,10`,
        // rather than leaving it stuck to the floor of the slot where it reads
        // as a smear along the bottom edge.
        constexpr float t = 1.0f / 16.0f;
        result.boxes[0] = {5 * t, 6 * t, 6 * t, 11 * t, 10 * t, 10 * t};
        result.count = 1;
        return result;
    }
    case BlockShape::Plate:
        // A plate has no inventory model of its own; it is drawn lying down,
        // which is what it looks like. The boxes have to come from the drawn
        // set rather than from collision, because it collides with nothing.
        return uncollidableDrawnBoxes(id);
    case BlockShape::Sign:
        // The same, and for the same reason.
        return uncollidableDrawnBoxes(id);
    default:
        // A slab stands at the height it actually is, so the picture matches
        // the block rather than implying a full cube.
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, shapeHeight(shape), 1.0f};
        result.count = 1;
        return result;
    }
}

// A lone pane is a two-texel post you walk past on either side, and only a
// joined one fills its cell. Asking `collisionBoxes` alone gave every pane in
// the game the full cross, which is an invisible shell round a sheet of glass.
static_assert(collisionBoxesWith(paneAt(0), 0).count == 1);
static_assert(collisionBoxesWith(paneAt(0), ConnectAll).count == 5);
static_assert(collisionBoxesWith(BlockId::Stone, 0).count == 1,
              "anything that does not connect must ignore the bits entirely");
static_assert(shapeReaches(BlockShape::Pane, BlockId::IronBars) &&
                  !shapeReaches(BlockShape::Pane, BlockId::PlanksFence),
              "a pane reaches toward bars and never toward a fence");

// The two models, box for box: a lantern is a body, a cap and two handle
// prongs; an end rod is a base and a shaft.
static_assert(postModel(BlockId::Lantern).count == 4);
static_assert(postModel(BlockId::SoulLantern).count == 4);
static_assert(postModel(BlockId::EndRod).count == 2);
// A model's lid is painted somewhere other than its walls, and reusing the wall
// rectangle for it is what squashed the lantern's cap into a two-texel sliver.
static_assert(postModel(BlockId::Lantern).boxes[0].topVMin != postModel(BlockId::Lantern).boxes[0].vMin);

/// Whether a block one step away in `offsetY` fully hides the face it is pressed
/// against. A full cube hides every face it touches; a bottom slab only hides
/// the one directly above it, because that is the only boundary its geometry
/// actually reaches.
constexpr bool occludesFace(BlockId neighbour, int offsetY) {
    switch (blockShape(neighbour)) {
    case BlockShape::Full:
        return isOpaque(neighbour);
    case BlockShape::Slab:
        // A half block only hides the boundary its solid half is flush against.
        return isUpperHalf(neighbour) ? offsetY == -1 : offsetY == 1;
    case BlockShape::Tilled:
        // Flush at the bottom and a texel short at the top, so it hides the
        // face beneath it and nothing else.
        return offsetY == 1;
    case BlockShape::Stairs:
        return stairIsTop(neighbour) ? offsetY == -1 : offsetY == 1;
    default:
        return false;
    }
}

/// Drawn in the transparent pass, after everything opaque.
///
/// **Water and the seventeen blended glasses.** Lava is a fluid geometrically -
/// its surface sits below the top of its cell - but it is fully opaque, so
/// putting it in the blended pass meant it neither wrote depth nor occluded
/// anything: water in front of it showed the lava through itself, and standing
/// in a lava cell you could see straight out of the world.
///
/// **The glass was in the cutout pass instead, and that is why it was solid.**
/// A cutout keeps a texel whole or throws it away, and the reference paints
/// stained and tinted glass at about half alpha across every texel, so there
/// was nothing for it to throw away and the result was an opaque cube. Plain
/// glass is deliberately not here: its art really is holes, so a cutout draws
/// it correctly and blending it would only turn its frame ghostly.
constexpr bool isTranslucent(BlockId id) {
    return isWater(id) || isBlendedGlass(id);
}

static_assert(isTranslucent(BlockId::TintedGlass) && !isTranslucent(BlockId::Glass),
              "the blended glasses go through the transparent pass; plain glass does not");
static_assert(!isOpaque(BlockId::TintedGlass),
              "a translucent block must never occlude - isOpaque reads isCutout, so the blended "
              "glasses have to stay in the cutout family even though they no longer draw as one");

/// Highest light level a source can have. Four bits per channel, so a level fits
/// in a nibble and sky plus block light fit in one byte per block.
constexpr int kMaxLight = 15;

/// How much light a block gives off. Zero for everything that is not a source.
///
/// **Bedrock's numbers, where the two editions disagree.** A brewing stand and
/// an end portal frame both glow at 1 in Java and at nothing in Bedrock, so
/// neither is named here - the brewing stand's own infobox reads
/// `light = JE: Yes (1) / BE: No`, and the end portal frame has always been 0
/// here. The combined table on the wiki's `Light` page lists both at 1 with no
/// edition tag, which is why this is written down rather than left to the next
/// reader to re-derive from the page that disagrees.
constexpr int blockLightEmission(BlockId id) {
    // Every facing of a lit furnace glows, so this asks the family rather than
    // naming one id - which is what it did, and would have left three of the
    // four directions dark.
    if (isFurnaceLit(id)) {
        return 13;
    }
    // Lava lights at full strength whatever its depth, and so does fire.
    if (isLava(id) || id == BlockId::Fire) {
        return kMaxLight;
    }
    if (isJackOLantern(id)) {
        return kMaxLight;
    }
    // A lit candle is the reference's three per candle, so a stack of four
    // lights as well as a torch.
    if (isCandle(id)) {
        return isCandleLit(id) ? 3 * candleCount(id) : 0;
    }
    // The fifth run's emitters, in the reference's own levels. **A copper bulb
    // dims as it oxidises** - 15, 12, 8, 4 - which is a table rather than a
    // formula, so it is written as one.
    if (isCopperBulb(id)) {
        if (!isCopperBulbLit(id)) {
            return 0;
        }
        constexpr int kBulbLight[4] = {15, 12, 8, 4};
        return kBulbLight[(static_cast<int>(id) - static_cast<int>(BlockId::CopperBulb)) / 2];
    }
    switch (id) {
    case BlockId::RedstoneLampLit:
    case BlockId::Campfire:
        return kMaxLight;
    case BlockId::Beacon:
    case BlockId::Conduit:
        return kMaxLight;
    case BlockId::CaveVinesBerries:
        return 14;
    case BlockId::CryingObsidian:
    case BlockId::SoulCampfire:
        return 10;
    case BlockId::LargeAmethystBud:
        return 4;
    case BlockId::MediumAmethystBud:
        return 2;
    case BlockId::SmallAmethystBud:
    case BlockId::SculkSensor:
    // Bedrock's dragon egg and brown mushroom both glow at one. The red
    // mushroom beside it does not, which is why this is two named ids and not
    // `isMushroom`.
    case BlockId::DragonEgg:
    case BlockId::BrownMushroom:
        return 1;
    case BlockId::EnchantingTable:
        return 7;
    default:
        break;
    }
    if (isEnderChest(id)) {
        return 7;
    }
    if (id == BlockId::MagmaBlock) {
        return 3;
    }
    // The new emitters, brightest first. Froglights and shroomlight are the
    // reference's full 15; the rest are dim enough that they light a cave
    // without lighting a room.
    if (id == BlockId::Shroomlight || id == BlockId::OchreFroglight ||
        id == BlockId::VerdantFroglight || id == BlockId::PearlescentFroglight) {
        return kMaxLight;
    }
    if (id == BlockId::GlowLichen) {
        return 7;
    }
    // **A flat 6 where the reference scales, and the missing thing is a state
    // rather than a number.** Mojang's `mojang-blocks.json` gives
    // `minecraft:sea_pickle` two properties: `cluster_count`, domain `[0,1,2,3]`
    // with exactly one user - so authoritative, and **zero-based, four pickles
    // is stored as 3** - and `dead_bit`. Lit only when waterlogged: 6, 9, 12, 15
    // for one to four, and 0 for any count out of water. We carry one id, so 6
    // is the one-pickle answer and is the only one representable. Whoever adds
    // the four ids: store the count zero-based to match, and `BlockDrops.hpp`
    // needs no new row - its identity tail already routes through
    // `dropCountForBlock`, which is where a candle stack is answered.
    if (id == BlockId::SeaPickle) {
        return 6;
    }
    if (id == BlockId::AmethystCluster) {
        return 5;
    }
    // **The catalyst alone.** The crimson fungus shared this line and glowed at
    // six, which is a value the reference gives neither fungus - a crimson
    // forest is lit by its shroomlights and its lava and by nothing else. The
    // warped fungus one enumerator away was never given the six, which is what
    // named it as the one-of-a-pair slip rather than a decision.
    if (id == BlockId::SculkCatalyst) {
        return 6;
    }
    if (id == BlockId::Torch) {
        return 14;
    }
    // The lights that arrived with the bow. The reference's own levels: a
    // lantern is as bright as a torch and its soul form is dimmer, exactly as
    // the soul torch is dimmer than the torch.
    if (id == BlockId::Lantern) {
        return 15;
    }
    if (id == BlockId::SoulTorch || id == BlockId::SoulLantern) {
        return 10;
    }
    if (id == BlockId::EndRod) {
        return 14;
    }
    // **The family, not the floor id.** A lit redstone torch has five ids - the
    // one standing on the floor and four hanging off walls - and naming only
    // the first left every wall-mounted one, which is where most of them are
    // placed, giving no light at all. `redstoneTorchLit` already owns exactly
    // that set for the texture path.
    if (redstoneTorchLit(id)) {
        return 7;
    }
    if (id == BlockId::SeaLantern) {
        return 15;
    }
    // Glowstone is one of the reference's full-strength sources, not one below
    // it: the same 15 as sea lantern, shroomlight, froglight and a lantern,
    // every one of which this function already gets right.
    return id == BlockId::Glowstone ? kMaxLight : 0;
}

// **Written as the whole expression a caller evaluates, id and all.** Change
// the glowstone line back to 14 and the first fails; drop `redstoneTorchLit`
// for `id == BlockId::RedstoneTorch` and the second fails on the wall run while
// the floor torch keeps passing, which is exactly how that bug survived.
static_assert(blockLightEmission(BlockId::Glowstone) == kMaxLight,
              "glowstone is a full-strength source, one level brighter than it used to be here");
static_assert(blockLightEmission(BlockId::RedstoneTorch) == 7 &&
                  blockLightEmission(BlockId::RedstoneTorchWallFirst) == 7 &&
                  blockLightEmission(BlockId::RedstoneTorchWallLast) == 7 &&
                  blockLightEmission(BlockId::RedstoneTorchOff) == 0 &&
                  blockLightEmission(BlockId::RedstoneTorchOffWallFirst) == 0,
              "every lit redstone torch glows and no unlit one does, wall-mounted included");
// The pairs and near-pairs this function has already got wrong once each: two
// enumerators apart, one given a value the other was not.
static_assert(blockLightEmission(BlockId::CrimsonFungus) == 0 &&
                  blockLightEmission(BlockId::WarpedFungus) == 0 &&
                  blockLightEmission(BlockId::SculkCatalyst) == 6,
              "neither fungus glows in the reference; the catalyst that shared the line does");
static_assert(blockLightEmission(BlockId::DragonEgg) == 1 &&
                  blockLightEmission(BlockId::BrownMushroom) == 1 &&
                  blockLightEmission(BlockId::RedMushroom) == 0,
              "the dim ones - and the red mushroom, which is the half of the pair that does not");
static_assert(blockLightEmission(BlockId::BrewingStand) == 0 &&
                  blockLightEmission(BlockId::EndPortalFrame) == 0,
              "both are Java-only emitters and Bedrock is the reference");

/// Whether a chunk far from the player may leave this block's geometry out.
///
/// **A rendering tier, not a simulation one.** The block is still in the world:
/// it still breaks, drops, burns, gets washed away and is still saved. Only its
/// triangles are skipped, and they come back the moment the chunk is re-meshed
/// at full detail.
///
/// Plants and vines are the whole of the set, because they are the only
/// geometry that neither occupies a cell you can walk into nor contributes
/// anything to the world's silhouette - a hillside with its grass left out has
/// exactly the same outline as one with it. Leaves are deliberately **not** in
/// here: they are full cubes and a forest without them is a forest of bare
/// trunks.
///
/// A light source is kept whatever its shape, or a torch on a distant hillside
/// would go out while its light stayed baked into the ground around it.
///
/// **And nothing mineral is dropped**, whatever it is drawn as. A stalagmite,
/// a coral head and a chorus tree are as much a part of the landscape's
/// silhouette as the rock they grow out of, and popping them in as the player
/// walks toward them is the one thing this tier is meant not to do.
constexpr bool isDistantDecoration(BlockId id) {
    if (isMineralGrowth(id)) {
        return false;
    }
    // Bamboo is a stalk rather than a cross since it gained `bamboo1_age0`'s
    // model, and a jungle is thousands of them - dropping them at distance is
    // most of what this tier is worth there. Named the way the torch is in
    // `isWashedAway`, and for the same reason: the shape stopped answering.
    if (id == BlockId::Bamboo) {
        return true;
    }
    const BlockShape shape = blockShape(id);
    return (shape == BlockShape::Cross || shape == BlockShape::Vine) && blockLightEmission(id) == 0;
}

// **Delete the `isMineralGrowth` guard and this fails.** Written against a
// plant of the same shape that is still dropped, so it cannot be satisfied by
// disabling the tier altogether.
static_assert(!isDistantDecoration(BlockId::PointedDripstone) &&
                  !isDistantDecoration(BlockId::TubeCoral) &&
                  !isDistantDecoration(BlockId::ChorusPlant) &&
                  isDistantDecoration(BlockId::TallGrass),
              "a dripstone cave and a coral reef are landscape, not grass");

/// Held up by something other than the block below: a wall, a ceiling, or
/// nothing at all.
///
/// **The one place this file keeps getting the pair wrong.** Every entry here
/// is a block that *looks* like it needs a floor because of its shape and does
/// not, and each one was found by mining the block under it and watching
/// something pop off that the reference leaves alone.
constexpr bool hangsFromSomethingElse(BlockId id) {
    // The reference grows these on any of the six faces of their support, which
    // is what a geode is: crystals pointing inward from the walls and the roof.
    // With support-below only, five of the six faces are unusable.
    return id == BlockId::AmethystCluster || id == BlockId::SmallAmethystBud ||
           id == BlockId::MediumAmethystBud || id == BlockId::LargeAmethystBud ||
           // **The reference's two multiface overlays, and they must answer the
           // same.** Glow lichen and sculk vein are placed on any of the six
           // faces by the identical rule; only the lichen was listed here, so a
           // vein on a wall fell when the floor under it went and a lichen
           // beside it did not.
           id == BlockId::GlowLichen || id == BlockId::SculkVein ||
           // Hung from a ceiling by definition. A stalactite is the half of
           // pointed dripstone that points down, and without this no stalactite
           // can exist at all.
           id == BlockId::PointedDripstone || id == BlockId::HangingRoots ||
           id == BlockId::SporeBlossom || id == BlockId::CaveVines ||
           id == BlockId::CaveVinesBerries || id == BlockId::WeepingVines ||
           // A cobweb is the one cross-shaped block that is not a plant: the
           // reference places it anywhere and holds it up with nothing. Ours
           // deleted it when its floor went, and `settleAround` spills with bare
           // hands, so the string was lost too.
           id == BlockId::Cobweb;
}

/// Falls if whatever it was standing on goes away. True for the flat things
/// that have nothing to hold themselves up with, and for a torch, which used to
/// get this free from being cross-shaped.
constexpr bool needsSupportBelow(BlockId id) {
    // A sign or a banner standing on the ground falls over without one; the
    // wall-mounted form is held by the wall and the hanging form by the ceiling,
    // so neither of those asks.
    if (isSignLike(id)) {
        return !signOnWall(id) && signKind(id) != 1;
    }
    // **A torch on a wall is held by the wall**, and this is the same question
    // the sign line above already answers correctly - written there, missing
    // here. `isWallTorch` has existed since the torch gained a model and had no
    // caller: mining the block beneath a wall-mounted redstone torch popped it
    // off while the wall it was screwed to stood untouched.
    if (isTorchBlock(id)) {
        return !isWallTorch(id);
    }
    // A button and a lever store which face they were stuck to, exactly as a
    // sign does. Only the floor mounts ask for a block below; the ceiling and
    // wall ones are held by what they hang off.
    if (isButton(id)) {
        return buttonMount(id) == 0;
    }
    if (isLever(id)) {
        return leverMount(id) == LeverFloorX || leverMount(id) == LeverFloorZ;
    }
    // Floor-only components. Each is a `Model` or `Plate` shape, so none of them
    // was reached by the two shape tests below and all of them hung in mid-air
    // when their support went - paying no drop, which is the part a player
    // notices.
    if (isPressurePlate(id) || isRepeater(id) || isComparator(id)) {
        return true;
    }
    if (hangsFromSomethingElse(id)) {
        return false;
    }
    // **A cactus needs a floor and had no rule at all**, so it floated when its
    // sand was mined. Only this half is here: the reference also demands that
    // the block below be sand, red sand, suspicious sand or another cactus, and
    // that all four horizontal neighbours be clear - both of those are
    // placement and neighbour rules that live outside this file.
    if (id == BlockId::Cactus) {
        return true;
    }
    // **The three that stopped being crosses when they gained their models.**
    // A candle, a stalk of bamboo and a sea pickle each still need the block
    // under them, and the shape test on the line below no longer reaches any
    // of them - which is exactly how a torch floated once before.
    if (isCandle(id) || id == BlockId::Bamboo || id == BlockId::SeaPickle) {
        return true;
    }
    return blockShape(id) == BlockShape::Cross || blockShape(id) == BlockShape::Flat;
}

// **The wall-mounted trio, each written against the floor form of the same
// family**, so a change that breaks one side is visible against the other.
// Delete the `isTorchBlock` branch and the first fails; delete the `isButton`
// or `isLever` branch and the second or third does.
static_assert(needsSupportBelow(BlockId::Torch) &&
                  !needsSupportBelow(BlockId::RedstoneTorchWallFirst) &&
                  needsSupportBelow(BlockId::RedstoneTorch),
              "a torch on a wall is held by the wall; one on the floor is not");
static_assert(needsSupportBelow(buttonAt(0, 0, false)) &&
                  !needsSupportBelow(buttonAt(0, 2, false)) &&
                  !needsSupportBelow(buttonAt(0, 1, false)),
              "only a floor-mounted button asks for a floor");
static_assert(needsSupportBelow(leverAt(LeverFloorX, false)) &&
                  needsSupportBelow(leverAt(LeverFloorZ, true)) &&
                  !needsSupportBelow(leverAt(LeverCeilingX, false)) &&
                  !needsSupportBelow(leverAt(LeverWallFirst, false)),
              "and only a floor-mounted lever, whichever way its handle throws and whether or not "
              "it is on");
// **Remove any id from `hangsFromSomethingElse` and this fails.** It is written
// against `blockShape`, which is what used to answer for all of them, so it also
// fires if one of them stops being cross-shaped and the exception goes stale.
static_assert(blockShape(BlockId::PointedDripstone) == BlockShape::Cross &&
                  !needsSupportBelow(BlockId::PointedDripstone) &&
                  !needsSupportBelow(BlockId::AmethystCluster) &&
                  !needsSupportBelow(BlockId::CaveVines) &&
                  !needsSupportBelow(BlockId::HangingRoots) &&
                  !needsSupportBelow(BlockId::Cobweb) && needsSupportBelow(BlockId::TallGrass),
              "the things that hang, against a plant that genuinely needs a floor");
// **The two multiface overlays, written against each other.** Drop `SculkVein`
// from `hangsFromSomethingElse` and this fails; the glow lichen half is the
// counterweight, so it also fails if the whole clause is deleted rather than
// only the vein.
static_assert(!needsSupportBelow(BlockId::SculkVein) &&
                  !needsSupportBelow(BlockId::GlowLichen),
              "sculk vein and glow lichen are placed on any of the six faces by the same rule, so "
              "they must answer support the same way");

static_assert(blockShape(BlockId::Farmland) == BlockShape::Tilled &&
                  blockShape(BlockId::DirtPath) == BlockShape::Tilled,
              "tilled ground is a sixteenth short, not a full cube");
static_assert(isSolid(BlockId::Farmland) && !needsSupportBelow(BlockId::Farmland),
              "tilled ground is stood on, and holds itself up");
/// Held up by the water itself rather than by anything solid. **The lily pad is
/// the only one**, and it is why support is a question with two answers rather
/// than a single `isSolid` test: a pad that demanded solid ground could not be
/// put on a lake at all, and one that shared the water's cell punched a hole in
/// the surface.
constexpr bool restsOnWater(BlockId id) {
    return id == BlockId::LilyPad;
}

/// Blocks a flow runs *around* rather than through, even though their shape says
/// otherwise.
///
/// **`BlockShape::Cross` stopped meaning "a plant" and this is the bill.** It is
/// now the general shape for anything drawn on two diagonals, so a flow was
/// deleting an amethyst geode, a dripstone cave's stalagmites, a conduit, a sea
/// pickle and every coral on the reef - and underwater, a flow is routine. The
/// reference destroys none of them: it waterlogs them, which in a world where a
/// cell holds one block is the same answer as leaving them where they are.
///
/// Glow lichen is the one with a bug number: MC-212116 and MCPE-133803 are both
/// resolved Works As Intended, "cannot be destroyed by flowing water or lava".
constexpr bool survivesFlow(BlockId id) {
    return isMineralGrowth(id) || id == BlockId::GlowLichen ||
           // Held up by the water rather than broken by it, and asserted below
           // because deleting a pad with the water under it is instant.
           restsOnWater(id) ||
           // The reference waterlogs the vein rather than breaking it, and it is
           // on a wall the water is running down.
           id == BlockId::SculkVein;
}

/// Destroyed and dropped when water spreads into it, rather than damming the
/// flow.
///
/// **The reference's list is far wider than "plants and torches", and this held
/// only the cross-shaped half of it.** Snow layers, carpets, redstone dust,
/// rails and tripwire string all wash away there, and every one of them dammed
/// a flow here: a single carpet on a stone floor stopped a bucket of water
/// dead, and a rail line across a stream was a functioning aqueduct wall.
///
/// It is written as named families rather than `blockShape(id) == Flat`, which
/// is the tempting one-liner and is wrong twice over - see `survivesFlow` above
/// for the two flat exceptions and for the cross-shaped ones the shape test got
/// wrong in the other direction.
///
/// The torch is named outright because it stopped being cross-shaped when it
/// gained a real model. Moss carpet is named separately from `isCarpet`, which
/// covers only the sixteen wool colours.
constexpr bool isWashedAway(BlockId id) {
    if (survivesFlow(id)) {
        return false;
    }
    return blockShape(id) == BlockShape::Cross || isVine(id) || isTorchBlock(id) ||
           isSnowLayer(id) || isCarpet(id) || id == BlockId::MossCarpet || isRedstoneWire(id) ||
           isRail(id) || isTripwire(id) ||
           // A stalk of bamboo, named for the torch's reason: it stopped being
           // cross-shaped when it gained a real model, and the reference does
           // break it when water runs into it. The candle beside it in that
           // move is deliberately not here - the reference waterlogs a candle,
           // and `canWaterlog` names it for that.
           id == BlockId::Bamboo ||
           // **The non-solid redstone components**, which the reference's own
           // water page calls "some other redstone components" and each of whose
           // pages spells out: removed and dropped "if water or lava flows into
           // its space". Every one of them dammed a stream while the dust beside
           // it correctly washed away. A daylight detector and a lightning rod
           // are deliberately not here - the reference waterlogs the rod and the
           // detector is a solid slab.
           isButton(id) || isPressurePlate(id) || isLever(id) || isRepeater(id) ||
           isComparator(id) || isTripwireHook(id) ||
           // Named outright on the reference's water page beside heads and
           // torches.
           id == BlockId::FlowerPot || id == BlockId::EndRod ||
           // "Powder snow is broken by water or lava" - the standard way of
           // clearing a powder snow trap did nothing at all.
           id == BlockId::PowderSnow ||
           // "An azalea also breaks if a piston extends or pushes a block into
           // its location, or if water flows into it." Both azaleas are `Full`
           // and stay solid; only this answer changes.
           id == BlockId::Azalea || id == BlockId::FloweringAzalea;
}

static_assert(isWashedAway(BlockId::CarpetRunFirst) && isWashedAway(BlockId::MossCarpet) &&
                  isWashedAway(BlockId::RedstoneWireFirst) && isWashedAway(railRunFirst(0)),
              "the reference washes all of these away; each one used to dam a flow");
// **Drop either exception from `survivesFlow` and these fail**, which is the
// whole reason the widening is a list and not `blockShape(id) == Flat`.
static_assert(!isWashedAway(BlockId::LilyPad), "a pad washed away by the water holding it up");
static_assert(!isWashedAway(BlockId::SculkVein), "the reference waterlogs the vein, not breaks it");
// **Put `survivesFlow` back behind the `Cross` test and every one of these
// fails.** Written against a plant of the same shape that genuinely does wash
// away, so neither half can be satisfied by deleting the other.
static_assert(!isWashedAway(BlockId::AmethystCluster) &&
                  !isWashedAway(BlockId::PointedDripstone) && !isWashedAway(BlockId::Conduit) &&
                  !isWashedAway(BlockId::SeaPickle) && !isWashedAway(BlockId::TubeCoral) &&
                  !isWashedAway(BlockId::TubeCoralFan) && !isWashedAway(BlockId::GlowLichen) &&
                  !isWashedAway(BlockId::ChorusPlant) && isWashedAway(BlockId::TallGrass),
              "a flow runs around stone and coral and through grass");
// The four the reference names and this did not. Delete any one line above and
// one of these fails.
static_assert(isWashedAway(buttonAt(0, 0, false)) && isWashedAway(leverAt(LeverFloorX, false)) &&
                  isWashedAway(BlockId::RepeaterRunFirst) &&
                  isWashedAway(BlockId::ComparatorRunFirst) &&
                  isWashedAway(BlockId::TripwireHookRunFirst) &&
                  isWashedAway(BlockId::FlowerPot) && isWashedAway(BlockId::EndRod) &&
                  isWashedAway(BlockId::PowderSnow) && isWashedAway(BlockId::Azalea) &&
                  isWashedAway(BlockId::FloweringAzalea),
              "and the components, pot, rod, powder snow and azaleas it left damming a stream");

/// The sixteen powders, and the set block each one becomes on contact with
/// water. Both runs are in the same colour order, which is what lets this be
/// arithmetic instead of a sixteen-case switch - and the `static_assert`s below
/// are what stop that assumption rotting silently.
constexpr bool isConcretePowder(BlockId id) {
    return id >= BlockId::WhiteConcretePowder && id <= BlockId::BlackConcretePowder;
}

constexpr BlockId concreteFor(BlockId powder) {
    return static_cast<BlockId>(static_cast<int>(BlockId::WhiteConcrete) +
                                (static_cast<int>(powder) -
                                 static_cast<int>(BlockId::WhiteConcretePowder)));
}

static_assert(concreteFor(BlockId::WhiteConcretePowder) == BlockId::WhiteConcrete);
static_assert(concreteFor(BlockId::BlackConcretePowder) == BlockId::BlackConcrete);
static_assert(concreteFor(BlockId::CyanConcretePowder) == BlockId::CyanConcrete);
static_assert(static_cast<int>(BlockId::BlackConcrete) - static_cast<int>(BlockId::WhiteConcrete) ==
                  static_cast<int>(BlockId::BlackConcretePowder) -
                      static_cast<int>(BlockId::WhiteConcretePowder),
              "the two colour runs must stay the same length and order");

/// Falls straight down when nothing holds it up.
///
/// Unlike `needsSupportBelow`, which deletes a plant on the spot, one of these
/// *moves*: it has to arrive one block lower, so it is a scheduled update
/// rather than something the breaking code can settle by itself.
///
/// **The doc comment here used to read "plus concrete powder and anvils it has
/// and we do not", and both halves were stale** - the powder was handled on the
/// line below it and all three anvil ids exist. Anvils are `Model`-shaped, and
/// before this no `Model` block in the game fell at all, so an anvil placed in
/// mid-air hung there and the whole anvil-trap mechanic was missing.
constexpr bool isFalling(BlockId id) {
    return id == BlockId::Sand || id == BlockId::Gravel || isConcretePowder(id) ||
           id == BlockId::TntPrimed || isAnvil(id) ||
           // "The anvil falls in the same way sand, gravel, concrete powder, and
           // dragon eggs fall."
           id == BlockId::DragonEgg ||
           // Both open with "a fragile gravity-affected block". **They are not
           // finished here**: the reference destroys one outright if it falls
           // ("it drops nothing if it breaks, and will break if it falls"), and
           // that half belongs to `FallingBlock.cpp`, which has no
           // destroy-on-land case. Landing as a block is still nearer the
           // reference than hanging in the air, which is what they did.
           id == BlockId::SuspiciousSand || id == BlockId::SuspiciousGravel;
}

// **Scaffolding is deliberately not in the list above, and adding it would
// break the block.** The reference does drop unsupported scaffolding, but only
// once it is more than six cells from a column that reaches the ground - which
// is the whole point of the thing, since you build outward from a tower and
// walk on what you have built. `isFalling` is answered from an id alone, so it
// cannot express "how far is the nearest support", and a scaffold that falls
// the moment nothing is under it is worse than one that never falls at all.
// The missing piece is a horizontal distance solve, not an entry here.
// **Written against the shape that used to decide this.** Delete `isAnvil` and
// the first fails while sand keeps passing, which is how a whole shape family
// stayed out of a gravity rule nobody thought was shape-keyed.
static_assert(isFalling(BlockId::Anvil) && isFalling(BlockId::DamagedAnvil) &&
                  blockShape(BlockId::Anvil) == BlockShape::Model && isFalling(BlockId::Sand),
              "an anvil falls the same way sand does, whatever shape it is drawn as");
static_assert(isFalling(BlockId::DragonEgg) && isFalling(BlockId::SuspiciousSand) &&
                  isFalling(BlockId::SuspiciousGravel) && !isFalling(BlockId::Stone),
              "the three the reference calls gravity-affected and this list had never named");

/// What a new block may be put **into** rather than beside.
///
/// The reference's `canBeReplaced`, and it answers two questions that would
/// otherwise drift apart: what a falling block displaces on its way down, and
/// which cell a placement actually lands in. Aiming at a plant and getting the
/// block one cell higher is what happens when only the first is written down.
///
/// **It used to be `isWashedAway` plus air and fluid, and that coupling is now
/// broken on purpose.** The two lists overlapped by accident rather than by
/// derivation, and widening `isWashedAway` to the families the reference
/// actually washes away would have dragged every one of them in here too - so
/// aiming at a rail would have replaced it instead of placing beside it, and a
/// falling gravel block would have swallowed a redstone line rather than
/// landing on it. The reference agrees: it breaks a carpet with water and does
/// **not** let you build into one.
///
/// Snow is the single exception among the deeper families, and only the
/// thinnest layer of it: one layer deep is walked over, and the reference lets a
/// block be placed straight into it. Anything deeper is a step you would be
/// building inside.
///
/// **It used to be every cross-shaped block, which the generated table measured
/// at 327 ids, 279 of them from that one clause.** The reference's
/// `minecraft:replaceable` tag is short and is written out below - grass, fern,
/// dead bush, seagrass, vines, glow lichen, fire, one-deep snow and the nether
/// roots - and everything else the shape swept in was a block a player loses for
/// nothing: aim at the floor of a geode with a torch in hand and the amethyst
/// cluster was silently deleted, and the same for a conduit, a candle, a
/// cobweb, a sapling, a crop and every coral on a reef.
constexpr bool isReplaceable(BlockId id) {
    return id == BlockId::Air || isFluid(id) || id == BlockId::Fire ||
           // The dry ground cover, which is the whole of what the reference lets
           // you build straight through.
           id == BlockId::TallGrass || id == BlockId::Fern || id == BlockId::LargeFern ||
           id == BlockId::DeadBush || id == BlockId::Seagrass || id == BlockId::GlowLichen ||
           // The nether's equivalent, all three of which carry the tag.
           id == BlockId::NetherSprouts || id == BlockId::CrimsonRoots ||
           id == BlockId::WarpedRoots || isVine(id) ||
           (isSnowLayer(id) && snowLayerDepth(id) == 1);
    // **A torch is deliberately gone from this list.** It was named outright
    // rather than swept in by a shape, so it was a decision - but the reference
    // does not carry the tag on one, and aiming at a lit torch on a cave wall
    // with a block in hand destroyed the light source instead of building
    // beside it.
}

// **Widen `isWashedAway` again and these two hold; wire `isReplaceable` back to
// it and they fail.** That is the coupling this pair exists to keep broken.
static_assert(!isReplaceable(railRunFirst(0)) && !isReplaceable(BlockId::CarpetRunFirst) &&
                  !isReplaceable(BlockId::RedstoneWireFirst),
              "these wash away but are not built into");
static_assert(isReplaceable(BlockId::Air) && isReplaceable(BlockId::TallGrass) &&
              isReplaceable(BlockId::Water0));
// **Put `blockShape(id) == BlockShape::Cross` back and every one of these
// fails.** Each is a block that costs something real to get back and that the
// old shape test deleted without a drop; the grass on the line above is the
// counterweight, so neither half can be satisfied by removing the other.
static_assert(!isReplaceable(BlockId::AmethystCluster) && !isReplaceable(BlockId::Conduit) &&
                  !isReplaceable(BlockId::Cobweb) && !isReplaceable(BlockId::Candle) &&
                  !isReplaceable(BlockId::OakSapling) && !isReplaceable(BlockId::TubeCoral) &&
                  !isReplaceable(BlockId::ChorusPlant) && !isReplaceable(BlockId::NetherWart0) &&
                  !isReplaceable(BlockId::PointedDripstone),
              "the reference's replaceable tag is a short list and none of these is on it");
// **Delete the `isTorchBlock` removal and this fails.** Written beside a torch
// that is still washed away, because the two questions came apart on purpose.
static_assert(!isReplaceable(BlockId::Torch) && !isReplaceable(BlockId::RedstoneTorch) &&
                  isWashedAway(BlockId::Torch),
              "water still takes a torch; a block placed at one does not");

/// Anything a vein places.
/// A property of the *block*. Where each one generates belongs to the ore table
/// in the terrain generator; these answer different questions and neither is a
/// copy of the other.
constexpr bool isOre(BlockId id) {
    return id == BlockId::CoalOre || id == BlockId::IronOre || id == BlockId::CopperOre ||
           id == BlockId::GoldOre || id == BlockId::RedstoneOre || id == BlockId::LapisOre ||
           id == BlockId::DiamondOre || id == BlockId::EmeraldOre || id == BlockId::AncientDebris ||
           (id >= BlockId::DeepslateCoalOre && id <= BlockId::DeepslateEmeraldOre);
}

/// The deepslate form of an ore, which is what generates below the deepslate
/// line. **Both runs are declared in the same order**, so converting between
/// them is arithmetic rather than eight cases that could quietly disagree - and
/// the asserts below are what keep that true.
constexpr bool isDeepslateOre(BlockId id) {
    return id >= BlockId::DeepslateCoalOre && id <= BlockId::DeepslateEmeraldOre;
}

constexpr BlockId stoneOreFor(BlockId deepslateOre) {
    return static_cast<BlockId>(static_cast<int>(BlockId::CoalOre) +
                                static_cast<int>(deepslateOre) -
                                static_cast<int>(BlockId::DeepslateCoalOre));
}

constexpr BlockId deepslateOreFor(BlockId stoneOre) {
    return static_cast<BlockId>(static_cast<int>(BlockId::DeepslateCoalOre) +
                                static_cast<int>(stoneOre) - static_cast<int>(BlockId::CoalOre));
}

static_assert(static_cast<int>(BlockId::EmeraldOre) - static_cast<int>(BlockId::CoalOre) ==
                  static_cast<int>(BlockId::DeepslateEmeraldOre) -
                      static_cast<int>(BlockId::DeepslateCoalOre),
              "the two ore runs must be the same length");
static_assert(stoneOreFor(BlockId::DeepslateDiamondOre) == BlockId::DiamondOre);
static_assert(deepslateOreFor(BlockId::LapisOre) == BlockId::DeepslateLapisOre);

/// The eight ores that have a deepslate form. Ancient debris does not - it is
/// already the deepest thing in the ground and has no shallow half.
constexpr bool hasDeepslateForm(BlockId id) {
    return id >= BlockId::CoalOre && id <= BlockId::EmeraldOre;
}

/// Whether this block can share its cell with water.
///
/// **Plants only** - a named divergence from the reference, which waterlogs
/// slabs and stairs too. Two reasons: a stair sharing a cell with water fights
/// that water for the same depth values and shimmers, and more importantly
/// putting a real block into a source should *destroy* the source rather than
/// leave it running around the block's edges.
constexpr bool canWaterlog(BlockId id) {
    // A candle and a sea pickle are named because they stopped being crosses
    // when they gained the reference's own models, and both stand in water
    // there - a sea pickle grows in it. Bamboo made the same move and is
    // deliberately absent: the reference breaks a stalk rather than flooding
    // it, which is what `isWashedAway` says about it.
    return blockShape(id) == BlockShape::Cross || isCandle(id) || id == BlockId::SeaPickle;
}

static_assert(canWaterlog(BlockId::SeaPickle) && canWaterlog(BlockId::Candle) &&
                  !canWaterlog(BlockId::Bamboo),
              "delete either name above and a pickle or a candle displaces the sea it stands in");
static_assert(canWaterlog(BlockId::TallGrass) && canWaterlog(BlockId::Kelp),
              "a plant stands in water");
static_assert(!canWaterlog(BlockId::StoneSlab) && !canWaterlog(BlockId::CobbleStairs0) &&
                  !canWaterlog(BlockId::Stone),
              "anything that occupies its cell displaces the water instead");

/// Drags whatever walks on it. Honey in the reference; the honeycomb block is
/// ours, because a block made of wax reads as sticky whether or not the
/// reference agrees.
///
/// **A slime block does not belong here**, however much the name suggests it.
/// Its three callers are all movement - walking speed, fall damage and the
/// drag applied while standing - and the reference makes slime *bounce* rather
/// than cling: adding it would slow a player crossing slime and swallow the
/// bounce, which is the opposite of what the block is for. Slime needs a
/// restitution rule in the player, not membership of a drag predicate.
constexpr bool isSticky(BlockId id) {
    return id == BlockId::HoneyBlock || id == BlockId::HoneycombBlock;
}

/// Either state of a charge. It blinks between the two while its fuse burns, so
/// everything that cares whether a charge is *there* has to ask this rather
/// than naming one of them.
constexpr bool isTntBlock(BlockId id) {
    return id == BlockId::Tnt || id == BlockId::TntPrimed;
}

/// What an axe turns this log into, or the log itself when there is no stripped
/// form - which is now nothing, and the audit's `log-strips-to-itself` check is
/// what keeps it that way.
///
/// **`default: return id` is a `default:` that returns a real value**, this
/// project's third recorded failure shape, and it is deliberate here for a
/// reason worth writing down rather than rediscovering: the caller strips
/// whatever it hits, so all three thousand ids reach this and "unchanged" is
/// the only honest answer for stone. It is safe only because the caller
/// compares the result against what it passed in and does nothing when they
/// match - which is also the audit check named above.
///
/// **The eleven logs were not the whole of it.** Ten bark blocks - eight woods
/// and two hyphae - sat one enum run away with their stripped forms declared
/// directly after them, and an axe could not touch any of them, though the
/// reference strips wood and hyphae exactly as it strips a log. They are handled
/// by arithmetic rather than by ten more `case` labels, because the two runs are
/// the same ten in the same order, and the `static_assert` below is what stops
/// that assumption rotting the way the concrete and deepslate pairs are guarded.
constexpr BlockId strippedFor(BlockId id) {
    if (isBarkBlock(id)) {
        return static_cast<BlockId>(static_cast<int>(id) + kBarkRunLength);
    }
    switch (id) {
    case BlockId::Log:
        return BlockId::StrippedOakLog;
    case BlockId::SpruceLog:
        return BlockId::StrippedSpruceLog;
    case BlockId::BirchLog:
        return BlockId::StrippedBirchLog;
    case BlockId::JungleLog:
        return BlockId::StrippedJungleLog;
    case BlockId::AcaciaLog:
        return BlockId::StrippedAcaciaLog;
    case BlockId::DarkOakLog:
        return BlockId::StrippedDarkOakLog;
    case BlockId::CherryLog:
        return BlockId::StrippedCherryLog;
    case BlockId::MangroveLog:
        return BlockId::StrippedMangroveLog;
    case BlockId::CrimsonStem:
        return BlockId::StrippedCrimsonStem;
    case BlockId::WarpedStem:
        return BlockId::StrippedWarpedStem;
    case BlockId::BambooBlock:
        return BlockId::StrippedBambooBlock;
    default:
        return id;
    }
}

// **Delete the `isBarkBlock` branch from `strippedFor` and these fail**, and so
// does letting the two runs drift apart by an enumerator.
static_assert(strippedFor(BlockId::OakWood) == BlockId::StrippedOakWood &&
                  strippedFor(BlockId::WarpedHyphae) == BlockId::StrippedWarpedHyphae &&
                  strippedFor(BlockId::CherryWood) == BlockId::StrippedCherryWood,
              "an axe strips bark blocks in the reference too");
// Stripping is once only: nothing already stripped may change, or an axe would
// walk a block along the run one hit at a time.
static_assert(strippedFor(BlockId::StrippedOakWood) == BlockId::StrippedOakWood &&
                  strippedFor(BlockId::StrippedWarpedHyphae) == BlockId::StrippedWarpedHyphae &&
                  strippedFor(BlockId::StrippedOakLog) == BlockId::StrippedOakLog,
              "stripped wood must strip to itself");
static_assert(strippedFor(BlockId::Stone) == BlockId::Stone,
              "the catch-all returns the block unchanged, and the caller reads that as no");

/// Ordinary ground. Named because it is the value every derived motion scale is
/// measured against, so a surface at 0.6 changes nothing at all.
constexpr float kDefaultSlipperiness = 0.6f;

/// True for the ice family. Named because several rules ask about it, and the
/// last time one of them named `PackedIce` alone, adding plain ice silently made
/// frozen oceans walkable like stone.
constexpr bool isIce(BlockId id) {
    return id == BlockId::PackedIce || id == BlockId::Ice || id == BlockId::BlueIce;
}

/// How much speed a surface lets whatever stands on it keep, the reference's
/// `slipperiness`. Ice and packed ice are 0.98 there, blue ice 0.989, slime 0.8,
/// and everything else \u2014 including air, water and cobwebs \u2014 is the default.
///
/// It is read from the block **below** the feet rather than the one being stood
/// in, which is what makes a slab laid over ice as slippery as the ice.
constexpr float slipperiness(BlockId id) {
    if (id == BlockId::BlueIce) {
        return 0.989f;
    }
    return isIce(id) ? 0.98f : kDefaultSlipperiness;
}

/// Whether sky light falls through at **full strength**.
///
/// Narrower than `isLightTransparent`, and that gap is the whole reason a
/// canopy shades the ground: light drops straight down for free until it meets
/// something that is not sky-transparent, and from then on it dims one level
/// per block in every direction, downward included. Leaves therefore cast shade
/// without needing an attenuation value of their own. Ground plants do not
/// shade anything, so they stay fully sky-transparent.
constexpr bool isSkyTransparent(BlockId id) {
    // Glass is the one full cube that does not dim what is under it, which is
    // the whole point of building with it - and its coloured forms are glass.
    //
    // **A fence, a wall and a gate belong here too**, and leaving them out is
    // what made every fence line cast a hard shadow along itself: sky light
    // stopped falling at full strength the moment it met one, so the ground
    // under a paddock rail and the blocks beside it dimmed a level for no
    // visible reason. They are a post and two thin rails - almost entirely air.
    //
    // **A trapdoor belongs here for the same reason and was missed by the same
    // pass**: it is a three-texel plate, and 192 of them stopped sky light dead
    // wherever one was hung in a roof or a doorway.
    //
    // **And so does a door, which is the trapdoor's sibling and was left out
    // when the trapdoor went in** - the same three texels of wood, `kDoorThickness`
    // against the trapdoor's own, standing up instead of lying down. This is the
    // failure shape this file keeps repeating: a derivation applied to one of a
    // pair and not the other. A doorway cut through a wall went dark the moment
    // it was fitted with a door, open or shut, and the fix for the trapdoor sat
    // one line above it for four milestones.
    //
    // **Buttons, plates, signs and beds are the last four thin shapes and were
    // the same bug again, at nearly double the scale.** The comment that used
    // to sit here claimed none of them is ever the only thing between the sky
    // and a floor, which is false for three of the four: a pressure plate sits
    // on a doorstep, a sign or a banner stands in a field and a bed is put down
    // outdoors, and each darkened the cell beneath it and read as unlit itself.
    // That is 304 sign-like ids, 224 plates, 144 buttons and 128 beds against
    // the 384 the door fix was worth. The reference filters sky light through a
    // short published list - the light block, a beacon, an anvil, a hopper, a
    // brewing stand, a cauldron, ice, leaves, water, cobweb, powder snow and a
    // slab - and none of these four is on it.
    //
    // **That list is the sky half of a two-channel table, and its other half
    // now lives at `isLightTransparent`**, which names the three members - the
    // slab, plain ice and powder snow - that neither this function nor
    // `isCutout` reaches. Filtering is not free passage, so nothing on it
    // belongs *here*: a filtered block breaks the full-strength column and then
    // costs a level per cell, which is what those two functions say together
    // and neither says alone.
    //
    // **A copper grate is a lattice**, and its infobox says `transparent = Yes`
    // flatly. It is a `Full` cube so no shape reaches it, which is the only
    // reason it was ever in shadow.
    if (id == BlockId::Air || id == BlockId::Glass || isFluid(id) || id == BlockId::Fire ||
        (id >= BlockId::WhiteStainedGlass && id <= BlockId::BlackStainedGlass) ||
        isCopperGrate(id)) {
        return true;
    }
    // Asked once. `blockShape` is a long chain of family tests, and this used to
    // walk it sixteen times per call - enough that a `constexpr` sweep over
    // every id in the game hit the compiler's step limit.
    switch (blockShape(id)) {
    case BlockShape::Cross:
    case BlockShape::Flat:
    case BlockShape::Pane:
    case BlockShape::Model:
    case BlockShape::Ladder:
    case BlockShape::Vine:
    case BlockShape::Cocoa:
    case BlockShape::Fence:
    case BlockShape::Wall:
    case BlockShape::Gate:
    case BlockShape::Trapdoor:
    case BlockShape::Door:
    case BlockShape::Button:
    case BlockShape::Plate:
    case BlockShape::Sign:
    case BlockShape::Bed:
        return true;
    default:
        return false;
    }
}

// **Drop `Door` from the line above and this fails.** It is written as the pair
// it belongs to, not as a list of ids, so it also fires if either shape stops
// being thin.
static_assert(isSkyTransparent(BlockId::DoorRunFirst) ==
                  isSkyTransparent(BlockId::TrapdoorRunFirst),
              "a door and a trapdoor are the same three texels of wood");
// **Drop any one of the four shapes added above and one of these fails.** Each
// names the run's first id rather than a shape, so it also catches a family
// being moved to a shape that is not on the list.
static_assert(isSkyTransparent(BlockId::ButtonRunFirst) &&
                  isSkyTransparent(BlockId::PressurePlateRunFirst) &&
                  isSkyTransparent(BlockId::SignRunFirst) &&
                  isSkyTransparent(BlockId::HangingSignRunFirst) &&
                  isSkyTransparent(BlockId::BannerRunFirst) &&
                  isSkyTransparent(BlockId::BedRunFirst),
              "the four thin shapes the door fix missed; the reference filters sky light through "
              "none of them");
static_assert(isSkyTransparent(BlockId::CopperGrate) &&
                  isSkyTransparent(BlockId::WaxedOxidizedCopperGrate),
              "a grate is a lattice and its infobox says so outright");
static_assert(!isSkyTransparent(BlockId::Stone) && !isSkyTransparent(BlockId::Leaves),
              "rock stops the sky column, and so does a canopy - that gap is what makes shade");

/// Whether light passes through at all.
///
/// **Everything `isSkyTransparent` admits, plus the blocks that shade rather
/// than block.** The two used to be written independently and the comment above
/// claimed this one was the wider of the pair while it was in fact far
/// narrower: 368 ids passed the sky column and stopped block light dead, so a
/// fence, wall, gate, rail or redstone line was fully lit when the terrain
/// generator made it and cast a permanent shadow column the moment a player
/// placed the identical block. Deriving one from the other is what makes that
/// unrepresentable.
///
/// The reverse containment is deliberately **not** true and is the whole reason
/// there are two functions: leaves, a cactus and an azalea pass block light and
/// still break the full-strength sky column, which is what makes a canopy shade
/// the ground.
///
/// **Tinted glass is the one exception in the other direction.** It is a cutout
/// like every other glass - it has to be, or the transparent pass loses it, and
/// the `!isOpaque(TintedGlass)` assert beside `isTranslucent` pins it there -
/// but it is the one block in the game whose entire purpose is to be
/// see-through and dark: "blocks light and beacon beams completely, despite
/// being visually transparent". Four amethyst shards buy a window that does not
/// leak, and ours leaked.
///
/// **The third term is the rest of the reference's own filter table, and it is
/// here because the list was written into `isSkyTransparent`'s comment above
/// and then only partly applied** - this file's fourteenth failure shape, a
/// rule that exists, is correct and is commented in one of the two places that
/// need it. Bedrock publishes the whole thing as *amounts*, not as a boolean
/// (minecraft.wiki `Light`, "Light-filtering blocks" `{{in|Bedrock}}`):
///
///     any  light block
///     14   beacon
///      3   anvil, hopper, brewing stand, cauldron, ice, frosted ice
///      2   leaves
///      1   water, cobweb, powder snow, slabs (double slabs block outright)
///
/// Seven of those already arrive without being named: anvil, hopper, brewing
/// stand, cauldron and leaves through `isCutout`, water through `isFluid` on
/// the sky list, and cobweb through both at once (it is a cross block, so
/// `isCrossBlock` puts it in `isCutout` and `BlockShape::Cross` puts it on the
/// sky list) - and **this engine spends exactly one level per cell whatever the
/// table says**, so every one of them is approximated at 1 already. Two more,
/// the light block and frosted ice, have no id here. The three named below were
/// the only members reaching neither derivation, and they were stopping light
/// dead, which is the *double* slab's answer rather than the slab's.
///
/// **Three deliberate exclusions, each the obvious next edit and each wrong:**
/// - **`isIce` instead of `BlockId::Ice`.** Only plain ice is on the table.
///   Packed ice and blue ice are opaque in the reference - "packed ice ... is
///   opaque instead of transparent" - and widening the predicate is the second
///   failure shape written down again.
/// - **Stairs.** Not on Bedrock's table at all. *Java* gives stairs directional
///   opacity, and Bedrock is the reference, so admitting them here would be a
///   regression dressed as a fix.
/// - **The beacon, which really is on the table.** Its amount is 14: light 15
///   arrives as 1. One level per cell would make a beacon nearly free, which is
///   further from the reference than blocking outright, so it stays out until
///   there is a per-block amount to spend.
///
/// Only the *block* channel moves. None of the three becomes sky-transparent,
/// so the free-fall column still breaks on them and then dims a level per cell
/// below - which is what the table's own "attenuates by a certain number of
/// light levels" describes, and is exact for the slab and the powder snow.
///
/// **One knowing over-admission, written down rather than hidden: ice.** Its
/// amount is 3, and `World.cpp`'s grass rule reads this predicate as "opacity
/// below 2" - deliberately, so that a block can never be dark enough to kill
/// grass yet bright enough to pass light. At 1 and 1 the slab and the powder
/// snow land on the right side of that line and grass now correctly survives
/// under both, which is the common case by a wide margin. Ice at 3 lands on the
/// wrong side, so grass under ice in the dark will now live where the reference
/// kills it. That is the price of a binary channel, it is a configuration the
/// generator does not produce - ice forms over water - and it is far cheaper
/// than the alternative, which is an ice sheet over a frozen lake stopping
/// light dead. **The real fix is a per-block amount to spend, and it lives in
/// `World.cpp`'s propagator, not here.**
constexpr bool isLightTransparent(BlockId id) {
    return id != BlockId::TintedGlass &&
           (isSkyTransparent(id) || isCutout(id) || isSlab(id) || id == BlockId::Ice ||
            id == BlockId::PowderSnow);
}

/// **The rule this replaced, kept only so the assert below can reject it.**
/// A containment test that passes whatever it is handed proves nothing, and
/// this file has already paid for eleven asserts that compared one side of a
/// derivation against itself.
constexpr bool isLightTransparentBeforeTheFilterTable(BlockId id) {
    return id != BlockId::TintedGlass && (isSkyTransparent(id) || isCutout(id));
}
static_assert(!isLightTransparentBeforeTheFilterTable(BlockId::StoneSlab) &&
                  !isLightTransparentBeforeTheFilterTable(BlockId::Ice) &&
                  !isLightTransparentBeforeTheFilterTable(BlockId::PowderSnow),
              "the three really did stop light dead before this, so the widening moved something "
              "rather than agreeing with what was already there");
static_assert(isLightTransparent(BlockId::StoneSlab) && isLightTransparent(BlockId::StoneSlabTop) &&
                  isLightTransparent(BlockId::SlabRunFirst) &&
                  isLightTransparent(BlockId::SlabRunLast) && isLightTransparent(BlockId::Ice) &&
                  isLightTransparent(BlockId::PowderSnow),
              "every slab, both halves and both runs, plus the two full cubes the reference "
              "filters through");
static_assert(!isLightTransparent(BlockId::PackedIce) && !isLightTransparent(BlockId::BlueIce),
              "only plain ice is on the reference's table - writing isIce here is the widening "
              "this file keeps paying for");
static_assert(!isLightTransparent(BlockId::CobbleStairs0) &&
                  !isLightTransparent(BlockId::StairsRunFirst) &&
                  !isLightTransparent(BlockId::StairsRunLast),
              "a stair is not on Bedrock's table; the directional opacity that tempts you to add "
              "one is Java's, and both stair runs have to stay out or only half of them does");
static_assert(!isLightTransparent(BlockId::Beacon),
              "a beacon is on the table and still stays out, because its amount is 14 and this "
              "engine can only spend 1");
static_assert(!isSkyTransparent(BlockId::StoneSlab) && !isSkyTransparent(BlockId::Ice) &&
                  !isSkyTransparent(BlockId::PowderSnow),
              "filtering is not free passage - the sky column must still break on all three, or "
              "the seabed under a frozen lake is as bright as the ice");

// **There is no sweep over the ids here, and its absence is the fix.** One
// stood here asserting `!(isSkyTransparent(id) && !isLightTransparent(id))`
// over all 3,269 of them. Substitute the derivation above and that condition
// reduces to `isSkyTransparent(id) && id == TintedGlass`, so the whole walk was
// one more spelling of the tinted-glass line below it - and the failing edit
// its own comment advertised, putting `BlockShape::Slab` on the sky list,
// *passed*, because the derivation makes the slab light-transparent in the same
// stroke. **Nothing derived from a predicate can prove anything about it**,
// which is this file's eleven self-comparing asserts wearing a loop.
//
// **That hypothetical edit is now caught, and by a named id rather than a
// walk**: `!isSkyTransparent(BlockId::StoneSlab)` above fails the moment
// anybody puts the slab on the sky list. A slab passes block light and does
// *not* pass the sky column at full strength, and those are now two separate
// statements that can each fail on their own.
//
// The containment is guarded where it can actually fail: `World.cpp` asserts it
// of an oak fence, which is sky-transparent and is *not* a cutout, so dropping
// the `isSkyTransparent(id) ||` term - the one edit that can bring the 368-id
// divergence back - stops that file compiling.
static_assert(isCutout(BlockId::TintedGlass) && !isLightTransparent(BlockId::TintedGlass) &&
                  !isSkyTransparent(BlockId::TintedGlass),
              "tinted glass is the one cutout that stops light dead - that is what it is for");
static_assert(isLightTransparent(BlockId::Leaves) && !isSkyTransparent(BlockId::Leaves),
              "and the canopy is the one gap that goes the other way");

/// The sixteen dyed wools, which several rules ask about as a family.
constexpr bool isWoolBlock(BlockId id) {
    return id >= BlockId::WhiteWool && id <= BlockId::BlackWool;
}

/// Every wood's planks. Named rather than ranged because the six runs were
/// appended at different times and are not contiguous.
constexpr bool isPlanksBlock(BlockId id) {
    return id == BlockId::Planks || id == BlockId::SprucePlanks || id == BlockId::BirchPlanks ||
           id == BlockId::JunglePlanks || id == BlockId::AcaciaPlanks ||
           id == BlockId::DarkOakPlanks || id == BlockId::CherryPlanks ||
           id == BlockId::MangrovePlanks || id == BlockId::BambooPlanks ||
           id == BlockId::BambooMosaic || id == BlockId::CrimsonPlanks ||
           id == BlockId::WarpedPlanks;
}

/// Wood cut in the nether: the two stems, the two hyphae, both of their stripped
/// forms and both planks - **and anything cut from one of them**.
///
/// **A whole branch of the wood tree that does not burn.** The reference's
/// non-flammable table gives every crimson and warped plank, slab, stair,
/// fence, gate, door, plate, sign, trapdoor, stem and hyphae "can burn away:
/// no" - which is the point of building with it. Bedrock lets one *catch* from
/// lava (MCPE-73085) and the flame still never consumes it, and we do not model
/// that distinction, so a single answer is right for both editions.
///
/// The furnace half is the same list read the other way: wiki `[[Log]]` says
/// "Stems cannot be smelted into charcoal" and "Logs, but not stems, can be
/// used as a fuel", and Bedrock's tags agree - every overworld log carries
/// `minecraft:logs_that_burn`, the two stems carry only `minecraft:logs`.
///
/// **Asked through `shapedParent`, so a crimson stair, slab, fence, gate,
/// button, plate or sign is answered by the plank it was cut from** rather than
/// by six hundred more ids - and, far more to the point, so that there is one
/// list. `Smelting.cpp` carried a second copy of the same ten ids, spelled
/// `isNetherWood`, and nothing compared them. Split them again and the day an
/// eleventh nether wood arrives only one half gets it: update this file alone
/// and the new block never burns but smelts into charcoal, which the reference
/// refuses outright; update the furnace alone and it burns away in a fire,
/// destroying the one property nether wood exists for. Neither warns.
constexpr bool isNetherWoodBlock(BlockId id) {
    const BlockId material = shapedParent(id);
    return material == BlockId::CrimsonPlanks || material == BlockId::WarpedPlanks ||
           material == BlockId::CrimsonStem || material == BlockId::WarpedStem ||
           material == BlockId::StrippedCrimsonStem || material == BlockId::StrippedWarpedStem ||
           material == BlockId::CrimsonHyphae || material == BlockId::WarpedHyphae ||
           material == BlockId::StrippedCrimsonHyphae || material == BlockId::StrippedWarpedHyphae;
}

// The index the line below reaches the crimson button by, and the oak one it is
// paired against. `kButtonFamilies` is in this file, so an inserted family moves
// both and this fires first.
static_assert(kButtonFamilies[8].parent == BlockId::CrimsonPlanks &&
                  kButtonFamilies[0].parent == BlockId::Planks,
              "the crimson button is family eight and the oak one is family zero");
// **Delete the `shapedParent` call and the middle pair fails**, which is the
// half `Smelting.cpp`'s copy of this list was written for: a crimson button is
// not one of the ten ids and must still answer yes, while an oak button made of
// the same shape must answer no.
static_assert(isNetherWoodBlock(BlockId::CrimsonPlanks) &&
                  isNetherWoodBlock(BlockId::StrippedWarpedHyphae) &&
                  isNetherWoodBlock(buttonAt(8, 0, false)) &&
                  !isNetherWoodBlock(buttonAt(0, 0, false)) &&
                  !isNetherWoodBlock(BlockId::Planks) && !isNetherWoodBlock(BlockId::Log) &&
                  !isNetherWoodBlock(BlockId::BambooBlock),
              "nether wood is a material, and a shape cut from it is still that material");

/// The plants the reference's flammable table actually names.
///
/// **Written out rather than derived from the plant shape**, which is the
/// single change that matters most in `isFlammable`. `BlockShape::Cross` began
/// as "a plant" and has since become the general shape for anything drawn on
/// two diagonals - so it now also answers for a conduit, seventeen candles, a
/// sea pickle, coral, an amethyst cluster and its three buds, a cobweb, every
/// sapling, every crop and nether wart, none of which the reference lets burn.
/// Growing the list is a deliberate act now, and that is the trade: a new
/// flower has to be added here, and a new mineral no longer catches fire on the
/// day it is defined.
constexpr bool isFlammablePlant(BlockId id) {
    // 60/100 in the reference: the dry ground cover.
    return id == BlockId::TallGrass || id == BlockId::Fern || id == BlockId::LargeFern ||
           id == BlockId::DeadBush ||
           // Every flower, both halves of the two-block ones.
           isFlower(id) || isTallFlower(id) ||
           // 60/60, and the block of bamboo is wood so `isLogBlock` has it.
           id == BlockId::Bamboo ||
           // 30/100 in Bedrock.
           id == BlockId::SweetBerryBush ||
           // 15/100 and 15/60: the things that hang.
           id == BlockId::GlowLichen || id == BlockId::CaveVines ||
           id == BlockId::CaveVinesBerries ||
           // 30/60 and 60/100: the lush cave set.
           id == BlockId::HangingRoots || id == BlockId::SporeBlossom ||
           id == BlockId::BigDripleaf || id == BlockId::SmallDripleaf ||
           id == BlockId::Azalea || id == BlockId::FloweringAzalea;
}

/// Whether fire will take hold here. The reference splits this into *ignite
/// odds* and *burn odds*; we need only the first, because ours does not model
/// how fast a block is consumed.
///
/// **The list is the reference's two tables, not a shape.** It used to be
/// mostly shape tests, and every one of them had drifted wider than the family
/// it was written for - see `isFlammablePlant` above for the worst of it, and
/// the thin-shape early-out below for the largest count.
constexpr bool isFlammable(BlockId id) {
    // Fire is not fuel: it would feed on itself and never go out. The torch is
    // the reference's own example of "never catches fire at all".
    if (id == BlockId::Fire || id == BlockId::Torch) {
        return false;
    }
    // **The three thin wooden shapes, and 672 ids of them** - 144 buttons, 224
    // pressure plates and 304 signs and banners, counted by sweeping every id
// rather than by reading this line. The three sets do not overlap: they sum to
// exactly the number that answers yes. **The 612 this said before was measured
// against a smaller enum and never re-measured**, which is the comment-rot this
// file has paid for before - a confident number in this codebase's voice is
// trusted precisely because the rest have earned it. A wooden button, a
// wooden pressure plate, a sign and a banner all sit in the reference's
    // *non-flammable* table - a plate, a sign and a banner catch from lava and
    // still never burn away, and a wooden button of any type does not even do
    // that. Every one of them forwards to its planks through `shapedParent`
    // below, so they all burned, and a lightning strike or a stray lava bucket
    // cleared out a village's signage and every button on its doors. Stairs,
    // slabs, fences and gates are *not* here: those the reference does burn.
    if (isButton(id) || isPressurePlate(id) || isSignLike(id)) {
        return false;
    }
    // **A wooden door and trapdoor need no line here, and that is worth saying
    // rather than rediscovering.** They sit in the same non-flammable table as
    // the four above, and all 384 doors and 192 trapdoors already answer no,
    // because nothing above claims them and the `shapedParent` forwarding below
    // does not reach them. Written down because "wood burns" makes the absence
    // read as an omission, and adding them would be a regression against the
    // reference rather than a fix.
    // Nether wood, before the forwarding, so a crimson stair is answered by the
    // same rule as the plank it came from rather than by a second copy of it.
    if (isNetherWoodBlock(id)) {
        return false;
    }
    // A wooden stair, slab, fence or gate burns exactly as the planks it was cut
    // from do, and a stone one does not - which is the parent's answer, not a
    // list of six hundred ids.
    const BlockId material = shapedParent(id);
    if (material != id) {
        return isFlammable(material);
    }
    // **Bark and stripped bark burn at 5/5**, the same as the log inside them,
    // and were simply never listed: `isLogBlock` covers the stripped *logs* and
    // stops there, so the twenty six-sided wood blocks were fireproof.
    return isLogBlock(id) || isBarkBlock(id) || isStrippedBarkBlock(id) || isLeafBlock(id) ||
           isPlanksBlock(id) || isWoolBlock(id) || isVine(id) || isFlammablePlant(id) ||
           // The reference's 5/20 workstations. A crafting table is deliberately
           // **not** among them - it is in the non-flammable table beside the
           // barrel, the loom and the smithing table, which is the company it
           // has always kept.
           id == BlockId::Lectern || isBeehive(id) || isComposter(id) ||
           id == BlockId::Bookshelf || id == BlockId::HayBlock || id == BlockId::CoalBlock ||
           id == BlockId::Tnt || id == BlockId::DriedKelpBlock ||
           // 60/60, and the one piece of scaffolding a player climbs.
           id == BlockId::Scaffolding;
    // Nether wart block is gone from this list: it is a nether block and the
    // reference's flammable table has never held it.
}

/// Blocks that keep a fire burning for ever. The reference's list, minus the
/// ones we have no dimension for.
constexpr bool feedsEternalFire(BlockId id) {
    return id == BlockId::Netherrack || id == BlockId::MagmaBlock || id == BlockId::SoulSand ||
           id == BlockId::SoulSoil;
}

static_assert(isFlammable(BlockId::Planks) && isFlammable(BlockId::Log) &&
                  isFlammable(BlockId::Leaves) && isFlammable(BlockId::WhiteWool) &&
                  isFlammable(BlockId::TallGrass),
              "the obvious fuels must burn");
static_assert(!isFlammable(BlockId::Stone) && !isFlammable(BlockId::Fire) &&
                  !isFlammable(BlockId::Water0),
              "fire must not feed on rock, itself or water");
// **The three thin shapes, written against the shaped families that still do
// burn.** Delete the `isButton || isPressurePlate || isSignLike` early-out and
// the first half fails; delete the `shapedParent` forwarding under it and the
// second half fails. Neither half can be satisfied by the other, which is the
// property the eleven self-comparing asserts in this file lacked.
static_assert(!isFlammable(BlockId::ButtonRunFirst) &&
                  !isFlammable(BlockId::PressurePlateRunFirst) &&
                  !isFlammable(BlockId::SignRunFirst) &&
                  !isFlammable(BlockId::HangingSignRunFirst) &&
                  !isFlammable(BlockId::BannerRunFirst) && !isFlammable(BlockId::BedRunFirst),
              "a wooden button, plate, sign, banner and bed never burn away");
static_assert(isFlammable(BlockId::PlanksFence) && isFlammable(BlockId::GateRunFirst) &&
                  isFlammable(BlockId::CarpetRunFirst) && !isFlammable(BlockId::CobbleStairs0),
              "and the shaped families the reference does burn still forward to their planks and "
              "their wool, while a stone one still does not");
// **The nether branch.** Remove `isNetherWoodBlock` from `isFlammable` and
// every one of these fails, because each of the six reaches a family predicate
// - `isPlanksBlock`, `isLogBlock`, `isBarkBlock`, `isStrippedBarkBlock` - that
// includes the nether woods. Cut shapes are covered by the same line rather
// than by a second copy of the list: `isNetherWoodBlock` forwards through
// `shapedParent` itself, which is what let `Smelting.cpp`'s duplicate of these
// ten ids be deleted.
//
// **That file's guard is not a belt, and this branch is not what excuses it.**
// Its fuel path no longer asks `isFlammable` anywhere - the general branch gates
// on `isPlanksBlock(shapedParent(block))`, and crimson and warped planks **are**
// planks blocks, so the nether test is the sole thing standing between a warped
// fence gate and the furnace. Every charcoal and fuel answer over there asks
// this predicate directly rather than keeping a copy, which is the whole point:
// fire and the furnace read the same ten ids or they drift.
static_assert(!isFlammable(BlockId::CrimsonPlanks) && !isFlammable(BlockId::WarpedPlanks) &&
                  !isFlammable(BlockId::CrimsonStem) && !isFlammable(BlockId::WarpedHyphae) &&
                  !isFlammable(BlockId::StrippedCrimsonStem) &&
                  !isFlammable(BlockId::StrippedWarpedHyphae),
              "nether wood catches from lava in Bedrock and never burns away");
static_assert(isFlammable(BlockId::OakWood) && isFlammable(BlockId::StrippedOakWood) &&
                  isFlammable(BlockId::OakWood) == isFlammable(BlockId::Log),
              "bark burns exactly as the log inside it does - the twenty six-sided wood blocks "
              "were the half of that pair nobody wrote down");
// **The shape that stopped meaning 'plant'.** Put `blockShape(id) ==
// BlockShape::Cross` back into `isFlammable` and every one of these fails.
static_assert(!isFlammable(BlockId::Conduit) && !isFlammable(BlockId::Candle) &&
                  !isFlammable(BlockId::SeaPickle) && !isFlammable(BlockId::AmethystCluster) &&
                  !isFlammable(BlockId::Cobweb) && !isFlammable(BlockId::OakSapling) &&
                  !isFlammable(BlockId::NetherWart0),
              "a mineral, a candle, a cobweb, a sapling and a crop are not kindling");
static_assert(!isFlammable(BlockId::CraftingTable) && !isFlammable(BlockId::NetherWartBlock),
              "both sat in this list and both are in the reference's non-flammable table");
static_assert(isFlammable(BlockId::Lectern) && isFlammable(BlockId::Beehive) &&
                  isFlammable(BlockId::Composter0) && isFlammable(BlockId::Scaffolding) &&
                  isFlammable(BlockId::Azalea) && isFlammable(BlockId::FloweringAzalea),
              "and these six were missing from it");

/// Which face of a block a texture is for. Most blocks use the same image on
/// every side; grass does not.
enum class BlockFace : std::uint8_t {
    Top,
    Bottom,
    Side,
};

/// Layers of the texture array, in the order the game loads the image files.
/// Adding a block type means adding its image to that list and returning the
/// new index here.
enum class TextureLayer : std::uint32_t {
    Stone = 0,
    Dirt = 1,
    GrassTop = 2,
    GrassSide = 3,
    Sand = 4,
    /// Flat white, for geometry that carries its own colour instead of a
    /// material: the targeting cage and the crosshair.
    White = 5,
    Cobblestone = 6,
    Gravel = 7,
    Snow = 8,
    Planks = 9,
    Bricks = 10,
    Glowstone = 11,
    Water = 12,
    LogSide = 13,
    LogTop = 14,
    Leaves = 15,
    /// Not a block. Shares the array because the sun is drawn with the same
    /// pipeline, and a texture array needs every layer the same size.
    Sun = 16,
    TallGrass = 17,
    /// Not a block face. The array is really "every 16x16 sprite", and putting
    /// item icons in it costs nothing where a second array would need its own
    /// binding and sampler.
    Stick = 18,
    CraftingTableTop = 19,
    CraftingTableFront = 20,
    CraftingTableSide = 21,
    FurnaceTop = 22,
    FurnaceSide = 23,
    FurnaceFront = 24,
    FurnaceFrontLit = 25,
    Charcoal = 26,
    Torch = 27,
    WoodenPickaxe = 28,
    WoodenAxe = 29,
    WoodenShovel = 30,
    WoodenSword = 31,
    WoodenHoe = 32,
    StonePickaxe = 33,
    StoneAxe = 34,
    StoneShovel = 35,
    StoneSword = 36,
    StoneHoe = 37,
    Andesite = 38,
    Diorite = 39,
    Granite = 40,
    SmoothStone = 41,
    StoneBricks = 42,
    MossyCobblestone = 43,
    Obsidian = 44,
    Clay = 45,
    SandstoneTop = 46,
    SandstoneSide = 47,
    SandstoneBottom = 48,
    Bookshelf = 49,
    Glass = 50,
    Dandelion = 51,
    Poppy = 52,
    DeadBush = 53,
    CoalOre = 54,
    IronOre = 55,
    CopperOre = 56,
    GoldOre = 57,
    RedstoneOre = 58,
    LapisOre = 59,
    DiamondOre = 60,
    EmeraldOre = 61,
    DeepslateSide = 62,
    DeepslateTop = 63,
    Bedrock = 64,
    Terracotta = 65,
    PackedIce = 66,
    /// One spawn egg per creature, **in `CreatureKind` order**, which is what
    /// lets an egg's item id, its sprite layer and the species it produces all
    /// be the same offset from their respective firsts. Only the first is
    /// named: the rest are reached by adding the species index, and a name for
    /// each would be thirty-six chances to get the order wrong.
    ///
    /// **This moves every time a block texture is added.** `Main.cpp` compares
    /// it against the loaded list at startup, because getting it wrong does not
    /// fail - it slides all thirty-six eggs by one and mistextures the lot.
    SpawnEggFirst = 67,
};

/// How many sprite layers the *first* spawn egg run occupies. Kept beside the
/// enum because the loader has to reserve exactly this many.
///
/// **Nothing asserts this number against the species count, and nothing may.**
/// The species are covered by both runs together - `Creature.hpp` holds
/// `kSpawnEggItems`, which is this plus `kExtraSpawnEggLayers`, against
/// `CreatureKind::Count`. Widening *this* one to fit a new species is the edit
/// the second run exists to prevent; see `kExtraSpawnEggLayers` below for what
/// it would slide.
constexpr int kSpawnEggLayers = 36;

/// Resource item sprites, appended after the whole egg run so adding one can
/// never shift an egg's layer.
constexpr int kResourceSpritesFirst = static_cast<int>(TextureLayer::SpawnEggFirst) + kSpawnEggLayers;
/// How many the resource run holds. `Item.hpp` takes its item count from this
/// rather than writing 11 down a second time.
constexpr int kResourceSpriteCount = 11;

/// Bucket sprites - empty, then full of water - after the resource run, for the
/// same reason that run sits after the eggs: appending can never shift a layer
/// that something already saved to disk depends on.
constexpr int kBucketSpritesFirst = kResourceSpritesFirst + kResourceSpriteCount;
constexpr int kBucketSpriteCount = 2;

/// The water surface's animation, one layer per frame, right at the end of the
/// run so adding it shifts nothing that a save already depends on.
///
/// The reference ships `water_still` as a 16x512 strip - thirty-two frames of
/// 16x16 stacked vertically - and plays it at two ticks a frame. The mesher
/// still writes `TextureLayer::Water`; the fragment shader swaps in whichever
/// of these the clock is on, so animating costs no re-meshing.
constexpr int kWaterFrameFirst = kBucketSpritesFirst + kBucketSpriteCount;
constexpr int kWaterFrames = 32;
constexpr float kWaterFrameSeconds = 0.1f;

/// Spawn eggs for species added after the first thirty-six, in `CreatureKind`
/// order continuing from where that run stopped.
///
/// **A second run rather than a wider first one**, because the resource, bucket
/// and water layers sit immediately behind the first - and the matching item
/// ids are written into the player's inventory on disk. Widening the egg run
/// would slide all of those and a saved world would come back holding the wrong
/// things. `Creature.hpp` static-asserts that the two runs together cover every
/// species.
constexpr int kExtraSpawnEggFirst = kWaterFrameFirst + kWaterFrames;
constexpr int kExtraSpawnEggLayers = 22;

/// The iron, diamond and Emberite tools, the two Emberite resources, and the
/// two blocks that come with them.
///
/// **Right at the end of the whole run**, block textures included, so not one
/// existing layer moves. A block's texture does not have to live among the
/// first sixty-seven - `blockTextureLayer` returns a layer index and nothing
/// cares where it points - and putting these there would have slid every spawn
/// egg, resource, bucket and water frame behind them.
///
/// The fifteen tools are **contiguous and in the same order as their item ids**
/// (pickaxe, axe, shovel, sword, hoe, by rising tier), so a tool's sprite is
/// arithmetic rather than fifteen cases.
constexpr int kUpgradeToolSpritesFirst = kExtraSpawnEggFirst + kExtraSpawnEggLayers;
constexpr int kUpgradeToolSprites = 15;
constexpr int kEmberiteScrapSprite = kUpgradeToolSpritesFirst + kUpgradeToolSprites;
constexpr int kEmberiteIngotSprite = kEmberiteScrapSprite + 1;
constexpr int kAncientDebrisSideSprite = kEmberiteIngotSprite + 1;
constexpr int kAncientDebrisTopSprite = kAncientDebrisSideSprite + 1;
constexpr int kEmberiteBlockSprite = kAncientDebrisTopSprite + 1;
constexpr int kSmokerFrontSprite = kEmberiteBlockSprite + 1;
constexpr int kSmokerFrontLitSprite = kSmokerFrontSprite + 1;
constexpr int kSmokerSideSprite = kSmokerFrontLitSprite + 1;
constexpr int kSmokerTopSprite = kSmokerSideSprite + 1;
constexpr int kSmithingTopSprite = kSmokerTopSprite + 1;
constexpr int kSmithingFrontSprite = kSmithingTopSprite + 1;
constexpr int kSmithingSideSprite = kSmithingFrontSprite + 1;
constexpr int kSmithingBottomSprite = kSmithingSideSprite + 1;
constexpr int kChestTopSprite = kSmithingBottomSprite + 1;
constexpr int kChestFrontSprite = kChestTopSprite + 1;
constexpr int kChestSideSprite = kChestFrontSprite + 1;

/// Food sprites, **one contiguous run in `ItemId` order** so an item's icon is
/// arithmetic rather than eleven cases. Raw and cooked alternate in pairs after
/// the apple, which is what lets the smelting table be written as a loop.
constexpr int kFoodSpritesFirst = kChestSideSprite + 1;
constexpr int kFoodSprites = 11;

/// The three appended blocks, one layer each: every one is the same image on
/// all six faces, which is why they cost a single layer where grass costs three.
constexpr int kExtraBlockSpritesFirst = kFoodSpritesFirst + kFoodSprites;
constexpr int kPrismarineSprite = kExtraBlockSpritesFirst;
constexpr int kSeaLanternSprite = kPrismarineSprite + 1;
constexpr int kCoarseDirtSprite = kSeaLanternSprite + 1;
constexpr int kExtraBlockSprites = 3;

/// Everything about one of the table-driven blocks, in the one place that owns
/// it. Indexed by `id - kFirstExtraBlock`, so **this array's order must match
/// the enum run exactly**; nothing else does, and a mismatch would silently give
/// a block someone else's name and texture.
///
/// `topLayer` is -1 for the overwhelming majority, which use the same image on
/// every face. Only logs differ, and only on the two end caps.
struct ExtraBlockInfo {
    const char* name;
    /// Offset into the appended texture run.
    int layer;
    int topLayer = -1;
    /// Belongs under Nature rather than Construction. A flag rather than a
    /// range test, because the run is no longer sorted by tab.
    bool natural = false;
};

/// Where the table-driven textures begin, after the three hand-placed ones.
constexpr int kTableSpritesFirst = kExtraBlockSpritesFirst + kExtraBlockSprites;

/// **Designated initialisers, not positional values.** The table was a wall of
/// bare numbers until a fourth column was needed; at that point setting the new
/// field on an old row would have meant restating everything before it, which
/// is the highest-risk edit there is. Fields must appear in declaration order,
/// so the struct above is the checklist.
constexpr std::array<ExtraBlockInfo, 580> kExtraBlocks{{
    {.name = "Cobbled Deepslate", .layer = 0},
    {.name = "Ice", .layer = 1, .natural = true},
    {.name = "Blue Ice", .layer = 2, .natural = true},

    {.name = "Block of Coal", .layer = 3},
    {.name = "Block of Iron", .layer = 4},
    {.name = "Block of Gold", .layer = 5},
    {.name = "Block of Diamond", .layer = 6},
    {.name = "Block of Emerald", .layer = 7},
    {.name = "Block of Lapis Lazuli", .layer = 8},
    {.name = "Block of Redstone", .layer = 9},
    {.name = "Block of Copper", .layer = 10},

    {.name = "Polished Andesite", .layer = 11},
    {.name = "Polished Diorite", .layer = 12},
    {.name = "Polished Granite", .layer = 13},
    {.name = "Chiseled Stone Bricks", .layer = 14},
    {.name = "Mossy Stone Bricks", .layer = 15},
    {.name = "Cracked Stone Bricks", .layer = 16},
    {.name = "Polished Deepslate", .layer = 17},
    {.name = "Deepslate Bricks", .layer = 18},
    {.name = "Deepslate Tiles", .layer = 19},

    {.name = "Smooth Sandstone", .layer = 20},
    {.name = "Cut Sandstone", .layer = 21},
    {.name = "Chiseled Sandstone", .layer = 22},

    {.name = "Tube Coral Block", .layer = 23, .natural = true},
    {.name = "Brain Coral Block", .layer = 24, .natural = true},
    {.name = "Bubble Coral Block", .layer = 25, .natural = true},
    {.name = "Fire Coral Block", .layer = 26, .natural = true},
    {.name = "Horn Coral Block", .layer = 27, .natural = true},
    {.name = "Sponge", .layer = 28, .natural = true},
    {.name = "Wet Sponge", .layer = 29, .natural = true},
    {.name = "Dark Prismarine", .layer = 30},
    {.name = "Prismarine Bricks", .layer = 31},

    {.name = "Spruce Log", .layer = 32, .topLayer = 33, .natural = true},
    {.name = "Spruce Leaves", .layer = 34, .natural = true},
    {.name = "Spruce Planks", .layer = 35},
    {.name = "Birch Log", .layer = 36, .topLayer = 37, .natural = true},
    {.name = "Birch Leaves", .layer = 38, .natural = true},
    {.name = "Birch Planks", .layer = 39},

    {.name = "Cornflower", .layer = 40, .natural = true},
    {.name = "Oxeye Daisy", .layer = 41, .natural = true},
    {.name = "Azure Bluet", .layer = 42, .natural = true},
    {.name = "Allium", .layer = 43, .natural = true},
    {.name = "Red Tulip", .layer = 44, .natural = true},
    {.name = "Orange Tulip", .layer = 45, .natural = true},
    {.name = "Brown Mushroom", .layer = 46, .natural = true},
    {.name = "Red Mushroom", .layer = 47, .natural = true},
    {.name = "Kelp", .layer = 48, .natural = true},
    {.name = "Seagrass", .layer = 49, .natural = true},

    {.name = "White Wool", .layer = 50},
    {.name = "Orange Wool", .layer = 51},
    {.name = "Magenta Wool", .layer = 52},
    {.name = "Light Blue Wool", .layer = 53},
    {.name = "Yellow Wool", .layer = 54},
    {.name = "Lime Wool", .layer = 55},
    {.name = "Pink Wool", .layer = 56},
    {.name = "Gray Wool", .layer = 57},
    {.name = "Light Gray Wool", .layer = 58},
    {.name = "Cyan Wool", .layer = 59},
    {.name = "Purple Wool", .layer = 60},
    {.name = "Blue Wool", .layer = 61},
    {.name = "Brown Wool", .layer = 62},
    {.name = "Green Wool", .layer = 63},
    {.name = "Red Wool", .layer = 64},
    {.name = "Black Wool", .layer = 65},

    {.name = "White Concrete", .layer = 66},
    {.name = "Orange Concrete", .layer = 67},
    {.name = "Magenta Concrete", .layer = 68},
    {.name = "Light Blue Concrete", .layer = 69},
    {.name = "Yellow Concrete", .layer = 70},
    {.name = "Lime Concrete", .layer = 71},
    {.name = "Pink Concrete", .layer = 72},
    {.name = "Gray Concrete", .layer = 73},
    {.name = "Light Gray Concrete", .layer = 74},
    {.name = "Cyan Concrete", .layer = 75},
    {.name = "Purple Concrete", .layer = 76},
    {.name = "Blue Concrete", .layer = 77},
    {.name = "Brown Concrete", .layer = 78},
    {.name = "Green Concrete", .layer = 79},
    {.name = "Red Concrete", .layer = 80},
    {.name = "Black Concrete", .layer = 81},

    {.name = "White Terracotta", .layer = 82},
    {.name = "Orange Terracotta", .layer = 83},
    {.name = "Magenta Terracotta", .layer = 84},
    {.name = "Light Blue Terracotta", .layer = 85},
    {.name = "Yellow Terracotta", .layer = 86},
    {.name = "Lime Terracotta", .layer = 87},
    {.name = "Pink Terracotta", .layer = 88},
    {.name = "Gray Terracotta", .layer = 89},
    {.name = "Light Gray Terracotta", .layer = 90},
    {.name = "Cyan Terracotta", .layer = 91},
    {.name = "Purple Terracotta", .layer = 92},
    {.name = "Blue Terracotta", .layer = 93},
    {.name = "Brown Terracotta", .layer = 94},
    {.name = "Green Terracotta", .layer = 95},
    {.name = "Red Terracotta", .layer = 96},
    {.name = "Black Terracotta", .layer = 97},

    {.name = "Deepslate Coal Ore", .layer = 98, .natural = true},
    {.name = "Deepslate Iron Ore", .layer = 99, .natural = true},
    {.name = "Deepslate Copper Ore", .layer = 100, .natural = true},
    {.name = "Deepslate Gold Ore", .layer = 101, .natural = true},
    {.name = "Deepslate Redstone Ore", .layer = 102, .natural = true},
    {.name = "Deepslate Lapis Ore", .layer = 103, .natural = true},
    {.name = "Deepslate Diamond Ore", .layer = 104, .natural = true},
    {.name = "Deepslate Emerald Ore", .layer = 105, .natural = true},

    {.name = "Jungle Log", .layer = 106, .topLayer = 107, .natural = true},
    {.name = "Jungle Leaves", .layer = 108, .natural = true},
    {.name = "Jungle Planks", .layer = 109},
    {.name = "Acacia Log", .layer = 110, .topLayer = 111, .natural = true},
    {.name = "Acacia Leaves", .layer = 112, .natural = true},
    {.name = "Acacia Planks", .layer = 113},
    {.name = "Dark Oak Log", .layer = 114, .topLayer = 115, .natural = true},
    {.name = "Dark Oak Leaves", .layer = 116, .natural = true},
    {.name = "Dark Oak Planks", .layer = 117},
    {.name = "Cherry Log", .layer = 118, .topLayer = 119, .natural = true},
    {.name = "Cherry Leaves", .layer = 120, .natural = true},
    {.name = "Cherry Planks", .layer = 121},

    {.name = "Stripped Oak Log", .layer = 122, .topLayer = 123, .natural = true},
    {.name = "Stripped Spruce Log", .layer = 124, .topLayer = 125, .natural = true},
    {.name = "Stripped Birch Log", .layer = 126, .topLayer = 127, .natural = true},
    {.name = "Stripped Jungle Log", .layer = 128, .topLayer = 129, .natural = true},
    {.name = "Stripped Acacia Log", .layer = 130, .topLayer = 131, .natural = true},
    {.name = "Stripped Dark Oak Log", .layer = 132, .topLayer = 133, .natural = true},

    {.name = "Tuff", .layer = 134, .natural = true},
    {.name = "Calcite", .layer = 135, .natural = true},
    {.name = "Dripstone Block", .layer = 136, .natural = true},
    {.name = "Moss Block", .layer = 137, .natural = true},
    {.name = "Mud", .layer = 138, .natural = true},
    {.name = "Packed Mud", .layer = 139},
    {.name = "Mud Bricks", .layer = 140},
    {.name = "Rooted Dirt", .layer = 141, .natural = true},
    {.name = "Block of Amethyst", .layer = 142, .natural = true},
    {.name = "Smooth Basalt", .layer = 143, .natural = true},
    {.name = "Basalt", .layer = 144, .topLayer = 145, .natural = true},
    {.name = "Magma Block", .layer = 146, .natural = true},
    {.name = "Honeycomb Block", .layer = 147},
    {.name = "Honey Block", .layer = 148, .topLayer = 149},

    {.name = "Red Sandstone", .layer = 150, .topLayer = 151},
    {.name = "Cut Red Sandstone", .layer = 152},
    {.name = "Chiseled Red Sandstone", .layer = 153},

    {.name = "Pumpkin", .layer = 154, .topLayer = 155, .natural = true},
    {.name = "Melon", .layer = 156, .topLayer = 157, .natural = true},
    {.name = "Hay Bale", .layer = 158, .topLayer = 159, .natural = true},
    {.name = "Note Block", .layer = 160},
    {.name = "Jukebox", .layer = 161, .topLayer = 162},

    {.name = "Blue Orchid", .layer = 163, .natural = true},
    {.name = "Pink Tulip", .layer = 164, .natural = true},
    {.name = "White Tulip", .layer = 165, .natural = true},
    {.name = "Lily of the Valley", .layer = 166, .natural = true},
    {.name = "Oak Sapling", .layer = 167, .natural = true},
    {.name = "Spruce Sapling", .layer = 168, .natural = true},
    {.name = "Birch Sapling", .layer = 169, .natural = true},
    {.name = "Jungle Sapling", .layer = 170, .natural = true},
    {.name = "Acacia Sapling", .layer = 171, .natural = true},
    {.name = "Dark Oak Sapling", .layer = 172, .natural = true},
    {.name = "Fern", .layer = 173, .natural = true},
    {.name = "Sugar Cane", .layer = 174, .natural = true},
    {.name = "Cobweb", .layer = 175, .natural = true},

    // --- The second run. Ids continue after the hive; layers continue here. ---
    {.name = "Netherrack", .layer = 176, .natural = true},
    {.name = "Soul Sand", .layer = 177, .natural = true},
    {.name = "Soul Soil", .layer = 178, .natural = true},
    {.name = "Blackstone", .layer = 179, .topLayer = 180, .natural = true},
    {.name = "Polished Blackstone", .layer = 181},
    {.name = "Polished Blackstone Bricks", .layer = 182},
    {.name = "Chiseled Polished Blackstone", .layer = 183},
    {.name = "Cracked Polished Blackstone Bricks", .layer = 184},
    {.name = "Gilded Blackstone", .layer = 185, .natural = true},
    {.name = "Nether Bricks", .layer = 186},
    {.name = "Red Nether Bricks", .layer = 187},
    {.name = "Cracked Nether Bricks", .layer = 188},
    {.name = "Chiseled Nether Bricks", .layer = 189},
    {.name = "Nether Gold Ore", .layer = 190, .natural = true},
    {.name = "Nether Quartz Ore", .layer = 191, .natural = true},
    {.name = "Block of Quartz", .layer = 192, .topLayer = 193},
    {.name = "Smooth Quartz", .layer = 194},
    {.name = "Chiseled Quartz Block", .layer = 195, .topLayer = 196},
    {.name = "Quartz Bricks", .layer = 197},
    {.name = "End Stone", .layer = 198, .natural = true},
    {.name = "End Stone Bricks", .layer = 199},
    {.name = "Purpur Block", .layer = 200},
    {.name = "Podzol", .layer = 201, .topLayer = 202, .natural = true},
    {.name = "Mycelium", .layer = 203, .topLayer = 204, .natural = true},
    {.name = "Dried Kelp Block", .layer = 205, .topLayer = 206, .natural = true},
    {.name = "Slime Block", .layer = 207},
    {.name = "Sculk", .layer = 208, .natural = true},
    {.name = "Budding Amethyst", .layer = 209, .natural = true},
    {.name = "Polished Tuff", .layer = 210},
    {.name = "Tuff Bricks", .layer = 211},
    {.name = "Chiseled Tuff", .layer = 212},
    {.name = "Polished Basalt", .layer = 213, .topLayer = 214, .natural = true},
    {.name = "Block of Raw Iron", .layer = 215},
    {.name = "Block of Raw Gold", .layer = 216},
    {.name = "Block of Raw Copper", .layer = 217},
    {.name = "Exposed Copper", .layer = 218},
    {.name = "Weathered Copper", .layer = 219},
    {.name = "Oxidized Copper", .layer = 220},
    {.name = "Cut Copper", .layer = 221},
    {.name = "Exposed Cut Copper", .layer = 222},
    {.name = "Weathered Cut Copper", .layer = 223},
    {.name = "Oxidized Cut Copper", .layer = 224},
    {.name = "Chiseled Copper", .layer = 225},
    {.name = "Reinforced Deepslate", .layer = 226, .topLayer = 227},
    {.name = "Chiseled Deepslate", .layer = 228},
    {.name = "Cracked Deepslate Bricks", .layer = 229},
    {.name = "Cracked Deepslate Tiles", .layer = 230},
    {.name = "Smooth Red Sandstone", .layer = 231},
    {.name = "Nether Wart Block", .layer = 232, .natural = true},
    {.name = "White Concrete Powder", .layer = 233},
    {.name = "Orange Concrete Powder", .layer = 234},
    {.name = "Magenta Concrete Powder", .layer = 235},
    {.name = "Light Blue Concrete Powder", .layer = 236},
    {.name = "Yellow Concrete Powder", .layer = 237},
    {.name = "Lime Concrete Powder", .layer = 238},
    {.name = "Pink Concrete Powder", .layer = 239},
    {.name = "Gray Concrete Powder", .layer = 240},
    {.name = "Light Gray Concrete Powder", .layer = 241},
    {.name = "Cyan Concrete Powder", .layer = 242},
    {.name = "Purple Concrete Powder", .layer = 243},
    {.name = "Blue Concrete Powder", .layer = 244},
    {.name = "Brown Concrete Powder", .layer = 245},
    {.name = "Green Concrete Powder", .layer = 246},
    {.name = "Red Concrete Powder", .layer = 247},
    {.name = "Black Concrete Powder", .layer = 248},

    // --- The third run. Plants first, matching the enum. ---
    {.name = "Bamboo", .layer = 251, .natural = true},
    {.name = "Sweet Berry Bush", .layer = 252, .natural = true},
    {.name = "Glow Lichen", .layer = 253, .natural = true},
    {.name = "Pointed Dripstone", .layer = 254, .natural = true},
    {.name = "Sea Pickle", .layer = 255, .natural = true},
    {.name = "Nether Sprouts", .layer = 256, .natural = true},
    {.name = "Crimson Roots", .layer = 257, .natural = true},
    {.name = "Warped Roots", .layer = 258, .natural = true},
    {.name = "Crimson Fungus", .layer = 259, .natural = true},
    {.name = "Warped Fungus", .layer = 260, .natural = true},
    {.name = "Twisting Vines", .layer = 261, .natural = true},
    {.name = "Weeping Vines", .layer = 262, .natural = true},
    {.name = "Hanging Roots", .layer = 263, .natural = true},
    {.name = "Spore Blossom", .layer = 264, .natural = true},
    {.name = "Amethyst Cluster", .layer = 265, .natural = true},
    {.name = "Large Fern", .layer = 266, .natural = true},
    {.name = "Lily Pad", .layer = 267, .natural = true},

    {.name = "Cactus", .layer = 249, .topLayer = 250, .natural = true},
    {.name = "White Glazed Terracotta", .layer = 268},
    {.name = "Orange Glazed Terracotta", .layer = 269},
    {.name = "Magenta Glazed Terracotta", .layer = 270},
    {.name = "Light Blue Glazed Terracotta", .layer = 271},
    {.name = "Yellow Glazed Terracotta", .layer = 272},
    {.name = "Lime Glazed Terracotta", .layer = 273},
    {.name = "Pink Glazed Terracotta", .layer = 274},
    {.name = "Gray Glazed Terracotta", .layer = 275},
    {.name = "Light Gray Glazed Terracotta", .layer = 276},
    {.name = "Cyan Glazed Terracotta", .layer = 277},
    {.name = "Purple Glazed Terracotta", .layer = 278},
    {.name = "Blue Glazed Terracotta", .layer = 279},
    {.name = "Brown Glazed Terracotta", .layer = 280},
    {.name = "Green Glazed Terracotta", .layer = 281},
    {.name = "Red Glazed Terracotta", .layer = 282},
    {.name = "Black Glazed Terracotta", .layer = 283},
    {.name = "Shroomlight", .layer = 284, .natural = true},
    {.name = "Ochre Froglight", .layer = 285, .topLayer = 286},
    {.name = "Verdant Froglight", .layer = 287, .topLayer = 288},
    {.name = "Pearlescent Froglight", .layer = 289, .topLayer = 290},
    {.name = "Crimson Nylium", .layer = 291, .topLayer = 292, .natural = true},
    {.name = "Warped Nylium", .layer = 293, .topLayer = 294, .natural = true},
    {.name = "Crimson Stem", .layer = 295, .topLayer = 296, .natural = true},
    {.name = "Warped Stem", .layer = 297, .topLayer = 298, .natural = true},
    {.name = "Crimson Planks", .layer = 299},
    {.name = "Warped Planks", .layer = 300},
    {.name = "Warped Wart Block", .layer = 301, .natural = true},
    {.name = "Mangrove Log", .layer = 302, .topLayer = 303, .natural = true},
    {.name = "Mangrove Planks", .layer = 304},
    {.name = "Mangrove Leaves", .layer = 305, .natural = true},
    {.name = "Muddy Mangrove Roots", .layer = 306, .topLayer = 307, .natural = true},
    {.name = "Block of Bamboo", .layer = 308, .topLayer = 309},
    {.name = "Bamboo Planks", .layer = 310},
    {.name = "Bamboo Mosaic", .layer = 311},
    {.name = "Bone Block", .layer = 312, .topLayer = 313},
    {.name = "Quartz Pillar", .layer = 314, .topLayer = 315},
    {.name = "Purpur Pillar", .layer = 316, .topLayer = 317},
    {.name = "Target", .layer = 318, .topLayer = 319},
    {.name = "Snow Block", .layer = 320, .natural = true},
    {.name = "Sculk Catalyst", .layer = 321, .topLayer = 322, .natural = true},
    {.name = "Azalea", .layer = 323, .topLayer = 324, .natural = true},
    {.name = "Flowering Azalea", .layer = 325, .topLayer = 326, .natural = true},

    {.name = "Stripped Cherry Log", .layer = 327, .topLayer = 328, .natural = true},
    {.name = "Stripped Mangrove Log", .layer = 329, .topLayer = 330, .natural = true},
    {.name = "Stripped Crimson Stem", .layer = 331, .topLayer = 332, .natural = true},
    {.name = "Stripped Warped Stem", .layer = 333, .topLayer = 334, .natural = true},
    {.name = "Block of Stripped Bamboo", .layer = 335, .topLayer = 336, .natural = true},

    // The third run: the coloured glass, and the light sources that came with
    // the bow. Declared white-first like every other dyed family, so a colour
    // is one offset here too.
    {.name = "White Stained Glass", .layer = 337},
    {.name = "Orange Stained Glass", .layer = 338},
    {.name = "Magenta Stained Glass", .layer = 339},
    {.name = "Light Blue Stained Glass", .layer = 340},
    {.name = "Yellow Stained Glass", .layer = 341},
    {.name = "Lime Stained Glass", .layer = 342},
    {.name = "Pink Stained Glass", .layer = 343},
    {.name = "Gray Stained Glass", .layer = 344},
    {.name = "Light Gray Stained Glass", .layer = 345},
    {.name = "Cyan Stained Glass", .layer = 346},
    {.name = "Purple Stained Glass", .layer = 347},
    {.name = "Blue Stained Glass", .layer = 348},
    {.name = "Brown Stained Glass", .layer = 349},
    {.name = "Green Stained Glass", .layer = 350},
    {.name = "Red Stained Glass", .layer = 351},
    {.name = "Black Stained Glass", .layer = 352},
    {.name = "Iron Bars", .layer = 353},
    {.name = "Lantern", .layer = 354},
    {.name = "Soul Lantern", .layer = 355},
    {.name = "Soul Torch", .layer = 356},
    {.name = "Redstone Torch", .layer = 357},
    {.name = "End Rod", .layer = 358},

    // ---- The fourth run: the farm. ----
    // Farmland keeps dirt on its sides, which is why it is the one pair of rows
    // that shares a `layer` and differs only in `topLayer`.
    {.name = "Farmland", .layer = 359, .topLayer = 360, .natural = true},
    {.name = "Farmland", .layer = 359, .topLayer = 361, .natural = true},
    {.name = "Dirt Path", .layer = 362, .topLayer = 363, .natural = true},

    // Wheat is the one crop with a picture per age.
    {.name = "Wheat Crop", .layer = 364, .natural = true},
    {.name = "Wheat Crop", .layer = 365, .natural = true},
    {.name = "Wheat Crop", .layer = 366, .natural = true},
    {.name = "Wheat Crop", .layer = 367, .natural = true},
    {.name = "Wheat Crop", .layer = 368, .natural = true},
    {.name = "Wheat Crop", .layer = 369, .natural = true},
    {.name = "Wheat Crop", .layer = 370, .natural = true},
    {.name = "Wheat Crop", .layer = 371, .natural = true},

    // Four pictures over eight ages: 0-1, 2-3, 4-6, 7. The repeats below are
    // that mapping, written where the texture is chosen rather than in a rule
    // somewhere else that could disagree with it.
    {.name = "Carrot Crop", .layer = 372, .natural = true},
    {.name = "Carrot Crop", .layer = 372, .natural = true},
    {.name = "Carrot Crop", .layer = 373, .natural = true},
    {.name = "Carrot Crop", .layer = 373, .natural = true},
    {.name = "Carrot Crop", .layer = 374, .natural = true},
    {.name = "Carrot Crop", .layer = 374, .natural = true},
    {.name = "Carrot Crop", .layer = 374, .natural = true},
    {.name = "Carrot Crop", .layer = 375, .natural = true},

    {.name = "Potato Crop", .layer = 376, .natural = true},
    {.name = "Potato Crop", .layer = 376, .natural = true},
    {.name = "Potato Crop", .layer = 377, .natural = true},
    {.name = "Potato Crop", .layer = 377, .natural = true},
    {.name = "Potato Crop", .layer = 378, .natural = true},
    {.name = "Potato Crop", .layer = 378, .natural = true},
    {.name = "Potato Crop", .layer = 378, .natural = true},
    {.name = "Potato Crop", .layer = 379, .natural = true},

    {.name = "Beetroot Crop", .layer = 380, .natural = true},
    {.name = "Beetroot Crop", .layer = 380, .natural = true},
    {.name = "Beetroot Crop", .layer = 381, .natural = true},
    {.name = "Beetroot Crop", .layer = 381, .natural = true},
    {.name = "Beetroot Crop", .layer = 382, .natural = true},
    {.name = "Beetroot Crop", .layer = 382, .natural = true},
    {.name = "Beetroot Crop", .layer = 382, .natural = true},
    {.name = "Beetroot Crop", .layer = 383, .natural = true},

    {.name = "Melon Stem", .layer = 384, .natural = true},
    {.name = "Melon Stem", .layer = 384, .natural = true},
    {.name = "Melon Stem", .layer = 384, .natural = true},
    {.name = "Melon Stem", .layer = 384, .natural = true},
    {.name = "Melon Stem", .layer = 384, .natural = true},
    {.name = "Melon Stem", .layer = 384, .natural = true},
    {.name = "Melon Stem", .layer = 384, .natural = true},
    {.name = "Melon Stem", .layer = 384, .natural = true},
    {.name = "Melon Stem", .layer = 385, .natural = true},
    {.name = "Melon Stem", .layer = 385, .natural = true},
    {.name = "Melon Stem", .layer = 385, .natural = true},
    {.name = "Melon Stem", .layer = 385, .natural = true},

    {.name = "Pumpkin Stem", .layer = 386, .natural = true},
    {.name = "Pumpkin Stem", .layer = 386, .natural = true},
    {.name = "Pumpkin Stem", .layer = 386, .natural = true},
    {.name = "Pumpkin Stem", .layer = 386, .natural = true},
    {.name = "Pumpkin Stem", .layer = 386, .natural = true},
    {.name = "Pumpkin Stem", .layer = 386, .natural = true},
    {.name = "Pumpkin Stem", .layer = 386, .natural = true},
    {.name = "Pumpkin Stem", .layer = 386, .natural = true},
    {.name = "Pumpkin Stem", .layer = 387, .natural = true},
    {.name = "Pumpkin Stem", .layer = 387, .natural = true},
    {.name = "Pumpkin Stem", .layer = 387, .natural = true},
    {.name = "Pumpkin Stem", .layer = 387, .natural = true},

    {.name = "Nether Wart", .layer = 388, .natural = true},
    {.name = "Nether Wart", .layer = 389, .natural = true},
    {.name = "Nether Wart", .layer = 390, .natural = true},
    {.name = "Nether Wart", .layer = 390, .natural = true},

    // Four facings each. The carved face itself is chosen in
    // `blockTextureLayer`, because a table row has no column for a front.
    {.name = "Carved Pumpkin", .layer = 391, .topLayer = 392},
    {.name = "Carved Pumpkin", .layer = 391, .topLayer = 392},
    {.name = "Carved Pumpkin", .layer = 391, .topLayer = 392},
    {.name = "Carved Pumpkin", .layer = 391, .topLayer = 392},
    {.name = "Jack o'Lantern", .layer = 391, .topLayer = 392},
    {.name = "Jack o'Lantern", .layer = 391, .topLayer = 392},
    {.name = "Jack o'Lantern", .layer = 391, .topLayer = 392},
    {.name = "Jack o'Lantern", .layer = 391, .topLayer = 392},

    // Levels 0-7 wear the plain lid; level 8 is the reference's separate
    // "ready" picture, which is the whole visual cue that there is bone meal
    // waiting in it.
    {.name = "Composter", .layer = 396, .topLayer = 395},
    {.name = "Composter", .layer = 396, .topLayer = 395},
    {.name = "Composter", .layer = 396, .topLayer = 395},
    {.name = "Composter", .layer = 396, .topLayer = 395},
    {.name = "Composter", .layer = 396, .topLayer = 395},
    {.name = "Composter", .layer = 396, .topLayer = 395},
    {.name = "Composter", .layer = 396, .topLayer = 395},
    {.name = "Composter", .layer = 396, .topLayer = 395},
    {.name = "Composter", .layer = 396, .topLayer = 397},

    // ---- The fifth run: bark, coral, copper and the decoratives. ----
    // Bark blocks point straight at the log sides their own family already
    // staged, so twenty blocks arrive here with **no new art at all**.
    {.name = "Oak Wood", .layer = 398, .natural = true},
    {.name = "Spruce Wood", .layer = 399, .natural = true},
    {.name = "Birch Wood", .layer = 400, .natural = true},
    {.name = "Jungle Wood", .layer = 401, .natural = true},
    {.name = "Acacia Wood", .layer = 402, .natural = true},
    {.name = "Dark Oak Wood", .layer = 403, .natural = true},
    {.name = "Cherry Wood", .layer = 404, .natural = true},
    {.name = "Mangrove Wood", .layer = 405, .natural = true},
    {.name = "Crimson Hyphae", .layer = 406, .natural = true},
    {.name = "Warped Hyphae", .layer = 407, .natural = true},
    {.name = "Stripped Oak Wood", .layer = 408, .natural = true},
    {.name = "Stripped Spruce Wood", .layer = 409, .natural = true},
    {.name = "Stripped Birch Wood", .layer = 410, .natural = true},
    {.name = "Stripped Jungle Wood", .layer = 411, .natural = true},
    {.name = "Stripped Acacia Wood", .layer = 412, .natural = true},
    {.name = "Stripped Dark Oak Wood", .layer = 413, .natural = true},
    {.name = "Stripped Cherry Wood", .layer = 414, .natural = true},
    {.name = "Stripped Mangrove Wood", .layer = 415, .natural = true},
    {.name = "Stripped Crimson Hyphae", .layer = 416, .natural = true},
    {.name = "Stripped Warped Hyphae", .layer = 417, .natural = true},

    {.name = "Brown Mushroom Block", .layer = 418, .natural = true},
    {.name = "Red Mushroom Block", .layer = 419, .natural = true},
    {.name = "Mushroom Stem", .layer = 420, .natural = true},

    {.name = "Dead Tube Coral Block", .layer = 421, .natural = true},
    {.name = "Dead Brain Coral Block", .layer = 422, .natural = true},
    {.name = "Dead Bubble Coral Block", .layer = 423, .natural = true},
    {.name = "Dead Fire Coral Block", .layer = 424, .natural = true},
    {.name = "Dead Horn Coral Block", .layer = 425, .natural = true},

    // Waxed copper wears the same face as unwaxed copper - wax is a promise,
    // not a picture - so these nine reuse the run's existing layers.
    {.name = "Waxed Block of Copper", .layer = 426},
    {.name = "Waxed Exposed Copper", .layer = 427},
    {.name = "Waxed Weathered Copper", .layer = 428},
    {.name = "Waxed Oxidized Copper", .layer = 429},
    {.name = "Waxed Cut Copper", .layer = 430},
    {.name = "Waxed Exposed Cut Copper", .layer = 431},
    {.name = "Waxed Weathered Cut Copper", .layer = 432},
    {.name = "Waxed Oxidized Cut Copper", .layer = 433},
    {.name = "Waxed Chiseled Copper", .layer = 434},

    {.name = "Copper Grate", .layer = 435},
    {.name = "Exposed Copper Grate", .layer = 436},
    {.name = "Weathered Copper Grate", .layer = 437},
    {.name = "Oxidized Copper Grate", .layer = 438},
    {.name = "Waxed Copper Grate", .layer = 435},
    {.name = "Waxed Exposed Copper Grate", .layer = 436},
    {.name = "Waxed Weathered Copper Grate", .layer = 437},
    {.name = "Waxed Oxidized Copper Grate", .layer = 438},

    {.name = "Copper Bulb", .layer = 439},
    {.name = "Copper Bulb", .layer = 440},
    {.name = "Exposed Copper Bulb", .layer = 441},
    {.name = "Exposed Copper Bulb", .layer = 442},
    {.name = "Weathered Copper Bulb", .layer = 443},
    {.name = "Weathered Copper Bulb", .layer = 444},
    {.name = "Oxidized Copper Bulb", .layer = 445},
    {.name = "Oxidized Copper Bulb", .layer = 446},

    {.name = "Crying Obsidian", .layer = 447},
    {.name = "Powder Snow", .layer = 448, .natural = true},
    {.name = "Suspicious Sand", .layer = 449, .natural = true},
    {.name = "Suspicious Gravel", .layer = 450, .natural = true},
    {.name = "Azalea Leaves", .layer = 451, .natural = true},
    {.name = "Flowering Azalea Leaves", .layer = 452, .natural = true},
    {.name = "Redstone Lamp", .layer = 453},
    {.name = "Redstone Lamp", .layer = 454},
    {.name = "Lodestone", .layer = 455, .topLayer = 456},
    {.name = "Enchanting Table", .layer = 457, .topLayer = 458},
    {.name = "Chiseled Bookshelf", .layer = 459, .topLayer = 460},
    {.name = "Cartography Table", .layer = 461, .topLayer = 462},
    {.name = "Fletching Table", .layer = 463, .topLayer = 464},
    {.name = "Barrel", .layer = 465, .topLayer = 466},
    {.name = "Blast Furnace", .layer = 467, .topLayer = 468},
    {.name = "Loom", .layer = 469, .topLayer = 470},
    {.name = "Stonecutter", .layer = 471, .topLayer = 472},
    {.name = "Grindstone", .layer = 473, .topLayer = 474},
    {.name = "Lectern", .layer = 475, .topLayer = 476},
    {.name = "Bell", .layer = 477, .topLayer = 478},
    {.name = "Cauldron", .layer = 479, .topLayer = 480},
    {.name = "Brewing Stand", .layer = 481, .topLayer = 482},
    {.name = "Anvil", .layer = 483, .topLayer = 484},
    {.name = "Chipped Anvil", .layer = 483, .topLayer = 485},
    {.name = "Damaged Anvil", .layer = 483, .topLayer = 486},
    {.name = "Scaffolding", .layer = 487, .topLayer = 488},
    {.name = "Flower Pot", .layer = 489, .natural = true},

    {.name = "Sculk Vein", .layer = 490, .natural = true},
    {.name = "Sculk Sensor", .layer = 491, .topLayer = 492, .natural = true},
    {.name = "Sculk Shrieker", .layer = 493, .topLayer = 494, .natural = true},
    {.name = "Small Amethyst Bud", .layer = 495, .natural = true},
    {.name = "Medium Amethyst Bud", .layer = 496, .natural = true},
    {.name = "Large Amethyst Bud", .layer = 497, .natural = true},
    {.name = "Big Dripleaf", .layer = 498, .natural = true},
    {.name = "Small Dripleaf", .layer = 499, .natural = true},
    {.name = "Cave Vines", .layer = 500, .natural = true},
    {.name = "Glow Berries", .layer = 501, .natural = true},
    {.name = "Moss Carpet", .layer = 502, .natural = true},
    {.name = "Chorus Plant", .layer = 503, .natural = true},
    {.name = "Chorus Flower", .layer = 504, .natural = true},

    {.name = "Tube Coral", .layer = 505, .natural = true},
    {.name = "Brain Coral", .layer = 506, .natural = true},
    {.name = "Bubble Coral", .layer = 507, .natural = true},
    {.name = "Fire Coral", .layer = 508, .natural = true},
    {.name = "Horn Coral", .layer = 509, .natural = true},
    {.name = "Dead Tube Coral", .layer = 510, .natural = true},
    {.name = "Dead Brain Coral", .layer = 511, .natural = true},
    {.name = "Dead Bubble Coral", .layer = 512, .natural = true},
    {.name = "Dead Fire Coral", .layer = 513, .natural = true},
    {.name = "Dead Horn Coral", .layer = 514, .natural = true},
    {.name = "Tube Coral Fan", .layer = 515, .natural = true},
    {.name = "Brain Coral Fan", .layer = 516, .natural = true},
    {.name = "Bubble Coral Fan", .layer = 517, .natural = true},
    {.name = "Fire Coral Fan", .layer = 518, .natural = true},
    {.name = "Horn Coral Fan", .layer = 519, .natural = true},
    {.name = "Dead Tube Coral Fan", .layer = 520, .natural = true},
    {.name = "Dead Brain Coral Fan", .layer = 521, .natural = true},
    {.name = "Dead Bubble Coral Fan", .layer = 522, .natural = true},
    {.name = "Dead Fire Coral Fan", .layer = 523, .natural = true},
    {.name = "Dead Horn Coral Fan", .layer = 524, .natural = true},

    {.name = "Sunflower", .layer = 525, .natural = true},
    {.name = "Sunflower", .layer = 526, .natural = true},
    {.name = "Lilac", .layer = 527, .natural = true},
    {.name = "Lilac", .layer = 528, .natural = true},
    {.name = "Rose Bush", .layer = 529, .natural = true},
    {.name = "Rose Bush", .layer = 530, .natural = true},
    {.name = "Peony", .layer = 531, .natural = true},
    {.name = "Peony", .layer = 532, .natural = true},
    {.name = "Wither Rose", .layer = 533, .natural = true},

    {.name = "Campfire", .layer = 534, .topLayer = 535},
    {.name = "Soul Campfire", .layer = 534, .topLayer = 536},
    {.name = "Respawn Anchor", .layer = 537, .topLayer = 538},

    // ---- The sixth run: the candles, and the last few oddments. ----
    {.name = "Candle", .layer = 539},
    {.name = "White Candle", .layer = 540},
    {.name = "Orange Candle", .layer = 541},
    {.name = "Magenta Candle", .layer = 542},
    {.name = "Light Blue Candle", .layer = 543},
    {.name = "Yellow Candle", .layer = 544},
    {.name = "Lime Candle", .layer = 545},
    {.name = "Pink Candle", .layer = 546},
    {.name = "Gray Candle", .layer = 547},
    {.name = "Light Gray Candle", .layer = 548},
    {.name = "Cyan Candle", .layer = 549},
    {.name = "Purple Candle", .layer = 550},
    {.name = "Blue Candle", .layer = 551},
    {.name = "Brown Candle", .layer = 552},
    {.name = "Green Candle", .layer = 553},
    {.name = "Red Candle", .layer = 554},
    {.name = "Black Candle", .layer = 555},
    {.name = "Tinted Glass", .layer = 556},
    {.name = "Beacon", .layer = 557},
    {.name = "Conduit", .layer = 558},
    {.name = "Dragon Egg", .layer = 559, .natural = true},
    {.name = "End Portal Frame", .layer = 560, .topLayer = 561},
    {.name = "Monster Spawner", .layer = 562},
    {.name = "Trapped Chest", .layer = 563, .topLayer = 564},
    {.name = "Trapped Chest", .layer = 563, .topLayer = 564},
    {.name = "Trapped Chest", .layer = 563, .topLayer = 564},
    {.name = "Trapped Chest", .layer = 563, .topLayer = 564},
    // The blast furnace's other seven states, sharing the row the first one
    // already has: the mouth is chosen in `blockTextureLayer`, not here.
    {.name = "Blast Furnace", .layer = 467, .topLayer = 468},
    {.name = "Blast Furnace", .layer = 467, .topLayer = 468},
    {.name = "Blast Furnace", .layer = 467, .topLayer = 468},
    {.name = "Blast Furnace", .layer = 467, .topLayer = 468},
    {.name = "Blast Furnace", .layer = 467, .topLayer = 468},
    {.name = "Blast Furnace", .layer = 467, .topLayer = 468},
    {.name = "Blast Furnace", .layer = 467, .topLayer = 468},
    {.name = "Ender Chest", .layer = 685, .topLayer = 686},
    {.name = "Ender Chest", .layer = 685, .topLayer = 686},
    {.name = "Ender Chest", .layer = 685, .topLayer = 686},
    {.name = "Ender Chest", .layer = 685, .topLayer = 686},
    // The cauldron's six filled levels share the empty one's row: how full it
    // is shows in the lid, which the model does not draw yet.
    {.name = "Cauldron", .layer = 479, .topLayer = 480},
    {.name = "Cauldron", .layer = 479, .topLayer = 480},
    {.name = "Cauldron", .layer = 479, .topLayer = 480},
    {.name = "Cauldron", .layer = 479, .topLayer = 480},
    {.name = "Cauldron", .layer = 479, .topLayer = 480},
    {.name = "Cauldron", .layer = 479, .topLayer = 480},
    // Five spouts, one row each. Which way it points is not in the texture -
    // the reference's hopper model is the same funnel whichever way the spout
    // hangs, and only the spout itself moves.
    {.name = "Hopper", .layer = 687, .topLayer = 688},
    {.name = "Hopper", .layer = 687, .topLayer = 688},
    {.name = "Hopper", .layer = 687, .topLayer = 688},
    {.name = "Hopper", .layer = 687, .topLayer = 688},
    {.name = "Hopper", .layer = 687, .topLayer = 688},
    // Seventeen stowboxes, one sprite each on every face. Plain first, then
    // the sixteen dyes in the order every other dyed family here uses.
    {.name = "Stowbox", .layer = 689, .topLayer = 689},
    {.name = "White Stowbox", .layer = 690, .topLayer = 690},
    {.name = "Orange Stowbox", .layer = 691, .topLayer = 691},
    {.name = "Magenta Stowbox", .layer = 692, .topLayer = 692},
    {.name = "Light Blue Stowbox", .layer = 693, .topLayer = 693},
    {.name = "Yellow Stowbox", .layer = 694, .topLayer = 694},
    {.name = "Lime Stowbox", .layer = 695, .topLayer = 695},
    {.name = "Pink Stowbox", .layer = 696, .topLayer = 696},
    {.name = "Gray Stowbox", .layer = 697, .topLayer = 697},
    {.name = "Light Gray Stowbox", .layer = 698, .topLayer = 698},
    {.name = "Cyan Stowbox", .layer = 699, .topLayer = 699},
    {.name = "Purple Stowbox", .layer = 700, .topLayer = 700},
    {.name = "Blue Stowbox", .layer = 701, .topLayer = 701},
    {.name = "Brown Stowbox", .layer = 702, .topLayer = 702},
    {.name = "Green Stowbox", .layer = 703, .topLayer = 703},
    {.name = "Red Stowbox", .layer = 704, .topLayer = 704},
    {.name = "Black Stowbox", .layer = 705, .topLayer = 705},
}};

/// **A short `std::array` initialiser is not an error - it zero-fills.** A table
/// declared `kSignFamilyCount` and given one row fewer silently gains a final row
/// whose `parent` is `BlockId::Air`, because `Air` is 0. It builds clean at `/W4`,
/// validates clean, and nothing in the game says a word: the family simply behaves
/// as though air were a material. Only a too-*long* initialiser is diagnosed.
///
/// **The tail is the right place to look**, and it is the one place that works for
/// every table at once: a short initialiser leaves the *last* element zeroed whatever
/// else it fills. Adding a row does not invalidate this - it checks the new last row -
/// so there is nothing here to keep up to date and no comment that can rot.
///
/// **Deriving each size from its own table would be better and is not available.**
/// The counts feed the enum run arithmetic - `StairsRunLast` and its siblings - which
/// must be evaluated while `BlockId` is being defined, whereas the tables name
/// `BlockId` enumerators and so can only exist afterwards. Size-from-table is
/// circular here, so the sizes stay hand-written and this assert stands in for the
/// derivation that cannot be written.
///
/// `kSignFamilies` and `kHangingSignFamilies` share one constant deliberately - they
/// carry the identical parent list (checked 2026-08-19: the same eleven planks in the
/// same order), and sharing is what makes them unable to disagree. But it also means
/// a bump made for one shortens the other, which is this defect with a name on it.
constexpr bool everyFamilyTableIsFullyWritten() {
    return kStairFamilies.back().parent != BlockId::Air
        && kSlabFamilies.back().parent != BlockId::Air
        && kWallFamilies.back().parent != BlockId::Air
        && kFenceFamilies.back().parent != BlockId::Air
        && kGateFamilies.back().parent != BlockId::Air
        && kCarpetFamilies.back().parent != BlockId::Air
        && kPaneFamilies.back().parent != BlockId::Air
        && kButtonFamilies.back().parent != BlockId::Air
        && kPressurePlateFamilies.back().parent != BlockId::Air
        && kSignFamilies.back().parent != BlockId::Air
        && kHangingSignFamilies.back().parent != BlockId::Air
        && kBannerFamilies.back().parent != BlockId::Air
        && kDoorFamilies.back().name != nullptr
        && kTrapdoorFamilies.back().name != nullptr
        && kExtraBlocks.back().name != nullptr;
}

/// The negative control, and it is the hazard itself rather than a model of it: three
/// slots, two rows. **If a short initialiser were diagnosed this would not compile,
/// and if it did not zero-fill the assert below would fail** - so both halves of the
/// defect are proven by the same three lines, and `everyFamilyTableIsFullyWritten` is
/// shown able to *fire* rather than merely able to pass.
///
/// **It is also the calibration standard for the declared-size-against-literal-rows
/// sweep** that finds this class of bug. Run that sweep over this file and it must
/// report **exactly one** shortfall, and it must be this table. Zero means the sweep
/// is blind, and the usual cause is a pattern requiring `= {` when every table here
/// is brace-initialised with no `=` anywhere. More than one means a real table is
/// short. **So do not "fix" this table** - its shortfall is the instrument's zero.
constexpr std::array<ShapedFamily, 3> kShortInitialiserControl{{
    {BlockId::Stone, "Deliberately Short - see the comment above"},
    {BlockId::Stone, "Deliberately Short - see the comment above"},
}};
static_assert(kShortInitialiserControl.back().parent == BlockId::Air,
              "a short std::array initialiser no longer zero-fills its tail, so "
              "everyFamilyTableIsFullyWritten can no longer detect a family table "
              "missing a row; find another way to prove each table is fully written");

static_assert(everyFamilyTableIsFullyWritten(),
              "a family table holds fewer rows than the count it is declared with, so "
              "its last row is a zero-filled one whose parent is BlockId::Air; count "
              "the rows against the k...FamilyCount in the array's declaration");

/// The faces a table row cannot carry. Both pumpkins and the trapped chest are
/// ordinary rows for five of their six faces and differ only on the front, so
/// the front lives here - beside the run it indexes - rather than as a bare
/// number inside `blockTextureLayer`.
constexpr int kCarvedPumpkinFaceLayer = 393;
constexpr int kJackOLanternFaceLayer = 394;
constexpr int kTrappedChestFaceLayer = 565;
/// The blast furnace's mouth, unlit and lit.
constexpr int kBlastFurnaceFaceLayer = 566;
constexpr int kBlastFurnaceLitFaceLayer = 567;
/// The seventeen unlit candles. The lit ones are ordinary table rows at 539
/// through 555, so a colour's two pictures are one subtraction apart.
constexpr int kUnlitCandleFirstLayer = 568;
constexpr int kLitCandleFirstLayer = 539;
/// Plain plus the sixteen dyes, which is the length of both candle runs.
constexpr int kCandleColours = 17;

/// One more than the highest layer the run uses. **Derived rather than written
/// down**, because nothing else asserts it: too small and the last few blocks
/// silently sample whatever run follows this one, which is a wrong texture with
/// no error anywhere.
///
/// It has to consider the face layers above as well as the rows, because those
/// are reached only from code - and a layer nothing in the table mentions is
/// exactly the one a row-only scan would leave off the end.
///
/// **It said that and did not do it.** The seed was the bed run alone and the
/// loop reads rows, so the seven code-only constants above - the two pumpkin
/// faces, the trapped chest, the two blast furnace mouths and the seventeen
/// unlit candles, which reach 584 - were never in the maximum. It has been safe
/// only by luck, because the table's own rows happen to run out at 705; move the
/// candles to the end of the run, or trim a few rows, and the atlas would stop
/// one short of a texture `blockTextureLayer` still returns. They are folded in
/// now, so the claim and the code agree.
constexpr int maxTableLayer() {
    int highest = kBedFirstLayer + kBedColours * 4 + 1;
    const int codeOnly[] = {kCarvedPumpkinFaceLayer,   kJackOLanternFaceLayer,
                            kTrappedChestFaceLayer,    kBlastFurnaceFaceLayer,
                            kBlastFurnaceLitFaceLayer, kUnlitCandleFirstLayer + kCandleColours - 1,
                            kLitCandleFirstLayer + kCandleColours - 1};
    for (const int layer : codeOnly) {
        highest = layer > highest ? layer : highest;
    }
    for (const ExtraBlockInfo& info : kExtraBlocks) {
        highest = info.layer > highest ? info.layer : highest;
        highest = info.topLayer > highest ? info.topLayer : highest;
    }
    return highest;
}

constexpr int kTableSprites = maxTableLayer() + 1;
static_assert(kTableSprites == 706, "the table's sprite run changed size; update Main.cpp's list");
// **The code-only layers must stay inside the run they are not listed in.**
// Delete the `codeOnly` loop above and this still passes today, which is exactly
// why it is written the other way round: it fails the moment a code-only layer
// becomes the highest one, and that is the case the loop exists for.
static_assert(kUnlitCandleFirstLayer + kCandleColours <= kTableSprites &&
                  kLitCandleFirstLayer + kCandleColours <= kTableSprites &&
                  kBlastFurnaceLitFaceLayer < kTableSprites &&
                  kTrappedChestFaceLayer < kTableSprites,
              "a layer reached only from code fell off the end of the atlas run");

static_assert(kExtraBlocks.size() == static_cast<std::size_t>(kExtraRun1Count + kExtraRun2Count +
                                                             kExtraRun3Count + kExtraRun4Count +
                                                             kExtraRun5Count + kExtraRun6Count +
                                                             kExtraRun7Count),
              "kExtraBlocks must have exactly one row per id across all seven table-driven runs");

/// The double chest, three faces per half, right at the very end of the run so
/// not one existing layer moves.
///
/// **Named for the viewer's left and right, which is the opposite of Mojang's
/// own file names** - `normal_right.png` is the half a player sees on the left.
/// That was measured rather than assumed: each half paints its dark border on
/// its outer vertical edge only, and the staging tool aligns each crop so that
/// border lands on the outer edge of the block.
///
/// There is no half-specific side or bottom. The outer side rect is
/// pixel-identical to a single chest's, and the face where the two halves meet
/// is culled outright, because a chest is a solid opaque cube.
constexpr int kChestHalfSpritesFirst = kTableSpritesFirst + kTableSprites;
constexpr int kChestLeftTopSprite = kChestHalfSpritesFirst;
constexpr int kChestLeftFrontSprite = kChestLeftTopSprite + 1;
constexpr int kChestLeftBackSprite = kChestLeftFrontSprite + 1;
constexpr int kChestRightTopSprite = kChestLeftBackSprite + 1;
constexpr int kChestRightFrontSprite = kChestRightTopSprite + 1;
constexpr int kChestRightBackSprite = kChestRightFrontSprite + 1;
constexpr int kChestHalfSprites = 6;

/// The appended items - foods, materials, the sixteen dyes, the armour and the
/// rest - as one contiguous run in `ItemId` order, so an icon is arithmetic
/// rather than a case per item.
///
/// **This is the appended run's length minus the brewing run's, and that is not
/// an oddity.** `itemTextureLayer` catches `FermentedSpiderEye..DragonBreath`
/// *before* it reaches this arithmetic and answers them from `kBrewingSprites`
/// above, so the two ids at the top of the appended run take no slot here. The
/// run this length covers is `Bread` through `GoldNugget`.
///
/// **One hundred and fifty-one, not one hundred and sixty-six**, since
/// 2026-08-18: fifteen shadow music discs that duplicated the collectible run
/// were deleted from the item catalogue, and their pictures with them. This
/// number is why that deletion had to reach three files - a count left too high
/// does not fail to compile and does not fail to draw. It slides every icon
/// above the gap onto the item fifteen slots below it, which is a thing only
/// eyes catch.
constexpr int kExtraItemSpritesFirst = kChestHalfSpritesFirst + kChestHalfSprites;
constexpr int kExtraItemSprites = 151;

/// The beehive: front, its full-of-honey form, the sides and the end caps.
constexpr int kBeehiveSpritesFirst = kExtraItemSpritesFirst + kExtraItemSprites;
constexpr int kBeehiveFrontSprite = kBeehiveSpritesFirst;
constexpr int kBeehiveFrontHoneySprite = kBeehiveFrontSprite + 1;
constexpr int kBeehiveSideSprite = kBeehiveFrontHoneySprite + 1;
constexpr int kBeehiveEndSprite = kBeehiveSideSprite + 1;
constexpr int kBeehiveSprites = 4;

/// The bee nest's five faces - **which are the hive's four for now, on purpose
/// and with the way out written down.**
///
/// The reference gives the nest art of its own, and five pieces rather than
/// four: `bee_nest_front`, `bee_nest_front_honey`, `bee_nest_side`,
/// `bee_nest_top` and `bee_nest_bottom`, all five present under
/// `reference/minecraft-assets-26.2` and **none of them staged**. Staging is a
/// three-file protocol - a row in `make-reference-blocks.ps1`, a filename in
/// `Main.cpp`'s sprite list and a placeholder under `assets/` - and **a sprite
/// index naming a picture nobody loaded is a hard load failure rather than a
/// wrong picture**, so a run of five declared here alone would not be a
/// half-finished nest, it would be a game that does not start.
///
/// **Aliases rather than a hive branch inside `blockTextureLayer`.** The texture
/// function already asks the nest for its top and its bottom separately, which
/// is the only part of this that would be easy to get wrong later; closing the
/// gap is then five numbers here plus the staging, and no future reader has to
/// deduce from a shared branch that the nest was ever meant to differ.
constexpr int kBeeNestFrontSprite = kBeehiveFrontSprite;
constexpr int kBeeNestFrontHoneySprite = kBeehiveFrontHoneySprite;
constexpr int kBeeNestSideSprite = kBeehiveSideSprite;
constexpr int kBeeNestTopSprite = kBeehiveEndSprite;
constexpr int kBeeNestBottomSprite = kBeehiveEndSprite;

/// **The placeholder, stated as a fact the compiler holds rather than as a
/// comment that can rot.** The hive shares one `beehive_end` between its top and
/// its bottom; the nest's model names two different files. So the day
/// `bee_nest_top` and `bee_nest_bottom` are staged these two stop being equal
/// and this line fails - which is exactly the moment to read the note above and
/// delete both together.
static_assert(kBeeNestTopSprite == kBeeNestBottomSprite,
              "the nest is still wearing the hive's art: stage bee_nest_top and bee_nest_bottom, "
              "then delete this assert and the note above it");

/// Lava, fire and TNT. A run of its own at the very end rather than more rows
/// on the table, because none of these three is table-driven: lava carries a
/// level in its id, fire is not a cube, and TNT has three distinct faces.
constexpr int kHazardSpritesFirst = kBeehiveSpritesFirst + kBeehiveSprites;
constexpr int kLavaSprite = kHazardSpritesFirst;
constexpr int kFireSprite = kLavaSprite + 1;
constexpr int kTntTopSprite = kFireSprite + 1;
constexpr int kTntBottomSprite = kTntTopSprite + 1;
constexpr int kTntSideSprite = kTntBottomSprite + 1;
/// Lit TNT is the same block flashing white. The reference does it with a
/// shader on the whole entity, so **all three faces** need a bleached twin - one
/// side texture alone left the top and bottom unlit through the flash.
constexpr int kTntPrimedTopSprite = kTntSideSprite + 1;
constexpr int kTntPrimedBottomSprite = kTntPrimedTopSprite + 1;
constexpr int kTntPrimedSideSprite = kTntPrimedBottomSprite + 1;
constexpr int kHazardSprites = 8;

/// Fire's animation, on the same arrangement as water's: every frame is its own
/// layer and the renderer swaps which one the meshed layer samples. The
/// reference's strip is 32 frames, and playing them in order is what gives the
/// licks that rise past the top of the block and vanish.
constexpr int kFireFrameFirst = kHazardSpritesFirst + kHazardSprites;
constexpr int kFireFrames = 32;
constexpr float kFireFrameSeconds = 0.055f;

/// The three drawn-bow pictures and the arrow entity's own sheet.
///
/// The bow's resting icon and the arrow's icon are **not** here: both are
/// ordinary items and live in the appended item run, where the icon is one
/// offset. Only these four have no item to be the icon of.
///
/// **The entity sprite is the top-left sixteenth of the reference's 32x32
/// sheet**, cropped rather than resized: both rects its model samples - the
/// 16x5 shaft strip and the 5x5 nock - already live in that corner, and a
/// texture array needs every layer the same size.
constexpr int kProjectileSpritesFirst = kFireFrameFirst + kFireFrames;
constexpr int kBowPullingFirstSprite = kProjectileSpritesFirst;
constexpr int kArrowEntitySprite = kBowPullingFirstSprite + 3;
/// The ladder rides at the end of this run rather than in the block table,
/// because it spends four ids on which wall it is fixed to and a table row is
/// one id.
constexpr int kLadderSprite = kArrowEntitySprite + 1;
constexpr int kVineSprite = kLadderSprite + 1;
constexpr int kCocoaFirstSprite = kVineSprite + 1;
constexpr int kProjectileSprites = 9;

/// The moon's eight phases, one layer each.
///
/// **Appended after every existing run**, which is the rule for adding texture
/// layers here: anything inserted in the middle silently slides every layer
/// behind it and mistextures them all.
///
/// The order is the reference's own cycle - full, waning gibbous, third quarter,
/// waning crescent, new, waxing crescent, first quarter, waxing gibbous - so the
/// phase index is the day count modulo eight and needs no table.
constexpr int kMoonPhaseFirst = kProjectileSpritesFirst + kProjectileSprites;
constexpr int kMoonPhases = 8;

/// The stonecutter's saw blade. Appended after every existing run, for the same
/// reason the moon was: the blade is a plane standing out of the bench, not one
/// of the bench's own faces, so it cannot come off the block's side or top.
constexpr int kStonecutterSawSprite = kMoonPhaseFirst + kMoonPhases;

/// The end face of a bed's head - the pillow's own end, white across the whole
/// mattress band, and one image for all sixteen colours because the reference
/// shares it too. The side texture is half pillow and half blanket, so standing
/// it in painted the headboard red down one half.
constexpr int kBedHeadNorthSprite = kStonecutterSawSprite + 1;

/// The compost inside a composter, at every level below a ready one.
constexpr int kComposterCompostSprite = kBedHeadNorthSprite + 1;

static_assert(kRedstoneSprites == 55, "the redstone sprite run changed size; update Main.cpp");
static_assert(kRedstoneSpritesFirst == kComposterCompostSprite + 1,
              "the redstone run must start immediately after the last appended layer");

/// True for anything in any table-driven run.
constexpr bool isExtraBlock(BlockId id) {
    return (id >= kFirstExtraBlock && id <= kLastExtraBlock) ||
           (id >= kFirstExtraBlock2 && id <= kLastExtraBlock2) ||
           (id >= kFirstExtraBlock3 && id <= kLastExtraBlock3) ||
           (id >= kFirstExtraBlock4 && id <= kLastExtraBlock4) ||
           (id >= kFirstExtraBlock5 && id <= kLastExtraBlock5) ||
           (id >= kFirstExtraBlock6 && id <= kLastExtraBlock6) ||
           (id >= kFirstExtraBlock7 && id <= kLastExtraBlock7);
}

/// All seven runs index one table, each continuing where the last left off.
constexpr std::size_t extraBlockIndex(BlockId id) {
    if (id <= kLastExtraBlock) {
        return static_cast<std::size_t>(static_cast<int>(id) - static_cast<int>(kFirstExtraBlock));
    }
    if (id <= kLastExtraBlock2) {
        return static_cast<std::size_t>(kExtraRun1Count + static_cast<int>(id) -
                                        static_cast<int>(kFirstExtraBlock2));
    }
    if (id <= kLastExtraBlock3) {
        return static_cast<std::size_t>(kExtraRun1Count + kExtraRun2Count + static_cast<int>(id) -
                                        static_cast<int>(kFirstExtraBlock3));
    }
    if (id <= kLastExtraBlock4) {
        return static_cast<std::size_t>(kExtraRun1Count + kExtraRun2Count + kExtraRun3Count +
                                        static_cast<int>(id) -
                                        static_cast<int>(kFirstExtraBlock4));
    }
    if (id <= kLastExtraBlock5) {
        return static_cast<std::size_t>(kExtraRun1Count + kExtraRun2Count + kExtraRun3Count +
                                        kExtraRun4Count + static_cast<int>(id) -
                                        static_cast<int>(kFirstExtraBlock5));
    }
    if (id <= kLastExtraBlock6) {
        return static_cast<std::size_t>(kExtraRun1Count + kExtraRun2Count + kExtraRun3Count +
                                        kExtraRun4Count + kExtraRun5Count + static_cast<int>(id) -
                                        static_cast<int>(kFirstExtraBlock6));
    }
    return static_cast<std::size_t>(kExtraRun1Count + kExtraRun2Count + kExtraRun3Count +
                                    kExtraRun4Count + kExtraRun5Count + kExtraRun6Count +
                                    static_cast<int>(id) - static_cast<int>(kFirstExtraBlock7));
}

constexpr const ExtraBlockInfo& extraBlockInfo(BlockId id) {
    return kExtraBlocks[extraBlockIndex(id)];
}

/// Which way this block's distinguishing face points, or `Unknown` for the
/// majority that look the same all the way round.
///
/// An **icon** needs this: it draws two side faces at once, and they point
/// different ways, so handing both the same direction put a furnace's mouth on
/// both of them. A crafting table has no facing to store - the reference puts
/// its tooled face on the two Z sides and a plainer one on the two X sides,
/// fixed - so it answers with that axis.
constexpr FaceDirection blockFacing(BlockId id) {
    if (isFurnace(id)) {
        return furnaceFacing(id);
    }
    if (isChest(id)) {
        return chestFacing(id);
    }
    if (isBeehive(id)) {
        return beehiveFacing(id);
    }
    if (isFenceGate(id)) {
        return gateFacing(id);
    }
    if (isLadder(id)) {
        return ladderFacing(id);
    }
    if (isCarvedPumpkin(id)) {
        return static_cast<FaceDirection>(static_cast<int>(id) -
                                          static_cast<int>(BlockId::CarvedPumpkinFirst));
    }
    if (isJackOLantern(id)) {
        return static_cast<FaceDirection>(static_cast<int>(id) -
                                          static_cast<int>(BlockId::JackOLanternFirst));
    }
    if (id == BlockId::CraftingTable || id == BlockId::SmithingTable) {
        return FaceDirection::NegZ;
    }
    // The six-way machines. **A miss here renders perfectly in the world and
    // wrong in every inventory slot**, because the mesher hands the face a real
    // direction and an icon has none to give - so both of an icon's visible
    // side quads come back as the identifying face, and a sticky piston wears
    // its plate on all of them.
    //
    // `facing6AsDirection` answers `Unknown` for a machine pointing straight up
    // or down, which is right: neither of an icon's two side quads is its front,
    // and `machineFaceRole` reads that same `Unknown` as "show me a side".
    if (isPiston(id)) {
        return facing6AsDirection(pistonFacing(id));
    }
    if (isPistonHead(id)) {
        return facing6AsDirection(pistonHeadFacing(id));
    }
    if (isObserver(id)) {
        return facing6AsDirection(observerFacing(id));
    }
    if (isDispenserLike(id)) {
        return facing6AsDirection(dispenserFacing(id));
    }
    if (isLightningRod(id)) {
        return facing6AsDirection(lightningRodFacing(id));
    }
    if (isRepeater(id)) {
        return repeaterFacing(id);
    }
    if (isComparator(id)) {
        return comparatorFacing(id);
    }
    if (isSignLike(id)) {
        return signFacing(id);
    }
    return FaceDirection::Unknown;
}

// **The pumpkin constructors, round-tripped through the real reader.** Both are
// asserted against `blockFacing` itself rather than against the arithmetic they
// share with it, so neither side can be quietly changed alone: give either run a
// different first enumerator, reorder `FaceDirection`, or swap the two base ids
// in `carvedPumpkinAt`/`jackOLanternAt`, and this fails. The last two lines are
// what catches the swap, because a swapped pair still round-trips its facing.
static_assert(blockFacing(carvedPumpkinAt(FaceDirection::PosX)) == FaceDirection::PosX &&
                  blockFacing(carvedPumpkinAt(FaceDirection::NegX)) == FaceDirection::NegX &&
                  blockFacing(carvedPumpkinAt(FaceDirection::PosZ)) == FaceDirection::PosZ &&
                  blockFacing(jackOLanternAt(FaceDirection::NegZ)) == FaceDirection::NegZ &&
                  blockFacing(jackOLanternAt(FaceDirection::Unknown)) == FaceDirection::NegZ &&
                  isCarvedPumpkin(carvedPumpkinAt(FaceDirection::Unknown)) &&
                  isJackOLantern(jackOLanternAt(FaceDirection::Unknown)),
              "a pumpkin built by facing must read back the same facing, and must land in its own "
              "run");


/// Which way a block's front should point when someone puts it down: **toward
/// the placer**, so the identifying face is the one they can see.
///
/// The expression was written out at four separate placement sites - the stair,
/// the gate, the furnace and the chest - which is exactly the shape of bug this
/// project keeps paying for. One owner, and the sign is stated once: a camera
/// looking along +X is west of the block, so the block faces -X.
///
/// **Two floats rather than a vector**, because `Block.hpp` deliberately has no
/// glm include and adding one has broken the build here before.
constexpr FaceDirection facingToward(float aimX, float aimZ) {
    const float ax = aimX < 0.0f ? -aimX : aimX;
    const float az = aimZ < 0.0f ? -aimZ : aimZ;
    if (ax > az) {
        return aimX > 0.0f ? FaceDirection::NegX : FaceDirection::PosX;
    }
    return aimZ > 0.0f ? FaceDirection::NegZ : FaceDirection::PosZ;
}

/// The same compass in the other enum, so a placement site that needs `Facing`
/// need not write the toward-the-placer rule out a second time.
///
/// **A converter rather than a second aim-reading function**, deliberately. A
/// `facingTowardAsFacing(aimX, aimZ)` would copy the two comparisons above and
/// hand this project a second answer to "which way is the placer looking" - its
/// most expensive failure shape. `facingToward` stays the one owner of the aim
/// rule and this only renames its result, so the two cannot drift apart.
///
/// **The mapping is read off `Facing`'s own axis comments rather than assumed**:
/// its `North` is -Z, `East` is +X, `South` is +Z and `West` is -X, which pins
/// all four against `FaceDirection`'s axes with nothing left to choose.
///
/// `Unknown` has no twin, because `Facing` is four values with no unknown, so it
/// falls out as `North`. A caller that can receive `Unknown` and cares must test
/// for it before converting; every placement site takes its value from
/// `facingToward`, which never returns `Unknown`.
constexpr Facing toFacing(FaceDirection d) {
    switch (d) {
    case FaceDirection::PosX:
        return Facing::East;
    case FaceDirection::NegX:
        return Facing::West;
    case FaceDirection::PosZ:
        return Facing::South;
    case FaceDirection::NegZ:
        return Facing::North;
    case FaceDirection::Unknown:
        break;
    }
    return Facing::North;
}

/// **The quarter-turn error this consolidation exists to prevent, kept compiled
/// so the assert below can reject it.** A four-value compass rotated by one step
/// returns a perfectly valid `Facing` for every input, satisfies any check that
/// only asks whether a `Facing` came back, and reads correctly in a diff.
/// Nothing but a test naming the axes can tell it from the truth, which is why
/// the negative half here is worth more than the positive one.
constexpr Facing toFacingRotatedByAQuarterTurn(FaceDirection d) {
    switch (d) {
    case FaceDirection::PosX:
        return Facing::South;
    case FaceDirection::NegX:
        return Facing::North;
    case FaceDirection::PosZ:
        return Facing::West;
    case FaceDirection::NegZ:
        return Facing::East;
    case FaceDirection::Unknown:
        break;
    }
    return Facing::North;
}

/// Checks a candidate twin against the axes both enums document, rather than
/// against itself: an assert comparing one side of a derivation with the other
/// side of the same derivation proves nothing, and eleven of them once passed in
/// this file while pointing at the wrong texture.
constexpr bool facingTwinMatchesTheAxes(Facing (*convert)(FaceDirection)) {
    return convert(FaceDirection::PosX) == Facing::East &&
           convert(FaceDirection::NegX) == Facing::West &&
           convert(FaceDirection::PosZ) == Facing::South &&
           convert(FaceDirection::NegZ) == Facing::North;
}
static_assert(facingTwinMatchesTheAxes(toFacing),
              "the Facing twin must agree with both enums' documented axes");
static_assert(!facingTwinMatchesTheAxes(toFacingRotatedByAQuarterTurn),
              "and a compass wrong by a quarter turn must be rejected - it returns a valid "
              "Facing for every input and looks entirely reasonable in a diff");

/// Picks between a single chest's sprite and the two halves', in the one place
/// that knows the mapping.
constexpr int chestHalfSprite(ChestHalf half, int single, int left, int right) {
    switch (half) {
    case ChestHalf::Left:
        return left;
    case ChestHalf::Right:
        return right;
    default:
        return single;
    }
}

/// Which layer of the texture array a face of this block samples.
///
/// `direction` only matters to blocks whose sides are not all alike, and
/// defaults to `Unknown` so every caller that has no direction to give - item
/// icons, the drop mesh, the hotbar - keeps working untouched. `half` is the
/// same arrangement for the double chest: an icon has no neighbours, so it
/// draws the single-chest art.
/// Which of a six-way machine's three kinds of face is being drawn: **0 the
/// front, 1 the back, 2 one of the four sides**.
///
/// A furnace only ever faces one of four ways, so `BlockFace` plus a horizontal
/// `FaceDirection` was enough for it. A piston, an observer, a dispenser and a
/// dropper can all point straight up or straight down, and for those the top
/// and bottom faces are what carries the front - so the drawn face has to be
/// recovered as one of six before it can be compared with the facing.
///
/// An icon has no direction to give, and answers **front**, which is the same
/// rule the furnace follows: show the face that identifies the block.
constexpr int machineFaceRole(int facing, BlockFace face, FaceDirection direction) {
    if (face == BlockFace::Side && direction == FaceDirection::Unknown) {
        return facing == Facing6Up || facing == Facing6Down ? 2 : 0;
    }
    const int drawn = face == BlockFace::Top      ? Facing6Up
                      : face == BlockFace::Bottom ? Facing6Down
                                                  : directionAsFacing6(direction);
    if (drawn == facing) {
        return 0;
    }
    return drawn == oppositeFacing6(facing) ? 1 : 2;
}

// **An icon draws two side quads at once**, one at the block's facing and one a
// quarter turn from it. If `blockFacing` has nothing to say, both come back as
// the front - which is how a sticky piston came to wear its plate all the way
// round in the slot while looking perfectly right in the world, where the mesher
// hands every face a real direction.
//
// Written against the whole expression an icon actually evaluates, `blockFacing`
// and all, rather than against one half of it: an assert that compares a
// derivation with itself proves nothing.
static_assert(
    machineFaceRole(pistonFacing(pistonAt(Facing6North, false, true)), BlockFace::Side,
                    blockFacing(pistonAt(Facing6North, false, true))) == 0 &&
        machineFaceRole(pistonFacing(pistonAt(Facing6North, false, true)), BlockFace::Side,
                        quarterTurn(blockFacing(pistonAt(Facing6North, false, true)))) == 2,
    "a sticky piston's icon must show its plate on one side quad and a plain side on the other");
static_assert(
    machineFaceRole(observerFacing(observerAt(Facing6North, false)), BlockFace::Side,
                    blockFacing(observerAt(Facing6North, false))) == 0 &&
        machineFaceRole(observerFacing(observerAt(Facing6North, false)), BlockFace::Side,
                        quarterTurn(blockFacing(observerAt(Facing6North, false)))) == 2,
    "an observer's icon must show its face on one side quad and a plain side on the other");
static_assert(
    machineFaceRole(dispenserFacing(dispenserAt(Facing6North, false)), BlockFace::Side,
                    blockFacing(dispenserAt(Facing6North, false))) == 0 &&
        machineFaceRole(dispenserFacing(dispenserAt(Facing6North, false)), BlockFace::Side,
                        quarterTurn(blockFacing(dispenserAt(Facing6North, false)))) == 2,
    "a dispenser's icon must show its mouth on one side quad and a plain side on the other");
// A machine pointing straight up has no front an icon can show from the side, so
// both quads must fall back to a plain side rather than to the plate.
static_assert(machineFaceRole(Facing6Up, BlockFace::Side,
                              blockFacing(pistonAt(Facing6Up, false, true))) == 2,
              "a piston pointing up shows no plate on either of an icon's side quads");

/// The layer a block's face samples, or **-1 when nothing here claims it**.
///
/// **This is the guarded half of a pair, and the guard is the whole point.**
/// The function used to end `return static_cast<float>(TextureLayer::Stone)` -
/// a real, plausible value - which is this project's third recorded failure
/// shape: a `default:` that answers instead of failing hides every missing
/// entry, and there is no error, no warning and no wrong-looking screen, because
/// a block drawn as stone looks like a block. `blockName` has had a sweep behind
/// its `Air` catch-all for milestones; its twin here had none.
///
/// So the fall-through says -1, `blockTextureLayer` below turns that back into
/// stone for every caller, and `everyBlockTextured` walks all `kBlockIdCount`
/// ids at compile time asking this one whether anybody answered.
///
/// It is `constexpr` for that reason alone. Nothing calls it at compile time
/// except the sweep.
constexpr float blockTextureLayerOrNone(BlockId id, BlockFace face,
                                        FaceDirection direction = FaceDirection::Unknown,
                                        ChestHalf half = ChestHalf::Single) {
    // A cut shape is drawn with the block it was cut from, face for face - which
    // is what makes a sandstone stair keep its own top, sides and underside
    // without a single row anywhere naming it.
    const BlockId material = shapedParent(id);
    if (material != id) {
        return blockTextureLayerOrNone(material, face, direction, half);
    }
    // A chest nobody has opened yet is drawn as the chest it will become. The
    // whole point of the family is that it looks like nothing special.
    if (isLootChest(id)) {
        return blockTextureLayerOrNone(plainChestFor(id), face, direction, half);
    }
    // **Wet farmland is wet farmland, all the way down to level 1.** The
    // reference draws the moist top on 1-7 and the dry top only at 0, so the six
    // new levels have no picture of their own to reach for, and delegating is
    // both correct and the reason no art needed staging. One line rather than
    // six rows in `kExtraBlocks`, and it cannot drift from what `FarmlandMoist`
    // does because it *is* what `FarmlandMoist` does.
    if (isFarmlandMoistureRun(id)) {
        return blockTextureLayerOrNone(BlockId::FarmlandMoist, face, direction, half);
    }
    // Both fluids and fire answer before the big switch, because each covers a
    // run of ids rather than one, and a `case` per level is what a family test
    // exists to avoid.
    if (isLava(id)) {
        return static_cast<float>(kLavaSprite);
    }
    // Every depth draws the snow that was already loaded, which is the whole
    // reason this family cost no art.
    if (isSnowLayer(id)) {
        return static_cast<float>(TextureLayer::Snow);
    }
    if (id == BlockId::Fire) {
        return static_cast<float>(kFireSprite);
    }
    if (isLadder(id)) {
        return static_cast<float>(kLadderSprite);
    }
    if (isVine(id)) {
        return static_cast<float>(kVineSprite);
    }
    if (isCocoa(id)) {
        return static_cast<float>(kCocoaFirstSprite + cocoaAge(id));
    }
    if (id == BlockId::Tnt || id == BlockId::TntPrimed) {
        const bool lit = id == BlockId::TntPrimed;
        switch (face) {
        case BlockFace::Top:
            return static_cast<float>(lit ? kTntPrimedTopSprite : kTntTopSprite);
        case BlockFace::Bottom:
            return static_cast<float>(lit ? kTntPrimedBottomSprite : kTntBottomSprite);
        default:
            return static_cast<float>(lit ? kTntPrimedSideSprite : kTntSideSprite);
        }
    }
    // Redstone. Everything here is answered before the switch for the same
    // reason the fluids are: each covers a run of ids rather than a single one.
    if (isRedstoneWire(id)) {
        return static_cast<float>(kWireFirstSprite + wireSignal(id));
    }
    if (isRedstoneTorch(id)) {
        return redstoneTorchLit(id) ? kRedstoneTorchLayer
                                    : static_cast<float>(kRedstoneTorchOffSprite);
    }
    if (isLever(id)) {
        return static_cast<float>(kLeverSprite);
    }
    if (isRepeater(id) || isComparator(id)) {
        // Only the top carries the block's own picture; every other face is the
        // smooth stone bench it stands on.
        if (face != BlockFace::Top) {
            return static_cast<float>(kRedstoneSlabSprite);
        }
        if (isRepeater(id)) {
            return static_cast<float>(repeaterPowered(id) ? kRepeaterOnSprite : kRepeaterSprite);
        }
        return static_cast<float>(comparatorPowered(id) || comparatorSubtracts(id)
                                      ? kComparatorOnSprite
                                      : kComparatorSprite);
    }
    if (isObserver(id)) {
        switch (machineFaceRole(observerFacing(id), face, direction)) {
        case 0:
            return static_cast<float>(kObserverFrontSprite);
        case 1:
            return static_cast<float>(observerPowered(id) ? kObserverBackOnSprite
                                                          : kObserverBackSprite);
        default:
            // The reference paints `observer_top` on both the up and down faces
            // and `observer_side` on the four round the middle.
            return static_cast<float>(face == BlockFace::Side ? kObserverSideSprite
                                                              : kObserverTopSprite);
        }
    }
    if (isPiston(id) || isPistonHead(id)) {
        const bool head = isPistonHead(id);
        const int facing = head ? pistonHeadFacing(id) : pistonFacing(id);
        const bool sticky = head ? pistonHeadSticky(id) : pistonSticky(id);
        switch (machineFaceRole(facing, face, direction)) {
        case 0:
            // An extended body shows the hollow it left behind; anything else
            // shows the plate, sticky or plain.
            if (!head && pistonExtended(id)) {
                return static_cast<float>(kPistonInnerSprite);
            }
            return static_cast<float>(sticky ? kPistonTopStickySprite : kPistonTopSprite);
        case 1:
            return static_cast<float>(head ? kPistonSideSprite : kPistonBottomSprite);
        default:
            return static_cast<float>(kPistonSideSprite);
        }
    }
    if (isDispenserLike(id)) {
        const bool dropper = isDropper(id);
        const int facing = dispenserFacing(id);
        const bool vertical = facing == Facing6Up || facing == Facing6Down;
        switch (machineFaceRole(facing, face, direction)) {
        case 0:
            if (vertical) {
                return static_cast<float>(dropper ? kDropperFrontVerticalSprite
                                                  : kDispenserFrontVerticalSprite);
            }
            return static_cast<float>(dropper ? kDropperFrontSprite : kDispenserFrontSprite);
        default:
            return static_cast<float>(face == BlockFace::Side ? kMachineSideSprite
                                                              : kMachineTopSprite);
        }
    }
    if (isDaylightDetector(id)) {
        if (face != BlockFace::Top) {
            return static_cast<float>(kDaylightSideSprite);
        }
        return static_cast<float>(daylightDetectorInverted(id) ? kDaylightInvertedTopSprite
                                                               : kDaylightTopSprite);
    }
    if (isLightningRod(id)) {
        return static_cast<float>(lightningRodPowered(id) ? kLightningRodOnSprite
                                                          : kLightningRodSprite);
    }
    if (isTripwireHook(id)) {
        return static_cast<float>(kTripwireHookSprite);
    }
    if (isTripwire(id)) {
        return static_cast<float>(kTripwireSprite);
    }
    if (isRail(id)) {
        const int family = railFamily(id);
        if (family == 0) {
            // The plain rail is the one that can bend, and its second picture is
            // the corner rather than a live form it does not have.
            return static_cast<float>(kRailFirstSprite + (railShape(id) >= 6 ? 1 : 0));
        }
        return static_cast<float>(kRailFirstSprite + family * 2 + (railPowered(id) ? 1 : 0));
    }
    if (id == BlockId::RedstoneLampLit) {
        return static_cast<float>(kRedstoneLampOnSprite);
    }
    // A struck target and a retuned note block wear the same picture as the
    // quiet ones they were appended after - the strength and the pitch are
    // state, not a different block. **Without these two the appended runs fall
    // through to the switch's default and draw as stone**, which is invisible
    // until the material table reports one layer claimed by two families.
    if (isTarget(id)) {
        return static_cast<float>(kTableSpritesFirst + extraBlockInfo(BlockId::Target).layer);
    }
    if (isNoteBlock(id)) {
        return static_cast<float>(kTableSpritesFirst + extraBlockInfo(BlockId::NoteBlock).layer);
    }
    switch (id) {
    case BlockId::Stone:
        return static_cast<float>(TextureLayer::Stone);
    case BlockId::TallGrass:
        return static_cast<float>(TextureLayer::TallGrass);
    case BlockId::Torch:
        return static_cast<float>(TextureLayer::Torch);
    case BlockId::CraftingTable:
        // The reference puts the tooled face on the two Z sides and a plainer
        // one on the two X sides. It is fixed rather than a placement state, so
        // it needs no facing stored - only a face that knows which way it
        // points, which `FaceDirection` now supplies.
        switch (face) {
        case BlockFace::Top:
            return static_cast<float>(TextureLayer::CraftingTableTop);
        case BlockFace::Bottom:
            return static_cast<float>(TextureLayer::Planks);
        case BlockFace::Side:
            if (direction == FaceDirection::PosX || direction == FaceDirection::NegX) {
                return static_cast<float>(TextureLayer::CraftingTableSide);
            }
            // An unknown direction shows the face that identifies the block.
            return static_cast<float>(TextureLayer::CraftingTableFront);
        }
        return static_cast<float>(TextureLayer::CraftingTableFront);
    case BlockId::Furnace:
    case BlockId::FurnaceLit:
    case BlockId::FurnaceEast:
    case BlockId::FurnaceEastLit:
    case BlockId::FurnaceSouth:
    case BlockId::FurnaceSouthLit:
    case BlockId::FurnaceWest:
    case BlockId::FurnaceWestLit:
        // Three plain sides and one mouth, which is what the reference has and
        // what needs the face direction: without it the mesher could only say
        // "a side", so the mouth went on all four.
        switch (face) {
        case BlockFace::Top:
        case BlockFace::Bottom:
            return static_cast<float>(TextureLayer::FurnaceTop);
        case BlockFace::Side:
            if (direction == furnaceFacing(id)) {
                return static_cast<float>(isFurnaceLit(id) ? TextureLayer::FurnaceFrontLit
                                                           : TextureLayer::FurnaceFront);
            }
            // An icon has no direction, so it shows the face that identifies
            // the block rather than a blank side.
            if (direction == FaceDirection::Unknown) {
                return static_cast<float>(isFurnaceLit(id) ? TextureLayer::FurnaceFrontLit
                                                           : TextureLayer::FurnaceFront);
            }
            return static_cast<float>(TextureLayer::FurnaceSide);
        }
        return static_cast<float>(TextureLayer::FurnaceSide);
    case BlockId::Dirt:
        return static_cast<float>(TextureLayer::Dirt);
    case BlockId::Grass:
        // Grass is soil with a living surface, so all three faces differ.
        switch (face) {
        case BlockFace::Top:
            return static_cast<float>(TextureLayer::GrassTop);
        case BlockFace::Bottom:
            return static_cast<float>(TextureLayer::Dirt);
        case BlockFace::Side:
            return static_cast<float>(TextureLayer::GrassSide);
        }
        return static_cast<float>(TextureLayer::GrassTop);
    case BlockId::Sand:
        return static_cast<float>(TextureLayer::Sand);
    case BlockId::Cobblestone:
        return static_cast<float>(TextureLayer::Cobblestone);
    case BlockId::Gravel:
        return static_cast<float>(TextureLayer::Gravel);
    case BlockId::Snow:
        return static_cast<float>(TextureLayer::Snow);
    case BlockId::Planks:
        return static_cast<float>(TextureLayer::Planks);
    case BlockId::Bricks:
        return static_cast<float>(TextureLayer::Bricks);
    case BlockId::Glowstone:
        return static_cast<float>(TextureLayer::Glowstone);
    case BlockId::Log:
        // End grain differs from bark, the same way grass differs from soil.
        return static_cast<float>(face == BlockFace::Side ? TextureLayer::LogSide : TextureLayer::LogTop);
    case BlockId::Leaves:
        return static_cast<float>(TextureLayer::Leaves);
    case BlockId::Andesite:
        return static_cast<float>(TextureLayer::Andesite);
    case BlockId::Diorite:
        return static_cast<float>(TextureLayer::Diorite);
    case BlockId::Granite:
        return static_cast<float>(TextureLayer::Granite);
    case BlockId::SmoothStone:
        return static_cast<float>(TextureLayer::SmoothStone);
    case BlockId::StoneBricks:
        return static_cast<float>(TextureLayer::StoneBricks);
    case BlockId::MossyCobblestone:
        return static_cast<float>(TextureLayer::MossyCobblestone);
    case BlockId::Obsidian:
        return static_cast<float>(TextureLayer::Obsidian);
    case BlockId::Clay:
        return static_cast<float>(TextureLayer::Clay);
    case BlockId::Sandstone:
        // Cut stone: a patterned cap, a plain base and a banded side.
        switch (face) {
        case BlockFace::Top:
            return static_cast<float>(TextureLayer::SandstoneTop);
        case BlockFace::Bottom:
            return static_cast<float>(TextureLayer::SandstoneBottom);
        case BlockFace::Side:
            return static_cast<float>(TextureLayer::SandstoneSide);
        }
        return static_cast<float>(TextureLayer::SandstoneSide);
    case BlockId::Bookshelf:
        // Shelves on the sides only; the reference caps it with plain planks.
        return static_cast<float>(face == BlockFace::Side ? TextureLayer::Bookshelf
                                                          : TextureLayer::Planks);
    case BlockId::Glass:
        return static_cast<float>(TextureLayer::Glass);
    case BlockId::Dandelion:
        return static_cast<float>(TextureLayer::Dandelion);
    case BlockId::Poppy:
        return static_cast<float>(TextureLayer::Poppy);
    case BlockId::DeadBush:
        return static_cast<float>(TextureLayer::DeadBush);
    case BlockId::CoalOre:
        return static_cast<float>(TextureLayer::CoalOre);
    case BlockId::IronOre:
        return static_cast<float>(TextureLayer::IronOre);
    case BlockId::CopperOre:
        return static_cast<float>(TextureLayer::CopperOre);
    case BlockId::GoldOre:
        return static_cast<float>(TextureLayer::GoldOre);
    case BlockId::RedstoneOre:
        return static_cast<float>(TextureLayer::RedstoneOre);
    case BlockId::LapisOre:
        return static_cast<float>(TextureLayer::LapisOre);
    case BlockId::DiamondOre:
        return static_cast<float>(TextureLayer::DiamondOre);
    case BlockId::EmeraldOre:
        return static_cast<float>(TextureLayer::EmeraldOre);
    case BlockId::Deepslate:
        // Grained like a log: the cut end differs from the sides.
        return static_cast<float>(face == BlockFace::Side ? TextureLayer::DeepslateSide
                                                          : TextureLayer::DeepslateTop);
    case BlockId::Bedrock:
        return static_cast<float>(TextureLayer::Bedrock);
    case BlockId::Terracotta:
        return static_cast<float>(TextureLayer::Terracotta);
    case BlockId::PackedIce:
        return static_cast<float>(TextureLayer::PackedIce);
    case BlockId::Prismarine:
        return static_cast<float>(kPrismarineSprite);
    case BlockId::SeaLantern:
        return static_cast<float>(kSeaLanternSprite);
    case BlockId::CoarseDirt:
        return static_cast<float>(kCoarseDirtSprite);
    default:
        break;
    }

    // Every state of a candle picks between its colour's two pictures, lit and
    // unlit. **Asked before the table**, because the appended states sit
    // outside every run and would otherwise index off the end of it.
    if (isCandle(id)) {
        const int base = isCandleLit(id) ? kLitCandleFirstLayer : kUnlitCandleFirstLayer;
        return static_cast<float>(kTableSpritesFirst + base + candleColour(id));
    }

    // A door wears its upper picture on the top half and its lower on the
    // bottom; a trapdoor has only the one.
    if (isDoor(id)) {
        const OpeningFamily& family = kDoorFamilies[static_cast<std::size_t>(doorFamily(id))];
        return static_cast<float>(kTableSpritesFirst +
                                  (doorIsUpper(id) ? family.upperLayer : family.lowerLayer));
    }
    if (isTrapdoor(id)) {
        return static_cast<float>(
            kTableSpritesFirst +
            kTrapdoorFamilies[static_cast<std::size_t>(trapdoorFamily(id))].lowerLayer);
    }
    if (isBed(id)) {
        const BedFamily family = bedFamilyAt(bedColour(id));
        const bool cap = (face == BlockFace::Top || face == BlockFace::Bottom);
        // The head's outward end is the pillow's own end face. The side
        // texture is half pillow and half blanket, so standing it in there
        // painted half the headboard red.
        if (!cap && bedIsHead(id) && direction == bedFacing(id)) {
            return static_cast<float>(kBedHeadNorthSprite);
        }
        const int layer = bedIsHead(id) ? (cap ? family.headTop : family.headSide)
                                        : (cap ? family.footTop : family.footSide);
        return static_cast<float>(kTableSpritesFirst + layer);
    }

    if (isExtraBlock(id)) {
        // no column for a front - so the front is answered here, the way the
        // furnace's is, before the row's own layer is reached for.
        if ((isCarvedPumpkin(id) || isJackOLantern(id)) && face == BlockFace::Side &&
            (direction == blockFacing(id) || direction == FaceDirection::Unknown)) {
            return static_cast<float>(kTableSpritesFirst + (isJackOLantern(id)
                                                                ? kJackOLanternFaceLayer
                                                                : kCarvedPumpkinFaceLayer));
        }
        if (isTrappedChest(id) && face == BlockFace::Side &&
            (direction == chestFacing(id) || direction == FaceDirection::Unknown)) {
            return static_cast<float>(kTableSpritesFirst + kTrappedChestFaceLayer);
        }
        if (isBlastFurnace(id) && face == BlockFace::Side &&
            (direction == blastFurnaceFacing(id) || direction == FaceDirection::Unknown)) {
            return static_cast<float>(kTableSpritesFirst + (isBlastFurnaceLit(id)
                                                                ? kBlastFurnaceLitFaceLayer
                                                                : kBlastFurnaceFaceLayer));
        }
        const ExtraBlockInfo& info = extraBlockInfo(id);
        const bool cap = (face == BlockFace::Top || face == BlockFace::Bottom);
        return static_cast<float>(kTableSpritesFirst +
                                  (cap && info.topLayer >= 0 ? info.topLayer : info.layer));
    }

    // **Before the hive, because `isBeehive` is true of a nest.** A nest asks
    // for its top and its bottom one at a time where the hive asks for "not a
    // side" once, since the hive genuinely has one picture for both caps and the
    // nest genuinely has two. Writing that difference in now - while the two
    // answers happen to be equal - is what makes staging the art a data change
    // rather than a logic change.
    if (isBeeNest(id)) {
        if (face == BlockFace::Top) {
            return static_cast<float>(kBeeNestTopSprite);
        }
        if (face == BlockFace::Bottom) {
            return static_cast<float>(kBeeNestBottomSprite);
        }
        if (direction == beehiveFacing(id) || direction == FaceDirection::Unknown) {
            return static_cast<float>(beehiveHasHoney(id) ? kBeeNestFrontHoneySprite
                                                          : kBeeNestFrontSprite);
        }
        return static_cast<float>(kBeeNestSideSprite);
    }

    if (isBeehive(id)) {
        if (face != BlockFace::Side) {
            return static_cast<float>(kBeehiveEndSprite);
        }
        if (direction == beehiveFacing(id) || direction == FaceDirection::Unknown) {
            return static_cast<float>(beehiveHasHoney(id) ? kBeehiveFrontHoneySprite
                                                          : kBeehiveFrontSprite);
        }
        return static_cast<float>(kBeehiveSideSprite);
    }

    switch (id) {
    case BlockId::Smoker:
    case BlockId::SmokerLit:
    case BlockId::SmokerEast:
    case BlockId::SmokerEastLit:
    case BlockId::SmokerSouth:
    case BlockId::SmokerSouthLit:
    case BlockId::SmokerWest:
    case BlockId::SmokerWestLit:
        // The furnace's arrangement exactly, on the smoker's own textures.
        switch (face) {
        case BlockFace::Top:
        case BlockFace::Bottom:
            return static_cast<float>(kSmokerTopSprite);
        case BlockFace::Side:
            if (direction == furnaceFacing(id) || direction == FaceDirection::Unknown) {
                return static_cast<float>(isFurnaceLit(id) ? kSmokerFrontLitSprite : kSmokerFrontSprite);
            }
            return static_cast<float>(kSmokerSideSprite);
        }
        return static_cast<float>(kSmokerSideSprite);
    case BlockId::SmithingTable:
        // Fixed faces: the tooled front on the two Z sides, the plainer one on
        // the X sides, exactly as the crafting table does it.
        switch (face) {
        case BlockFace::Top:
            return static_cast<float>(kSmithingTopSprite);
        case BlockFace::Bottom:
            return static_cast<float>(kSmithingBottomSprite);
        case BlockFace::Side:
            return static_cast<float>(direction == FaceDirection::PosX || direction == FaceDirection::NegX
                                          ? kSmithingSideSprite
                                          : kSmithingFrontSprite);
        }
        return static_cast<float>(kSmithingFrontSprite);
    case BlockId::Chest:
    case BlockId::ChestEast:
    case BlockId::ChestSouth:
    case BlockId::ChestWest:
        switch (face) {
        case BlockFace::Top:
        case BlockFace::Bottom:
            return static_cast<float>(chestHalfSprite(half, kChestTopSprite, kChestLeftTopSprite,
                                                      kChestRightTopSprite));
        case BlockFace::Side:
            if (direction == chestFacing(id) || direction == FaceDirection::Unknown) {
                return static_cast<float>(chestHalfSprite(half, kChestFrontSprite,
                                                          kChestLeftFrontSprite,
                                                          kChestRightFrontSprite));
            }
            if (direction == oppositeDirection(chestFacing(id))) {
                // A single chest's back is just another side; a half's is not,
                // because its border stops at the seam like the front's does.
                return static_cast<float>(chestHalfSprite(half, kChestSideSprite,
                                                          kChestLeftBackSprite,
                                                          kChestRightBackSprite));
            }
            return static_cast<float>(kChestSideSprite);
        }
        return static_cast<float>(kChestSideSprite);
    case BlockId::AncientDebris:
        // Grained like deepslate and a log: the cut end is not the side.
        return static_cast<float>(face == BlockFace::Side ? kAncientDebrisSideSprite
                                                          : kAncientDebrisTopSprite);
    case BlockId::EmberiteBlock:
        return static_cast<float>(kEmberiteBlockSprite);
    case BlockId::Water0:
    case BlockId::Water1:
    case BlockId::Water2:
    case BlockId::Water3:
    case BlockId::Water4:
    case BlockId::Water5:
    case BlockId::Water6:
    case BlockId::Water7:
    case BlockId::WaterFalling:
        return static_cast<float>(TextureLayer::Water);
    case BlockId::Air:
        break;
    }
    return -1.0f;
}

/// The layer a block's face samples. **Stone for anything unclaimed**, which is
/// what every caller has always been handed and what the mesher, the icon and
/// the dropped item all expect - the only change is that the fall-through is now
/// one named place with a compile-time sweep behind it instead of a `return` at
/// the bottom of a nine-hundred-line function.
constexpr float blockTextureLayer(BlockId id, BlockFace face,
                                  FaceDirection direction = FaceDirection::Unknown,
                                  ChestHalf half = ChestHalf::Single) {
    const float layer = blockTextureLayerOrNone(id, face, direction, half);
    return layer < 0.0f ? static_cast<float>(TextureLayer::Stone) : layer;
}

/// **Where farmland stops looking watered** - the half of this block a player
/// actually sees, and the half two ids got wrong.
///
/// Driven through `blockTextureLayer`, which is the function the mesher calls,
/// rather than through the table underneath it - a check that reads the data
/// instead of the answer can pass while the answer is wrong. It sits here
/// rather than beside `farmlandAtMoisture` because `blockTextureLayer` is
/// declared a few thousand lines below it, and a checker placed above the
/// function it calls is a build failure this file has already paid for once.
///
/// **Parameterised on the first wet level so the wrong rules can be named.** An
/// assert that hard-codes the right answer agrees with whatever the code does;
/// this one is handed a candidate and refuses two of them below.
constexpr int farmlandWetTopLayer() {
    return static_cast<int>(blockTextureLayer(BlockId::FarmlandMoist, BlockFace::Top));
}
constexpr int farmlandDryTopLayer() {
    return static_cast<int>(blockTextureLayer(BlockId::Farmland, BlockFace::Top));
}

// **The vacuity guard, and it is stated before anything leans on it.** If wet
// and dry farmland drew the same top, every assert below would pass for every
// possible rule while proving nothing at all - bug shape #11, where eleven
// asserts passed while comparing one side of a derivation against itself. It
// is its own `static_assert` rather than a branch inside the checker because a
// compile-time-constant `if` is exactly the C4127 this build treats as an
// error, and because a guard nobody can see is a guard nobody maintains.
static_assert(farmlandWetTopLayer() != farmlandDryTopLayer(),
              "wet and dry farmland must draw different tops, or every farmland texture "
              "assert below this line is vacuous");

constexpr bool farmlandWetTopFrom(int firstWetLevel) {
    const int wetTop = farmlandWetTopLayer();
    const int dryTop = farmlandDryTopLayer();
    for (int level = 0; level < kFarmlandMoistureLevels; ++level) {
        const int top =
            static_cast<int>(blockTextureLayer(farmlandAtMoisture(level), BlockFace::Top));
        if (top != wetTop && top != dryTop) {
            return false;
        }
        if ((top == wetTop) != (level >= firstWetLevel)) {
            return false;
        }
    }
    return true;
}

static_assert(farmlandWetTopFrom(1),
              "the reference draws the moist top on levels 1-7 and the dry top only at 0");
static_assert(!farmlandWetTopFrom(kFarmlandFullMoisture),
              "this is the rule a reader assumes - that farmland looks wet only when fully "
              "watered - and it is the one that made the dry texture appear far too early. "
              "If it ever passes, the delegation above has been removed");
static_assert(!farmlandWetTopFrom(0),
              "level 0 is the one level that must look dry; if this passes, the dry "
              "texture has no level left that shows it");

// The layer numbers a model box borrows are written out beside the models,
// three thousand lines above the table that owns them - so they are checked
// against it here. **This is what stops them being a second copy that rots**:
// move a row and the build fails rather than the bell quietly turning gold.
//
// **But only in the runs a pin actually names, and that is four of the seven.**
// Every id asserted below falls in the third run (the redstone torch), the
// fourth (the composters), the fifth (the ten from the stonecutter through the
// anvil and scaffolding to the two campfires) or the seventh (the hopper). The
// **first, second and sixth runs carry no alignment pin at all** - the ores and
// metal blocks through the wools, concretes and flowers to the cobweb; the
// nether and bamboo run; and the candle run. That is most of the table.
// `NoteBlock` and `Target` look like pins and are not: both sit in
// `blockTextureLayer` as returns, so they *consume* a row rather than check it,
// and a shifted table carries them along in silence with everything else.
//
// The size assert above cannot close that gap, because **cardinality is not
// alignment**. Insert an enumerator inside a run and append its row at that
// run's end - the natural slip when the array is hundreds of rows and the eye
// is on the wrong visual group - and the count still balances, the size assert
// still passes, and every row after the insertion shifts by one, so a whole
// tail of blocks wears the next block's name and picture.
//
// Closing it wants one pin per unpinned run, and the warning beside
// `kTableLayerBase` states the shape it must take: assert the **sum** the
// reader evaluates, never the offset against itself, or it joins the eleven
// that passed while pointing at a nether plant.
// Checked against `extraBlockInfo` rather than `blockTextureLayer`, which is
// `inline` and not `constexpr` - so the base `blockTextureLayer` would have
// added has to be checked too, and is, on the first line. Without that line
// every assert below passed while pointing at a nether plant.
static_assert(static_cast<int>(kTableLayerBase) == kTableSpritesFirst,
              "the appended run has moved - every model layer below is off by the difference");
static_assert(kStoneLayer == static_cast<float>(TextureLayer::Stone));
static_assert(kDirtLayer == static_cast<float>(TextureLayer::Dirt));
static_assert(kPlanksLayer == static_cast<float>(TextureLayer::Planks));
static_assert(kLogSideLayer == static_cast<float>(TextureLayer::LogSide));
static_assert(kStonecutterSawLayer == static_cast<float>(kStonecutterSawSprite));
static_assert(kComposterCompostLayer == static_cast<float>(kComposterCompostSprite));
static_assert(kAnvilBodyLayer == kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Anvil).layer));
static_assert(kScaffoldSideLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Scaffolding).layer));
static_assert(kCampfireLogLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Campfire).layer));
// The ember rows, held against the two campfires' `topLayer`. **The edit that
// makes these fail is changing either row in `extraBlockInfo` without moving
// the literal beside `kCampfireLogLayer`** - which is exactly what the soul
// campfire's row is waiting for, once `soul_campfire_log_lit.png` is staged and
// 536 stops being the flame sheet.
static_assert(kCampfireLitLogLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Campfire).topLayer));
static_assert(kSoulCampfireLitLogLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::SoulCampfire).topLayer));
static_assert(kCampfireLitLogLayer != kCampfireLogLayer);
static_assert(kSoulCampfireLitLogLayer != kCampfireLitLogLayer);
static_assert(kHopperSideLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Hopper).layer));
static_assert(kWaterLayer == static_cast<float>(TextureLayer::Water));
static_assert(kComposterTopLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(composterAt(0)).topLayer));
static_assert(kComposterSideLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(composterAt(0)).layer));
static_assert(kComposterReadyLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(composterAt(8)).topLayer));
static_assert(kStonecutterTopLayer ==
              kTableLayerBase +
                  static_cast<float>(extraBlockInfo(BlockId::Stonecutter).topLayer));
static_assert(kGrindstoneRoundLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Grindstone).topLayer));
static_assert(kBellSideLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Bell).layer));
// **Move the lectern's row and this fails**, which is the only thing standing
// between the plinth's top ring and whatever block ends up at 475 instead.
static_assert(kLecternSideLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Lectern).layer));
static_assert(kRedstoneTorchLayer ==
                  kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::RedstoneTorch).layer),
              "the lit redstone torch must still be the table row it was placed in");
static_assert(kBellTopLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Bell).topLayer));
static_assert(kCauldronSideLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::Cauldron).layer));
static_assert(kBrewingStandBaseLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::BrewingStand).layer));
static_assert(kBrewingStandRodLayer ==
              kTableLayerBase + static_cast<float>(extraBlockInfo(BlockId::BrewingStand).topLayer));

// Nothing drawn as a model may be a full cube, or it goes back to culling the
// faces of everything it touches - which is what let you see through the world
// around a cauldron.
static_assert(blockShape(BlockId::Cauldron) == BlockShape::Model &&
                  blockShape(BlockId::Bell) == BlockShape::Model &&
                  blockShape(BlockId::Grindstone) == BlockShape::Model &&
                  blockShape(BlockId::Stonecutter) == BlockShape::Model &&
                  blockShape(BlockId::BrewingStand) == BlockShape::Model &&
                  blockShape(composterAt(4)) == BlockShape::Model &&
                  blockShape(BlockId::Torch) == BlockShape::Model,
              "the seven blocks that used to draw as cubes or as flowers are models now");
// **The five that stopped being crosses and cubes, and the counts they draw
// with.** Put any of them back behind `isCrossBlock` or leave it falling
// through to `Full` and the first fails; the second is written against
// `candleCount`, so a candle whose sticks and whose count part company - four
// candles drawn as one, say - fails too.
static_assert(blockShape(BlockId::Candle) == BlockShape::Model &&
                  blockShape(BlockId::Bamboo) == BlockShape::Model &&
                  blockShape(BlockId::SeaPickle) == BlockShape::Model &&
                  blockShape(BlockId::Lectern) == BlockShape::Model &&
                  blockShape(BlockId::DragonEgg) == BlockShape::Model,
              "each of these has a model of its own in the reference and drew as a whole cell");
static_assert(postModel(candleAt(0, 1, true)).count == candleCount(candleAt(0, 1, true)) &&
                  postModel(candleAt(7, 2, false)).count == candleCount(candleAt(7, 2, false)) &&
                  postModel(candleAt(3, 3, true)).count == candleCount(candleAt(3, 3, true)) &&
                  postModel(candleAt(16, 4, false)).count == candleCount(candleAt(16, 4, false)) &&
                  postModel(BlockId::DragonEgg).count == 6,
              "a candle draws as many sticks as it holds, whatever its colour and whether or not "
              "it is lit");
static_assert(!occludesFace(BlockId::Cauldron, 1) && !occludesFace(BlockId::Bell, 1) &&
                  !occludesFace(composterAt(0), -1),
              "a model must never occlude its neighbour's face");
static_assert(!isSolid(BlockId::Torch) && isSolid(BlockId::Cauldron),
              "a torch is walked through; a cauldron is not");
static_assert(postBoxes(BlockId::Torch).boxes[0].maxY < 0.7f &&
                  postBoxes(BlockId::Torch).boxes[0].minX > 0.4f,
              "a torch is aimed at as a stick, not as the cell it stands in");

/// What a block is called, for the HUD and for logs.
///
/// Lives beside the block definition rather than in whichever screen happens to
/// need it, so a new block is named once.
constexpr const char* blockName(BlockId id) {
    // **Bedrock's name, and it has to differ from the cube's.** Both were called
    // "Snow", so the catalogue showed two entries with one name and no way to
    // tell which was the thin layer. Java calls the layer "Snow" and the cube
    // "Snow Block"; Bedrock is the reference here and does it the other way
    // round, which also leaves the cube's existing name alone.
    if (isSnowLayer(id)) {
        return "Top Snow";
    }
    // All eight moisture levels are one block with one name. The reference has
    // no separate name for wet farmland either - it is a state, not a block.
    if (isFarmlandMoistureRun(id)) {
        return blockName(BlockId::FarmlandMoist);
    }
    // Every cut shape, in one branch. **The legacy `case` labels for the stone
    // slab and the oak fence were removed with it**: a family test placed above
    // a switch swallows the ids that switch was written for, and the compiler is
    // perfectly happy with the dead code that leaves behind.
    if (isStairs(id)) {
        return kStairFamilies[static_cast<std::size_t>(stairFamily(id))].name;
    }
    if (isSlab(id)) {
        return kSlabFamilies[static_cast<std::size_t>(slabFamily(id))].name;
    }
    if (isWall(id)) {
        return kWallFamilies[static_cast<std::size_t>(wallFamily(id))].name;
    }
    if (isFence(id)) {
        return kFenceFamilies[static_cast<std::size_t>(fenceFamily(id))].name;
    }
    if (isFenceGate(id)) {
        return kGateFamilies[static_cast<std::size_t>(gateFamily(id))].name;
    }
    if (isCarpet(id)) {
        return kCarpetFamilies[static_cast<std::size_t>(carpetFamily(id))].name;
    }
    if (isPane(id)) {
        return kPaneFamilies[static_cast<std::size_t>(paneFamily(id))].name;
    }
    if (isButton(id)) {
        return kButtonFamilies[static_cast<std::size_t>(buttonFamily(id))].name;
    }
    if (isPressurePlate(id)) {
        return kPressurePlateFamilies[static_cast<std::size_t>(pressurePlateFamily(id))].name;
    }
    if (isSign(id)) {
        return kSignFamilies[static_cast<std::size_t>(signFamily(id))].name;
    }
    if (isHangingSign(id)) {
        return kHangingSignFamilies[static_cast<std::size_t>(signFamily(id))].name;
    }
    if (isBanner(id)) {
        return kBannerFamilies[static_cast<std::size_t>(signFamily(id))].name;
    }    // Redstone. Every one of these spends most of its ids on a state - a
    // strength, a facing, a delay - and every state shares the one name, so
    // seven hundred ids cost eighteen strings.
    if (isRedstoneWire(id)) {
        return "Redstone Dust";
    }
    if (isRedstoneTorch(id)) {
        return "Redstone Torch";
    }
    if (isLever(id)) {
        return "Lever";
    }
    if (isRepeater(id)) {
        return "Redstone Repeater";
    }
    if (isComparator(id)) {
        return "Redstone Comparator";
    }
    if (isPiston(id)) {
        return pistonSticky(id) ? "Sticky Piston" : "Piston";
    }
    if (isPistonHead(id)) {
        return "Piston Head";
    }
    if (isObserver(id)) {
        return "Observer";
    }
    if (isDropper(id)) {
        return "Dropper";
    }
    if (isDispenser(id)) {
        return "Dispenser";
    }
    if (isDaylightDetector(id)) {
        return "Daylight Detector";
    }
    if (isLightningRod(id)) {
        return "Lightning Rod";
    }
    if (isTripwireHook(id)) {
        return "Tripwire Hook";
    }
    if (isTripwire(id)) {
        return "Tripwire";
    }
    // The name a player reads has to be the plain one: an unrolled chest is a
    // chest, and calling it anything else in the HUD would announce that this
    // one has loot in it before they have opened it.
    if (isLootChest(id)) {
        return "Chest";
    }
    if (isRail(id)) {
        switch (railFamily(id)) {
        case 1:
            return "Powered Rail";
        case 2:
            return "Detector Rail";
        case 3:
            return "Activator Rail";
        default:
            return "Rail";
        }
    }
    // Both answered before the switch, which still holds their quiet forms as
    // ordinary table rows - and would otherwise name the lit ones nothing.
    if (isTarget(id)) {
        return "Target";
    }
    if (isNoteBlock(id)) {
        return "Note Block";
    }
    if (isLadder(id)) {
        return "Ladder";
    }
    if (isVine(id)) {
        return "Vines";
    }
    if (isCocoa(id)) {
        return "Cocoa";
    }
    if (isExtraBlock(id)) {
        return extraBlockInfo(id).name;
    }
    // Ahead of the hive, which now answers `true` for a nest as well.
    if (isBeeNest(id)) {
        return "Bee Nest";
    }
    if (isBeehive(id)) {
        return "Beehive";
    }
    if (isWater(id)) {
        return "Water";
    }
    if (isLava(id)) {
        return "Lava";
    }
    if (id == BlockId::Fire) {
        return "Fire";
    }
    if (id == BlockId::Tnt || id == BlockId::TntPrimed) {
        return "TNT";
    }
    // Before `isFurnace`, which answers true for both of the other cookers too.
    if (isSmoker(id)) {
        return "Smoker";
    }
    if (isBlastFurnace(id)) {
        return "Blast Furnace";
    }
    if (isFurnace(id)) {
        return "Furnace";
    }
    // Every state of a candle borrows the name from its colour's own row, so
    // the seventeen names are written down exactly once.
    if (isCandle(id)) {
        return extraBlockInfo(static_cast<BlockId>(static_cast<int>(BlockId::Candle) +
                                                  candleColour(id)))
            .name;
    }
    // Both halves of a door and both halves of a trapdoor read their family's
    // one name, so twelve names cover three hundred and eighty-four ids.
    if (isDoor(id)) {
        return kDoorFamilies[static_cast<std::size_t>(doorFamily(id))].name;
    }
    if (isTrapdoor(id)) {
        return kTrapdoorFamilies[static_cast<std::size_t>(trapdoorFamily(id))].name;
    }
    if (isBed(id)) {
        return bedFamilyAt(bedColour(id)).name;
    }
    switch (id) {
    case BlockId::Stone:
        return "Stone";
    case BlockId::Dirt:
        return "Dirt";
    case BlockId::Grass:
        return "Grass";
    case BlockId::Sand:
        return "Sand";
    case BlockId::Cobblestone:
        return "Cobblestone";
    case BlockId::Gravel:
        return "Gravel";
    case BlockId::Snow:
        return "Snow";
    // **Oak's three original blocks, named before there was a second wood.**
    // Every other wood is fully qualified - "Spruce Planks", "Birch Log",
    // "Cherry Leaves" - and oak's own later arrivals already are too ("Oak
    // Wood", "Oak Sapling", "Stripped Oak Log"), so these three read as a
    // default rather than a wood in the one list that shows them side by side.
    case BlockId::Planks:
        return "Oak Planks";
    case BlockId::Bricks:
        return "Bricks";
    case BlockId::Glowstone:
        return "Glowstone";
    case BlockId::Log:
        return "Oak Log";
    case BlockId::Leaves:
        return "Oak Leaves";
    case BlockId::TallGrass:
        return "Tall Grass";
    case BlockId::Torch:
        return "Torch";
    case BlockId::CraftingTable:
        return "Crafting Table";
    case BlockId::Andesite:
        return "Andesite";
    case BlockId::Diorite:
        return "Diorite";
    case BlockId::Granite:
        return "Granite";
    case BlockId::SmoothStone:
        return "Smooth Stone";
    case BlockId::StoneBricks:
        return "Stone Bricks";
    case BlockId::MossyCobblestone:
        return "Mossy Cobblestone";
    case BlockId::Obsidian:
        return "Obsidian";
    case BlockId::Clay:
        return "Clay";
    case BlockId::Sandstone:
        return "Sandstone";
    case BlockId::Bookshelf:
        return "Bookshelf";
    case BlockId::Glass:
        return "Glass";
    case BlockId::Dandelion:
        return "Dandelion";
    case BlockId::Poppy:
        return "Poppy";
    case BlockId::DeadBush:
        return "Dead Bush";
    case BlockId::CoalOre:
        return "Coal Ore";
    case BlockId::IronOre:
        return "Iron Ore";
    case BlockId::CopperOre:
        return "Copper Ore";
    case BlockId::GoldOre:
        return "Gold Ore";
    case BlockId::RedstoneOre:
        return "Redstone Ore";
    case BlockId::LapisOre:
        return "Lapis Ore";
    case BlockId::DiamondOre:
        return "Diamond Ore";
    case BlockId::EmeraldOre:
        return "Emerald Ore";
    case BlockId::Deepslate:
        return "Deepslate";
    case BlockId::Bedrock:
        return "Bedrock";
    case BlockId::Terracotta:
        return "Terracotta";
    case BlockId::PackedIce:
        return "Packed Ice";
    case BlockId::Prismarine:
        return "Prismarine";
    case BlockId::SeaLantern:
        return "Sea Lantern";
    case BlockId::CoarseDirt:
        return "Coarse Dirt";
    case BlockId::AncientDebris:
        return "Ancient Debris";
    case BlockId::EmberiteBlock:
        return "Block of Emberite";
    case BlockId::SmithingTable:
        return "Smithing Table";
    case BlockId::Chest:
    case BlockId::ChestEast:
    case BlockId::ChestSouth:
    case BlockId::ChestWest:
        return "Chest";
    default:
        return "Air";
    }
}
/// Whether a block's name reads as `text`.
///
/// **The characters, not the pointers.** Two `const char*` into different string
/// literals cannot be compared in a constant expression, so the sweep below can
/// only ask whether a name *reads* like the catch-all, not whether it is
/// literally that one - which comes to the same thing for a reader of the HUD.
constexpr bool blockNameIs(BlockId id, const char* text) {
    const char* name = blockName(id);
    while (*name != '\0' && *name == *text) {
        ++name;
        ++text;
    }
    return *name == *text;
}

// **What widening a family predicate costs if the early-out behind it is not
// split.** `blockName` answered "Beehive" for everything `isBeehive` accepted,
// so the nest joining that family would have renamed twenty-four blocks without
// touching a line of naming code - bug shape #2, and the reason every one of the
// sixteen callers was read rather than only the new one. The last line is the
// one that fails if the nest branch is ever moved below the hive's.
static_assert(blockNameIs(BlockId::BeeNestRunFirst, "Bee Nest") &&
                  blockNameIs(BlockId::BeeNestRunLast, "Bee Nest") &&
                  blockNameIs(BlockId::Beehive, "Beehive") &&
                  blockNameIs(BlockId::BeehiveHoneyRunLast, "Beehive") &&
                  !blockNameIs(BlockId::BeeNestRunFirst, "Beehive"),
              "a nest and a hive must not share a name");

/// How many ids one `static_assert` below covers.
///
/// **The sweep is split rather than written as one assert.** MSVC counts
/// constexpr steps per evaluation, and one pass over the whole enum costs more
/// than a quarter of a million of them - measured, and over the limit at every
/// setting up to 250,000. Each assert is its own evaluation with its own
/// budget, and a stride of this size fits inside the documented default of
/// 100,000 with room to spare.
constexpr int kNameSweepStride = 512;

/// Whether every id in one stride is named by something other than
/// `blockName`'s last resort.
///
/// That last resort returns "Air", which is a **real** name rather than an
/// obviously wrong one, so a block that falls past all forty family tests *and*
/// the switch neither crashes nor blanks - it quietly appears in the HUD and the
/// catalogue as air, which is this project's recorded bug shape ten. Returning
/// something is still the right runtime behaviour; refusing to compile is how
/// the missing entry gets found instead. Air itself is the one id allowed to
/// answer "Air".
constexpr bool everyBlockNamed(int stride) {
    const int first = stride * kNameSweepStride;
    const int end = first + kNameSweepStride;
    for (int id = first; id < end && id < static_cast<int>(kBlockIdCount); ++id) {
        const BlockId block = static_cast<BlockId>(id);
        if (block != BlockId::Air && blockNameIs(block, "Air")) {
            return false;
        }
    }
    return true;
}

/// How many strides the name sweep runs.
///
/// **One owner for the count.** The passes below are generated from this
/// constant and the coverage assert multiplies the same constant, so raising it
/// cannot silence the alarm without also sweeping the ids it just admitted.
/// Written out as seven asserts beside a literal `7`, those were two
/// transcriptions of the same number: bumping the literal to 8 without adding
/// the matching line would have quietened the coverage check while 512 ids went
/// unswept. **The same shape as every other strided sweep in the tree; find them
/// by searching for `*SweepPasses` and `*SweepStride` rather than from a list
/// here, because a list rots and a search does not.**
///
/// **Derived rather than written by hand, so it can never need raising again.**
/// The *stride* is what the step budget cares about and it stays fixed at 512,
/// so the cost of a pass is unchanged however many ids arrive; only the number
/// of passes grows, and a pass here is a template instantiation rather than a
/// line of source. **That direction is available precisely because the passes
/// are generated** - a sweep whose passes are hand-written `static_assert` lines
/// cannot compute them, and must derive its *stride* instead, which keeps its
/// capacity but makes every pass dearer as the count grows. Prefer this form
/// wherever the passes come from a `make_integer_sequence`; it gives up nothing.
///
/// **The rounding is up, deliberately.** Rounding down under-covers silently,
/// which is the exact failure this sweep exists to catch.
constexpr int kNameSweepPasses =
    (static_cast<int>(kBlockIdCount) + kNameSweepStride - 1) / kNameSweepStride;

/// **The per-pass assert lives inside the template on purpose.** Each
/// instantiation is its own constant evaluation with its own budget, which is
/// the whole point of striding; folding `everyBlockNamed(Pass)` directly into
/// one expression would spend all seven strides out of a single 100,000-step
/// allowance and put the sweep straight back over the limit.
template <int Pass>
struct NameSweep {
    static_assert(everyBlockNamed(Pass),
                  "a block id falls past every name test and shows up as Air");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyNamePassSwept(std::integer_sequence<int, Pass...>) {
    return (NameSweep<Pass>::swept && ...);
}

static_assert(everyNamePassSwept(std::make_integer_sequence<int, kNameSweepPasses>{}),
              "a block id falls past every name test and shows up as Air - the failing NameSweep "
              "instantiation above names which stride");
/// **Kept after the derivation, and it is not vacuous.** It no longer guards a
/// hand-written count falling behind the enum, because nothing is hand-written
/// now - it guards the *rounding*. Write the division above without its
/// `+ kNameSweepStride - 1` and this fires for every count that is not an exact
/// multiple of the stride, which is precisely the silent under-sweep the
/// derivation could still be written to produce.
static_assert(kNameSweepPasses * kNameSweepStride >= static_cast<int>(kBlockIdCount),
              "the name sweep rounds the wrong way and no longer covers every block id - the "
              "pass count must round up");

/// **The twin sweep, and the reason `blockTextureLayerOrNone` exists.**
///
/// `blockName`'s catch-all has had a sweep behind it for milestones;
/// `blockTextureLayer`'s had none, and its catch-all is worse - `Air` is at
/// least a name nobody wants to see, while stone is a texture half the world is
/// already made of. A block that fell through simply looked like stone, and the
/// only way it ever surfaced was a material audit noticing one layer claimed by
/// two families.
///
/// One face is enough and `Side` is the one chosen: falling out of the switch is
/// a property of the **id**, not of the face - every `case` that varies by face
/// ends in its own return, so a missing entry is missing for all three at once.
/// Sweeping three faces would triple the constexpr steps to prove the same
/// thing.
///
/// Air is the one id allowed to fall through, exactly as it is the one id
/// allowed to be named "Air".
constexpr int kTextureSweepStride = 512;

constexpr bool everyBlockTextured(int stride) {
    const int first = stride * kTextureSweepStride;
    const int end = first + kTextureSweepStride;
    for (int id = first; id < end && id < static_cast<int>(kBlockIdCount); ++id) {
        const BlockId block = static_cast<BlockId>(id);
        if (block != BlockId::Air && blockTextureLayerOrNone(block, BlockFace::Side) < 0.0f) {
            return false;
        }
    }
    return true;
}

// **Delete any one `case` from `blockTextureLayerOrNone` and one of these
// fails.** That is the single edit, and it is the edit that used to be silent.
//
// **Derived for the same reason as `kNameSweepPasses`, whose comment carries the
// argument for why this is the direction to prefer**: the stride stays fixed, so
// the per-pass step budget never moves, and the passes are a template argument
// rather than lines of source, so they are free to be computed. Rounds up,
// because rounding down under-sweeps in silence.
constexpr int kTextureSweepPasses =
    (static_cast<int>(kBlockIdCount) + kTextureSweepStride - 1) / kTextureSweepStride;

template <int Pass>
struct TextureSweep {
    static_assert(everyBlockTextured(Pass),
                  "a block id falls past every texture test and draws as stone");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyTexturePassSwept(std::integer_sequence<int, Pass...>) {
    return (TextureSweep<Pass>::swept && ...);
}

static_assert(everyTexturePassSwept(std::make_integer_sequence<int, kTextureSweepPasses>{}),
              "a block id falls past every texture test and draws as stone - the failing "
              "TextureSweep instantiation above names which stride");
// Not vacuous after the derivation: it stops guarding the count and starts
// guarding the rounding, and fires if the division above is ever written to
// round down. See `kNameSweepPasses`.
static_assert(kTextureSweepPasses * kTextureSweepStride >= static_cast<int>(kBlockIdCount),
              "the texture sweep rounds the wrong way and no longer covers every block id - the "
              "pass count must round up");
// And the guard that keeps the sweep honest: if the fall-through ever went back
// to returning a real layer, every one of the seven above would pass for ever.
static_assert(blockTextureLayerOrNone(BlockId::Air, BlockFace::Side) < 0.0f &&
                  blockTextureLayer(BlockId::Air, BlockFace::Side) ==
                      static_cast<float>(TextureLayer::Stone),
              "the unclaimed answer must stay distinguishable from a real one");

} // namespace game
