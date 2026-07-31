#pragma once

#include "world/Chunk.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <array>

namespace game {

/// The single layer of a neighbouring chunk that touches this one.
///
/// A copy rather than a pointer into the world, because meshing runs on a worker
/// thread and the main thread may unload or edit that neighbour meanwhile. One
/// slab is 1 KB against the 32 KB of copying a whole neighbour chunk.
struct ChunkBorder {
    bool present = false;
    std::array<BlockId, Chunk::kSize * Chunk::kSize> blocks{};

    /// Indexed by the two axes the face does not lie along, lower axis first:
    /// (y,z) for an X face, (x,z) for a Y face, (x,y) for a Z face.
    BlockId at(int a, int b) const { return present ? blocks[a * Chunk::kSize + b] : BlockId::Air; }
};

/// The six chunks touching this one. An absent border is treated as air.
///
/// Passing these in rather than letting the mesher reach into a world container
/// is what keeps meshing a pure function. It also matters for size: without
/// neighbours, every chunk emits a full face sheet against its neighbour, and
/// those faces are buried where nobody can ever see them.
struct ChunkNeighbours {
    ChunkBorder negativeX;
    ChunkBorder positiveX;
    ChunkBorder negativeY;
    ChunkBorder positiveY;
    ChunkBorder negativeZ;
    ChunkBorder positiveZ;
};

/// Turns blocks into triangles, emitting a face only where a solid block touches
/// air. Interior faces are never generated, which is the single idea that makes
/// voxel worlds affordable to render.
///
/// Pure: it reads only its arguments, returns vertex data, and touches neither
/// the GPU nor any global state. That is what lets it run on a worker thread.
engine::MeshData meshChunk(const Chunk& chunk, const ChunkNeighbours& neighbours, const glm::vec3& originOffset);

} // namespace game
