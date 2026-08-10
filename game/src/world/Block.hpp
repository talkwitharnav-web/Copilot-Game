#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace game {

/// How many materials each cut shape comes in.
///
/// **These have to be declared before the enum**, because the runs of ids they
/// size are inside it, while the tables that name the materials cannot be
/// written until `BlockId` exists. Each table `static_assert`s its own size
/// against the matching constant, so the two halves cannot drift apart.
constexpr int kStairFamilyCount = 52;
constexpr int kSlabFamilyCount = 55;
constexpr int kWallFamilyCount = 25;
constexpr int kFenceFamilyCount = 12;
constexpr int kGateFamilyCount = 11;
constexpr int kCarpetFamilyCount = 16;
constexpr int kPaneFamilyCount = 17;

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
    /// these â€” name, texture layer, whether it is a cross â€” comes from one table
    /// rather than from forty-eight cases in four different switches. Grouped by
    /// family so `Tool.cpp` and `categoryFor` can test ranges instead of names.
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
    StairsRunFirst,
    StairsRunLast = StairsRunFirst + (kStairFamilyCount - 1) * 8 - 1,
    SlabRunFirst,
    SlabRunLast = SlabRunFirst + (kSlabFamilyCount - 1) * 2 - 1,
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
};

/// The highest id in use. Anything that walks every block reads this rather
/// than naming whichever block happens to be last, which is how the catalogue
/// silently stopped one short of the newest one.
constexpr BlockId kLastBlock = BlockId::SnowLayerLast;

/// How many block ids exist, for anything that wants an array with one slot per
/// block. Derived, so it cannot fall behind the enum the way a literal 256 did.
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

/// How many rows the first run occupies, which is where the second run's rows
/// begin in the table.
constexpr int kExtraRun1Count =
    static_cast<int>(kLastExtraBlock) - static_cast<int>(kFirstExtraBlock) + 1;
constexpr int kExtraRun2Count =
    static_cast<int>(kLastExtraBlock2) - static_cast<int>(kFirstExtraBlock2) + 1;
constexpr int kExtraRun3Count =
    static_cast<int>(kLastExtraBlock3) - static_cast<int>(kFirstExtraBlock3) + 1;

/// A bee's home: four facings, empty then full of honey.
constexpr bool isBeehive(BlockId id) {
    return id >= BlockId::Beehive && id <= BlockId::BeehiveHoneyWest;
}

/// Settled snow, one to seven layers deep.
constexpr bool isSnowLayer(BlockId id) {
    return id >= BlockId::SnowLayerFirst && id <= BlockId::SnowLayerLast;
}

/// How many layers deep, counting from one. Eight would be a solid block, which
/// is `BlockId::Snow` - so seven is as deep as this family goes.
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

/// Whether this hive is full. Only a full one is worth harvesting, and it is
/// the only visible difference - the reference shows honey at level 5 and looks
/// empty at every level below it, so two states carry the whole thing.
constexpr bool beehiveHasHoney(BlockId id) {
    return id >= BlockId::BeehiveHoney;
}

/// Every cross-shaped plant, across both of the runs they were appended in.
///
/// **One owner.** The geometry, the cutout test and the catalogue tab all have
/// to give the same answer and they live in three different files, so they ask
/// this rather than each carrying its own list. It replaced a
/// `id >= kFirstCrossExtra` range test, which quietly required plants to be the
/// last thing in the enum forever.
constexpr bool isCrossBlock(BlockId id) {
    return id == BlockId::TallGrass || id == BlockId::Torch || id == BlockId::Dandelion ||
           id == BlockId::Poppy || id == BlockId::DeadBush ||
           id == BlockId::SoulTorch || id == BlockId::RedstoneTorch ||
           (id >= BlockId::Cornflower && id <= BlockId::Seagrass) ||
           (id >= BlockId::BlueOrchid && id <= BlockId::Cobweb) ||
           (id >= BlockId::Bamboo && id <= BlockId::LargeFern);
}

/// The flowers alone, which is a narrower question than `isCrossBlock` and the
/// one a bee asks. Mushrooms, saplings, ferns, sugar cane and grass are all
/// cross-shaped and none of them is a flower.
///
/// **Two runs, both half-open on names rather than on the run's end**, because
/// each run continues past the last flower in it - so this cannot be written as
/// "everything from here on" the way `isCrossBlock` can.
constexpr bool isFlower(BlockId id) {
    return id == BlockId::Dandelion || id == BlockId::Poppy ||
           (id >= BlockId::Cornflower && id <= BlockId::OrangeTulip) ||
           (id >= BlockId::BlueOrchid && id <= BlockId::LilyOfTheValley);
}

static_assert(isFlower(BlockId::Dandelion) && isFlower(BlockId::Poppy) &&
                  isFlower(BlockId::Allium) && isFlower(BlockId::LilyOfTheValley),
              "every flower must answer yes");
static_assert(!isFlower(BlockId::BrownMushroom) && !isFlower(BlockId::TallGrass) &&
                  !isFlower(BlockId::OakSapling) && !isFlower(BlockId::Fern) &&
                  !isFlower(BlockId::SugarCane) && !isFlower(BlockId::Kelp),
              "a cross-shaped plant is not automatically a flower");

/// Leaves of every wood type: alpha-tested like a plant, but a full cube.
constexpr bool isLeafBlock(BlockId id) {
    return id == BlockId::Leaves || id == BlockId::SpruceLeaves || id == BlockId::BirchLeaves ||
           id == BlockId::JungleLeaves || id == BlockId::AcaciaLeaves ||
           id == BlockId::DarkOakLeaves || id == BlockId::CherryLeaves ||
           id == BlockId::MangroveLeaves;
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

/// Which sides a neighbour-aware block reaches toward.
enum ConnectionBits : std::uint8_t {
    ConnectNorth = 1, // -Z
    ConnectEast = 2,  // +X
    ConnectSouth = 4, // +Z
    ConnectWest = 8,  // -X
    ConnectAll = 15,
};

constexpr bool isSmoker(BlockId id) {
    return id >= BlockId::Smoker && id <= BlockId::SmokerWestLit;
}

constexpr bool isChest(BlockId id) {
    return id >= BlockId::Chest && id <= BlockId::ChestWest;
}

/// Which way this chest's lid faces, and the chest that faces that way. Four
/// ids in the order of `FaceDirection`'s own compass, so both are arithmetic.
constexpr FaceDirection chestFacing(BlockId id) {
    switch (static_cast<int>(id) - static_cast<int>(BlockId::Chest)) {
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

/// Which axis two chests join along - across the latch, never through it, so
/// they stand shoulder to shoulder rather than nose to tail. A bool rather
/// than a vector because this header deliberately knows nothing about glm.
constexpr bool chestJoinsAlongX(BlockId id) {
    const FaceDirection facing = chestFacing(id);
    return facing == FaceDirection::PosZ || facing == FaceDirection::NegZ;
}

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

constexpr FaceDirection beehiveFacing(BlockId id) {
    switch ((static_cast<int>(id) - static_cast<int>(BlockId::Beehive)) % 4) {
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

/// The hive facing this way, with or without honey. **The one place the two
/// halves of a hive's identity are combined**, so filling one can never quietly
/// turn it round - which is exactly what a plain `honey ? A : B` would do.
constexpr BlockId beehiveAt(FaceDirection facing, bool honey) {
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
    return static_cast<BlockId>(static_cast<int>(BlockId::Beehive) + step + (honey ? 4 : 0));
}

static_assert(beehiveFacing(beehiveAt(FaceDirection::PosZ, true)) == FaceDirection::PosZ);
static_assert(beehiveHasHoney(beehiveAt(FaceDirection::PosZ, true)));
static_assert(!beehiveHasHoney(beehiveAt(FaceDirection::NegX, false)));

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

/// **Every cooker, both families.** The block entity, the screen, opening it,
/// breaking it and spilling its contents all ask this and none of them care
/// which kind it is - which is the whole reason a smoker cost no new screen.
constexpr bool isFurnace(BlockId id) {
    return id == BlockId::Furnace || id == BlockId::FurnaceLit ||
           (id >= BlockId::FurnaceEast && id <= BlockId::FurnaceWestLit) || isSmoker(id);
}

constexpr bool isFurnaceLit(BlockId id) {
    if (isSmoker(id)) {
        // Unlit then lit, in pairs, so the low bit of the offset is the state.
        return ((static_cast<int>(id) - static_cast<int>(BlockId::Smoker)) & 1) != 0;
    }
    return id == BlockId::FurnaceLit || id == BlockId::FurnaceEastLit ||
           id == BlockId::FurnaceSouthLit || id == BlockId::FurnaceWestLit;
}

/// Which way this cooker's mouth points.
constexpr FaceDirection furnaceFacing(BlockId id) {
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
/// whole tick rather than two separate rates to keep in step.
constexpr float cookSpeed(BlockId id) {
    return isSmoker(id) ? 2.0f : 1.0f;
}

/// Opens a screen when it is right-clicked, rather than being placed against.
constexpr bool isInteractive(BlockId id) {
    return id == BlockId::CraftingTable || id == BlockId::SmithingTable || isFurnace(id) || isChest(id);
}

/// Highest flowing level. Water at this depth cannot spread any further, which
/// is what stops a single source flooding the world.
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
constexpr bool isCutout(BlockId id) {
    // The cactus art carries the reference model's inset as transparency: a
    // one-texel border on the end caps and a column down each side.
    //
    // The coloured glass is here rather than in the blended pass on purpose:
    // ours has no sorted transparency, and a cutout at least draws in the right
    // order. What it costs is that stained glass is see-through only where its
    // art is, which for the reference's own textures is the border alone.
    return isLeafBlock(id) || isCrossBlock(id) || id == BlockId::Glass || id == BlockId::Fire ||
           id == BlockId::Azalea || id == BlockId::FloweringAzalea || id == BlockId::Cactus ||
           id == BlockId::LilyPad || id == BlockId::IronBars || id == BlockId::EndRod ||
           id == BlockId::Lantern || id == BlockId::SoulLantern ||
           (id >= BlockId::WhiteStainedGlass && id <= BlockId::BlackStainedGlass) ||
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
}};

/// Every material slabs come in, **stone first** for the same reason - the two
/// stone-slab ids predate the run.
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
/// the one fence the reference makes out of rock, and it deliberately refuses
/// to connect to the wooden ones - which ours does too, for free, because
/// connection is a shape test rather than a material one.
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
           (id >= BlockId::StairsRunFirst && id <= BlockId::StairsRunLast);
}

constexpr bool isSlab(BlockId id) {
    return id == BlockId::StoneSlab || id == BlockId::StoneSlabTop ||
           (id >= BlockId::SlabRunFirst && id <= BlockId::SlabRunLast);
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
constexpr bool isClimbable(BlockId id) {
    return isLadder(id) || isVine(id);
}

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
constexpr int stairOffset(BlockId id) {
    return id <= BlockId::CobbleStairs7
               ? static_cast<int>(id) - static_cast<int>(BlockId::CobbleStairs0)
               : (static_cast<int>(id) - static_cast<int>(BlockId::StairsRunFirst)) % 8;
}

constexpr int stairFamily(BlockId id) {
    return id <= BlockId::CobbleStairs7
               ? 0
               : 1 + (static_cast<int>(id) - static_cast<int>(BlockId::StairsRunFirst)) / 8;
}

constexpr Facing stairFacing(BlockId id) { return static_cast<Facing>(stairOffset(id) & 3); }

/// Upside-down stairs, with the full half on top.
constexpr bool stairIsTop(BlockId id) { return (stairOffset(id) & 4) != 0; }

constexpr BlockId stairsAt(int family, Facing facing, bool top) {
    const int offset = static_cast<int>(facing) + (top ? 4 : 0);
    return family == 0
               ? static_cast<BlockId>(static_cast<int>(BlockId::CobbleStairs0) + offset)
               : static_cast<BlockId>(static_cast<int>(BlockId::StairsRunFirst) +
                                      (family - 1) * 8 + offset);
}

constexpr int slabFamily(BlockId id) {
    return id <= BlockId::StoneSlabTop
               ? 0
               : 1 + (static_cast<int>(id) - static_cast<int>(BlockId::SlabRunFirst)) / 2;
}

/// Whether a half block occupies the upper half of its cell.
constexpr bool isUpperHalf(BlockId id) {
    if (id == BlockId::StoneSlabTop) {
        return true;
    }
    if (id < BlockId::SlabRunFirst || id > BlockId::SlabRunLast) {
        return false;
    }
    return ((static_cast<int>(id) - static_cast<int>(BlockId::SlabRunFirst)) & 1) != 0;
}

constexpr BlockId slabAt(int family, bool top) {
    return family == 0
               ? (top ? BlockId::StoneSlabTop : BlockId::StoneSlab)
               : static_cast<BlockId>(static_cast<int>(BlockId::SlabRunFirst) + (family - 1) * 2 +
                                      (top ? 1 : 0));
}

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
    return id;
}

/// True for anything cut from another block.
constexpr bool isShapedBlock(BlockId id) {
    return isStairs(id) || isSlab(id) || isWall(id) || isFence(id) || isFenceGate(id) ||
           isCarpet(id) || isPane(id);
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
    /// A small box standing in the middle of its cell: a lantern, an end rod.
    Post,
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
    /// A full-width plate lying on the floor of its cell. The lily pad, which
    /// **rests on** the water rather than standing in it - the one support in
    /// the game that is not solid.
    Flat,
};

constexpr BlockShape blockShape(BlockId id) {
    if (id == BlockId::Air || isFluid(id)) {
        return BlockShape::Empty;
    }
    // Fire takes the plant shape without being a plant: two crossed blades, no
    // collision, swept away by water and needing something under it. All four
    // of those fall out of `Cross` for free, which is why it is not `Empty`.
    if (id == BlockId::Fire) {
        return BlockShape::Cross;
    }
    if (isCrossBlock(id)) {
        return BlockShape::Cross;
    }
    if (isSnowLayer(id)) {
        return BlockShape::Flat;
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
        return BlockShape::Post;
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
    if (isFenceGate(id)) {
        return BlockShape::Gate;
    }
    // A charge that has been struck comes off the floor. Ours stays a block
    // where the reference makes it an entity, so the detachment has to be in
    // the shape or nothing shows it.
    if (id == BlockId::TntPrimed) {
        return BlockShape::Hovering;
    }
    // A pad lies on the water. Answered before the cross test because it used to
    // be one, and standing it up in the cell is what made it fight the water.
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

/// Every box a block occupies. Nine covers the widest case, a fence drawn with
/// two rails toward each of four neighbours plus its post.
struct BlockBoxes {
    BlockBox boxes[9]{};
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

/// How tall a flat block lies. A carpet is a single texel; the lily pad is the
/// reference's one and a half; settled snow is two texels per layer, which is
/// the reference's own step.
constexpr float flatHeight(BlockId id) {
    if (isSnowLayer(id)) {
        return static_cast<float>(snowLayerDepth(id)) * 2.0f / 16.0f;
    }
    return isCarpet(id) ? 1.0f / 16.0f : kFlatHeight;
}

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
};

struct ModelBoxes {
    ModelBox boxes[4]{};
    int count = 0;
};

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

/// The reference's own `template_lantern`, `end_rod` and `cocoa_stage2`
/// models, in sixteenths, face rectangles and all.
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
    if (isCocoa(id)) {
        result.boxes[0] = {cocoaBoxes(cocoaFacing(id), cocoaAge(id)).boxes[0],
                           8 * t, 4 * t, 16 * t, 13 * t,
                           8 * t, 4 * t, 16 * t, 13 * t};
        result.count = 1;
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
    result.boxes[3] = {{7.5f * t, 9 * t, 6.5f * t, 8.5f * t, 11 * t, 9.5f * t},
                       11 * t, 10 * t, 14 * t, 12 * t,
                       11 * t, 10 * t, 14 * t, 11 * t};
    result.count = 4;
    return result;
}

/// The single source of truth for a block's extent. Meshing, collision and the
/// targeting outline all read this, so none of them can disagree about where a
/// block actually is.
constexpr BlockBoxes collisionBoxes(BlockId id) {
    BlockBoxes result;
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
    case BlockShape::Post:
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
        result.boxes[0] = {0.0f, 0.0f, 0.0f, 1.0f, flatHeight(id), 1.0f};
        result.count = 1;
        break;
    default:
        break;
    }
    return result;
}

/// Height a shape reaches, as a fraction of the block. Stairs reach the top of
/// their step, which is what the targeting outline needs to enclose.
constexpr float shapeHeight(BlockShape shape) {
    return shape == BlockShape::Slab ? 0.5f : 1.0f;
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
    return collisionBoxes(id);
}

/// Blocks movement. Water does not â€” you sink into it, and neither does a plant
/// you walk straight through.
constexpr bool isSolid(BlockId id) {
    const BlockShape shape = blockShape(id);
    return shape == BlockShape::Full || shape == BlockShape::Slab || shape == BlockShape::Stairs ||
           shape == BlockShape::Fence || shape == BlockShape::Wall || shape == BlockShape::Gate ||
           shape == BlockShape::Pane || shape == BlockShape::Post ||
           shape == BlockShape::Hovering || shape == BlockShape::Flat;
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
           (shape != BlockShape::Pane && other == BlockShape::Gate);
}

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

/// Whether the inventory and a dropped item draw this block as the flat picture
/// its texture actually is rather than as a little cube. **Shape, not a list**,
/// so a new plant or a new pane is right the day it is added.
constexpr bool usesFlatIcon(BlockShape shape) {
    return shape == BlockShape::Cross || shape == BlockShape::Flat || shape == BlockShape::Ladder ||
           shape == BlockShape::Vine || shape == BlockShape::Pane;
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
    case BlockShape::Stairs:
        return stairIsTop(neighbour) ? offsetY == -1 : offsetY == 1;
    default:
        return false;
    }
}

/// Drawn in the transparent pass, after everything opaque.
///
/// **Water alone.** Lava is a fluid geometrically - its surface sits below the
/// top of its cell - but it is fully opaque, so putting it in the blended pass
/// meant it neither wrote depth nor occluded anything: water in front of it
/// showed the lava through itself, and standing in a lava cell you could see
/// straight out of the world.
constexpr bool isTranslucent(BlockId id) {
    return isWater(id);
}

/// Highest light level a source can have. Four bits per channel, so a level fits
/// in a nibble and sky plus block light fit in one byte per block.
constexpr int kMaxLight = 15;

/// How much light a block gives off. Zero for everything that is not a source.
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
    if (id == BlockId::SeaPickle) {
        return 6;
    }
    if (id == BlockId::AmethystCluster) {
        return 5;
    }
    if (id == BlockId::CrimsonFungus || id == BlockId::SculkCatalyst) {
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
    if (id == BlockId::RedstoneTorch) {
        return 7;
    }
    if (id == BlockId::SeaLantern) {
        return 15;
    }
    return id == BlockId::Glowstone ? 14 : 0;
}

/// Falls if whatever it was standing on goes away. True for the flat things
/// that have nothing to hold themselves up with.
constexpr bool needsSupportBelow(BlockId id) {
    return blockShape(id) == BlockShape::Cross || blockShape(id) == BlockShape::Flat;
}
/// Held up by the water itself rather than by anything solid. **The lily pad is
/// the only one**, and it is why support is a question with two answers rather
/// than a single `isSolid` test: a pad that demanded solid ground could not be
/// put on a lake at all, and one that shared the water's cell punched a hole in
/// the surface.
constexpr bool restsOnWater(BlockId id) {
    return id == BlockId::LilyPad;
}

/// Destroyed and dropped when water spreads into it, rather than damming the
/// flow. The reference's list is plants, snow, torches, carpets and redstone;
/// ours is everything cross-shaped, which is exactly that set today.
constexpr bool isWashedAway(BlockId id) {
    return blockShape(id) == BlockShape::Cross || isVine(id);
}

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

/// Falls straight down when nothing holds it up. Sand and gravel in the
/// reference, plus concrete powder and anvils it has and we do not.
///
/// Unlike `needsSupportBelow`, which deletes a plant on the spot, one of these
/// *moves*: it has to arrive one block lower, so it is a scheduled update
/// rather than something the breaking code can settle by itself.
constexpr bool isFalling(BlockId id) {
    return id == BlockId::Sand || id == BlockId::Gravel || isConcretePowder(id) ||
           id == BlockId::TntPrimed;
}

/// What a new block may be put **into** rather than beside.
///
/// The reference's `canBeReplaced`, and it answers two questions that would
/// otherwise drift apart: what a falling block displaces on its way down, and
/// which cell a placement actually lands in. Aiming at a plant and getting the
/// block one cell higher is what happens when only the first is written down.
constexpr bool isReplaceable(BlockId id) {
    return id == BlockId::Air || isFluid(id) || id == BlockId::Fire || isWashedAway(id);
}

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
    return blockShape(id) == BlockShape::Cross;
}

static_assert(canWaterlog(BlockId::TallGrass) && canWaterlog(BlockId::Kelp),
              "a plant stands in water");
static_assert(!canWaterlog(BlockId::StoneSlab) && !canWaterlog(BlockId::CobbleStairs0) &&
                  !canWaterlog(BlockId::Stone),
              "anything that occupies its cell displaces the water instead");

/// Drags whatever walks on it. Honey in the reference; the honeycomb block is
/// ours, because a block made of wax reads as sticky whether or not the
/// reference agrees.
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
constexpr BlockId strippedFor(BlockId id) {
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

/// Whether light passes through. Currently the exact opposite of solid, but kept
/// separate because glass and water will be solid *and* transparent.
constexpr bool isLightTransparent(BlockId id) {
    return id == BlockId::Air || isFluid(id) || id == BlockId::Fire || isCutout(id);
}

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
    return id == BlockId::Air || id == BlockId::Glass || isFluid(id) || id == BlockId::Fire ||
           (id >= BlockId::WhiteStainedGlass && id <= BlockId::BlackStainedGlass) ||
           blockShape(id) == BlockShape::Cross || blockShape(id) == BlockShape::Flat ||
           blockShape(id) == BlockShape::Pane || blockShape(id) == BlockShape::Post ||
           blockShape(id) == BlockShape::Ladder || blockShape(id) == BlockShape::Vine ||
           blockShape(id) == BlockShape::Cocoa;
}

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

/// Whether fire will take hold here. The reference splits this into *ignite
/// odds* and *burn odds*; we need only the first, because ours does not model
/// how fast a block is consumed.
///
/// **A shape test wherever one will do.** Every cross-shaped plant burns, so
/// new flowers and saplings are covered the moment they exist rather than the
/// next time somebody remembers this list.
constexpr bool isFlammable(BlockId id) {
    // Fire and the torch are both cross-shaped and neither is fuel - fire would
    // feed on itself and never go out, and the reference lists the torch under
    // "never catches fire at all".
    if (id == BlockId::Fire || id == BlockId::Torch) {
        return false;
    }
    // A wooden stair, slab, fence or gate burns exactly as the planks it was cut
    // from do, and a stone one does not - which is the parent's answer, not a
    // list of six hundred ids.
    const BlockId material = shapedParent(id);
    if (material != id) {
        return isFlammable(material);
    }
    return isLogBlock(id) || isLeafBlock(id) || isPlanksBlock(id) || isWoolBlock(id) ||
           blockShape(id) == BlockShape::Cross || isVine(id) || id == BlockId::Bookshelf ||
           id == BlockId::HayBlock || id == BlockId::CoalBlock ||
           id == BlockId::CraftingTable || id == BlockId::Tnt || id == BlockId::DriedKelpBlock ||
           id == BlockId::NetherWartBlock;
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

/// How many sprite layers the spawn egg run occupies. Kept beside the enum
/// because the loader has to reserve exactly this many, and `Creature.hpp`
/// static-asserts it against `CreatureKind::Count`.
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
constexpr int kExtraSpawnEggLayers = 21;

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
constexpr std::array<ExtraBlockInfo, 309> kExtraBlocks{{
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
}};

/// One more than the highest `.layer` any row uses. **Derived rather than
/// written down**, because nothing else asserts it: too small and the last few
/// blocks silently sample whatever run follows this one, which is a wrong
/// texture with no error anywhere.
constexpr int maxTableLayer() {
    int highest = 0;
    for (const ExtraBlockInfo& info : kExtraBlocks) {
        highest = info.layer > highest ? info.layer : highest;
        highest = info.topLayer > highest ? info.topLayer : highest;
    }
    return highest;
}

constexpr int kTableSprites = maxTableLayer() + 1;
static_assert(kTableSprites == 359, "the table's sprite run changed size; update Main.cpp's list");

static_assert(kExtraBlocks.size() == static_cast<std::size_t>(kExtraRun1Count + kExtraRun2Count +
                                                             kExtraRun3Count),
              "kExtraBlocks must have exactly one row per id across all three table-driven runs");

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

/// The forty-nine appended items - foods, materials and the sixteen dyes - as
/// one contiguous run in `ItemId` order, so an icon is arithmetic rather than a
/// case per item.
constexpr int kExtraItemSpritesFirst = kChestHalfSpritesFirst + kChestHalfSprites;
constexpr int kExtraItemSprites = 90;

/// The beehive: front, its full-of-honey form, the sides and the end caps.
constexpr int kBeehiveSpritesFirst = kExtraItemSpritesFirst + kExtraItemSprites;
constexpr int kBeehiveFrontSprite = kBeehiveSpritesFirst;
constexpr int kBeehiveFrontHoneySprite = kBeehiveFrontSprite + 1;
constexpr int kBeehiveSideSprite = kBeehiveFrontHoneySprite + 1;
constexpr int kBeehiveEndSprite = kBeehiveSideSprite + 1;
constexpr int kBeehiveSprites = 4;

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

/// True for anything in either table-driven run.
constexpr bool isExtraBlock(BlockId id) {
    return (id >= kFirstExtraBlock && id <= kLastExtraBlock) ||
           (id >= kFirstExtraBlock2 && id <= kLastExtraBlock2) ||
           (id >= kFirstExtraBlock3 && id <= kLastExtraBlock3);
}

/// All three runs index one table, each continuing where the last left off.
constexpr std::size_t extraBlockIndex(BlockId id) {
    if (id <= kLastExtraBlock) {
        return static_cast<std::size_t>(static_cast<int>(id) - static_cast<int>(kFirstExtraBlock));
    }
    if (id <= kLastExtraBlock2) {
        return static_cast<std::size_t>(kExtraRun1Count + static_cast<int>(id) -
                                        static_cast<int>(kFirstExtraBlock2));
    }
    return static_cast<std::size_t>(kExtraRun1Count + kExtraRun2Count + static_cast<int>(id) -
                                    static_cast<int>(kFirstExtraBlock3));
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
    if (id == BlockId::CraftingTable || id == BlockId::SmithingTable) {
        return FaceDirection::NegZ;
    }
    return FaceDirection::Unknown;
}

/// A quarter turn about the vertical. `Unknown` stays unknown, which is what
/// keeps every faceless block's icon exactly as it was.
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
inline float blockTextureLayer(BlockId id, BlockFace face,
                               FaceDirection direction = FaceDirection::Unknown,
                               ChestHalf half = ChestHalf::Single) {
    // A cut shape is drawn with the block it was cut from, face for face - which
    // is what makes a sandstone stair keep its own top, sides and underside
    // without a single row anywhere naming it.
    const BlockId material = shapedParent(id);
    if (material != id) {
        return blockTextureLayer(material, face, direction, half);
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

    if (isExtraBlock(id)) {
        const ExtraBlockInfo& info = extraBlockInfo(id);
        const bool cap = (face == BlockFace::Top || face == BlockFace::Bottom);
        return static_cast<float>(kTableSpritesFirst +
                                  (cap && info.topLayer >= 0 ? info.topLayer : info.layer));
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
    return static_cast<float>(TextureLayer::Stone);
}

/// What a block is called, for the HUD and for logs.
///
/// Lives beside the block definition rather than in whichever screen happens to
/// need it, so a new block is named once.
constexpr const char* blockName(BlockId id) {
    if (isSnowLayer(id)) {
        return "Snow";
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
    // Before `isFurnace`, which answers true for smokers too.
    if (isSmoker(id)) {
        return "Smoker";
    }
    if (isFurnace(id)) {
        return "Furnace";
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
    case BlockId::Planks:
        return "Planks";
    case BlockId::Bricks:
        return "Bricks";
    case BlockId::Glowstone:
        return "Glowstone";
    case BlockId::Log:
        return "Log";
    case BlockId::Leaves:
        return "Leaves";
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

} // namespace game
