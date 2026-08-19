#include "world/ItemEntity.hpp"

#include "item/SpriteModel.hpp"

#include "world/Collision.hpp"
#include "world/FaceGeometry.hpp"
#include "world/FaceShading.hpp"
#include "world/Fluid.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace game {
namespace {

constexpr float kGravity = 22.0f;
/// Half a drop's extent. The reference renders one at a quarter of a block;
/// ours is larger on purpose, because a dropped tool at that size is hard to
/// pick out of grass. It is the collision box as well as the model, so the two
/// cannot disagree about how big the thing is.
constexpr float kHalfSize = 0.15f;
constexpr float kTerminalVelocity = 24.0f;

/// How fast a drop rises to the surface. Gentle on purpose - the reference's
/// buoyancy bobs an item up rather than firing it out of the water.
constexpr float kFloatSpeed = 0.6f;

/// Stops a drop being collected the instant it leaves the block, which would
/// make breaking look like the item never existed. Thrown items pass a longer
/// one, so they clear the attraction radius before it applies.
constexpr float kAttractRadius = 2.0f;
constexpr float kCollectRadius = 0.7f;
constexpr float kAttractSpeed = 7.0f;

constexpr float kBobHeight = 0.07f;
constexpr float kBobSpeed = 2.2f;
constexpr float kSpinSpeed = 1.1f;

/// How much speed a landing keeps, and how slow an impact has to be before the
/// drop simply stops.
///
/// This is a *deliberate* bounce, unlike the accidental ones that come from
/// resting an object above its surface. It terminates because restitution is
/// below one and anything slower than the threshold settles outright, so do not
/// remove the threshold to make it bouncier.
constexpr float kRestitution = 0.42f;
constexpr float kSettleSpeed = 2.0f;
constexpr float kGroundFriction = 0.6f;

/// Five minutes, as the reference has it. Ours runs on wall time rather than
/// pausing when a chunk stops ticking, because drops are not saved anyway.
constexpr float kDespawnSeconds = 300.0f;

/// A single frame may not advance a drop further than this. **The same 0.05 the
/// player (`Player.cpp`'s own `kMaxDeltaSeconds`) and the creatures
/// (`Creature.cpp`) already clamp to**, and deliberately not a new number: they
/// all resolve against the same world with the same one-block-deep assumption,
/// so a frame that is too long for one is too long for all three.
constexpr float kMaxDeltaSeconds = 0.05f;

/// Longest slice of a move - on **any** axis - that may be tested in one go.
///
/// Consecutive test boxes have to overlap, or a wall thinner than the gap
/// between two of them falls straight through the sweep - so this may never
/// exceed a drop's own extent. The `static_assert` is what makes shrinking
/// `kHalfSize` back toward the reference's quarter-block safe: it fails rather
/// than quietly reopening the tunnelling bug.
constexpr float kMaxSweepStep = 0.25f;
static_assert(kMaxSweepStep <= kHalfSize * 2.0f,
              "sweep slices must overlap, or a thin wall falls between two of them");
/// And the clamp has to be tight enough that the sweep cannot run away: a
/// clamped frame at terminal velocity covers 1.2 m, which is five slices. The
/// vertical axis is the fastest of the three - nothing throws a drop sideways
/// faster than it falls - so bounding it bounds all of them.
static_assert(kMaxDeltaSeconds * kTerminalVelocity / kMaxSweepStep <= 8.0f,
              "a clamped fall would need more sweep slices than intended");

// ---------------------------------------------------------------------------
// The six faces of a drop's box, and the proofs that they read the same way
// round as the world's do.
// ---------------------------------------------------------------------------

/// Which face of the shared cube table each of a drop's six quads draws, and
/// which world direction that quad points.
///
/// **The corners are not here and never will be again.** They live in
/// `world/FaceGeometry.hpp`, which owns the one cube-corner table and carries
/// the proofs that it reads the way the world does; this table holds only the
/// two facts that table cannot know - which of its rows a quad is, and which
/// wall that makes it for `blockTextureLayer`. Everything geometric - corners,
/// winding, axis, shade - is read from the shared owner rather than repeated.
///
/// This file *did* hold a transcription, written out as six calls with the
/// corners at each one, and the four side quads came out as the mesher's rows
/// with the first pair and the last pair exchanged. That permutation is exactly
/// a horizontal mirror, so **every dropped and thrown block read its side art
/// backwards**: a crafting table, a furnace, a TNT block, a bed and a lectern
/// all wore their sides the wrong way round, from the day drops stopped being
/// single cubes. The two lids were transcribed correctly, which is why it
/// survived - a lid is symmetric far more often than a side is, and the two
/// bugs this same path was caught by on 2026-08-11 were both side bugs as well.
/// It was not fixed by correcting the transcription; it was fixed by deleting
/// it.
struct DropQuad {
    /// Names the row of the shared table this quad draws. Everything geometric
    /// - corners, winding, axis, shade - is read from it rather than repeated.
    AxisFace face;
    /// **A real direction, one per wall.** `blockTextureLayer` decides "is this
    /// the front" by comparing what it is handed against the block's own
    /// facing, so a path that can only be told a single answer says "front"
    /// four times - which is how a sticky piston wore its plate right round the
    /// block. `Unknown` on the two lids, exactly as the mesher's table has it.
    FaceDirection direction;
};

constexpr DropQuad kDropQuads[]{
    {AxisFace::NegZ, FaceDirection::NegZ}, {AxisFace::PosZ, FaceDirection::PosZ},
    {AxisFace::PosX, FaceDirection::PosX}, {AxisFace::NegX, FaceDirection::NegX},
    {AxisFace::PosY, FaceDirection::Unknown}, {AxisFace::NegY, FaceDirection::Unknown},
};
constexpr int kDropQuadCount = static_cast<int>(sizeof(kDropQuads) / sizeof(kDropQuads[0]));
static_assert(kDropQuadCount == 6, "a box has six faces");

/// Which axis of the box the texture's u and v run along, by the mesher's own
/// rule: u is X unless the face looks along X, in which case it is Z; v is Y
/// unless the face looks along Y, in which case it is Z.
constexpr int quadUAxis(const DropQuad& q) { return faceAxis(q.face) == 0 ? 2 : 0; }
constexpr int quadVAxis(const DropQuad& q) { return faceAxis(q.face) == 1 ? 2 : 1; }

/// **Whether a quarter turn walks the four uv slots forward or backward.**
///
/// A turn is stated in the *texture's* space, and half the cube's faces
/// parameterise uv as a mirror of the other half's, so on those faces the same
/// turn runs the slots the other way. The rule is `flipU == flipV` - read off
/// the shared corner table, not tabulated - and it is emphatically **not**
/// `faceIsPositive`, which agrees on `+X`, `-X`, `+Y` and `-Y` and is backwards
/// on both `Z` walls.
///
/// This file used to shift every wall forward, which is wrong on `-X` and `+Z`.
/// Latent, because no shipping box states a wall turn - but the whole point of
/// asking `boxFaceTurns` per face was to be ready for the first one that does.
///
/// **`FallingBlock.cpp` owns the proof**, and it is the real kind: the same
/// rule run against `ChunkMesher.cpp`'s own fu/fv arithmetic over 4 walls x 3
/// non-zero turns x 2 mirrors, with the counted negative twin that shows
/// `faceIsPositive` failing that identical sweep. The pair below is a
/// regression guard on this file's copy of the rule, not a second proof of it.
///
/// **The promotion this note used to ask for has happened.** `faceUAxis`,
/// `faceVAxis` and `turnRunsWithTheSlots(AxisFace)` now live in
/// `FaceGeometry.hpp` beside the corner table they are derived from, and the
/// byte-identical copies that sat in `FallingBlock.cpp` and `HudPrimitives.cpp`
/// were deleted on 2026-08-19. **This overload is not one of those copies** and
/// is not a candidate for deletion: it takes a `DropQuad`, and it reads
/// `quadUAxis`/`quadVAxis` rather than the face axes, because a quad can carry
/// an axis assignment its face alone does not determine. It is an adapter with
/// a different input, which is a different thing from a second answer to the
/// same question.
///
/// **What it should not do is re-derive the rule.** The body below repeats
/// `flipU == flipV` rather than asking the header, and it is left that way only
/// because narrowing a `DropQuad` to an `AxisFace` is not a change worth making
/// without a compiler. If the two ever disagree, the header is right.
///
/// A note asking for work that is already done is the most expensive kind of
/// stale comment: the reader who believes it goes and does the work again, and
/// two definitions of one rule is precisely what the promotion existed to end.
constexpr bool turnRunsWithTheSlots(const DropQuad& q) {
    FaceCorners corners = cornersOf(q.face);
    const bool flipU = corners[0][quadUAxis(q)] != kWindingU[0];
    const bool flipV = corners[0][quadVAxis(q)] != kWindingV[0];
    return flipU == flipV;
}

/// The direction each face takes, in `AxisFace` order, as certified by
/// `FallingBlock.cpp`'s sweep against the mesher. Indexed by the enumerator
/// rather than by this file's quad order, so re-ordering `kDropQuads` cannot
/// silently re-label it.
constexpr bool kTurnRunsForward[static_cast<std::size_t>(AxisFace::Count)]{
    true,  // +X
    false, // -X
    true,  // +Y
    false, // -Y
    false, // +Z
    true,  // -Z
};

/// How many faces a candidate shift rule gets wrong, optionally counting one
/// axis only. Taking the rule as a parameter is what makes the negative test
/// below real: the true rule and the wrong one go through the identical
/// comparison, so a disagreement is about the rule and nothing else.
constexpr int facesTheRuleGetsWrong(bool (*rule)(const DropQuad&), int onlyAxis) {
    int wrong = 0;
    for (int i = 0; i < kDropQuadCount; ++i) {
        const DropQuad& q = kDropQuads[i];
        if (onlyAxis >= 0 && faceAxis(q.face) != onlyAxis) {
            continue;
        }
        if (rule(q) != kTurnRunsForward[static_cast<std::size_t>(q.face)]) {
            ++wrong;
        }
    }
    return wrong;
}

/// The rule this file actually shipped until tonight - every wall forward, the
/// floor backward - and the rule it was nearly "corrected" to. Both are kept
/// solely so the comparison above has something to reject, and they fail it
/// with *different* profiles: `faceIsPositive` misses both `Z` walls, the old
/// rule misses `-X` and `+Z`, one on each of two axes. Two controls that both
/// returned the same number would be an instrument reading, not a measurement.
constexpr bool everyWallForward(const DropQuad& q) {
    return !(faceAxis(q.face) == 1 && !faceIsPositive(q.face));
}
constexpr bool positiveFaceRule(const DropQuad& q) { return faceIsPositive(q.face); }

static_assert(facesTheRuleGetsWrong(turnRunsWithTheSlots, -1) == 0,
              "a dropped block's turned face no longer paints the rectangle the world mesher "
              "paints - flipU == flipV is the rule, see FallingBlock.cpp's proof against the "
              "mesher's own arithmetic");
static_assert(facesTheRuleGetsWrong(positiveFaceRule, -1) == 2 &&
                  facesTheRuleGetsWrong(positiveFaceRule, 2) == 2,
              "faceIsPositive has stopped failing on exactly the two Z walls, so the comparison "
              "above is no longer able to tell the two rules apart");
static_assert(facesTheRuleGetsWrong(everyWallForward, -1) == 2 &&
                  facesTheRuleGetsWrong(everyWallForward, 0) == 1 &&
                  facesTheRuleGetsWrong(everyWallForward, 2) == 1,
              "the rule this file shipped until tonight now passes, so either it was not the bug "
              "or the comparison stopped measuring");

/// That a rect written bottom-left first lands on the right corners.
///
/// The mesher works out its two flips from vertex 0 and then trusts the other
/// three to follow; this asserts they do, against `FaceGeometry.hpp`'s winding
/// rather than a second copy of it. It catches a scrambled corner list - but
/// **not** a mirror, because a mirrored quad is still a consistent bottom-left
/// quad. That is what the shared `wallReadsLeftToRightFromOutside` is for, and
/// the two are deliberately kept as separate proofs.
constexpr bool quadReadsAsWritten(const DropQuad& q) {
    const int ua = quadUAxis(q);
    const int va = quadVAxis(q);
    FaceCorners corners = cornersOf(q.face);
    const bool flipU = corners[0][ua] != kWindingU[0];
    const bool flipV = corners[0][va] != kWindingV[0];
    for (int c = 0; c < 4; ++c) {
        const int u = flipU ? 1 - corners[c][ua] : corners[c][ua];
        const int v = flipV ? 1 - corners[c][va] : corners[c][va];
        if (u != kWindingU[c] || v != kWindingV[c]) {
            return false;
        }
    }
    return true;
}

/// That each quad is told the direction its own geometry says it is, and that
/// the four walls are told four *different* directions.
///
/// One wrong entry here is the sticky-piston bug: `blockTextureLayer` answers
/// "front" for whichever direction matches the block's facing, so a wall handed
/// a neighbour's direction wears a neighbour's texture, and four walls handed
/// one direction wear the front four times. The shade is no longer checked
/// because it is no longer stored - `faceShade(q.face)` cannot disagree with
/// itself.
constexpr bool everyQuadKnowsWhichFaceItIs() {
    for (int i = 0; i < kDropQuadCount; ++i) {
        const DropQuad& q = kDropQuads[i];
        const int axis = faceAxis(q.face);
        const bool positive = faceIsPositive(q.face);
        const FaceDirection direction =
            axis == 1 ? FaceDirection::Unknown
                      : (axis == 0 ? (positive ? FaceDirection::PosX : FaceDirection::NegX)
                                   : (positive ? FaceDirection::PosZ : FaceDirection::NegZ));
        if (q.direction != direction) {
            return false;
        }
        for (int j = 0; j < i; ++j) {
            if (kDropQuads[j].face == q.face) {
                return false;
            }
        }
    }
    return true;
}

/// Every rule `FaceGeometry.hpp` states about the world, applied to the rows
/// this file actually draws. **This is the whole point of the shared header
/// taking its corners as a parameter**: the drop is held to the same four
/// proofs as the mesher without either copying them or trusting that it picked
/// the right row.
constexpr bool everyDropQuadReadsAsTheWorldDoes() {
    for (int i = 0; i < kDropQuadCount; ++i) {
        if (!faceReadsAsTheWorldDoes(kDropQuads[i].face, cornersOf(kDropQuads[i].face))) {
            return false;
        }
    }
    return true;
}

static_assert(quadReadsAsWritten(kDropQuads[0]) && quadReadsAsWritten(kDropQuads[1]) &&
                  quadReadsAsWritten(kDropQuads[2]) && quadReadsAsWritten(kDropQuads[3]) &&
                  quadReadsAsWritten(kDropQuads[4]) && quadReadsAsWritten(kDropQuads[5]),
              "a drop's quad no longer takes a bottom-left rect on the corners the mesher "
              "puts it on");
static_assert(everyDropQuadReadsAsTheWorldDoes(),
              "a dropped block's art is mirrored, upside-down or inside-out against the world - "
              "see the failing rule in FaceGeometry.hpp");
static_assert(everyQuadKnowsWhichFaceItIs(),
              "a drop's quad is told a direction that is not its own, or two quads draw the "
              "same face");

/// How many ids one pass of **this file's** sweep covers.
///
/// **Named for what it measures, not for what it is about.** `BlockDrops.hpp`
/// carries a `kDropSweepStride`/`kDropSweepPasses` pair with the same two
/// values, and that pair owns those names: it bounds the sweep over the drop
/// *table*, where this one bounds the sweep over how a dropped block is
/// *drawn*. The day the enum grows past one of them says nothing about the
/// other, so a shared constant would be two questions wearing one answer.
///
/// The two are in separate translation units today - `item/BlockDrops.hpp` is
/// included by `Main.cpp` and by nothing else, checked rather than assumed -
/// but that is one plausible `#include` line away from an anonymous-namespace
/// pair silently winning every unqualified use in this file, with nothing to
/// catch it: two identical values cannot disagree, and each sweep asserts its
/// own coverage against whichever constants it can see. The rename is what
/// makes that impossible rather than merely unlikely.
///
/// Same 512 as `kNameSweepStride` in `Block.hpp` and `kMiningSweepStride` in
/// `Mining.hpp`, for the same reason all three are.
///
/// **The pass count is DERIVED from the id count, not written down.** Changed
/// 2026-08-19, and it is the same ceiling division `kNameSweepPasses` in
/// `Block.hpp` already uses against this identical stride - this file simply
/// had not copied it, while sitting next to a comment naming that very
/// constant. It said "seven strides cover the enum with room to spare", which
/// was a measurement of a moving thing stated as a fact.
///
/// **This dissolves the coupling instead of guarding it.** A hand-written 7
/// needs the `>=` assert below to notice when blocks are added; a derived count
/// cannot be wrong, so the assert becomes a proof that can no longer fire.
/// That is worth strictly more than a correct guard, because the guard only
/// works if somebody is compiling - and on a night when nobody can, a hand
/// written 7 that has quietly gone stale is invisible. Block ids were added to
/// `Block.hpp` twice today; `kNameSweepPasses` scaled with them and this did
/// not.
constexpr int kDropModelSweepStride = 512;
constexpr int kDropModelSweepPasses =
    (static_cast<int>(kBlockIdCount) + kDropModelSweepStride - 1) / kDropModelSweepStride;

/// The capacity of the array the sweep below is really guarding, **read out of
/// the type** rather than restated as a literal - the same technique that binds
/// `BlockBoxes` to `ModelBoxes` in `Block.hpp`, and for the same reason: two
/// literals that agree today cannot notice when one of them moves.
///
/// **This name now exists twice and that is safe, but only for one reason.**
/// `Block.hpp` grew its own `game::kModelBoxCapacity` on 2026-08-19; this one
/// sits in this file's anonymous namespace, so it *shadows* the header's for
/// every use below. That resolves rather than colliding because both uses
/// (`everyModelIconBlockHasBoxes`, `tighterBoundWouldFail`) are inside the
/// anonymous namespace, where lookup finds this declaration in a nearer scope
/// and stops. A use of the bare name **after line 429** would be ambiguous
/// between the two and is a hard error - so if you move either function out of
/// the anonymous namespace, qualify it `game::kModelBoxCapacity` or delete this.
///
/// **It is not an instance of "a value derived somewhere other than its owner"**
/// - the shape this project has paid for most often - because *neither* copy is
/// a literal. Both are `sizeof(ModelBoxes::boxes)` reductions over the same
/// type, so they move together by construction and cannot disagree. Deleting
/// this one in favour of the header's is correct and slightly better, but it is
/// a tidy-up to do on a green tree, not a bug: it trades a provably-equal
/// duplicate for a dependency on a header another owner is actively editing.
///
/// **Falsifier:** this paragraph is wrong the moment either definition stops
/// reducing `sizeof(ModelBoxes::boxes)` - check both before trusting it.
constexpr int kModelBoxCapacity =
    static_cast<int>(sizeof(ModelBoxes::boxes) / sizeof(ModelBoxes::boxes[0]));

/// **Both ends of the count this file's drawing loop trusts.**
///
/// The loop below walks `for (b = 0; b < model.count; ++b)` and indexes
/// `model.boxes[b]`, so it depends on the count being at least one - a block on
/// the model-icon path with nothing in `postModel` draws **nothing**, an item
/// that falls, can be picked up and is invisible on the way - **and on its not
/// running off the end of the array**, which is a read past the object.
///
/// The upper bound is the half that can actually fail. `postModel` builds
/// `result` a box at a time and **nothing inside it bounds its own count**; the
/// worst id ships at 10 of 10 today, so the headroom is zero and the next box
/// added to a cauldron overruns. The lower bound, measured across all 3285 ids,
/// is presently unreachable - `postModel` has no path that returns a count of
/// zero - and it is kept only because it is the cheaper half of one comparison,
/// **not** because it is evidence of anything. Asserting it alone is what this
/// used to do, and a condition that cannot be false is not a test.
///
/// **Deliberately the superset, and this must not be "consolidated".** The walk
/// below asks `usesModelIcon(blockShape(id))`, not the narrower `iconIsModel(id)`
/// the drop itself now draws from. The 44 ids the two differ by - torches,
/// levers, tripwire hooks - are still built out of `postModel` by the mesher,
/// so narrowing this to match the runtime would drop them from the only
/// capacity guard they have while looking like a tidy-up.
constexpr bool everyModelIconBlockHasBoxes(int stride) {
    const int first = stride * kDropModelSweepStride;
    const int end = first + kDropModelSweepStride;
    for (int i = first; i < end && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (!usesModelIcon(blockShape(id))) {
            continue;
        }
        const int count = postModel(id).count;
        if (count < 1 || count > kModelBoxCapacity) {
            return false;
        }
    }
    return true;
}

/// **The negative control for the sweep above**, and the whole reason it is
/// trustworthy: the same walk with the bound tightened by one must *fail*.
///
/// Without this, `everyModelIconBlockHasBoxes` passing proves only that it was
/// compiled. With it, passing means the walk reaches real blocks, reads a real
/// count and compares it against a real capacity - because the identical walk
/// with `kModelBoxCapacity - 1` demonstrably finds the six cauldrons sitting on
/// the limit.
constexpr bool tighterBoundWouldFail(int stride) {
    const int first = stride * kDropModelSweepStride;
    const int end = first + kDropModelSweepStride;
    for (int i = first; i < end && i < static_cast<int>(kBlockIdCount); ++i) {
        const BlockId id = static_cast<BlockId>(i);
        if (!usesModelIcon(blockShape(id))) {
            continue;
        }
        if (postModel(id).count > kModelBoxCapacity - 1) {
            return true;
        }
    }
    return false;
}

/// Split into strides for the reason `Mining.hpp` and `BlockDrops.hpp` give:
/// MSVC counts constexpr steps per evaluation, and one pass over the whole enum
/// is past the budget. A `static_assert` inside a template is its own
/// evaluation with its own budget.
template <int Pass>
struct DropModelSweep {
    static_assert(everyModelIconBlockHasBoxes(Pass),
                  "a block drawn from its model has no boxes, so it drops invisible - or it has "
                  "more than the array holds, which is a read past the end of it");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyDropPassSwept(std::integer_sequence<int, Pass...>) {
    return (DropModelSweep<Pass>::swept && ...);
}

template <int... Pass>
constexpr bool someStrideSitsOnTheLimit(std::integer_sequence<int, Pass...>) {
    return (tighterBoundWouldFail(Pass) || ...);
}

static_assert(everyDropPassSwept(std::make_integer_sequence<int, kDropModelSweepPasses>{}),
              "a model-shaped block drops invisible or overruns its box array - the failing "
              "DropModelSweep instantiation above names which stride");
/// **Proof the sweep above can still return false.** It walks 499 model-icon
/// ids; the bound it applies is one the shipping table sits exactly on, so
/// tightening it by a single box finds them. A sweep whose condition cannot be
/// violated is `CLAUDE.md` bug shape #10 wearing an assert as a disguise, and
/// this is the line that stops that being true of this one.
static_assert(someStrideSitsOnTheLimit(std::make_integer_sequence<int, kDropModelSweepPasses>{}),
              "no model-icon block reaches the box-array capacity any more, so the sweep above no "
              "longer proves anything - retune kModelBoxCapacity's margin or delete this pair");
/// **Kept as a proof, not as a guard.** Now that `kDropModelSweepPasses` is a
/// ceiling division of `kBlockIdCount`, this cannot fail for any id count - so
/// it no longer protects anything and is instead a one-line statement that the
/// derivation above says what it means. Its old message told the reader to
/// raise the pass count, which is advice that can no longer be taken.
static_assert(kDropModelSweepPasses * kDropModelSweepStride >= static_cast<int>(kBlockIdCount),
              "the ceiling division above stopped covering kBlockIdCount, which is arithmetic "
              "rather than a stale constant - do not raise the pass count, it is derived");

glm::vec3 eyeLevel(const glm::vec3& feet) {
    return {feet.x, feet.y + 0.9f, feet.z};
}

} // namespace

void ItemEntities::spawn(const glm::vec3& position, ItemId item, int count, const glm::vec3& impulse,
                         float pickupDelay, int damage) {
    if (item == ItemId::None || count <= 0) {
        return;
    }

    Drop drop;
    drop.position = position;
    // A small deterministic-looking scatter, so several drops from one spot do
    // not stack into a single sprite.
    const float jitter = static_cast<float>((m_drops.size() * 37) % 17) / 17.0f - 0.5f;
    drop.velocity = impulse + glm::vec3{jitter * 1.4f, 3.0f, jitter * -1.1f};
    drop.item = item;
    drop.count = count;
    drop.damage = damage;
    drop.pickupDelay = pickupDelay;
    m_drops.push_back(drop);
}

void ItemEntities::update(const World& world, const glm::vec3& playerFeet, float deltaSeconds) {
    // Clamped exactly as the player's and the creatures' steps are. A drop is
    // 0.30 tall and falls at 24 m/s, so a frame past ~55 ms carried it a whole
    // block and a bit further, and the destination-only test below then found
    // clear air on the far side of a one-block floor. Alt-tabbing, a
    // chunk-streaming hitch and dragging the window - GLFW blocks `pollEvents`
    // for the whole drag - all produce that frame.
    deltaSeconds = std::min(deltaSeconds, kMaxDeltaSeconds);

    const glm::vec3 target = eyeLevel(playerFeet);

    // Anything nobody came back for is gone after five minutes, matching the
    // reference. Without it a long session accumulates every drop it ever made
    // - and each one rebuilds its geometry every frame.
    for (std::size_t i = m_drops.size(); i-- > 0;) {
        if (m_drops[i].age > kDespawnSeconds) {
            m_drops[i] = m_drops.back();
            m_drops.pop_back();
        }
    }

    for (Drop& drop : m_drops) {
        drop.age += deltaSeconds;

        // Ground that has not arrived yet reads as air, so a drop simulated over
        // it falls out of the world and is gone for good - the same rule the
        // player and the creatures already keep. Hanging still until the terrain
        // is back is the only safe answer, and the ageing above still runs so a
        // drop stranded over unloaded ground eventually despawns rather than
        // hanging there forever.
        if (!world.columnResident(static_cast<int>(std::floor(drop.position.x)),
                                  static_cast<int>(std::floor(drop.position.z)))) {
            drop.velocity = glm::vec3{0.0f};
            continue;
        }

        const float distance = glm::length(target - drop.position);
        if (drop.age > drop.pickupDelay && distance < kAttractRadius) {
            // Drifts toward the player rather than snapping, so collecting reads
            // as the item coming to you.
            //
            // Note the missing lower bound on distance. Falling through to
            // gravity once the drop arrives makes it drop, re-attract and drop
            // again - an item bouncing off the player it is trying to reach.
            if (distance > 0.0001f) {
                const glm::vec3 pull = (target - drop.position) / distance;
                drop.position += pull * std::min(kAttractSpeed * deltaSeconds, distance);
            }
            drop.velocity = glm::vec3{0.0f};
            continue;
        }

        // Resolved on every axis, through the same helpers the player and the
        // creatures use. It used to be vertical only, on the reasoning that a
        // drop is small and decorative - but nothing stopped one entering a
        // block sideways, and once inside, the ground probe found *that* block
        // and lifted the drop onto its top. So an item nudged against a step
        // climbed it.
        const auto boxAt = [](const glm::vec3& centre) {
            return Aabb{{centre.x - kHalfSize, centre.y - kHalfSize, centre.z - kHalfSize},
                        {centre.x + kHalfSize, centre.y + kHalfSize, centre.z + kHalfSize}};
        };

        // A dropped item genuinely floats in the reference - it carries
        // `minecraft:buoyant`, which the player does not - so anything lost in a
        // lake washes up rather than being gone. It is carried by the current
        // too, under the same drag as everything else in the water.
        //
        // Water **replaces** gravity here rather than following it. Applying
        // both offsets the settling speed by `g dt k / (1 - k)`, which is metres
        // per second at any real frame rate - enough to sink an item that every
        // constant says should float.
        const fluid::FluidContact water = fluid::sampleFluid(world, boxAt(drop.position));
        if (water.inFluid) {
            // **The float target now carries the current's downward half.** A
            // dropped item under a waterfall used to bob at the surface as
            // though the column were still, because the flow vector had no y at
            // all - "Falling water blocks have a downward current by default"
            // (minecraft.wiki, *Water*). It is added to the buoyancy rather than
            // replacing it, so an item in still water still floats.
            const glm::vec3 carried = water.flow * fluid::kCurrentSpeed;
            drop.velocity.y = fluid::approach(drop.velocity.y, kFloatSpeed + carried.y,
                                              fluid::kWaterDrag, deltaSeconds);
            drop.velocity.x =
                fluid::approach(drop.velocity.x, carried.x, fluid::kWaterDrag, deltaSeconds);
            drop.velocity.z =
                fluid::approach(drop.velocity.z, carried.z, fluid::kWaterDrag, deltaSeconds);
        } else {
            drop.velocity.y = std::max(drop.velocity.y - kGravity * deltaSeconds, -kTerminalVelocity);
        }

        const glm::vec3 step = drop.velocity * deltaSeconds;
        glm::vec3 next = drop.position;

        // **Swept on the horizontal axes too, one axis at a time**, so a corner
        // pushes out along one of them rather than being refused entirely.
        //
        // The per-axis resolution was always right; testing only where the axis
        // *ended* was not. A drop's box is 0.30 across, so any step longer than
        // that leaves an untested gap between the start box and the destination
        // box - wide enough for a 0.125-thick pane of glass or a fence post to
        // sit in - and a thrown stack crosses it. It is the identical hole the
        // vertical move was fixed for, in the two axes the fix did not travel
        // to; slicing both with the same `kMaxSweepStep` is what makes the
        // `static_assert` above cover all three.
        //
        // A step shorter than one slice behaves exactly as it always did: one
        // slice, blocked, revert. A longer one now stops at the last clear
        // slice rather than at the frame's start, so a drop slides up to a wall
        // instead of refusing the whole frame's travel.
        const auto sweepAxis = [&](int axis) {
            const int slices =
                std::max(1, static_cast<int>(std::ceil(std::abs(step[axis]) / kMaxSweepStep)));
            const float slice = step[axis] / static_cast<float>(slices);
            for (int i = 0; i < slices; ++i) {
                const float before = next[axis];
                next[axis] += slice;
                if (overlapsSolid(world, boxAt(next))) {
                    next[axis] = before;
                    drop.velocity[axis] = 0.0f;
                    return;
                }
            }
        };
        sweepAxis(0);
        sweepAxis(2);

        drop.onGround = false;
        // **Swept, not destination-only** - the vertical half, and the one that
        // has a landing to resolve as well as a wall to stop at.
        // `FallingBlock.cpp` walks every cell its cube would pass through for
        // exactly this reason, and a drop needs it more: it is 0.30 tall, so at
        // terminal velocity even a clamped frame moves it four times its own
        // height and a test of only where it *ended* found clear air under a
        // one-block floor. The move is walked in slices no longer than
        // `kMaxSweepStep` and the first one to hit something stops it.
        //
        // This does **not** share `sweepAxis` above, and the difference is real
        // rather than an oversight: a horizontal slice that is blocked simply
        // stops, where a vertical one has to ask `highestSurfaceBelow` where
        // the surface actually is - a slab's top is halfway up its cell - and
        // then decide between a bounce and a settle.
        const int slices =
            std::max(1, static_cast<int>(std::ceil(std::abs(step.y) / kMaxSweepStep)));
        const float slice = step.y / static_cast<float>(slices);
        for (int i = 0; i < slices; ++i) {
            // Where *this slice* began, and the ceiling a landing may not be
            // above. Handing the whole frame's start to `highestSurfaceBelow`
            // would let a later slice settle on a surface the drop had already
            // fallen past, which is a teleport upward.
            const float from = next.y;
            next.y += slice;
            if (step.y > 0.0f || !overlapsSolid(world, boxAt(next))) {
                continue;
            }
            // Landing reads the shape table, so a slab's top is halfway up its
            // cell rather than the cell boundary - the same rule that stopped
            // creatures hovering over slabs.
            const float surface = highestSurfaceBelow(world, boxAt(next), from - kHalfSize);
            if (std::isfinite(surface)) {
                next.y = surface + kHalfSize;

                if (-drop.velocity.y > kSettleSpeed) {
                    drop.velocity.y = -drop.velocity.y * kRestitution;
                    drop.velocity.x *= kGroundFriction;
                    drop.velocity.z *= kGroundFriction;
                } else {
                    drop.velocity = glm::vec3{0.0f};
                    drop.onGround = true;
                }
            } else {
                next.y = from;
                drop.velocity.y = 0.0f;
            }
            break;
        }

        drop.position = next;
    }
}

std::vector<ItemEntities::Collectable> ItemEntities::collectable(const glm::vec3& playerFeet) const {
    const glm::vec3 target = eyeLevel(playerFeet);
    std::vector<Collectable> ready;

    for (std::size_t i = 0; i < m_drops.size(); ++i) {
        const Drop& drop = m_drops[i];
        if (drop.age > drop.pickupDelay && glm::length(target - drop.position) < kCollectRadius) {
            ready.push_back({i, drop.item, drop.count, drop.damage});
        }
    }
    return ready;
}

void ItemEntities::remove(std::size_t index) {
    if (index < m_drops.size()) {
        // Order carries no meaning, so the last one fills the hole.
        m_drops[index] = m_drops.back();
        m_drops.pop_back();
    }
}

void ItemEntities::reduce(std::size_t index, int taken) {
    if (index >= m_drops.size()) {
        return;
    }
    m_drops[index].count -= taken;
    if (m_drops[index].count <= 0) {
        remove(index);
    }
}

std::vector<ItemEntities::Persisted> ItemEntities::persisted() const {
    std::vector<Persisted> out;
    out.reserve(m_drops.size());
    for (const Drop& drop : m_drops) {
        // **Written field by field rather than braced**, unlike `collectable`
        // three functions above. That one fills a `Collectable`, which has four
        // members and will keep them; this one crosses into the save path, and a
        // positional list is precisely what turns "someone added a field" into a
        // silent mis-assignment that compiles. The same rule now guards
        // `SavedPlayer`'s own construction.
        Persisted record;
        record.position = drop.position;
        record.velocity = drop.velocity;
        // The one place the three loose fields become a stack. Composed here so
        // that no caller can pair them wrongly.
        record.stack = ItemStack{drop.item, drop.count, drop.damage};
        record.age = drop.age;
        record.pickupDelay = drop.pickupDelay;
        record.onGround = drop.onGround;
        out.push_back(record);
    }
    return out;
}

void ItemEntities::restore(const glm::vec3& position, const glm::vec3& velocity,
                           const ItemStack& stack, float age, float pickupDelay, bool onGround) {
    Drop drop;
    drop.position = position;
    drop.velocity = velocity;
    // And the one place it comes apart again. `persisted` above is the only
    // other, and the two are inverses - which is the whole reason both live in
    // this file rather than in whatever is doing the saving.
    drop.item = stack.item;
    drop.count = stack.count;
    drop.damage = stack.damage;
    drop.age = age;
    drop.pickupDelay = pickupDelay;
    drop.onGround = onGround;
    m_drops.push_back(drop);
}

engine::MeshData ItemEntities::buildMesh(const World& world, float timeSeconds,
                                         const SpriteMask& sprites, const DrawRange& range,
                                         engine::MeshData* blended) const {
    engine::MeshData mesh;

    for (const Drop& drop : m_drops) {
        if (drop.item == ItemId::None) {
            continue;
        }
        if (!range.contains(drop.position)) {
            continue;
        }
        // Anything without a cube to build is drawn from its sprite: every
        // non-block item, and every plant. Until spawn eggs arrived nothing
        // ever tested the first: a dropped pickaxe existed, fell, could be
        // picked up and rendered **nothing at all**, which is a bug you can
        // only see by throwing one on the floor.
        const bool blockLike = isBlockItem(drop.item);
        const BlockId block = blockLike ? blockForItem(drop.item) : BlockId::Air;
        // **The same question the slot picture asks**, by id rather than by
        // shape, so a torch is the flat `block/torch` sprite on the floor
        // exactly as it is in the hotbar while the one standing in the world
        // stays a model. Asking `usesFlatIcon(blockShape(...))` here and
        // `iconIsFlat` there is precisely how the two come apart.
        const bool flat = !blockLike || iconIsFlat(block);
        const int spriteLayer =
            !flat ? -1
                  : (blockLike ? static_cast<int>(blockTextureLayer(block, BlockFace::Side))
                               : itemTextureLayer(drop.item));
        if (flat && spriteLayer < 0) {
            continue;
        }

        const float bob = drop.onGround ? (std::sin(timeSeconds * kBobSpeed + drop.position.x) * 0.5f + 0.5f) * kBobHeight
                                        : 0.0f;
        const glm::vec3 centre{drop.position.x, drop.position.y + bob, drop.position.z};

        const float angle = timeSeconds * kSpinSpeed + drop.position.z;
        const float c = std::cos(angle);
        const float s = std::sin(angle);

        // Lit by the cell it sits in, so a drop in a cave is as dark as its
        // surroundings instead of glowing.
        const int lx = static_cast<int>(std::floor(centre.x));
        const int ly = static_cast<int>(std::floor(centre.y));
        const int lz = static_cast<int>(std::floor(centre.z));
        const float sky = static_cast<float>(world.skyLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);
        const float blockLight = static_cast<float>(world.blockLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);

        // Faces of a spun cube. Only the four sides and the top are emitted; the
        // underside of something resting on the ground is never seen.
        const glm::vec3 right{c * kHalfSize, 0.0f, s * kHalfSize};
        const glm::vec3 forward{-s * kHalfSize, 0.0f, c * kHalfSize};
        const glm::vec3 up{0.0f, kHalfSize, 0.0f};

        // A tool, a spawn egg or a flower is its sprite made solid, built by
        // `appendSpriteModel` - shared with thrown items, which have to look
        // like the same object in the air as they do on the floor.
        //
        // **A plant takes the same path deliberately.** In the ground it is two
        // crossed quads, which is the shape it grows in; lying on the floor it
        // is one flower with real thickness, exactly like everything else that
        // has been dropped.
        if (flat) {
            appendSpriteModel(mesh, sprites, spriteLayer, centre, right, up, forward, sky, blockLight);
            continue;
        }

        // Everything else is a **miniature of the block itself**, box for box.
        // It used to be a single cube wearing the block's side texture, so a
        // dropped bell was a gold brick, a dropped fence a plank and a dropped
        // anvil a black cube. The boxes come from the same owner the slot
        // picture reads, so what is on the floor and what is in the hotbar
        // cannot disagree.
        const auto corner = [&](float x, float y, float z) {
            return centre + right * (x * 2.0f - 1.0f) + up * (y * 2.0f - 1.0f) +
                   forward * (z * 2.0f - 1.0f);
        };
        const auto part = [&](const BlockBox& b, const ModelBox* model) {
            // **Each side quad has to be told which side it is.**
            // `blockTextureLayer` works out "is this the front" by comparing the
            // direction it is given against the block's own facing, so one layer
            // computed from `blockFacing` and handed to all four sides says
            // "front" four times - which put a sticky piston's plate right round
            // the block. The mesher and the inventory icon both already pass a
            // real direction per face; this was the third place a block is drawn
            // and the one that still did not.
            // **Every face asks `Block.hpp` which layer it wants, and this path
            // asks per face rather than per pair.**
            //
            // It used to read `sideLayer` for the walls and `lidLayer` for both
            // the lid and the floor, which is one answer where the model now
            // holds six. `boxFaceLayer` is the owner of that question and
            // `ChunkMesher.cpp` is the reader it was written for; a dropped or
            // thrown anvil wore its damage texture on its underside because the
            // floor took the lid's answer instead of its own `floorOverride`.
            //
            // The `-1` reply means "take the block's own", and the fallback
            // below is what resolves it: `Top` and `Bottom` are genuinely
            // different textures, and each wall names the direction it faces so
            // `blockTextureLayer` can tell a front from a side.
            const auto faceLayer = [&](const DropQuad& quad) {
                if (model != nullptr) {
                    const float said = boxFaceLayer(*model, quad.face);
                    if (said >= 0.0f) {
                        return said;
                    }
                }
                if (faceAxis(quad.face) != 1) {
                    return blockTextureLayer(block, BlockFace::Side, quad.direction);
                }
                return blockTextureLayer(
                    block, faceIsPositive(quad.face) ? BlockFace::Top : BlockFace::Bottom);
            };
            // **Every face asks for its own rectangle and its own quarter turn,
            // from the same owner the mesher asks.**
            //
            // This used to hold one wall rect and one lid rect, with the floor
            // borrowing the lid's and all four walls sharing one. A model now
            // paints its floor and each of its four walls from different corners
            // of its net, and `boxFaceRect` and `boxFaceTurns` own those answers
            // - the same two `ChunkMesher.cpp` reads.
            //
            // Three things this keeps that a plain substitution would lose:
            //
            // - **The turn does not run the same way on every face.** It walks
            //   the four uv slots forward only where `flipU == flipV`, which is
            //   `turnRunsWithTheSlots` above: true on `+X`, `+Y` and `-Z`,
            //   false on `-X`, `-Y` and `+Z`. The floor being the lid seen from
            //   behind is one row of that rule rather than the whole of it, and
            //   writing it as "the floor goes backward" - which is what this
            //   line said until tonight - is wrong on `-X` and `+Z`.
            // - **`unmirror` is honoured *before* the turn**, not after. It is a
            //   property of the box rather than of one face, so it is read off
            //   the model; the order matters because a slot swap and a cyclic
            //   shift do not commute at turns 1 and 3, and mirror-then-turn is
            //   the order `FallingBlock.cpp` proved against the mesher.
            // - **Walls take their turn too.** No shipping box states one today,
            //   so the shift is a no-op now; the mesher applies it to every face
            //   without exception, and a path that skipped it would be a half fix
            //   the moment one appears.
            const auto faceUvs = [&](const DropQuad& quad, glm::vec2 (&out)[4]) {
                FaceRect r{0.0f, 0.0f, 1.0f, 1.0f};
                int turns = 0;
                if (model != nullptr) {
                    r = boxFaceRect(*model, quad.face);
                    turns = static_cast<int>(boxFaceTurns(*model, quad.face)) & 3;
                }
                glm::vec2 rect[4]{
                    {r.uMin, r.vMax}, {r.uMax, r.vMax}, {r.uMax, r.vMin}, {r.uMin, r.vMin}};
                // Undoing a U flip against a fixed set of corners is a swap of
                // the rect's two U columns. `Unknown` is tested for explicitly:
                // a faceless block answers `Unknown` to both sides, and
                // `Unknown == Unknown` would toggle every wall it has.
                if (faceAxis(quad.face) != 1 && model != nullptr &&
                    model->unmirror != FaceDirection::Unknown &&
                    model->unmirror == quad.direction) {
                    const glm::vec2 was[4]{rect[0], rect[1], rect[2], rect[3]};
                    rect[0] = was[1];
                    rect[1] = was[0];
                    rect[2] = was[3];
                    rect[3] = was[2];
                }
                const int shift = turnRunsWithTheSlots(quad) ? turns : (4 - turns) & 3;
                for (int c = 0; c < 4; ++c) {
                    out[c] = rect[(shift + c) & 3];
                }
            };

            const auto face = [&](const glm::vec3& a, const glm::vec3& b2, const glm::vec3& d,
                                  const glm::vec3& e, const glm::vec2* rect, float layer, float shade) {
                // **A dropped pane of stained glass is the fourth place a block
                // is drawn**, and the one that would have been missed: its art
                // keeps a real alpha now, so left in the opaque mesh the cutout
                // test would throw away the 0.40 centre panel and drop a
                // wireframe frame on the floor.
                const bool blend = blended != nullptr && isBlendedGlass(block);
                engine::MeshData& out = blend ? *blended : mesh;
                const auto base = static_cast<std::uint32_t>(out.vertices.size());
                const glm::vec3 corners[4]{a, b2, d, e};
                for (int i = 0; i < 4; ++i) {
                    out.vertices.push_back(
                        engine::Vertex{{corners[i].x, corners[i].y, corners[i].z},
                                       engine::packVertexColor(sky, blockLight, shade, 1.0f),
                                       {rect[i].x, rect[i].y},
                                       layer,
                                       engine::kVertexSurfaceDefault});
                }
                // Outward winding first, and the reverse behind it for an
                // opaque face only: it spins, so either side of a solid face
                // can end up toward the camera and the depth test hides
                // whichever of the pair is further away.
                //
                // **A blended face gets one winding, and that is not a tidy-up.**
                // `ChunkMesher.cpp` states the rule for the world - "never both.
                // A double-sided quad rasterises from either side, so blending
                // one would lay the same tint down twice and a single pane would
                // read as two" - and the drop was the path that did not honour
                // it. Both windings defeat the back-face cull, so a closed box
                // of stained glass rasterised all six of its faces instead of
                // the three you can see, laying the tint down twice along every
                // view ray; and because the six go in in a fixed order with
                // depth writes on, whether the far face was rejected depended on
                // which way the item happened to be facing, so it *pulsed*
                // between one and two layers once per revolution.
                //
                // The cost is that the inside of a face is invisible, which is
                // exactly what the placed block already does, and a 0.30 m cube
                // has no inside a camera can reach. Flats do not come through
                // here at all - `appendSpriteModel` draws those, and two
                // windings is right for a single quad with nothing behind it.
                out.indices.insert(out.indices.end(), {base + 0, base + 1, base + 2, base + 0,
                                                       base + 2, base + 3});
                if (!blend) {
                    out.indices.insert(out.indices.end(),
                                       {base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
                }
            };

            // **Six faces, from the one table that says where their corners
            // are.** They used to be six calls with the corners written out at
            // each one, and the four side quads were the mesher's rows with
            // their first and last pairs exchanged - a horizontal mirror on
            // every dropped and thrown block in the game. `kDropQuads` is the
            // mesher's own rows and the `static_assert`s above it are what stop
            // that coming back.
            for (int q = 0; q < kDropQuadCount; ++q) {
                const DropQuad& quad = kDropQuads[q];
                glm::vec2 rect[4];
                faceUvs(quad, rect);
                const float layer = faceLayer(quad);
                const auto at = [&](int c) {
                    FaceCorners corners = cornersOf(quad.face);
                    return corner(corners[c][0] != 0 ? b.maxX : b.minX,
                                  corners[c][1] != 0 ? b.maxY : b.minY,
                                  corners[c][2] != 0 ? b.maxZ : b.minZ);
                };
                face(at(0), at(1), at(2), at(3), rect, layer, faceShade(quad.face));
            }
        };

        if (iconIsModel(block)) {
            const ModelBoxes model = postModel(block);
            for (int i = 0; i < model.count; ++i) {
                part(model.boxes[i].box, &model.boxes[i]);
            }
        } else {
            const BlockBoxes parts = iconBoxes(block);
            for (int i = 0; i < parts.count; ++i) {
                part(parts.boxes[i], nullptr);
            }
        }
    }

    return mesh;
}

void dropStack(ItemEntities& drops, const glm::vec3& position, const ItemStack& stack,
               const glm::vec3& impulse, float pickupDelay) {
    dropStack(drops, position, stack, stack.count, impulse, pickupDelay);
}

void dropStack(ItemEntities& drops, const glm::vec3& position, const ItemStack& stack, int count,
               const glm::vec3& impulse, float pickupDelay) {
    if (stack.empty() || count <= 0) {
        return;
    }
    // The one place `damage` is read off the stack, so it is the one place it
    // can be forgotten - and it is not forgotten here.
    drops.spawn(position, stack.item, std::min(count, stack.count), impulse, pickupDelay,
                stack.damage);
}

} // namespace game
