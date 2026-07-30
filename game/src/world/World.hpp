#pragma once

#include "world/Chunk.hpp"
#include "world/TerrainGenerator.hpp"
#include "world/WorldStore.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace game {

/// How tall the world is, in chunks. Terrain never reaches the top, so the
/// upper chunks exist purely as building room.
constexpr int kWorldHeightChunks = 3;

/// Chunks stay loaded within this many chunks of the player, horizontally.
constexpr int kLoadRadiusChunks = 6;

/// A chunk is only meshed once its four horizontal neighbours exist, so the
/// visible radius is one less than the loaded radius. Meshing against a missing
/// neighbour emits a wall of faces at the frontier that then has to be undone.
constexpr int kVisibleRadiusChunks = kLoadRadiusChunks - 1;

/// Unload only past this, so pacing back and forth across the boundary does not
/// thrash chunks in and out.
constexpr int kUnloadRadiusChunks = kLoadRadiusChunks + 2;

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
    World(std::uint32_t seed, std::filesystem::path saveRoot);

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
    std::size_t pendingChunkCount() const { return m_pendingLoad.size() + m_pendingMesh.size(); }

    /// Writes every modified chunk still in memory. Call before shutting down;
    /// chunks that unload during play are saved as they go.
    void saveAll();

    /// Persistence for anything that is not block data, such as where the player
    /// was standing.
    const WorldStore& store() const { return m_store; }

    std::size_t savedChunkCount() const { return m_savedChunkCount; }

private:
    struct ChunkSlot {
        Chunk blocks;
        bool meshed = false;
        /// Set the moment a block is changed. Only these chunks are ever
        /// written: everything else regenerates from the seed.
        bool modified = false;
    };

    const Chunk* chunkAt(const ChunkCoord& coord) const;
    bool hasChunk(const ChunkCoord& coord) const;
    bool neighboursLoaded(const ChunkCoord& coord) const;
    void markDirty(const ChunkCoord& coord);
    void refreshQueues(const ChunkCoord& centre);
    engine::MeshData meshOne(const ChunkCoord& coord) const;
    void saveIfModified(const ChunkCoord& coord, ChunkSlot& slot);

    std::uint32_t m_seed;
    WorldStore m_store;
    std::unordered_map<ChunkCoord, ChunkSlot> m_chunks;

    // Deliberately vectors rather than sets: they are rebuilt whenever the
    // player crosses a chunk boundary, and keeping them ordered by distance is
    // what makes the world fill in from the player outwards.
    std::vector<ChunkCoord> m_pendingLoad;
    std::vector<ChunkCoord> m_pendingMesh;

    ChunkCoord m_centre{0, 0, 0};
    bool m_hasCentre = false;
    std::size_t m_savedChunkCount = 0;
};

} // namespace game
