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

/// Copies one face layer out of a chunk into the flat form the mesher wants.
///
/// `axis` is which axis the face lies on and `high` picks the far layer. The
/// two remaining axes become the border's (a, b) in ascending axis order, which
/// is the convention `ChunkBorder::at` documents.
ChunkBorder extractBorder(const Chunk* chunk, int axis, bool high) {
    ChunkBorder border;
    if (chunk == nullptr) {
        return border; // Absent borders read as air.
    }
    border.present = true;

    constexpr int size = Chunk::kSize;
    const int fixed = high ? size - 1 : 0;

    for (int a = 0; a < size; ++a) {
        for (int b = 0; b < size; ++b) {
            int x = 0;
            int y = 0;
            int z = 0;
            switch (axis) {
            case 0:
                x = fixed;
                y = a;
                z = b;
                break;
            case 1:
                x = a;
                y = fixed;
                z = b;
                break;
            default:
                x = a;
                y = b;
                z = fixed;
                break;
            }
            border.blocks[a * size + b] = chunk->at(x, y, z);
        }
    }
    return border;
}

/// Everything a mesh job reads, allocated once and shared with the job.
///
/// Captured by `shared_ptr` rather than by value: a lambda holding a whole chunk
/// gets copied into the `std::function` that carries it, so the 38 KB of blocks
/// and borders would be copied twice per job. A pointer-sized capture also fits
/// inside `std::function` without a second heap allocation.
struct MeshJobInput {
    ChunkCoord coord;
    std::uint32_t revision = 0;
    glm::vec3 origin{0.0f};
    Chunk blocks;
    ChunkNeighbours neighbours;
};

} // namespace

World::World(std::uint32_t seed, std::filesystem::path saveRoot, engine::JobSystem& jobs)
    : m_seed(seed), m_store(std::make_shared<WorldStore>(std::move(saveRoot), seed)), m_jobs(jobs),
      m_results(std::make_shared<JobResults>()) {}

/// Jobs hold `shared_ptr`s to the results buffer and the store, so anything
/// still running here keeps what it touches alive and simply writes into a
/// buffer nobody will read. Nothing needs to be waited for.
World::~World() = default;

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

    if (it->second.blocks.at(lx, ly, lz) == block) {
        return;
    }
    it->second.blocks.set(lx, ly, lz, block);
    it->second.modified = true;

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

ChunkNeighbours World::snapshotNeighbours(const ChunkCoord& coord) const {
    ChunkNeighbours neighbours;
    neighbours.negativeX = extractBorder(chunkAt({coord.x - 1, coord.y, coord.z}), 0, true);
    neighbours.positiveX = extractBorder(chunkAt({coord.x + 1, coord.y, coord.z}), 0, false);
    neighbours.negativeY = extractBorder(chunkAt({coord.x, coord.y - 1, coord.z}), 1, true);
    neighbours.positiveY = extractBorder(chunkAt({coord.x, coord.y + 1, coord.z}), 1, false);
    neighbours.negativeZ = extractBorder(chunkAt({coord.x, coord.y, coord.z - 1}), 2, true);
    neighbours.positiveZ = extractBorder(chunkAt({coord.x, coord.y, coord.z + 1}), 2, false);
    return neighbours;
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
    //
    // Queued, not invalidated: their contents have not changed, and bumping the
    // revision here would throw away perfectly good work every time the player
    // crossed a chunk boundary.
    for (const auto& [coord, slot] : m_chunks) {
        if (!slot.meshed && chebyshevDistance(coord, centre) <= kVisibleRadiusChunks) {
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
        if (chebyshevDistance(coord, centre) > kVisibleRadiusChunks || !neighboursLoaded(coord)) {
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
        input->blocks = it->second.blocks;
        input->neighbours = snapshotNeighbours(coord);

        m_jobs.submit([input = std::move(input), results = m_results] {
            engine::MeshData mesh = meshChunk(input->blocks, input->neighbours, input->origin);

            std::lock_guard<std::mutex> lock(results->mutex);
            results->meshed.push_back(MeshedChunk{input->coord, std::move(mesh), input->revision});
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
        if (m_hasCentre && chebyshevDistance(result.coord, m_centre) > kUnloadRadiusChunks) {
            continue;
        }

        m_chunks.emplace(result.coord, ChunkSlot{std::move(result.blocks), false, false, 0});

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
        // otherwise pile up forever as the player walks. In-flight jobs are left
        // alone; they are discarded on collection instead, because a running job
        // cannot be recalled.
        const auto outOfRange = [&](const ChunkCoord& c) {
            return chebyshevDistance(c, centre) > kUnloadRadiusChunks;
        };
        m_pendingMesh.erase(std::remove_if(m_pendingMesh.begin(), m_pendingMesh.end(), outOfRange),
                            m_pendingMesh.end());

        refreshQueues(centre);
    }

    collectFinishedJobs();

    const auto budgetSpent = [&] {
        return std::chrono::duration<float>(Clock::now() - start).count() >= budgetSeconds;
    };

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
        updates.push_back(ChunkMeshUpdate{result.coord, std::move(result.mesh), false});
    }

    dispatchLoads(budgetSpent, jobCapacity());
    dispatchMeshes(centre, budgetSpent, jobCapacity());

    return updates;
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
        dispatchLoads(never, kUnlimited);
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
