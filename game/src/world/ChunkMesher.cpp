#include "world/ChunkMesher.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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
    /// Fixed brightness by orientation, so surfaces stay readable even where the
    /// light is flat. Multiplied with the computed lighting rather than
    /// replacing it.
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

/// How dark a fully enclosed corner becomes. Ambient occlusion is a cheat, not a
/// simulation, so this is tuned by eye: too strong and the world looks grubby,
/// too weak and it looks flat.
constexpr std::array<float, 4> kOcclusionSteps{0.46f, 0.66f, 0.84f, 1.0f};

/// Converts a 0-15 light level into a brightness multiplier.
///
/// Deliberately curved rather than linear: the eye reads brightness roughly
/// logarithmically, and a linear ramp makes everything below about level 10 look
/// uniformly dark.
float lightCurve(float level) {
    const float normalised = std::clamp(level / static_cast<float>(kMaxLight), 0.0f, 1.0f);
    return std::pow(normalised, 1.4f);
}

/// Combines sky and block light. They are independent sources, so the brighter
/// wins rather than summing, which would blow out anywhere both reach.
///
/// The two are *not* combined here any more - the shader needs them apart,
/// because sunlight has to follow the sun's direction and the time of day while
/// a glowstone block must not.

/// Standard voxel ambient occlusion: how boxed-in a corner is, 0 (most
/// enclosed) to 3 (open). Two solid sides meeting means the corner is sealed
/// regardless of what sits diagonally behind them.
int occlusionAt(bool side1, bool side2, bool corner) {
    if (side1 && side2) {
        return 0;
    }
    return 3 - (static_cast<int>(side1) + static_cast<int>(side2) + static_cast<int>(corner));
}

/// Everything needed to draw one face, resolved before any merging happens.
struct FaceSample {
    float layer = kNoFace;
    /// Per corner, and deliberately kept apart: the shader applies the sun to
    /// sky light only, and `shading` has to survive where neither light reaches
    /// or unlit caves lose their ambient occlusion entirely.
    std::array<float, 4> sky{};
    std::array<float, 4> block{};
    std::array<float, 4> shading{};
    /// True when all four corners match, which is the only case where a face may
    /// be merged with its neighbours.
    bool flat = false;
    bool translucent = false;
    float alpha = 1.0f;
    /// How far the top of this block is lowered, for partly filled water.
    float surfaceDrop = 0.0f;
};

/// How much of a block each level of water gives up. Level 7 is a thin film.
constexpr float kWaterLevelDrop = 0.11f;

/// How much of the world shows through water.
constexpr float kWaterAlpha = 0.72f;

} // namespace

ChunkMeshes meshChunk(const ChunkVolume& volume, const glm::vec3& originOffset) {
    ChunkMeshes result;

    constexpr int size = Chunk::kSize;
    std::array<FaceSample, size * size> faces{};

    for (const Face& face : kFaces) {
        // The two axes the face spans. Which is called which does not matter;
        // texture scaling reads the face's own uAxis/vAxis rather than these.
        const int normal = face.axis;
        const int across = (normal + 1) % 3;
        const int down = (normal + 2) % 3;

        for (int slice = 0; slice < size; ++slice) {
            for (int j = 0; j < size; ++j) {
                for (int i = 0; i < size; ++i) {
                    glm::ivec3 p{0};
                    p[normal] = slice;
                    p[across] = i;
                    p[down] = j;

                    FaceSample& sample = faces[static_cast<std::size_t>(j) * size + i];
                    sample = FaceSample{};

                    const BlockId block = volume.blockAt(p.x, p.y, p.z);
                    if (block == BlockId::Air) {
                        continue;
                    }

                    // The whole optimisation: a face buried against something
                    // that hides it can never be seen, so it is never created.
                    // Water only shows where it meets air, so the faces between
                    // water and the seabed are skipped and you can see through.
                    const glm::ivec3 front = p + face.neighbourOffset;
                    const BlockId ahead = volume.blockAt(front.x, front.y, front.z);
                    // Water shows against air, and against thinner water, whose
                    // lower surface would otherwise leave a gap to see through.
                    const bool visible =
                        isTranslucent(block)
                            ? (ahead == BlockId::Air || (isWater(ahead) && waterLevel(ahead) > waterLevel(block)))
                            : !isOpaque(ahead);
                    if (!visible) {
                        continue;
                    }

                    sample.layer = blockTextureLayer(block, face.facing);
                    sample.translucent = isTranslucent(block);
                    sample.alpha = sample.translucent ? kWaterAlpha : 1.0f;
                    // Thinner flows sit lower, so a stream visibly tapers away
                    // from its source rather than running at full depth.
                    sample.surfaceDrop =
                        isWater(block) ? static_cast<float>(waterLevel(block)) * kWaterLevelDrop : 0.0f;

                    for (std::size_t c = 0; c < 4; ++c) {
                        // Each corner leans toward one end of both in-plane axes.
                        glm::ivec3 stepA{0};
                        glm::ivec3 stepB{0};
                        stepA[across] = face.corners[c][across] > 0.5f ? 1 : -1;
                        stepB[down] = face.corners[c][down] > 0.5f ? 1 : -1;

                        const glm::ivec3 a = front + stepA;
                        const glm::ivec3 b = front + stepB;
                        const glm::ivec3 diagonal = front + stepA + stepB;

                        const bool solidA = isOpaque(volume.blockAt(a.x, a.y, a.z));
                        const bool solidB = isOpaque(volume.blockAt(b.x, b.y, b.z));
                        const bool solidDiagonal = isOpaque(volume.blockAt(diagonal.x, diagonal.y, diagonal.z));

                        // Smooth lighting: average the open cells touching this
                        // corner. Solid ones are skipped rather than counted as
                        // dark, or every surface would be dimmed by its own
                        // neighbours.
                        float skySum = 0.0f;
                        float blockSum = 0.0f;
                        int samples = 0;
                        const auto accumulate = [&](const glm::ivec3& cell, bool solid) {
                            if (solid) {
                                return;
                            }
                            const std::uint8_t packed = volume.lightAt(cell.x, cell.y, cell.z);
                            skySum += static_cast<float>(packed >> 4);
                            blockSum += static_cast<float>(packed & 0x0F);
                            ++samples;
                        };
                        accumulate(front, false);
                        accumulate(a, solidA);
                        accumulate(b, solidB);
                        accumulate(diagonal, solidDiagonal || (solidA && solidB));

                        const float scale = 1.0f / static_cast<float>(std::max(1, samples));
                        const int occlusion = occlusionAt(solidA, solidB, solidDiagonal);

                        sample.sky[c] = lightCurve(skySum * scale);
                        sample.block[c] = lightCurve(blockSum * scale);
                        sample.shading[c] = face.shade * kOcclusionSteps[static_cast<std::size_t>(occlusion)];
                    }

                    sample.flat = true;
                    for (std::size_t c = 1; c < 4; ++c) {
                        if (sample.sky[c] != sample.sky[0] || sample.block[c] != sample.block[0] ||
                            sample.shading[c] != sample.shading[0]) {
                            sample.flat = false;
                            break;
                        }
                    }
                }
            }

            const auto emit = [&](int i, int j, int width, int height, const FaceSample& sample) {
                engine::MeshData& mesh = sample.translucent ? result.translucent : result.opaque;

                glm::ivec3 origin{0};
                origin[normal] = slice;
                origin[across] = i;
                origin[down] = j;

                // Extent of the merged quad per world axis. The normal axis stays
                // 1, so scaling a unit corner by this keeps the winding the
                // original tables established.
                glm::vec3 extent{1.0f};
                extent[across] = static_cast<float>(width);
                extent[down] = static_cast<float>(height);

                const glm::vec3 quadOrigin = originOffset + glm::vec3{origin};
                const glm::vec2 uvScale{extent[face.uAxis], extent[face.vAxis]};
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

                for (std::size_t c = 0; c < 4; ++c) {
                    glm::vec3 position = quadOrigin + face.corners[c] * extent;
                    // Only the top of the block moves; the sides follow it down
                    // so the column stays closed.
                    if (sample.surfaceDrop > 0.0f && face.corners[c].y > 0.5f) {
                        position.y -= sample.surfaceDrop;
                    }
                    const glm::vec2 uv = face.uvs[c] * uvScale;
                    // Red is sky light, green is block light, blue is face shade
                    // times ambient occlusion. The vertex format was already
                    // wide enough, because the shading was only ever greyscale.
                    mesh.vertices.push_back(
                        engine::Vertex{{position.x, position.y, position.z},
                                       {sample.sky[c], sample.block[c], sample.shading[c], sample.alpha},
                                       {uv.x, uv.y},
                                       sample.layer});
                }

                // Split along the darker diagonal. Quads with occlusion on
                // opposite corners otherwise show a visible seam running the
                // wrong way, which reads as a crease in flat ground.
                const float lit0 = sample.shading[0] * std::max(sample.sky[0], sample.block[0]);
                const float lit1 = sample.shading[1] * std::max(sample.sky[1], sample.block[1]);
                const float lit2 = sample.shading[2] * std::max(sample.sky[2], sample.block[2]);
                const float lit3 = sample.shading[3] * std::max(sample.sky[3], sample.block[3]);
                if (lit0 + lit2 > lit1 + lit3) {
                    mesh.indices.insert(mesh.indices.end(),
                                        {base + 1, base + 2, base + 3, base + 1, base + 3, base + 0});
                } else {
                    mesh.indices.insert(mesh.indices.end(),
                                        {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
                }
            };

            // Greedy merge, but only across faces that are evenly lit. A quad
            // spanning several blocks interpolates its corners across the whole
            // span, which only matches the individual faces it replaces when
            // every one of them was uniform to begin with.
            for (int j = 0; j < size; ++j) {
                for (int i = 0; i < size;) {
                    const FaceSample& sample = faces[static_cast<std::size_t>(j) * size + i];
                    if (sample.layer == kNoFace) {
                        ++i;
                        continue;
                    }
                    if (!sample.flat) {
                        emit(i, j, 1, 1, sample);
                        ++i;
                        continue;
                    }

                    const auto matches = [&](const FaceSample& other) {
                        return other.layer == sample.layer && other.flat && other.sky[0] == sample.sky[0] &&
                               other.block[0] == sample.block[0] && other.shading[0] == sample.shading[0] &&
                               other.translucent == sample.translucent && other.surfaceDrop == sample.surfaceDrop;
                    };

                    int width = 1;
                    while (i + width < size && matches(faces[static_cast<std::size_t>(j) * size + i + width])) {
                        ++width;
                    }

                    int height = 1;
                    while (j + height < size) {
                        bool wholeRowMatches = true;
                        for (int k = 0; k < width; ++k) {
                            if (!matches(faces[static_cast<std::size_t>(j + height) * size + i + k])) {
                                wholeRowMatches = false;
                                break;
                            }
                        }
                        if (!wholeRowMatches) {
                            break;
                        }
                        ++height;
                    }

                    emit(i, j, width, height, sample);

                    for (int b = 0; b < height; ++b) {
                        for (int a = 0; a < width; ++a) {
                            faces[static_cast<std::size_t>(j + b) * size + i + a].layer = kNoFace;
                        }
                    }

                    i += width;
                }
            }
        }
    }

    return result;
}

} // namespace game
