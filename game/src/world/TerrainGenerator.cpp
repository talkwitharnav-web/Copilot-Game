#include "world/TerrainGenerator.hpp"

#include "world/Biome.hpp"
#include "world/Noise.hpp"
#include "world/Structures.hpp"

#include <algorithm>
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
                if (worldY == surface) {
                    block = top;
                } else if (worldY > surface - biome.fillerDepth) {
                    block = biome.filler;
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
        }
    }

    structures::generateInto(chunk, seed, coord);

    return chunk;
}

} // namespace game
