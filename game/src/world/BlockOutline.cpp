#include "world/BlockOutline.hpp"

#include "world/Block.hpp"
#include "world/Collision.hpp"
#include "world/FaceGeometry.hpp"
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

// ---------------------------------------------------------------------------
// The six faces of a box, and the proof that this file places them the way the
// world does.
// ---------------------------------------------------------------------------

/// **This file used to carry its own copy of the cube's corner rows.** It was
/// the fifth, byte for byte identical to `FaceGeometry.hpp`'s and with no
/// assert of any kind in it - and it is the copy the eye could never have
/// caught, because a selection cage is one flat colour and a crack is a spray
/// of lines: a reversed row turns a bar inside out and a mirrored one flips a
/// crack, and neither changes what you are looking at. `cornersOf` is the one
/// owner now, and `kBottomLeftWinding` is the rect the mesher hands every quad,
/// so the crack picture cannot be mirrored against the block it sits on.
///
/// Both meshes below place a corner the same way: `glm::mix(lo, hi, corner)`,
/// which for a 0/1 corner is "take the low end of the box on this axis, or the
/// high one". That choice is what the proofs are about - the shared header
/// already proves the *rows*, and repeating that here would prove nothing this
/// file can get wrong.

/// A placed corner, as three plain floats. Not a `glm::vec3`, because GLM's
/// vector constructors are not reliably usable in a constant expression and a
/// proof that cannot be written is a proof that does not exist.
struct BoxCorner {
    float x;
    float y;
    float z;
};

constexpr float axisOf(const BoxCorner& c, int axis) {
    return axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
}

/// `glm::mix(lo, hi, corner)` for a 0/1 corner, written as the choice it is.
constexpr BoxCorner placeCorner(FaceCorners corners, int vertex, const BoxCorner& lo,
                                const BoxCorner& hi) {
    return {corners[vertex][0] != 0 ? hi.x : lo.x, corners[vertex][1] != 0 ? hi.y : lo.y,
            corners[vertex][2] != 0 ? hi.z : lo.z};
}

/// That a face named `+X` lands on the box's **geometrically** high-X side once
/// placed - the greater of the two corners handed in, not simply the one called
/// `hi`.
///
/// **That distinction is the whole proof.** Stating it against the parameter
/// named `hi` would be invariant under swapping the two, because the rule and
/// the placement would both follow the swap; stating it against the greater
/// coordinate catches it. And a swap is the one mistake this file can make that
/// the shared header cannot see: it mirrors the box through its own centre,
/// which leaves every winding untouched - the two edge vectors are both negated
/// and their cross product is unchanged - so a winding test passes happily on a
/// box drawn inside out, every bar is culled from the outside, and the cage is
/// simply not there.
constexpr bool placedFaceIsOnTheOuterSide(AxisFace face, FaceCorners corners, const BoxCorner& lo,
                                          const BoxCorner& hi) {
    const int axis = faceAxis(face);
    const float outer = faceIsPositive(face) ? std::max(axisOf(lo, axis), axisOf(hi, axis))
                                             : std::min(axisOf(lo, axis), axisOf(hi, axis));
    for (int c = 0; c < 4; ++c) {
        if (axisOf(placeCorner(corners, c, lo, hi), axis) != outer) {
            return false;
        }
    }
    return true;
}

/// That the first triangle still turns counter-clockwise seen from outside once
/// the corners are spread over a real box, so the bar survives back-face
/// culling. Derived from `faceOutwardNormal` rather than compared against a
/// second copy of the corner list.
constexpr bool placedFaceWindsOutward(AxisFace face, FaceCorners corners, const BoxCorner& lo,
                                      const BoxCorner& hi) {
    const BoxCorner a = placeCorner(corners, 0, lo, hi);
    const BoxCorner b = placeCorner(corners, 1, lo, hi);
    const BoxCorner c = placeCorner(corners, 2, lo, hi);
    const float ux = b.x - a.x;
    const float uy = b.y - a.y;
    const float uz = b.z - a.z;
    const float vx = c.x - a.x;
    const float vy = c.y - a.y;
    const float vz = c.z - a.z;
    const glm::ivec3 normal = faceOutwardNormal(face);
    const float cross[3]{uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
    return cross[0] * static_cast<float>(normal.x) + cross[1] * static_cast<float>(normal.y) +
               cross[2] * static_cast<float>(normal.z) >
           0.0f;
}

/// A box with a different extent, a different sign and a different origin on
/// every axis, so a proof cannot pass by accident on a symmetric one. These are
/// the shapes this file really draws: a bar of the selection cage is long on
/// one axis and `kThickness` on the other two, and a crack box is a block's own
/// extent grown by `kCrackGrow`.
constexpr BoxCorner kProofLo{-0.004f, 0.5f, -3.25f};
constexpr BoxCorner kProofHi{0.018f, 1.5f, 2.0f};

/// Every rule this file can break, over every face it draws. **The shared
/// header's three proofs about the rows are deliberately not repeated here** -
/// they are `static_assert`ed at the point the rows are defined, so calling
/// them again from this file would be a line that cannot fail.
constexpr bool everyPlacedFaceHolds(const BoxCorner& lo, const BoxCorner& hi) {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        const auto face = static_cast<AxisFace>(i);
        if (!placedFaceIsOnTheOuterSide(face, cornersOf(face), lo, hi) ||
            !placedFaceWindsOutward(face, cornersOf(face), lo, hi)) {
            return false;
        }
    }
    return true;
}

/// **The proofs, shown failing**, because a proof nobody has watched reject
/// something is indistinguishable from a tautology.
///
/// Two different faults, because the two rules catch different things. A
/// reversed row - the exact defect that made a falling block inside-out - keeps
/// every corner on its own face and is caught only by the winding; an inside-out
/// box keeps every winding and is caught only by the naming.
constexpr bool reversedRowIsRejectedOnABox(AxisFace face, const BoxCorner& lo,
                                           const BoxCorner& hi) {
    const std::uint8_t reversed[4][3]{
        {cornersOf(face)[3][0], cornersOf(face)[3][1], cornersOf(face)[3][2]},
        {cornersOf(face)[2][0], cornersOf(face)[2][1], cornersOf(face)[2][2]},
        {cornersOf(face)[1][0], cornersOf(face)[1][1], cornersOf(face)[1][2]},
        {cornersOf(face)[0][0], cornersOf(face)[0][1], cornersOf(face)[0][2]},
    };
    return placedFaceIsOnTheOuterSide(face, reversed, lo, hi) &&
           !placedFaceWindsOutward(face, reversed, lo, hi);
}

constexpr bool everyFaultIsRejected(const BoxCorner& lo, const BoxCorner& hi) {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        const auto face = static_cast<AxisFace>(i);
        if (!reversedRowIsRejectedOnABox(face, lo, hi)) {
            return false;
        }
        // The inside-out box, which is `lo` and `hi` exchanged.
        if (placedFaceIsOnTheOuterSide(face, cornersOf(face), hi, lo)) {
            return false;
        }
    }
    return true;
}

static_assert(everyPlacedFaceHolds(kProofLo, kProofHi),
              "the selection cage and the crack overlay no longer place a face where its own name "
              "says it goes, or wind it outward - see the failing rule in FaceGeometry.hpp");
static_assert(everyFaultIsRejected(kProofLo, kProofHi),
              "the proofs above accept a reversed row or a box drawn inside out, so they would "
              "have passed on the bugs they exist to catch");

void appendBox(engine::MeshData& mesh, const glm::vec3& lo, const glm::vec3& hi) {
    for (int f = 0; f < static_cast<int>(AxisFace::Count); ++f) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

        for (const glm::vec3& corner : faceCorners(static_cast<AxisFace>(f))) {
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
    //
    // **The rect is the mesher's own**, `kBottomLeftWinding` out of
    // `FaceGeometry.hpp`, paired with that header's corner rows in that header's
    // order. There is nothing here to keep in step: a crack laid over a block
    // reads the same way round as the block's own art by construction, where a
    // fourth copy of "0,1 1,1 1,0 0,0" would only have agreed until somebody
    // edited one of them. A crack picture is a spray of lines, so a mirrored
    // one is invisible to the eye - exactly how the dropped item's mirror
    // survived twenty milestones.

    // A plant is two crossed blades rather than a box, and it is the one family
    // no box function can describe - so the blades are the mesher's own, corner
    // for corner: `ChunkMesher.cpp`'s shape pass insets a `Cross` blade by 0.05
    // of a cell at both ends and lists it low-left, low-right, high-right,
    // high-left, which is what these two rows are. Verified against that pass
    // rather than taken on trust, because a cage drawn round a flower is what
    // this branch exists to avoid.
    if (blockShape(id) == BlockShape::Cross) {
        constexpr float lo = 0.05f;
        constexpr float hi = 1.0f - lo;
        for (int blade = 0; blade < 2; ++blade) {
            const float x0 = blade == 0 ? lo : hi;
            const float x1 = blade == 0 ? hi : lo;
            quad({origin + glm::vec3{x0, 0.0f, lo}, origin + glm::vec3{x1, 0.0f, hi},
                  origin + glm::vec3{x1, 1.0f, hi}, origin + glm::vec3{x0, 1.0f, lo}},
                 kBottomLeftWinding);
        }
        return mesh;
    }

    // Grown a shade so the crack sits just outside the surface it belongs to
    // rather than fighting it for the same depth. Same trick and the same order
    // of magnitude as the selection cage above.
    constexpr float kCrackGrow = 0.002f;

    // **A rail is one plane, not a box, and this is the second family that is
    // true of.** `ChunkMesher.cpp` emits a rail as a single quad on the lid's
    // corner order, tilted for the 28 ascending ids; `drawnBoxes` can only
    // answer with the flat 1/16 slab it shares with selection, so a crack drawn
    // from boxes sat *under* the track on a ramp rather than on it. Everything
    // here is asked of the same owner the mesher asks - `railShape` decodes the
    // id and `railCornerY` owns which edge is raised - so this is a second
    // reader rather than a second description, exactly as the `Cross` branch
    // above reads the mesher's own inset.
    //
    // The lift is along world up rather than along the plane's normal: on a
    // 45-degree ramp that clears the surface by `kCrackGrow` times cos 45, still
    // an order of magnitude more than the depth buffer needs at this range, and
    // it keeps a flat rail's crack exactly `kCrackGrow` above its track.
    if (isRail(id)) {
        constexpr auto lid = static_cast<std::size_t>(AxisFace::PosY);
        const int shape = railShape(id);
        std::array<glm::vec3, 4> corners{};
        for (int c = 0; c < 4; ++c) {
            const int cornerX = kFaceCornerBits[lid][c][0];
            const int cornerZ = kFaceCornerBits[lid][c][2];
            corners[static_cast<std::size_t>(c)] =
                origin + glm::vec3{static_cast<float>(cornerX),
                                   railCornerY(shape, cornerX, cornerZ) + kCrackGrow,
                                   static_cast<float>(cornerZ)};
        }
        quad(corners, kBottomLeftWinding);
        return mesh;
    }

    const BlockBoxes boxes = worldDrawnBoxes(world, cell.x, cell.y, cell.z);
    for (int i = 0; i < boxes.count; ++i) {
        const BlockBox& b = boxes.boxes[i];
        const glm::vec3 low = origin + glm::vec3{b.minX - kCrackGrow, b.minY - kCrackGrow,
                                                 b.minZ - kCrackGrow};
        const glm::vec3 high = origin + glm::vec3{b.maxX + kCrackGrow, b.maxY + kCrackGrow,
                                                  b.maxZ + kCrackGrow};
        for (int f = 0; f < static_cast<int>(AxisFace::Count); ++f) {
            const std::array<glm::vec3, 4> unit = faceCorners(static_cast<AxisFace>(f));
            std::array<glm::vec3, 4> corners{};
            for (int c = 0; c < 4; ++c) {
                corners[c] = glm::mix(low, high, unit[c]);
            }
            quad(corners, kBottomLeftWinding);
        }
    }

    return mesh;
}

} // namespace game
