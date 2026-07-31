#pragma once

#include "world/Chunk.hpp"
#include "world/ChunkMesher.hpp"
#include "world/TerrainGenerator.hpp"
#include "world/WorldStore.hpp"

#include <engine/core/JobSystem.hpp>
#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace game {

/// How tall the world is, in chunks. Terrain never reaches the top, so the
/// upper chunks exist purely as building room.
constexpr int kWorldHeightChunks = 3;

/// Fallback render distance when nothing else specifies one.
constexpr int kDefaultVisibleRadiusChunks = 5;

/// A chunk's drawable geometry has changed. `removed` means the chunk left the
/// world and its mesh should be released.
struct ChunkMeshUpdate {
    ChunkCoord coord;
    engine::MeshData mesh;
    bool removed = false;
};

/// Owns every loaded chunk and streams them in and out around the player.
///
/// **This is the single owner of block data, and only the main thread mutates
/// it.** Physics, raycasting and meshing all read through here; none of them
/// writes.
class World {
public:
    /// `jobs` must outlive the world. Generation and meshing are submitted to
    /// it; a pool with no workers runs them inline, which is a supported mode.
    ///
    /// `visibleRadiusChunks` is the render distance. Chunks are generated one
    /// ring beyond it, because a chunk can only be meshed once its four
    /// horizontal neighbours exist — meshing against a missing neighbour emits a
    /// wall of faces at the frontier that then has to be undone. They are
    /// unloaded two rings beyond *that*, so pacing back and forth across the
    /// boundary does not thrash chunks in and out.
    World(std::uint32_t seed, std::filesystem::path saveRoot, engine::JobSystem& jobs,
          int visibleRadiusChunks = kDefaultVisibleRadiusChunks);
    ~World();

    int visibleRadius() const { return m_visibleRadius; }
    int loadRadius() const { return m_loadRadius; }

    /// Changes the render distance while playing.
    ///
    /// Safe mid-session, unlike the worker count: nothing is in a half-migrated
    /// state, and the next update simply loads or unloads the difference.
    /// Shrinking returns removal updates for every chunk that leaves.
    void setVisibleRadius(int chunks);

    /// Anything not currently loaded reads as air.
    BlockId blockAt(int x, int y, int z) const;
    bool isSolid(int x, int y, int z) const;

    /// Highest solid block in a column, or -1 if empty or not loaded.
    int highestSolid(int x, int z) const;

    /// Changes one block and marks every affected chunk for re-meshing.
    void setBlock(int x, int y, int z, BlockId block);

    /// Loads, unloads and re-meshes around the player, stopping once the time
    /// budget is spent. Returns only what changed this call.
    ///
    /// Unloading is always finished because it frees memory and is cheap; only
    /// generation and meshing are metered.
    std::vector<ChunkMeshUpdate> update(const glm::vec3& playerPosition, float budgetSeconds);

    /// Generates and meshes everything visible around a point with no budget.
    /// Used once at startup so the player does not spawn into an empty world.
    std::vector<ChunkMeshUpdate> loadImmediately(const glm::vec3& position);

    std::uint32_t seed() const { return m_seed; }
    std::size_t loadedChunkCount() const { return m_chunks.size(); }

    /// Everything not yet drawable: queued, running on a worker, or waiting to
    /// be uploaded. Should fall to zero when the player stops moving.
    std::size_t pendingChunkCount() const {
        return m_pendingLoad.size() + m_pendingMesh.size() + m_loadInFlight.size() + m_meshInFlight.size() +
               m_readyMeshes.size();
    }

    /// Writes every modified chunk still in memory. Call before shutting down;
    /// chunks that unload during play are saved as they go.
    void saveAll();

    /// Persistence for anything that is not block data, such as where the player
    /// was standing.
    const WorldStore& store() const { return *m_store; }

    std::size_t savedChunkCount() const { return m_savedChunkCount; }

private:
    struct ChunkSlot {
        Chunk blocks;
        bool meshed = false;
        /// Set the moment a block is changed. Only these chunks are ever
        /// written: everything else regenerates from the seed.
        bool modified = false;
        /// Bumped on every edit. A mesh job records the revision it started
        /// from, and a result whose revision no longer matches is thrown away:
        /// that is how an edit made while meshing was in flight is caught.
        std::uint32_t revision = 0;
    };

    /// A finished generation job.
    struct LoadedChunk {
        ChunkCoord coord;
        Chunk blocks;
    };

    /// A finished mesh job, tagged with the chunk revision it was built from.
    struct MeshedChunk {
        ChunkCoord coord;
        engine::MeshData mesh;
        std::uint32_t revision = 0;
    };

    /// Where workers leave their results.
    ///
    /// Held by `shared_ptr` and captured by every job, so a job still running
    /// when the world is destroyed writes into a buffer that is still alive
    /// instead of into freed memory. This is the only state a worker touches
    /// that the main thread also touches, and the mutex covers all of it.
    struct JobResults {
        std::mutex mutex;
        std::vector<LoadedChunk> loaded;
        std::vector<MeshedChunk> meshed;
    };

    const Chunk* chunkAt(const ChunkCoord& coord) const;
    bool hasChunk(const ChunkCoord& coord) const;
    bool neighboursLoaded(const ChunkCoord& coord) const;

    /// Queues a chunk for meshing without claiming anything about it changed.
    /// Used when a mesh is simply missing.
    void queueMesh(const ChunkCoord& coord);

    /// Says the chunk's geometry is genuinely out of date: bumps its revision,
    /// which discards any mesh job already in flight for it. Anything that
    /// changes what a chunk *or its borders* look like must go through here, or
    /// an in-flight job will quietly install a stale mesh and nothing will ever
    /// correct it.
    void invalidateMesh(const ChunkCoord& coord);

    void refreshQueues(const ChunkCoord& centre);
    void saveIfModified(const ChunkCoord& coord, ChunkSlot& slot);

    /// Snapshots the six neighbouring border layers. Runs on the main thread so
    /// the job that follows owns copies of everything it reads.
    ChunkNeighbours snapshotNeighbours(const ChunkCoord& coord) const;

    using BudgetCheck = std::function<bool()>;

    void dispatchLoads(const BudgetCheck& budgetSpent, std::size_t capacity);
    void dispatchMeshes(const ChunkCoord& centre, const BudgetCheck& budgetSpent, std::size_t capacity);
    void collectFinishedJobs();

    /// How many jobs of one kind may be outstanding while playing. Kept short on
    /// purpose: a longer queue only produces results for places the player has
    /// already left. Startup ignores it, because nothing is moving yet.
    std::size_t jobCapacity() const;

    std::uint32_t m_seed;
    int m_visibleRadius;
    int m_loadRadius;
    int m_unloadRadius;
    /// Shared with in-flight jobs, which read chunk files from it.
    std::shared_ptr<WorldStore> m_store;
    engine::JobSystem& m_jobs;
    std::shared_ptr<JobResults> m_results;

    std::unordered_map<ChunkCoord, ChunkSlot> m_chunks;

    // Deliberately vectors rather than sets: they are rebuilt whenever the
    // player crosses a chunk boundary, and keeping them ordered by distance is
    // what makes the world fill in from the player outwards.
    std::vector<ChunkCoord> m_pendingLoad;
    std::vector<ChunkCoord> m_pendingMesh;

    /// Submitted but not yet collected. Stops the same chunk being queued twice.
    std::unordered_set<ChunkCoord> m_loadInFlight;
    std::unordered_set<ChunkCoord> m_meshInFlight;

    /// Collected from workers but not yet handed to the renderer. Uploading is
    /// main-thread work with a real cost, so it is metered like everything else.
    std::vector<MeshedChunk> m_readyMeshes;

    ChunkCoord m_centre{0, 0, 0};
    bool m_hasCentre = false;
    /// Set when the render distance shrinks, so the next update drops meshes
    /// that are now out of range instead of leaving them on screen.
    bool m_radiusShrunk = false;
    std::size_t m_savedChunkCount = 0;
};

} // namespace game
