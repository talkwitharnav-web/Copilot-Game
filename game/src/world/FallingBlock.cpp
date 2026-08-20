#include "world/FallingBlock.hpp"

#include "item/Item.hpp"
#include "world/FaceGeometry.hpp"
#include "world/FaceShading.hpp"
#include "world/Tick.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace game {
namespace {

/// The reference's falling-block gravity is 0.04 blocks per tick squared with a
/// 0.98 drag applied *after* it, which is 16 blocks/s^2 and a terminal speed of
/// 1.96 blocks/tick. Both are quoted per second here because nothing else in
/// this codebase counts in ticks.
///
/// Worth knowing: this is **half** the player's gravity. A falling block is
/// noticeably lazier than a falling player, and matching the player's 32 makes
/// sand look like it is being sucked down.
constexpr float kGravity = 16.0f;
constexpr float kTerminalVelocity = 39.2f;

// Both numbers are the reference's per-tick figures multiplied by the published
// twenty ticks a second, and that conversion is the single thing most likely to
// be undone by a well-meaning edit. **The edit that makes these fail is writing
// the reference's own 0.04 and 1.96 straight into the constants above** - which
// compiles, runs, and makes sand fall four hundred times too slowly under a
// comment saying it matches.
static_assert(kGravity > 15.99f && kGravity < 16.01f,
              "0.04 blocks per tick squared at twenty ticks a second is 16 m/s^2");
static_assert(kTerminalVelocity > 39.19f && kTerminalVelocity < 39.21f,
              "1.96 blocks per tick at twenty ticks a second is 39.2 m/s");

/// A single frame may not advance a falling cube further than this. **The same
/// 0.05 the player, the drops and the creatures already clamp to**, and
/// deliberately not a new number: they all resolve against one world under one
/// set of assumptions, so a frame too long for one is too long for all of them.
///
/// This was the one mover taking raw wall time. At 39.2 m/s a one-second hitch
/// - alt-tabbing, a chunk-streaming stall, dragging the window, which blocks
/// `pollEvents` for its whole duration - carried a cube thirty-nine blocks in a
/// single frame, straight past everything it should have crushed on the way.
constexpr float kMaxDeltaSeconds = 0.05f;

/// **It is one tick, and until now nothing here said so.** The paragraph above
/// states the coupling in prose - "a frame too long for one is too long for all
/// of them" - and nothing enforced it, which is the remedy ladder's third rung
/// sitting where its second is available. Copied verbatim from `Player.cpp` and
/// `Creature.cpp` so all three read identically.
///
/// The first half pins the clamp to the one owner (`Tick.hpp`), so it follows a
/// tick-rate change instead of being left behind. The second half is the
/// **absolute anchor**, and it is the half that does the work: both sides of
/// the first are derived, so a coherent tick-rate move would satisfy it on its
/// own, and this clamp is not free to move - it is the only thing bounding how
/// far a cube travels at `kTerminalVelocity` in a single frame.
static_assert(kMaxDeltaSeconds == tick::kSeconds && kMaxDeltaSeconds == 0.05f,
              "a frame may advance a falling cube by at most one simulation tick, and that tick "
              "is 50 ms - the same clamp the player, the drops and the creatures use");

// **And that bound is almost two cells, not one.** `kTerminalVelocity *
// kMaxDeltaSeconds` is 1.96 m, so a cube at terminal speed crosses two whole
// cell boundaries in a single frame. This is the premise `fallingCubeRest`
// exists for - it walks every cell in `[toCell, fromCell]` instead of testing
// where the cube lands - and the header's note above that function understated
// this distance four times over until 2026-08-19, at a value below one cell,
// so it read as an argument that the walk is redundant.
//
// The bound rather than the exact figure is asserted on purpose: 39.2 is
// already pinned twenty lines up, and repeating it here would be a second copy
// of one number. What is stated here is the independent thing - that a frame
// crosses more than one cell - which is what makes the walk necessary.
static_assert(kTerminalVelocity * kMaxDeltaSeconds > 1.0f,
              "a clamped frame at terminal speed no longer crosses a whole cell, so the note "
              "above `fallingCubeRest` in the header - which tells the reader the walk is what "
              "stops a cube falling through a one-block floor - no longer describes this build; "
              "re-read it before simplifying that walk into a destination test");

/// Below this the cube is close enough to its resting cell to snap into it
/// without the last fraction of a block reading as a hover.
constexpr float kSettleEpsilon = 0.001f;

// **What a displaced cell is worth is the drop table's question, not this
// file's**, and the assert is what lets the landing below stop answering it.
// The crush test used to read `!= Air && !isWater`, which is `isReplaceable`
// minus {Air, water} - so lava and fire, which a falling block genuinely does
// come to rest *in*, were reported as crushed while water was not. Water was
// written down and lava was not: one rule, correct in one of the two places
// that needed it, twenty-two lines apart inside one function.
//
// **The single edit that makes this fail is taking the `isFluid || Fire` row
// out of `dropForBlock`** (`Item.hpp`), which is the one thing standing between
// a displaced fluid and an item for a block `isCanonicalBlockItem` says cannot
// be held. Both sides are `constexpr`, so it cannot rot.
static_assert(dropForBlock(BlockId::Water0) == ItemId::None &&
                  dropForBlock(BlockId::Lava0) == ItemId::None &&
                  dropForBlock(BlockId::Fire) == ItemId::None,
              "a landing reports everything it displaced, so the drop table must stay the one "
              "place that knows a fluid and a fire are worth nothing");

/// **How long a cube is allowed to stay in the air before it is given up on.**
///
/// `RESEARCH.md` 9.2 and <https://minecraft.wiki/w/Falling_Block> (Behavior):
/// *"a falling block that has existed for more than 600 ticks (30 seconds)
/// destroys itself and drops as an item"*. Measured in seconds because nothing
/// else in this file counts in ticks; the conversion is the published twenty
/// ticks a second, and it is the thing most likely to be undone by a
/// well-meaning edit.
constexpr float kMaxAirborneSeconds = 30.0f;

static_assert(kMaxAirborneSeconds > 29.99f && kMaxAirborneSeconds < 30.01f,
              "600 ticks at twenty ticks a second is 30 seconds");

// ---------------------------------------------------------------------------
// Sweeps over every block id. **Strided at 7 x 512** for MSVC's constexpr step
// budget, which is the house pattern; Debug's `_ITERATOR_DEBUG_LEVEL=2` roughly
// triples the count, and both presets are built before this is believed.
// ---------------------------------------------------------------------------

constexpr int kFallSweepStride = 512;

/// **Derived, so it cannot go stale.** Changed 2026-08-19. It read 7, which was
/// a correct measurement of `kBlockIdCount` on the day it was written and a
/// hostage to it afterwards - the identical constant in `ChunkMesher.cpp` had
/// already been overtaken once tonight by the twenty-four ids the bee nest
/// added. Same ceiling division as `kNameSweepPasses` in `Block.hpp`; search
/// `*SweepPasses` for the whole family rather than trusting a list here.
constexpr int kFallSweepPasses =
    (static_cast<int>(kBlockIdCount) + kFallSweepStride - 1) / kFallSweepStride;

/// Kept as a proof rather than a guard: with the ceiling division above this
/// can no longer fail, so it now states that the derivation means what it says.
static_assert(kFallSweepStride * kFallSweepPasses >= static_cast<int>(kBlockIdCount),
              "the strided sweeps below stop short of the last block id");

/// **That `buildMesh` has a path for everything gravity moves.**
///
/// Three paths exist: a whole-cell cube, `postModel` box for box, and the two
/// crossed blades a plant is drawn with. A falling slab, stair or fence would
/// take the first and be drawn as a solid cube in mid-air, which is exactly the
/// defect the model path was added to fix - so this couples `isFalling` (which
/// lives in `Block.hpp` and can grow without anyone opening this file) to what
/// this file can actually draw.
///
/// `Hovering` is the primed charge: a full cube lifted off the floor, and the
/// lift is a fact about one *resting* on the ground rather than about one in
/// the air, so the whole-cell path is right for it.
///
/// !! **`isFalling` is not the only door into `spawn`, and this assert used to
/// believe it was.** `World` detaches an unsupported stalactite and `Main.cpp`
/// hands it straight to `spawn`, and `isFalling(PointedDripstone)` is *false* -
/// so the one coupling written to stop a faller being drawn wrong had a hole
/// exactly the size of the newest thing that falls, and the stalactite came
/// down as a plain stone cube. The sweep below cannot see that door, so the
/// blocks that come through it are named underneath it by hand. **Anything else
/// that reaches `spawn` without being `isFalling` has to be added there.**
constexpr bool canBeDrawnFalling(BlockShape shape) {
    return usesModelIcon(shape) || shape == BlockShape::Full || shape == BlockShape::Hovering ||
           shape == BlockShape::Cross;
}

constexpr bool everyFallerCanBeDrawn(int pass) {
    const int first = pass * kFallSweepStride;
    for (int i = first; i < first + kFallSweepStride && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (!isFalling(id)) {
            continue;
        }
        if (canBeDrawnFalling(blockShape(id))) {
            continue;
        }
        return false;
    }
    return true;
}

static_assert(everyFallerCanBeDrawn(0) && everyFallerCanBeDrawn(1) && everyFallerCanBeDrawn(2) &&
                  everyFallerCanBeDrawn(3) && everyFallerCanBeDrawn(4) &&
                  everyFallerCanBeDrawn(5) && everyFallerCanBeDrawn(6),
              "something that falls is neither a whole cube nor on the model path, so it would be "
              "drawn as a plain cube in mid-air and snap to its real shape on landing");

// **The door the sweep above cannot see.** These reach `spawn` without being
// `isFalling`, so nothing couples them to the drawing arms except this line.
// `World::updateDripstone` detaches an unsupported stalactite into
// `m_detachedBlocks`, and `Main.cpp` drains that straight into `spawn`.
static_assert(canBeDrawnFalling(blockShape(BlockId::PointedDripstone)),
              "a detached stalactite reaches spawn() and this file cannot draw its shape");

// The negative twin: being a shape is not what makes it drawable here, so a
// future reader cannot satisfy the line above by widening the predicate to
// everything. `Slab` is the case the doc comment above names - it would fall as
// a solid cube - and it must stay rejected.
static_assert(blockShape(BlockId::PointedDripstone) == BlockShape::Cross &&
                  !canBeDrawnFalling(BlockShape::Slab) &&
                  !canBeDrawnFalling(BlockShape::Stairs) && !canBeDrawnFalling(BlockShape::Fence),
              "the stalactite is drawn by the cross arm, and canBeDrawnFalling is not a tautology");

/// **That nothing gravity moves can be destroyed by breaking on landing.**
///
/// The landing hands a cube that may not become a block again straight to the
/// drop table, so an id with no item there is a block that falls onto a torch
/// and ceases to exist. `dropForBlock` is a proxy for the full `resolveBreak`
/// path - it is the same proxy the fluid assert above uses - and it is the half
/// that can go to `ItemId::None`.
constexpr bool everyFallerIsWorthAnItem(int pass) {
    const int first = pass * kFallSweepStride;
    for (int i = first; i < first + kFallSweepStride && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (isFalling(id) && dropForBlock(id) == ItemId::None) {
            return false;
        }
    }
    return true;
}

static_assert(everyFallerIsWorthAnItem(0) && everyFallerIsWorthAnItem(1) &&
                  everyFallerIsWorthAnItem(2) && everyFallerIsWorthAnItem(3) &&
                  everyFallerIsWorthAnItem(4) && everyFallerIsWorthAnItem(5) &&
                  everyFallerIsWorthAnItem(6),
              "a block that falls is worth nothing as an item, so one that lands on a torch or a "
              "slab is deleted rather than dropped");

/// Which of `BlockFace`'s three a direction is. Derived from the direction, not
/// tabulated, so it cannot drift from the corner table it is drawn beside.
constexpr BlockFace blockFaceOf(AxisFace face) {
    if (faceAxis(face) != 1) {
        return BlockFace::Side;
    }
    return faceIsPositive(face) ? BlockFace::Top : BlockFace::Bottom;
}

/// Which way a wall points, so `blockTextureLayer` can decide whether it is
/// looking at the block's front. **The lids get `Unknown`, exactly as the
/// mesher's table has it**, because a top has no facing to compare.
///
/// No `default:` here on purpose: C4062 is off at `/W4`, so a missing
/// enumerator would not warn, and a `default:` that returns a real value would
/// hide it as well. Every enumerator is named instead.
constexpr FaceDirection faceDirectionOf(AxisFace face) {
    switch (face) {
    case AxisFace::PosX:
        return FaceDirection::PosX;
    case AxisFace::NegX:
        return FaceDirection::NegX;
    case AxisFace::PosZ:
        return FaceDirection::PosZ;
    case AxisFace::NegZ:
        return FaceDirection::NegZ;
    case AxisFace::PosY:
    case AxisFace::NegY:
    case AxisFace::Count:
        break;
    }
    return FaceDirection::Unknown;
}

/// **What the corner table says a face is**, read off `kFaceCornerBits` and
/// nothing else: the face whose four corners all sit at `y = 1` is the top, the
/// one at `y = 0` is the bottom, and a face with a constant `x` or `z` is the
/// wall pointing that way. A lid's `x` and `z` both vary, so it falls through
/// to `Unknown`, which is what the mesher's own table gives it.
///
/// This is the *independent* authority the two rules below are held against.
/// It has to be: `blockFaceOf` and `faceDirectionOf` are both derived from
/// `faceAxis` and `faceIsPositive`, so checking either against those two proves
/// only that `!=` works. Two of the four clauses this replaced did exactly
/// that - `lid != (blockFaceOf(face) != BlockFace::Side)` reduces to `lid !=
/// lid` - and passed on every face for the same reason `skyTransparencyImplies-
/// Light` passed earlier in the session. Substituting the definition into the
/// expression is the test that finds them, and it takes a minute.
constexpr FaceDirection directionFromCorners(FaceCorners corners) {
    bool allHigh[3]{true, true, true};
    bool allLow[3]{true, true, true};
    for (int c = 0; c < 4; ++c) {
        for (int axis = 0; axis < 3; ++axis) {
            if (corners[c][axis] != 1) {
                allHigh[axis] = false;
            }
            if (corners[c][axis] != 0) {
                allLow[axis] = false;
            }
        }
    }
    if (allHigh[0]) {
        return FaceDirection::PosX;
    }
    if (allLow[0]) {
        return FaceDirection::NegX;
    }
    if (allHigh[2]) {
        return FaceDirection::PosZ;
    }
    if (allLow[2]) {
        return FaceDirection::NegZ;
    }
    return FaceDirection::Unknown;
}

constexpr BlockFace blockFaceFromCorners(FaceCorners corners) {
    int high = 0;
    for (int c = 0; c < 4; ++c) {
        high += corners[c][1];
    }
    return high == 4 ? BlockFace::Top : high == 0 ? BlockFace::Bottom : BlockFace::Side;
}

/// That each face is told the direction and the `BlockFace` its own *corners*
/// say it is.
///
/// **Both rules are parameters**, which is the whole point: a proof that can
/// only be aimed at the right answer cannot be shown to fail on a wrong one,
/// and `FaceGeometry.hpp` earns its keep by taking its corner rows the same
/// way. The three wrong rules below are the ones this project has actually
/// shipped, not invented failures.
constexpr bool faceRolesMatchTheCorners(FaceDirection (*direction)(AxisFace),
                                        BlockFace (*facing)(AxisFace)) {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        const auto face = static_cast<AxisFace>(i);
        if (direction(face) != directionFromCorners(cornersOf(face))) {
            return false;
        }
        if (facing(face) != blockFaceFromCorners(cornersOf(face))) {
            return false;
        }
    }
    return true;
}

static_assert(faceRolesMatchTheCorners(faceDirectionOf, blockFaceOf),
              "a falling block's face is told a direction or a BlockFace its own corners "
              "contradict");

/// **The rules this file has been wrong with before, each rejected.**
///
/// `everyWallFacesPosX` is the 2026-08-11 bug verbatim: `blockTextureLayer`
/// answers "front" for whichever direction matches the block's facing, so four
/// walls handed one direction wear the front four times, which is how a sticky
/// piston wore its plate right round the block on the drop path.
/// `everythingIsASide` is the drop's other half - one side layer on all six -
/// and `everyLidIsATop` is the icon's, which painted the top texture on the
/// underside because it never had a `Bottom` to give.
constexpr FaceDirection everyWallFacesPosX(AxisFace face) {
    return faceAxis(face) == 1 ? FaceDirection::Unknown : FaceDirection::PosX;
}

constexpr BlockFace everythingIsASide(AxisFace) { return BlockFace::Side; }

constexpr BlockFace everyLidIsATop(AxisFace face) {
    return faceAxis(face) == 1 ? BlockFace::Top : BlockFace::Side;
}

static_assert(!faceRolesMatchTheCorners(everyWallFacesPosX, blockFaceOf) &&
                  !faceRolesMatchTheCorners(faceDirectionOf, everythingIsASide) &&
                  !faceRolesMatchTheCorners(faceDirectionOf, everyLidIsATop),
              "the proof above accepts rules this project has already shipped as bugs, so it "
              "would have passed on the bugs it exists to catch");

// ---------------------------------------------------------------------------
// The texture rectangle, and the proof that it reads the way `FaceGeometry.hpp`
// says it does.
// ---------------------------------------------------------------------------

/// A rectangle laid across a face's four corners in `kBottomLeftWinding`'s
/// order: **u runs min, max, max, min and v runs max, max, min, min**, because
/// `FaceGeometry.hpp`'s v grows downward and `v = 0` is the top row of the
/// image. Written once so the whole-cell case and the model case cannot come
/// out of two different orders.
constexpr std::array<glm::vec2, 4> rectAcross(float uMin, float vMin, float uMax, float vMax) {
    return {glm::vec2{uMin, vMax}, glm::vec2{uMax, vMax}, glm::vec2{uMax, vMin},
            glm::vec2{uMin, vMin}};
}

constexpr bool sameRect(const std::array<glm::vec2, 4>& a, const std::array<glm::vec2, 4>& b) {
    for (int i = 0; i < 4; ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y) {
            return false;
        }
    }
    return true;
}

/// **That a whole-cell rectangle built by `rectAcross` is exactly the shared
/// winding**, so the model path and the cube path cannot disagree about which
/// way up a texture goes - and, feeding it back reversed, **that the comparison
/// rejects one that does not**.
///
/// The reversal is not a hypothetical: this file's corner rows *were* the
/// mesher's rows reversed, which is a pure V-flip on all 24 corners, and it
/// survived four milestones because sand is nearly symmetric. The positive half
/// of this proof passes on the reversed rect as readily as on the right one if
/// it is written as "compare the table with itself", which is the whole
/// argument for keeping the negative half beside it.
constexpr bool rectReadsAsTheSharedWindingDoes() {
    const std::array<glm::vec2, 4> whole = rectAcross(0.0f, 0.0f, 1.0f, 1.0f);
    const std::array<glm::vec2, 4> reversed{whole[3], whole[2], whole[1], whole[0]};
    return sameRect(whole, kBottomLeftWinding) && !sameRect(reversed, kBottomLeftWinding);
}

static_assert(rectReadsAsTheSharedWindingDoes(),
              "a model box's texture rectangle no longer runs the way FaceGeometry.hpp's does - "
              "reversed, it is a V-flip, which turns every falling block upside-down");

/// The whole cell, for everything that is not built out of model boxes.
constexpr BlockBox kWholeCell{0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};

/// **That the box arm costs the cube arm nothing.** `buildMesh` picks a corner
/// with `bit ? box.max : box.min`, so a whole-cell box has to reproduce
/// `faceCorners` exactly - otherwise adding the model path for the anvil would
/// have moved sand, gravel and sixteen concrete powders as well, which is the
/// silent half of this kind of change.
constexpr bool wholeCellBoxIsTheUnitCube() {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        const auto face = static_cast<AxisFace>(i);
        const std::array<glm::vec3, 4> corners = faceCorners(face);
        for (int c = 0; c < 4; ++c) {
            const glm::vec3 picked{corners[c].x != 0.0f ? kWholeCell.maxX : kWholeCell.minX,
                                   corners[c].y != 0.0f ? kWholeCell.maxY : kWholeCell.minY,
                                   corners[c].z != 0.0f ? kWholeCell.maxZ : kWholeCell.minZ};
            if (picked.x != corners[c].x || picked.y != corners[c].y || picked.z != corners[c].z) {
                return false;
            }
        }
    }
    return true;
}

static_assert(wholeCellBoxIsTheUnitCube(),
              "a falling cube is no longer drawn on the corners FaceGeometry.hpp gives it - the "
              "box arm has moved every block that is not a model");

// ---------------------------------------------------------------------------
// Per-face model boxes. `Block.hpp` owns the answers; this is how they reach a
// cube in mid-air.
// ---------------------------------------------------------------------------

/// Which world axes U and V run along for a face - **owned by
/// `FaceGeometry.hpp`, not restated here.**
///
/// This file kept a byte-identical copy of both one-liners until 2026-08-19,
/// and being correct is what made the copy dangerous: nothing bound it to the
/// header's pair. `ChunkMesher.cpp`'s `kFaces` states them in two columns -
/// `+X` and `-X` get `u = Z, v = Y`, the two lids get `u = X, v = Z`, and `+Z`
/// and `-Z` get `u = X, v = Y` - and that is the only assignment for which an
/// unturned rectangle comes out as `rectAcross`, which is what
/// `turnsAgreeWithTheMesher` pins and `swappedAxesAreRejected` shows failing.
/// Derived rather than tabulated, in one place, so a seventh face is impossible
/// and a correction cannot land in only half the files that ask.

/// **Whether a quarter turn runs forward or backward through the four uv
/// slots.** A turn is stated in the *texture's* space, and a face's uv
/// parameterisation is a mirror of its neighbour's on half the cube, so on
/// those faces the same turn walks the slots the other way.
///
/// The rule is `flipU == flipV`, read off the shared corner table exactly as
/// the mesher reads it. **It is emphatically not `faceIsPositive`**, which
/// agrees on four faces and is wrong on `+Z` and `-Z` - see
/// `theWrongTurnRuleIsRejected`. Nothing is drawn wrong today because no wall
/// carries a turn yet (measured: 268 turn-bearing faces, every one a lid or a
/// floor, 0 walls), and `Block.hpp` says two boxes want one. This is the shape
/// where a proxy that happens to be right today is worse than no rule at all.
///
/// **The answer comes from `FaceGeometry.hpp`, and this file no longer claims a
/// second route to it.** What stood here until 2026-08-19 was a
/// `turnRuleFromFloatCorners` that read the *float* corners against
/// `kBottomLeftWinding`, asserted to agree with the header's integer
/// `turnRunsWithTheSlots`, over a message reading *"one of the two tables has
/// moved without the other"*.
///
/// **It was a tautology, and it is worth spelling out why, because it read as
/// the strongest proof in the file.** `faceCorners(f)[c][a]` is
/// `float(kFaceCornerBits[f][c][a])`, and `kBottomLeftWinding[0].x/.y` are
/// `float(kWindingU[0])`/`float(kWindingV[0])` - so the float route reduced to
/// `(float(bits) != float(windingU)) == (float(bits) != float(windingV))` and
/// the integer route to the identical expression without the casts. Every
/// operand is 0 or 1 and exact in `float`, so the two are the same expression
/// and the condition held for **every possible content of all four tables**.
/// There were never two tables: `kFaceCornerBits`, `kWindingU`, `kWindingV`,
/// `kBottomLeftWinding` and `turnRunsWithTheSlots` all live in that one header,
/// and this file supplied nothing to compare against. `CLAUDE.md` bug shape
/// #11 - a `static_assert` comparing one side of a derivation against itself.
///
/// **What replaces it is the one real premise that pair could ever have
/// tested, stated directly and widened from one corner to four.**
/// `makeBottomLeftWinding` does not loop - it names indices 0, 1, 2 and 3 by
/// hand - so a swapped or mistyped entry there is a genuine, silent defect.
/// The header guards that function's *length* (`kBottomLeftWinding.size() ==
/// sizeof(kWindingU) / sizeof(kWindingU[0])`) and nothing guards its
/// *contents*. The old assert reached only corner 0, and only through a double
/// negation that cancelled; this reaches all four directly.
///
/// **This is a check, not an answer** - it derives nothing and is read by
/// nobody, which is the whole difference between a proof and the duplicate that
/// bug shape #1 warns about. It belongs in `FaceGeometry.hpp` beside the length
/// guard it completes; filed against that file rather than moved, since it is
/// not this one's to edit.
constexpr bool bottomLeftWindingTranscribesTheIntegers() {
    for (int c = 0; c < 4; ++c) {
        if (kBottomLeftWinding[c].x != static_cast<float>(kWindingU[c]) ||
            kBottomLeftWinding[c].y != static_cast<float>(kWindingV[c])) {
            return false;
        }
    }
    return true;
}

static_assert(bottomLeftWindingTranscribesTheIntegers(),
              "`kBottomLeftWinding` no longer matches the `kWindingU`/`kWindingV` pair it is "
              "built from - `makeBottomLeftWinding` names indices 0..3 by hand, so check it for "
              "a swapped u/v or a mistyped entry; the header's own assert covers only its length");

/// The rectangle one face of one box wants, in this file's slot order: the
/// override-aware rect, mirrored if the box forbids the automatic mirror on
/// this wall, then walked round by the quarter turns it is owed.
///
/// The mirror is applied before the turn, and that order is not a preference -
/// `turnsAgreeWithTheMesher` runs both against the mesher's own arithmetic with
/// `unmirror` on and off, at all four turns, on all six faces.
///
/// **The turn is a parameter and not read from the box, so that the proof can
/// reach a case the tables cannot.** It was written the other way first, and
/// that version had the exact hole this file has filed against three other
/// files tonight: `boxFaceTurns` answers 0 for every wall today, so a sweep
/// that took the turn from the box exercised the shift on lids and floors
/// only - and would have passed unchanged with `turnRunsWithTheSlots` wrong on
/// both `Z` walls, which is the one thing it exists to get right.
constexpr std::array<glm::vec2, 4> faceRectTurnedBy(const ModelBox& box, AxisFace face, int turns,
                                                    bool (*runsWithSlots)(AxisFace)) {
    const FaceRect said = boxFaceRect(box, face);
    std::array<glm::vec2, 4> rect = rectAcross(said.uMin, said.vMin, said.uMax, said.vMax);
    if (faceAxis(face) != 1 && box.unmirror != FaceDirection::Unknown &&
        box.unmirror == faceDirectionOf(face)) {
        rect = {rect[1], rect[0], rect[3], rect[2]};
    }
    const int wanted = turns & 3;
    if (wanted == 0) {
        return rect;
    }
    const auto t = static_cast<std::size_t>(runsWithSlots(face) ? wanted : ((4 - wanted) & 3));
    return {rect[t], rect[(t + 1) & 3], rect[(t + 2) & 3], rect[(t + 3) & 3]};
}

constexpr std::array<glm::vec2, 4> faceRectTurned(const ModelBox& box, AxisFace face, int turns) {
    return faceRectTurnedBy(box, face, turns, turnRunsWithTheSlots);
}

constexpr std::array<glm::vec2, 4> modelFaceRect(const ModelBox& box, AxisFace face) {
    return faceRectTurned(box, face, static_cast<int>(boxFaceTurns(box, face)));
}

/// **`ChunkMesher.cpp`'s own arithmetic, quoted as an oracle and never as a
/// drawing path.** It builds a uv by turning the corner's two in-plane
/// fractions and then mixing them across the rectangle, which is a different
/// shape of expression from the slot walk above; agreeing is therefore
/// evidence rather than a restatement. `axisOnlyLayer` sits in the mesher for
/// the same reason - a rule kept solely so a proof has something to reject.
constexpr glm::vec2 mesherUv(const ModelBox& box, AxisFace face, int corner, int uAxis,
                             int vAxis, int turns) {
    const std::array<glm::vec3, 4> corners = faceCorners(face);
    bool flipU = corners[0][uAxis] != kBottomLeftWinding[0].x;
    const bool flipV = corners[0][vAxis] != kBottomLeftWinding[0].y;
    if (faceAxis(face) != 1 && box.unmirror != FaceDirection::Unknown &&
        box.unmirror == faceDirectionOf(face)) {
        flipU = !flipU;
    }
    float fu = corners[corner][uAxis];
    float fv = corners[corner][vAxis];
    switch (turns & 3) {
    case 1: {
        const float was = fu;
        fu = fv;
        fv = 1.0f - was;
        break;
    }
    case 2:
        fu = 1.0f - fu;
        fv = 1.0f - fv;
        break;
    case 3: {
        const float was = fu;
        fu = 1.0f - fv;
        fv = was;
        break;
    }
    default:
        break;
    }
    const FaceRect r = boxFaceRect(box, face);
    return {fu * (flipU ? r.uMin - r.uMax : r.uMax - r.uMin) + (flipU ? r.uMax : r.uMin),
            fv * (flipV ? r.vMin - r.vMax : r.vMax - r.vMin) + (flipV ? r.vMax : r.vMin)};
}

/// A box that is not the unit cube, is not square, has a lid rect that differs
/// from its walls' and names a wall to unmirror - so every field the comparison
/// depends on is distinguishable from every other. A synthetic box is the point
/// rather than a shortcut: no shipping box carries a wall turn, so the
/// combination that would expose a wrong composition order is unreachable from
/// the real tables and has to be constructed.
constexpr ModelBox proofBox(unsigned char turns, bool unmirror) {
    ModelBox box{};
    box.uMin = 0.125f;
    box.vMin = 0.25f;
    box.uMax = 0.625f;
    box.vMax = 0.875f;
    box.topUMin = 0.0625f;
    box.topVMin = 0.5f;
    box.topUMax = 0.9375f;
    box.topVMax = 0.75f;
    box.sideLayer = 5.0f;
    box.lidLayer = 7.0f;
    box.lidTurns = turns;
    box.unmirror = unmirror ? FaceDirection::PosZ : FaceDirection::Unknown;
    return box;
}

/// Every face, every turn, with and without the mirror, against the mesher's
/// arithmetic. **Four turns and not one**: turn 2 is its own reverse, so a
/// proof that only ran it would pass on a shift rule pointing the wrong way.
///
/// **The turn is injected rather than read from the box**, which is what makes
/// this cover the four walls. `proofBox` can only state `lidTurns`, and
/// `boxFaceTurns` gives a wall its override row or nothing - and no shipping
/// row carries a turn. Taken from the box, this swept 24 face-cases of which
/// only 8 had a non-zero turn, all of them lids or floors.
constexpr bool turnsAgreeWithTheMesher(int uAxisOverride, int vAxisOverride) {
    for (int turns = 0; turns < 4; ++turns) {
        for (int mirrored = 0; mirrored < 2; ++mirrored) {
            const ModelBox box = proofBox(static_cast<unsigned char>(turns), mirrored != 0);
            for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
                const auto face = static_cast<AxisFace>(i);
                const int uAxis = uAxisOverride >= 0 ? faceVAxis(face) : faceUAxis(face);
                const int vAxis = vAxisOverride >= 0 ? faceUAxis(face) : faceVAxis(face);
                const std::array<glm::vec2, 4> mine = faceRectTurned(box, face, turns);
                for (int c = 0; c < 4; ++c) {
                    const glm::vec2 theirs = mesherUv(box, face, c, uAxis, vAxis, turns);
                    if (mine[static_cast<std::size_t>(c)].x != theirs.x ||
                        mine[static_cast<std::size_t>(c)].y != theirs.y) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

/// The wall half of the sweep above, **with the shift rule as a parameter**, so
/// the true rule and the wrong one are put through the identical comparison.
/// Returns true if the rule disagrees with the mesher anywhere on a wall.
///
/// Parameterising it is what makes the negative test real. The earlier version
/// of that test compared `turnRunsWithTheSlots` against `faceIsPositive` and
/// asserted they differ on two `Z` faces - which shows the two rules are not
/// the same function, and says nothing whatever about which of them draws the
/// right rectangle.
constexpr bool wallTurnsDisagreeWithTheMesher(bool (*runsWithSlots)(AxisFace)) {
    for (int turns = 1; turns < 4; ++turns) {
        for (int mirrored = 0; mirrored < 2; ++mirrored) {
            const ModelBox box = proofBox(static_cast<unsigned char>(turns), mirrored != 0);
            for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
                const auto face = static_cast<AxisFace>(i);
                if (faceAxis(face) == 1) {
                    continue;
                }
                const std::array<glm::vec2, 4> mine =
                    faceRectTurnedBy(box, face, turns, runsWithSlots);
                for (int c = 0; c < 4; ++c) {
                    const glm::vec2 theirs =
                        mesherUv(box, face, c, faceUAxis(face), faceVAxis(face), turns);
                    if (mine[static_cast<std::size_t>(c)].x != theirs.x ||
                        mine[static_cast<std::size_t>(c)].y != theirs.y) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/// 4 walls x 3 non-zero turns x 2 mirrors, counted rather than assumed, because
/// the sweep above returns as soon as it finds a disagreement and a loop that
/// ran zero times would report agreement just as loudly.
constexpr int wallTurnCasesSwept() {
    int cases = 0;
    for (int turns = 1; turns < 4; ++turns) {
        for (int mirrored = 0; mirrored < 2; ++mirrored) {
            for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
                if (faceAxis(static_cast<AxisFace>(i)) != 1) {
                    ++cases;
                }
            }
        }
    }
    return cases;
}

static_assert(turnsAgreeWithTheMesher(-1, -1),
              "a falling model box no longer paints the rectangle the world mesher paints - the "
              "same block reads one way standing and another way in mid-air");

/// The proof shown failing, twice, on the two rules that were nearly written
/// instead. Swapping U and V is the mesher's own `shapeUvsMatchFaceTables`
/// hazard restated here, and the turn rule is the live one: `faceIsPositive`
/// agrees on `+X`, `-X`, `+Y` and `-Y` and is backwards on both `Z` walls, so
/// it would have been indistinguishable from correct until the first wall turn
/// landed, and then wrong on exactly half the walls that have one.
constexpr bool swappedAxesAreRejected() { return !turnsAgreeWithTheMesher(1, 1); }

/// **Counted, not short-circuited**, and kept only as a description of *where*
/// the two rules part company - `+X`, `-X`, `+Y` and `-Y` agree, both `Z` walls
/// do not, which is why the wrong rule looked right for as long as it did.
///
/// **It is not the proof, and an earlier version of this file used it as one.**
/// Two functions differing on two inputs says nothing about which of them
/// draws the right rectangle; `wallTurnsDisagreeWithTheMesher` is the test that
/// answers that, by running each rule against the mesher's own arithmetic.
constexpr bool theTwoRulesPartOnTheZWalls() {
    int disagreements = 0;
    int onZ = 0;
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        const auto face = static_cast<AxisFace>(i);
        if (turnRunsWithTheSlots(face) != faceIsPositive(face)) {
            ++disagreements;
            onZ += faceAxis(face) == 2 ? 1 : 0;
        }
    }
    return disagreements == 2 && onZ == 2;
}

static_assert(swappedAxesAreRejected(),
              "the mesher comparison passes with U and V exchanged, so it is not measuring which "
              "way a rectangle is laid across a face");
static_assert(theTwoRulesPartOnTheZWalls(),
              "faceIsPositive and flipU == flipV have stopped differing on exactly the two Z "
              "walls, so the negative test below is no longer testing the rule it names");

// **The pair that actually proves the walls**, positive and negative through
// one comparison. The sweep above cannot reach these cases: `proofBox` states
// only `lidTurns`, `boxFaceTurns` gives a wall its override row or nothing, and
// no shipping row carries a turn - so every wall it sees is turned by zero.
static_assert(wallTurnCasesSwept() == 24,
              "the wall sweep stopped covering 4 walls x 3 non-zero turns x 2 mirrors, and a "
              "sweep of nothing reports agreement exactly as loudly as a sweep that passed");
static_assert(!wallTurnsDisagreeWithTheMesher(turnRunsWithTheSlots),
              "a turned wall on a falling block no longer paints what the world mesher paints");
static_assert(wallTurnsDisagreeWithTheMesher(faceIsPositive),
              "substituting faceIsPositive for flipU == flipV now passes the wall sweep, so the "
              "sweep has stopped measuring the shift direction it exists to pin down");

} // namespace

void FallingBlocks::spawn(const glm::ivec3& cell, BlockId block) {
    if (block == BlockId::Air) {
        return;
    }
    m_blocks.push_back(Falling{.position = glm::vec3{cell},
                               .velocity = 0.0f,
                               .age = 0.0f,
                               .fellCells = 0.0f,
                               .block = block});
}

std::vector<FallingBlocks::Crushed> FallingBlocks::update(World& world, float deltaSeconds,
                                                         std::vector<Landed>* landed) {
    std::vector<Crushed> crushed;
    deltaSeconds = std::min(deltaSeconds, kMaxDeltaSeconds);

    for (std::size_t i = m_blocks.size(); i-- > 0;) {
        Falling& falling = m_blocks[i];

        const int cellX = static_cast<int>(std::floor(falling.position.x));
        const int cellZ = static_cast<int>(std::floor(falling.position.z));

        // **The clock runs before every early-out below.** See `Falling::age`:
        // the physics may skip a frame, the lifetime may not, or a cube whose
        // column streamed out from under it lives in this vector for ever.
        falling.age += deltaSeconds;

        const auto give = [&](int cell) {
            applyFallingLanding(world, {cellX, cell, cellZ}, falling.block, falling.fellCells,
                                crushed, landed);
            m_blocks[i] = m_blocks.back();
            m_blocks.pop_back();
        };

        if (falling.age >= kMaxAirborneSeconds) {
            // Thirty seconds, and the reference gives up wherever the cube
            // happens to be. It goes through the same landing as everything
            // else, which in open air finds no support and hands it to the drop
            // table - so it becomes an item exactly as the reference says.
            give(static_cast<int>(std::floor(falling.position.y)));
            continue;
        }

        // **The row this file was missing from the guard table, and the only
        // one where the block itself is lost.** An absent chunk reads as air,
        // so the column walk below saw an open shaft all the way to bedrock -
        // and `World::setBlock` then returns early for a chunk it does not
        // have, so the landing write was dropped outright. The cube is already
        // detached (`World::updateFalls` set its source cell to air and flagged
        // that chunk modified, so the *hole* is saved), which makes this a
        // block deleted from a saved world: mine sand near the loaded edge,
        // sprint away, and both the block and its item are gone for good.
        //
        // The gate one function above the detach - `columnReadyOrDeferred` -
        // already carries a comment naming this exact failure. It protected the
        // spawn and not the flight: a rule that existed and was correct in only
        // one of the two places that needed it.
        //
        // Velocity is deliberately *not* zeroed. A skipped frame is a frame
        // that did not happen, and a cube resuming from rest reads as a hover;
        // nothing accumulates, because gravity is applied below this point.
        if (!world.columnResident(cellX, cellZ)) {
            continue;
        }

        falling.velocity = std::max(falling.velocity - kGravity * deltaSeconds, -kTerminalVelocity);

        const int fromCell = static_cast<int>(std::floor(falling.position.y));
        const float wantedY = falling.position.y + falling.velocity * deltaSeconds;
        const int toCell = static_cast<int>(std::floor(wantedY));

        const CubeRest rest = fallingCubeRest(
            fromCell, toCell, [&](int y) { return world.blockAt(cellX, y, cellZ); });

        // **Descended, not elapsed.** An anvil's damage is a function of how far
        // it actually came, so this only counts movement - and it counts the
        // final fraction of a block too, which is why it is written at each of
        // the three places `position.y` moves rather than once at the end.
        const auto descendTo = [&](float y) {
            falling.fellCells += falling.position.y - y;
            falling.position.y = y;
        };

        if (!rest.landed) {
            descendTo(wantedY);
            continue;
        }

        const float restY = static_cast<float>(rest.cell);
        if (falling.position.y > restY + kSettleEpsilon) {
            // Still above its resting cell, so it keeps falling toward it and
            // lands on a later frame. Without this the block snaps down the
            // instant its landing site is known, which is the teleport again.
            descendTo(std::max(wantedY, restY));
            continue;
        }

        descendTo(restY);
        give(rest.cell);
    }

    return crushed;
}

std::vector<FallingBlocks::Crushed> FallingBlocks::settleAll(World& world) {
    std::vector<Crushed> crushed;

    for (const Falling& falling : m_blocks) {
        const int cellX = static_cast<int>(std::floor(falling.position.x));
        const int cellZ = static_cast<int>(std::floor(falling.position.z));
        const int fromCell = static_cast<int>(std::floor(falling.position.y));

        // The rest of the fall in one step, down to the void floor if nothing
        // stops it. **The same walk the frame update uses** - the point of
        // `fallingCubeRest` taking its column as a parameter is that this is not
        // a second, simpler version of it that agrees today.
        //
        // An absent column reads as air all the way down, so a cube out there
        // finds no support, and `applyFallingLanding` hands it to the item
        // channel rather than writing a block into a chunk that is not loaded
        // and would swallow the write.
        const CubeRest rest = world.columnResident(cellX, cellZ)
                                  ? fallingCubeRest(fromCell, 0,
                                                    [&](int y) {
                                                        return world.blockAt(cellX, y, cellZ);
                                                    })
                                  : CubeRest{fromCell, true};

        applyFallingLanding(world, {cellX, rest.cell, cellZ}, falling.block,
                            falling.fellCells + (falling.position.y - static_cast<float>(rest.cell)),
                            crushed, nullptr);
    }

    m_blocks.clear();
    return crushed;
}

engine::MeshData FallingBlocks::buildMesh(const World& world, const DrawRange& range) const {
    engine::MeshData mesh;

    for (const Falling& falling : m_blocks) {
        if (!range.contains(falling.position)) {
            continue;
        }
        const glm::vec3 min = falling.position;

        // Lit by the cell the cube is passing through, so a block dropping into
        // a cave darkens as it goes rather than staying lit by the sky it left.
        const int lx = static_cast<int>(std::floor(min.x));
        const int ly = static_cast<int>(std::floor(min.y));
        const int lz = static_cast<int>(std::floor(min.z));
        const float sky =
            static_cast<float>(world.skyLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);
        const float blockLight =
            static_cast<float>(world.blockLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);

        const auto quad = [&](AxisFace face, const BlockBox& box, const ModelBox* model) {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            const FaceDirection direction = faceDirectionOf(face);

            // **A model box carries its own picture as well as its own
            // rectangle, and it carries them per face.** A bell is a wooden
            // frame round a gold bell; without a box layer at all every post
            // came out gold. Without the *face* half, an anvil wore its damage
            // texture on its underside in mid-air and its base texture the
            // instant it landed - `boxFaceLayer` is what closes that, and
            // `-1` still means "take the block's own", which is what a plain
            // cube always does.
            const float ownLayer = blockTextureLayer(falling.block, blockFaceOf(face), direction);
            const float boxLayer = model == nullptr ? -1.0f : boxFaceLayer(*model, face);
            const float layer = boxLayer >= 0.0f ? boxLayer : ownLayer;

            // Whole cell and whole texture when there is no model, which is the
            // sand-and-gravel case and every other cube that falls.
            //
            // **The three answers are asked separately**, because they were
            // once decided together on `faceAxis(face) == 1` and that single
            // test was wrong for all three: a floor took its lid's rectangle,
            // its lid's layer and its lid's quarter turn. Measured against the
            // mesher across every model block: 136 faces disagreeing on the
            // layer, 454 on the rectangle, 269 blocks and 486 boxes affected -
            // now 0 of each. Splitting two of the three and leaving the turn
            // would have put the right picture on a repeater's belly at the
            // wrong angle, which is the half-fix `Block.hpp` names.
            const std::array<glm::vec2, 4> rect =
                model == nullptr ? rectAcross(0.0f, 0.0f, 1.0f, 1.0f) : modelFaceRect(*model, face);

            // **The corners come from `FaceGeometry.hpp`, which owns them**,
            // rather than being written out here. They used to be written out
            // here, as the mesher's rows *reversed*, which is a pure V-flip on
            // all 24 corners and an inward winding on all 6 faces: a falling
            // cube was upside-down against the block that had been standing
            // there a moment earlier, and presented its inside rather than its
            // outside to a pipeline that culls back faces. Measured corner for
            // corner against the mesher, it disagreed on 24 of 24 - every one
            // of them in v alone, none in u.
            //
            // Near-invisible on sand, gravel and concrete powder, which are
            // close to symmetric, and that is exactly why it survived.
            const std::array<glm::vec3, 4> corners = faceCorners(face);

            for (int i = 0; i < 4; ++i) {
                const glm::vec3 corner{corners[i].x != 0.0f ? box.maxX : box.minX,
                                       corners[i].y != 0.0f ? box.maxY : box.minY,
                                       corners[i].z != 0.0f ? box.maxZ : box.minZ};
                mesh.vertices.push_back(engine::Vertex{
                    {min.x + corner.x, min.y + corner.y, min.z + corner.z},
                    engine::packVertexColor(sky, blockLight, faceShade(face), 1.0f),
                    {rect[static_cast<std::size_t>(i)].x, rect[static_cast<std::size_t>(i)].y},
                    layer,
                    engine::packVertexSurface(faceNormalCode(face), 1.0f)});
            }
            mesh.indices.insert(mesh.indices.end(),
                                {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
        };

        // Shades come from the shared table rather than being written out here.
        // They were written out here, and five of the six had drifted - a bottom
        // of 0.5 against the mesher's 0.45, sides of 0.8/0.6 against 0.86, 0.60
        // and 0.72 - so a falling block *was* lit differently from the block it
        // had just been, under a comment saying it was not.
        //
        // **The direction goes with the face too.** `blockTextureLayer` takes
        // one, and passing only `BlockFace::Side` hands a single side layer to
        // all four sides - the 2026-08-11 mistake, which the drop and the icon
        // paths have already paid for once each.
        const auto sixFaces = [&](const BlockBox& box, const ModelBox* model) {
            for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
                quad(static_cast<AxisFace>(i), box, model);
            }
        };

        // **A plant is not a box and must not be drawn as one.** A stalactite
        // torn loose by mining its ceiling is a `Cross`, and `usesModelIcon`
        // says no to `Cross`, so it took the whole-cell arm and fell as a solid
        // stone cube - the same defect the model arm below was added to fix,
        // one shape class along. `postModel` answers for it (four boxes), and
        // reading those would have been the plausible wrong fix: `ChunkMesher`
        // never asks `postModel` for a `Cross`, it emits two blades at
        // forty-five degrees and moves on, so boxes would have made the faller
        // disagree with the block it just was.
        //
        // Corners, winding, inset, shade and the double-sided index run are the
        // mesher's, read from it rather than re-derived - this file has already
        // paid once for a corner table written out a second time and reversed.
        // Two differences from the mesher, both deliberate: no sway, because
        // sway is measured from a root block and a falling plant has none; and
        // the layer is the block's own side texture, which is what the mesher
        // uses for a cross too.
        const auto crossBlades = [&]() {
            constexpr float lo = 0.05f;
            constexpr float hi = 1.0f - lo;
            const float layer = blockTextureLayer(falling.block, BlockFace::Side);
            const std::array<std::array<glm::vec3, 4>, 2> blades{
                std::array<glm::vec3, 4>{glm::vec3{lo, 0, lo}, glm::vec3{hi, 0, hi},
                                         glm::vec3{hi, 1, hi}, glm::vec3{lo, 1, lo}},
                std::array<glm::vec3, 4>{glm::vec3{hi, 0, lo}, glm::vec3{lo, 0, hi},
                                         glm::vec3{lo, 1, hi}, glm::vec3{hi, 1, lo}}};

            for (const std::array<glm::vec3, 4>& blade : blades) {
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                for (std::size_t c = 0; c < 4; ++c) {
                    mesh.vertices.push_back(engine::Vertex{
                        {min.x + blade[c].x, min.y + blade[c].y, min.z + blade[c].z},
                        // A blade stands at forty-five degrees, so it has no
                        // face to shade by and takes full brightness, exactly
                        // as the mesher does.
                        engine::packVertexColor(sky, blockLight, 1.0f, 1.0f),
                        {kBottomLeftWinding[c].x, kBottomLeftWinding[c].y},
                        layer,
                        engine::packVertexSurface(engine::kNormalUnaligned, 1.0f)});
                }
                mesh.indices.insert(mesh.indices.end(),
                                    {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
                // **Both windings, or the blade vanishes from one side.** The
                // world pass culls back faces and a plant is a single sheet, so
                // the mesher emits the reverse run too. Dropping it here would
                // make a falling stalactite half-invisible depending on which
                // way the player happened to be standing.
                mesh.indices.insert(mesh.indices.end(),
                                    {base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
            }
        };

        // **The fourth place a block is drawn, and the one nobody had checked.**
        // The mesher, the slot picture and the dropped item all iterate
        // `postModel` box for box; this one emitted a single whole-cell cube for
        // everything, so a falling anvil was a solid black cube in mid-air that
        // snapped to its real shape the instant it landed, and a dragon egg the
        // same. `everyFallerCanBeDrawn` is what keeps the three arms exhaustive.
        if (blockShape(falling.block) == BlockShape::Cross) {
            crossBlades();
        } else if (usesModelIcon(blockShape(falling.block))) {
            const ModelBoxes model = postModel(falling.block);
            for (int i = 0; i < model.count; ++i) {
                sixFaces(model.boxes[i].box, &model.boxes[i]);
            }
        } else {
            sixFaces(kWholeCell, nullptr);
        }
    }

    return mesh;
}

} // namespace game
