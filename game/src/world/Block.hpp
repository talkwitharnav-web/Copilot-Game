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
};

constexpr bool isFurnace(BlockId id) {
    return id == BlockId::Furnace || id == BlockId::FurnaceLit;
}

/// Opens a screen when it is right-clicked, rather than being placed against.
constexpr bool isInteractive(BlockId id) {
    return id == BlockId::CraftingTable || isFurnace(id);
}

/// Highest flowing level. Water at this depth cannot spread any further, which
/// is what stops a single source flooding the world.
constexpr int kMaxWaterLevel = 7;

constexpr bool isWater(BlockId id) {
    return id >= BlockId::Water0 && id <= BlockId::Water7;
}

/// 0 for a source, rising as the flow thins out.
constexpr int waterLevel(BlockId id) {
    return isWater(id) ? static_cast<int>(id) - static_cast<int>(BlockId::Water0) : kMaxWaterLevel + 1;
}

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
    return id == BlockId::Leaves || id == BlockId::TallGrass || id == BlockId::Torch;
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
    if (id == BlockId::TallGrass || id == BlockId::Torch) {
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
    if (id == BlockId::FurnaceLit) {
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
    return id == BlockId::Air || isWater(id) || blockShape(id) == BlockShape::Cross;
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
    /// One spawn egg per creature, **in `CreatureKind` order**, which is what
    /// lets an egg's item id, its sprite layer and the species it produces all
    /// be the same offset from their respective firsts. Only the first is
    /// named: the rest are reached by adding the species index, and a name for
    /// each would be thirty-six chances to get the order wrong.
    SpawnEggFirst = 38,
};

/// How many sprite layers the spawn egg run occupies. Kept beside the enum
/// because the loader has to reserve exactly this many, and `Creature.hpp`
/// static-asserts it against `CreatureKind::Count`.
constexpr int kSpawnEggLayers = 36;

inline float blockTextureLayer(BlockId id, BlockFace face) {
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
        // The reference puts the tooled face on two sides and a plainer one on
        // the other two, which needs a face direction the mesher does not
        // supply. One texture on all four sides is the deliberate simplification
        // - `CraftingTableSide` exists for when `BlockFace` gains a direction.
        switch (face) {
        case BlockFace::Top:
            return static_cast<float>(TextureLayer::CraftingTableTop);
        case BlockFace::Bottom:
            return static_cast<float>(TextureLayer::Planks);
        case BlockFace::Side:
            return static_cast<float>(TextureLayer::CraftingTableFront);
        }
        return static_cast<float>(TextureLayer::CraftingTableFront);
    case BlockId::Furnace:
    case BlockId::FurnaceLit:
        // Same simplification as the crafting table: the mesher cannot say
        // which way a side face points, so the mouth appears on all four.
        switch (face) {
        case BlockFace::Top:
        case BlockFace::Bottom:
            return static_cast<float>(TextureLayer::FurnaceTop);
        case BlockFace::Side:
            return static_cast<float>(id == BlockId::FurnaceLit ? TextureLayer::FurnaceFrontLit
                                                                : TextureLayer::FurnaceFront);
        }
        return static_cast<float>(TextureLayer::FurnaceFront);
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
    case BlockId::Water0:
    case BlockId::Water1:
    case BlockId::Water2:
    case BlockId::Water3:
    case BlockId::Water4:
    case BlockId::Water5:
    case BlockId::Water6:
    case BlockId::Water7:
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
    default:
        return "Air";
    }
}

} // namespace game
