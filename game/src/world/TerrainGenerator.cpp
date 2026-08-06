#include "world/TerrainGenerator.hpp"

#include "world/Biome.hpp"
#include "world/Noise.hpp"
#include "world/Structures.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

// Lower frequency means broader landforms. This value gives hills a few dozen
// blocks across rather than noise you have to fly a long way to notice.
constexpr float kTerrainFrequency = 0.010f;
constexpr int kOctaves = 5;

// Caves are carved where a 3D noise field passes close to a chosen value, which
// gives connected winding tunnels. Thresholding the field directly instead would
// give disconnected blobs, which read as holes rather than as caves.
//
// Frequency matters more than width for cost: geometry tracks cave *surface
// area*, and narrow tunnels have more surface per unit volume than open
// caverns. Halving the width barely moved the triangle count; halving the
// frequency did.
constexpr float kCaveFrequency = 0.018f;
constexpr int kCaveOctaves = 2;
constexpr float kCaveCentre = 0.5f;
constexpr float kCaveWidth = 0.038f;

/// No caves within this distance of the surface, fading in below it. Without it
/// tunnels breach open ground constantly and the landscape reads as rotten
/// rather than as hollow.
constexpr int kCaveSurfaceMargin = 6;
constexpr int kCaveFadeDepth = 10;

/// The world's floor is never carved, so there is always something to stand on.
constexpr int kBedrockHeight = 2;

/// Fraction of grassy surface cells carrying a tuft. Gated on the surface block
/// rather than on a biome column, so it follows wherever grass actually ends up.
constexpr float kTallGrassDensity = 0.12f;

/// Share of the ground cover that comes up a flower instead of grass. A second
/// roll on a cell that already won the first, so flowers thin the grass out
/// rather than adding to it and the meadow keeps its density.
constexpr float kFlowerShare = 0.18f;

constexpr float kDeadBushDensity = 0.02f;

/// How far the sand of a desert turns to sandstone before it reaches stone.
constexpr int kSandstoneDepth = 3;

/// Stone gives way to deepslate below this, the way the deep rock does in the
/// reference. Sea level is 24, so this is well under any seabed.
constexpr int kDeepslateTop = 12;

/// Where each ore appears and how much of the rock it takes.
///
/// A smooth 3D field thresholded high gives small connected blobs, which is
/// what a vein looks like; a per-cell roll would scatter single blocks and read
/// as speckle. Same mechanism as the caves, at a much higher frequency so the
/// features are metres rather than tens of metres across.
struct OreVein {
    BlockId block;
    std::uint32_t salt;
    float frequency;
    float threshold;
    int minY;
    int maxY;
};

/// **Rarest first**: the first match wins, so a common ore can never overwrite a
/// scarce one where their depth bands overlap.
///
/// Bands and rarities are the wiki's, mapped onto our world: it runs y 0-96
/// with sea level 24, against the reference's -64 to 320 with sea level 63, so
/// depths below sea level compress by about 0.17 and heights above it by 0.28.
/// The thresholds come from that share of rock via `t = 1 - sqrt(share)`, which
/// holds because a single octave of value noise is near enough triangular.
constexpr std::array<OreVein, 8> kOreVeins{{
    {BlockId::EmeraldOre, 0x51c3b7u, 0.17f, 0.968f, 10, 60},
    {BlockId::DiamondOre, 0x2f9a41u, 0.17f, 0.948f, 3, 16},
    {BlockId::LapisOre, 0x7b31d9u, 0.16f, 0.941f, 3, 24},
    {BlockId::GoldOre, 0x1de4a3u, 0.16f, 0.941f, 3, 19},
    {BlockId::RedstoneOre, 0x64b8f2u, 0.16f, 0.929f, 3, 16},
    {BlockId::IronOre, 0x3ac05eu, 0.15f, 0.900f, 3, 26},
    {BlockId::CopperOre, 0x9e271bu, 0.15f, 0.890f, 10, 38},
    {BlockId::CoalOre, 0x0c7d86u, 0.14f, 0.866f, 13, 90},
}};

/// The ore a cell of rock turns into, or the rock itself.
BlockId oreAt(std::uint32_t seed, int worldX, int worldY, int worldZ, BlockId rock) {
    for (const OreVein& vein : kOreVeins) {
        if (worldY < vein.minY || worldY > vein.maxY) {
            continue;
        }
        const float field = noise::value3D(seed ^ vein.salt, static_cast<float>(worldX) * vein.frequency,
                                           static_cast<float>(worldY) * vein.frequency,
                                           static_cast<float>(worldZ) * vein.frequency);
        if (field > vein.threshold) {
            return vein.block;
        }
    }
    return rock;
}

/// True where a cave should hollow out the rock.
bool isCave(std::uint32_t seed, int worldX, int worldY, int worldZ, int surfaceHeight) {
    if (worldY <= kBedrockHeight) {
        return false;
    }

    const int depth = surfaceHeight - worldY;
    if (depth < kCaveSurfaceMargin) {
        return false;
    }

    const float field = noise::fbm3D(seed ^ 0x5eed1234u, static_cast<float>(worldX) * kCaveFrequency,
                                     static_cast<float>(worldY) * kCaveFrequency * 1.6f,
                                     static_cast<float>(worldZ) * kCaveFrequency, kCaveOctaves);

    // Tunnels widen with depth, so the surface stays mostly intact while deep
    // rock becomes genuinely open.
    const float fade =
        std::clamp(static_cast<float>(depth - kCaveSurfaceMargin) / static_cast<float>(kCaveFadeDepth), 0.0f, 1.0f);

    return std::abs(field - kCaveCentre) < kCaveWidth * fade;
}

} // namespace

int surfaceHeightAt(std::uint32_t seed, int worldX, int worldZ) {
    const BiomeSample biome = sampleBiome(seed, worldX, worldZ);

    const float raw = noise::fbm2D(seed, static_cast<float>(worldX) * kTerrainFrequency,
                                   static_cast<float>(worldZ) * kTerrainFrequency, kOctaves);

    // Raising to a power above 1 pulls mid values down, which widens the flat
    // lowlands and keeps peaks rare instead of leaving everything equally lumpy.
    const float shaped = std::pow(raw, 1.6f);

    return static_cast<int>(biome.baseHeight + shaped * biome.amplitude);
}

Chunk generateChunk(std::uint32_t seed, ChunkCoord coord) {
    Chunk chunk;
    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    for (int z = 0; z < Chunk::kSize; ++z) {
        for (int x = 0; x < Chunk::kSize; ++x) {
            const int worldX = baseX + x;
            const int worldZ = baseZ + z;

            const int surface = surfaceHeightAt(seed, worldX, worldZ);
            const BiomeSample sample = sampleBiome(seed, worldX, worldZ);
            const Biome& biome = biomeInfo(sample.dominant);

            // Altitude overrides the biome's own surface, which is what makes a
            // mountain read as a mountain rather than as tall grass. Anything
            // just under the waterline becomes shore instead.
            BlockId top = biome.top;
            if (surface >= sample.snowLine) {
                top = BlockId::Snow;
            } else if (surface <= kSeaLevel + 1) {
                top = BlockId::Sand;
            }

            // Only the part of this column that falls inside this chunk.
            const int localTop = std::min(surface - baseY, Chunk::kSize - 1);

            for (int y = 0; y <= localTop; ++y) {
                const int worldY = baseY + y;

                if (isCave(seed, worldX, worldY, worldZ, surface)) {
                    continue;
                }

                BlockId block = BlockId::Stone;
                if (worldY <= kBedrockHeight) {
                    // Checked first, so the floor wins even where terrain is
                    // low enough that the surface would otherwise claim it.
                    block = BlockId::Bedrock;
                } else if (worldY == surface) {
                    block = top;
                } else if (worldY > surface - biome.fillerDepth) {
                    block = biome.filler;
                } else if (biome.filler == BlockId::Sand &&
                           worldY > surface - biome.fillerDepth - kSandstoneDepth) {
                    // Sand sits on sandstone rather than straight on stone.
                    // Keyed off the filler so it needs no biome name.
                    block = BlockId::Sandstone;
                } else if (worldY <= kDeepslateTop) {
                    block = BlockId::Deepslate;
                }

                if (block == BlockId::Stone || block == BlockId::Deepslate) {
                    block = oreAt(seed, worldX, worldY, worldZ, block);
                }

                chunk.set(x, y, z, block);
            }

            // Everything still empty below sea level is ocean. Filling after the
            // solid pass means caves that reach under the waterline flood, which
            // is both correct and free. Sea water is all source, so it never
            // drains into whatever the player digs.
            const int waterTop = std::min(kSeaLevel - baseY, Chunk::kSize - 1);
            for (int y = 0; y <= waterTop; ++y) {
                if (chunk.at(x, y, z) == BlockId::Air) {
                    chunk.set(x, y, z, BlockId::Water0);
                }
            }

            // Ground cover, on whatever the surface turned out to be rather than
            // on the biome's nominal top block: a snow line or a shoreline may
            // already have overridden it.
            const int plantY = surface + 1 - baseY;
            if (plantY >= 0 && plantY < Chunk::kSize && surface > kSeaLevel + 1 &&
                chunk.at(x, plantY, z) == BlockId::Air) {
                if (top == BlockId::Grass &&
                    noise::hashUnit2D(seed ^ 0x91a5eedu, worldX, worldZ) < kTallGrassDensity) {
                    const float pick = noise::hashUnit2D(seed ^ 0x5f3aa17u, worldX, worldZ);
                    BlockId cover = BlockId::TallGrass;
                    if (pick < kFlowerShare) {
                        cover = pick < kFlowerShare * 0.5f ? BlockId::Dandelion : BlockId::Poppy;
                    }
                    chunk.set(x, plantY, z, cover);
                } else if (top == BlockId::Sand && surface > kSeaLevel + 2 &&
                           noise::hashUnit2D(seed ^ 0x2c9b4d1u, worldX, worldZ) < kDeadBushDensity) {
                    // Clear of the shoreline, so a beach does not sprout scrub.
                    chunk.set(x, plantY, z, BlockId::DeadBush);
                }
            }
        }
    }

    structures::generateInto(chunk, seed, coord);

    return chunk;
}

} // namespace game
