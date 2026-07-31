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
    /// Emitted with both windings, so looking through the holes in the near side
    /// of a leaf block shows the inside of its far side rather than straight
    /// through the block.
    bool doubleSided = false;
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
                    // Anything that is not a unit cube is emitted by the shape
                    // pass below. Teaching the greedy mask about partial faces
                    // would slow down the path that carries the whole world for
                    // the sake of a few decorative blocks.
                    const BlockShape shape = blockShape(block);
                    if (shape == BlockShape::Cross || shape == BlockShape::Slab || shape == BlockShape::Stairs ||
                        shape == BlockShape::Fence) {
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
                    //
                    // Cutout blocks deliberately keep the faces they share with
                    // their own kind. Culling those is the cheaper "fast" style
                    // of foliage, and it makes a canopy a hollow shell: every
                    // hole in the near face shows the sky instead of more leaves
                    // behind it.
                    //
                    // Both neighbours would otherwise emit that shared boundary,
                    // leaving two coplanar quads fighting over the same depth,
                    // so only the positive-facing side of the pair emits it. It
                    // is double-sided, so the other neighbour is still covered.
                    const bool sharedWithOwnKind = isCutout(block) && ahead == block;
                    const bool positiveFacing =
                        face.neighbourOffset.x + face.neighbourOffset.y + face.neighbourOffset.z > 0;
                    const bool visible =
                        isTranslucent(block)
                            ? (ahead == BlockId::Air || (isWater(ahead) && waterLevel(ahead) > waterLevel(block)))
                            : (!occludesFace(ahead, face.neighbourOffset.y) &&
                               (positiveFacing || !sharedWithOwnKind));
                    if (!visible) {
                        continue;
                    }

                    sample.layer = blockTextureLayer(block, face.facing);
                    sample.translucent = isTranslucent(block);
                    sample.doubleSided = isCutout(block);
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
                    if (sample.doubleSided) {
                        mesh.indices.insert(mesh.indices.end(),
                                            {base + 3, base + 2, base + 1, base + 0, base + 3, base + 1});
                    }
                } else {
                    mesh.indices.insert(mesh.indices.end(),
                                        {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
                    if (sample.doubleSided) {
                        mesh.indices.insert(mesh.indices.end(),
                                            {base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
                    }
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
                               other.translucent == sample.translucent && other.doubleSided == sample.doubleSided &&
                               other.surfaceDrop == sample.surfaceDrop;
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

    // Shapes that are not unit cubes, one block at a time and never merged.
    const auto lightOf = [&](const glm::ivec3& cell, float& sky, float& blockLight) {
        const std::uint8_t packed = volume.lightAt(cell.x, cell.y, cell.z);
        sky = lightCurve(static_cast<float>(packed >> 4));
        blockLight = lightCurve(static_cast<float>(packed & 0x0F));
    };

    const auto pushQuad = [&](const std::array<glm::vec3, 4>& corners, const std::array<glm::vec2, 4>& uvs,
                              float layer, float sky, float blockLight, float shading, bool doubleSided) {
        engine::MeshData& mesh = result.opaque;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (std::size_t c = 0; c < 4; ++c) {
            mesh.vertices.push_back(engine::Vertex{{corners[c].x, corners[c].y, corners[c].z},
                                                   {sky, blockLight, shading, 1.0f},
                                                   {uvs[c].x, uvs[c].y},
                                                   layer});
        }
        mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
        if (doubleSided) {
            mesh.indices.insert(mesh.indices.end(), {base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
        }
    };

    for (int z = 0; z < size; ++z) {
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const BlockId block = volume.blockAt(x, y, z);
                const BlockShape shape = blockShape(block);
                if (shape != BlockShape::Cross && shape != BlockShape::Slab && shape != BlockShape::Stairs &&
                    shape != BlockShape::Fence) {
                    continue;
                }

                const glm::vec3 cellOrigin = originOffset + glm::vec3{x, y, z};
                float sky = 0.0f;
                float blockLight = 0.0f;

                if (shape == BlockShape::Cross) {
                    // A plant sits in an otherwise empty cell, so it is lit by
                    // its own cell rather than by a neighbour.
                    lightOf({x, y, z}, sky, blockLight);

                    // Reference plants span 0.8 to 15.2 in sixteenths, so the
                    // blade very nearly reaches the cell corners. Holding it
                    // further in made plants read a size too small.
                    constexpr float lo = 0.05f;
                    constexpr float hi = 1.0f - lo;
                    const float layer = blockTextureLayer(block, BlockFace::Side);

                    const std::array<std::array<glm::vec3, 4>, 2> blades{
                        std::array<glm::vec3, 4>{glm::vec3{lo, 0, lo}, glm::vec3{hi, 0, hi}, glm::vec3{hi, 1, hi},
                                                 glm::vec3{lo, 1, lo}},
                        std::array<glm::vec3, 4>{glm::vec3{hi, 0, lo}, glm::vec3{lo, 0, hi}, glm::vec3{lo, 1, hi},
                                                 glm::vec3{hi, 1, lo}}};

                    for (const std::array<glm::vec3, 4>& blade : blades) {
                        std::array<glm::vec3, 4> corners{};
                        for (std::size_t c = 0; c < 4; ++c) {
                            corners[c] = cellOrigin + blade[c];
                        }
                        pushQuad(corners, kBottomLeftWinding, layer, sky, blockLight, 1.0f, true);
                    }
                    continue;
                }

                // Slabs and stairs are both just boxes, and they read the very
                // same table collision does, so what you see and what you bump
                // into cannot disagree.
                //
                // A fence is the exception: its arms follow its neighbours,
                // which the id alone cannot express, so the drawn shape is
                // computed here while collision assumes every arm.
                BlockBoxes shapeBoxes = collisionBoxes(block);
                if (shape == BlockShape::Fence) {
                    const auto reaches = [&](int dx, int dz) {
                        const BlockId neighbour = volume.blockAt(x + dx, y, z + dz);
                        return isOpaque(neighbour) || blockShape(neighbour) == BlockShape::Fence;
                    };
                    std::uint8_t connections = 0;
                    if (reaches(0, -1)) {
                        connections |= ConnectNorth;
                    }
                    if (reaches(0, 1)) {
                        connections |= ConnectSouth;
                    }
                    if (reaches(-1, 0)) {
                        connections |= ConnectWest;
                    }
                    if (reaches(1, 0)) {
                        connections |= ConnectEast;
                    }
                    shapeBoxes = fenceRailBoxes(connections);
                }

                for (int i = 0; i < shapeBoxes.count; ++i) {
                    const BlockBox& b = shapeBoxes.boxes[i];
                    const glm::vec3 lo{b.minX, b.minY, b.minZ};
                    const glm::vec3 hi{b.maxX, b.maxY, b.maxZ};

                    for (const Face& face : kFaces) {
                        const int axis = face.axis;
                        const bool positive = face.neighbourOffset[axis] > 0;

                        // A face flush with the cell wall can be buried by a
                        // neighbour; one floating inside the cell never can.
                        if (positive ? hi[axis] >= 1.0f : lo[axis] <= 0.0f) {
                            const glm::ivec3 ahead{x + face.neighbourOffset.x, y + face.neighbourOffset.y,
                                                   z + face.neighbourOffset.z};
                            if (occludesFace(volume.blockAt(ahead.x, ahead.y, ahead.z), face.neighbourOffset.y)) {
                                continue;
                            }
                            lightOf(ahead, sky, blockLight);
                        } else {
                            lightOf({x, y, z}, sky, blockLight);
                        }

                        // Faces buried inside another box of the same block are
                        // never seen, and emitting them leaves two coplanar
                        // quads fighting over one depth - a stair's step sits
                        // directly on its own lower half.
                        glm::vec3 probe{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
                        probe[axis] = positive ? hi[axis] : lo[axis];
                        probe += glm::vec3{face.neighbourOffset} * 0.01f;

                        bool buried = false;
                        for (int j = 0; j < shapeBoxes.count && !buried; ++j) {
                            if (j == i) {
                                continue;
                            }
                            const BlockBox& o = shapeBoxes.boxes[j];
                            buried = probe.x > o.minX && probe.x < o.maxX && probe.y > o.minY && probe.y < o.maxY &&
                                     probe.z > o.minZ && probe.z < o.maxZ;
                        }
                        if (buried) {
                            continue;
                        }

                        // V runs the same way as the corner on some faces and
                        // the opposite way on others, so it is read off the
                        // tables rather than assumed.
                        const int uAxis = face.uAxis;
                        const int vAxis = face.vAxis;
                        const bool flipV = std::abs(face.uvs[0].y - face.corners[0][vAxis]) > 0.5f;

                        std::array<glm::vec3, 4> corners{};
                        std::array<glm::vec2, 4> uvs{};
                        for (std::size_t c = 0; c < 4; ++c) {
                            // Mixing the shared unit-cube corners keeps the
                            // winding the face tables established.
                            const glm::vec3 local = lo + face.corners[c] * (hi - lo);
                            corners[c] = cellOrigin + local;
                            uvs[c] = {local[uAxis], flipV ? 1.0f - local[vAxis] : local[vAxis]};
                        }
                        pushQuad(corners, uvs, blockTextureLayer(block, face.facing), sky, blockLight, face.shade,
                                 false);
                    }
                }
            }
        }
    }

    return result;
}

} // namespace game
