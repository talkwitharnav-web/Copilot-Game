#include "world/World.hpp"

#include "world/ChunkMesher.hpp"

#include <glm/glm.hpp>

namespace game {
namespace {

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

} // namespace

World::World(std::uint32_t seed, int chunksX, int chunksY, int chunksZ)
    : m_seed(seed), m_chunksX(chunksX), m_chunksY(chunksY), m_chunksZ(chunksZ) {
    m_chunks.resize(static_cast<std::size_t>(chunksX * chunksY * chunksZ));

    for (int y = 0; y < m_chunksY; ++y) {
        for (int z = 0; z < m_chunksZ; ++z) {
            for (int x = 0; x < m_chunksX; ++x) {
                m_chunks[chunkIndex(x, y, z)] = generateChunk(m_seed, ChunkCoord{x, y, z});
            }
        }
    }
}

std::size_t World::chunkIndex(int cx, int cy, int cz) const {
    return static_cast<std::size_t>((cy * m_chunksZ + cz) * m_chunksX + cx);
}

const Chunk* World::chunkAt(int cx, int cy, int cz) const {
    if (cx < 0 || cy < 0 || cz < 0 || cx >= m_chunksX || cy >= m_chunksY || cz >= m_chunksZ) {
        return nullptr;
    }
    return &m_chunks[chunkIndex(cx, cy, cz)];
}

BlockId World::blockAt(int x, int y, int z) const {
    const Chunk* chunk = chunkAt(floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize));
    if (chunk == nullptr) {
        return BlockId::Air;
    }
    return chunk->at(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

bool World::isSolid(int x, int y, int z) const {
    return game::isSolid(blockAt(x, y, z));
}

bool World::inBounds(int x, int y, int z) const {
    return x >= 0 && y >= 0 && z >= 0 && x < blocksX() && y < blocksY() && z < blocksZ();
}

std::vector<std::size_t> World::setBlock(int x, int y, int z, BlockId block) {
    if (!inBounds(x, y, z) || blockAt(x, y, z) == block) {
        return {};
    }

    const int cx = floorDiv(x, Chunk::kSize);
    const int cy = floorDiv(y, Chunk::kSize);
    const int cz = floorDiv(z, Chunk::kSize);
    const int lx = floorMod(x, Chunk::kSize);
    const int ly = floorMod(y, Chunk::kSize);
    const int lz = floorMod(z, Chunk::kSize);

    m_chunks[chunkIndex(cx, cy, cz)].set(lx, ly, lz, block);

    std::vector<std::size_t> dirty{chunkIndex(cx, cy, cz)};

    // Only a block touching a chunk face can affect a neighbour's mesh.
    const auto addNeighbour = [&](int nx, int ny, int nz) {
        if (chunkAt(nx, ny, nz) != nullptr) {
            dirty.push_back(chunkIndex(nx, ny, nz));
        }
    };
    if (lx == 0) {
        addNeighbour(cx - 1, cy, cz);
    }
    if (lx == Chunk::kSize - 1) {
        addNeighbour(cx + 1, cy, cz);
    }
    if (ly == 0) {
        addNeighbour(cx, cy - 1, cz);
    }
    if (ly == Chunk::kSize - 1) {
        addNeighbour(cx, cy + 1, cz);
    }
    if (lz == 0) {
        addNeighbour(cx, cy, cz - 1);
    }
    if (lz == Chunk::kSize - 1) {
        addNeighbour(cx, cy, cz + 1);
    }

    return dirty;
}

int World::highestSolid(int x, int z) const {
    for (int y = blocksY() - 1; y >= 0; --y) {
        if (isSolid(x, y, z)) {
            return y;
        }
    }
    return -1;
}

engine::MeshData World::buildChunkMesh(std::size_t index) const {
    const int cx = static_cast<int>(index) % m_chunksX;
    const int cz = (static_cast<int>(index) / m_chunksX) % m_chunksZ;
    const int cy = static_cast<int>(index) / (m_chunksX * m_chunksZ);

    ChunkNeighbours neighbours;
    neighbours.negativeX = chunkAt(cx - 1, cy, cz);
    neighbours.positiveX = chunkAt(cx + 1, cy, cz);
    neighbours.negativeY = chunkAt(cx, cy - 1, cz);
    neighbours.positiveY = chunkAt(cx, cy + 1, cz);
    neighbours.negativeZ = chunkAt(cx, cy, cz - 1);
    neighbours.positiveZ = chunkAt(cx, cy, cz + 1);

    const glm::vec3 origin{static_cast<float>(cx * Chunk::kSize), static_cast<float>(cy * Chunk::kSize),
                           static_cast<float>(cz * Chunk::kSize)};

    return meshChunk(m_chunks[index], neighbours, origin);
}

std::vector<engine::MeshData> World::buildMeshes() const {
    std::vector<engine::MeshData> meshes;
    meshes.reserve(m_chunks.size());

    for (std::size_t i = 0; i < m_chunks.size(); ++i) {
        meshes.push_back(buildChunkMesh(i));
    }

    return meshes;
}

} // namespace game
