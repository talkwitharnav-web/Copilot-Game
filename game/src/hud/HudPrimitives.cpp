#include "hud/HudPrimitives.hpp"

#include "item/Mining.hpp"
#include "world/Block.hpp"
#include "world/FaceGeometry.hpp"
#include "world/FaceShading.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace game::hud {
namespace {

/// One step nearer the eye, for marks that must land on top of the icon they
/// annotate rather than beside it.
///
/// The UI pass has **no depth attachment at all** (`Renderer.cpp`, `recordUiPass`
/// - "No depth attachment. That is the whole point of this pass"), so this is
/// not a depth test being satisfied: it is the key the renderer's stable sort
/// runs on, far first, append order breaking ties. Same-depth marks would still
/// come out in the right order here because they are appended after the icon -
/// this is belt and braces for the day a caller appends decorations before the
/// picture, which is exactly the sort of reorder nobody would think to check.
constexpr float kDecorationStep = 0.000005f;

// **The header publishes the reach; this is where it comes from.** The count
// text is the nearest mark a decoration set draws, two steps in front of the
// depth the caller handed in, so a screen asserting its band against
// `kDecorationSpan` is asserting against this line. Adding a third mark in
// front of the count without widening the published span is the edit this
// catches.
static_assert(kDecorationSpan == kDecorationStep * 2.0f,
              "kDecorationSpan no longer states how far in front of its own depth a stack's marks "
              "reach, so every screen's depth-band assert is checking the wrong number");

/// The durability bar's rectangle, in sixteenths of the item square. `UI.md`
/// §6.5b, which is the reference's own layout: two pixels in from the left,
/// thirteen wide, starting thirteen down, two tall, of which only the top row
/// carries the colour.
constexpr float kBarLeft = 2.0f / 16.0f;
constexpr float kBarWidth = 13.0f / 16.0f;
constexpr float kBarTop = 13.0f / 16.0f;
constexpr float kBarHeight = 2.0f / 16.0f;
constexpr float kBarFillHeight = 1.0f / 16.0f;
/// Thirteen pixels wide is thirteen states, not a continuum.
constexpr float kBarSteps = 13.0f;

static_assert(kBarLeft + kBarWidth <= 1.0f && kBarTop + kBarHeight <= 1.0f,
              "the durability bar must fit inside the item square it annotates - widening kBarWidth "
              "or dropping kBarTop by one sixteenth hangs it over the slot's bevel");

/// Green through yellow to red as wear rises, as two straight lines meeting at
/// half rather than a table, because the reference's ramp is exactly that.
///
/// Split into scalars so the shape can be proved at compile time: a `glm::vec4`
/// is not reliably usable in a constant expression across GLM versions, and an
/// assert that cannot be written is an assert that does not exist.
constexpr float durabilityRed(float remaining) {
    return remaining <= 0.5f ? 1.0f : 2.0f - 2.0f * remaining;
}
constexpr float durabilityGreen(float remaining) {
    return remaining <= 0.5f ? 2.0f * remaining : 1.0f;
}

// **A formula written from memory can come out inverted, and reading it as a
// ratio hides that.** Three known points, stated as the colours a player names.
static_assert(durabilityRed(1.0f) == 0.0f && durabilityGreen(1.0f) == 1.0f,
              "an unworn tool's bar is green - swapping the two arms of durabilityRed paints a fresh "
              "pick red and a nearly-spent one green");
static_assert(durabilityRed(0.5f) == 1.0f && durabilityGreen(0.5f) == 1.0f,
              "the two arms must meet at yellow - changing either 2.0f factor tears a visible seam "
              "across the middle of the ramp");
static_assert(durabilityRed(0.0f) == 1.0f && durabilityGreen(0.0f) == 0.0f,
              "a spent tool's bar is red - flipping the <= 0.5f comparison ends the ramp on black");

/// One corner's position along a face's own two axes, in the unit-cube 0/1
/// pattern `FaceGeometry.hpp` owns.
///
/// A plain pair rather than a `glm::vec2` so the rotation below can be proved in
/// a constant expression: GLM's vector constructors are not `constexpr` on every
/// version, and an assert that cannot be written is an assert that does not
/// exist.
struct FaceParam {
    float u;
    float v;
};

/// (u, v) of the four corners, **in the order `appendBlockIcon` lists them** -
/// which is the icon's own listing and deliberately is *not* the mesher's +Y
/// row. A lid is listed minZ-first and a wall top-first, which is why they need
/// two tables and the flips do the rest.
constexpr FaceParam kLidParams[4]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
constexpr FaceParam kWallParams[4]{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

/// Which world axis a face's U and V run along - **owned by
/// `FaceGeometry.hpp`, not restated here.**
///
/// This file kept a byte-identical copy of both one-liners until 2026-08-19.
/// They were correct, and that is exactly why the copy was dangerous: nothing
/// bound them to the header's pair, so a fix there would have left this file
/// answering the old way with no diagnostic. `ChunkMesher.cpp`'s `kFaces`
/// carries these as two columns per row (`uAxis`, `vAxis`) and every row obeys
/// the same pair of rules - a face's U takes the first world axis that is not
/// its own, and its V takes the second; the six rows read 2/1, 2/1, 0/2, 0/2,
/// 0/1, 0/1. The header proves that against the corner table in
/// `uvAxesAreTheOnesThatMove`, which is a stronger statement than this comment
/// used to make on its own.

/// One corner of one face as the mesher parameterises it: the corner's own bits
/// read along that face's U and V axes.
///
/// **`FaceGeometry.hpp` owns the corner rows and this reads them.** It used to
/// be a hand transcription of the +Y row sitting under a comment that said what
/// it had been transcribed from - which is exactly the shape the shared header
/// exists to abolish, and exactly the shape that mirrored every dropped block
/// in this game for twenty milestones. The transcription happened to be right;
/// that is not a property anybody can check at a glance, and now nobody has to.
constexpr FaceParam mesherFaceParam(AxisFace face, int corner) {
    return {static_cast<float>(cornersOf(face)[corner][faceUAxis(face)]),
            static_cast<float>(cornersOf(face)[corner][faceVAxis(face)])};
}

/// The mesher's own +Y corner order. **Here only to be asserted against, never
/// read by the drawing code.** It is what turns the paragraph below the
/// rotation into a checked fact instead of a claim, and it is the reason nobody
/// has to take a comment's word for which listing is which.
constexpr FaceParam kMesherPosYParams[4]{
    mesherFaceParam(AxisFace::PosY, 0), mesherFaceParam(AxisFace::PosY, 1),
    mesherFaceParam(AxisFace::PosY, 2), mesherFaceParam(AxisFace::PosY, 3)};

/// **The mesher's own lid rotation, transcribed rather than re-derived.**
///
/// A model turns its lid to say which way round a texture goes when the block
/// itself cannot be rotated - a bed's pillow is painted along one edge of its
/// top texture. Applied to the face-local `(u, v)` **before** the flips, exactly
/// as `ChunkMesher.cpp` applies it, or the two orders disagree on every odd
/// number of turns.
constexpr FaceParam turnedLidParam(FaceParam p, unsigned char turns) {
    switch (turns & 3u) {
    case 1:
        return {p.v, 1.0f - p.u};
    case 2:
        return {1.0f - p.u, 1.0f - p.v};
    case 3:
        return {1.0f - p.v, p.u};
    default:
        return p;
    }
}

constexpr bool sameParam(FaceParam a, FaceParam b) { return a.u == b.u && a.v == b.v; }

/// Which way a turn moves the corners **of a given listing**: `shift` of +1
/// means turning by `n` sends the corner at index `c` to where `c + n` was, and
/// -1 means it sends it back to where `c - n` was.
///
/// Taking the listing as a parameter is the whole point. The direction of the
/// shift is a property of the *table*, not of the arithmetic, and every argument
/// this file has had about `lidTurns` came from a sentence that confused the
/// two.
constexpr bool lidTurnShifts(const FaceParam (&params)[4], int shift, unsigned char turns) {
    for (int c = 0; c < 4; ++c) {
        const int to = ((c + shift * static_cast<int>(turns)) % 4 + 4) % 4;
        if (!sameParam(turnedLidParam(params[c], turns), params[to])) {
            return false;
        }
    }
    return true;
}

/// The two listings run opposite ways round the same square.
constexpr bool lidListingsAreReverses() {
    for (int c = 0; c < 4; ++c) {
        if (!sameParam(kLidParams[c], kMesherPosYParams[3 - c])) {
            return false;
        }
    }
    return true;
}

// **The rotation, pinned against both listings at once - which is the only way
// the apparent disagreement between this file and the other two ever gets
// settled.**
//
// The dropped-item path writes the same idea as `cap[i] = capRect[(turns + i) & 3]`,
// the *opposite* shift from the one below, and an earlier version of this
// comment claimed `kLidParams` was "the same corner order ChunkMesher.cpp uses".
// **It is not**: `kLidParams` is the mesher's **-Y** row, and its +Y row is
// `kMesherPosYParams`, the exact reverse. Under a reversed listing a -1 shift
// and a +1 shift are the same rotation, which is why all three files can look
// like they contradict each other while every one of them is right.
//
// So the shared truth is `turnedLidParam` - the mesher's switch, transcribed,
// applied to face-local (u, v), where corner *order* cannot reach it. These
// four asserts show the same arithmetic producing a backward shift through this
// file's listing and a forward one through the mesher's, so nobody has to take
// the paragraph above on trust.
//
// **The single edit that breaks this is "fixing" the switch to match the drop
// path's `(turns + i) & 3`** without re-ordering `kLidParams` - which turns
// every lid the wrong way while looking, in isolation, exactly right. The
// mirror-image edit, re-ordering `kLidParams` to match the mesher's +Y row and
// leaving the switch alone, breaks the same two asserts from the other side.
static_assert(lidListingsAreReverses(),
              "kLidParams is the mesher's -Y listing, which is its +Y listing reversed - the "
              "whole reason the shift below looks backward next to the mesher and the drop");
static_assert(lidTurnShifts(kLidParams, -1, 0) && lidTurnShifts(kLidParams, -1, 1) &&
                  lidTurnShifts(kLidParams, -1, 2) && lidTurnShifts(kLidParams, -1, 3),
              "through this file's own corner listing, a lid turned by n must send corner c to "
              "where corner c - n was");
static_assert(lidTurnShifts(kMesherPosYParams, 1, 0) && lidTurnShifts(kMesherPosYParams, 1, 1) &&
                  lidTurnShifts(kMesherPosYParams, 1, 2) && lidTurnShifts(kMesherPosYParams, 1, 3),
              "the identical arithmetic, through the mesher's +Y listing, must shift the other "
              "way - which is what makes the icon and the placed block agree");
static_assert(sameParam(turnedLidParam(turnedLidParam(turnedLidParam(turnedLidParam({0.25f, 0.75f}, 1), 1), 1), 1),
                        {0.25f, 0.75f}),
              "four quarter turns must come back to where they started - written against an "
              "off-centre point, because a symmetric one passes whatever the arithmetic does");
static_assert(sameParam(turnedLidParam({0.25f, 0.75f}, 2), {0.75f, 0.25f}),
              "two turns is the 180-degree flip, so both components invert");

/// **Every face this icon draws is turned, and the underside is the only one it
/// does not draw.**
///
/// This paragraph used to say the opposite - "a lid is the only face this icon
/// can turn", and that "the day a `ModelBox` states a turn for a wall, the world
/// will honour it and this icon will silently not". That was true when it was
/// written and stopped being true eleven minutes later: `wallRule` now asks
/// `boxFaceTurns(box, axisFace)` for `+Z` and `+X` exactly as `lidRule` asks it
/// for `+Y`, and `writeUvs` applies `rule.turns` on every face without
/// exception. A comment that tells a reader a working path is broken is the
/// most expensive kind this project has - it survives review, because it is
/// written in the voice of the notes that earned their trust.
///
/// **What is genuinely absent, and why it is absent rather than missing.** The
/// isometric view shows `+Y`, `+Z` and `+X` and never the underside, so the
/// `-Y` row of the tables is never read. It is also the one row that would need
/// the mirror the drop path spells `(4 - turns) & 3`, because `-Y` carries
/// `flipV`.
///
/// **The proof is a call-site enumeration, not a name count**, because a name
/// count cannot answer this question. `NegY` is only one spelling of the `-Y`
/// row: the row is equally reachable by arithmetic (`cornersOf(1)`), by a cast
/// (`static_cast<AxisFace>(i)`), or by a loop over all six faces, and a search
/// for the token sees none of those. So ask first whether the token is the only
/// way the concept can be spelled, and where it is not, follow the data instead.
///
/// Every consumer of the face tables in this file, listed rather than counted
/// because the set is small - 2026-08-19 11:54:
///
///     lidRule()   -> boxFaceRect / boxFaceTurns (AxisFace::PosY)   literal
///     wallRule()  <- called twice, AxisFace::PosZ and AxisFace::PosX  literal
///     faceLayer() <- called three times, PosY / PosZ / PosX          literal
///
/// Six call sites, every argument a literal enumerator, drawn from a set of
/// three. There is no expression here that *could* evaluate to `-Y`, which is a
/// structural absence rather than an unobserved one - materially stronger than
/// the token sweep it replaces, and it stays true under renaming.
///
/// The supporting sweep is retained only as evidence the detector works, which
/// is all an instrument control is for: `PosY` 8, `PosZ` 6, `PosX` 6, `NegZ` 1,
/// `NegX` 1, **`NegY` 0**, and `cornersOf` 4 - all four of those resolved above.
/// The nearest neighbours of the missing row are found, so the zero is absent
/// rather than blind. The three positive asserts below name exactly those three
/// faces, which is the same fact stated where a compiler can hold it.
///
/// Falsified by any call to `faceLayer`, `wallRule`, `lidRule`, `cornersOf`,
/// `boxFaceRect`, `boxFaceLayer` or `boxFaceTurns` in this file whose face
/// argument is not a literal `AxisFace::` enumerator. Re-run that search rather
/// than trusting this paragraph.
///
/// **And this path needs no shift-direction rule at all**, which is the
/// property that makes the wall adoption safe here where it was not in
/// `ItemEntity.cpp`. `writeUvs` turns the *face-local* `(u, v)` with
/// `turnedLidParam` and applies the flips afterwards - `ChunkMesher.cpp`'s own
/// composition order - so the flips do the work a slot permutation would need a
/// direction for. A file that permutes corner *slots*, which the drop and the
/// falling block both do, has to know whether the turn runs with them
/// (`flipU == flipV`), and getting that wrong is invisible on every lid.
///
/// Measured 2026-08-19, across all 3285 ids and 12527 boxes: faces stating a
/// non-zero turn = 288, every one of them a lid or a floor, 0 walls. So the
/// wall turn is exercised by nothing today - which is exactly why it had to be
/// written from the rule rather than from what the tables happen to contain.

// ---------------------------------------------------------------------------
// The three faces an isometric icon shows, and the proof that each one reads
// the same way round as the block it stands for.
// ---------------------------------------------------------------------------

/// `glm::mix` for scalars, spelled the way GLM spells it - `x + a * (y - x)`,
/// not `x * (1 - a) + y * a`. The two agree exactly at the 0 and 1 the tables
/// below use, but a proof is only worth anything if it evaluates the expression
/// the drawing code really runs, and `glm::mix` cannot be called in one.
constexpr float mixf(float x, float y, float a) { return x + a * (y - x); }

/// One component of a corner's texture coordinate: the face's rectangle of the
/// sheet, entered from whichever end the flip names. **`writeUvs` below calls
/// this**, so the asserts are about the drawing code rather than about a second
/// copy of it - `CLAUDE.md` bug shape #11, an assert comparing one side of a
/// derivation against itself proves nothing.
constexpr float iconAxisUv(float low, float high, bool flip, float p) {
    return flip ? mixf(high, low, p) : mixf(low, high, p);
}

/// Which corner of the box each of the three drawn faces lists, as the 0/1 picks
/// off `BlockBox` that `appendBlockIcon` makes. **The drawing code reads these**
/// - they are not a description of it.
constexpr std::uint8_t kIconLidCorners[4][3]{{0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}};
constexpr std::uint8_t kIconFrontCorners[4][3]{{0, 1, 1}, {1, 1, 1}, {1, 0, 1}, {0, 0, 1}};
constexpr std::uint8_t kIconRightCorners[4][3]{{1, 1, 0}, {1, 1, 1}, {1, 0, 1}, {1, 0, 0}};

/// The texture coordinate the mesher gives one **named world corner** of a face:
/// find that corner in the shared row and take the matching slot of the rect
/// every quad in the game is handed.
///
/// Going by the corner rather than by position in the list is the whole point.
/// Two listings of the same square can run opposite ways and still be right; a
/// corner is the thing both sides can name.
constexpr FaceParam mesherCornerUv(AxisFace face, const std::uint8_t (&corner)[3]) {
    for (int c = 0; c < 4; ++c) {
        if (cornersOf(face)[c][0] == corner[0] && cornersOf(face)[c][1] == corner[1] &&
            cornersOf(face)[c][2] == corner[2]) {
            return {static_cast<float>(kWindingU[c]), static_cast<float>(kWindingV[c])};
        }
    }
    // Not a corner of that face at all, which no real listing can be and every
    // scrambled one is.
    return {-1.0f, -1.0f};
}

/// Every corner of one icon face against the mesher's answer for the same
/// corner, with the icon's own listing walked by `direction` and `shift` so a
/// reversed or rotated listing can be fed in and watched to fail.
constexpr bool iconFaceMatchesMesher(AxisFace face, const std::uint8_t (&corners)[4][3],
                                     const FaceParam (&params)[4], bool flipU, bool flipV,
                                     int direction, int shift) {
    for (int c = 0; c < 4; ++c) {
        const int p = ((direction * c + shift) % 4 + 4) % 4;
        const FaceParam icon{iconAxisUv(0.0f, 1.0f, flipU, params[p].u),
                             iconAxisUv(0.0f, 1.0f, flipV, params[p].v)};
        if (!sameParam(icon, mesherCornerUv(face, corners[c]))) {
            return false;
        }
    }
    return true;
}

// **The icon and the block, corner for corner, on all three faces the slot
// shows.** A block cut out of a cube samples the whole sheet cell, which is the
// 0-to-1 rectangle above; a model narrows that rectangle on both axes, which
// cannot change which end of it a corner enters from, so proving the whole cell
// proves the model too.
//
// This is the check the icon path never had. It replaces a paragraph of prose
// naming three flips - and prose is what let the right-hand face run its U
// backward for however long it did, because the V flip was taken off the table
// and the U flip was not. That is bug shape #5, a derivation applied to one of
// a pair and not the other, and it is the single most expensive shape in this
// codebase.
static_assert(iconFaceMatchesMesher(AxisFace::PosY, kIconLidCorners, kLidParams, false, false, 1, 0),
              "the icon's lid no longer reads the way the mesher's +Y face does - a block's "
              "picture and the block itself now disagree about which way round its top is");
static_assert(iconFaceMatchesMesher(AxisFace::PosZ, kIconFrontCorners, kWallParams, false, true, 1,
                                    0),
              "the icon's front face no longer reads the way the mesher's +Z face does - dropping "
              "the V flip here draws every side texture upside-down in the slot only");
static_assert(iconFaceMatchesMesher(AxisFace::PosX, kIconRightCorners, kWallParams, true, true, 1,
                                    0),
              "the icon's right face no longer reads the way the mesher's +X face does - this is "
              "the assert that would have caught the missing U flip");

// **The same three, shown failing**, because a proof nobody has watched reject
// something is indistinguishable from a tautology. Three different faults: the
// exact historical bug, a listing walked backward, and one rotated by a quarter
// - which is what a copy-paste between a lid and a wall produces.
static_assert(!iconFaceMatchesMesher(AxisFace::PosX, kIconRightCorners, kWallParams, false, true, 1,
                                     0),
              "the proof above accepts the right face with its U flip removed, which is the bug it "
              "exists to catch, so it proves nothing");
static_assert(!iconFaceMatchesMesher(AxisFace::PosZ, kIconFrontCorners, kWallParams, false, true, -1,
                                     3),
              "the proof above accepts a reversed corner listing - it is checking membership "
              "rather than the pairing, and a mirrored face would sail through it");
static_assert(!iconFaceMatchesMesher(AxisFace::PosY, kIconLidCorners, kLidParams, false, false, 1,
                                     1),
              "the proof above accepts a listing rotated by a quarter turn, so it would not notice "
              "a lid turned ninety degrees against the block it stands for");

/// **Every shape takes exactly one of the three icon paths.**
///
/// `usesFlatIcon` draws the artwork as the flat picture it is, `usesModelIcon`
/// builds it from `postModel`, and everything else is cut out of a cube by
/// `iconBoxes`. A shape answering *both* of the first two would be drawn as a
/// sprite here and as a model on the floor - the hotbar and the drop
/// disagreeing about the same block, which is exactly the split this pair of
/// predicates exists to prevent. Swept over the whole enum rather than the
/// three shapes we happen to know about, so a shape added to one list and
/// forgotten in the other fails the build.
///
/// **This is the shape half only, and it is kept because it is not redundant.**
/// `Block.hpp`'s `iconPathIsUnambiguous` sweeps the same question over every
/// *id*, which is the answer the two call sites below actually ask. It cannot
/// reach a shape that no block currently has; this can. Keep both, and do not
/// grow this one into a second id-level owner.
constexpr bool iconPathsAreExclusive() {
    for (int s = 0; s <= static_cast<int>(BlockShape::Sign); ++s) {
        const auto shape = static_cast<BlockShape>(s);
        if (usesFlatIcon(shape) && usesModelIcon(shape)) {
            return false;
        }
    }
    return true;
}
static_assert(iconPathsAreExclusive(),
              "a block shape is on both icon paths, so its slot picture and its dropped item "
              "are built from different geometry");

/// **The icon's two side quads must never be told the same direction.**
///
/// It shows +Y, +Z and +X, and it turns the block so its front lands on the +Z
/// one; the +X one is then a quarter turn away. `blockTextureLayer` answers
/// "front" for whichever direction matches the block's facing, so if that
/// quarter turn ever returned its own argument both visible sides would wear
/// the front - the sticky-piston bug shape, in the one path that only gets to
/// name two faces out of six.
constexpr bool quarterTurnIsAQuarterTurn() {
    const FaceDirection directions[4]{FaceDirection::PosX, FaceDirection::NegX, FaceDirection::PosZ,
                                      FaceDirection::NegZ};
    for (FaceDirection d : directions) {
        if (quarterTurn(d) == d) {
            return false;
        }
        if (quarterTurn(quarterTurn(quarterTurn(quarterTurn(d)))) != d) {
            return false;
        }
    }
    return true;
}
static_assert(quarterTurnIsAQuarterTurn(),
              "the icon's two visible side quads would be handed the same face direction");

/// The reference's own space width, and the fallback every other glyph gets
/// before `setFontAdvances` has run. A blank cell has no rightmost column to
/// measure, so a space can only ever come from a number.
constexpr std::uint8_t kSpaceAdvance = 4;
constexpr std::uint8_t kDefaultAdvance = 6;

std::array<std::uint8_t, 128>& advanceTable() {
    // Function-local so it is initialised before first use and cannot be read
    // half-built. Written once at startup and only read afterwards.
    static std::array<std::uint8_t, 128> table = [] {
        std::array<std::uint8_t, 128> initial{};
        initial.fill(kDefaultAdvance);
        initial[' '] = kSpaceAdvance;
        return initial;
    }();
    return table;
}

/// The twelve indices of one quad, offset by the vertex it starts at.
///
/// **Every quad in this file goes through here.** Four sites used to write the
/// literal out, which is four chances to type eleven of the twelve.
void appendQuadIndices(engine::MeshData& mesh, std::uint32_t base) {
    for (const std::uint32_t index : kQuadIndices) {
        mesh.indices.push_back(base + index);
    }
}

} // namespace

void setFontAdvances(const std::array<std::uint8_t, 128>& advances) { advanceTable() = advances; }

float fontAdvance(char c) {
    const auto code = static_cast<unsigned char>(c);
    return code < advanceTable().size() ? static_cast<float>(advanceTable()[code])
                                        : static_cast<float>(kDefaultAdvance);
}

void appendQuad(engine::MeshData& mesh, float centreX, float centreY, float halfWidth, float halfHeight, float depth,
                const glm::vec4& color, float layer, bool textured) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    const glm::vec2 offsets[4]{
        {-halfWidth, -halfHeight}, {halfWidth, -halfHeight}, {halfWidth, halfHeight}, {-halfWidth, halfHeight}};
    const glm::vec2 uvs[4]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (int corner = 0; corner < 4; ++corner) {
        const glm::vec2 uv = textured ? uvs[corner] : glm::vec2{0.5f, 0.5f};
        mesh.vertices.push_back(engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y, depth},
                                               engine::packVertexColor(color.r, color.g, color.b, color.a),
                                               {uv.x, uv.y},
                                               layer,
                                               engine::kVertexSurfaceDefault});
    }

    appendQuadIndices(mesh, base);
}

void appendSprite(engine::MeshData& mesh, float centreX, float centreY, float halfWidth, float halfHeight,
                  float depth, const glm::vec2& pixelMin, const glm::vec2& pixelSize, const glm::vec2& sheetSize,
                  const glm::vec4& tint) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    const glm::vec2 uvMin = pixelMin / sheetSize;
    const glm::vec2 uvMax = (pixelMin + pixelSize) / sheetSize;

    const glm::vec2 offsets[4]{
        {-halfWidth, -halfHeight}, {halfWidth, -halfHeight}, {halfWidth, halfHeight}, {-halfWidth, halfHeight}};
    const glm::vec2 uvs[4]{{uvMin.x, uvMin.y}, {uvMax.x, uvMin.y}, {uvMax.x, uvMax.y}, {uvMin.x, uvMax.y}};

    for (int corner = 0; corner < 4; ++corner) {
        mesh.vertices.push_back(engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y, depth},
                                               engine::packVertexColor(tint.r, tint.g, tint.b, tint.a),
                                               {uvs[corner].x, uvs[corner].y},
                                               kHudLayer,
                                               engine::kVertexSurfaceDefault});
    }

    appendQuadIndices(mesh, base);
}

void appendQuadCorners(engine::MeshData& mesh, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], float depth,
                       const glm::vec4& color, float layer) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    for (int corner = 0; corner < 4; ++corner) {
        mesh.vertices.push_back(engine::Vertex{{corners[corner].x, corners[corner].y, depth},
                                               engine::packVertexColor(color.r, color.g, color.b, color.a),
                                               {uvs[corner].x, uvs[corner].y},
                                               layer,
                                               engine::kVertexSurfaceDefault});
    }

    appendQuadIndices(mesh, base);
}

void appendBlockIcon(engine::MeshData& mesh, BlockId block, float centreX, float centreY, float halfHeight,
                     float depth) {
    // Matching order for every face, so one texture-coordinate set serves all.
    const glm::vec2 uvs[4]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    // A plant is drawn flat, as the artwork actually is. Wrapping it around a
    // cube shows it as a box of grass, which is not what gets placed. A ladder,
    // a vine and a pane go the same way, for the same reason.
    //
    // **Asked of the id, not of the shape.** The reference keys a held item's
    // picture on `models/item/*.json`, which is free to disagree with the block
    // model: a torch is a post with a flame in the world and the flat
    // `block/torch` sprite in your hand. `blockShape` cannot answer both, so
    // the per-id half lives in `Block.hpp` and the dropped item asks the very
    // same function. Measured over 3315 ids: 44 differ between the shape
    // question and this one, every one of the 44 has a usable side layer, and
    // 3271 are untouched.
    if (iconIsFlat(block)) {
        const float half = halfHeight * 0.95f;
        const glm::vec2 corners[4]{{centreX - half, centreY - half},
                                   {centreX + half, centreY - half},
                                   {centreX + half, centreY + half},
                                   {centreX - half, centreY + half}};
        appendQuadCorners(mesh, corners, uvs, depth, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                          blockTextureLayer(block, BlockFace::Side));
        return;
    }

    // Isometric projection of a unit cube: x runs right-and-down, z runs
    // left-and-down, y runs straight up. Only the three faces pointing at the
    // viewer are drawn, so the icon costs three quads rather than six.
    constexpr float kIsoX = 0.866f; // cos(30 degrees)
    constexpr float kIsoY = 0.5f;   // sin(30 degrees)

    // **Each box of a multi-box icon is offset by how near it is to the eye.**
    // The renderer's UI sort is stable and runs far-first, so two boxes at one
    // depth come out in append order - which left the slab's lit top face
    // standing in front of the step that sits on it, and made a stair's upper
    // prism read with the wrong shading. The key is the box centre summed along
    // the three axes, because the view direction here is (1,1,1) and nothing
    // else about the projection matters.
    //
    // **The key is normalised across this icon's own boxes, and that is the
    // whole point.** It used to be `depth - kBoxStep * centroid`, an offset that
    // grew with how far a box sat towards the eye and so had no upper bound at
    // all. A probe over every id put the worst at 2.3747 (scaffolding), which
    // spends 0.0000475 of the 0.00005 the hotbar leaves between an icon and the
    // decorations drawn over it - so 201 blocks already reached in front of the
    // stack count's drop shadow, and 5% more reach would have taken the
    // durability bar. Nothing about the artwork was wrong; the *budget* was
    // being spent by geometry. Normalising spends exactly `kIconDepthSpan`
    // however far a box sticks out, so a model added tomorrow cannot take any
    // more of it, and the ordering is untouched because mapping a range onto a
    // range keeps every comparison it had.
    // Published in the header as `kIconDepthSpan` so each screen can assert its
    // own depth band against it. This line was the only statement of it and it
    // was private, so no caller could check the gap it was relying on.

    // Filled per icon, below, before any box is drawn. Equal when there is one
    // box, which is the common case and costs the offset nothing.
    float spreadLow = 0.0f;
    float spreadHigh = 0.0f;
    const auto centroidSum = [](const BlockBox& b) {
        return (b.minX + b.maxX + b.minY + b.maxY + b.minZ + b.maxZ) * 0.5f;
    };

    const auto project = [&](float x, float y, float z) {
        return glm::vec2{centreX + (x - z) * kIsoX * halfHeight,
                         centreY + ((x + z) * kIsoY - y) * halfHeight};
    };

    // The icon's two visible side faces point different ways, and a block whose
    // sides differ has to be told which is which - handing both the same
    // direction is what put a furnace's mouth on the right-hand face as well as
    // the front. A faceless block answers `Unknown` to both and is untouched.
    const FaceDirection frontFacing = blockFacing(block);
    const FaceDirection rightFacing = quarterTurn(frontFacing);

    // **The mesher's own texture-coordinate rules, face for face - and now
    // asserted rather than described.**
    //
    // `iconFaceMatchesMesher` above checks all three of these corner for
    // corner against `FaceGeometry.hpp`'s rows, and three negative asserts
    // watch it reject the exact faults that produce them. What follows is what
    // those asserts say, in words:
    //
    //   +Y (lid)   U along +X, V along +Z, neither flipped.
    //   +Z (front) U along +X, V along +Y **flipped**, so V grows downward.
    //   +X (right) U along +Z **flipped**, V along +Y flipped.
    //
    // **Taking the V flip and not the U flip is bug shape #5, and it was live
    // here**: the icon ran the right-hand face's U along +Z where the mesher
    // runs it along -Z, so every side texture that is not left-right symmetric
    // - a crafting table's tools, a bed's flank, any lettering - was drawn
    // mirrored on that one face while looking perfectly right in the world.
    // That is now the second negative assert, so the bug cannot come back
    // quietly.
    struct FaceUv {
        float uLow, uHigh, vLow, vHigh;
        bool flipU, flipV;
        /// Quarter turns of the face, asked of `boxFaceTurns` per face - the
        /// lid and both walls, not the lid alone. No `ModelBox` states a turn
        /// for a wall today (measured: 288 turn-bearing faces, every one a lid
        /// or a floor), so the wall half is exercised by nothing - **a fact
        /// about the tables, not a rule the mesher enforces**, and the reason
        /// it is written from the rule rather than from the data.
        unsigned char turns;
    };

    const auto writeUvs = [](const FaceUv& rule, const FaceParam (&params)[4], glm::vec2 (&out)[4]) {
        for (int corner = 0; corner < 4; ++corner) {
            const FaceParam p = turnedLidParam(params[corner], rule.turns);
            out[corner] = {iconAxisUv(rule.uLow, rule.uHigh, rule.flipU, p.u),
                           iconAxisUv(rule.vLow, rule.vHigh, rule.flipV, p.v)};
        }
    };

    // One box of the icon: the three faces an isometric view can see. `model`
    // is null for anything cut out of a cube, which samples from where the box
    // sits; a model names its own rectangle of the sheet per face.
    const auto box = [&](const BlockBox& b, const ModelBox* model) {
        const float spread = spreadHigh - spreadLow;
        const float nearness = spread > 0.0f ? (centroidSum(b) - spreadLow) / spread : 0.0f;
        const float boxDepth = depth - kIconDepthSpan * nearness;

        // A model box names its own rectangle of the sheet per face, and may
        // name its own layer with it. Where it does not, the face falls back to
        // **the block's own layer for that face** - the lid to its top and a
        // wall to its side.
        //
        // The lid used to fall back to the *side* layer instead, which left an
        // end portal frame wearing its sandstone flank where its eye socket
        // should be and reading as a plain sandy cube. The dropped-item version
        // of this same drawing had it right all along; this is the second of
        // the three places a block is drawn, and they now agree.
        // **Each drawn face asks `Block.hpp` which layer it wants**, from the
        // same accessor `ChunkMesher.cpp` reads. It used to read `sideLayer` for
        // both walls and `lidLayer` for the top, which is two answers where the
        // model now holds six - a lectern's desk and plinth take different
        // layers from its post, and the icon showed one of them everywhere.
        //
        // A `-1` reply means "take the block's own", which is what the fallback
        // resolves. The lid used to fall back to the *side* layer instead, which
        // left an end portal frame wearing its sandstone flank where its eye
        // socket should be and reading as a plain sandy cube.
        const auto faceLayer = [&](AxisFace axisFace, FaceDirection direction) {
            if (model != nullptr) {
                const float said = boxFaceLayer(*model, axisFace);
                if (said >= 0.0f) {
                    return said;
                }
            }
            if (faceAxis(axisFace) == 1) {
                return blockTextureLayer(
                    block, faceIsPositive(axisFace) ? BlockFace::Top : BlockFace::Bottom);
            }
            return blockTextureLayer(block, BlockFace::Side, direction);
        };

        // **The rectangle and the quarter turn are asked per face too.** A net
        // paints a lid somewhere other than its walls, and now paints each wall
        // somewhere other than its neighbours, so `boxFaceRect` and
        // `boxFaceTurns` are the owners of both answers.
        const auto lidRule = [&] {
            FaceRect r{0.0f, 0.0f, 1.0f, 1.0f};
            unsigned char turns = 0;
            if (model != nullptr) {
                r = boxFaceRect(*model, AxisFace::PosY);
                turns = boxFaceTurns(*model, AxisFace::PosY);
            }
            return FaceUv{r.uMin, r.uMax, r.vMin, r.vMax, false, false, turns};
        };

        // A side texture that marks a *world* direction - the red mattress at a
        // bed's join, the white pillow at its head - must not be mirrored, so a
        // model may name one face whose automatic flip is undone. `unmirror` is
        // a property of the box rather than of one face, so it is read off the
        // model rather than from the per-face tables. Compared against the same
        // two directions the layer lookup uses, and only when the model names a
        // real one: a faceless block answers `Unknown` to both, and
        // `Unknown == Unknown` would toggle every wall it has.
        const auto wallRule = [&](AxisFace axisFace, FaceDirection direction, bool flipU) {
            FaceRect r{0.0f, 0.0f, 1.0f, 1.0f};
            unsigned char turns = 0;
            if (model != nullptr) {
                r = boxFaceRect(*model, axisFace);
                turns = boxFaceTurns(*model, axisFace);
            }
            if (model != nullptr && model->unmirror != FaceDirection::Unknown &&
                model->unmirror == direction) {
                flipU = !flipU;
            }
            return FaceUv{r.uMin, r.uMax, r.vMin, r.vMax, flipU, true, turns};
        };

        glm::vec2 lid[4];
        glm::vec2 front[4];
        glm::vec2 right[4];
        writeUvs(lidRule(), kLidParams, lid);
        writeUvs(wallRule(AxisFace::PosZ, frontFacing, false), kWallParams, front);
        writeUvs(wallRule(AxisFace::PosX, rightFacing, true), kWallParams, right);

        // **The corner listings are `kIconLidCorners` and friends**, read here
        // rather than written here, so the three asserts above are about this
        // drawing and not about a description of it. Change a row and the
        // picture changes with it - and the build stops if the change puts a
        // face out of step with the block it stands for.
        const auto placed = [&](const std::uint8_t (&row)[3]) {
            return project(row[0] != 0 ? b.maxX : b.minX, row[1] != 0 ? b.maxY : b.minY,
                           row[2] != 0 ? b.maxZ : b.minZ);
        };

        const glm::vec2 topFace[4]{placed(kIconLidCorners[0]), placed(kIconLidCorners[1]),
                                   placed(kIconLidCorners[2]), placed(kIconLidCorners[3])};
        // **The mesher's own shades, not literals.** The right face was written
        // as 0.68 against the table's 0.72, so an icon was lit differently from
        // the block it stands for.
        const float topShade = kFaceShades[static_cast<std::size_t>(AxisFace::PosY)];
        const float frontShade = kFaceShades[static_cast<std::size_t>(AxisFace::PosZ)];
        const float rightShade = kFaceShades[static_cast<std::size_t>(AxisFace::PosX)];
        appendQuadCorners(mesh, topFace, lid, boxDepth, glm::vec4{topShade, topShade, topShade, 1.0f},
                          faceLayer(AxisFace::PosY, FaceDirection::Unknown));

        const glm::vec2 frontFace[4]{placed(kIconFrontCorners[0]), placed(kIconFrontCorners[1]),
                                     placed(kIconFrontCorners[2]), placed(kIconFrontCorners[3])};
        appendQuadCorners(mesh, frontFace, front, boxDepth,
                          glm::vec4{frontShade, frontShade, frontShade, 1.0f},
                          faceLayer(AxisFace::PosZ, frontFacing));

        const glm::vec2 rightFace[4]{placed(kIconRightCorners[0]), placed(kIconRightCorners[1]),
                                     placed(kIconRightCorners[2]), placed(kIconRightCorners[3])};
        appendQuadCorners(mesh, rightFace, right, boxDepth,
                          glm::vec4{rightShade, rightShade, rightShade, 1.0f},
                          faceLayer(AxisFace::PosX, rightFacing));
    };

    // A lantern and an end rod are *models*, and drawing either as a flat crop
    // of its sheet is what made the rod's slot picture a two-texel sliver. A
    // bed joins them so its icon and its dropped form are a little bed rather
    // than a cube of mattress.
    //
    // `iconIsModel`, not `usesModelIcon`: the exclusion is the whole point. The
    // flat test above already returned for a torch, so this only has to avoid
    // *re-claiming* it - but the two have to be migrated together, because one
    // of them left on the shape is how the hotbar and the floor come to
    // disagree about the same block.
    if (iconIsModel(block)) {
        const ModelBoxes model = postModel(block);
        // The range this icon's ordering key is normalised against. Measured
        // over exactly the boxes about to be drawn, so the nearest lands on
        // `depth - kIconDepthSpan` and the farthest on `depth`, whatever the
        // model's own coordinates happen to be.
        for (int i = 0; i < model.count; ++i) {
            const float sum = centroidSum(model.boxes[i].box);
            spreadLow = i == 0 ? sum : glm::min(spreadLow, sum);
            spreadHigh = i == 0 ? sum : glm::max(spreadHigh, sum);
        }
        for (int i = 0; i < model.count; ++i) {
            box(model.boxes[i].box, &model.boxes[i]);
        }
        return;
    }

    // Everything else that is a set of boxes draws as those boxes, so a fence in
    // the slot reads as a fence rather than as a cube of planks. **`iconBoxes`
    // owns that choice** - a dropped block reads the same function, so what is
    // on the floor and what is in the hotbar cannot disagree.
    const BlockBoxes parts = iconBoxes(block);
    for (int i = 0; i < parts.count; ++i) {
        const float sum = centroidSum(parts.boxes[i]);
        spreadLow = i == 0 ? sum : glm::min(spreadLow, sum);
        spreadHigh = i == 0 ? sum : glm::max(spreadHigh, sum);
    }
    for (int i = 0; i < parts.count; ++i) {
        box(parts.boxes[i], nullptr);
    }
}

float textWidth(std::string_view text, float charHeight) {
    // The scale from a cell texel to the screen. A glyph is drawn at full cell
    // size and only its *advance* varies, which is the reference's arrangement
    // and why a proportional font needs no per-glyph geometry.
    const float texel = charHeight / kFontCell;
    float width = 0.0f;
    for (const char c : text) {
        width += fontAdvance(c) * texel;
    }
    return width;
}

float appendText(engine::MeshData& mesh, std::string_view text, float leftX, float centreY, float charHeight,
                 float depth, const glm::vec4& color, bool shadow) {
    const float texel = charHeight / kFontCell;
    const float half = charHeight * 0.5f;

    // Behind the glyphs and drawn first, so a shadow can never land on top of
    // the letter in front of it.
    const int passes = shadow ? 2 : 1;
    for (int pass = 0; pass < passes; ++pass) {
        const bool drawingShadow = shadow && pass == 0;
        const glm::vec4 tint = drawingShadow ? glm::vec4{color.r * kFontShadowTint, color.g * kFontShadowTint,
                                                         color.b * kFontShadowTint, color.a}
                                             : color;
        const float shift = drawingShadow ? kFontShadowOffset * texel : 0.0f;
        const float passDepth = drawingShadow ? depth + kFontShadowDepth : depth;

        float pen = leftX;
        for (const char c : text) {
            const int code = static_cast<unsigned char>(c);
            const float advance = fontAdvance(c);
            // **Cropped to the advance, not drawn at full cell width.** A
            // full-width quad overlaps the next letter by the two columns of
            // spacing, and the later glyph is blended over that overlap - so
            // every character after the first had its left edge dimmed by the
            // transparent margin of the one before it. Cropping puts the quads
            // edge to edge again, which is what the old monospace atlas got for
            // free.
            const float columns = std::min(advance, kFontCell);
            if (code > ' ' && code < 127) {
                const glm::vec2 pixelMin{static_cast<float>(code % kFontColumns) * kFontCell,
                                         static_cast<float>(code / kFontColumns) * kFontCell};
                const glm::vec2 uvMin = pixelMin / kFontSheetSize;
                const glm::vec2 uvMax = (pixelMin + glm::vec2{columns, kFontCell}) / kFontSheetSize;

                const float halfW = columns * texel * 0.5f;
                const float centreX = pen + halfW + shift;
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                const glm::vec2 offsets[4]{{-halfW, -half}, {halfW, -half}, {halfW, half}, {-halfW, half}};
                const glm::vec2 uvs[4]{
                    {uvMin.x, uvMin.y}, {uvMax.x, uvMin.y}, {uvMax.x, uvMax.y}, {uvMin.x, uvMax.y}};

                for (int corner = 0; corner < 4; ++corner) {
                    mesh.vertices.push_back(
                        engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y + shift, passDepth},
                                       engine::packVertexColor(tint.r, tint.g, tint.b, tint.a),
                                       {uvs[corner].x, uvs[corner].y},
                                       kFontLayer,
                                       engine::kVertexSurfaceDefault});
                }

                appendQuadIndices(mesh, base);
            }
            pen += advance * texel;
        }
    }

    return textWidth(text, charHeight);
}

void appendStackDecorations(engine::MeshData& mesh, const ItemStack& stack, const glm::vec2& centre,
                            float iconHalf, float depth) {
    const float iconSize = iconHalf * 2.0f;

    // **The durability bar, `UI.md` §6.5b.** Every measurement is a sixteenth of
    // the item square, which is what the reference draws it against: two pixels
    // in from the left, thirteen wide, thirteen down, two tall, and only the
    // top row of those two is coloured.
    //
    // `damage` is overloaded and that is the trap here: on a **stowbox** it
    // counts what is inside rather than what has worn off, and one is a
    // perfectly ordinary state for a box holding one thing. Gating on a
    // *durability* the tool table actually publishes is what keeps a shulker
    // box from growing a red bar as it fills - `toolProperties` answers 0 for
    // everything that is not a tool.
    const int maxDurability = mining::toolProperties(stack.item).durability;
    if (stack.damage > 0 && maxDurability > 0) {
        const float remaining =
            glm::clamp(1.0f - static_cast<float>(stack.damage) / static_cast<float>(maxDurability), 0.0f, 1.0f);
        const float left = centre.x - iconHalf + iconSize * kBarLeft;
        const float width = iconSize * kBarWidth;
        const float top = centre.y - iconHalf + iconSize * kBarTop;
        const float height = iconSize * kBarHeight;

        // Thirteen pixels wide means thirteen states, not a continuum: rounding
        // to a whole pixel is what stops the bar dithering along its last pixel
        // and makes "one hit left" look the same on every tool.
        const float fillWidth = width * std::round(kBarSteps * remaining) / kBarSteps;
        const glm::vec4 fill{durabilityRed(remaining), durabilityGreen(remaining), 0.0f, 1.0f};

        // The black is the full two pixels and the colour only the top one, so
        // the bar keeps a dark underline however full it is. Drawn as `White`
        // rather than on the sprite sheet: there is no bar sprite, and reaching
        // for `kHudLayer` here would sample whatever happens to sit at (0,0).
        appendQuad(mesh, left + width * 0.5f, top + height * 0.5f, width * 0.5f, height * 0.5f, depth,
                   {0.0f, 0.0f, 0.0f, 1.0f}, static_cast<float>(TextureLayer::White), false);
        if (fillWidth > 0.0f) {
            const float fillHeight = iconSize * kBarFillHeight;
            appendQuad(mesh, left + fillWidth * 0.5f, top + fillHeight * 0.5f, fillWidth * 0.5f,
                       fillHeight * 0.5f, depth - kDecorationStep, fill,
                       static_cast<float>(TextureLayer::White), false);
        }
    }

    if (stack.count > 1) {
        // **Bottom right, right-aligned, with a shadow** - `UI.md` §6.5a. It sat
        // bottom *left* with no shadow, which is the one place the eye is
        // certain of, because a two-digit count then grew leftward out of its
        // own slot and over whatever was in the next one.
        //
        // The glyphs are 8 art pixels tall inside a 16-pixel item square, so the
        // text height is the *half*, not half of it again.
        const float countHeight = iconHalf;
        const std::string label = std::to_string(stack.count);
        const float left = centre.x + iconHalf - textWidth(label, countHeight);
        const float baseline = centre.y + iconHalf - countHeight * 0.5f;
        appendText(mesh, label, left, baseline, countHeight, depth - kDecorationStep * 2.0f,
                   {1.0f, 1.0f, 1.0f, 1.0f}, true);
    }
}

void appendStack(engine::MeshData& mesh, const ItemStack& stack, const glm::vec2& centre, float slotHalf,
                 float iconDepth, float countDepth) {
    if (stack.empty()) {
        return;
    }
    const float iconHalf = itemBoxHalf(slotHalf);
    if (isBlockItem(stack.item)) {
        appendBlockIcon(mesh, blockForItem(stack.item), centre.x, centre.y, iconHalf, iconDepth);
    } else if (const int layer = itemTextureLayer(stack.item); layer >= 0) {
        // Flat, because there is no block to build a little cube out of.
        appendQuad(mesh, centre.x, centre.y, iconHalf, iconHalf, iconDepth, {1.0f, 1.0f, 1.0f, 1.0f},
                   static_cast<float>(layer), true);
    }

    appendStackDecorations(mesh, stack, centre, iconHalf, countDepth);
}

void appendTooltip(engine::MeshData& mesh, std::string_view text, float cursorX, float cursorY, float aspect,
                   float charHeight, float depth) {
    if (text.empty()) {
        return;
    }

    // Proportional to the text, so the box keeps its shape whatever size the
    // label is drawn at.
    const float padding = charHeight * 0.45f;
    const float rule = charHeight * 0.11f;
    const float gap = charHeight * 0.35f;

    // Layers are a twentieth of the gap between the screen's own depth bands,
    // which is the same margin the hotbar's selected cell already relies on.
    constexpr float kLayer = 0.00005f;

    const glm::vec4 panel{0.05f, 0.03f, 0.09f, 0.94f};
    const glm::vec4 edge{0.32f, 0.16f, 0.62f, 1.0f};
    const glm::vec4 label{0.94f, 0.94f, 0.98f, 1.0f};

    const float innerHalfWidth = textWidth(text, charHeight) * 0.5f + padding;
    const float innerHalfHeight = charHeight * 0.5f + padding;
    const float outerHalfWidth = innerHalfWidth + rule * 2.0f;
    const float outerHalfHeight = innerHalfHeight + rule * 2.0f;

    // Offset down and to the right of the pointer, the way a pointer's own
    // label sits, then pulled back inside the window if that would overflow.
    float centreX = cursorX + gap + outerHalfWidth;
    float centreY = cursorY + gap + outerHalfHeight;

    centreX = std::min(centreX, aspect - outerHalfWidth);
    centreX = std::max(centreX, -aspect + outerHalfWidth);
    // Flips above the cursor rather than merely clamping, or a label near the
    // bottom edge would sit on top of the slot it describes.
    if (centreY + outerHalfHeight > 1.0f) {
        centreY = cursorY - gap - outerHalfHeight;
    }
    centreY = std::max(centreY, -1.0f + outerHalfHeight);

    const float white = static_cast<float>(TextureLayer::White);
    appendQuad(mesh, centreX, centreY, outerHalfWidth, outerHalfHeight, depth, panel, white, false);
    appendQuad(mesh, centreX, centreY, outerHalfWidth - rule, outerHalfHeight - rule, depth - kLayer, edge, white,
               false);
    appendQuad(mesh, centreX, centreY, innerHalfWidth, innerHalfHeight, depth - kLayer * 2.0f, panel, white, false);

    appendText(mesh, text, centreX - innerHalfWidth + padding, centreY, charHeight, depth - kLayer * 3.0f, label);
}

void appendPointer(engine::MeshData& mesh, float x, float y, float height, float depth) {
    // The arrow every desktop has drawn since 1984, as a fraction of its own
    // height with the tip at the origin. Nothing here needs explaining to a
    // player, which is the whole reason for using this shape.
    const glm::vec2 shape[3]{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
        glm::vec2{0.70f, 0.70f},
    };
    const glm::vec2 centroid = (shape[0] + shape[1] + shape[2]) / 3.0f;

    // Grown about the centroid rather than stroked, which keeps the tip in the
    // same place as the point being tested.
    constexpr float kOutlineGrowth = 0.22f;
    constexpr float kOutlineDepth = 0.00004f;

    const glm::vec2 uvs[4]{{0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}};
    const float white = static_cast<float>(TextureLayer::White);

    // Outline first and behind it, so the body draws over its own edge.
    for (int pass = 0; pass < 2; ++pass) {
        const bool outline = pass == 0;
        const float scale = outline ? 1.0f + kOutlineGrowth : 1.0f;

        glm::vec2 corners[4];
        for (int point = 0; point < 3; ++point) {
            const glm::vec2 placed = centroid + (shape[point] - centroid) * scale;
            corners[point] = glm::vec2{x + placed.x * height, y + placed.y * height};
        }
        // A triangle, drawn as a quad whose last two corners coincide.
        corners[3] = corners[2];

        appendQuadCorners(mesh, corners, uvs, outline ? depth + kOutlineDepth : depth,
                          outline ? glm::vec4{0.05f, 0.05f, 0.07f, 0.9f} : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                          white);
    }
}

} // namespace game::hud
