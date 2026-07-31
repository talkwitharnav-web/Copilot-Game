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

/// The six directions light travels.
constexpr std::array<glm::ivec3, 6> kLightSteps{glm::ivec3{1, 0, 0},  glm::ivec3{-1, 0, 0}, glm::ivec3{0, 1, 0},
                                                glm::ivec3{0, -1, 0}, glm::ivec3{0, 0, 1},  glm::ivec3{0, 0, -1}};

/// The four directions water spreads sideways.
constexpr std::array<glm::ivec3, 4> kFlowSteps{glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, 0, 1},
                                               glm::ivec3{0, 0, -1}};

/// Everything a mesh job reads, allocated once and shared with the job.
///
/// Captured by `shared_ptr` rather than by value: a lambda holding a whole chunk
/// gets copied into the `std::function` that carries it, so the volume would be
/// copied twice per job. A pointer-sized capture also fits inside
/// `std::function` without a second heap allocation.
struct MeshJobInput {
    ChunkCoord coord;
    std::uint32_t revision = 0;
    glm::vec3 origin{0.0f};
    ChunkVolume volume;
};

} // namespace

World::World(std::uint32_t seed, std::filesystem::path saveRoot, engine::JobSystem& jobs, int visibleRadiusChunks)
    : m_seed(seed), m_visibleRadius(std::max(1, visibleRadiusChunks)), m_loadRadius(m_visibleRadius + 1),
      m_unloadRadius(m_loadRadius + 2),
      m_store(std::make_shared<WorldStore>(std::move(saveRoot), seed)), m_jobs(jobs),
      m_results(std::make_shared<JobResults>()) {}

/// Jobs hold `shared_ptr`s to the results buffer and the store, so anything
/// still running here keeps what it touches alive and simply writes into a
/// buffer nobody will read. Nothing needs to be waited for.
World::~World() = default;

int World::skyLightAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    // Unloaded reads as full sky, so the frontier does not darken as it streams.
    if (chunk == nullptr) {
        return kMaxLight;
    }
    return chunk->skyLightAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

int World::blockLightAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return 0;
    }
    return chunk->blockLightAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

void World::setSkyLightAt(int x, int y, int z, int level) {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    it->second.blocks.setSkyLight(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize),
                                  level);
    lightChangedAt(x, y, z);
}

void World::setBlockLightAt(int x, int y, int z, int level) {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    it->second.blocks.setBlockLight(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize),
                                    level);
    lightChangedAt(x, y, z);
}

/// Records that a chunk's geometry needs rebuilding because its light moved.
///
/// Only collects coordinates; the actual invalidation happens once per frame in
/// `flushLightDirty`. Propagation touches tens of thousands of cells, and
/// invalidating per cell would mean a linear scan of the pending queue each time.
void World::lightChangedAt(int x, int y, int z) {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    m_lightDirty.insert(coord);

    // A lit cell on a chunk face shades the neighbour's geometry too.
    const int lx = floorMod(x, Chunk::kSize);
    const int ly = floorMod(y, Chunk::kSize);
    const int lz = floorMod(z, Chunk::kSize);
    constexpr int last = Chunk::kSize - 1;

    if (lx == 0) {
        m_lightDirty.insert({coord.x - 1, coord.y, coord.z});
    }
    if (lx == last) {
        m_lightDirty.insert({coord.x + 1, coord.y, coord.z});
    }
    if (ly == 0) {
        m_lightDirty.insert({coord.x, coord.y - 1, coord.z});
    }
    if (ly == last) {
        m_lightDirty.insert({coord.x, coord.y + 1, coord.z});
    }
    if (lz == 0) {
        m_lightDirty.insert({coord.x, coord.y, coord.z - 1});
    }
    if (lz == last) {
        m_lightDirty.insert({coord.x, coord.y, coord.z + 1});
    }
}

void World::flushLightDirty() {
    for (const ChunkCoord& coord : m_lightDirty) {
        invalidateMesh(coord);
    }
    m_lightDirty.clear();
}

bool World::columnLoaded(int chunkX, int chunkZ) const {
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        if (!hasChunk({chunkX, cy, chunkZ})) {
            return false;
        }
    }
    return true;
}

void World::seedColumnLight(int chunkX, int chunkZ) {
    const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
                              static_cast<std::uint32_t>(chunkZ);
    if (!m_litColumns.insert(key).second) {
        return;
    }

    constexpr int worldTop = kWorldHeightChunks * Chunk::kSize - 1;
    constexpr int size = Chunk::kSize;

    // Writes go straight into the chunks rather than through `setSkyLightAt`:
    // this touches ~98,000 cells per column, and marking dirty per cell would
    // dwarf the actual work. The whole column is marked once at the end.
    std::array<ChunkSlot*, kWorldHeightChunks> column{};
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        const auto it = m_chunks.find({chunkX, cy, chunkZ});
        if (it == m_chunks.end()) {
            return;
        }
        column[static_cast<std::size_t>(cy)] = &it->second;
    }

    // Lowest y still reached by open sky, per column. One cell of margin so the
    // seeding step below can compare against neighbouring chunk-columns.
    constexpr int span = size + 2;
    std::array<int, span * span> skyFloor{};

    for (int mz = 0; mz < span; ++mz) {
        for (int mx = 0; mx < span; ++mx) {
            const int lx = mx - 1;
            const int lz = mz - 1;
            const bool inside = lx >= 0 && lx < size && lz >= 0 && lz < size;
            const int worldX = chunkX * size + lx;
            const int worldZ = chunkZ * size + lz;

            int floorY = 0;
            for (int y = worldTop; y >= 0; --y) {
                const BlockId block =
                    inside ? column[static_cast<std::size_t>(y / size)]->blocks.at(lx, y % size, lz)
                           : blockAt(worldX, y, worldZ);
                if (!isSkyTransparent(block)) {
                    floorY = y + 1;
                    break;
                }
            }
            skyFloor[static_cast<std::size_t>(mz) * span + mx] = floorY;
        }
    }

    for (int lz = 0; lz < size; ++lz) {
        for (int lx = 0; lx < size; ++lx) {
            const int worldX = chunkX * size + lx;
            const int worldZ = chunkZ * size + lz;
            const int floorY = skyFloor[static_cast<std::size_t>(lz + 1) * span + (lx + 1)];

            for (int y = worldTop; y >= 0; --y) {
                ChunkSlot& slot = *column[static_cast<std::size_t>(y / size)];
                const int ly = y % size;
                const BlockId block = slot.blocks.at(lx, ly, lz);

                // Sky falls straight down at full strength until something stops
                // it. Vertical travel costs nothing, which is what makes open
                // ground uniformly bright; only sideways spread dims.
                slot.blocks.setSkyLight(lx, ly, lz, y >= floorY ? kMaxLight : 0);

                if (const int emission = blockLightEmission(block); emission > 0) {
                    slot.blocks.setBlockLight(lx, ly, lz, emission);
                    m_blockAdditions.push_back({worldX, y, worldZ});
                }
            }

            // Only cells that can actually spread anywhere are worth queueing.
            // A lit cell whose neighbours are all lit has nothing to give, and
            // seeding every one of them buried the queue under tens of millions
            // of entries that did nothing.
            const int highestDarkNeighbour =
                std::max({skyFloor[static_cast<std::size_t>(lz + 1) * span + lx],
                          skyFloor[static_cast<std::size_t>(lz + 1) * span + lx + 2],
                          skyFloor[static_cast<std::size_t>(lz) * span + lx + 1],
                          skyFloor[static_cast<std::size_t>(lz + 2) * span + lx + 1]});

            // The lowest full-sky cell always gets queued: whatever stopped the
            // free fall may still be something light seeps into, such as a
            // canopy, and nothing else would ever push light down into it.
            if (floorY <= worldTop) {
                m_skyAdditions.push_back({worldX, floorY, worldZ});
            }

            for (int y = floorY + 1; y < highestDarkNeighbour && y <= worldTop; ++y) {
                m_skyAdditions.push_back({worldX, y, worldZ});
            }
        }
    }

    // The column and everything touching it, since border light shades
    // neighbouring geometry.
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        m_lightDirty.insert({chunkX, cy, chunkZ});
        m_lightDirty.insert({chunkX - 1, cy, chunkZ});
        m_lightDirty.insert({chunkX + 1, cy, chunkZ});
        m_lightDirty.insert({chunkX, cy, chunkZ - 1});
        m_lightDirty.insert({chunkX, cy, chunkZ + 1});
    }
}

void World::unpropagate(std::deque<LightRemoval>& removals, std::deque<glm::ivec3>& additions, bool sky) {
    while (!removals.empty()) {
        const LightRemoval entry = removals.front();
        removals.pop_front();

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = entry.position + step;
            if (n.y < 0 || n.y >= kWorldHeightChunks * Chunk::kSize) {
                continue;
            }

            const int level = sky ? skyLightAt(n.x, n.y, n.z) : blockLightAt(n.x, n.y, n.z);
            if (level == 0) {
                continue;
            }

            if (level < entry.previousLevel) {
                // This neighbour was lit by what we just removed.
                if (sky) {
                    setSkyLightAt(n.x, n.y, n.z, 0);
                } else {
                    setBlockLightAt(n.x, n.y, n.z, 0);
                }
                removals.push_back({n, level});
            } else {
                // Brighter than the source, so it has its own supply and will
                // fill the hole back in.
                additions.push_back(n);
            }
        }
    }
}

void World::propagateLight(const BudgetCheck& budgetSpent) {
    unpropagate(m_skyRemovals, m_skyAdditions, true);
    unpropagate(m_blockRemovals, m_blockAdditions, false);

    const int worldHeight = kWorldHeightChunks * Chunk::kSize;

    // Block light goes first even though sky light is what makes newly streamed
    // terrain look right. Block light is rare and its queue is short, so it
    // costs sky light almost nothing - but behind sky light it can be starved
    // outright, because streaming refills the sky queue every frame and the two
    // share one budget. That is a placed torch that never lights anything.
    while (!m_blockAdditions.empty() && !budgetSpent()) {
        const glm::ivec3 position = m_blockAdditions.front();
        m_blockAdditions.pop_front();

        const int level = blockLightAt(position.x, position.y, position.z);
        if (level <= 0) {
            continue;
        }

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = position + step;
            if (n.y < 0 || n.y >= worldHeight) {
                continue;
            }
            if (!isLightTransparent(blockAt(n.x, n.y, n.z))) {
                continue;
            }
            if (blockLightAt(n.x, n.y, n.z) >= level - 1) {
                continue;
            }

            setBlockLightAt(n.x, n.y, n.z, level - 1);
            m_blockAdditions.push_back(n);
        }
    }

    while (!m_skyAdditions.empty() && !budgetSpent()) {
        const glm::ivec3 position = m_skyAdditions.front();
        m_skyAdditions.pop_front();

        const int level = skyLightAt(position.x, position.y, position.z);
        if (level <= 0) {
            continue;
        }

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = position + step;
            if (n.y < 0 || n.y >= worldHeight) {
                continue;
            }
            if (!isLightTransparent(blockAt(n.x, n.y, n.z))) {
                continue;
            }

            // Straight down keeps full strength, but only through cells that
            // are sky-transparent. A canopy breaks the free fall, and from there
            // light dims one level per block downward like any other direction.
            const int reaching =
                (step.y == -1 && level == kMaxLight && isSkyTransparent(blockAt(n.x, n.y, n.z))) ? kMaxLight
                                                                                                 : level - 1;
            if (reaching <= 0 || skyLightAt(n.x, n.y, n.z) >= reaching) {
                continue;
            }

            setSkyLightAt(n.x, n.y, n.z, reaching);
            m_skyAdditions.push_back(n);
        }
    }
}

void World::scheduleFluidUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_fluidUpdates.push_back({x, y, z});
}

void World::updateFluids(const BudgetCheck& budgetSpent) {
    while (!m_fluidUpdates.empty() && !budgetSpent()) {
        const glm::ivec3 p = m_fluidUpdates.front();
        m_fluidUpdates.pop_front();

        const BlockId current = blockAt(p.x, p.y, p.z);
        // Only air and water are the fluid system's business. Testing for
        // "not solid" was the same thing while every block was a full cube or
        // water, but a plant is neither - and falling through here rewrites it
        // to air, which silently deleted anything you placed.
        if (current != BlockId::Air && !isWater(current)) {
            continue;
        }
        // Sources are the fixed points of the whole system. Without something
        // that never drains, every body of water eventually empties itself.
        if (isWaterSource(current)) {
            continue;
        }

        int supply = kMaxWaterLevel + 1;
        int adjacentSources = 0;

        // Anything falling from above arrives nearly full, and falling beats
        // spreading: water only runs sideways once it has nowhere to drop.
        if (isWater(blockAt(p.x, p.y + 1, p.z))) {
            supply = 1;
        }

        for (const glm::ivec3& step : kFlowSteps) {
            const glm::ivec3 n = p + step;
            const BlockId neighbour = blockAt(n.x, n.y, n.z);
            if (!isWater(neighbour)) {
                continue;
            }
            if (isWaterSource(neighbour)) {
                ++adjacentSources;
            }

            // That neighbour has somewhere to fall, so it drains downward
            // instead of feeding us.
            if (blockAt(n.x, n.y - 1, n.z) == BlockId::Air) {
                continue;
            }

            const int level = waterLevel(neighbour);
            if (level < kMaxWaterLevel) {
                // Competing flows resolve to whichever supply is strongest.
                supply = std::min(supply, level + 1);
            }
        }

        BlockId wanted = BlockId::Air;
        if (adjacentSources >= 2 && game::isSolid(blockAt(p.x, p.y - 1, p.z))) {
            // Two sources meeting over solid ground fill the gap permanently.
            wanted = BlockId::Water0;
        } else if (supply <= kMaxWaterLevel) {
            wanted = waterAtLevel(supply);
        }

        if (wanted != current) {
            setBlock(p.x, p.y, p.z, wanted);
        }
    }
}

void World::setVisibleRadius(int chunks) {
    const int radius = std::clamp(chunks, 1, 64);
    if (radius == m_visibleRadius) {
        return;
    }

    const int previous = m_visibleRadius;
    m_visibleRadius = radius;
    m_loadRadius = m_visibleRadius + 1;
    m_unloadRadius = m_loadRadius + 2;

    // Chunks are kept loaded a few rings past the visible radius so pacing back
    // and forth does not thrash them. That band would otherwise stay on screen
    // after shrinking, making the change look like it did nothing.
    m_radiusShrunk = radius < previous;

    // Forces the next update to run its recentre path, which is what unloads
    // what no longer fits and re-queues what is newly wanted.
    m_hasCentre = false;
}

void World::saveIfModified(const ChunkCoord& coord, ChunkSlot& slot) {
    if (!slot.modified) {
        return;
    }
    m_store->save(coord, slot.blocks);
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

void World::queueMesh(const ChunkCoord& coord) {
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    if (std::find(m_pendingMesh.begin(), m_pendingMesh.end(), coord) == m_pendingMesh.end()) {
        m_pendingMesh.push_back(coord);
    }
}

void World::invalidateMesh(const ChunkCoord& coord) {
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    ++it->second.revision;
    queueMesh(coord);
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

    const BlockId previous = it->second.blocks.at(lx, ly, lz);
    if (previous == block) {
        return;
    }
    it->second.blocks.set(lx, ly, lz, block);
    it->second.modified = true;

    // Relight around the change. Removals have to run before additions, because
    // stale light must be cleared out before anything fills the gap.
    const int oldSky = it->second.blocks.skyLightAt(lx, ly, lz);
    const int oldBlockLight = it->second.blocks.blockLightAt(lx, ly, lz);

    if (!isLightTransparent(block)) {
        if (oldSky > 0) {
            it->second.blocks.setSkyLight(lx, ly, lz, 0);
            m_skyRemovals.push_back({{x, y, z}, oldSky});
        }
        if (oldBlockLight > 0) {
            it->second.blocks.setBlockLight(lx, ly, lz, 0);
            m_blockRemovals.push_back({{x, y, z}, oldBlockLight});
        }
    } else {
        // Newly transparent: whatever surrounds it can now flow in.
        if (blockLightEmission(previous) > 0 && oldBlockLight > 0) {
            it->second.blocks.setBlockLight(lx, ly, lz, 0);
            m_blockRemovals.push_back({{x, y, z}, oldBlockLight});
        }
        for (const glm::ivec3& step : kLightSteps) {
            m_skyAdditions.push_back(glm::ivec3{x, y, z} + step);
            m_blockAdditions.push_back(glm::ivec3{x, y, z} + step);
        }
    }

    if (const int emission = blockLightEmission(block); emission > 0) {
        it->second.blocks.setBlockLight(lx, ly, lz, emission);
        m_blockAdditions.push_back({x, y, z});
    }

    lightChangedAt(x, y, z);

    // Water re-evaluates itself and everything touching it. Removing a block
    // under a stream is what makes it fall; removing its supply is what makes it
    // recede.
    scheduleFluidUpdate(x, y, z);
    for (const glm::ivec3& step : kLightSteps) {
        scheduleFluidUpdate(x + step.x, y + step.y, z + step.z);
    }

    invalidateMesh(coord);

    // Only a block on a chunk face can change a neighbour's mesh, but missing
    // those neighbours leaves holes at chunk seams. These must invalidate rather
    // than merely queue: a neighbour may already be meshing against the old
    // border, and that result would otherwise be accepted as current.
    constexpr int last = Chunk::kSize - 1;
    if (lx == 0) {
        invalidateMesh({coord.x - 1, coord.y, coord.z});
    }
    if (lx == last) {
        invalidateMesh({coord.x + 1, coord.y, coord.z});
    }
    if (ly == 0) {
        invalidateMesh({coord.x, coord.y - 1, coord.z});
    }
    if (ly == last) {
        invalidateMesh({coord.x, coord.y + 1, coord.z});
    }
    if (lz == 0) {
        invalidateMesh({coord.x, coord.y, coord.z - 1});
    }
    if (lz == last) {
        invalidateMesh({coord.x, coord.y, coord.z + 1});
    }
}

bool World::neighboursLoaded(const ChunkCoord& coord) const {
    return hasChunk({coord.x - 1, coord.y, coord.z}) && hasChunk({coord.x + 1, coord.y, coord.z}) &&
           hasChunk({coord.x, coord.y, coord.z - 1}) && hasChunk({coord.x, coord.y, coord.z + 1});
}

ChunkVolume World::gatherVolume(const ChunkCoord& coord) const {
    ChunkVolume volume;

    // The padded region spans at most one chunk in each direction, so the 27
    // possible source chunks are looked up once rather than per cell.
    const Chunk* sources[3][3][3]{};
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                sources[dx + 1][dy + 1][dz + 1] = chunkAt({coord.x + dx, coord.y + dy, coord.z + dz});
            }
        }
    }

    constexpr int size = Chunk::kSize;
    constexpr int pad = ChunkVolume::kPad;

    for (int y = -pad; y < size + pad; ++y) {
        const int dy = (y < 0) ? -1 : (y >= size ? 1 : 0);
        const int ly = y - dy * size;

        for (int z = -pad; z < size + pad; ++z) {
            const int dz = (z < 0) ? -1 : (z >= size ? 1 : 0);
            const int lz = z - dz * size;

            for (int x = -pad; x < size + pad; ++x) {
                const int dx = (x < 0) ? -1 : (x >= size ? 1 : 0);
                const int lx = x - dx * size;

                const Chunk* source = sources[dx + 1][dy + 1][dz + 1];
                const std::size_t at = ChunkVolume::index(x, y, z);

                if (source == nullptr) {
                    // Missing neighbours read as open sky rather than as solid,
                    // so the streaming frontier does not draw a wall of faces or
                    // a band of darkness.
                    volume.blocks[at] = BlockId::Air;
                    volume.light[at] = 0xF0;
                } else {
                    volume.blocks[at] = source->at(lx, ly, lz);
                    volume.light[at] = source->lightAt(lx, ly, lz);
                }
            }
        }
    }

    return volume;
}

void World::refreshQueues(const ChunkCoord& centre) {
    m_pendingLoad.clear();

    for (int dz = -m_loadRadius; dz <= m_loadRadius; ++dz) {
        for (int dx = -m_loadRadius; dx <= m_loadRadius; ++dx) {
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
    //
    // Queued, not invalidated: their contents have not changed, and bumping the
    // revision here would throw away perfectly good work every time the player
    // crossed a chunk boundary.
    for (const auto& [coord, slot] : m_chunks) {
        if (!slot.meshed && chebyshevDistance(coord, centre) <= m_visibleRadius) {
            queueMesh(coord);
        }
    }
}

std::size_t World::jobCapacity() const {
    // Enough to keep every worker fed with a little slack, and no more. Split
    // between the two kinds of work so a long stream of loads cannot starve
    // meshing and leave the player standing in an invisible world.
    return static_cast<std::size_t>(m_jobs.threadCount()) * 2 + 2;
}

void World::dispatchLoads(const BudgetCheck& budgetSpent, std::size_t capacity) {
    // The budget matters even with workers, and matters completely without them:
    // a pool with no threads runs `submit` inline, so this loop *is* the work.
    while (!m_pendingLoad.empty() && m_loadInFlight.size() < capacity && !budgetSpent()) {
        const ChunkCoord coord = m_pendingLoad.back();
        m_pendingLoad.pop_back();

        if (hasChunk(coord) || m_loadInFlight.count(coord) != 0) {
            continue;
        }

        m_loadInFlight.insert(coord);

        // Captures only copies and shared owners. Nothing here reaches back into
        // the world, which is what makes it safe to run anywhere.
        m_jobs.submit([coord, seed = m_seed, store = m_store, results = m_results] {
            // A saved chunk replaces generation entirely: it already contains
            // the generated terrain plus whatever the player did to it.
            std::optional<Chunk> stored = store->load(coord);
            Chunk blocks = stored.has_value() ? std::move(*stored) : generateChunk(seed, coord);

            std::lock_guard<std::mutex> lock(results->mutex);
            results->loaded.push_back(LoadedChunk{coord, std::move(blocks)});
        });
    }
}

void World::dispatchMeshes(const ChunkCoord& centre, const BudgetCheck& budgetSpent, std::size_t capacity) {
    while (!m_pendingMesh.empty() && m_meshInFlight.size() < capacity && !budgetSpent()) {
        const ChunkCoord coord = m_pendingMesh.back();
        m_pendingMesh.pop_back();

        const auto it = m_chunks.find(coord);
        if (it == m_chunks.end()) {
            continue;
        }
        if (chebyshevDistance(coord, centre) > m_visibleRadius || !neighboursLoaded(coord)) {
            continue;
        }
        // Already being meshed. Dropping it is safe: if the chunk has changed
        // since that job started, the revision check on collection re-queues it.
        if (m_meshInFlight.count(coord) != 0) {
            continue;
        }

        m_meshInFlight.insert(coord);

        // The chunk and its borders are copied here, on the main thread, so the
        // job owns everything it reads. That copy is what removes the whole
        // question of what the main thread may do to this chunk meanwhile.
        auto input = std::make_shared<MeshJobInput>();
        input->coord = coord;
        input->revision = it->second.revision;
        input->origin = glm::vec3{static_cast<float>(coord.x * Chunk::kSize),
                                  static_cast<float>(coord.y * Chunk::kSize),
                                  static_cast<float>(coord.z * Chunk::kSize)};
        input->volume = gatherVolume(coord);

        m_jobs.submit([input = std::move(input), results = m_results] {
            ChunkMeshes meshes = meshChunk(input->volume, input->origin);

            std::lock_guard<std::mutex> lock(results->mutex);
            results->meshed.push_back(MeshedChunk{input->coord, std::move(meshes), input->revision});
        });
    }
}

void World::collectFinishedJobs() {
    std::vector<LoadedChunk> loaded;
    std::vector<MeshedChunk> meshed;

    {
        std::lock_guard<std::mutex> lock(m_results->mutex);
        loaded.swap(m_results->loaded);
        meshed.swap(m_results->meshed);
    }

    for (LoadedChunk& result : loaded) {
        m_loadInFlight.erase(result.coord);

        // The player may have walked far enough that this is no longer wanted,
        // or a later pass may already have loaded it.
        if (hasChunk(result.coord)) {
            continue;
        }
        if (m_hasCentre && chebyshevDistance(result.coord, m_centre) > m_unloadRadius) {
            continue;
        }

        m_chunks.emplace(result.coord, ChunkSlot{std::move(result.blocks), false, false, 0});

        // Sky light is traced from the top of the world down, so it can only be
        // done once every chunk in the column is present.
        if (columnLoaded(result.coord.x, result.coord.z)) {
            seedColumnLight(result.coord.x, result.coord.z);
        }

        // The new chunk and its neighbours may all have gained or lost visible
        // faces along the shared border, so a neighbour already being meshed is
        // now building against a border that no longer matches.
        queueMesh(result.coord);
        invalidateMesh({result.coord.x - 1, result.coord.y, result.coord.z});
        invalidateMesh({result.coord.x + 1, result.coord.y, result.coord.z});
        invalidateMesh({result.coord.x, result.coord.y, result.coord.z - 1});
        invalidateMesh({result.coord.x, result.coord.y, result.coord.z + 1});
    }

    for (MeshedChunk& result : meshed) {
        m_meshInFlight.erase(result.coord);
        m_readyMeshes.push_back(std::move(result));
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
            if (chebyshevDistance(it->first, centre) > m_unloadRadius) {
                // Must happen before the erase, or an edited chunk is lost the
                // moment the player walks away from it.
                saveIfModified(it->first, it->second);

                if (it->second.meshed) {
                    updates.push_back(ChunkMeshUpdate{it->first, {}, {}, true});
                }
                it = m_chunks.erase(it);
            } else {
                ++it;
            }
        }

        // Queued work for chunks that no longer exist or are out of range would
        // otherwise pile up forever as the player walks. In-flight jobs are left
        // alone; they are discarded on collection instead, because a running job
        // cannot be recalled.
        const auto outOfRange = [&](const ChunkCoord& c) {
            return chebyshevDistance(c, centre) > m_unloadRadius;
        };
        m_pendingMesh.erase(std::remove_if(m_pendingMesh.begin(), m_pendingMesh.end(), outOfRange),
                            m_pendingMesh.end());

        refreshQueues(centre);
    }

    if (m_radiusShrunk) {
        m_radiusShrunk = false;
        for (auto& [coord, slot] : m_chunks) {
            if (slot.meshed && chebyshevDistance(coord, centre) > m_visibleRadius) {
                slot.meshed = false;
                updates.push_back(ChunkMeshUpdate{coord, {}, {}, true});
            }
        }
    }

    collectFinishedJobs();

    const auto budgetSpent = [&] {
        return std::chrono::duration<float>(Clock::now() - start).count() >= budgetSeconds;
    };

    propagateLight(budgetSpent);
    flushLightDirty();
    updateFluids(budgetSpent);

    // Uploading is the only part still on the main thread, so it is what the
    // budget now meters.
    while (!m_readyMeshes.empty() && !budgetSpent()) {
        MeshedChunk result = std::move(m_readyMeshes.back());
        m_readyMeshes.pop_back();

        const auto it = m_chunks.find(result.coord);
        if (it == m_chunks.end()) {
            continue; // Unloaded while the job ran.
        }
        if (it->second.revision != result.revision) {
            // Something changed while the job ran. The revision has already moved
            // on, so this only needs re-queueing, not another bump.
            queueMesh(result.coord);
            continue;
        }

        it->second.meshed = true;
        updates.push_back(ChunkMeshUpdate{result.coord, std::move(result.meshes.opaque),
                                          std::move(result.meshes.translucent), false});
    }

    dispatchLoads(budgetSpent, jobCapacity());
    dispatchMeshes(centre, budgetSpent, jobCapacity());

    return updates;
}

bool World::isSettled() const {
    return pendingChunkCount() == 0 && m_skyAdditions.empty() && m_blockAdditions.empty() &&
           m_skyRemovals.empty() && m_blockRemovals.empty() && m_lightDirty.empty() && m_fluidUpdates.empty();
}

float World::initialLoadProgress() const {
    const int span = 2 * m_loadRadius + 1;
    const auto expected = static_cast<float>(span * span * kWorldHeightChunks);
    if (expected <= 0.0f) {
        return 1.0f;
    }

    const float loaded = std::min(1.0f, static_cast<float>(m_chunks.size()) / expected);

    std::size_t meshed = 0;
    for (const auto& entry : m_chunks) {
        if (entry.second.meshed) {
            ++meshed;
        }
    }
    const float meshedFraction =
        m_chunks.empty() ? 0.0f : static_cast<float>(meshed) / static_cast<float>(m_chunks.size());

    // Generation is most of the work but meshing is what you can actually see,
    // so the bar splits between them rather than hitting 100% while the world is
    // still invisible.
    const float reported = 0.6f * loaded + 0.4f * loaded * meshedFraction;

    // Held short of full until everything has drained, light included, so the
    // bar cannot finish ahead of the world.
    return (loaded >= 1.0f && isSettled()) ? 1.0f : std::min(reported, 0.99f);
}

std::vector<ChunkMeshUpdate> World::loadImmediately(const glm::vec3& position) {
    std::vector<ChunkMeshUpdate> all;

    // Startup is the one place a stall is preferable to popping, and nothing is
    // moving yet, so every chunk is queued at once rather than in capacity-sized
    // batches with a barrier between each. Batching here left eleven workers
    // waiting on each other and was slower than doing it single-threaded.
    constexpr std::size_t kUnlimited = ~std::size_t{0};
    const BudgetCheck never = [] { return false; };

    const ChunkCoord centre{floorDiv(static_cast<int>(std::floor(position.x)), Chunk::kSize), 0,
                            floorDiv(static_cast<int>(std::floor(position.z)), Chunk::kSize)};

    // First pass establishes the centre and fills the load queue.
    std::vector<ChunkMeshUpdate> batch = update(position, 1000.0f);
    all.insert(all.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));

    for (int pass = 0; pass < 16; ++pass) {
        // Loads, then light, then meshes. Meshing before the light has settled
        // bakes darkness into the geometry, and the world then visibly brightens
        // over the next second as everything is rebuilt.
        dispatchLoads(never, kUnlimited);
        m_jobs.waitForIdle();
        collectFinishedJobs();

        propagateLight(never);
        flushLightDirty();

        dispatchMeshes(centre, never, kUnlimited);
        m_jobs.waitForIdle();
        collectFinishedJobs();

        // Newly arrived chunks unlock their neighbours for meshing, so this
        // repeats until neither queue has anything left.
        if (m_pendingLoad.empty() && m_pendingMesh.empty() && m_loadInFlight.empty() && m_meshInFlight.empty()) {
            break;
        }
    }

    batch = update(position, 1000.0f);
    all.insert(all.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));

    return all;
}

} // namespace game
