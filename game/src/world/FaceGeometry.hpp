#pragma once

#include "world/FaceShading.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace game {

// ---------------------------------------------------------------------------
// Which corner of a unit cube each vertex of each face sits on, and the texture
// rect laid across them. **One owner**, and the proofs that it reads the way
// the world does.
// ---------------------------------------------------------------------------

/// A block is drawn in four places - the world mesher, the falling cube, the
/// dropped item and the inventory icon - and this table used to be written out
/// in each of them. Every one of those copies has now been wrong at least once:
///
///  - the falling cube's shades drifted from the mesher's on five of six faces;
///  - the falling cube's corner rows were the mesher's rows **reversed**, which
///    is a pure V-flip on all 24 corners and an inward winding on all 6 faces,
///    so a falling block was upside-down and inside-out against the block that
///    had been standing there a moment earlier;
///  - the dropped item's four wall rows had their first and last pairs
///    exchanged, a horizontal mirror on every dropped and thrown block;
///  - the drop handed one side layer to all four side quads, and the icon
///    painted the side layer on the lid.
///
/// Four copies agreeing today is not four copies agreeing after the next edit,
/// and the last three of those were found by eye, in play, years of milestones
/// after they were written. So the corners live here and the readers read them.
///
/// **`ChunkMesher.cpp` is the authority this was lifted from**, unchanged: its
/// table is the one validated against the reference's `models/block/*.json` for
/// all 499 model-shaped blocks with zero disagreements.
///
/// ## The standing constraint, for whoever edits one of the four drawing paths
///
/// **This header is the single owner. A second same-signature copy of any of
/// these tables or functions, anywhere in the tree, is the bug - not a
/// convenience, not a local optimisation. If you find one, delete it and call
/// here; do not add a third.** That sentence is the part meant to outlive
/// everything below it, and it is stated as a rule rather than as a description
/// of the tree precisely so that closing the gap cannot make it read false.
///
/// **The dated state - corrected three times, every time in the same direction:
/// MORE reach than the previous version claimed.** As of 2026-08-19 12:58 all
/// four drawing paths `#include` this header and all four reach it:
///
///  - `ChunkMesher.cpp` **calls `faceCorners` once per face inside the
///    `constexpr` initialiser of its `kFaces` table**, and also reads
///    `kFaceCornerBits` and `kBottomLeftWinding`;
///  - `FallingBlock.cpp` calls `turnRunsWithTheSlots`, `faceUAxis`, `faceVAxis`
///    and `cornersOf`, and takes `FaceCorners` as a parameter type;
///  - `ItemEntity.cpp` reads `kWindingU` and `kWindingV` and calls `cornersOf`;
///  - `HudPrimitives.cpp` does both - the two axis functions and both winding
///    tables.
///
/// **`ChunkMesher.cpp` calls it from a `constexpr` initialiser**, so a change to
/// `faceCorners` is compile-time work in that translation unit, not runtime
/// work. `Copper.hpp` records this project's sweep family passing Release and
/// dying in Debug on the constant-evaluation step limit. Treat any added
/// `constexpr` here as spending that budget in four translation units at once.
///
/// **What is NOT claimed: that no second copy exists elsewhere in the tree.**
/// The measurement behind the list above searched *named files* for *named
/// symbols*, and both halves are narrower than "anywhere". A copy under a
/// different name in a file nobody thought to open answers 0 to every probe and
/// reads as migrated. **The rule above is stated as a rule for exactly that
/// reason** - it stays true whether or not the tree is clean today.
///
/// **To re-measure, search the concept and not the token.** Regenerate the
/// export surface from this file with `^(constexpr|using|inline)` - 28
/// declarations today, of which about half are consumable and the rest are the
/// proofs below - and grep candidate files for the substring `orner`
/// case-insensitively rather than for any particular name. That is what found
/// the error this paragraph corrects.
///
/// **The sweep that built the list above was wrong three times, and the third
/// diagnosis is the only one worth carrying.** The first two versions of this
/// paragraph blamed table-routing: that `ChunkMesher.cpp` "reaches this header
/// without naming a single function", so a call-site sweep misses it. **That is
/// false. It names `faceCorners` six times.** The real cause was simpler and
/// less flattering: **the sweep searched seven names and this header exports
/// about fourteen consumable ones**, and `faceCorner` and `faceCorners` differ
/// by one character.
///
/// So the rule that actually applies here is not "sweep tables as well as
/// calls", true though that is elsewhere. It is: **before building any search,
/// ask whether the thing you are looking for could be spelled a way you have
/// not typed.** No control can rescue a search that asks the wrong question -
/// a perfect instrument returning 0 for `cornersOf` says nothing whatever about
/// a file that calls `faceCorners`. **Enumerate the surface from the header
/// first, then search; do not search from memory.**
///
/// Falsified by: any consumable declaration in this file absent from the list
/// of names your sweep used. Re-derive it, do not trust this sentence.
///
/// **So do not read anything here as inert.** An earlier version of this
/// paragraph said "nothing here is reached from production code yet", and
/// acting on that - editing a row on the assumption that only the asserts below
/// would notice - changes what **four** translation units draw. **It was
/// falsified by the exact condition it named**, which is the only reason it was
/// caught: a negative claim that names its own falsifier costs five minutes to
/// re-check, and one that does not is never re-checked at all.
///
/// **What is NOT a surviving copy:** `ItemEntity.cpp` defines
/// `turnRunsWithTheSlots(const DropQuad&)`, a different parameter type
/// answering a different question, and `ChunkMesher.cpp`'s `kFaces` is an array
/// of face descriptors rather than a second corner table. Same names in the
/// neighbourhood, neither a duplicate of anything here. Do not delete either
/// looking for the copy this rule is about.
///
/// **Run the search, do not trust the number four.** Four is what the copies
/// numbered when they were collapsed; a fifth drawing path is exactly the kind
/// of thing that arrives without telling this file, and this project has
/// already paid for a block being drawn in more places than anyone listed.
///
/// **Do not search from a list of names, including any list written here.**
/// That is the mistake this comment used to make, and it is the mistake that
/// missed `ChunkMesher.cpp`: a seven-name list stood here while this header
/// exported about fourteen consumable symbols, so `faceCorners` - one character
/// from its singular sibling `faceCorner`, and called six times from a
/// `constexpr` initialiser - answered zero to every probe and read as
/// migrated. **Generate the names, then search them:**
///
///   1. `^(constexpr|using|inline)` over THIS file gives the export surface
///      (28 declarations as of 2026-08-19; the tail of them are proofs).
///   2. Search candidate files for the substring `orner`, case-insensitively,
///      as a concept net that no single spelling can slip through.
///   3. **Mark every hit CODE or COMMENT before believing it** - a definition
///      quoted inside a `///` block reads as a real one, and it produced a
///      false positive in this very file's own audit.
///   4. **Print how many probes landed, computed from the results**, so that a
///      zero reads as "absent" rather than "the search never reached the file".
///
/// A generated search finds a path nobody has told you about. A list finds only
/// what its author already knew, and silently reports the rest as clean.
///
/// **What the person deleting a local copy needs to know, if a fifth path ever
/// arrives with one, because it is not visible from either file alone:** such a
/// copy sits in an anonymous namespace, so at its own call sites it *shadows*
/// this header rather than overloading against it - which is why the two can
/// coexist and why no ordering of the two edits breaks the build. It also means
/// **deleting one silently switches that file over to this header**, with no
/// diagnostic either way. The check that matters is therefore not that it
/// compiles but that the copy you removed agreed with this one; where it did
/// not, this header is the authority and the difference is a bug you have just
/// fixed, so say which face changed.

/// 0 or 1 per axis, four vertices per face, in `AxisFace` order. Unitless -
/// these are corners of a unit cube, scaled and offset by whoever draws it.
///
/// **WARNING TO WHOEVER EDITS A ROW BELOW: other files `static_assert` against
/// these numbers and none of it is visible from here.** Changing a row does not
/// fail here; it fails in a file you are not looking at, with a message about a
/// derivation you did not touch. As of 2026-08-19 the dependants are:
///
///  - `FallingBlock.cpp`, `bothTurnDerivationsAgree()` - walks all six faces
///    comparing this header's `turnRunsWithTheSlots` against that file's own
///    float-corner derivation. A row edit that changes a face's turn direction
///    breaks it, and the message will talk about falling blocks.
///  - `HudPrimitives.cpp`, `iconFaceMatchesMesher()` - three positive asserts
///    (`PosY`, `PosZ`, `PosX`) and three negative controls, all reading
///    `cornersOf` and `kWindingU`/`kWindingV` through `mesherCornerUv()`.
///    The negative controls mean a row edit can fail it in EITHER direction:
///    breaking a match, or accidentally making a control match.
///
/// **If your edit is correct and one of those fires, the assert is what must
/// move - not this table.** Both are derivations of these rows, so this table
/// is the premise and they are the conclusions.
///
/// **Cited by symbol, never by line, on purpose.** Falsified by: any file
/// naming `kFaceCornerBits`, `cornersOf`, `faceCorners`, `kWindingU` or
/// `kWindingV` inside a `static_assert` and absent from the two above. Re-derive
/// with the generated search described earlier in this file rather than
/// trusting this list - a list of dependants rots exactly as fast as the
/// seven-name search list that already failed once here.
inline constexpr std::uint8_t kFaceCornerBits[static_cast<std::size_t>(AxisFace::Count)][4][3]{
    {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, // +X
    {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, // -X
    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, // +Y
    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, // -Y
    {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, // +Z
    {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, // -Z
};

/// The texture rect laid across those four corners, as integers, so the proofs
/// below can be arithmetic rather than float comparison. Bottom-left first:
/// u runs 0,1,1,0 and v runs 1,1,0,0 across a quad's four vertices.
///
/// **V grows downward**, matching how image rows are stored - so `v = 0` is the
/// *top* row of the texture and belongs on the *higher* pair of corners. That
/// single sentence is the whole of the falling block's bug.
inline constexpr int kWindingU[4]{0, 1, 1, 0};
inline constexpr int kWindingV[4]{1, 1, 0, 0};

constexpr std::array<glm::vec2, 4> makeBottomLeftWinding() {
    return {glm::vec2{static_cast<float>(kWindingU[0]), static_cast<float>(kWindingV[0])},
            glm::vec2{static_cast<float>(kWindingU[1]), static_cast<float>(kWindingV[1])},
            glm::vec2{static_cast<float>(kWindingU[2]), static_cast<float>(kWindingV[2])},
            glm::vec2{static_cast<float>(kWindingU[3]), static_cast<float>(kWindingV[3])}};
}

/// The same rect the mesher hands every quad, built from the integers above so
/// there is one winding rather than two that agree today.
inline constexpr std::array<glm::vec2, 4> kBottomLeftWinding = makeBottomLeftWinding();

/// **The one silent join in the pair above.** `makeBottomLeftWinding` does not
/// loop - it names indices 0, 1, 2 and 3 by hand - so there are three literal
/// fours here: this array's size, that function's return type, and the length
/// of `kWindingU`/`kWindingV`. **Two of the three are loud**: a size that
/// disagrees with the return type is a type mismatch and cannot compile.
/// **The third is silent.** Lengthen `kWindingU` and `kWindingV` and the
/// function keeps reading four entries, dropping the new one with no
/// diagnostic of any kind - the shape this project has paid for twice as "two
/// unlinked literal fours".
///
/// `sizeof` rather than `std::size` deliberately, so this needs no header that
/// is not already here.
static_assert(kBottomLeftWinding.size() == sizeof(kWindingU) / sizeof(kWindingU[0]),
              "kBottomLeftWinding no longer covers kWindingU - if you lengthened the "
              "winding tables, makeBottomLeftWinding still names indices 0..3 by hand "
              "and is silently dropping the rest; add the entries there too");
static_assert(sizeof(kWindingU) == sizeof(kWindingV),
              "kWindingU and kWindingV must stay the same length - they are read as a "
              "pair, one index at a time, so a longer one is read past its partner");

/// Which world axis a face looks along: 0 is X, 1 is Y, 2 is Z.
constexpr int faceAxis(AxisFace face) { return static_cast<int>(face) / 2; }

/// Whether it looks along the positive end of that axis.
constexpr bool faceIsPositive(AxisFace face) { return (static_cast<int>(face) % 2) == 0; }

/// The outward normal of a face, derived from the enumerator rather than stored.
constexpr glm::ivec3 faceOutwardNormal(AxisFace face) {
    const int sign = faceIsPositive(face) ? 1 : -1;
    const int axis = faceAxis(face);
    return {axis == 0 ? sign : 0, axis == 1 ? sign : 0, axis == 2 ? sign : 0};
}

/// One corner as a float position on the unit cube.
constexpr glm::vec3 faceCorner(AxisFace face, int vertex) {
    const auto& bits = kFaceCornerBits[static_cast<std::size_t>(face)][vertex];
    return {static_cast<float>(bits[0]), static_cast<float>(bits[1]), static_cast<float>(bits[2])};
}

/// A face's four corners, counter-clockwise seen from outside, paired with
/// `kBottomLeftWinding` in that order.
constexpr std::array<glm::vec3, 4> faceCorners(AxisFace face) {
    return {faceCorner(face, 0), faceCorner(face, 1), faceCorner(face, 2), faceCorner(face, 3)};
}

/// Which world axis the texture's `u` runs along, and which its `v` runs along.
/// A face spans the two axes it does not look along; `u` takes the horizontal
/// one and `v` the other, so on a wall `v` is world Y and on a lid - which has
/// no vertical axis left to take - `u` runs east along X and `v` south along Z.
///
/// **These two lines were lifted from four separate `.cpp` files** - the
/// mesher's `kFaces` columns, the falling cube, the dropped item and the HUD -
/// which is the same four-copy shape that produced every bug listed at the top
/// of this header, and the standing constraint stated there governs them too.
/// `uvAxesAreTheOnesThatMove` below proves they name the axes
/// the corner table actually moves along, rather than merely asserting it.
constexpr int faceUAxis(AxisFace face) { return faceAxis(face) == 0 ? 2 : 0; }
constexpr int faceVAxis(AxisFace face) { return faceAxis(face) == 1 ? 2 : 1; }

/// Whether a quarter turn of a texture walks the four uv slots forward or
/// backward - that is, whether the map from the face's own `(u, v)` to the
/// image's `(U, V)` keeps its handedness or mirrors it.
///
/// One vertex is enough because the map is axis-aligned: `U` runs along
/// `faceUAxis` and `V` along `faceVAxis` with no swap, which is exactly what
/// `uvAxesAreTheOnesThatMove` checks, so all that is left to learn is whether
/// each of the two is reversed. Both reversed or neither is a rotation and
/// preserves handedness; exactly one is a mirror and inverts it.
///
/// **This is the rule that was right in `FallingBlock.cpp` and wrong in
/// `ItemEntity.cpp` for three hours, forty lines apart in the same directory** -
/// `CLAUDE.md` bug shape #14, a rule that is correct and commented in only one
/// of the places that needs it. The fix for that shape is one owner.
constexpr bool turnRunsWithTheSlots(AxisFace face) {
    const auto& c = kFaceCornerBits[static_cast<std::size_t>(face)];
    return (c[0][faceUAxis(face)] != kWindingU[0]) == (c[0][faceVAxis(face)] != kWindingV[0]);
}

// ---------------------------------------------------------------------------
// The proofs. Each states a rule about the *world* and checks a table against
// it, rather than checking the table against a second copy of itself - a
// `static_assert` comparing one side of a derivation against the other side of
// the same derivation passes on a wrong table, which this project has paid for.
//
// **They take the corners as a parameter on purpose.** A proof that can only
// be aimed at one table cannot be shown to fail on a wrong one, and a proof
// nobody has watched fail is indistinguishable from a tautology. Pointed at the
// falling block's old rows - this table's rows reversed - all three reject
// them; that was measured, not assumed. It also means the three other places a
// block is drawn can be held to the same rules without copying them.
// ---------------------------------------------------------------------------

/// Four vertices, three 0/1 bits each: the shape of one row of the table above.
using FaceCorners = const std::uint8_t (&)[4][3];

constexpr FaceCorners cornersOf(AxisFace face) {
    return kFaceCornerBits[static_cast<std::size_t>(face)];
}

/// That every vertex of a face lies *on* that face, that the four are distinct,
/// and that the enumerator's own name agrees with where the corners are. This
/// is what welds `AxisFace`'s order to the table: get the order wrong and every
/// face is told it looks along the wrong axis.
constexpr bool faceIsWhereItsNameSays(AxisFace face, FaceCorners corners) {
    const int axis = faceAxis(face);
    const int side = faceIsPositive(face) ? 1 : 0;
    for (int c = 0; c < 4; ++c) {
        if (corners[c][axis] != side) {
            return false;
        }
        for (int other = 0; other < c; ++other) {
            if (corners[c][0] == corners[other][0] && corners[c][1] == corners[other][1] &&
                corners[c][2] == corners[other][2]) {
                return false;
            }
        }
    }
    return true;
}

/// That the first triangle - `(0, 1, 2)`, which is what every reader emits -
/// turns counter-clockwise *seen from outside*, so it survives back-face
/// culling. The pipeline is `VK_FRONT_FACE_COUNTER_CLOCKWISE` with
/// `VK_CULL_MODE_BACK_BIT`, so a face wound the other way is not drawn at all
/// and the viewer sees the inside of the far side of the box instead.
///
/// Derived from `faceOutwardNormal`, so reversing a corner list fails it rather
/// than quietly agreeing with it.
constexpr bool windsOutward(AxisFace face, FaceCorners corners) {
    const int ax = corners[1][0] - corners[0][0];
    const int ay = corners[1][1] - corners[0][1];
    const int az = corners[1][2] - corners[0][2];
    const int bx = corners[2][0] - corners[0][0];
    const int by = corners[2][1] - corners[0][1];
    const int bz = corners[2][2] - corners[0][2];
    const glm::ivec3 normal = faceOutwardNormal(face);
    const int cross[3]{ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx};
    return cross[0] * normal.x + cross[1] * normal.y + cross[2] * normal.z > 0;
}

/// **The rule the V-flip broke, stated without reference to any table.**
///
/// Seen from outside, a wall's texture has to read left to right and top to
/// bottom: the vertex carrying `u = 1` lies to the viewer's right of the one
/// carrying `u = 0`, and the vertex carrying `v = 0` is the *higher* of its
/// pair, because `v = 0` is the top row of the image.
///
/// "Right" is `cross(-normal, +Y)` - the direction a viewer facing the wall
/// with their head up calls right - which for a face looking along X or Z
/// comes out as `(normal.z, 0, -normal.x)`. A lid has no world up to derive
/// from and is excused here; `lidReadsNorthUp` covers those two instead.
constexpr bool wallReadsLeftToRightFromOutside(AxisFace face, FaceCorners corners) {
    if (faceAxis(face) == 1) {
        return true;
    }
    const glm::ivec3 normal = faceOutwardNormal(face);
    const glm::ivec3 right{normal.z, 0, -normal.x};
    const int alongRight =
        (corners[1][0] - corners[0][0]) * right.x + (corners[1][2] - corners[0][2]) * right.z;
    // Slots 0 and 1 carry v = 1, slots 3 and 2 carry v = 0, so both of those
    // pairs must rise from the first to the second.
    const bool topRowIsHigher = corners[3][1] > corners[0][1] && corners[2][1] > corners[1][1];
    return alongRight > 0 && topRowIsHigher;
}

/// The same for the two lids, which have no "up" to cross against.
///
/// **This one is a stated convention rather than a derivation, and saying so is
/// the point** - it is weaker than the wall proof above and must not be read as
/// equally strong. The convention is the reference's: a top texture's *north*
/// edge is the top row of its image, which is what makes a furnace top, a
/// grass path, farmland and every other directional lid look right. So on the
/// top face u runs east (+X) and v runs south (+Z), since v grows downward;
/// and the bottom face is the top rotated about X to be looked at from below,
/// which reflects v and leaves u alone.
///
/// Corroborated by two things that were measured rather than assumed: the
/// mesher's own `uAxis`/`vAxis` columns name X and Z for both lids and greedy
/// merging tiles by them, and the icon, drop and mesher paths were verified to
/// agree on all 499 model-shaped blocks.
constexpr bool lidReadsNorthUp(AxisFace face, FaceCorners corners) {
    if (faceAxis(face) != 1) {
        return true;
    }
    const bool uRunsEast = corners[1][0] > corners[0][0];
    // Slot 0 carries v = 1 and slot 3 carries v = 0 at the same u, so v grows
    // from slot 3 towards slot 0.
    const bool vRunsSouth = corners[0][2] > corners[3][2];
    return uRunsEast && (vRunsSouth == faceIsPositive(face));
}

/// That `faceUAxis` and `faceVAxis` name the axes the corner table actually
/// moves along - which is what licenses `turnRunsWithTheSlots` reading a single
/// vertex. Two slots carrying the same texture `u` must sit at the same world
/// `u`, two carrying different ones must differ, and the same for `v`; that
/// holds only if `U` runs purely along `faceUAxis` and `V` purely along
/// `faceVAxis`, with no swap and no diagonal.
///
/// **Weaker than the three rules above, and saying so is the point**: it is
/// blind to reversal, because a reversed row moves along the same two axes. A
/// reversed row passes this and fails `windsOutward`, which is why both exist.
constexpr bool uvAxesAreTheOnesThatMove(AxisFace face, FaceCorners corners) {
    const int u = faceUAxis(face);
    const int v = faceVAxis(face);
    if (u == v || u == faceAxis(face) || v == faceAxis(face)) {
        return false;
    }
    for (int a = 0; a < 4; ++a) {
        for (int b = 0; b < 4; ++b) {
            if ((kWindingU[a] == kWindingU[b]) != (corners[a][u] == corners[b][u])) {
                return false;
            }
            if ((kWindingV[a] == kWindingV[b]) != (corners[a][v] == corners[b][v])) {
                return false;
            }
        }
    }
    return true;
}

/// Every rule above, against one candidate row. This is what a second drawing
/// path should call on its own table.
constexpr bool faceReadsAsTheWorldDoes(AxisFace face, FaceCorners corners) {
    return faceIsWhereItsNameSays(face, corners) && windsOutward(face, corners) &&
           wallReadsLeftToRightFromOutside(face, corners) && lidReadsNorthUp(face, corners);
}

constexpr bool everyFaceHolds() {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        const auto face = static_cast<AxisFace>(i);
        if (!faceReadsAsTheWorldDoes(face, cornersOf(face))) {
            return false;
        }
    }
    return true;
}

static_assert(static_cast<std::size_t>(AxisFace::Count) == 6, "a box has six faces");
static_assert(sizeof(kFaceCornerBits) / sizeof(kFaceCornerBits[0]) ==
                  static_cast<std::size_t>(AxisFace::Count),
              "one corner row per face and no more");
static_assert(everyFaceHolds(),
              "the cube's corner table no longer reads the way the world does - a reversed or "
              "scrambled row flips the texture, mirrors it, or turns the face inside out");

// The three proofs are separate on purpose: each catches something the others
// pass. Reversing a row keeps every corner on its own face, so only the last
// two catch it; mirroring a row keeps the winding, so only the last one does;
// and moving a corner to the wrong face keeps both rect rules, so only the
// first one does.
//
// **Two of those three claims are now shown failing at the bottom of this file
// and the third is not** - `everyReversedRowIsRejected` and
// `everyMirroredRowIsRejected` exist; nothing feeds in a row with a corner
// moved to the wrong face. That third one is the cheapest of the three to
// write and the least likely to have shipped, which is why it is last rather
// than forgotten. **Falsified by an `everyOffFaceRowIsRejected` appearing
// beside the other two** - if you write it, delete this paragraph rather than
// adding a second note. Dated 2026-08-19.
static_assert(faceIsWhereItsNameSays(AxisFace::PosX, cornersOf(AxisFace::PosX)) &&
                  faceIsWhereItsNameSays(AxisFace::NegY, cornersOf(AxisFace::NegY)),
              "a face's corners left the face its name says it is");
static_assert(windsOutward(AxisFace::PosX, cornersOf(AxisFace::PosX)) &&
                  windsOutward(AxisFace::NegZ, cornersOf(AxisFace::NegZ)),
              "a face is wound inward and would be culled");
static_assert(wallReadsLeftToRightFromOutside(AxisFace::NegZ, cornersOf(AxisFace::NegZ)),
              "a wall's texture is mirrored or upside-down against the world");
static_assert(lidReadsNorthUp(AxisFace::PosY, cornersOf(AxisFace::PosY)) &&
                  lidReadsNorthUp(AxisFace::NegY, cornersOf(AxisFace::NegY)),
              "a lid's texture no longer has north at the top of the image");

constexpr bool everyFaceUvAxisHolds() {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        const auto face = static_cast<AxisFace>(i);
        if (!uvAxesAreTheOnesThatMove(face, cornersOf(face))) {
            return false;
        }
    }
    return true;
}

static_assert(everyFaceUvAxisHolds(),
              "faceUAxis or faceVAxis names an axis the corner table does not move along, so "
              "turnRunsWithTheSlots is reading a vertex that cannot answer the question");

/// **A pin, not a proof, and the difference matters** - the right-hand side was
/// read off this table rather than derived from anywhere else, so it restates
/// today's six answers rather than justifying them. What it buys is that a
/// later edit to `kFaceCornerBits` which silently flips one face's turn
/// direction is announced here instead of appearing as a rotated texture on one
/// dropped item that nobody throws. The odd pair is Z: every positive face runs
/// with the slots except `PosZ`, and every negative one runs against except
/// `NegZ`. If this fires, work out which side moved before touching either.
constexpr bool turnDirectionsAreWhereTheyWere() {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        const auto face = static_cast<AxisFace>(i);
        if (turnRunsWithTheSlots(face) != (faceIsPositive(face) != (faceAxis(face) == 2))) {
            return false;
        }
    }
    return true;
}

static_assert(turnDirectionsAreWhereTheyWere(),
              "a face's texture turn direction has changed - three faces used to run with the uv "
              "slots and three against, and one of them has moved");

/// **The proofs, shown failing.** Reversing a row is exactly what the falling
/// block's table did, so this is the real defect held up against the rule that
/// now rejects it - and it is `constexpr`, so it cannot rot into agreement.
constexpr bool reversedRowIsRejected(AxisFace face) {
    const std::uint8_t reversed[4][3]{
        {cornersOf(face)[3][0], cornersOf(face)[3][1], cornersOf(face)[3][2]},
        {cornersOf(face)[2][0], cornersOf(face)[2][1], cornersOf(face)[2][2]},
        {cornersOf(face)[1][0], cornersOf(face)[1][1], cornersOf(face)[1][2]},
        {cornersOf(face)[0][0], cornersOf(face)[0][1], cornersOf(face)[0][2]},
    };
    // Still on its own face - which is why the first proof alone was not enough.
    return faceIsWhereItsNameSays(face, reversed) && !windsOutward(face, reversed) &&
           !faceReadsAsTheWorldDoes(face, reversed);
}

constexpr bool everyReversedRowIsRejected() {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        if (!reversedRowIsRejected(static_cast<AxisFace>(i))) {
            return false;
        }
    }
    return true;
}

static_assert(everyReversedRowIsRejected(),
              "the proofs above accept a face wound backwards, so they would have passed on the "
              "bug they exist to catch");

/// **The second proof, shown failing - and this is the defect that actually
/// shipped.** A horizontal mirror keeps every corner on its own face *and*
/// keeps the winding, so neither of the first two rules can see it and only the
/// rect rules can. That is precisely what the dropped item's four wall rows did
/// with their first and last pairs exchanged, mirroring every dropped and
/// thrown block for four milestones.
///
/// The mirror is `{c2, c3, c0, c1}` - a rotation of the cyclic order by two,
/// and **that is why the winding survives it**: rotating a quad's vertices
/// preserves orientation where reversing them inverts it. The argument is
/// geometric rather than per-face, so it holds on all six without sampling one
/// and hoping. **So this asserts the winding survives rather than merely
/// tolerating it** - the second conjunct is a control that fires in the
/// opposite direction to the third, which is what makes the pair non-vacuous.
///
/// Rejection then comes from whichever rect rule owns the face, and each face
/// has exactly one because both rules return `true` off their own shape: a wall
/// fails `wallReadsLeftToRightFromOutside`, whose `alongRight` measures slot 1
/// minus slot 0 along the u direction and sees it reversed; a lid fails
/// `lidReadsNorthUp`, whose bare `uRunsEast` conjunct flips for the same reason.
constexpr bool mirroredRowIsRejected(AxisFace face) {
    const std::uint8_t mirrored[4][3]{
        {cornersOf(face)[2][0], cornersOf(face)[2][1], cornersOf(face)[2][2]},
        {cornersOf(face)[3][0], cornersOf(face)[3][1], cornersOf(face)[3][2]},
        {cornersOf(face)[0][0], cornersOf(face)[0][1], cornersOf(face)[0][2]},
        {cornersOf(face)[1][0], cornersOf(face)[1][1], cornersOf(face)[1][2]},
    };
    return faceIsWhereItsNameSays(face, mirrored) && windsOutward(face, mirrored) &&
           !faceReadsAsTheWorldDoes(face, mirrored);
}

constexpr bool everyMirroredRowIsRejected() {
    for (int i = 0; i < static_cast<int>(AxisFace::Count); ++i) {
        if (!mirroredRowIsRejected(static_cast<AxisFace>(i))) {
            return false;
        }
    }
    return true;
}

static_assert(everyMirroredRowIsRejected(),
              "a horizontally mirrored face is being accepted - that is the bug that shipped on "
              "every dropped block, the winding rules cannot see it, and only "
              "wallReadsLeftToRightFromOutside and lidReadsNorthUp can");

} // namespace game
