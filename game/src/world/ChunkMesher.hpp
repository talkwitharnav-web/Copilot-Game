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

/// The six axis-aligned face directions, in **the order the mesher emits them**.
///
/// **This enum is the table, and nothing may re-derive the order from anywhere
/// else.** `translucentFacings` is indexed by it, `facingNormal` answers from
/// it, and a `static_assert` in `ChunkMesher.cpp` proves both still agree with
/// the mesher's own `kFaces` array. Reorder `kFaces`, or insert a face into it,
/// and that assert is what fails - which is the point, because a range would
/// then be labelled with a direction it does not hold, and **no consumer could
/// tell**: the indices are still valid and still draw, they simply mean
/// something else. There is no runtime symptom to notice.
enum class MeshFacing : std::uint8_t {
    PosX,
    NegX,
    PosY,
    NegY,
    PosZ,
    NegZ,
    Count,
};

constexpr std::size_t kMeshFacings = static_cast<std::size_t>(MeshFacing::Count);

/// The outward normal of a facing.
///
/// Here rather than at the reader, so "which of these six points away from the
/// camera" is answered from the same table that decided the order. A renderer
/// keeping its own copy of these vectors is a value derived somewhere other
/// than the table that owns it, which is how this codebase loses afternoons.
constexpr glm::ivec3 facingNormal(MeshFacing facing) {
    switch (facing) {
    case MeshFacing::PosX:
        return {1, 0, 0};
    case MeshFacing::NegX:
        return {-1, 0, 0};
    case MeshFacing::PosY:
        return {0, 1, 0};
    case MeshFacing::NegY:
        return {0, -1, 0};
    case MeshFacing::PosZ:
        return {0, 0, 1};
    case MeshFacing::NegZ:
        return {0, 0, -1};
    case MeshFacing::Count:
        break;
    }
    return {0, 0, 0};
}

/// A contiguous run of indices inside one `MeshData`.
///
/// `count == 0` is the ordinary case, not an error - most chunks have
/// translucent faces pointing only one or two ways, and a great many have none
/// at all. **A zero-length draw must be skipped rather than issued**: it costs
/// a command-buffer entry and, on some drivers, a pipeline flush, for nothing.
struct IndexRange {
    std::uint32_t first = 0;
    std::uint32_t count = 0;

    constexpr bool empty() const { return count == 0; }
};

/// Geometry for one chunk, split by how it has to be drawn.
///
/// Translucent faces must be drawn after every opaque face in the scene, not
/// merely after the ones in their own chunk, so they cannot share a buffer.
struct ChunkMeshes {
    engine::MeshData opaque;
    engine::MeshData translucent;

    /// Where each face direction's translucent indices sit inside
    /// `translucent.indices`, indexed by `MeshFacing`.
    ///
    /// **A description of the emission order, never a change to it.** The
    /// mesher walks the six directions in `MeshFacing` order and finishes one
    /// before starting the next, so each direction's quads are already
    /// contiguous; these are the boundaries that were always there and were
    /// simply not written down. Every index buffer is byte-identical to the one
    /// the same volume produced before this existed.
    ///
    /// **Nothing reads these today, and that is deliberate - do not "fix" the
    /// renderer to make it.** They were added to let the forward pass draw the
    /// away-facing directions first, because that pass blends *and* writes depth
    /// (kept on purpose: rain rejection depends on water writing depth), so
    /// within one buffer the near face of a volume drawn before its far face
    /// depth-rejects it. **That reordering turned out to be worth nothing.** The
    /// pipeline culls back faces - `VK_CULL_MODE_BACK_BIT`, the `PipelineDesc`
    /// default, and not dynamic state - and every quad here is single-sided and
    /// wound outward, so the directions pointing away from the camera rasterise
    /// **no fragments at all**; ordering draws that produce nothing cannot
    /// change the image, at any recompute frequency. Nor can a per-direction
    /// order settle the case that is left - two toward-facing surfaces at
    /// different depths - because those share a direction and so share a range.
    /// `Renderer` draws `0 .. translucentShaped.first` as a single range, and is
    /// right to.
    ///
    /// **The one real use left is to stop issuing the away-facing three**,
    /// trading extra draw calls per chunk for the vertex work of quads that are
    /// discarded straight after transform. Measured before deciding, rather than
    /// argued: **1.55 ms GPU against an 8.33 ms frame - 18.6 %, and the frame is
    /// CPU-bound.** Paying CPU to save GPU is the wrong direction on the wrong
    /// axis today. If that ever inverts, the numbers are already here.
    ///
    /// **What a consumer would have to do**, written down because only the
    /// mesher knows what these guarantee. A direction's outward normal is
    /// `facingNormal(f)`, plain world-axis ±X/±Y/±Z - `static_assert`ed in
    /// `ChunkMesher.cpp` against the face table the emitter actually walks, so
    /// it cannot drift from it - and greedy merging never spans two directions
    /// or two slices, so a range is a *pure* direction rather than a mostly-one.
    /// All six sit in the same index buffer as everything else, so choosing
    /// among them is `firstIndex`/`indexCount` on the draw already being issued:
    /// no rebind, no second buffer, no re-mesh when the camera moves. A
    /// direction faces away from the eye when
    /// `dot(facingNormal(f), boxCentre - eye) > 0`, decided per chunk from the
    /// bounds already computed for frustum culling. **Skip every `empty()`
    /// range** - see `IndexRange`.
    ///
    /// The six runs are contiguous and in order, starting at 0, and together
    /// with `translucentShaped` they exactly tile `translucent.indices`.
    std::array<IndexRange, kMeshFacings> translucentFacings{};

    /// The tail: translucent geometry from the shape pass, which emits a whole
    /// block's faces together and so belongs to no single direction.
    ///
    /// **Called out rather than folded into the six**, because a reader that
    /// assumed the six covered the buffer would silently stop drawing every
    /// stained pane in the world. Stained panes are the only thing in it today.
    ///
    /// **Unlike the six, this one is read.** `Renderer` draws
    /// `0 .. translucentShaped.first` as one range and then this as a second,
    /// which is what lets the tail be ordered against the rest at all: it has
    /// no direction, so there is no right place for it inside a directional
    /// order, and drawing it last is the sane default - a pane is far more
    /// often in front of water than inside it.
    IndexRange translucentShaped{};
};

/// How much of a chunk is turned into triangles.
///
/// **An argument, never something looked up while the job runs.** Meshing is a
/// pure function of its input volume and runs on a worker thread; reading the
/// player's position from inside one would make two chunks meshed in the same
/// frame disagree about where the boundary was. The owning world decides the
/// tier on the main thread and hands it over with the volume.
enum class MeshDetail : std::uint8_t {
    /// Everything. What every chunk near the player gets.
    Full,
    /// Blocks and their baked lighting, with `isDistantDecoration` left out.
    /// The silhouette is identical; the grass is not there.
    TerrainOnly,
};

/// Turns blocks into triangles, emitting a face only where a solid block touches
/// air. Interior faces are never generated, which is the single idea that makes
/// voxel worlds affordable to render.
///
/// Pure: it reads only its arguments, returns vertex data, and touches neither
/// the GPU nor any global state. That is what lets it run on a worker thread.
ChunkMeshes meshChunk(const ChunkVolume& volume, const glm::vec3& originOffset,
                      MeshDetail detail = MeshDetail::Full);

} // namespace game
