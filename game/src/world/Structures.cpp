#include "world/Structures.hpp"

#include "world/Biome.hpp"
#include "world/Noise.hpp"
#include "world/TerrainGenerator.hpp"

#include <algorithm>
#include <cmath>

namespace game::structures {
namespace {

constexpr int kMinTrunk = 4;
constexpr int kTrunkVariation = 3;

/// Where a tree stands inside its cell, and how tall it is. Everything is
/// derived from the cell coordinates alone, so any chunk that asks about this
/// cell gets the same tree.
struct Tree {
    int x = 0;
    int z = 0;
    int trunkHeight = 0;
};

bool treeInCell(std::uint32_t seed, int cellX, int cellZ, Tree& out) {
    const float presence = noise::hashUnit2D(seed ^ 0x7ee50001u, cellX, cellZ);

    // Most cells are empty, and finding that out costs one hash. Sampling the
    // biome and surface height first would mean three noise evaluations per
    // candidate for a question already answered.
    if (presence >= maxTreeDensity()) {
        return false;
    }

    // Kept away from the cell edges so neighbouring trees cannot end up touching
    // across a boundary.
    constexpr int margin = 2;
    constexpr int span = kCellSize - 2 * margin;

    const std::uint32_t jitter = noise::hash2D(seed ^ 0x7ee50002u, cellX, cellZ);

    out.x = cellX * kCellSize + margin + static_cast<int>(jitter % static_cast<std::uint32_t>(span));
    out.z = cellZ * kCellSize + margin + static_cast<int>((jitter >> 8) % static_cast<std::uint32_t>(span));
    out.trunkHeight = kMinTrunk + static_cast<int>((jitter >> 16) % kTrunkVariation);

    const BiomeSample sample = sampleBiome(seed, out.x, out.z);
    const Biome& biome = biomeInfo(sample.dominant);
    if (presence >= biome.treeDensity) {
        return false;
    }

    // Nothing grows on bare rock, in the sea, or on a snow cap.
    const int surface = surfaceHeightAt(seed, out.x, out.z);
    if (surface <= kSeaLevel + 1 || surface >= sample.snowLine) {
        return false;
    }
    return biome.top == BlockId::Grass;
}

/// Writes one block if it falls inside this chunk. Out-of-range writes are
/// simply dropped, which is what lets the same tree be built by several chunks.
void place(Chunk& chunk, ChunkCoord coord, int worldX, int worldY, int worldZ, BlockId block, bool onlyIntoAir) {
    const int lx = worldX - coord.x * Chunk::kSize;
    const int ly = worldY - coord.y * Chunk::kSize;
    const int lz = worldZ - coord.z * Chunk::kSize;

    if (!Chunk::contains(lx, ly, lz)) {
        return;
    }
    if (onlyIntoAir && chunk.at(lx, ly, lz) != BlockId::Air) {
        return;
    }
    chunk.set(lx, ly, lz, block);
}

void buildTree(Chunk& chunk, ChunkCoord coord, std::uint32_t seed, const Tree& tree) {
    const int base = surfaceHeightAt(seed, tree.x, tree.z);
    const int top = base + tree.trunkHeight;

    // Canopy first, trunk second, so the trunk is never eaten by its own leaves.
    for (int dy = -2; dy <= 1; ++dy) {
        const int y = top + dy;
        const int radius = dy <= -1 ? 2 : 1;

        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                // Square canopies read as boxes; trimming the corners is what
                // makes them look grown.
                if (std::abs(dx) == radius && std::abs(dz) == radius) {
                    if (radius == 2 || dy == 1) {
                        continue;
                    }
                }
                // The very top is a cross rather than a full layer.
                if (dy == 1 && std::abs(dx) + std::abs(dz) > 1) {
                    continue;
                }
                place(chunk, coord, tree.x + dx, y, tree.z + dz, BlockId::Leaves, true);
            }
        }
    }

    for (int y = base + 1; y <= top; ++y) {
        place(chunk, coord, tree.x, y, tree.z, BlockId::Log, false);
    }
    // Roots the tree, so it never appears to float over a one-block dip.
    place(chunk, coord, tree.x, base, tree.z, BlockId::Dirt, false);
}

} // namespace

void generateInto(Chunk& chunk, std::uint32_t seed, ChunkCoord coord) {
    const int baseX = coord.x * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    // Every cell whose structure could reach into this chunk. The margin is what
    // makes a tree straddling the border come out identical from both sides.
    const int firstCellX = floorDivInt(baseX - kReach, kCellSize);
    const int lastCellX = floorDivInt(baseX + Chunk::kSize + kReach, kCellSize);
    const int firstCellZ = floorDivInt(baseZ - kReach, kCellSize);
    const int lastCellZ = floorDivInt(baseZ + Chunk::kSize + kReach, kCellSize);

    for (int cellZ = firstCellZ; cellZ <= lastCellZ; ++cellZ) {
        for (int cellX = firstCellX; cellX <= lastCellX; ++cellX) {
            Tree tree;
            if (treeInCell(seed, cellX, cellZ, tree)) {
                buildTree(chunk, coord, seed, tree);
            }
        }
    }
}

} // namespace game::structures
