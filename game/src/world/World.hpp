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

/// Forward-declared rather than included: `Fluid.hpp` owns this enum and
/// includes *this* header, so the include cannot go the other way. Legal
/// because the underlying type is fixed at its definition, and it is only ever
/// passed and compared here - never sized, never switched on.
namespace fluid {
enum class FluidKind : std::uint8_t;
}

/// Fallback render distance when nothing else specifies one.
constexpr int kDefaultVisibleRadiusChunks = 5;

/// A chunk's drawable geometry has changed. `removed` means the chunk left the
/// world and its meshes should be released.
struct ChunkMeshUpdate {
    ChunkCoord coord;
    engine::MeshData mesh;
    engine::MeshData translucentMesh;
    bool removed = false;
    /// Where each face direction's indices sit inside `translucentMesh`, and
    /// the shape pass's directionless tail. Passed straight through from
    /// `ChunkMeshes`; see it for what they are for. Both are empty on a
    /// `removed` update, which has no geometry to describe.
    std::array<IndexRange, kMeshFacings> translucentFacings{};
    IndexRange translucentShaped{};
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
    /// horizontal neighbours exist - meshing against a missing neighbour emits a
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

    /// The y a body's feet rest at on top of a column.
    ///
    /// **Not `highestSolid() + 1`**, which is what three separate places used
    /// to write: that answers -1 for a column no chunk is loaded for, and -1 + 1
    /// is the bottom of the world. Falls back to the generator, which is the
    /// right answer for ground nobody has touched and is always available.
    int groundHeight(int x, int z) const;

    /// Changes one block and marks every affected chunk for re-meshing.
    /// Why a block left the grid, stated by the producer.
    ///
    /// **One channel carried five different events and the consumer had to
    /// guess between them**, which is what this exists to end. The reader has
    /// to treat them differently in at least three ways and every one of them
    /// was being inferred from the world *after* the fact:
    ///
    ///  - the cobweb rule. minecraft.wiki [[Cobweb]]: a web "drops one piece of
    ///    string if broken with a non-Silk Touch sword, **if water touches or
    ///    flows over it**, or a piston pushes it". Only `Flow` meets that
    ///    condition, so only `Flow` may be handed a sword-shaped break context;
    ///    a leaf decaying and a torch losing its floor are bare hands.
    ///  - `Copy` removes nothing at all. Bone meal on a tall flower pays a
    ///    flower and leaves the plant standing, so a reader that clears the
    ///    twin, plays a break sound or spawns break particles for it is wrong
    ///    three times over. That was being detected by re-reading the cell and
    ///    asking whether it had been vacated - true today and a trap the moment
    ///    anything else writes to that cell first.
    ///  - the twin of a two-block plant. `World` now clears it itself, silently
    ///    (see `setBlock`), so **no event on this channel is ever a half whose
    ///    other half is also owed**. A reader that pays per event pays once.
    enum class Removal : std::uint8_t {
        /// A spreading fluid swept it aside. The only cause water was ever
        /// involved in, and so the only one the cobweb rule fires for.
        Flow,
        /// It lost the block it was standing on. `updateSupports`.
        Support,
        /// A leaf too far from any log. `updateLeafDecay`.
        Decay,
        /// Sugar cane whose water went. It pulls itself out of the ground.
        Uprooted,
        /// **Nothing was removed.** Bone meal on a two-block flower pays a copy
        /// of the flower and the plant is still standing; the position names
        /// where the item is owed, not a cell that changed.
        Copy,
    };

    /// A block a spreading flow destroyed, and what it was.
    struct WashedBlock {
        glm::ivec3 position;
        BlockId block;
        /// Defaulted so every existing reader compiles unchanged and every
        /// existing producer keeps its meaning: `Flow` is what this channel
        /// carried before the other four arrived.
        Removal cause = Removal::Flow;
    };

    /// Hands over everything water swept aside since the last call.
    ///
    /// Returned rather than dropped here, because `World` has no idea items
    /// exist - the owning loop turns these into drops, exactly as it does for a
    /// plant left hanging when you mine the block under it.
    ///
    /// **Five things ride this channel now, not one, and each says which.**
    /// Water still sweeps plants aside; a leaf that has decayed, a block that
    /// has lost its support and sugar cane whose water went all go out the same
    /// way, because all four are "the world removed a block and something is
    /// owed for it" and a second channel would be a second place for the drop
    /// to be forgotten. Bone meal on a tall flower is the fifth and is the odd
    /// one: it owes an item without removing anything. See `Removal`.
    std::vector<WashedBlock> takeWashedBlocks();

    /// Sand and gravel that just lost its footing, already removed from the
    /// grid. The caller turns each into a falling entity; `World` cannot,
    /// because it has no idea entities exist.
    std::vector<WashedBlock> takeDetachedBlocks();

    /// A cell where water put something out - lava setting, or a fire dying.
    struct FizzEvent {
        glm::ivec3 position;
        /// What the cell holds *now*: `Obsidian`, `Cobblestone` or `Stone`
        /// where lava set, and `Air` or water where a fire went out.
        ///
        /// **Carried rather than looked up on drain**, and that is the point of
        /// the field. By the time anyone reads this the cell is a block like
        /// any other and a later edit may have changed it again, so a channel
        /// reporting only a position would force its reader to re-query a world
        /// that has moved on. The event says what happened; the world says what
        /// is.
        BlockId became;
    };

    /// Cells that hissed since the last call, for whoever owns making noises.
    ///
    /// **The general case of the fizz, and the common one**: flowing lava
    /// meeting flowing water converts with no player anywhere near it, and a
    /// storm puts fires out on its own. `World` cannot play a sound - it has no
    /// visibility of `Sounds` and is not going to be given any, for the same
    /// reason it cannot spawn an item - so it reports the event and the frame
    /// loop decides what it sounds like. Same hand-off as `takeWashedBlocks`.
    ///
    /// **Capped, unlike the other three channels, and deliberately.** A drop
    /// that is never drained is an item the player never receives, so growing
    /// without bound is the lesser evil there; a *sound* that is never drained
    /// is worthless the moment its frame is over, and the buffer would still be
    /// paid for. Past `kMaxPendingFizzes` further events in the same drain
    /// window are dropped rather than queued - see the constant in `World.cpp`
    /// for the number and why.
    std::vector<FizzEvent> takeFizzes();

    /// Starts a charge's fuse. The block must already be `TntPrimed`; this only
    /// schedules when it goes off.
    ///
    /// The one-argument form is the reference's 80-tick fuse - anything lit by
    /// hand, by fire or by redstone. The two-argument form is for a charge set
    /// off by another blast, which the reference gives a much shorter and
    /// randomised fuse so a stack goes up as one blast rather than rippling:
    /// pair it with `blastFuse()`, which owns that range.
    ///
    /// **Priming an already-lit charge shortens its fuse or does nothing; it
    /// never restarts one and never queues a second countdown.** A charge in a
    /// dense stack is re-primed by every blast that reaches it, and a fuse that
    /// restarted would let a big enough pile hold itself lit indefinitely.
    void primeTnt(const glm::ivec3& at);
    void primeTnt(const glm::ivec3& at, std::chrono::milliseconds fuse);

    /// A fresh randomised fuse for a charge set off by a blast, in the range
    /// the reference uses. Not `const`: it advances the world's own ignition
    /// PRNG, which is main-thread simulation state and is not seeded from the
    /// clock. See the constants in `World.cpp` for the number and its source.
    std::chrono::milliseconds blastFuse();

    /// Whether it is currently precipitating overhead.
    ///
    /// Plain data rather than a handle on the weather: the fire update needs to
    /// know one bit, and a fire under an open sky goes out in the rain. Without
    /// this, the first bolt of a storm could burn a forest down permanently -
    /// the reference lights those fires *and* puts them out.
    void setPrecipitating(bool precipitating) { m_precipitating = precipitating; }

    /// Whether precipitation of **any kind** is falling anywhere in the world.
    ///
    /// **Not the same bit as `setPrecipitating`, and the difference is what
    /// makes snow possible.** That one means *"rain is wetting the column the
    /// player is standing in"*: `Main.cpp` computes it as `falls && rainAmount >
    /// 0.2 && kind == Precipitation::Rain`, sampling the player's own biome. It
    /// is exactly right for farmland hydration and for putting fires out, and it
    /// is useless as a snowfall gate, because it goes *false* the moment the
    /// player walks into a biome cold enough to snow - the one place snow needs
    /// it to be true.
    ///
    /// This one means *"something is coming down"* and nothing more. The world
    /// asks `weather::precipitationFor` per column, which already knows the
    /// biome, the height and the freezing jitter, so the rain-or-snow decision
    /// lands where the answer differs - per column - instead of being taken once
    /// under the camera and applied to a thousand chunks.
    ///
    /// Pushed from `Weather::strike`, which is the weather's only world-touching
    /// tick; the comment on that function says why it lives there.
    ///
    /// **No matching getter, deliberately.** The only reader is
    /// `weatherTickColumn`, which is a member and reads the field. A public
    /// accessor with no caller is the exact shape three findings were filed
    /// about tonight, and adding one "for symmetry" is how they start.
    void setWeatherFalling(bool falling) { m_weatherFalling = falling; }

    /// Charges that reached the end of their fuse this frame. Drained by the
    /// caller and turned into blasts there, because `World` reads blocks and
    /// knows nothing about drops, creatures or damage - the same hand-off
    /// `takeDetachedBlocks` already uses.
    std::vector<glm::ivec3> takeDetonations();

    /// Who put a block here. **Leaves are the only block that cares**, and they
    /// care a great deal: minecraft.wiki [[Leaves]] gives Bedrock a
    /// `persistent_bit` - *"If the block persists regardless of having no wood
    /// nearby"* - which is set on a leaf a player placed and clear on one a
    /// tree grew. A player-placed canopy is permanent; a natural one decays
    /// when its tree is felled.
    ///
    /// **Defaulted to `Player` on purpose.** Every existing caller is a player
    /// action or a machine acting on the player's behalf, so none of them had
    /// to change and none of them can accidentally plant a leaf that rots. The
    /// two callers that pass `Natural` are the tree a sapling grows and nothing
    /// else; worldgen never comes through here at all, and a freshly generated
    /// chunk is all zeroes, which is `Natural` - which is why leaves that came
    /// out of the generator decay without anyone writing a bit for them.
    enum class Placement : std::uint8_t { Player, Natural };

    void setBlock(int x, int y, int z, BlockId block, Placement by = Placement::Player);

    /// Bone meal on a crop, a stem or a sapling. **A random tick you asked
    /// for** - it runs the same growth rule the sampler does, so there is no
    /// second idea of what "grow" means that could disagree with it.
    ///
    /// Returns whether anything was actually fed, so the caller knows whether
    /// to spend the item.
    bool applyBoneMeal(const glm::ivec3& at);

    /// Loads, unloads and re-meshes around the player, stopping once the time
    /// budget is spent. Returns only what changed this call.
    ///
    /// Unloading is always finished because it frees memory and is cheap; only
    /// generation and meshing are metered.
    ///
    /// **This is also how startup primes the world.** `Main` spins this in a
    /// loop on a 16 ms budget and draws `loadStatus()` as the progress bar, so
    /// the window keeps pumping while chunks arrive.
    ///
    /// There is deliberately no unbudgeted variant (checked 2026-08-19; what
    /// would make this false is someone adding one). `loadImmediately` was
    /// exactly that, carried a comment claiming it was the startup path, and
    /// had never been called at any commit - it would have blocked the main
    /// thread across sixteen `m_jobs.waitForIdle()` barriers with an unpumped
    /// window.
    std::vector<ChunkMeshUpdate> update(const glm::vec3& playerPosition, float budgetSeconds);

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
    ///
    /// **Every queue that terminates, and deliberately none of the two that do
    /// not.** Fire and lava reschedule themselves for as long as there is
    /// anything to burn, so a world with one lava lake in it would never report
    /// settled and the loading screen would hang for ever; falls and fuses both
    /// run out, and handing the player a world mid-collapse is what leaving
    /// falls out did.
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
        /// Stamped from `m_nextRevision` when the chunk arrives, and again on
        /// every edit. A mesh job records the stamp it started from, and a
        /// result whose stamp no longer matches is thrown away: that is how an
        /// edit made while meshing was in flight is caught, and - because the
        /// stamps come from a world-wide counter that is never reset - how a
        /// chunk that unloaded and came back cannot be handed the mesh of its
        /// own previous life.
        std::uint64_t revision = 0;
        /// The stamp the geometry **currently on the GPU** was built from.
        ///
        /// **Two fields for exactly the reason `detailWanted`/`detailBuilt` are
        /// two fields, and it was missed here for four milestones.** A queued
        /// re-mesh is dropped when the chunk sits outside the visible radius,
        /// and chunks stay resident and drawn two rings past it - so an edit out
        /// there bumped `revision`, lost its queue entry, and left a chunk whose
        /// `meshed` and tiers all agree with themselves. Nothing ever looked at
        /// it again and it drew its pre-edit geometry for the rest of the
        /// session. Comparing the two is what `refreshQueues` recovers from.
        std::uint64_t builtRevision = 0;
    };

    /// A finished generation job.
    struct LoadedChunk {
        ChunkCoord coord;
        Chunk blocks;
        /// Whether these blocks came off disk rather than out of the generator.
        /// Read by `rescanRestoredChunks`, which only sweeps the first kind -
        /// see that function for why the second kind cannot owe anything.
        bool fromDisk = false;
    };

    /// A finished mesh job, tagged with the chunk revision it was built from
    /// and the tier it was built at.
    struct MeshedChunk {
        ChunkCoord coord;
        ChunkMeshes meshes;
        std::uint64_t revision = 0;
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

    /// Re-meshes every chunk a run of chests through `at` reaches.
    ///
    /// **The one block whose edit is not local.** Pairing is derived by walking
    /// to the start of the run and counting off in twos, so removing or adding
    /// one flips which half every chest further along wears - as far as
    /// `kMaxChestRun`, which is two chunks. Invalidating the six face
    /// neighbours, which is right for every other block in the game, leaves the
    /// rest of the row wearing the wrong half until something unrelated dirties
    /// it.
    void invalidateChestRun(const glm::ivec3& at, BlockId chest);

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
    ///
    /// **Metered like the two addition passes it precedes.** A roof thrown over
    /// a lit area tears down every cell under it, and running that to completion
    /// however long it took was the one light pass the frame budget did not
    /// cover. `propagateLight` holds the additions back until both removal
    /// queues are empty, so stopping part-way costs a frame of darkness rather
    /// than a wrong answer.
    void unpropagate(std::deque<LightRemoval>& removals, std::deque<glm::ivec3>& additions, bool sky,
                     const BudgetCheck& budgetSpent);

    void setSkyLightAt(int x, int y, int z, int level);
    void setBlockLightAt(int x, int y, int z, int level);

    /// Light at a cell, or nothing when no chunk holds it.
    ///
    /// **`skyLightAt` cannot tell "dark" from "not here"** - it answers full sky
    /// for both, which is exactly right for the renderer, because it is what
    /// stops the streaming frontier going black. Propagation needs the other
    /// answer: an absent chunk reading as full sky is a *source*, so an addition
    /// popped in one seeds real light into the loaded cells beside it, and a
    /// removal stepping into one satisfies the `litFromAbove` clause and tears
    /// down light that came from somewhere else entirely. Same shape as
    /// `blockIfLoaded`, same reason, same cost - one map lookup.
    std::optional<int> skyLightIfLoaded(int x, int y, int z) const;
    std::optional<int> blockLightIfLoaded(int x, int y, int z) const;

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

    /// Records one hiss, if there is still room for it.
    ///
    /// A function rather than five `push_back`s because the cap is a rule, and
    /// a rule written at four of its five sites is this project's most
    /// expensive bug shape. Every producer goes through here.
    void recordFizz(const glm::ivec3& p, BlockId became);

    /// Drops sand and gravel that has nothing under it, one block per step.
    ///
    /// Stepwise rather than a falling entity, which is what the reference uses.
    /// An entity buys a smooth animation and costs a whole spawn/land/merge
    /// path; a scheduled move down one block reuses the queue that is already
    /// here and cannot leave anything in mid-air.
    void updateFalls(const BudgetCheck& budgetSpent);
    void scheduleFallUpdate(int x, int y, int z);

    /// Breaks whatever has just lost the block it was standing on, and pays for
    /// it.
    ///
    /// **`needsSupportBelow` had exactly one caller and it was in `Main.cpp`'s
    /// break path**, so a torch, a flower, a crop, a rail or a door stayed
    /// floating when its floor was washed away by water, burnt by fire, dropped
    /// out from under it by falling sand, or decayed out from under it by a
    /// leaf. The rule existed, was correct, and did not travel - which is this
    /// project's most expensive bug shape.
    ///
    /// A queue rather than a direct call, for the same three reasons the fall
    /// queue is one: it must not recurse on the C++ stack (a stack of sugar
    /// cane or a column of bamboo is arbitrarily deep), it must not run while a
    /// chunk is still streaming in, and `setBlock` writing Air re-schedules the
    /// cell above, so a column collapses one step at a time with no loop here.
    void updateSupports(const BudgetCheck& budgetSpent);
    void scheduleSupportUpdate(int x, int y, int z);
    /// A leaf that has just lost something beside it, and the pass that asks
    /// whether it can still see wood.
    ///
    /// **This is Bedrock's `update_bit`, kept in a queue instead of a bit.**
    /// minecraft.wiki [[Leaves]] lists `update_bit` alongside `persistent_bit`,
    /// and the legacy algorithm it names only runs its scan on a leaf whose
    /// neighbour has changed. Without that, the random tick is the whole
    /// mechanism and a felled canopy hangs about for minutes at this project's
    /// sampling rate. The random tick stays as the backstop for a canopy that
    /// came off disk already orphaned.
    void updateLeafDecay(const BudgetCheck& budgetSpent);
    void scheduleLeafCheck(int x, int y, int z);
    /// Re-seeds the scheduled-update queues for one chunk that came off disk.
    ///
    /// **Nothing in a save file records a pending update, and this is what
    /// stands in for that.** `m_leafChecks`, `m_supportUpdates` and the fluid
    /// queues are wall-clock deadlines against `steady_clock`, whose epoch does
    /// not survive a process restart - so they cannot be written to disk as
    /// they stand, and writing them as remaining durations would put a fourth
    /// timestamp format in the save file to solve a problem that has a cheaper
    /// answer. The cheaper answer is that **the world itself already records
    /// what is owed**: an orphaned leaf is a leaf with no wood near it whether
    /// or not anyone queued it, and a floating block is floating whether or not
    /// anyone queued it. So the chunk is read once when it arrives and the
    /// queues are rebuilt from what is actually in it.
    ///
    /// **Only chunks that came from disk are swept.** A generated chunk is a
    /// pure function of `(seed, coord)` and contains nothing pending by
    /// construction; a chunk file only exists because something changed the
    /// cell, which is exactly the population that can owe an update.
    ///
    /// Budgeted and one chunk per call, because this is 32,768 cells and it
    /// runs during streaming, when the frame has the least room to spare.
    void rescanRestoredChunks(const BudgetCheck& budgetSpent);
    /// The sweep itself, for one resident chunk. Split out so the budget loop
    /// above holds no per-cell logic.
    void rescanChunk(const ChunkCoord& coord);
    /// One leaf's decay rule, shared by the random tick and the queue so the
    /// two can never disagree about which leaf lives. Returns whether it went.
    bool decayLeafIfOrphaned(const glm::ivec3& at);
    /// Whether this block still has what holds it up in the cell below.
    ///
    /// **The sideways half of the rule is deliberately absent**, and the probe
    /// that proved it can be: of the 803 ids `needsSupportBelow` answers true
    /// for, not one is wall-mounted, so a ladder, a wall torch, a lever, a
    /// button, a cocoa pod and a wall sign never reach this. Losing *their*
    /// wall is still handled only on the break path in `Main.cpp`, because the
    /// function that owns which way a thing leans lives in that file and cannot
    /// be reached from here. Filed as a finding.
    ///
    /// **And it asks whether the cell below is empty, not whether it is a legal
    /// support** - see the body, where a second probe result explains why the
    /// stricter test deletes sugar cane stacks and tall flower heads.
    bool blockHasSupport(BlockId block, int x, int y, int z) const;

    /// Whether the pointed dripstone at this cell still has something holding
    /// it up, at either end.
    ///
    /// **It is the one family in the game that is not held up from below**, so
    /// `blockHasSupport` cannot answer for it and `needsSupportBelow` is false
    /// for it - a stalactite hangs from the ceiling and a stalagmite stands on
    /// the floor, and the same id is both.
    ///
    /// **Which one it is is derived from the world rather than stored, and that
    /// is honest here rather than `CLAUDE.md` bug shape #1.** The reference
    /// keeps a `hanging` boolean beside `dripstone_thickness`; `Block.hpp` has
    /// a single `PointedDripstone` id and no room for either. But direction is
    /// not a lost state in this case - it *is* the support question, and the
    /// support question can only be asked of the world. The walk goes to the
    /// top of the contiguous column and asks what is above it, then to the
    /// bottom and asks what is below it; anchored at either end is anchored.
    /// A column joined to both (the reference's "dripstone column") passes on
    /// the first test and never needs the second.
    bool dripstoneAnchored(int x, int y, int z) const;

    /// Random ticks: crops, stems, tilled ground and nether wart.
    ///
    /// **Sampled per chunk rather than globally**, which is the reference's own
    /// arrangement and the only one that gives a sane rate: a fixed number of
    /// cells per loaded chunk per tick means a given cell comes up about every
    /// three and a half minutes however far you can see, where a fixed global
    /// budget would slow every farm down as the render distance grew.
    void updateGrowth(const BudgetCheck& budgetSpent);

    /// Freezing and settling snow, for one loaded column.
    ///
    /// **Weather is per column and everything else here is per cell**, which is
    /// why this is not another branch of `growOne`. A random tick lands on a
    /// cell at a depth; freezing and snowfall only ever act on whatever is at
    /// the *top* of a column under an open sky, so the sampler picks an `(x, z)`
    /// and this walks down to find the surface. Called from `updateGrowth`'s own
    /// chunk loop so it inherits the tick catch-up, the moving cursor and the
    /// frame budget rather than growing a second, subtly different copy of all
    /// three - the rule that exists in only one of the two places that need it
    /// is this project's most expensive bug shape.
    ///
    /// Does both jobs because both need the same expensive thing: the top of the
    /// column and its sky exposure. Freezing runs whatever the weather - the
    /// reference freezes ponds on a clear night - while settling snow needs
    /// `m_weatherFalling`.
    void weatherTickColumn(int worldX, int worldZ);
    /// Whether tilled ground at this cell has water within the reference's own
    /// nine-by-nine box, at its level or one above.
    bool farmlandIsHydrated(int x, int y, int z) const;

    /// Advances one crop, stem, farmland or wart cell. Split out of the sampler
    /// so the same rule can be reached by bone meal, which is a random tick you
    /// asked for.
    void growOne(const glm::ivec3& at);
    /// Whether a log or bark sits within `farming::kLeafDecayReach` **leaf
    /// steps** of this cell. A flood fill through leaves, not a box test - see
    /// the constant's own comment for why the shape matters.
    bool leafHasWoodNearby(const glm::ivec3& at) const;
    /// Turns a sapling into the tree it names, if it has the room. Returns
    /// whether it grew; a failed attempt is silent and costs the sapling
    /// nothing, which is the reference's own behaviour.
    bool growSaplingAt(const glm::ivec3& at, BlockId sapling);
    /// One copper block's weathering attempt, already past the pre-oxidation
    /// roll. Split out because the neighbourhood scan is 129 cells and reads
    /// nothing else in `growOne`.
    void weatherCopperAt(const glm::ivec3& at, BlockId here);
    /// The one bit of blockstate `BlockId` has no room for: a leaf's
    /// `persistent_bit` and a sapling's `age_bit`, one flag per cell alongside
    /// the waterlogging flags in `Chunk`.
    ///
    /// **The two meanings share one bit safely only because `setBlock` rewrites
    /// it on every single write** - put a sapling where a player-placed leaf
    /// was and it starts at age zero, not half-grown. That line is load-bearing
    /// and its comment says so.
    ///
    /// False when no chunk is loaded for the cell, which is the safe answer for
    /// both readers: an unloaded leaf is not persistent (and the decay pass
    /// refuses to run on a streaming column anyway) and an unloaded sapling is
    /// not ready.
    bool stateBitAt(int x, int y, int z) const;
    void setStateBit(int x, int y, int z, bool value);
    /// Which of the two fluids a rule is being asked about.
    ///
    /// **The five helpers below were water's and lava had its own half-copy of
    /// two of them**, which is `CLAUDE.md` bug shape #14 - a rule that exists,
    /// is correct and is commented in only one of the two places that need it.
    /// Lava had `fluidFeedsSideways`'s body inlined into `updateLava` and no
    /// slope search at all, so it never sought a hole: poured onto a ledge it
    /// fanned out evenly instead of running for the drop. They are one function
    /// each now, taking this, rather than two bodies that have to be kept
    /// agreeing by hand.
    ///
    /// **`fluid::FluidKind`, not a second enum of the same shape.** That header
    /// already owns the concept and `fluid::isKind` already answers "is this
    /// block that fluid"; declaring a private twin here would be the identical
    /// mistake one level up. It is forward-declared at the top of this file
    /// because `Fluid.hpp` includes *this* one.
    using FluidKind = fluid::FluidKind;

    /// A cell that fluid may occupy, one it may still drain downward out of,
    /// and whether it spreads sideways at all. **The last two are different
    /// questions**: a cell resting on fluid can neither drain nor pool.
    bool fluidCanEnter(int x, int y, int z, FluidKind kind) const;
    bool fluidCanDrainFrom(int x, int y, int z) const;
    bool fluidFeedsSideways(int x, int y, int z, FluidKind kind) const;
    /// The block at a cell, or nothing when no chunk holds it.
    ///
    /// **`blockAt` cannot tell those two apart** - it answers `Air` for both -
    /// and the flow search *steers* on the answer, so an unloaded chunk reads
    /// as a hole and every stream near the streaming frontier aims itself at
    /// the void. One map lookup, the same as `blockAt` costs.
    std::optional<BlockId> blockIfLoaded(int x, int y, int z) const;
    /// Steps to the nearest cell the fluid could fall from, or 1000 within
    /// four.
    int slopeDistance(int x, int z, int y, int fromDirection, FluidKind kind) const;
    /// Whether fluid at `from` runs this way - true only for the direction or
    /// directions whose way down is nearest.
    bool fluidSpreadsToward(const glm::ivec3& from, std::size_t direction, FluidKind kind) const;
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

    /// The next mesh-revision stamp, handed out in order and **never reset**.
    ///
    /// It lives here rather than in the slot because the slot is exactly what
    /// does not survive: a per-chunk counter starts again at 0 when a chunk is
    /// unloaded and later regenerated, so a mesh job dispatched during the
    /// chunk's first residency lands afterwards, compares its 0 against the
    /// reloaded slot's 0, and installs pre-unload geometry - permanently, since
    /// the slot is then marked meshed and its tiers agree, and nothing ever
    /// looks at it again. A stamp from here is used once for the whole run.
    std::uint64_t m_nextRevision = 0;

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

    /// The one residency gate all four cell queues go through, and the only
    /// place the reason is written down.
    ///
    /// `blockAt` answers `Air` for a chunk that is not resident, so an update
    /// out there does not merely fail to write - **it decides on a lie**, and
    /// `setBlock` then silently drops the decision. A column on the edge of the
    /// loaded world pours itself into nothing, a stream finds phantom drops in
    /// territory that does not exist and aims itself at them, and a fire burns
    /// air nobody has generated yet. Only the falling queue used to know this.
    ///
    /// Returns true when the update may be decided now. When it may not, the
    /// entry is **put back** at `retryDelay` if its column is still inside the
    /// load radius - the missing chunks are already queued and on their way, so
    /// dropping the update would strand whatever scheduled it - and dropped
    /// only once nothing is going to bring that column back. The entry goes to
    /// the back of its own queue, which is what keeps the deque in due order:
    /// every entry carries the same delay, so one pushed now is due after
    /// everything already in it.
    bool columnReadyOrDeferred(const glm::ivec3& p, std::deque<PendingFluid>& queue,
                               std::chrono::milliseconds retryDelay);

    /// Lava's own, drained on its own slower cadence.
    std::deque<PendingFluid> m_lavaUpdates;

    /// Lit charges, and the cells that finished counting down this frame.
    ///
    /// **The one cell queue that is not in due order**, and `updateTnt` scans
    /// it rather than draining the head for exactly that reason: fuses stopped
    /// being uniform once a blast-lit charge got a shorter one than a hand-lit
    /// one, so the entry at the front is no longer the soonest.
    std::deque<PendingFluid> m_tntFuses;
    std::vector<glm::ivec3> m_detonations;
    void updateTnt(const BudgetCheck& budgetSpent);

    /// Burning cells due for another look.
    std::deque<PendingFluid> m_fireUpdates;

    /// The ignition PRNG - fire spread, lava's obsidian roll and a blast-lit
    /// charge's fuse. Kept here rather than taken from a shared generator so a
    /// burning forest cannot perturb worldgen or spawning.
    std::uint32_t m_fireRandom = 0x9E3779B9u;

    /// Whether **rain** is wetting the player's own column. Read by the fire
    /// update, which puts sky-exposed fires out, and by `farmlandIsHydrated`,
    /// which counts rain as water. Set from `Main.cpp`, which samples the
    /// camera's biome - so it is rain-only and local by construction, and the
    /// comment that used to say "read only by the fire update" was written
    /// before farmland grew its second caller.
    bool m_precipitating = false;

    /// Whether **anything** is falling, anywhere. Set from `Weather::strike`;
    /// see `setWeatherFalling` for why this is a second bit rather than a wider
    /// reading of the one above.
    bool m_weatherFalling = false;
    /// Sand and gravel that may have lost its footing, on the same pacing.
    std::deque<PendingFluid> m_fallUpdates;

    /// Cells whose *neighbours* may have lost their footing. Separate from
    /// `m_fallUpdates` because the two ask opposite questions - that one is
    /// "does this block fall?", this one is "does anything leaning on this
    /// block fall?" - and because a shared queue would make one budget starve
    /// the other.
    std::deque<PendingFluid> m_supportUpdates;

    /// Leaves a felled log or a decayed neighbour has put in doubt. See
    /// `updateLeafDecay` for why this exists at all rather than leaving the
    /// job to the random tick.
    std::deque<PendingFluid> m_leafChecks;

    /// Chunks that arrived from disk and have not been read back into the
    /// queues yet. Coordinates rather than deadlines: this is work owed as soon
    /// as the frame can afford it, not work owed at a time. See
    /// `rescanRestoredChunks`.
    std::deque<ChunkCoord> m_pendingRescan;

    /// The growth sampler's own clock and randomness, kept apart from the
    /// fire's for the same reason that one is kept apart from worldgen's: a
    /// field growing must not shift what a burning forest does next.
    std::chrono::steady_clock::time_point m_nextGrowthTick{};
    std::uint32_t m_growthRandom = 0x85EBCA6Bu;

    /// Where the growth walk starts, so a busy frame does not always starve the
    /// same chunks.
    ///
    /// The sampler walks every loaded chunk and stops the moment the frame's
    /// budget is gone, and `m_chunks` iterates in a stable order - so with a
    /// fixed start the chunks at the far end were sampled only on frames with
    /// time to spare, which while flying is never. An index rather than a saved
    /// iterator on purpose: the map rehashes as chunks stream in and out, and
    /// this only has to *move*, not to resume exactly where it left off.
    std::size_t m_growthCursor = 0;

    /// Plants a flow destroyed this frame, waiting to be turned into drops.
    std::vector<WashedBlock> m_washedBlocks;

    /// Cells that hissed this frame, waiting to be turned into a noise.
    std::vector<FizzEvent> m_fizzes;

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
