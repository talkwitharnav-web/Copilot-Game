#pragma once

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

namespace game {

class Chunk;

/// The six chunks touching this one. Any of them may be null, which is treated
/// as air.
///
/// Passing these in rather than letting the mesher reach into a world container
/// is what keeps meshing a pure function. It also matters for size: without
/// neighbours, every chunk emits a full face sheet against its neighbour, and
/// those faces are buried where nobody can ever see them.
struct ChunkNeighbours {
    const Chunk* negativeX = nullptr;
    const Chunk* positiveX = nullptr;
    const Chunk* negativeY = nullptr;
    const Chunk* positiveY = nullptr;
    const Chunk* negativeZ = nullptr;
    const Chunk* positiveZ = nullptr;
};

/// Turns blocks into triangles, emitting a face only where a solid block touches
/// air. Interior faces are never generated, which is the single idea that makes
/// voxel worlds affordable to render.
///
/// Deliberately a pure function: it reads only its arguments, returns vertex
/// data, and touches neither the GPU nor any global state. That is what lets it
/// move to a worker thread at M12 without restructuring anything around it.
engine::MeshData meshChunk(const Chunk& chunk, const ChunkNeighbours& neighbours, const glm::vec3& originOffset);

} // namespace game
