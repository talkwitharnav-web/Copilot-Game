#include "world/ChunkMesher.hpp"

#include "world/FaceShading.hpp"

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
    /// replacing it. **Read from `FaceShading.hpp`, which owns it** - the value
    /// used to be written out here and in three other files, and one of those
    /// copies had drifted.
    float shade;
    /// The three-bit code a vertex on this face carries, so the shader knows
    /// which way the surface points without recovering it from derivatives.
    std::uint32_t normalCode;
    /// Which world axis the face's normal points along.
    int axis;
    /// World axes that U and V run along. Merged quads scale their texture
    /// coordinates by the extent along these, so the sampler tiles the texture
    /// instead of stretching it. Read off the corner/uv tables rather than
    /// derived, because getting them wrong is silent.
    int uAxis;
    int vAxis;
    /// Which way this face points, for blocks whose sides differ. The table
    /// already names all six, so it costs a column rather than a derivation.
    FaceDirection direction;
};

constexpr std::array<glm::vec2, 4> kBottomLeftWinding{glm::vec2{0, 1}, glm::vec2{1, 1}, glm::vec2{1, 0},
                                                      glm::vec2{0, 0}};

constexpr std::array<Face, 6> kFaces{{
    // +X
    {{1, 0, 0},
     {glm::vec3{1, 0, 1}, glm::vec3{1, 0, 0}, glm::vec3{1, 1, 0}, glm::vec3{1, 1, 1}},
     kBottomLeftWinding,
     BlockFace::Side,
     faceShade(AxisFace::PosX),
     faceNormalCode(AxisFace::PosX),
     0,
     2,
     1,
     FaceDirection::PosX},
    // -X
    {{-1, 0, 0},
     {glm::vec3{0, 0, 0}, glm::vec3{0, 0, 1}, glm::vec3{0, 1, 1}, glm::vec3{0, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Side,
     faceShade(AxisFace::NegX),
     faceNormalCode(AxisFace::NegX),
     0,
     2,
     1,
     FaceDirection::NegX},
    // +Y
    {{0, 1, 0},
     {glm::vec3{0, 1, 1}, glm::vec3{1, 1, 1}, glm::vec3{1, 1, 0}, glm::vec3{0, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Top,
     faceShade(AxisFace::PosY),
     faceNormalCode(AxisFace::PosY),
     1,
     0,
     2,
     FaceDirection::Unknown},
    // -Y
    {{0, -1, 0},
     {glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{1, 0, 1}, glm::vec3{0, 0, 1}},
     kBottomLeftWinding,
     BlockFace::Bottom,
     faceShade(AxisFace::NegY),
     faceNormalCode(AxisFace::NegY),
     1,
     0,
     2,
     FaceDirection::Unknown},
    // +Z
    {{0, 0, 1},
     {glm::vec3{0, 0, 1}, glm::vec3{1, 0, 1}, glm::vec3{1, 1, 1}, glm::vec3{0, 1, 1}},
     kBottomLeftWinding,
     BlockFace::Side,
     faceShade(AxisFace::PosZ),
     faceNormalCode(AxisFace::PosZ),
     2,
     0,
     1,
     FaceDirection::PosZ},
    // -Z
    {{0, 0, -1},
     {glm::vec3{1, 0, 0}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0}, glm::vec3{1, 1, 0}},
     kBottomLeftWinding,
     BlockFace::Side,
     faceShade(AxisFace::NegZ),
     faceNormalCode(AxisFace::NegZ),
     2,
     0,
     1,
     FaceDirection::NegZ},
}};

/// Empty slot in the merge mask. Texture layers are never negative.
constexpr float kNoFace = -1.0f;

/// Whether the U and V flips the shape pass derives reproduce the face tables
/// exactly, on a box that fills its cell.
///
/// **This is the check that was missing.** `flipV` was derived and `flipU` was
/// not, so every +X and -Z face of every shaped block was mirrored - invisible
/// until a texture had a letter on it, which is why lit TNT read backwards
/// while ordinary TNT, which goes through the merged pass, did not.
constexpr bool shapeUvsMatchFaceTables() {
    for (const Face& face : kFaces) {
        const float u0 = face.corners[0][face.uAxis];
        const float v0 = face.corners[0][face.vAxis];
        const bool flipU = face.uvs[0].x - u0 > 0.5f || u0 - face.uvs[0].x > 0.5f;
        const bool flipV = face.uvs[0].y - v0 > 0.5f || v0 - face.uvs[0].y > 0.5f;
        for (int c = 0; c < 4; ++c) {
            const float u = face.corners[c][face.uAxis];
            const float v = face.corners[c][face.vAxis];
            if ((flipU ? 1.0f - u : u) != face.uvs[c].x || (flipV ? 1.0f - v : v) != face.uvs[c].y) {
                return false;
            }
        }
    }
    return true;
}
static_assert(shapeUvsMatchFaceTables(),
              "a shaped block's faces must sample exactly where the merged ones do");

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
    /// **Ambient occlusion alone, no longer multiplied by the face shade.**
    /// A deferred pass has to be able to tell the two apart - one is a property
    /// of the corner, the other of which way the surface points - and once they
    /// are fused into a single float they cannot be recovered.
    std::array<float, 4> occlusion{};
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
    /// Marks the quad's top edge as a water surface, which the vertex stage is
    /// allowed to move with the waves. Zero surface drop cannot stand in for it:
    /// a source block has none and is exactly the case that must move.
    ///
    /// **Water only, not every fluid.** Lava is never displaced, so flagging it
    /// would cost it its greedy merge for nothing.
    bool wavy = false;
    /// Bends with the wind.
    bool sways = false;
};

/// How much of a block each level of water gives up. Level 7 is a thin film.
constexpr float kWaterLevelDrop = 0.11f;

/// How much of the world shows through water.
constexpr float kWaterAlpha = 0.72f;

} // namespace

ChunkMeshes meshChunk(const ChunkVolume& volume, const glm::vec3& originOffset, MeshDetail detail) {
    ChunkMeshes result;

    const bool terrainOnly = detail == MeshDetail::TerrainOnly;

    constexpr int size = Chunk::kSize;
    std::array<FaceSample, size * size> faces{};

    const auto waterAt = [&volume](int x, int y, int z) {
        return volume.waterloggedAt(x, y, z) || isWater(volume.blockAt(x, y, z));
    };
    // True when every one of the four columns meeting this corner holds water at
    // this level - and therefore when the wave may move it without pulling the
    // surface away from something solid.
    const auto surfaceIsOpen = [&waterAt](int y, int cornerX, int cornerZ) {
        return waterAt(cornerX - 1, y, cornerZ - 1) && waterAt(cornerX, y, cornerZ - 1) &&
               waterAt(cornerX - 1, y, cornerZ) && waterAt(cornerX, y, cornerZ);
    };

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

                    const BlockId block = volume.waterloggedAt(p.x, p.y, p.z)
                                              ? BlockId::Water0
                                              : volume.blockAt(p.x, p.y, p.z);
                    if (block == BlockId::Air) {
                        continue;
                    }
                    // Anything that is not a unit cube is emitted by the shape
                    // pass below. Teaching the greedy mask about partial faces
                    // would slow down the path that carries the whole world for
                    // the sake of a few decorative blocks.
                    const BlockShape shape = blockShape(block);
                    if (usesShapePass(shape)) {
                        continue;
                    }

                    // The whole optimisation: a face buried against something
                    // that hides it can never be seen, so it is never created.
                    // Water only shows where it meets air, so the faces between
                    // water and the seabed are skipped and you can see through.
                    const glm::ivec3 front = p + face.neighbourOffset;
                    const BlockId ahead = volume.waterloggedAt(front.x, front.y, front.z)
                                              ? BlockId::Water0
                                              : volume.blockAt(front.x, front.y, front.z);
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
                    // **The far tier drops those shared faces entirely**, which
                    // turns a canopy into the hollow shell the paragraph above
                    // deliberately refuses to build up close. It is by a wide
                    // margin the largest saving the tier makes: a solid blob of
                    // leaves emits every internal boundary, and every one of
                    // them is drawn from both sides.
                    //
                    // The silhouette is untouched - only faces *between two
                    // identical cutout blocks* go, never one that meets air. All
                    // that is lost is seeing more leaves through the holes in
                    // the near ones, at a distance where a hole is smaller than
                    // a pixel.
                    const bool cutoutInterior =
                        sharedWithOwnKind && (terrainOnly || !positiveFacing);
                    // **A fluid's face rules, not a translucent block's.** Lava
                    // is opaque and still needs these: a fluid draws only where
                    // it meets air or a thinner fluid below, so its internal
                    // faces never appear and the block behind it supplies its
                    // own face.
                    //
                    // "Air" here has to mean *anything that does not hide the
                    // face*, not the air block. Testing for air alone deleted
                    // the water surface under every lily pad, torch, plant,
                    // fence, pane of glass and slab standing in it - the block
                    // beside the water does not supply the water's own face, so
                    // the result was a hole you could see straight through.
                    const bool visible =
                        isFluid(block)
                            ? ((!isFluid(ahead) &&
                                !occludesFace(ahead, face.neighbourOffset.y)) ||
                               (isFluid(ahead) && ahead != block &&
                                face.neighbourOffset.y <= 0 &&
                                fluidLevel(ahead) > fluidLevel(block)))
                            : (!occludesFace(ahead, face.neighbourOffset.y) && !cutoutInterior);
                    if (!visible) {
                        continue;
                    }

                    sample.layer = blockTextureLayer(block, face.facing, face.direction,
                                                     volume.chestHalfAt(p.x, p.y, p.z));
                    sample.translucent = isTranslucent(block);
                    sample.doubleSided = isCutout(block);
                    sample.alpha = sample.translucent ? kWaterAlpha : 1.0f;
                    // Thinner flows sit lower, so a stream visibly tapers away
                    // from its source rather than running at full depth.
                    sample.surfaceDrop =
                        isFluid(block) ? static_cast<float>(fluidLevel(block)) * kWaterLevelDrop
                                       : 0.0f;
                    // **The far tier holds the surface still**, which is what
                    // lets an ocean merge greedily: a wave moves a quad's own
                    // corners, so every wavy face has to be emitted one cell at
                    // a time. Only the geometric displacement goes - the
                    // fragment stage shades the same wave field from world
                    // position, so distant water still ripples, it just no
                    // longer moves. The amplitude it gives up is a fraction of
                    // a pixel at the boundary's own distance.
                    sample.wavy = isWater(block) && !terrainOnly;
                    // Leaves move with the wind too, at a quarter of a blade's
                    // reach. **Every vertex of a leaf block, not just its top**:
                    // the displacement is a pure function of world position, so
                    // two blocks sharing an edge move identically and the canopy
                    // stays watertight. Moving only the tops would tear it.
                    sample.sways = isLeafBlock(block);

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
                        sample.occlusion[c] = kOcclusionSteps[static_cast<std::size_t>(occlusion)];
                    }

                    // **The far tier drops faces no light of any kind reaches**,
                    // which is the great majority of a voxel world: two thirds
                    // of everything this mesher emits sits in the bottom third
                    // of the map, on the walls of caves sealed inside rock.
                    //
                    // It is a rendering tier and it is safe by construction.
                    // Light here is a flood fill, so for such a face to be seen
                    // from outside there would have to be an opening - and an
                    // opening is exactly what would have raised its light above
                    // zero. What is dropped is geometry that renders black
                    // inside solid rock, at a distance where the hole you would
                    // have to look through is smaller than a pixel.
                    //
                    // Sky light is a *level*, not a time of day: outdoor faces
                    // read 15 at midnight as well as at noon, so nothing on the
                    // surface is ever caught by this.
                    if (terrainOnly) {
                        bool anyLight = false;
                        for (std::size_t c = 0; c < 4 && !anyLight; ++c) {
                            anyLight = sample.sky[c] > 0.0f || sample.block[c] > 0.0f;
                        }
                        if (!anyLight) {
                            sample = FaceSample{};
                            continue;
                        }
                    }

                    sample.flat = true;
                    for (std::size_t c = 1; c < 4; ++c) {
                        if (sample.sky[c] != sample.sky[0] || sample.block[c] != sample.block[0] ||
                            sample.occlusion[c] != sample.occlusion[0]) {
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
                    //
                    // **A surface vertex may only move if all four columns
                    // meeting it are water.** A wave that lifts the waterline
                    // above the bank beside it exposes the top of a water side
                    // face that was culled precisely because that bank was
                    // supposed to hide it - so you see straight through the
                    // seam. Dipping below the bank instead uncovers a strip of
                    // the bank's own side face. Both are the same mistake:
                    // **where water meets anything else it has to stay exactly
                    // flush**, and only open water is free to move.
                    const bool topCorner = face.corners[c].y > 0.5f;
                    const bool fluidTop =
                        sample.wavy && topCorner &&
                        surfaceIsOpen(origin.y,
                                      origin.x + static_cast<int>(face.corners[c].x * extent.x + 0.5f),
                                      origin.z + static_cast<int>(face.corners[c].z * extent.z + 0.5f));
                    if (sample.surfaceDrop > 0.0f && face.corners[c].y > 0.5f) {
                        position.y -= sample.surfaceDrop;
                    }
                    const glm::vec2 uv = face.uvs[c] * uvScale;
                    // Red is sky light, green is block light, blue is the face
                    // shade. Ambient occlusion rides in the surface word beside
                    // the normal instead of being folded into the blue channel,
                    // so a lighting pass can tell a dark corner from a
                    // downward-facing surface.
                    //
                    // **A double-sided quad keeps its own face normal**, and
                    // the fragment shader flips it toward the eye. Naming it
                    // "unaligned" instead - which is what this did until M25 -
                    // made the shader derive it from the triangle, and a
                    // derived normal on a two-sided quad always faces the
                    // camera. Every leaf on a tree was therefore lit as though
                    // it were turned to face you, so the sun's highlight
                    // followed you round the trunk.
                    mesh.vertices.push_back(engine::Vertex{
                        {position.x, position.y, position.z},
                        engine::packVertexColor(sample.sky[c], sample.block[c], face.shade, sample.alpha),
                        {uv.x, uv.y},
                        sample.layer,
                        engine::packVertexSurface(face.normalCode, sample.occlusion[c], fluidTop,
                                                  sample.sways ? engine::kSwayOneBlock : 0u)});
                }

                // Split along the darker diagonal. Quads with occlusion on
                // opposite corners otherwise show a visible seam running the
                // wrong way, which reads as a crease in flat ground.
                const float lit0 = sample.occlusion[0] * std::max(sample.sky[0], sample.block[0]);
                const float lit1 = sample.occlusion[1] * std::max(sample.sky[1], sample.block[1]);
                const float lit2 = sample.occlusion[2] * std::max(sample.sky[2], sample.block[2]);
                const float lit3 = sample.occlusion[3] * std::max(sample.sky[3], sample.block[3]);
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
                    // **A moving surface cannot be merged.** The wave displaces
                    // vertices, and a quad has only its four corners - so a
                    // sixteen-block merged face becomes one flat tilted plane
                    // instead of a swell, and where it meets a differently-sized
                    // neighbour their shared edge disagrees about its own
                    // height. That is a visible crease across open water, and
                    // the larger the merge the straighter and longer the cut.
                    if (sample.wavy) {
                        emit(i, j, 1, 1, sample);
                        ++i;
                        continue;
                    }

                    const auto matches = [&](const FaceSample& other) {
                        return other.layer == sample.layer && other.flat && other.sky[0] == sample.sky[0] &&
                               other.block[0] == sample.block[0] &&
                               other.occlusion[0] == sample.occlusion[0] &&
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
                              float layer, float sky, float blockLight, float shading,
                              std::uint32_t normalCode, bool doubleSided, bool sways = false,
                              std::uint32_t rootHalfBlocks = 0) {
        engine::MeshData& mesh = result.opaque;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        // **Measured against this quad's own extent, not against a block
        // boundary.** A plant blade's corners sit at whole world heights, so
        // asking for a fractional part above a half is asking a question whose
        // answer is always no - which is why grass stood perfectly still while
        // leaves moved.
        float lowest = corners[0].y;
        float highest = corners[0].y;
        for (const glm::vec3& corner : corners) {
            lowest = std::min(lowest, corner.y);
            highest = std::max(highest, corner.y);
        }
        const float middle = (lowest + highest) * 0.5f;
        for (std::size_t c = 0; c < 4; ++c) {
            // Height above the plant's own root, so the value is continuous
            // across a block edge and a stalk leans as one piece. The very
            // bottom of the root block is zero, which is what keeps it planted.
            const std::uint32_t sway =
                sways ? rootHalfBlocks + (corners[c].y > middle ? engine::kSwayOneBlock : 0u) : 0u;
            mesh.vertices.push_back(
                engine::Vertex{{corners[c].x, corners[c].y, corners[c].z},
                               engine::packVertexColor(sky, blockLight, shading, 1.0f),
                               {uvs[c].x, uvs[c].y},
                               layer,
                               engine::packVertexSurface(normalCode, 1.0f, false, sway)});
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
                if (!usesShapePass(shape)) {
                    continue;
                }
                // The distance tier, and the only place it acts. Everything the
                // greedy pass above emits is terrain and is untouched, so a
                // far chunk keeps its exact outline and its baked lighting -
                // which lives in vertex colours on that geometry and therefore
                // costs nothing to keep.
                if (terrainOnly && isDistantDecoration(block)) {
                    continue;
                }

                const glm::vec3 cellOrigin = originOffset + glm::vec3{x, y, z};
                float sky = 0.0f;
                float blockLight = 0.0f;

                if (shape == BlockShape::Cross) {
                    // A plant sits in an otherwise empty cell, so it is lit by
                    // its own cell rather than by a neighbour.
                    lightOf({x, y, z}, sky, blockLight);

                    // **How much of this plant is underneath this block.** A
                    // stalk of sugar cane or bamboo is several blocks of the
                    // same id in one column, and the wind has to see it as one
                    // plant or each block bends against the one below it.
                    //
                    // The walk stops at the volume's own edge, so a stalk that
                    // straddles a chunk boundary leans slightly less below the
                    // join than above it. Meshing may not read outside what it
                    // was given, and a stalk is at most seven blocks against a
                    // thirty-two block chunk.
                    std::uint32_t rootHalfBlocks = 0;
                    for (int below = 1; below <= 7; ++below) {
                        if (y - below < -1 || volume.blockAt(x, y - below, z) != block) {
                            break;
                        }
                        rootHalfBlocks += engine::kSwayOneBlock;
                    }

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
                        // A blade stands at 45 degrees, so it has no axis to
                        // name and takes the unaligned code.
                        pushQuad(corners, kBottomLeftWinding, layer, sky, blockLight, 1.0f,
                                 engine::kNormalUnaligned, true, true, rootHalfBlocks);
                    }
                    continue;
                }

                // Slabs and stairs are both just boxes, and they read the very
                // same table collision does, so what you see and what you bump
                // into cannot disagree.
                //
                // A fence is the exception: its arms follow its neighbours,
                // which the id alone cannot express, so the drawn shape is
                // computed here - and `Collision.hpp`'s `worldCollisionBoxes`
                // computes it through the same `connectionBits`, so the two
                // still cannot disagree.
                BlockBoxes shapeBoxes = collisionBoxes(block);
                // A *model* rather than a shape cut out of a cube, so each box
                // names the rectangle of texture it samples instead of taking
                // it from where the box happens to sit.
                ModelBoxes model;
                if (shape == BlockShape::Model || shape == BlockShape::Cocoa ||
                    shape == BlockShape::Bed) {
                    model = postModel(block);
                    shapeBoxes = BlockBoxes{};
                    for (int i = 0; i < model.count; ++i) {
                        shapeBoxes.boxes[shapeBoxes.count++] = model.boxes[i].box;
                    }
                } else if (shape == BlockShape::Vine) {
                    // The ceiling sheet is **derived from the world, not from
                    // the id** - which is Bedrock's own arrangement and why
                    // sixteen ids cover the block where Java needs thirty-two.
                    shapeBoxes = vineBoxes(vineSides(block),
                                           isOpaque(volume.blockAt(x, y + 1, z)));
                } else if (shape == BlockShape::Ladder) {
                    // Drawn but not collided with, the same split the fence
                    // uses in the other direction: `collisionBoxes` answers
                    // nothing here so you can step onto the rungs.
                    shapeBoxes = ladderBoxes(ladderFacing(block));
                } else if (drawsWithoutColliding(block)) {
                    // The same split again, for the five redstone shapes you
                    // walk straight over.
                    shapeBoxes = uncollidableDrawnBoxes(block);
                } else if (shape == BlockShape::Fence || shape == BlockShape::Wall ||
                           shape == BlockShape::Pane) {
                    // A fence reaches toward fences and gates; a wall reaches
                    // toward walls and gates; a pane reaches toward panes and
                    // bars. `connectionBits` owns that rule, so what is drawn
                    // and what is bumped into cannot disagree about it.
                    const std::uint8_t connections =
                        connectionBits(shape, volume.blockAt(x, y, z - 1), volume.blockAt(x, y, z + 1),
                                       volume.blockAt(x - 1, y, z), volume.blockAt(x + 1, y, z));
                    shapeBoxes = shape == BlockShape::Fence  ? fenceRailBoxes(connections)
                                 : shape == BlockShape::Wall ? wallBoxes(connections)
                                                             : paneBoxes(connections);
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
                            // Two arms meeting at a cell wall each drew their
                            // own end cap: coplanar quads fighting over one
                            // depth, which is the hard line that ran down the
                            // middle of what should be one sheet of glass.
                            if (connectsToNeighbours(shape) && face.neighbourOffset.y == 0 &&
                                blockShape(volume.blockAt(ahead.x, ahead.y, ahead.z)) == shape) {
                                continue;
                            }
                            lightOf(ahead, sky, blockLight);
                        } else {
                            // **Not the block's own cell.** A partial block's
                            // cell is solid, so light never propagates into it
                            // and reads as zero - which painted every stair
                            // step, slab top and fence rail pure black. The
                            // face is lit by whatever it actually looks at, and
                            // where that is solid too, by the brightest cell
                            // touching this one, so a fence inside a wall does
                            // not go dark either.
                            const glm::ivec3 ahead{x + face.neighbourOffset.x, y + face.neighbourOffset.y,
                                                   z + face.neighbourOffset.z};
                            if (!isOpaque(volume.blockAt(ahead.x, ahead.y, ahead.z))) {
                                lightOf(ahead, sky, blockLight);
                            } else {
                                sky = 0.0f;
                                blockLight = 0.0f;
                                for (const Face& around : kFaces) {
                                    float neighbourSky = 0.0f;
                                    float neighbourBlock = 0.0f;
                                    lightOf({x + around.neighbourOffset.x, y + around.neighbourOffset.y,
                                             z + around.neighbourOffset.z},
                                            neighbourSky, neighbourBlock);
                                    sky = std::max(sky, neighbourSky);
                                    blockLight = std::max(blockLight, neighbourBlock);
                                }
                            }
                        }

                        // Faces buried inside another box of the same block are
                        // never seen, and emitting them leaves two coplanar
                        // quads fighting over one depth - a stair's step sits
                        // directly on its own lower half.
                        //
                        // **The whole face, not its middle.** A lantern's cap
                        // covers the centre of its body's lid and leaves a ring
                        // of it showing all round; a centre probe called that
                        // buried and cut a hole straight through the top of
                        // every lantern in the game.
                        const glm::vec3 mid{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
                        const glm::vec3 outward = glm::vec3{face.neighbourOffset} * 0.01f;
                        std::array<glm::vec3, 5> probes{};
                        probes[0] = mid;
                        for (std::size_t c = 0; c < 4; ++c) {
                            // Pulled a hundredth in from the corner, so a face
                            // exactly flush with another box's edge still counts
                            // as covered.
                            probes[c + 1] = glm::mix(lo + face.corners[c] * (hi - lo), mid, 0.01f);
                        }
                        for (glm::vec3& probe : probes) {
                            probe[axis] = positive ? hi[axis] : lo[axis];
                            probe += outward;
                        }

                        bool buried = false;
                        for (int j = 0; j < shapeBoxes.count && !buried; ++j) {
                            if (j == i) {
                                continue;
                            }
                            const BlockBox& o = shapeBoxes.boxes[j];
                            buried = true;
                            for (const glm::vec3& probe : probes) {
                                if (!(probe.x > o.minX && probe.x < o.maxX && probe.y > o.minY &&
                                      probe.y < o.maxY && probe.z > o.minZ && probe.z < o.maxZ)) {
                                    buried = false;
                                    break;
                                }
                            }
                        }
                        if (buried) {
                            continue;
                        }

                        // U and V each run the same way as the corner on some
                        // faces and the opposite way on others, so both are
                        // read off the tables rather than assumed. **Leaving U
                        // out is what mirrored every shaped block's +X and -Z
                        // faces** - visible the moment a texture had a letter
                        // on it, which is why lit TNT read backwards.
                        const int uAxis = face.uAxis;
                        const int vAxis = face.vAxis;
                        bool flipU = std::abs(face.uvs[0].x - face.corners[0][uAxis]) > 0.5f;
                        const bool flipV = std::abs(face.uvs[0].y - face.corners[0][vAxis]) > 0.5f;
                        // Mirroring is right for a texture that should read the
                        // same from every side and wrong for one that marks a
                        // world direction, which is why a bed's pillow came out
                        // at the head end on one long side and the foot on the
                        // other.
                        if (model.count > 0 && axis != 1 &&
                            model.boxes[i].unmirror == face.direction) {
                            flipU = !flipU;
                        }

                        // A model box paints its lid from a different corner of
                        // its net than its walls.
                        float uLow = 0.0f;
                        float uHigh = 1.0f;
                        float vLow = 0.0f;
                        float vHigh = 1.0f;
                        if (model.count > 0) {
                            const ModelBox& m = model.boxes[i];
                            const bool lid = axis == 1;
                            uLow = lid ? m.topUMin : m.uMin;
                            uHigh = lid ? m.topUMax : m.uMax;
                            vLow = lid ? m.topVMin : m.vMin;
                            vHigh = lid ? m.topVMax : m.vMax;
                        }

                        std::array<glm::vec3, 4> corners{};
                        std::array<glm::vec2, 4> uvs{};
                        for (std::size_t c = 0; c < 4; ++c) {
                            // Mixing the shared unit-cube corners keeps the
                            // winding the face tables established.
                            const glm::vec3 local = lo + face.corners[c] * (hi - lo);
                            corners[c] = cellOrigin + local;
                            if (model.count > 0) {
                                float fu = face.corners[c][uAxis];
                                float fv = face.corners[c][vAxis];
                                // A lid may be turned, which is how a model
                                // says which way round a texture goes when the
                                // block itself cannot be rotated.
                                if (axis == 1) {
                                    switch (model.boxes[i].lidTurns) {
                                    case 1: {
                                        const float wasU = fu;
                                        fu = fv;
                                        fv = 1.0f - wasU;
                                        break;
                                    }
                                    case 2:
                                        fu = 1.0f - fu;
                                        fv = 1.0f - fv;
                                        break;
                                    case 3: {
                                        const float wasU = fu;
                                        fu = 1.0f - fv;
                                        fv = wasU;
                                        break;
                                    }
                                    default:
                                        break;
                                    }
                                }
                                uvs[c] = {flipU ? glm::mix(uHigh, uLow, fu) : glm::mix(uLow, uHigh, fu),
                                          flipV ? glm::mix(vHigh, vLow, fv) : glm::mix(vLow, vHigh, fv)};
                            } else {
                                uvs[c] = {flipU ? 1.0f - local[uAxis] : local[uAxis],
                                          flipV ? 1.0f - local[vAxis] : local[vAxis]};
                            }
                        }
                        pushQuad(corners, uvs,
                                 // A model box may name its own layer, which is
                                 // what lets a bell be a wooden frame round a
                                 // gold bell rather than gold throughout.
                                 [&] {
                                     if (model.count > 0) {
                                         const ModelBox& m = model.boxes[i];
                                         const float own = axis == 1 ? m.lidLayer : m.sideLayer;
                                         if (own >= 0.0f) {
                                             return own;
                                         }
                                     }
                                     return blockTextureLayer(block, face.facing, face.direction);
                                 }(),
                                 sky, blockLight, face.shade,
                                 shape == BlockShape::Vine ? engine::kNormalUnaligned : face.normalCode,
                                 shape == BlockShape::Vine);
                    }
                }
            }
        }
    }

    return result;
}

} // namespace game
