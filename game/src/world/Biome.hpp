#pragma once

#include "world/Block.hpp"

#include <cstdint>

namespace game {

/// Which region of the world a column belongs to.
///
/// A biome is the answer to "which block goes here, and why". Without one, every
/// new block type needs another hardcoded height comparison, and those start
/// contradicting each other. The player-facing variety is a consequence of that
/// structure, not its purpose.
enum class BiomeId : std::uint8_t {
    Ocean,
    Beach,
    Plains,
    Desert,
    Rocky,
    Mountains,
    SnowyPeaks,
    Count,
};

/// Everything below this fills with water. Terrain that dips under it becomes
/// seabed rather than a dry pit.
constexpr int kSeaLevel = 24;

/// Everything that varies between regions, in one row per biome.
///
/// Adding a block type to the world should mean adding or editing a row here,
/// never adding a branch to the generator.
struct Biome {
    const char* name;

    /// The exposed block, what sits underneath it, and how deep that goes.
    BlockId top;
    BlockId filler;
    int fillerDepth;

    /// Terrain shaping, in blocks. `base` is the lowest this region reaches and
    /// `amplitude` how far above that it can rise.
    float baseHeight;
    float amplitude;

    /// Above this height the surface turns to snow regardless of `top`. Warm
    /// biomes set it out of reach rather than special-casing them elsewhere.
    int snowLine;

    /// Where this biome sits in selection space, both in [0, 1]. Regions are
    /// chosen by proximity in that space, so these are centres rather than
    /// boundaries and nothing has to define an edge.
    float temperature;
    float humidity;
};

const Biome& biomeInfo(BiomeId id);

/// Terrain parameters at a column, blended across every nearby biome.
///
/// Height must be blended or neighbouring regions meet at a cliff. The surface
/// blocks come from the single strongest biome instead, because a blend of two
/// block types is not a thing — the boundary still looks natural because the
/// selection noise makes it a wandering contour rather than a straight line.
struct BiomeSample {
    BiomeId dominant;
    float baseHeight;
    float amplitude;
    int snowLine;
};

BiomeSample sampleBiome(std::uint32_t seed, int worldX, int worldZ);

} // namespace game
