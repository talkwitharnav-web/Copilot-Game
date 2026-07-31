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
};

constexpr std::array<glm::vec2, 4> kBottomLeftWinding{glm::vec2{0, 1}, glm::vec2{1, 1}, glm::vec2{1, 0},
                                                      glm::vec2{0, 0}};

constexpr std::array<Face, 6> kFaces{{
    // +X
    {{1, 0, 0},
     {glm::vec3{1, 0, 1}, glm::vec3{1, 0, 0}, glm::vec3{1, 1, 0}, glm::vec3{1, 1, 1}},
     kBottomLeftWinding,
     BlockFace::Side,
     0.72f},
    // -X
    {{-1, 0, 0},
     {glm::vec3{0, 0, 0}, glm::vec3{0, 0, 1}, glm::vec3{0, 1, 1}, glm::vec3{0, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Side,
     0.72f},
    // +Y
    {{0, 1, 0},
     {glm::vec3{0, 1, 1}, glm::vec3{1, 1, 1}, glm::vec3{1, 1, 0}, glm::vec3{0, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Top,
     1.00f},
    // -Y
    {{0, -1, 0},
     {glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{1, 0, 1}, glm::vec3{0, 0, 1}},
     kBottomLeftWinding,
     BlockFace::Bottom,
     0.45f},
    // +Z
    {{0, 0, 1},
     {glm::vec3{0, 0, 1}, glm::vec3{1, 0, 1}, glm::vec3{1, 1, 1}, glm::vec3{0, 1, 1}},
     kBottomLeftWinding,
     BlockFace::Side,
     0.86f},
    // -Z
    {{0, 0, -1},
     {glm::vec3{1, 0, 0}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0}, glm::vec3{1, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Side,
     0.60f},
}};

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

    for (int y = 0; y < Chunk::kSize; ++y) {
        for (int z = 0; z < Chunk::kSize; ++z) {
            for (int x = 0; x < Chunk::kSize; ++x) {
                const BlockId block = chunk.at(x, y, z);
                if (!isSolid(block)) {
                    continue;
                }

                const glm::vec3 blockOrigin = originOffset + glm::vec3{x, y, z};

                for (const Face& face : kFaces) {
                    // The whole optimisation: a face buried against another
                    // solid block can never be seen, so it is never created.
                    if (isSolid(blockAt(x + face.neighbourOffset.x, y + face.neighbourOffset.y,
                                        z + face.neighbourOffset.z))) {
                        continue;
                    }

                    // Shade only: the texture supplies the colour, so this is a
                    // grey multiplier that keeps face orientation readable.
                    const glm::vec3 color{face.shade, face.shade, face.shade};
                    const float layer = blockTextureLayer(block, face.facing);
                    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

                    for (std::size_t corner = 0; corner < face.corners.size(); ++corner) {
                        const glm::vec3 p = blockOrigin + face.corners[corner];
                        const glm::vec2 uv = face.uvs[corner];
                        mesh.vertices.push_back(engine::Vertex{
                            {p.x, p.y, p.z}, {color.r, color.g, color.b, 1.0f}, {uv.x, uv.y}, layer});
                    }

                    mesh.indices.insert(mesh.indices.end(),
                                        {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
                }
            }
        }
    }

    return mesh;
}

} // namespace game
