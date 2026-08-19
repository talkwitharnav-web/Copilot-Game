#include "world/ChunkMesher.hpp"

#include "world/FaceGeometry.hpp"
#include "world/FaceShading.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>

namespace game {
namespace {

struct Face {
    glm::ivec3 neighbourOffset;
    /// Corners of the face on a unit cube, counter-clockwise seen from outside.
    /// Reverse any of these and that face vanishes under backface culling.
    ///
    /// **Read from `FaceGeometry.hpp`, which owns them** - they used to be
    /// written out here and transcribed into the three other places a block is
    /// drawn, and the falling cube's copy was this table's rows *reversed*: a
    /// V-flip on all 24 corners and an inward winding on all 6 faces.
    std::array<glm::vec3, 4> corners;
    /// Texture coordinates for those same four corners, in the same order.
    /// V grows downward, matching how image rows are stored. Same owner as the
    /// corners, because a rect only means anything paired with the list it was
    /// written for.
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

/// **Provably complete, by declared size against literal rows.** 2026-08-19.
/// The array declares 6 and contains 6 brace-initialised rows, so every face
/// this mesher can emit is written out longhand here. That is the property a
/// token search cannot establish: a row produced by a cast, by arithmetic or in
/// a loop would still count toward the declared size while contributing no
/// literal row, and would show up as a shortfall. There is none, so there is no
/// seventh face hiding behind an expression, and a sweep of this table is a
/// sweep of the whole set.
///
/// Worth stating because the icon path in `HudPrimitives.cpp` deliberately
/// draws only three of these, and the two files are checked against each other.
/// **Falsified by** the declared size and the literal row count disagreeing.
constexpr std::array<Face, 6> kFaces{{
    // +X
    {{1, 0, 0},
     faceCorners(AxisFace::PosX),
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
     faceCorners(AxisFace::NegX),
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
     faceCorners(AxisFace::PosY),
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
     faceCorners(AxisFace::NegY),
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
     faceCorners(AxisFace::PosZ),
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
     faceCorners(AxisFace::NegZ),
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

/// How much of a block each level of water gives up, as a fraction of a cell.
/// Level 7 is a thin film.
///
/// **One ninth exactly, not a rounded 0.11.** The reference states a fluid's
/// fill as `amount / 9` and `Fluid.hpp::fluidHeight` divides by that same nine,
/// so the step drawn here has to be the same ninth or the surface you see and
/// the surface you swim against drift apart as the level rises: 0.11 agreed
/// only at level 0 and left a level-7 film 7.8 mm above where the physics put
/// it. Two owners for one number, which `audit.md` section 12.9 already named.
///
/// The drawn ladder still sits **one step high on purpose** - a source draws
/// with no drop at all, so its top is flush with the cell boundary rather than
/// at 8/9, which is what `kWaveDisplacement` in `engine/shaders/waves.glsl`
/// documents and what makes a full ocean look full. Once this is exact that
/// offset is a constant ninth at every level, which is the only way it can be
/// stated as one sentence rather than as a table of eight differences.
constexpr float kWaterLevelDrop = 1.0f / 9.0f;

/// The nine is `kMaxWaterLevel + 2`: seven flowing levels, the source they fall
/// away from, and the one ninth the reference leaves empty above a source. Bound
/// to `Block.hpp`'s level range rather than to `Fluid.hpp`'s height function
/// because **meshing may not include `Fluid.hpp`** - that would pull in
/// `World.hpp` and end the "pure function of blocks and borders" rule this file
/// exists under. Compared with a tolerance because the round trip through a
/// binary float is not obliged to land back on 1 exactly.
constexpr float kWaterLevelDropError =
    kWaterLevelDrop * static_cast<float>(kMaxWaterLevel + 2) - 1.0f;
static_assert(kWaterLevelDropError < 1.0e-6f && kWaterLevelDropError > -1.0e-6f,
              "the drawn step must be the reference's ninth, derived from the level range");
static_assert(kMaxWaterLevel == kMaxLavaLevel,
              "one drop serves both fluids, so a level range that stops serving both breaks it");

/// How much of the world shows through water.
constexpr float kWaterAlpha = 0.72f;

/// How much of what is behind a translucent block survives it.
///
/// **Glass answers 1.0 because its art already says**, and that is the fix for
/// what the user reported as "it looks like a ghost". Its alpha map is a frame
/// at 200, a diagonal highlight streak at 155 and a centre panel at 110; this
/// used to hand back the *average* of those - 0.46, or 0.51 for tinted - and
/// one number for the whole face is exactly a ghost. Staging no longer flattens
/// that map and the blended pass no longer runs the cutout test over it, so the
/// three survive all the way to the blend and the vertex has nothing left to
/// say. Water is the other way round: its art is opaque and the number here is
/// the whole of it.
constexpr float translucentAlpha(BlockId block) {
    return isBlendedGlass(block) ? 1.0f : kWaterAlpha;
}

/// Whether `ahead` is the **same fluid** as `block`, which is the only case the
/// level comparison below is a rule about.
///
/// `isFluid` covers both fluids at once, and everything that reads it treats
/// them as interchangeable - which is right for "can something move through
/// this" and wrong for "does this hide my face". The two fluids are disjoint,
/// so being water settles it.
constexpr bool sameFluidAs(BlockId block, BlockId ahead) {
    return isFluid(ahead) && isWater(block) == isWater(ahead);
}

static_assert(sameFluidAs(BlockId::Water0, BlockId::Water3) &&
                  sameFluidAs(BlockId::Water0, BlockId::WaterFalling),
              "every water cell is the same fluid as every other, whatever its level");
static_assert(!sameFluidAs(BlockId::Water0, BlockId::Lava0) &&
                  !sameFluidAs(BlockId::Lava0, BlockId::Water0),
              "water and lava are not, read from either side - which is the bug this exists for");
static_assert(!sameFluidAs(BlockId::Water0, BlockId::Stone),
              "and nothing that is not a fluid is one");
// The other half of that rule, and the half a comment cannot keep honest: once
// the neighbour is a *different* fluid the face falls through to the ordinary
// occlusion test, so the fix only closes the hole while that test lets it
// through. Both fluids are `BlockShape::Empty`, so `occludesFace` answers false
// at every offset. **The single edit that makes this fail is giving either
// fluid `BlockShape::Full`** - after which lava would hide water's face and the
// water-lava boundary would quietly be a hole again from one side.
static_assert(!occludesFace(BlockId::Lava0, -1) && !occludesFace(BlockId::Lava0, 0) &&
                  !occludesFace(BlockId::Lava0, 1) && !occludesFace(BlockId::Water0, -1) &&
                  !occludesFace(BlockId::Water0, 0) && !occludesFace(BlockId::Water0, 1),
              "no fluid hides the other fluid's face, at any offset");

/// `MeshFacing` names the same six directions, in the same order, as the array
/// the mesher actually walks.
///
/// **`ChunkMeshes::translucentFacings` is indexed by one and filled by the
/// other**, so the single edit that makes this fail is moving an entry of
/// `kFaces` - or adding one - without moving the matching enumerator. **The
/// assert is the only guard there can be**: nothing reads the ranges today, and
/// a consumer that did would be handed the +X run labelled -Z with every index
/// in it still valid. It would draw, it would not crash, no validation layer
/// would say a word, and the order would simply be wrong.
///
/// **"Nothing reads them" is a measurement with a date, not a property of the
/// code - so here is the date, the test, and the control.** Re-measured
/// 2026-08-19 11:48.
///
/// **Four mentions are code, and not one of them subscripts the array**: the
/// declaration on `ChunkMeshes`, its twin on `ChunkMeshUpdate` over in
/// `World.hpp`, the fill in `buildMesh` below, and `World.cpp` copying the
/// array straight through into the update struct. That copy is why a call-site
/// sweep is not enough on its own - the value travels inside a struct, so a
/// consumer would never name this function.
///
/// **Prose mentions are deliberately not counted.** An earlier version of this
/// paragraph said "seven mentions" and itemised them; hardening the paragraph
/// added prose and the figure was stale within the hour, against a total that
/// is nine as this is written. Worse, re-deriving it locally gave seven again
/// from a different set - `World.cpp` and `World.hpp` omitted, extra prose
/// counted - and the two sevens agreeing nearly justified deleting the
/// `World.cpp` line, which is true. **A total is not a set.**
///
/// **This is an ABSENCE claim, so it can only ever have an instrument control.**
/// There is no same-symbol control available: the whole content of the claim is
/// that there is nothing there to measure, so varying a condition around the
/// subject has nothing to move. What can be shown instead is that the detector
/// was capable of seeing a read had one existed - and how many places it
/// actually looked, so that a zero reads as *absent* rather than as *blind*.
///
/// **Denominator: 97 files under `game/src`, swept 2026-08-19 11:17.** Nine
/// mentions today, four of them code and listed above, none a subscript.
///
/// **Instrument control**, all in `Main.cpp`, one search, one moment:
/// `translucentShaped` 2 - it takes `.first` and `.count` off the update struct
/// - against `translucentFacings` 0, against an invented `opaqueFacings` 0, a
/// member that does not exist anywhere. Non-uniform, and the zero side includes
/// a name that *cannot* be found, so a zero here carries information rather
/// than merely being one. The non-zero side was predicted before it was
/// measured, by `ChunkMesher.hpp`'s own note that "unlike the six, this one is
/// read" - a different author in a different file, which is much harder to
/// fool oneself with than a number chosen after the fact.
///
/// **`Renderer.cpp` returns 0 for both and is therefore worthless here** - it
/// is vacuous on both sides, and correctly so, since `engine/` may not name a
/// game type at all. Recorded because testing only there would have produced
/// 0 and 0 and permitted any conclusion at all.
///
/// It also settles the struct question empirically: the consumer writes the
/// member's own name, so searching for that name is sufficient even though the
/// value travels inside a struct.
///
/// **Falsified by**: any expression that *subscripts* `translucentFacings`, or
/// that reads `ChunkMeshUpdate::translucentFacings`, anywhere outside this file.
/// The moment one exists, the ordering above stops being theoretical and this
/// paragraph must be rewritten rather than re-dated - and the assert below
/// becomes load-bearing instead of precautionary.
///
/// **Why this is written at all**: a correct-because-unread claim expires the
/// instant somebody makes it read, and nothing announces that. The renderer
/// gained a wind consumer tonight and turned another file's correct
/// "nothing reads it" note into a live defect between two saves of the same
/// tree. Dating the claim is what makes the recheck cost a minute.
constexpr bool facingsMatchFaceTable() {
    for (std::size_t i = 0; i < kFaces.size(); ++i) {
        if (kFaces[i].neighbourOffset != facingNormal(static_cast<MeshFacing>(i))) {
            return false;
        }
    }
    return true;
}

/// That row `i` of the face table is holding row `i`'s corners.
///
/// The corners come from `FaceGeometry.hpp` now, indexed by `AxisFace`, while
/// `neighbourOffset` is still written out here and indexed by `MeshFacing` -
/// **three orders that have to be the same one**, and only the first two were
/// tied together. Hand `faceCorners` the wrong enumerator and the mesher emits
/// one face's quad while testing the *opposite* neighbour for occlusion, so a
/// buried face is drawn and a visible one is culled. Both enums are dense and
/// start at the same direction, which is exactly why swapping them compiles.
///
/// Checked by deriving each corner's side from the offset the mesher actually
/// samples with, rather than by comparing the two tables to each other.
constexpr bool cornersSitOnTheFaceSampled() {
    for (std::size_t i = 0; i < kFaces.size(); ++i) {
        const glm::ivec3 offset = kFaces[i].neighbourOffset;
        const int axis = offset.x != 0 ? 0 : (offset.y != 0 ? 1 : 2);
        const float side = offset[axis] > 0 ? 1.0f : 0.0f;
        for (const glm::vec3& corner : kFaces[i].corners) {
            if (corner[axis] != side) {
                return false;
            }
        }
        if (faceOutwardNormal(static_cast<AxisFace>(i)) != offset) {
            return false;
        }
    }
    return true;
}

static_assert(kFaces.size() == kMeshFacings, "one range per face the mesher emits, and no more");
static_assert(facingsMatchFaceTable(), "MeshFacing and kFaces must name the same six, in order");
static_assert(cornersSitOnTheFaceSampled(),
              "a face's corners are on a different side of the cube from the neighbour it "
              "samples - AxisFace and MeshFacing have drifted apart");

// ---------------------------------------------------------------------------
// **That a model box is asked per face, and that asking changed nothing on its
// own.**
//
// These live here rather than in `Block.hpp` because `AxisFace` does: that
// header declares it opaquely to stay clear of the renderer's vertex format, so
// it can read the enumerators as the integers they are but cannot name them.
// This file sees both headers, and it is where the real reader sits.

/// The rule the three sites above replaced, kept **only** so the proofs below
/// have something that has to fail. `axis == 1` is true of +Y and -Y alike,
/// which is the whole defect in one line.
constexpr float axisOnlyLayer(const ModelBox& box, AxisFace face) {
    return faceAxis(face) == 1 ? box.lidLayer : box.sideLayer;
}

using BoxLayerRule = float (*)(const ModelBox&, AxisFace);

/// A box whose floor is not its lid, answered by whichever rule is handed in.
///
/// `kBoxFloorLecternPlinth` is a **shipping** row rather than one written for
/// the test, so this drives the real table through the real accessor. Its layer
/// is `kPlanksLayer`, which `Block.hpp` separately asserts against
/// `TextureLayer::Planks`, so the 9 below is not this table quoted back at
/// itself.
constexpr bool floorIsAskedSeparately(BoxLayerRule resolve) {
    ModelBox box{};
    box.lidLayer = 7.0f;
    box.sideLayer = 5.0f;
    box.floorOverride = kBoxFloorLecternPlinth;
    return resolve(box, AxisFace::PosY) == 7.0f && resolve(box, AxisFace::NegY) == kPlanksLayer &&
           resolve(box, AxisFace::PosX) != kPlanksLayer;
}

static_assert(floorIsAskedSeparately(boxFaceLayer),
              "a model box's floor is answering with its lid's layer");
static_assert(!floorIsAskedSeparately(axisOnlyLayer),
              "the axis-only rule this replaced now passes the test written to catch it, so the "
              "test has stopped measuring anything");

/// **A box that names no row answers exactly as it did before rows existed.**
///
/// This is what makes "nothing moved until a box's own data was edited" a
/// property of the encoding rather than something measured afterwards: -1 means
/// "the answer this face already gets", so a default box's lid and floor agree
/// and its four walls agree, rectangle, layer and turn alike.
constexpr bool defaultBoxIsInert() {
    ModelBox box{};
    box.uMin = 0.125f;
    box.vMin = 0.25f;
    box.uMax = 0.375f;
    box.vMax = 0.5f;
    box.topUMin = 0.625f;
    box.topVMin = 0.75f;
    box.topUMax = 0.875f;
    box.topVMax = 1.0f;
    box.sideLayer = 5.0f;
    box.lidLayer = 7.0f;
    box.lidTurns = 3;
    const FaceRect lid = boxFaceRect(box, AxisFace::PosY);
    const FaceRect floor = boxFaceRect(box, AxisFace::NegY);
    if (lid.uMin != floor.uMin || lid.vMin != floor.vMin || lid.uMax != floor.uMax ||
        lid.vMax != floor.vMax || lid.uMin != box.topUMin || lid.vMax != box.topVMax) {
        return false;
    }
    if (boxFaceLayer(box, AxisFace::PosY) != 7.0f || boxFaceLayer(box, AxisFace::NegY) != 7.0f) {
        return false;
    }
    if (boxFaceTurns(box, AxisFace::PosY) != 3 || boxFaceTurns(box, AxisFace::NegY) != 3) {
        return false;
    }
    constexpr AxisFace walls[]{AxisFace::PosX, AxisFace::NegX, AxisFace::PosZ, AxisFace::NegZ};
    for (const AxisFace wall : walls) {
        const FaceRect rect = boxFaceRect(box, wall);
        if (rect.uMin != box.uMin || rect.vMin != box.vMin || rect.uMax != box.uMax ||
            rect.vMax != box.vMax) {
            return false;
        }
        if (boxFaceLayer(box, wall) != 5.0f || boxFaceTurns(box, wall) != 0) {
            return false;
        }
    }
    return true;
}

static_assert(defaultBoxIsInert(),
              "adding the per-face rows moved a box that names none of them");

/// **A lid and a floor are turned separately**, on the two real models that
/// need it.
///
/// `lidTurns` was gated on the same `axis == 1` test as the rectangle and the
/// layer, so it says "the lid" and means both of them. `lectern.json` is the
/// case that proves the split is real rather than tidy: it puts
/// `"rotation": 180` on both of its `up` faces and on **neither** `down`, so
/// one number cannot answer for both and only the floor row can hold the
/// underside at zero.
constexpr bool turnsAreAskedPerFace(const ModelBox& box, unsigned char lid, unsigned char floor) {
    return boxFaceTurns(box, AxisFace::PosY) == lid && boxFaceTurns(box, AxisFace::NegY) == floor;
}

/// The control for it: the same box with its floor row taken away. If the
/// assert below still passed on this, the row would not be what is holding the
/// lectern's underside straight and the test would be proving nothing.
constexpr ModelBox withoutFloorRow(ModelBox box) {
    box.floorOverride = -1;
    return box;
}

static_assert(turnsAreAskedPerFace(postModel(BlockId::Lectern).boxes[0], 2, 0) &&
                  turnsAreAskedPerFace(postModel(BlockId::Lectern).boxes[2], 2, 0),
              "lectern.json turns both of its up faces half round and neither of its down faces");
static_assert(!turnsAreAskedPerFace(withoutFloorRow(postModel(BlockId::Lectern).boxes[0]), 2, 0) &&
                  !turnsAreAskedPerFace(withoutFloorRow(postModel(BlockId::Lectern).boxes[2]), 2, 0),
              "the lectern's floor rows are not what hold its undersides at zero turns, so the "
              "assert above is not testing what it says");

/// The anvil is the other half of the same reading, and the opposite answer:
/// `template_anvil.json` turns #0 and #3 half round on **both** faces, states
/// only the `up` of #1, and states neither face of the waist. Asserting the
/// waist stays at zero is what stops a future "turn every anvil box" edit from
/// looking correct.
static_assert(turnsAreAskedPerFace(postModel(BlockId::Anvil).boxes[0], 2, 2) &&
                  turnsAreAskedPerFace(postModel(BlockId::Anvil).boxes[3], 2, 2) &&
                  boxFaceTurns(postModel(BlockId::Anvil).boxes[1], AxisFace::PosY) == 2 &&
                  boxFaceTurns(postModel(BlockId::Anvil).boxes[2], AxisFace::PosY) == 0 &&
                  boxFaceTurns(postModel(BlockId::Anvil).boxes[2], AxisFace::NegY) == 0,
              "template_anvil.json's half turns are not where its JSON puts them");

/// Every one of the five is `180`, and that is the reason they could be taken
/// while the same two models' `90`s and `270`s could not: **half a turn is its
/// own inverse**, so it lands correctly whether our clockwise runs the same way
/// as the reference's or the other way. A quarter turn does not, and nothing
/// here has yet proved the two agree.
static_assert(boxFaceTurns(postModel(BlockId::Lectern).boxes[0], AxisFace::PosY) == 2 &&
                  boxFaceTurns(postModel(BlockId::Anvil).boxes[0], AxisFace::PosY) == 2,
              "a half turn stopped being a half turn");

using WallSlotRule = int (*)(AxisFace);

/// The slot order written backwards, so the proof below has something to
/// reject. Same technique as `FaceGeometry.hpp`'s reversed-row test: a check
/// that cannot fail is not a check.
constexpr int reversedWallSlot(AxisFace face) {
    const int slot = boxWallSlot(face);
    return slot < 0 ? slot : 3 - slot;
}

/// **That wall slot `i` is the direction the mesher samples at row `i`.**
///
/// Derived twice over from things that own the answer - `faceOutwardNormal`,
/// which `FaceGeometry.hpp` owns, and the `direction` column the texture lookup
/// actually reads - rather than from the order the slots happen to be written
/// in. A row of `kBoxWallFaces` in the wrong order would otherwise put a
/// campfire's embers on its outward face and its bark on the fire, and nothing
/// would say a word.
constexpr bool wallSlotsRunWithTheGeometry(WallSlotRule slotOf) {
    for (std::size_t i = 0; i < kFaces.size(); ++i) {
        const AxisFace face = static_cast<AxisFace>(i);
        const int slot = slotOf(face);
        if (faceAxis(face) == 1) {
            // A lid and a floor have no wall slot, and exactly one of them is
            // the floor.
            if (slot >= 0 || boxFaceIsFloor(face) == faceIsPositive(face)) {
                return false;
            }
            continue;
        }
        const glm::ivec3 normal = faceOutwardNormal(face);
        const int expected = normal.x > 0 ? 0 : (normal.x < 0 ? 1 : (normal.z > 0 ? 2 : 3));
        if (slot != expected || boxWallSlotDirection(slot) != kFaces[i].direction) {
            return false;
        }
    }
    return true;
}

static_assert(wallSlotsRunWithTheGeometry(boxWallSlot),
              "a wall override slot names a different face from the one the mesher draws there");
static_assert(!wallSlotsRunWithTheGeometry(reversedWallSlot),
              "the slot order reversed still satisfies the order test, so the test proves "
              "nothing about the order");

/// Every model box in the game, every face of it, through the same three
/// accessors the loop below calls.
///
/// What it can catch that reading cannot: a row constant left pointing past the
/// end of its table after an edit, a sentinel leaking out of `boxFaceLayer` as
/// a layer index, a rectangle stated backwards - which is a silent mirror - and
/// a turn outside the four the switch handles, which would fall to `default:`
/// and quietly not turn.
constexpr bool boxFacesAreSane(int pass);

/// Finer than the house 7 x 512 on purpose. Debug builds compile with
/// `_ITERATOR_DEBUG_LEVEL=2`, which bounds-checks every `std::array` subscript
/// and roughly triples the constexpr step count, and this sweep calls
/// `postModel` - which returns 684 bytes by value - once per id.
constexpr int kBoxFaceSweepStride = 128;

/// **Derived from the id count, not written down.** Changed 2026-08-19.
///
/// This constant is the reason the change is worth making. It used to read 27
/// with a comment explaining that 26 passes covered 3328 ids against a count of
/// 3285, and that the twenty-four ids the bee nest added had cut the headroom
/// to nineteen - so a hand-written number had already gone stale once tonight,
/// and the repair was a hand-written safety margin, which is the same thing
/// again one step later.
///
/// A ceiling division cannot go stale. It also makes the deliberate spare pass
/// unnecessary: there is nothing left to trip, so the margin that existed to
/// absorb the next family can go. Whether that lands on 26 or 27 today depends
/// on a count in `Block.hpp` that changed twice this morning, and the point of
/// this form is that this file no longer has to know which.
///
/// This is the same idiom as `kNameSweepPasses` in `Block.hpp` and
/// `kDropModelSweepPasses` in `ItemEntity.cpp`. Search `*SweepPasses` for the
/// full set rather than trusting this list.
constexpr int kBoxFaceSweepPasses =
    (static_cast<int>(kBlockIdCount) + kBoxFaceSweepStride - 1) / kBoxFaceSweepStride;

constexpr bool boxFacesAreSane(int pass) {
    const int first = pass * kBoxFaceSweepStride;
    const int end = first + kBoxFaceSweepStride;
    for (int raw = first; raw < end && raw < static_cast<int>(kBlockIdCount); ++raw) {
        const ModelBoxes model = postModel(static_cast<BlockId>(raw));
        for (int b = 0; b < model.count; ++b) {
            const ModelBox& box = model.boxes[b];
            if (box.floorOverride >= kBoxFloorFaceCount ||
                box.wallOverride >= kBoxWallFaceCount) {
                return false;
            }
            for (std::size_t f = 0; f < kFaces.size(); ++f) {
                const AxisFace face = static_cast<AxisFace>(f);
                if (boxFaceLayer(box, face) < -1.0f || boxFaceTurns(box, face) > 3) {
                    return false;
                }
                const FaceRect rect = boxFaceRect(box, face);
                if (rect.uMax < rect.uMin || rect.vMax < rect.vMin) {
                    return false;
                }
            }
        }
    }
    return true;
}

template <int Pass>
struct BoxFaceSweep {
    static_assert(boxFacesAreSane(Pass),
                  "a model box names an override row that is not there, or resolves a face to a "
                  "layer, rectangle or turn nothing downstream can use");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyBoxFacePassSwept(std::integer_sequence<int, Pass...>) {
    return (BoxFaceSweep<Pass>::swept && ...);
}

static_assert(everyBoxFacePassSwept(std::make_integer_sequence<int, kBoxFaceSweepPasses>{}),
              "the failing BoxFaceSweep instantiation above names which stride");
static_assert(kBoxFaceSweepPasses * kBoxFaceSweepStride >= static_cast<int>(kBlockIdCount),
              "the box face sweep no longer covers every block id - the count above is a ceiling "
              "division, so this is arithmetic rather than a stale constant; do not raise it");

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

    // **Six passes, one per direction, and each finishes before the next
    // begins.** That was already true; recording where each one started and
    // stopped is the whole of what `translucentFacings` and `translucentShaped`
    // add - **they observe this loop and change nothing inside it**, so the
    // ranges cost no geometry and cannot move a vertex.
    //
    // **That is a claim about the ranges, not about the loop.** The
    // `sameFluidAs` rule further down did change what is emitted, at every
    // water-lava boundary, and index buffers there are different as a result -
    // deliberately, because they used to have a hole in them. Do not read this
    // paragraph as "the mesher was not touched this round".
    for (std::size_t facing = 0; facing < kFaces.size(); ++facing) {
        const Face& face = kFaces[facing];
        const auto facingFirst = static_cast<std::uint32_t>(result.translucent.indices.size());
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
                    // **Two panes of the same glass drop the boundary between
                    // them entirely**, from both sides, unlike a cutout - which
                    // keeps it so you can still see leaves through the holes in
                    // the leaves in front. There is nothing to see through here:
                    // the boundary is invisible in the reference and all it
                    // would contribute is a second helping of tint, so a wall
                    // two blocks thick would read darker than one.
                    const bool blendedInterior = isTranslucent(block) && ahead == block;
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
                    //
                    // **And "the neighbour is a fluid" has to mean *my* fluid.**
                    // The level comparison says "I draw the boundary my thinner
                    // neighbour's lower surface would leave a gap in", which is
                    // a statement about two cells of one fluid. Water beside
                    // lava at the same level answered `0 > 0` from *both* sides,
                    // so neither drew the shared face and there was a hole
                    // straight through the world where they met.
                    //
                    // A different fluid is then judged like any other
                    // neighbour, and **no fluid occludes anything**: both are
                    // `BlockShape::Empty`, so `occludesFace` reaches its
                    // `default` and answers false at every offset. *Measured
                    // rather than assumed* - `isOpaque(Lava0)` reads as though
                    // a cube of lava were solid and it is false, which is the
                    // reading this comment previously had to correct. So both
                    // sides emit the shared quad, into different meshes: lava
                    // is not translucent and goes to the opaque pass, water is
                    // and goes to the blended one. Which of the two coplanar
                    // faces survives is the depth test's business and the
                    // renderer's; the hole is closed either way, because the
                    // lava face alone is enough to fill it.
                    const bool sameFluid = sameFluidAs(block, ahead);
                    const bool visible =
                        isFluid(block)
                            ? ((!sameFluid &&
                                !occludesFace(ahead, face.neighbourOffset.y)) ||
                               (sameFluid && ahead != block &&
                                face.neighbourOffset.y <= 0 &&
                                fluidLevel(ahead) > fluidLevel(block)))
                            : (!occludesFace(ahead, face.neighbourOffset.y) && !cutoutInterior &&
                               !blendedInterior);
                    if (!visible) {
                        continue;
                    }

                    sample.layer = blockTextureLayer(block, face.facing, face.direction,
                                                     volume.chestHalfAt(p.x, p.y, p.z));
                    sample.translucent = isTranslucent(block);
                    // **Never both.** A double-sided quad rasterises from either
                    // side, so blending one would lay the same tint down twice
                    // and a single pane would read as two.
                    sample.doubleSided = isCutout(block) && !sample.translucent;
                    sample.alpha = sample.translucent ? translucentAlpha(block) : 1.0f;
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

        result.translucentFacings[facing] =
            IndexRange{facingFirst,
                       static_cast<std::uint32_t>(result.translucent.indices.size()) - facingFirst};
    }

    // Shapes that are not unit cubes, one block at a time and never merged.
    //
    // **Everything from here on is the `translucentShaped` tail.** This pass
    // emits a whole block's faces together, so a run of its indices spans
    // several directions at once and cannot join the six above. Recorded
    // separately rather than left unaccounted for: a reader that took the six
    // ranges to cover the buffer would stop drawing stained panes entirely.
    const auto shapedFirst = static_cast<std::uint32_t>(result.translucent.indices.size());
    const auto lightOf = [&](const glm::ivec3& cell, float& sky, float& blockLight) {
        const std::uint8_t packed = volume.lightAt(cell.x, cell.y, cell.z);
        sky = lightCurve(static_cast<float>(packed >> 4));
        blockLight = lightCurve(static_cast<float>(packed & 0x0F));
    };

    const auto pushQuad = [&](const std::array<glm::vec3, 4>& corners, const std::array<glm::vec2, 4>& uvs,
                              float layer, float sky, float blockLight, float shading,
                              std::uint32_t normalCode, bool doubleSided, bool sways = false,
                              std::uint32_t rootHalfBlocks = 0, bool translucent = false) {
        // Defaulted, so every shape but a stained pane emits exactly the quad it
        // did before. A pane is the one thing in this pass that has to be sorted
        // behind the opaque world, because it wears a blended block's own art -
        // and that art carries its own alpha, so the vertex still says 1.
        engine::MeshData& mesh = translucent ? result.translucent : result.opaque;
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

                if (isRail(block)) {
                    // **A rail is one plane, not a box.** `rail_flat.json` is a
                    // zero-thickness element at y = 1 with an `up` face and a
                    // `down` face, and `template_rail_raised_ne.json` is that
                    // same plane tilted - so both are one double-sided quad here
                    // and the reference's two faces come out of `doubleSided`
                    // rather than out of two emissions.
                    //
                    // **This is why the box path could not draw it.** Boxes are
                    // axis-aligned, so an ascending rail came out as a flat
                    // sheet lying on the floor of its own cell: a player who
                    // built a rail ramp saw a flight of separate flat rails on
                    // steps rather than a continuous incline. And a box has no
                    // way to turn its texture, so north-south and east-west were
                    // pixel-identical and rail direction was invisible.
                    //
                    // Every fact this needs is asked for rather than restated:
                    // `railShape` decodes the id, `railCornerY` owns which edge
                    // is raised, and `railUvTurns` owns the turn.
                    lightOf({x, y, z}, sky, blockLight);
                    const int railShapeIndex = railShape(block);
                    const float layer = blockTextureLayer(block, BlockFace::Top);
                    const auto turns = static_cast<std::size_t>(railUvTurns(railShapeIndex));

                    // The lid's own corner order, so a rail's texture lies the
                    // same way round as every other top face in the world - the
                    // one table, rather than a fifth copy of it.
                    constexpr auto lid = static_cast<std::size_t>(AxisFace::PosY);
                    std::array<glm::vec3, 4> corners{};
                    std::array<glm::vec2, 4> uvs{};
                    for (std::size_t c = 0; c < 4; ++c) {
                        const int cornerX = kFaceCornerBits[lid][c][0];
                        const int cornerZ = kFaceCornerBits[lid][c][2];
                        corners[c] = cellOrigin + glm::vec3{static_cast<float>(cornerX),
                                                            railCornerY(railShapeIndex, cornerX, cornerZ),
                                                            static_cast<float>(cornerZ)};
                        // Turning which uv lands on which corner turns the
                        // picture, and the four uvs are a cycle, so a quarter
                        // turn is a step round it.
                        uvs[c] = kBottomLeftWinding[(c + turns) & std::size_t{3}];
                    }

                    // A ramp is at 45 degrees like a plant blade, so it has no
                    // axis to name; a flat rail is a lid and takes the lid's.
                    pushQuad(corners, uvs, layer, sky, blockLight, faceShade(AxisFace::PosY),
                             railSlopes(railShapeIndex) ? engine::kNormalUnaligned
                                                        : faceNormalCode(AxisFace::PosY),
                             true);
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
                // A *model* rather than a shape cut out of a cube, so each box
                // names the rectangle of texture it samples instead of taking
                // it from where the box happens to sit.
                ModelBoxes model;
                if (shape == BlockShape::Model || shape == BlockShape::Cocoa ||
                    shape == BlockShape::Bed) {
                    model = postModel(block);
                }
                // **`drawnBoxes` owns which boxes these are**, so the crack
                // overlay and anything else that draws onto a block gets the
                // same answer this does rather than a second copy of it. The
                // two facts an id cannot carry are supplied here: what a fence
                // reaches toward, and whether a vine has a ceiling to hang from.
                const BlockShape connectShape = shape;
                const std::uint8_t connections =
                    connectsToNeighbours(connectShape)
                        ? connectionBits(connectShape, volume.blockAt(x, y, z - 1),
                                         volume.blockAt(x, y, z + 1), volume.blockAt(x - 1, y, z),
                                         volume.blockAt(x + 1, y, z))
                        : std::uint8_t{0};
                const BlockBoxes shapeBoxes =
                    drawnBoxes(block, connections, isOpaque(volume.blockAt(x, y + 1, z)));

                for (int i = 0; i < shapeBoxes.count; ++i) {
                    const BlockBox& b = shapeBoxes.boxes[i];
                    const glm::vec3 lo{b.minX, b.minY, b.minZ};
                    const glm::vec3 hi{b.maxX, b.maxY, b.maxZ};

                    for (std::size_t f = 0; f < kFaces.size(); ++f) {
                        const Face& face = kFaces[f];
                        // **The name of the face, not merely its axis.** A
                        // `ModelBox` answers per face, and until it could, one
                        // answer served +Y and -Y together and one served all
                        // four walls. `cornersSitOnTheFaceSampled` above proves
                        // row `f` of this table is the `AxisFace` of the same
                        // number, so this cast is derived rather than assumed.
                        const AxisFace axisFace = static_cast<AxisFace>(f);
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
                        // its net than its walls - and its floor and each of
                        // its four walls from different corners again, which is
                        // what `boxFaceRect` is asked rather than told.
                        float uLow = 0.0f;
                        float uHigh = 1.0f;
                        float vLow = 0.0f;
                        float vHigh = 1.0f;
                        if (model.count > 0) {
                            const FaceRect rect = boxFaceRect(model.boxes[i], axisFace);
                            uLow = rect.uMin;
                            uHigh = rect.uMax;
                            vLow = rect.vMin;
                            vHigh = rect.vMax;
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
                                // A face may be turned, which is how a model
                                // says which way round a texture goes when the
                                // block itself cannot be rotated. **This was
                                // gated on the same axis test as the rectangle
                                // and the layer**, so a floor wore its lid's
                                // quarter turn; splitting those two and leaving
                                // this one would have put the right picture on
                                // a repeater's belly at the wrong angle.
                                // `boxFaceTurns` answers 0 for every wall no
                                // box has stated one for, which is all of them
                                // today.
                                switch (boxFaceTurns(model.boxes[i], axisFace)) {
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
                                 // gold bell rather than gold throughout. It
                                 // names it **per face**: the fallback below
                                 // has always told Top from Bottom and named
                                 // all four walls, and for twenty milestones
                                 // only the override on top of it was coarser
                                 // than that.
                                 [&] {
                                     if (model.count > 0) {
                                         const float own = boxFaceLayer(model.boxes[i], axisFace);
                                         if (own >= 0.0f) {
                                             return own;
                                         }
                                     }
                                     return blockTextureLayer(block, face.facing, face.direction);
                                 }(),
                                 sky, blockLight, face.shade,
                                 shape == BlockShape::Vine ? engine::kNormalUnaligned : face.normalCode,
                                 shape == BlockShape::Vine, false, 0u, isTranslucent(block));
                    }
                }
            }
        }
    }

    result.translucentShaped =
        IndexRange{shapedFirst,
                   static_cast<std::uint32_t>(result.translucent.indices.size()) - shapedFirst};

    return result;
}

} // namespace game
