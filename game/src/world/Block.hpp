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
};

constexpr bool isSolid(BlockId id) {
    return id != BlockId::Air;
}

/// Highest light level a source can have. Four bits per channel, so a level fits
/// in a nibble and sky plus block light fit in one byte per block.
constexpr int kMaxLight = 15;

/// How much light a block gives off. Zero for everything that is not a source.
constexpr int blockLightEmission(BlockId id) {
    return id == BlockId::Glowstone ? 14 : 0;
}

/// Whether light passes through. Currently the exact opposite of solid, but kept
/// separate because glass and water will be solid *and* transparent.
constexpr bool isLightTransparent(BlockId id) {
    return id == BlockId::Air;
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
    /// Not a block. Shares the array because the sun is drawn with the same
    /// pipeline, and a texture array needs every layer the same size.
    Sun = 12,
};

inline float blockTextureLayer(BlockId id, BlockFace face) {
    switch (id) {
    case BlockId::Stone:
        return static_cast<float>(TextureLayer::Stone);
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
    case BlockId::Air:
        break;
    }
    return static_cast<float>(TextureLayer::Stone);
}

} // namespace game
