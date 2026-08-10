#pragma once

#include "world/Chunk.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace game {

/// A chunk plus one cell of its surroundings, in blocks and light.
///
/// Ambient occlusion samples diagonally, so a face on a chunk edge needs cells
/// from as many as three neighbouring chunks at once. Six face borders cannot
/// supply that; a padded copy can, and it also turns every neighbour lookup in
/// the mesher into a plain array index instead of a chain of bounds tests.
///
/// A copy rather than pointers into the world, because meshing runs on a worker
/// thread and the main thread may unload or edit those chunks meanwhile.
struct ChunkVolume {
    static constexpr int kPad = 1;
    static constexpr int kSpan = Chunk::kSize + 2 * kPad;

    /// Coordinates run from -1 to Chunk::kSize inclusive.
    static constexpr std::size_t index(int x, int y, int z) {
        return static_cast<std::size_t>(x + kPad) + static_cast<std::size_t>(z + kPad) * kSpan +
               static_cast<std::size_t>(y + kPad) * kSpan * kSpan;
    }

    BlockId blockAt(int x, int y, int z) const { return blocks[index(x, y, z)]; }
    std::uint8_t lightAt(int x, int y, int z) const { return light[index(x, y, z)]; }
    bool waterloggedAt(int x, int y, int z) const {
        return (flags[index(x, y, z)] & kWaterlogged) != 0u;
    }
    /// Which half of a double chest this cell is.
    ///
    /// **Filled in by the caller, not worked out here**, because deciding it
    /// means walking a run of chests that can be longer than this volume's one
    /// cell of padding - and only the main thread can see that far.
    ChestHalf chestHalfAt(int x, int y, int z) const {
        return static_cast<ChestHalf>((flags[index(x, y, z)] >> kChestHalfShift) & 3u);
    }

    static constexpr std::uint8_t kWaterlogged = 1u << 0;
    static constexpr int kChestHalfShift = 1;

    std::array<BlockId, kSpan * kSpan * kSpan> blocks{};
    /// Sky light in the high nibble, block light in the low one.
    std::array<std::uint8_t, kSpan * kSpan * kSpan> light{};
    /// Per-cell facts the mesher cannot derive from `blocks` alone. A byte
    /// rather than the chunk's packed bit: this is a scratch copy handed to a
    /// worker, and unpacking once is cheaper than masking per lookup.
    std::array<std::uint8_t, kSpan * kSpan * kSpan> flags{};
};

/// Geometry for one chunk, split by how it has to be drawn.
///
/// Translucent faces must be drawn after every opaque face in the scene, not
/// merely after the ones in their own chunk, so they cannot share a buffer.
struct ChunkMeshes {
    engine::MeshData opaque;
    engine::MeshData translucent;
};

/// Turns blocks into triangles, emitting a face only where a solid block touches
/// air. Interior faces are never generated, which is the single idea that makes
/// voxel worlds affordable to render.
///
/// Pure: it reads only its arguments, returns vertex data, and touches neither
/// the GPU nor any global state. That is what lets it run on a worker thread.
ChunkMeshes meshChunk(const ChunkVolume& volume, const glm::vec3& originOffset);

} // namespace game
