#include "item/SpriteModel.hpp"

#include <cstdint>

namespace game {

void appendSpriteModel(engine::MeshData& mesh, const SpriteMask& sprites, int spriteLayer,
                       const glm::vec3& centre, const glm::vec3& right, const glm::vec3& up,
                       const glm::vec3& forward, float sky, float blockLight) {
    if (spriteLayer < 0) {
        return;
    }

    const auto quad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d,
                          float u0, float v0, float u1, float v1, float shade) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const glm::vec3 corners[4]{a, b, c, d};
        const glm::vec2 uvs[4]{{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back(engine::Vertex{{corners[i].x, corners[i].y, corners[i].z},
                                                   engine::packVertexColor(sky, blockLight, shade, 1.0f),
                                                   {uvs[i].x, uvs[i].y},
                                                   static_cast<float>(spriteLayer),
                                                   engine::kVertexSurfaceDefault});
        }
        // Both windings: the model turns, so either side can face the camera.
        mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                                 base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
    };

    const int cols = sprites.width();
    const int rows = sprites.height();
    // A texel of the sprite's own grid, so thickness follows the art rather
    // than a constant that has to be kept in step with it.
    const glm::vec3 depth = forward / static_cast<float>(cols > 0 ? cols : 16);

    quad(centre - right - up + depth, centre + right - up + depth, centre + right + up + depth,
         centre - right + up + depth, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    quad(centre - right - up - depth, centre + right - up - depth, centre + right + up - depth,
         centre - right + up - depth, 0.0f, 0.0f, 1.0f, 1.0f, 0.82f);

    if (cols <= 0 || rows <= 0) {
        return;
    }

    // A wall samples the one texel it belongs to, at its centre, so it carries
    // that texel's own colour. Every corner takes the same coordinate, which
    // also pins it to the sharpest mip level - a one-texel strip averaged down
    // would be discarded at distance and the object would go hollow as you
    // walked away.
    const auto wall = [&](const glm::vec3 (&corners)[4], float u, float v, float shade) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back(engine::Vertex{{corners[i].x, corners[i].y, corners[i].z},
                                                   engine::packVertexColor(sky, blockLight, shade, 1.0f),
                                                   {u, v},
                                                   static_cast<float>(spriteLayer),
                                                   engine::kVertexSurfaceDefault});
        }
        mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                                 base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
    };

    // Sprite space to world space. `v` runs down the image, so the top row is
    // at +up - the same convention the flat faces above use.
    const auto at = [&](float u, float v, float side) {
        return centre + right * (u * 2.0f - 1.0f) + up * (1.0f - v * 2.0f) + depth * side;
    };

    const float uStep = 1.0f / static_cast<float>(cols);
    const float vStep = 1.0f / static_cast<float>(rows);
    for (int ty = 0; ty < rows; ++ty) {
        for (int tx = 0; tx < cols; ++tx) {
            if (!sprites.solid(spriteLayer, tx, ty)) {
                continue;
            }
            const float u0 = static_cast<float>(tx) * uStep;
            const float u1 = u0 + uStep;
            const float v0 = static_cast<float>(ty) * vStep;
            const float v1 = v0 + vStep;
            const float uc = u0 + uStep * 0.5f;
            const float vc = v0 + vStep * 0.5f;

            if (!sprites.solid(spriteLayer, tx - 1, ty)) {
                const glm::vec3 face[4]{at(u0, v0, 1.0f), at(u0, v1, 1.0f), at(u0, v1, -1.0f),
                                        at(u0, v0, -1.0f)};
                wall(face, uc, vc, 0.72f);
            }
            if (!sprites.solid(spriteLayer, tx + 1, ty)) {
                const glm::vec3 face[4]{at(u1, v0, 1.0f), at(u1, v1, 1.0f), at(u1, v1, -1.0f),
                                        at(u1, v0, -1.0f)};
                wall(face, uc, vc, 0.72f);
            }
            if (!sprites.solid(spriteLayer, tx, ty - 1)) {
                const glm::vec3 face[4]{at(u0, v0, 1.0f), at(u1, v0, 1.0f), at(u1, v0, -1.0f),
                                        at(u0, v0, -1.0f)};
                wall(face, uc, vc, 0.94f);
            }
            if (!sprites.solid(spriteLayer, tx, ty + 1)) {
                const glm::vec3 face[4]{at(u0, v1, 1.0f), at(u1, v1, 1.0f), at(u1, v1, -1.0f),
                                        at(u0, v1, -1.0f)};
                wall(face, uc, vc, 0.62f);
            }
        }
    }
}

} // namespace game
