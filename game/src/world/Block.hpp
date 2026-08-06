#pragma once

#include <cstdint>

namespace game {

/// One byte per block: a 32-cubed chunk is then 32 KB, small enough to copy
/// cheaply when meshing moves to a worker thread.
enum class BlockId : std::uint8_t {
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
};

/// The highest id in use. Anything that walks every block reads this rather
/// than naming whichever block happens to be last, which is how the catalogue
/// silently stopped one short of the newest one.
constexpr BlockId kLastBlock = BlockId::WaterFalling;

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

constexpr bool isFurnace(BlockId id) {
    return id == BlockId::Furnace || id == BlockId::FurnaceLit ||
           (id >= BlockId::FurnaceEast && id <= BlockId::FurnaceWestLit);
}

constexpr bool isFurnaceLit(BlockId id) {
    return id == BlockId::FurnaceLit || id == BlockId::FurnaceEastLit ||
           id == BlockId::FurnaceSouthLit || id == BlockId::FurnaceWestLit;
}

/// Which way this furnace's mouth points.
constexpr FaceDirection furnaceFacing(BlockId id) {
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

/// The furnace that faces this way and is lit or not. **The one place the two
/// halves of a furnace's identity are combined**, so lighting one can never
/// quietly turn it to face north.
constexpr BlockId furnaceFacing(FaceDirection facing, bool lit) {
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

/// Opens a screen when it is right-clicked, rather than being placed against.
constexpr bool isInteractive(BlockId id) {
    return id == BlockId::CraftingTable || isFurnace(id);
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

/// Drawn in the *opaque* pass, but with its fully transparent pixels thrown
/// away by the shader. Not the same thing as translucent: nothing is blended,
/// depth is still written, and so no sorting is needed.
constexpr bool isCutout(BlockId id) {
    return id == BlockId::Leaves || id == BlockId::TallGrass || id == BlockId::Torch ||
           id == BlockId::Glass || id == BlockId::Dandelion || id == BlockId::Poppy ||
           id == BlockId::DeadBush;
}

/// Which way a stair's low step faces. The tall half sits on the opposite side.
enum class Facing : std::uint8_t {
    North = 0, // -Z
    East = 1,  // +X
    South = 2, // +Z
    West = 3,  // -X
};

constexpr bool isStairs(BlockId id) {
    return id >= BlockId::CobbleStairs0 && id <= BlockId::CobbleStairs7;
}

constexpr bool isSlab(BlockId id) {
    return id == BlockId::StoneSlab || id == BlockId::StoneSlabTop;
}

/// Whether a half block occupies the upper half of its cell.
constexpr bool isUpperHalf(BlockId id) {
    return id == BlockId::StoneSlabTop;
}

constexpr Facing stairFacing(BlockId id) {
    return static_cast<Facing>((static_cast<int>(id) - static_cast<int>(BlockId::CobbleStairs0)) & 3);
}

/// Upside-down stairs, with the full half on top.
constexpr bool stairIsTop(BlockId id) {
    return ((static_cast<int>(id) - static_cast<int>(BlockId::CobbleStairs0)) & 4) != 0;
}

constexpr BlockId stairsAt(Facing facing, bool top) {
    return static_cast<BlockId>(static_cast<int>(BlockId::CobbleStairs0) + static_cast<int>(facing) +
                                (top ? 4 : 0));
}

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
};

constexpr BlockShape blockShape(BlockId id) {
    if (id == BlockId::Air || isWater(id)) {
        return BlockShape::Empty;
    }
    if (id == BlockId::TallGrass || id == BlockId::Torch || id == BlockId::Dandelion ||
        id == BlockId::Poppy || id == BlockId::DeadBush) {
        return BlockShape::Cross;
    }
    if (id == BlockId::StoneSlab || id == BlockId::StoneSlabTop) {
        return BlockShape::Slab;
    }
    if (isStairs(id)) {
        return BlockShape::Stairs;
    }
    if (id == BlockId::PlanksFence) {
        return BlockShape::Fence;
    }
    return BlockShape::Full;
}

/// Axis-aligned box in block-local space, each axis running 0 to 1.
struct BlockBox {
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
};

/// Every box a block occupies. Nine covers the widest case, a fence drawn with
/// two rails toward each of four neighbours plus its post.
struct BlockBoxes {
    BlockBox boxes[9]{};
    int count = 0;
};

/// Which sides a neighbour-aware block reaches toward.
enum ConnectionBits : std::uint8_t {
    ConnectNorth = 1, // -Z
    ConnectEast = 2,  // +X
    ConnectSouth = 4, // +Z
    ConnectWest = 8,  // -X
    ConnectAll = 15,
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
    return collisionBoxes(id);
}

/// Blocks movement. Water does not — you sink into it, and neither does a plant
/// you walk straight through.
constexpr bool isSolid(BlockId id) {
    const BlockShape shape = blockShape(id);
    return shape == BlockShape::Full || shape == BlockShape::Slab || shape == BlockShape::Stairs ||
           shape == BlockShape::Fence;
}

/// Hides whatever is behind it. Kept separate from `isSolid` because water is
/// neither solid nor invisible, and conflating the two is how you end up either
/// walking on water or unable to see the seabed.
constexpr bool isOpaque(BlockId id) {
    return blockShape(id) == BlockShape::Full && !isCutout(id);
}

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
    if (id == BlockId::Torch) {
        return 14;
    }
    return id == BlockId::Glowstone ? 14 : 0;
}

/// Falls if whatever it was standing on goes away. True for the flat things
/// that have nothing to hold themselves up with.
constexpr bool needsSupportBelow(BlockId id) {
    return blockShape(id) == BlockShape::Cross;
}

/// Destroyed and dropped when water spreads into it, rather than damming the
/// flow. The reference's list is plants, snow, torches, carpets and redstone;
/// ours is everything cross-shaped, which is exactly that set today.
constexpr bool isWashedAway(BlockId id) {
    return blockShape(id) == BlockShape::Cross;
}

/// Whether light passes through. Currently the exact opposite of solid, but kept
/// separate because glass and water will be solid *and* transparent.
constexpr bool isLightTransparent(BlockId id) {
    return id == BlockId::Air || isWater(id) || isCutout(id);
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
    // the whole point of building with it.
    return id == BlockId::Air || id == BlockId::Glass || isWater(id) ||
           blockShape(id) == BlockShape::Cross;
}

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
constexpr int kExtraSpawnEggLayers = 20;

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
    if (id == BlockId::CraftingTable) {
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

/// Which layer of the texture array a face of this block samples.
///
/// `direction` only matters to blocks whose sides are not all alike, and
/// defaults to `Unknown` so every caller that has no direction to give - item
/// icons, the drop mesh, the hotbar - keeps working untouched.
inline float blockTextureLayer(BlockId id, BlockFace face,
                               FaceDirection direction = FaceDirection::Unknown) {
    if (isStairs(id)) {
        return static_cast<float>(TextureLayer::Cobblestone);
    }
    switch (id) {
    case BlockId::Stone:
        return static_cast<float>(TextureLayer::Stone);
    case BlockId::TallGrass:
        return static_cast<float>(TextureLayer::TallGrass);
    case BlockId::Torch:
        return static_cast<float>(TextureLayer::Torch);
    case BlockId::StoneSlab:
        return static_cast<float>(TextureLayer::Stone);
    case BlockId::StoneSlabTop:
        return static_cast<float>(TextureLayer::Stone);
    case BlockId::PlanksFence:
        return static_cast<float>(TextureLayer::Planks);
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
    if (isStairs(id)) {
        return "Cobblestone Stairs";
    }
    if (isWater(id)) {
        return "Water";
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
    case BlockId::StoneSlab:
    case BlockId::StoneSlabTop:
        return "Stone Slab";
    case BlockId::PlanksFence:
        return "Fence";
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
    default:
        return "Air";
    }
}

} // namespace game
