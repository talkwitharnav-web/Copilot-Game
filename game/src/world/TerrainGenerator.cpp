#include "world/TerrainGenerator.hpp"

#include "world/Noise.hpp"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

// Lower frequency means broader landforms. This value gives hills a few dozen
// blocks across rather than noise you have to fly a long way to notice.
constexpr float kTerrainFrequency = 0.010f;
constexpr int kOctaves = 5;

constexpr int kSeaLevel = 22;
constexpr int kMinHeight = 6;
constexpr int kHeightRange = 46;

constexpr int kDirtDepth = 4;

} // namespace

int surfaceHeightAt(std::uint32_t seed, int worldX, int worldZ) {
    const float raw = noise::fbm2D(seed, static_cast<float>(worldX) * kTerrainFrequency,
                                   static_cast<float>(worldZ) * kTerrainFrequency, kOctaves);

    // Raising to a power above 1 pulls mid values down, which widens the flat
    // lowlands and keeps peaks rare instead of leaving everything equally lumpy.
    const float shaped = std::pow(raw, 1.6f);

    return kMinHeight + static_cast<int>(shaped * static_cast<float>(kHeightRange));
}

Chunk generateChunk(std::uint32_t seed, ChunkCoord coord) {
    Chunk chunk;

    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    for (int z = 0; z < Chunk::kSize; ++z) {
        for (int x = 0; x < Chunk::kSize; ++x) {
            const int surface = surfaceHeightAt(seed, baseX + x, baseZ + z);

            // Only the part of this column that falls inside this chunk.
            const int localTop = std::min(surface - baseY, Chunk::kSize - 1);

            for (int y = 0; y <= localTop; ++y) {
                const int worldY = baseY + y;

                BlockId block = BlockId::Stone;
                if (worldY == surface) {
                    block = surface <= kSeaLevel ? BlockId::Sand : BlockId::Grass;
                } else if (worldY > surface - kDirtDepth) {
                    block = BlockId::Dirt;
                }

                chunk.set(x, y, z, block);
            }
        }
    }

    return chunk;
}

} // namespace game
