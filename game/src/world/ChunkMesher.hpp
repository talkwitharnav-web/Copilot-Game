#pragma once

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

namespace game {

class Chunk;

/// Turns blocks into triangles, emitting a face only where a solid block touches
/// air. Interior faces are never generated, which is the single idea that makes
/// voxel worlds affordable to render.
///
/// Deliberately a pure function: it reads the chunk, returns vertex data, and
/// touches neither the GPU nor any global state. That is what lets it move to a
/// worker thread at M12 without restructuring anything around it.
engine::MeshData meshChunk(const Chunk& chunk, const glm::vec3& originOffset);

} // namespace game
