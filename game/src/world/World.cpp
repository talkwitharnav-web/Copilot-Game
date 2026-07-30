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

int World::highestSolid(int x, int z) const {
    for (int y = blocksY() - 1; y >= 0; --y) {
        if (isSolid(x, y, z)) {
            return y;
        }
    }
    return -1;
}

std::vector<engine::MeshData> World::buildMeshes() const {
    std::vector<engine::MeshData> meshes;
    meshes.reserve(m_chunks.size());

    for (int y = 0; y < m_chunksY; ++y) {
        for (int z = 0; z < m_chunksZ; ++z) {
            for (int x = 0; x < m_chunksX; ++x) {
                ChunkNeighbours neighbours;
                neighbours.negativeX = chunkAt(x - 1, y, z);
                neighbours.positiveX = chunkAt(x + 1, y, z);
                neighbours.negativeY = chunkAt(x, y - 1, z);
                neighbours.positiveY = chunkAt(x, y + 1, z);
                neighbours.negativeZ = chunkAt(x, y, z - 1);
                neighbours.positiveZ = chunkAt(x, y, z + 1);

                const glm::vec3 origin{static_cast<float>(x * Chunk::kSize), static_cast<float>(y * Chunk::kSize),
                                       static_cast<float>(z * Chunk::kSize)};

                meshes.push_back(meshChunk(m_chunks[chunkIndex(x, y, z)], neighbours, origin));
            }
        }
    }

    return meshes;
}

} // namespace game
