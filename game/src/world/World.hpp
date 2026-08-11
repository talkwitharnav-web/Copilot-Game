#pragma once

#include "world/Chunk.hpp"
#include "world/ChunkMesher.hpp"
#include "world/TerrainGenerator.hpp"
#include "world/WorldStore.hpp"

#include <engine/core/JobSystem.hpp>
#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace game {

/// Fallback render distance when nothing else specifies one.
constexpr int kDefaultVisibleRadiusChunks = 5;

/// A chunk's drawable geometry has changed. `removed` means the chunk left the
/// world and its meshes should be released.
struct ChunkMeshUpdate {
    ChunkCoord coord;
    engine::MeshData mesh;
    engine::MeshData translucentMesh;
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

    /// How far out chunks are meshed with everything in them, in chunks.
    ///
    /// Beyond it a chunk is built terrain-only: the blocks and their baked
    /// lighting are identical, and `isDistantDecoration` is left out. **This is
    /// a rendering tier and nothing else** - the plants are still in the block
    /// array, still break, still burn, still wash away and are still saved.
    ///
    /// Setting it to the render distance or higher turns it off, which is what
    /// makes it A/B testable rather than a permanent change of behaviour.
    void setDetailRadius(int chunks);
    int detailRadius() const { return m_detailRadius; }

    /// Chunks currently carrying their decoration, and how many are drawn at
    /// all. Reported by the diagnostics overlay so the tier can be seen working
    /// rather than taken on trust.
    std::size_t detailedChunkCount() const;

    /// Chunks that are drawn at a tier they should no longer be at, and how far
    /// away the nearest of them is.
    ///
    /// **The number that says whether the tier is keeping up.** While standing
    /// still it should be zero; while flying, the only chunks in it should be
    /// the ring at the boundary that is currently being rebuilt. Anything close
    /// to the player means the re-meshing is losing the race, which on screen
    /// is plants appearing a few paces in front of you.
    struct DetailLag {
        std::size_t chunks = 0;
        /// Chebyshev distance in chunks, or -1 when nothing is behind.
        int nearest = -1;
    };
    DetailLag detailLag() const;

    /// Changes the render distance while playing.
    ///
    /// Safe mid-session, unlike the worker count: nothing is in a half-migrated
    /// state, and the next update simply loads or unloads the difference.
    /// Shrinking returns removal updates for every chunk that leaves.
    void setVisibleRadius(int chunks);

    /// Anything not currently loaded reads as air.
    BlockId blockAt(int x, int y, int z) const;
    bool isSolid(int x, int y, int z) const;

    /// Whether fire may exist in this cell: something flammable beside it, or a
    /// floor under it. **Shared by lighting one and by keeping one alive** - the
    /// two used to disagree, and the placement half demanded solid ground where
    /// the tick half accepted fuel, so a fire could burn where it could not be
    /// lit.
    bool fireCanSurvive(int x, int y, int z) const;

    /// Whether this cell also holds water. Anything asking "am I in water?"
    /// has to ask this too, or a swimmer drowns in a patch of seagrass.
    bool waterloggedAt(int x, int y, int z) const;

    /// The other half of a double chest, or nothing if this one stands alone.
    ///
    /// **Worked out from position alone, never remembered.** A stored pairing
    /// would have to go into the save, survive a reload and be repaired every
    /// time a neighbour was placed or broken; deriving it costs a short walk and
    /// cannot disagree with itself, because both halves run this same function.
    /// The price is one named divergence: a long row re-pairs when a chest in
    /// the middle of it is removed.
    std::optional<glm::ivec3> chestPartnerAt(const glm::ivec3& at) const;

    /// Which half of a double chest this is, seen from the front. What the
    /// mesher needs, and derived from the same walk as the pairing so the
    /// texture and the screen can never disagree about which half is which.
    ChestHalf chestHalfAt(const glm::ivec3& at) const;

    /// Whether the whole vertical column at a block position is resident.
    /// Anything that falls under gravity must ask this first: an absent chunk
    /// reads as air, so a creature over one drops straight through the world.
    bool columnResident(int x, int z) const;

    /// Highest solid block in a column, or -1 if empty or not loaded.
    int highestSolid(int x, int z) const;

    /// Changes one block and marks every affected chunk for re-meshing.
    /// A block a spreading flow destroyed, and what it was.
    struct WashedBlock {
        glm::ivec3 position;
        BlockId block;
    };

    /// Hands over everything water swept aside since the last call.
    ///
    /// Returned rather than dropped here, because `World` has no idea items
    /// exist - the owning loop turns these into drops, exactly as it does for a
    /// plant left hanging when you mine the block under it.
    std::vector<WashedBlock> takeWashedBlocks();

    /// Sand and gravel that just lost its footing, already removed from the
    /// grid. The caller turns each into a falling entity; `World` cannot,
    /// because it has no idea entities exist.
    std::vector<WashedBlock> takeDetachedBlocks();

    /// Starts a charge's fuse. The block must already be `TntPrimed`; this only
    /// schedules when it goes off.
    void primeTnt(const glm::ivec3& at);

    /// Whether it is currently precipitating overhead.
    ///
    /// Plain data rather than a handle on the weather: the fire update needs to
    /// know one bit, and a fire under an open sky goes out in the rain. Without
    /// this, the first bolt of a storm could burn a forest down permanently -
    /// the reference lights those fires *and* puts them out.
    void setPrecipitating(bool precipitating) { m_precipitating = precipitating; }

    /// Charges that reached the end of their fuse this frame. Drained by the
    /// caller and turned into blasts there, because `World` reads blocks and
    /// knows nothing about drops, creatures or damage - the same hand-off
    /// `takeDetachedBlocks` already uses.
    std::vector<glm::ivec3> takeDetonations();

    void setBlock(int x, int y, int z, BlockId block);

    /// Bone meal on a crop or a stem. **A random tick you asked for** - it runs
    /// the same growth rule the sampler does, so there is no second idea of
    /// what "grow" means that could disagree with it.
    ///
    /// Returns whether anything was actually fed, so the caller knows whether
    /// to spend the item.
    bool applyBoneMeal(const glm::ivec3& at);

    /// Loads, unloads and re-meshes around the player, stopping once the time
    /// budget is spent. Returns only what changed this call.
    ///
    /// Unloading is always finished because it frees memory and is cheap; only
    /// generation and meshing are metered.
    std::vector<ChunkMeshUpdate> update(const glm::vec3& playerPosition, float budgetSeconds);

    /// Generates and meshes everything visible around a point with no budget.
    /// Used once at startup so the player does not spawn into an empty world.
    std::vector<ChunkMeshUpdate> loadImmediately(const glm::vec3& position);

    /// Light level at a world position, 0-15. Unloaded chunks read as full sky.
    int skyLightAt(int x, int y, int z) const;
    int blockLightAt(int x, int y, int z) const;

    std::uint32_t seed() const { return m_seed; }
    std::size_t loadedChunkCount() const { return m_chunks.size(); }

    /// Where the initial load has actually got to, checkpoint by checkpoint.
    ///
    /// Measured rather than estimated: `drawn` counts meshed chunks over the
    /// whole *visible box*, so it starts at zero and reaches one only when every
    /// chunk you could look at is built and uploaded.
    struct LoadStatus {
        /// Chunks present, over what the load radius asked for.
        float generated = 0.0f;
        /// Visible chunks meshed, over how many the visible radius holds.
        float drawn = 0.0f;
        /// Every queue drained - chunk work, light and water alike.
        bool settled = false;
        /// All three. **The only thing the loading screen may finish on.**
        bool complete = false;
    };
    LoadStatus loadStatus() const;

    /// How far through the initial load the world is, 0 to 1.
    ///
    /// A weighted blend of `LoadStatus`'s checkpoints, held below 1 until
    /// `complete`, so the bar can never finish ahead of the world.
    float initialLoadProgress() const;

    /// Everything not yet drawable: queued, running on a worker, or waiting to
    /// be uploaded. Should fall to zero when the player stops moving.
    std::size_t pendingChunkCount() const {
        return m_pendingLoad.size() + m_pendingMesh.size() + m_loadInFlight.size() + m_meshInFlight.size() +
               m_readyMeshes.size();
    }

    /// Nothing outstanding anywhere, light included.
    ///
    /// Light is the part that is easy to forget: propagating it dirties chunks,
    /// which re-meshes them, so a world with empty chunk queues can still have
    /// geometry about to change.
    bool isSettled() const;

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
        /// Whether the mesh last **dispatched** for this chunk carried its
        /// decoration, and whether the mesh currently **on the GPU** does.
        ///
        /// **Two fields, not one, and that is the whole of why a tier change
        /// cannot be lost.** The intent has to be recorded to make the
        /// boundary's hysteresis work at all - what a chunk should be depends
        /// on what it already is - but a queued re-mesh can be dropped, by a
        /// missing neighbour or by the player moving on. With only the intent,
        /// such a chunk agrees with itself and keeps the wrong mesh for ever.
        /// Comparing the two is what `refreshQueues` recovers from.
        ///
        /// Both start false, so a chunk streaming in outside the boundary is
        /// built terrain-only the first time rather than built whole and
        /// immediately rebuilt.
        bool detailWanted = false;
        bool detailBuilt = false;
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

    /// A finished mesh job, tagged with the chunk revision it was built from
    /// and the tier it was built at.
    struct MeshedChunk {
        ChunkCoord coord;
        ChunkMeshes meshes;
        std::uint32_t revision = 0;
        MeshDetail detail = MeshDetail::Full;
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

    /// A cell to darken, carrying the level it used to have. Neighbours dimmer
    /// than that were lit by it and must be darkened too; brighter ones have
    /// another source and become re-fill seeds instead.
    struct LightRemoval {
        glm::ivec3 position;
        int previousLevel;
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

    /// Which tier this chunk should be built at, given the tier it is already
    /// at. **Asymmetric on purpose**: detail is added at `m_detailRadius` and
    /// only dropped one chunk further out, so a player pacing across the
    /// boundary re-meshes the ring it crosses once rather than every frame.
    MeshDetail detailFor(const ChunkCoord& coord, bool currentlyDetailed) const;

    /// Re-meshes every chunk whose tier changed when the player's chunk did.
    void refreshDetail(const ChunkCoord& centre);

    void refreshQueues(const ChunkCoord& centre);
    void saveIfModified(const ChunkCoord& coord, ChunkSlot& slot);

    /// Copies the chunk plus one cell of surrounding blocks and light. Runs on
    /// the main thread so the job that follows owns everything it reads.
    ///
    /// **Fills the caller's volume rather than returning one.** It is 118 KB of
    /// arrays; returning it by value meant zeroing a local, filling it, then
    /// copying it over a second volume that had also just been zeroed - four
    /// passes over that memory where two will do, on the one thread the whole
    /// streaming budget is measured against.
    void gatherVolume(const ChunkCoord& coord, ChunkVolume& out) const;

    using BudgetCheck = std::function<bool()>;

    void dispatchLoads(const BudgetCheck& budgetSpent, std::size_t capacity);
    void dispatchMeshes(const ChunkCoord& centre, const BudgetCheck& budgetSpent, std::size_t capacity);
    void collectFinishedJobs();

    /// True once every vertical chunk of a column is loaded, which is when its
    /// sky light can be traced from the top of the world downwards.
    bool columnLoaded(int chunkX, int chunkZ) const;

    /// Fills in a column's sky light and seeds the propagation queues. Runs once
    /// per column, when the last of its chunks arrives.
    void seedColumnLight(int chunkX, int chunkZ);

    /// Spreads queued light outwards until the queues empty or the budget runs
    /// out. Chunks whose light changed are re-meshed.
    void propagateLight(const BudgetCheck& budgetSpent);

    /// Walks back light that came from a source which no longer exists, then
    /// re-fills from whatever still reaches the emptied region.
    void unpropagate(std::deque<LightRemoval>& removals, std::deque<glm::ivec3>& additions, bool sky);

    void setSkyLightAt(int x, int y, int z, int level);
    void setBlockLightAt(int x, int y, int z, int level);

    /// Re-evaluates one cell of water and spreads the consequences.
    ///
    /// Incremental on purpose: generated oceans are already settled, so nothing
    /// runs until something disturbs them.
    void updateFluids(const BudgetCheck& budgetSpent);
    void scheduleFluidUpdate(int x, int y, int z);

    /// The same for lava, on its own queue because it runs six times slower.
    /// One queue holding both would no longer be in due order, and the drain
    /// loop's "the front one is not ready so none of them is" shortcut - which
    /// is what keeps it O(ready) rather than O(queued) - depends on that.
    void updateLava(const BudgetCheck& budgetSpent);
    void scheduleLavaUpdate(int x, int y, int z);

    /// Fire, on its own cadence again. It both spreads and dies out, so unlike
    /// the fluids it reschedules itself rather than waiting for an edit nearby.
    void updateFire(const BudgetCheck& budgetSpent);
    void scheduleFireUpdate(int x, int y, int z);

    /// The reference's fluid mixing. Returns true when the cell was consumed,
    /// so the caller stops treating it as lava.
    bool coolLava(const glm::ivec3& p, BlockId lava);

    /// Drops sand and gravel that has nothing under it, one block per step.
    ///
    /// Stepwise rather than a falling entity, which is what the reference uses.
    /// An entity buys a smooth animation and costs a whole spawn/land/merge
    /// path; a scheduled move down one block reuses the queue that is already
    /// here and cannot leave anything in mid-air.
    void updateFalls(const BudgetCheck& budgetSpent);
    void scheduleFallUpdate(int x, int y, int z);

    /// Random ticks: crops, stems, tilled ground and nether wart.
    ///
    /// **Sampled per chunk rather than globally**, which is the reference's own
    /// arrangement and the only one that gives a sane rate: a fixed number of
    /// cells per loaded chunk per tick means a given cell comes up about every
    /// three and a half minutes however far you can see, where a fixed global
    /// budget would slow every farm down as the render distance grew.
    void updateGrowth(const BudgetCheck& budgetSpent);
    /// Whether tilled ground at this cell has water within the reference's own
    /// nine-by-nine box, at its level or one above.
    bool farmlandIsHydrated(int x, int y, int z) const;

    /// Advances one crop, stem, farmland or wart cell. Split out of the sampler
    /// so the same rule can be reached by bone meal, which is a random tick you
    /// asked for.
    void growOne(const glm::ivec3& at);
    /// A cell water may occupy, one it may still drain downward out of, and
    /// whether it spreads sideways at all. **The last two are different
    /// questions**: a cell resting on water can neither drain nor pool.
    bool fluidCanEnter(int x, int y, int z) const;
    bool fluidCanDrainFrom(int x, int y, int z) const;
    bool fluidFeedsSideways(int x, int y, int z) const;
    /// Steps to the nearest cell water could fall from, or 1000 within four.
    int slopeDistance(int x, int z, int y, int fromDirection) const;
    /// Whether water at `from` runs this way - true only for the direction or
    /// directions whose way down is nearest.
    bool fluidSpreadsToward(const glm::ivec3& from, std::size_t direction) const;
    /// Notes that a chunk's light changed. Collected rather than acted on, then
    /// applied once per frame.
    void lightChangedAt(int x, int y, int z);
    void flushLightDirty();

    /// How many jobs of one kind may be outstanding while playing. Kept short on
    /// purpose: a longer queue only produces results for places the player has
    /// already left. Startup ignores it, because nothing is moving yet.
    std::size_t jobCapacity() const;

    std::uint32_t m_seed;
    int m_visibleRadius;
    int m_loadRadius;
    int m_unloadRadius;
    /// Chunks nearer than this carry their decoration. Starts at the render
    /// distance, which is the tier turned off.
    int m_detailRadius;
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

    /// Membership of `m_pendingMesh`, so queueing is O(1).
    ///
    /// The queue used to be searched linearly on every push. Flying with a
    /// large render distance puts thousands of chunks in it and pushes hundreds
    /// per chunk step - light propagation alone dirties most of the streaming
    /// frontier - so that search became a real cost on the one thread the
    /// streaming budget is measured against.
    std::unordered_set<ChunkCoord> m_pendingMeshSet;

    /// Submitted but not yet collected. Stops the same chunk being queued twice.
    std::unordered_set<ChunkCoord> m_loadInFlight;
    std::unordered_set<ChunkCoord> m_meshInFlight;

    /// Collected from workers but not yet handed to the renderer. Uploading is
    /// main-thread work with a real cost, so it is metered like everything else.
    std::vector<MeshedChunk> m_readyMeshes;

    // Light propagation runs on the main thread, because it crosses chunk
    // boundaries freely and so cannot be handed a self-contained snapshot the
    // way generation and meshing are.
    std::deque<glm::ivec3> m_skyAdditions;
    std::deque<glm::ivec3> m_blockAdditions;
    std::deque<LightRemoval> m_skyRemovals;
    std::deque<LightRemoval> m_blockRemovals;

    /// Columns whose sky light has already been traced.
    std::unordered_set<std::uint64_t> m_litColumns;

    /// Chunks needing a rebuild because light moved through them.
    std::unordered_set<ChunkCoord> m_lightDirty;

    /// Water cells whose supply may have changed, each held back until its own
    /// due time. The reference spreads one block every five ticks, and without
    /// that pacing a stream simply appears at its full extent in one frame.
    struct PendingFluid {
        glm::ivec3 position;
        std::chrono::steady_clock::time_point due;
        /// Only a lit charge uses this: how many blinks are left before it goes.
        int blinks = 0;
    };
    std::deque<PendingFluid> m_fluidUpdates;

    /// Lava's own, drained on its own slower cadence.
    std::deque<PendingFluid> m_lavaUpdates;

    /// Lit charges, and the cells that finished counting down this frame.
    std::deque<PendingFluid> m_tntFuses;
    std::vector<glm::ivec3> m_detonations;
    void updateTnt(const BudgetCheck& budgetSpent);

    /// Burning cells due for another look.
    std::deque<PendingFluid> m_fireUpdates;    /// Fire's own randomness. Kept here rather than taken from a shared
    /// generator so a burning forest cannot perturb worldgen or spawning.
    std::uint32_t m_fireRandom = 0x9E3779B9u;

    /// Whether the sky is currently dropping something. Read only by the fire
    /// update.
    bool m_precipitating = false;
    /// Sand and gravel that may have lost its footing, on the same pacing.
    std::deque<PendingFluid> m_fallUpdates;

    /// The growth sampler's own clock and randomness, kept apart from the
    /// fire's for the same reason that one is kept apart from worldgen's: a
    /// field growing must not shift what a burning forest does next.
    std::chrono::steady_clock::time_point m_nextGrowthTick{};
    std::uint32_t m_growthRandom = 0x85EBCA6Bu;

    /// Plants a flow destroyed this frame, waiting to be turned into drops.
    std::vector<WashedBlock> m_washedBlocks;

    /// Blocks that started falling this frame, waiting to become entities.
    std::vector<WashedBlock> m_detachedBlocks;

    ChunkCoord m_centre{0, 0, 0};
    bool m_hasCentre = false;
    /// Set when the render distance shrinks, so the next update drops meshes
    /// that are now out of range instead of leaving them on screen.
    bool m_radiusShrunk = false;
    std::size_t m_savedChunkCount = 0;
};

} // namespace game
