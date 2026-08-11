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

    /// **A fourth table-driven run**, and it exists for the same reason the
    /// second and third do: everything before it is already written into saved
    /// worlds, so widening a run in place renumbers every block above it.
    ///
    /// This one is the farm. Crops keep their age *in the id* exactly the way
    /// water keeps its level and stairs keep their orientation - a growth stage
    /// is which block this is, not a number beside it, so it costs no
    /// per-chunk array and it saves and meshes for free.
    ///
    /// Farmland is two ids rather than a moisture counter: the reference stores
    /// 0-7 and only ever *looks* different wet or dry, and a cell that is one
    /// of two things is a cell that needs no second table.
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

    /// `sticky<<4 | extended<<3 | facing`, six facings including up and down.
    PistonRunFirst,
    PistonRunLast = PistonRunFirst + 23,
    /// The head an extended piston pushes out in front of itself.
    PistonHeadRunFirst,
    PistonHeadRunLast = PistonHeadRunFirst + 11,

    /// `powered<<3 | facing`. The facing is the **watching** face; the pulse
    /// leaves from the opposite side.
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

    /// `powered<<3 | facing`, six facings.
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

    /// **Signs, hanging signs and banners.** Each spends twenty ids on where it
    /// is: sixteen rotations standing on the ground, then four facings for the
    /// form that hangs off a wall.
    SignRunFirst,
    SignRunLast = SignRunFirst + kSignFamilyCount * kSignStates - 1,
    HangingSignRunFirst,
    HangingSignRunLast = HangingSignRunFirst + kSignFamilyCount * kSignStates - 1,
    BannerRunFirst,
    BannerRunLast = BannerRunFirst + kBannerFamilyCount * kSignStates - 1,
};

/// The highest id in use. Anything that walks every block reads this rather
/// than naming whichever block happens to be last, which is how the catalogue
/// silently stopped one short of the newest one.
constexpr BlockId kLastBlock = BlockId::BannerRunLast;

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

/// Farmland, wet or dry. Two ids for one block, so the mesher and the crop
/// rules can both ask one question.
constexpr bool isFarmland(BlockId id) {
    return id == BlockId::Farmland || id == BlockId::FarmlandMoist;
}

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

/// The wall the hook is screwed to; the wire runs away from it.
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

/// Everything that carries or answers a signal, in one question.
///
/// **Its own predicate rather than a widening of an existing one.** The
/// temptation is to route redstone through `isOpaque`, and the two disagree on
/// about fifteen blocks - glass, slabs, leaves, the redstone block itself - so
/// sharing them produces exactly the class of bug that compiles, validates and
/// is only ever found by playing.
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
// **sixteen** rotations - the reference's own count, and what lets a sign face
// a path that does not run north - or hangs off a wall at one of four.
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

/// Zero to fifteen standing, sixteen to nineteen against a wall.
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

constexpr bool isChest(BlockId id) {
    return (id >= BlockId::Chest && id <= BlockId::ChestWest) || id == BlockId::Barrel ||
           isTrappedChest(id) || isEnderChest(id) || isStowbox(id);
}

/// Whether two of these standing side by side join into a fifty-four slot
/// container. **Neither a barrel nor an ender chest ever does** - the reference
/// says so outright, and without this the pairing walk would happily join two
/// of either, because its only test is that the neighbour is the same id.
constexpr bool chestPairs(BlockId id) {
    return id != BlockId::Barrel && !isEnderChest(id) && !isStowbox(id);
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
constexpr bool isInteractive(BlockId id) {
    return id == BlockId::CraftingTable || id == BlockId::SmithingTable || isFurnace(id) ||
           isChest(id) || isHopper(id) || id == BlockId::Grindstone || id == BlockId::Anvil ||
           id == BlockId::Stonecutter ||
           id == BlockId::ChippedAnvil || id == BlockId::DamagedAnvil;
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
    if (isCrossBlock(id)) {
        return BlockShape::Cross;
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
        result.boxes[0] = {2 * t, 0.0f, 2 * t, 14 * t, 1.0f, 14 * t};
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
    unsigned char lidTurns = 0;
    /// One side face whose automatic mirroring is to be undone.
    ///
    /// The mesher flips U on half the faces so a texture reads the same way
    /// from every side, which is what a word or a furnace front wants. A bed's
    /// side marks a *world* direction - red mattress at the join, white pillow
    /// at the head - so the flip puts the pillow at the wrong end on exactly
    /// one of the two long sides.
    FaceDirection unmirror = FaceDirection::Unknown;
};

struct ModelBoxes {
    ModelBox boxes[10]{};
    int count = 0;
};

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
constexpr int kRedstoneSpritesFirst = 1157;

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

/// **The brewing run, appended after the redstone one.** Eight ingredients, then
/// every potion three ways - drunk, thrown and on the end of an arrow.
///
/// The forty-one potion pictures are the reference's own bottle with its
/// overlay **tinted at staging time by the effect's colour**, exactly as the
/// redstone wire is: the reference tints at draw time and nothing here can.
constexpr int kBrewingSpritesFirst = kRedstoneSpritesFirst + kRedstoneSprites;
constexpr int kBrewingSprites = 5;
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
/// Four more that a model box needs by name rather than by face, because these
/// blocks paint a *lid* from their side image: an anvil's plinths, a campfire's
/// logs, a scaffold's posts and a hopper's funnel are all the same picture all
/// the way round.
constexpr float kAnvilBodyLayer = kTableLayerBase + 483.0f;
constexpr float kScaffoldSideLayer = kTableLayerBase + 487.0f;
constexpr float kCampfireLogLayer = kTableLayerBase + 534.0f;
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
constexpr float kStonecutterSawLayer = 1154.0f;
/// The compost inside a composter. **The tub's own top texture is a rim with a
/// transparent middle**, and that middle is exactly the rectangle the contents
/// plate samples - so a composter filled to anything below level eight drew
/// nothing at all and read as broken. The reference has this image; it simply
/// had not been staged.
constexpr float kComposterCompostLayer = 1156.0f;

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
/// `0,4.33,0.33` to `16,12.33,1.67`; `template_hanging_sign_rot_0` is a board
/// `1,0,7` to `15,10,9` on two chains. **The standing board's top reaches
/// outside its own cell in the reference and is clipped to it here**, because
/// nothing else in this game draws past a cell wall.
///
/// A banner has no reference model at all - it is a block entity there - so its
/// post and cloth are ours, and that is recorded at `kBannerFamilies`.
constexpr BlockBoxes signBoxes(int kind, FaceDirection facing, bool onWall) {
    constexpr float t = 1.0f / 16.0f;
    constexpr float postLo = 7.33333f / 16.0f;
    constexpr float postHi = 8.66667f / 16.0f;
    const bool alongX = facing == FaceDirection::PosZ || facing == FaceDirection::NegZ;
    BlockBoxes result;

    // How thick the board is and where it sits along the axis it faces.
    const auto board = [&](float lo, float hi, float faceLo, float faceHi) {
        return alongX ? BlockBox{lo, faceLo, postLo, hi, faceHi, postHi}
                      : BlockBox{postLo, faceLo, lo, postHi, faceHi, hi};
    };

    if (onWall) {
        // Against the wall behind it, a texel and a third clear of it.
        constexpr float near = 0.33333f / 16.0f;
        constexpr float far = 1.66667f / 16.0f;
        const float lo = kind == 1 ? 1 * t : 0.0f;
        const float hi = kind == 1 ? 15 * t : 1.0f;
        const float bottom = kind == 1 ? 2 * t : 4.33333f / 16.0f;
        const float top = kind == 1 ? 12 * t : 12.33333f / 16.0f;
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
        result.boxes[0] = board(1 * t, 15 * t, 0.0f, 10 * t);
        result.boxes[1] = board(3 * t, 4 * t, 10 * t, 1.0f);
        result.boxes[2] = board(12 * t, 13 * t, 10 * t, 1.0f);
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
        result.boxes[0] = {{0.0f, 0.0f, 0.0f, 1.0f, 2 * t, 1.0f},
                           0.0f, 14 * t, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f,
                           static_cast<float>(kRedstoneSlabSprite), top};
        result.count = 1;
        // A torch two texels wide, standing on the bench. `along` measures from
        // the **output** end, which is the end the facing points at.
        const FaceDirection facing = repeater ? repeaterFacing(id) : comparatorFacing(id);
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
            // A pair at the output end, five tall, and a single one at the back
            // three tall - which is the comparator's only asymmetry, and is the
            // same in both modes. The mode is told by which torch is lit, never
            // by its height.
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
        const unsigned char turns = lie == FaceDirection::NegZ   ? 0
                                    : lie == FaceDirection::PosX ? 1
                                    : lie == FaceDirection::PosZ ? 2
                                                                 : 3;
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
        result.boxes[0] = {{5 * t, 0.0f, 5 * t, 6 * t, 6 * t, 11 * t},
                           5 * t, 10 * t, 11 * t, 1.0f,
                           5 * t, 5 * t, 11 * t, 11 * t};
        result.boxes[1] = {{10 * t, 0.0f, 5 * t, 11 * t, 6 * t, 11 * t},
                           5 * t, 10 * t, 11 * t, 1.0f,
                           5 * t, 5 * t, 11 * t, 11 * t};
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
        result.count = 5;
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
    // Four logs in a square, the reference's own arrangement. The fire itself
    // is a pair of animated quads there and is left out here.
    if (id == BlockId::Campfire || id == BlockId::SoulCampfire) {
        constexpr float log = kCampfireLogLayer;
        result.boxes[0] = {{1 * t, 0.0f, 0.0f, 5 * t, 4 * t, 1.0f},
                           0.0f, 0.0f, 1.0f, 4 * t,
                           0.0f, 0.0f, 1.0f, 4 * t, log, log};
        result.boxes[1] = {{11 * t, 0.0f, 0.0f, 15 * t, 4 * t, 1.0f},
                           0.0f, 0.0f, 1.0f, 4 * t,
                           0.0f, 0.0f, 1.0f, 4 * t, log, log};
        result.boxes[2] = {{0.0f, 3 * t, 1 * t, 1.0f, 7 * t, 5 * t},
                           0.0f, 4 * t, 1.0f, 8 * t,
                           0.0f, 4 * t, 1.0f, 8 * t, log, log};
        result.boxes[3] = {{0.0f, 3 * t, 11 * t, 1.0f, 7 * t, 15 * t},
                           0.0f, 4 * t, 1.0f, 8 * t,
                           0.0f, 4 * t, 1.0f, 8 * t, log, log};
        result.count = 4;
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
        result.count = 5;
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
        result.count = 1;
        return result;
    }
    // A hollow tub: four walls and a floor. **Drawn hollow is the whole point**
    // - as a full cube its lid showed the rim texture's transparent middle with
    // nothing behind it, and being opaque it culled the faces of everything it
    // touched, so a small window opened through the world around it.
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
        result.boxes[0] = {{0.0f, 0.0f, 0.0f, 1.0f, floorTop, 1.0f},
                           0.0f, 1.0f - floorTop, 1.0f, 1.0f,
                           0.0f, 0.0f, 1.0f, 1.0f, -1.0f, inner};
        result.boxes[1] = {{0.0f, floorTop, 0.0f, 1.0f, 1.0f, wall},
                           0.0f, 0.0f, 1.0f, 1.0f - floorTop,
                           0.0f, 0.0f, 1.0f, wall, -1.0f, rim};
        result.boxes[2] = {{0.0f, floorTop, 1.0f - wall, 1.0f, 1.0f, 1.0f},
                           0.0f, 0.0f, 1.0f, 1.0f - floorTop,
                           0.0f, 1.0f - wall, 1.0f, 1.0f, -1.0f, rim};
        result.boxes[3] = {{0.0f, floorTop, wall, wall, 1.0f, 1.0f - wall},
                           0.0f, 0.0f, 1.0f, 1.0f - floorTop,
                           0.0f, wall, wall, 1.0f - wall, -1.0f, rim};
        result.boxes[4] = {{1.0f - wall, floorTop, wall, 1.0f, 1.0f, 1.0f - wall},
                           0.0f, 0.0f, 1.0f, 1.0f - floorTop,
                           1.0f - wall, wall, 1.0f, 1.0f - wall, -1.0f, rim};
        result.count = 5;
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
            result.boxes[5] = {{wall, fill - 0.5f * t, wall, 1.0f - wall, fill, 1.0f - wall},
                               wall, 1.0f - fill, 1.0f - wall, 1.0f - fill + 0.5f * t,
                               wall, wall, 1.0f - wall, 1.0f - wall,
                               surface, surface};
            result.count = 6;
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
    // the top.
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
    // A rod standing in three separate base plates, with three blades near the
    // top - the reference's own arrangement. It was drawn as two crossed
    // sheets, like a flower, and then as one solid plate with no blades.
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
        // The blades, one over each plate. Painted from the band the texture
        // carries at rows 2-4, which nothing else samples.
        result.boxes[4] = {{9 * t, 11 * t, 7 * t, 14 * t, 12 * t, 9 * t},
                           9 * t, 2 * t, 14 * t, 4 * t,
                           9 * t, 2 * t, 14 * t, 4 * t, rod, rod};
        result.boxes[5] = {{2 * t, 11 * t, 3 * t, 7 * t, 12 * t, 5 * t},
                           2 * t, 2 * t, 7 * t, 4 * t,
                           2 * t, 2 * t, 7 * t, 4 * t, rod, rod};
        result.boxes[6] = {{2 * t, 11 * t, 11 * t, 7 * t, 12 * t, 13 * t},
                           2 * t, 2 * t, 7 * t, 4 * t,
                           2 * t, 2 * t, 7 * t, 4 * t, rod, rod};
        result.count = 7;
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
        // A redstone model is bumped into and aimed at as **the union of what
        // it draws**, worked out from `postModel` rather than written down a
        // second time. The older models above keep their hand-written
        // silhouettes, which are deliberately simpler than their geometry - one
        // box for an anvil's four, one for a campfire's logs.
        if (isRedstoneComponent(id) || isWallTorch(id)) {
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
    return collisionBoxes(id);
}

/// Blocks movement. Water does not - you sink into it, and neither does a plant
/// you walk straight through.
constexpr bool isSolid(BlockId id) {
    // A torch is drawn as a box and walked straight through, which is the one
    // place a `Model` is not solid. Named here rather than given a shape of its
    // own, because everything else about it - the box, its own texture
    // rectangle, not occluding a neighbour - is exactly what a model already is.
    if (isTorchBlock(id)) {
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

/// Whether a slot picture and a dropped item are built from `postModel` rather
/// than from boxes cut out of a cube.
constexpr bool usesModelIcon(BlockShape shape) {
    return shape == BlockShape::Model || shape == BlockShape::Cocoa || shape == BlockShape::Bed;
}

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
    case BlockId::BrewingStand:
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
constexpr bool isDistantDecoration(BlockId id) {
    const BlockShape shape = blockShape(id);
    return (shape == BlockShape::Cross || shape == BlockShape::Vine) && blockLightEmission(id) == 0;
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
    return blockShape(id) == BlockShape::Cross || blockShape(id) == BlockShape::Flat ||
           isTorchBlock(id);
}

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

/// Destroyed and dropped when water spreads into it, rather than damming the
/// flow. The reference's list is plants, snow, torches, carpets and redstone;
/// the torch is named outright because it stopped being cross-shaped when it
/// gained a real model.
constexpr bool isWashedAway(BlockId id) {
    return blockShape(id) == BlockShape::Cross || isVine(id) || isTorchBlock(id);
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
    return id == BlockId::Air || id == BlockId::Glass || isFluid(id) || id == BlockId::Fire ||
           (id >= BlockId::WhiteStainedGlass && id <= BlockId::BlackStainedGlass) ||
           blockShape(id) == BlockShape::Cross || blockShape(id) == BlockShape::Flat ||
           blockShape(id) == BlockShape::Pane || blockShape(id) == BlockShape::Model ||
           blockShape(id) == BlockShape::Ladder || blockShape(id) == BlockShape::Vine ||
           blockShape(id) == BlockShape::Cocoa || blockShape(id) == BlockShape::Fence ||
           blockShape(id) == BlockShape::Wall || blockShape(id) == BlockShape::Gate ||
           blockShape(id) == BlockShape::Trapdoor;
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

/// One more than the highest layer the run uses. **Derived rather than written
/// down**, because nothing else asserts it: too small and the last few blocks
/// silently sample whatever run follows this one, which is a wrong texture with
/// no error anywhere.
///
/// It has to consider the face layers above as well as the rows, because those
/// are reached only from code - and a layer nothing in the table mentions is
/// exactly the one a row-only scan would leave off the end.
constexpr int maxTableLayer() {
    int highest = kBedFirstLayer + kBedColours * 4 + 1;
    for (const ExtraBlockInfo& info : kExtraBlocks) {
        highest = info.layer > highest ? info.layer : highest;
        highest = info.topLayer > highest ? info.topLayer : highest;
    }
    return highest;
}

constexpr int kTableSprites = maxTableLayer() + 1;
static_assert(kTableSprites == 706, "the table's sprite run changed size; update Main.cpp's list");

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

/// The forty-nine appended items - foods, materials and the sixteen dyes - as
/// one contiguous run in `ItemId` order, so an icon is arithmetic rather than a
/// case per item.
constexpr int kExtraItemSpritesFirst = kChestHalfSpritesFirst + kChestHalfSprites;
constexpr int kExtraItemSprites = 166;

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

    if (isBeehive(id)) {
        if (face != BlockFace::Side) {
            return static_cast<float>(kBeehiveEndSprite);
        }        if (direction == beehiveFacing(id) || direction == FaceDirection::Unknown) {
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

// The layer numbers a model box borrows are written out beside the models,
// three thousand lines above the table that owns them - so they are checked
// against it here. **This is what stops them being a second copy that rots**:
// move a row and the build fails rather than the bell quietly turning gold.
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
              kTableLayerBase +
                  static_cast<float>(extraBlockInfo(BlockId::BrewingStand).topLayer));

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
