#include "world/World.hpp"

#include "world/ChunkMesher.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>

namespace game {
namespace {

using Clock = std::chrono::steady_clock;

/// Floor division, correct for negative coordinates. Plain integer division
/// truncates toward zero, which puts blocks at -1 and 0 in the same chunk.
int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

int floorMod(int value, int divisor) {
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

int chebyshevDistance(const ChunkCoord& a, const ChunkCoord& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

} // namespace

World::World(std::uint32_t seed, std::filesystem::path saveRoot)
    : m_seed(seed), m_store(std::move(saveRoot), seed) {}

void World::saveIfModified(const ChunkCoord& coord, ChunkSlot& slot) {
    if (!slot.modified) {
        return;
    }
    m_store.save(coord, slot.blocks);
    slot.modified = false;
    ++m_savedChunkCount;
}

void World::saveAll() {
    for (auto& [coord, slot] : m_chunks) {
        saveIfModified(coord, slot);
    }
}

const Chunk* World::chunkAt(const ChunkCoord& coord) const {
    const auto it = m_chunks.find(coord);
    return it == m_chunks.end() ? nullptr : &it->second.blocks;
}

bool World::hasChunk(const ChunkCoord& coord) const {
    return m_chunks.find(coord) != m_chunks.end();
}

BlockId World::blockAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return BlockId::Air;
    }
    return chunk->at(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

bool World::isSolid(int x, int y, int z) const {
    return game::isSolid(blockAt(x, y, z));
}

int World::highestSolid(int x, int z) const {
    for (int y = kWorldHeightChunks * Chunk::kSize - 1; y >= 0; --y) {
        if (isSolid(x, y, z)) {
            return y;
        }
    }
    return -1;
}

void World::markDirty(const ChunkCoord& coord) {
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    if (std::find(m_pendingMesh.begin(), m_pendingMesh.end(), coord) == m_pendingMesh.end()) {
        m_pendingMesh.push_back(coord);
    }
}

void World::setBlock(int x, int y, int z, BlockId block) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }

    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }

    const int lx = floorMod(x, Chunk::kSize);
    const int ly = floorMod(y, Chunk::kSize);
    const int lz = floorMod(z, Chunk::kSize);

    if (it->second.blocks.at(lx, ly, lz) == block) {
        return;
    }
    it->second.blocks.set(lx, ly, lz, block);
    it->second.modified = true;

    markDirty(coord);

    // Only a block on a chunk face can change a neighbour's mesh, but missing
    // those neighbours leaves holes at chunk seams.
    constexpr int last = Chunk::kSize - 1;
    if (lx == 0) {
        markDirty({coord.x - 1, coord.y, coord.z});
    }
    if (lx == last) {
        markDirty({coord.x + 1, coord.y, coord.z});
    }
    if (ly == 0) {
        markDirty({coord.x, coord.y - 1, coord.z});
    }
    if (ly == last) {
        markDirty({coord.x, coord.y + 1, coord.z});
    }
    if (lz == 0) {
        markDirty({coord.x, coord.y, coord.z - 1});
    }
    if (lz == last) {
        markDirty({coord.x, coord.y, coord.z + 1});
    }
}

bool World::neighboursLoaded(const ChunkCoord& coord) const {
    return hasChunk({coord.x - 1, coord.y, coord.z}) && hasChunk({coord.x + 1, coord.y, coord.z}) &&
           hasChunk({coord.x, coord.y, coord.z - 1}) && hasChunk({coord.x, coord.y, coord.z + 1});
}

engine::MeshData World::meshOne(const ChunkCoord& coord) const {
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return {};
    }

    ChunkNeighbours neighbours;
    neighbours.negativeX = chunkAt({coord.x - 1, coord.y, coord.z});
    neighbours.positiveX = chunkAt({coord.x + 1, coord.y, coord.z});
    neighbours.negativeY = chunkAt({coord.x, coord.y - 1, coord.z});
    neighbours.positiveY = chunkAt({coord.x, coord.y + 1, coord.z});
    neighbours.negativeZ = chunkAt({coord.x, coord.y, coord.z - 1});
    neighbours.positiveZ = chunkAt({coord.x, coord.y, coord.z + 1});

    const glm::vec3 origin{static_cast<float>(coord.x * Chunk::kSize), static_cast<float>(coord.y * Chunk::kSize),
                           static_cast<float>(coord.z * Chunk::kSize)};

    return meshChunk(*chunk, neighbours, origin);
}

void World::refreshQueues(const ChunkCoord& centre) {
    m_pendingLoad.clear();

    for (int dz = -kLoadRadiusChunks; dz <= kLoadRadiusChunks; ++dz) {
        for (int dx = -kLoadRadiusChunks; dx <= kLoadRadiusChunks; ++dx) {
            for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
                const ChunkCoord coord{centre.x + dx, cy, centre.z + dz};
                if (!hasChunk(coord)) {
                    m_pendingLoad.push_back(coord);
                }
            }
        }
    }

    // Sorted farthest-first so the nearest chunk is at the back, where removing
    // it is free. Draining from the front would be quadratic.
    std::sort(m_pendingLoad.begin(), m_pendingLoad.end(), [&](const ChunkCoord& a, const ChunkCoord& b) {
        return chebyshevDistance(a, centre) > chebyshevDistance(b, centre);
    });

    // Chunks that exist but were never meshed: they were dropped from the queue
    // because a neighbour was missing or because they sat outside the visible
    // radius. Nothing else would ever pick them up again, which shows as a
    // permanent hole when the player walks back toward them.
    for (const auto& [coord, slot] : m_chunks) {
        if (!slot.meshed && chebyshevDistance(coord, centre) <= kVisibleRadiusChunks) {
            markDirty(coord);
        }
    }
}

std::vector<ChunkMeshUpdate> World::update(const glm::vec3& playerPosition, float budgetSeconds) {
    const auto start = Clock::now();
    std::vector<ChunkMeshUpdate> updates;

    const ChunkCoord centre{floorDiv(static_cast<int>(std::floor(playerPosition.x)), Chunk::kSize), 0,
                            floorDiv(static_cast<int>(std::floor(playerPosition.z)), Chunk::kSize)};

    if (!m_hasCentre || centre.x != m_centre.x || centre.z != m_centre.z) {
        m_centre = centre;
        m_hasCentre = true;

        // Unload first so memory is released before anything new is allocated.
        for (auto it = m_chunks.begin(); it != m_chunks.end();) {
            if (chebyshevDistance(it->first, centre) > kUnloadRadiusChunks) {
                // Must happen before the erase, or an edited chunk is lost the
                // moment the player walks away from it.
                saveIfModified(it->first, it->second);

                if (it->second.meshed) {
                    updates.push_back(ChunkMeshUpdate{it->first, {}, true});
                }
                it = m_chunks.erase(it);
            } else {
                ++it;
            }
        }

        // Queued work for chunks that no longer exist or are out of range would
        // otherwise pile up forever as the player walks.
        const auto outOfRange = [&](const ChunkCoord& c) {
            return chebyshevDistance(c, centre) > kUnloadRadiusChunks;
        };
        m_pendingMesh.erase(std::remove_if(m_pendingMesh.begin(), m_pendingMesh.end(), outOfRange),
                            m_pendingMesh.end());

        refreshQueues(centre);
    }

    const auto budgetSpent = [&] {
        return std::chrono::duration<float>(Clock::now() - start).count() >= budgetSeconds;
    };

    while (!m_pendingLoad.empty() && !budgetSpent()) {
        const ChunkCoord coord = m_pendingLoad.back();
        m_pendingLoad.pop_back();

        if (hasChunk(coord)) {
            continue;
        }

        // A saved chunk replaces generation entirely: it already contains the
        // generated terrain plus whatever the player did to it.
        if (std::optional<Chunk> stored = m_store.load(coord)) {
            m_chunks.emplace(coord, ChunkSlot{std::move(*stored), false, false});
        } else {
            m_chunks.emplace(coord, ChunkSlot{generateChunk(m_seed, coord), false, false});
        }

        // The new chunk and its neighbours may all have gained or lost visible
        // faces along the shared border.
        markDirty(coord);
        markDirty({coord.x - 1, coord.y, coord.z});
        markDirty({coord.x + 1, coord.y, coord.z});
        markDirty({coord.x, coord.y, coord.z - 1});
        markDirty({coord.x, coord.y, coord.z + 1});
    }

    while (!m_pendingMesh.empty() && !budgetSpent()) {
        const ChunkCoord coord = m_pendingMesh.back();
        m_pendingMesh.pop_back();

        const auto it = m_chunks.find(coord);
        if (it == m_chunks.end()) {
            continue;
        }
        if (chebyshevDistance(coord, centre) > kVisibleRadiusChunks || !neighboursLoaded(coord)) {
            continue;
        }

        it->second.meshed = true;
        updates.push_back(ChunkMeshUpdate{coord, meshOne(coord), false});
    }

    return updates;
}

std::vector<ChunkMeshUpdate> World::loadImmediately(const glm::vec3& position) {
    std::vector<ChunkMeshUpdate> all;

    // A budget large enough that nothing is deferred, repeated until both
    // queues drain. Startup is the one place a stall is preferable to popping.
    for (int pass = 0; pass < 4; ++pass) {
        std::vector<ChunkMeshUpdate> batch = update(position, 1000.0f);
        all.insert(all.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
        if (m_pendingLoad.empty() && m_pendingMesh.empty()) {
            break;
        }
    }

    return all;
}

} // namespace game
