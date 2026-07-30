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
    /// Fixed brightness standing in for lighting, so surface orientation is
    /// readable. Real lighting arrives at M14.
    float shade;
};

constexpr std::array<Face, 6> kFaces{{
    // +X
    {{1, 0, 0}, {glm::vec3{1, 0, 1}, glm::vec3{1, 0, 0}, glm::vec3{1, 1, 0}, glm::vec3{1, 1, 1}}, 0.72f},
    // -X
    {{-1, 0, 0}, {glm::vec3{0, 0, 0}, glm::vec3{0, 0, 1}, glm::vec3{0, 1, 1}, glm::vec3{0, 1, 0}}, 0.72f},
    // +Y
    {{0, 1, 0}, {glm::vec3{0, 1, 1}, glm::vec3{1, 1, 1}, glm::vec3{1, 1, 0}, glm::vec3{0, 1, 0}}, 1.00f},
    // -Y
    {{0, -1, 0}, {glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{1, 0, 1}, glm::vec3{0, 0, 1}}, 0.45f},
    // +Z
    {{0, 0, 1}, {glm::vec3{0, 0, 1}, glm::vec3{1, 0, 1}, glm::vec3{1, 1, 1}, glm::vec3{0, 1, 1}}, 0.86f},
    // -Z
    {{0, 0, -1}, {glm::vec3{1, 0, 0}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0}, glm::vec3{1, 1, 0}}, 0.60f},
}};

} // namespace

engine::MeshData meshChunk(const Chunk& chunk, const glm::vec3& originOffset) {
    engine::MeshData mesh;

    for (int y = 0; y < Chunk::kSize; ++y) {
        for (int z = 0; z < Chunk::kSize; ++z) {
            for (int x = 0; x < Chunk::kSize; ++x) {
                const BlockId block = chunk.at(x, y, z);
                if (!isSolid(block)) {
                    continue;
                }

                const glm::vec3 blockOrigin = originOffset + glm::vec3{x, y, z};
                const glm::vec3 baseColor = blockColor(block);

                for (const Face& face : kFaces) {
                    // The whole optimisation: a face buried against another
                    // solid block can never be seen, so it is never created.
                    if (isSolid(chunk.at(x + face.neighbourOffset.x, y + face.neighbourOffset.y,
                                         z + face.neighbourOffset.z))) {
                        continue;
                    }

                    const glm::vec3 color = baseColor * face.shade;
                    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

                    for (const glm::vec3& corner : face.corners) {
                        const glm::vec3 p = blockOrigin + corner;
                        mesh.vertices.push_back(engine::Vertex{{p.x, p.y, p.z}, {color.r, color.g, color.b}});
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
