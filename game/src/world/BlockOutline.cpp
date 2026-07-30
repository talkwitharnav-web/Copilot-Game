#include "world/BlockOutline.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

namespace game {
namespace {

/// Pushed just outside the block so the bars do not fight the block's own
/// surface for the same depth values, which flickers.
constexpr float kInflate = 0.004f;
constexpr float kThickness = 0.022f;
constexpr glm::vec3 kColor{0.05f, 0.05f, 0.06f};

struct Face {
    /// Counter-clockwise seen from outside, matching the chunk mesher.
    std::array<glm::vec3, 4> corners;
};

constexpr std::array<Face, 6> kFaces{{
    {{glm::vec3{1, 0, 1}, glm::vec3{1, 0, 0}, glm::vec3{1, 1, 0}, glm::vec3{1, 1, 1}}},
    {{glm::vec3{0, 0, 0}, glm::vec3{0, 0, 1}, glm::vec3{0, 1, 1}, glm::vec3{0, 1, 0}}},
    {{glm::vec3{0, 1, 1}, glm::vec3{1, 1, 1}, glm::vec3{1, 1, 0}, glm::vec3{0, 1, 0}}},
    {{glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{1, 0, 1}, glm::vec3{0, 0, 1}}},
    {{glm::vec3{0, 0, 1}, glm::vec3{1, 0, 1}, glm::vec3{1, 1, 1}, glm::vec3{0, 1, 1}}},
    {{glm::vec3{1, 0, 0}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0}, glm::vec3{1, 1, 0}}},
}};

void appendBox(engine::MeshData& mesh, const glm::vec3& lo, const glm::vec3& hi) {
    for (const Face& face : kFaces) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

        for (const glm::vec3& corner : face.corners) {
            const glm::vec3 p = glm::mix(lo, hi, corner);
            // Layer 0 with a near-black tint: the cage reads as a solid dark
            // frame whatever texture happens to sit on layer 0.
            mesh.vertices.push_back(
                engine::Vertex{{p.x, p.y, p.z}, {kColor.r, kColor.g, kColor.b}, {0.5f, 0.5f}, 0.0f});
        }

        mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
}

} // namespace

engine::MeshData makeBlockOutline() {
    engine::MeshData mesh;

    const float lo = -kInflate;
    const float hi = 1.0f + kInflate;
    const float t = kThickness;

    // Four bars along each axis, one per edge of the cube.
    for (int axis = 0; axis < 3; ++axis) {
        const int a = (axis + 1) % 3;
        const int b = (axis + 2) % 3;

        for (int corner = 0; corner < 4; ++corner) {
            glm::vec3 min{lo};
            glm::vec3 max{hi};

            const bool farA = (corner & 1) != 0;
            const bool farB = (corner & 2) != 0;

            min[a] = farA ? hi - t : lo;
            max[a] = farA ? hi : lo + t;
            min[b] = farB ? hi - t : lo;
            max[b] = farB ? hi : lo + t;

            appendBox(mesh, min, max);
        }
    }

    return mesh;
}

} // namespace game
