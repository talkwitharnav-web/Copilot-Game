#pragma once

#include "world/Chunk.hpp"
#include "world/TerrainGenerator.hpp"

#include <engine/render/MeshData.hpp>

#include <cstdint>
#include <vector>

namespace game {

/// Owns every chunk and answers questions in world block coordinates.
///
/// **This is the single owner of block data, and only the main thread mutates
/// it.** Physics, raycasting and meshing all read through here; none of them
/// writes. That ownership rule is what keeps the M12 job system a migration
/// rather than a rewrite.
class World {
public:
    World(std::uint32_t seed, int chunksX, int chunksY, int chunksZ);

    /// Anything outside the generated volume reads as air.
    BlockId blockAt(int x, int y, int z) const;
    bool isSolid(int x, int y, int z) const;
    bool inBounds(int x, int y, int z) const;

    /// Changes one block and reports which chunk meshes are now stale.
    ///
    /// Editing a block on a chunk border exposes or hides a face belonging to
    /// the *neighbouring* chunk's mesh, so more than one chunk usually has to be
    /// rebuilt. Missing those neighbours leaves holes in the world.
    ///
    /// Returns an empty list if the coordinate is out of bounds or the block was
    /// already what was asked for.
    std::vector<std::size_t> setBlock(int x, int y, int z, BlockId block);

    /// Highest solid block in a column, or -1 if the column is empty. Used to
    /// place the player without dropping them inside terrain.
    int highestSolid(int x, int z) const;

    /// One mesh per chunk, each meshed with its real neighbours so no faces are
    /// generated between two solid blocks across a chunk boundary.
    std::vector<engine::MeshData> buildMeshes() const;

    /// Rebuilds a single chunk's mesh. The index matches `buildMeshes()` order.
    engine::MeshData buildChunkMesh(std::size_t index) const;

    int blocksX() const { return m_chunksX * Chunk::kSize; }
    int blocksY() const { return m_chunksY * Chunk::kSize; }
    int blocksZ() const { return m_chunksZ * Chunk::kSize; }
    std::size_t chunkCount() const { return m_chunks.size(); }
    std::uint32_t seed() const { return m_seed; }

private:
    const Chunk* chunkAt(int cx, int cy, int cz) const;
    std::size_t chunkIndex(int cx, int cy, int cz) const;
    std::uint32_t m_seed;
    int m_chunksX;
    int m_chunksY;
    int m_chunksZ;
    std::vector<Chunk> m_chunks;
};

} // namespace game
