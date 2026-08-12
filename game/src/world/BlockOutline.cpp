#include "world/BlockOutline.hpp"

#include "world/Block.hpp"
#include "world/Collision.hpp"
#include "world/Noise.hpp"

#include <glm/glm.hpp>

#include <array>
#include <algorithm>
#include <cstdint>

namespace game {
namespace {

/// Pushed just outside the block so the bars do not fight the block's own
/// surface for the same depth values, which flickers.
constexpr float kInflate = 0.004f;
constexpr float kThickness = 0.022f;
constexpr glm::vec3 kColor{0.05f, 0.05f, 0.06f};

/// How dark a crack is allowed to get, as a multiplier on the picture's own
/// grey. **One is the art untouched and near zero is black**, so the range runs
/// from the grey Mojang drew down to almost nothing.
constexpr float kCrackShadeMin = 0.15f;
constexpr float kCrackShadeMax = 1.0f;
/// Cycles of the field per block. **Deliberately below one**, because a quad is
/// sampled at its four corners and nothing finer than that survives: a higher
/// frequency would alias into per-corner speckle, which is the opposite of what
/// was asked for. At this scale the darkness drifts along a crack and from one
/// block to the next rather than flickering.
constexpr float kCrackNoiseScale = 0.5f;
constexpr std::uint32_t kCrackNoiseSeed = 0x63726B73u;
/// Widens the field about its middle before it is used. **Measured, not
/// guessed**: raw value noise is a bell, so over 72000 samples 42% of it landed
/// in the middle fifth of the range and almost every crack came out the same
/// mid-grey. At 1.5 the ends carry 8% each against 15% in the middle, which is
/// a spread you can see; at 2.5 nearly two cracks in five are pinned to an
/// extreme, which reads as two colours rather than a range.
constexpr float kCrackContrast = 1.5f;

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
            mesh.vertices.push_back(engine::Vertex{{p.x, p.y, p.z},
                                                   engine::packVertexColor(kColor.r, kColor.g, kColor.b, 1.0f),
                                                   {0.5f, 0.5f},
                                                   static_cast<float>(TextureLayer::White),
                                                   engine::kVertexSurfaceDefault});
        }

        mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
}

} // namespace

engine::MeshData makeBlockOutline(const glm::vec3& size) {
    engine::MeshData mesh;

    const glm::vec3 lo{-kInflate, -kInflate, -kInflate};
    const glm::vec3 hi{size.x + kInflate, size.y + kInflate, size.z + kInflate};
    const float t = kThickness;

    // Four bars along each axis, one per edge of the box.
    for (int axis = 0; axis < 3; ++axis) {
        const int a = (axis + 1) % 3;
        const int b = (axis + 2) % 3;

        for (int corner = 0; corner < 4; ++corner) {
            glm::vec3 min = lo;
            glm::vec3 max = hi;

            const bool farA = (corner & 1) != 0;
            const bool farB = (corner & 2) != 0;

            min[a] = farA ? hi[a] - t : lo[a];
            max[a] = farA ? hi[a] : lo[a] + t;
            min[b] = farB ? hi[b] - t : lo[b];
            max[b] = farB ? hi[b] : lo[b] + t;

            appendBox(mesh, min, max);
        }
    }

    return mesh;
}

engine::MeshData makeBlockCracks(const World& world, const glm::ivec3& cell, float progress) {
    engine::MeshData mesh;

    const BlockId id = world.blockAt(cell.x, cell.y, cell.z);
    if (id == BlockId::Air) {
        return mesh;
    }

    const auto layer = static_cast<float>(destroyStageLayer(progress));
    const glm::vec3 origin{cell};

    // **Grey to black, and smoothly.** The two light channels are held at full
    // so a crack reads the same in a cave as in daylight; the third is face
    // shade, which `diffuseLevel` multiplies straight into the result, so it is
    // the one honest per-vertex darkness knob the lit path offers. Sampling one
    // smooth field at each corner's own **world** position is what makes it
    // drift along a crack instead of jumping: two quads meeting on an edge take
    // the same sample there, so the darkness carries across the join.
    //
    // **One octave, not several.** Octaves sum toward their mean - four of them
    // land near N(0.47, 0.13) - so an fbm here would spend almost all its time
    // in the middle of the range and the cracks would come out one shade of
    // grey however wide the ends were set. This project has been caught by that
    // distribution four times; a single octave is far wider and just as smooth.
    const auto shadeAt = [](const glm::vec3& p) {
        float n = noise::value3D(kCrackNoiseSeed, p.x * kCrackNoiseScale, p.y * kCrackNoiseScale,
                                 p.z * kCrackNoiseScale);
        n = std::clamp((n - 0.5f) * kCrackContrast + 0.5f, 0.0f, 1.0f);
        return kCrackShadeMin + (kCrackShadeMax - kCrackShadeMin) * n;
    };
    const auto quad = [&](const std::array<glm::vec3, 4>& corners,
                          const std::array<glm::vec2, 4>& uvs) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back(
                engine::Vertex{{corners[i].x, corners[i].y, corners[i].z},
                               engine::packVertexColor(1.0f, 1.0f, shadeAt(corners[i]), 1.0f),
                               {uvs[i].x, uvs[i].y},
                               layer,
                               engine::kVertexSurfaceDefault});
        }
        // Both windings: a crack sits on a face that may be seen from either
        // side - a pane, a ladder, a plant - and one winding would leave half
        // of them uncracked.
        mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2,
                                                 base + 3, base + 2, base + 1, base + 0, base + 3,
                                                 base + 2, base + 0});
    };

    // Stretched over each face whole, which is what the reference does: the
    // crack picture is not tied to the block's own texture coordinates, so a
    // half-height slab shows the top half of nothing and the whole crack.
    constexpr std::array<glm::vec2, 4> kFaceUvs{
        glm::vec2{0.0f, 1.0f}, glm::vec2{1.0f, 1.0f}, glm::vec2{1.0f, 0.0f}, glm::vec2{0.0f, 0.0f}};

    // A plant is two crossed blades rather than a box, and it is the one family
    // no box function can describe - so the blades are reproduced here from the
    // mesher's own numbers rather than a cage being drawn round the flower.
    if (blockShape(id) == BlockShape::Cross) {
        constexpr float lo = 0.05f;
        constexpr float hi = 0.95f;
        for (int blade = 0; blade < 2; ++blade) {
            const float x0 = blade == 0 ? lo : hi;
            const float x1 = blade == 0 ? hi : lo;
            quad({origin + glm::vec3{x0, 0.0f, lo}, origin + glm::vec3{x1, 0.0f, hi},
                  origin + glm::vec3{x1, 1.0f, hi}, origin + glm::vec3{x0, 1.0f, lo}},
                 kFaceUvs);
        }
        return mesh;
    }

    // Grown a shade so the crack sits just outside the surface it belongs to
    // rather than fighting it for the same depth. Same trick and the same order
    // of magnitude as the selection cage above.
    constexpr float kCrackGrow = 0.002f;
    const BlockBoxes boxes = worldDrawnBoxes(world, cell.x, cell.y, cell.z);
    for (int i = 0; i < boxes.count; ++i) {
        const BlockBox& b = boxes.boxes[i];
        const glm::vec3 low = origin + glm::vec3{b.minX - kCrackGrow, b.minY - kCrackGrow,
                                                 b.minZ - kCrackGrow};
        const glm::vec3 high = origin + glm::vec3{b.maxX + kCrackGrow, b.maxY + kCrackGrow,
                                                  b.maxZ + kCrackGrow};
        for (const Face& face : kFaces) {
            std::array<glm::vec3, 4> corners{};
            for (int c = 0; c < 4; ++c) {
                corners[c] = glm::mix(low, high, face.corners[c]);
            }
            quad(corners, kFaceUvs);
        }
    }

    return mesh;
}

} // namespace game
