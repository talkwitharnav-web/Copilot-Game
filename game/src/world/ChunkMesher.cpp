#include "world/ChunkMesher.hpp"

#include "world/Chunk.hpp"

#include <array>

namespace game {
namespace {

struct Face {
    glm::ivec3 neighbourOffset;
    /// Corners of the face on a unit cube, counter-clockwise seen from outside.
    /// Reverse any of these and that face vanishes under backface culling.
    std::array<glm::vec3, 4> corners;
    /// Texture coordinates for those same four corners, in the same order.
    /// V grows downward, matching how image rows are stored.
    std::array<glm::vec2, 4> uvs;
    BlockFace facing;
    /// Fixed brightness standing in for lighting, so surface orientation is
    /// readable. Real lighting arrives at M14.
    float shade;
    /// Which world axis the face's normal points along.
    int axis;
    /// World axes that U and V run along. Merged quads scale their texture
    /// coordinates by the extent along these, so the sampler tiles the texture
    /// instead of stretching it. Read off the corner/uv tables rather than
    /// derived, because getting them wrong is silent.
    int uAxis;
    int vAxis;
};

constexpr std::array<glm::vec2, 4> kBottomLeftWinding{glm::vec2{0, 1}, glm::vec2{1, 1}, glm::vec2{1, 0},
                                                      glm::vec2{0, 0}};

constexpr std::array<Face, 6> kFaces{{
    // +X
    {{1, 0, 0},
     {glm::vec3{1, 0, 1}, glm::vec3{1, 0, 0}, glm::vec3{1, 1, 0}, glm::vec3{1, 1, 1}},
     kBottomLeftWinding,
     BlockFace::Side,
     0.72f,
     0,
     2,
     1},
    // -X
    {{-1, 0, 0},
     {glm::vec3{0, 0, 0}, glm::vec3{0, 0, 1}, glm::vec3{0, 1, 1}, glm::vec3{0, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Side,
     0.72f,
     0,
     2,
     1},
    // +Y
    {{0, 1, 0},
     {glm::vec3{0, 1, 1}, glm::vec3{1, 1, 1}, glm::vec3{1, 1, 0}, glm::vec3{0, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Top,
     1.00f,
     1,
     0,
     2},
    // -Y
    {{0, -1, 0},
     {glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{1, 0, 1}, glm::vec3{0, 0, 1}},
     kBottomLeftWinding,
     BlockFace::Bottom,
     0.45f,
     1,
     0,
     2},
    // +Z
    {{0, 0, 1},
     {glm::vec3{0, 0, 1}, glm::vec3{1, 0, 1}, glm::vec3{1, 1, 1}, glm::vec3{0, 1, 1}},
     kBottomLeftWinding,
     BlockFace::Side,
     0.86f,
     2,
     0,
     1},
    // -Z
    {{0, 0, -1},
     {glm::vec3{1, 0, 0}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0}, glm::vec3{1, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Side,
     0.60f,
     2,
     0,
     1},
}};

/// Empty slot in the merge mask. Texture layers are never negative.
constexpr float kNoFace = -1.0f;

} // namespace

engine::MeshData meshChunk(const Chunk& chunk, const ChunkNeighbours& neighbours, const glm::vec3& originOffset) {
    // Face offsets only ever move along a single axis, so at most one coordinate
    // can fall outside the chunk and the neighbour lookup stays unambiguous.
    const auto blockAt = [&](int x, int y, int z) -> BlockId {
        if (x < 0) {
            return neighbours.negativeX.at(y, z);
        }
        if (x >= Chunk::kSize) {
            return neighbours.positiveX.at(y, z);
        }
        if (y < 0) {
            return neighbours.negativeY.at(x, z);
        }
        if (y >= Chunk::kSize) {
            return neighbours.positiveY.at(x, z);
        }
        if (z < 0) {
            return neighbours.negativeZ.at(x, y);
        }
        if (z >= Chunk::kSize) {
            return neighbours.positiveZ.at(x, y);
        }
        return chunk.at(x, y, z);
    };

    engine::MeshData mesh;

    // One slice of the chunk, holding the texture layer of every visible face on
    // it. Reused across all six directions and every slice.
    std::array<float, Chunk::kSize * Chunk::kSize> mask{};

    for (const Face& face : kFaces) {
        // The two axes the face spans. Which is called which does not matter;
        // texture scaling reads the face's own uAxis/vAxis rather than these.
        const int normal = face.axis;
        const int across = (normal + 1) % 3;
        const int down = (normal + 2) % 3;

        for (int slice = 0; slice < Chunk::kSize; ++slice) {
            for (int j = 0; j < Chunk::kSize; ++j) {
                for (int i = 0; i < Chunk::kSize; ++i) {
                    glm::ivec3 p{0};
                    p[normal] = slice;
                    p[across] = i;
                    p[down] = j;

                    const BlockId block = chunk.at(p.x, p.y, p.z);
                    const glm::ivec3 n = p + face.neighbourOffset;

                    // The whole optimisation: a face buried against another
                    // solid block can never be seen, so it is never created.
                    const bool visible = isSolid(block) && !isSolid(blockAt(n.x, n.y, n.z));
                    mask[static_cast<std::size_t>(j) * Chunk::kSize + i] =
                        visible ? blockTextureLayer(block, face.facing) : kNoFace;
                }
            }

            // Greedy merge: grow a run along `across`, then extend it along
            // `down` for as long as every row matches. Faces merge when their
            // texture layer matches, which already accounts for both block type
            // and which way the face points.
            for (int j = 0; j < Chunk::kSize; ++j) {
                for (int i = 0; i < Chunk::kSize;) {
                    const float layer = mask[static_cast<std::size_t>(j) * Chunk::kSize + i];
                    if (layer == kNoFace) {
                        ++i;
                        continue;
                    }

                    int width = 1;
                    while (i + width < Chunk::kSize &&
                           mask[static_cast<std::size_t>(j) * Chunk::kSize + i + width] == layer) {
                        ++width;
                    }

                    int height = 1;
                    while (j + height < Chunk::kSize) {
                        bool wholeRowMatches = true;
                        for (int k = 0; k < width; ++k) {
                            if (mask[static_cast<std::size_t>(j + height) * Chunk::kSize + i + k] != layer) {
                                wholeRowMatches = false;
                                break;
                            }
                        }
                        if (!wholeRowMatches) {
                            break;
                        }
                        ++height;
                    }

                    glm::ivec3 origin{0};
                    origin[normal] = slice;
                    origin[across] = i;
                    origin[down] = j;

                    // Extent of the merged quad per world axis. The normal axis
                    // stays 1, so scaling a unit corner by this keeps the
                    // winding the original tables established.
                    glm::vec3 extent{1.0f};
                    extent[across] = static_cast<float>(width);
                    extent[down] = static_cast<float>(height);

                    const glm::vec3 quadOrigin = originOffset + glm::vec3{origin};
                    const glm::vec2 uvScale{extent[face.uAxis], extent[face.vAxis]};
                    const glm::vec3 color{face.shade, face.shade, face.shade};
                    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

                    for (std::size_t corner = 0; corner < face.corners.size(); ++corner) {
                        const glm::vec3 p = quadOrigin + face.corners[corner] * extent;
                        const glm::vec2 uv = face.uvs[corner] * uvScale;
                        mesh.vertices.push_back(engine::Vertex{
                            {p.x, p.y, p.z}, {color.r, color.g, color.b, 1.0f}, {uv.x, uv.y}, layer});
                    }

                    mesh.indices.insert(mesh.indices.end(),
                                        {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});

                    for (int b = 0; b < height; ++b) {
                        for (int a = 0; a < width; ++a) {
                            mask[static_cast<std::size_t>(j + b) * Chunk::kSize + i + a] = kNoFace;
                        }
                    }

                    i += width;
                }
            }
        }
    }

    return mesh;
}

} // namespace game
