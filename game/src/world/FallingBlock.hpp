#pragma once

#include "engine/render/MeshData.hpp"
#include "world/Block.hpp"
#include "world/DrawRange.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

namespace game {

class World;

// ---------------------------------------------------------------------------
// What a falling cube does, written as pure functions of block ids.
//
// **They are here rather than in the .cpp's anonymous namespace so a probe can
// drive the same code the world does.** A probe that re-types the rule proves
// the probe, and this file has already paid for a second copy that agreed with
// its original until it did not: its corner table was the mesher's rows
// reversed for four milestones under a comment saying the two matched.
// ---------------------------------------------------------------------------

/// **What stops a falling cube, which is not the same question as what a new
/// block may be put into.**
///
/// `isReplaceable` is the reference's `canBeReplaced` and answers a *placement*
/// question - which cell the block in your hand lands in. Asking it here is what
/// parked a falling cube one cell above every torch, flower, sapling, mushroom,
/// rail, redstone line, button, lever, pressure plate, crop and cane in the
/// game: none of those is replaceable, so every one of them read as a floor.
///
/// `RESEARCH.md` 9.2 asks a different question - the cube "continues until it
/// lands on a block with a **solid top surface**" - and what stops an entity is
/// a collision box, which a torch and a rail do not have.
/// <https://minecraft.wiki/w/Falling_Block> (Behavior).
///
/// **Both halves are load-bearing.** Dropping `!isReplaceable` lets a cube rest
/// on one-deep snow, which the reference lets you build straight into; dropping
/// the collision test is the bug this replaces.
constexpr bool stopsAFallingCube(BlockId id) {
    return !isReplaceable(id) && collisionBoxes(id).count > 0;
}

/// The highest point of a block's collision shape, **in cells** - so a full
/// cube is 1.0, a slab 0.5 and soul sand 0.875.
constexpr float collisionShapeTop(BlockId id) {
    const BlockBoxes boxes = collisionBoxes(id);
    float top = 0.0f;
    for (int i = 0; i < boxes.count; ++i) {
        top = boxes.boxes[i].maxY > top ? boxes.boxes[i].maxY : top;
    }
    return top;
}

/// **Whether a cube coming to rest on this can go back to being a block.**
///
/// The reference lands the cube on the shape's real surface, not on the cell
/// boundary, so a half-height top leaves the cube's bottom centre *inside* the
/// cell it was falling through - and that is what the wiki's rule turns on:
/// return to a block state only "if it lands with the bottom center of its
/// hitbox in a replaceable block, **and the block below can support it**".
/// Otherwise it breaks and drops as an item, which is why landing on a slab
/// gives you the sand back rather than a floating cube on a step.
constexpr bool holdsACubeUp(BlockId id) {
    return stopsAFallingCube(id) && collisionShapeTop(id) >= 1.0f;
}

/// **The whole landing rule: whether a cube that has stopped goes back to being
/// a block, or breaks into an item.** `occupying` is what is in the cell its
/// bottom centre came to rest in; `support` is the cell below that one.
///
/// <https://minecraft.wiki/w/Falling_Block> (Behavior): *"If it lands with the
/// bottom center of its hitbox in a replaceable block, and the block below can
/// support it ... then the falling block returns to its block state. Otherwise,
/// it breaks and drops as an item."*
///
/// !! **THE INVERSE OF THIS IS THE OBVIOUS ANSWER AND IT DESTROYS A MECHANIC.**
/// The reading a reader reaches for - *the cube always wins, and whatever was
/// in the cell is crushed and paid out from the drop table* - is backwards.
/// **The faller loses and the occupant survives**, which is the torch trick:
/// dig out the base of a sand column, put a torch where it will land, and the
/// whole column comes back to you as items with the torch still standing. It is
/// one of the first things every player learns, and "crush the torch, keep the
/// sand" deletes it outright. `theInverseIsRejected` below is that wrong rule
/// written out and shown failing, so it cannot be arrived at again by accident.
constexpr bool landsAsABlock(BlockId occupying, BlockId support) {
    return isReplaceable(occupying) && holdsACubeUp(support);
}

// **The torch trick, written as a proof rather than as a hope.** Every one of
// these families is one the old `isReplaceable` walk stopped dead on.
static_assert(!stopsAFallingCube(BlockId::Torch) && !stopsAFallingCube(BlockId::RailRunFirst) &&
                  !stopsAFallingCube(BlockId::Dandelion) &&
                  !stopsAFallingCube(BlockId::WheatCrop0),
              "a falling cube passes through what has no collision box, whatever it is drawn as");
static_assert(!stopsAFallingCube(BlockId::Air) && !stopsAFallingCube(BlockId::Water0) &&
                  !stopsAFallingCube(BlockId::Lava0) && !stopsAFallingCube(BlockId::Fire) &&
                  !stopsAFallingCube(BlockId::SnowLayerFirst),
              "nor through the empty cases, and one-deep snow is one of them");
static_assert(stopsAFallingCube(BlockId::Stone) && holdsACubeUp(BlockId::Stone) &&
                  stopsAFallingCube(BlockId::Sand) && holdsACubeUp(BlockId::Sand),
              "while a full cube both stops one and holds one up, or nothing would ever land");

// **The half-height cases, which are the other half of the same rule.** A slab
// stops a cube and does not hold one up, so the cube breaks; getting this
// backwards is what would let sand stack on a step as though it were ground.
// Soul sand, farmland and a dirt path are the same shape of case and are the
// ones a player meets by accident.
static_assert(stopsAFallingCube(BlockId::SlabRunFirst) && !holdsACubeUp(BlockId::SlabRunFirst) &&
                  !holdsACubeUp(BlockId::SoulSand) && !holdsACubeUp(BlockId::Farmland) &&
                  !holdsACubeUp(BlockId::DirtPath),
              "landing on a slab turns a falling block into an item - RESEARCH.md 9.2");

// **A fence does hold one up, and that is not an oversight.** The reference
// took this away in 1.13 and gave it back in 1.16 - *"falling blocks once again
// turn back into blocks when landing on fences, closed fence gates, walls or
// boats"* (<https://minecraft.wiki/w/Falling_Block>, History) - so a cube
// landing on a fence post is a block, not an item.
static_assert(holdsACubeUp(BlockId::FenceRunFirst),
              "a fence post is a full cell tall and a cube lands on one as a block");

// ---------------------------------------------------------------------------
// The wrong rules, written out and shown failing.
//
// **A proof nobody has watched fail is indistinguishable from a tautology**,
// which is `FaceGeometry.hpp`'s argument and the reason its negative test
// exists. So the three formulations a reader actually reaches for are named
// here and each is held against the case that kills it.
// ---------------------------------------------------------------------------

/// The caller's comment, and this file until 2026-08-19: the cube always wins.
constexpr bool wrongCrushesTheOccupant(BlockId, BlockId) { return true; }

/// Bug shape 4, "a predicate written as equals the empty case". Sand poured
/// into a pond would never settle.
constexpr bool wrongEmptyCellOnly(BlockId occupying, BlockId) {
    return occupying == BlockId::Air;
}

/// The near-miss: reusing the *walk's* predicate for the *landing*. It reads
/// right, it is one word from the real rule, and it is the torch trick
/// inverted - a cube that fell through a torch would then stand on its cell.
constexpr bool wrongAnythingItFellThrough(BlockId occupying, BlockId support) {
    return !stopsAFallingCube(occupying) && holdsACubeUp(support);
}

constexpr bool theInverseIsRejected() {
    return landsAsABlock(BlockId::Torch, BlockId::Stone) !=
               wrongCrushesTheOccupant(BlockId::Torch, BlockId::Stone) &&
           landsAsABlock(BlockId::Water0, BlockId::Stone) !=
               wrongEmptyCellOnly(BlockId::Water0, BlockId::Stone) &&
           landsAsABlock(BlockId::Torch, BlockId::Stone) !=
               wrongAnythingItFellThrough(BlockId::Torch, BlockId::Stone) &&
           // And it is not simply "never", which every one of the three above
           // would also disagree with.
           landsAsABlock(BlockId::Air, BlockId::Stone) &&
           landsAsABlock(BlockId::TallGrass, BlockId::Sand);
}

static_assert(theInverseIsRejected(),
              "the landing rule agrees with one of the three wrong rules it exists to reject - "
              "the first of them deletes the torch trick, the second stops sand settling in "
              "water, and the third lets a cube stand on the cell it just fell through");

/// Where a cube passing downward through the cells `[fromCell, toCell]` comes
/// to rest. `columnAt(y)` answers what is in the column at cell `y`.
///
/// **Every cell the cube would pass through this frame is tested, not just the
/// one it ends in.** At terminal speed a clamped frame covers **1.96 m** - the
/// reference's 1.96 blocks a tick, and one tick is exactly the clamp - so the
/// cube crosses two whole cell boundaries at once and can straddle parts of two
/// more. A fast block that only checked its destination would drop straight
/// through a one-block floor.
///
/// **That figure was understated four times over until 2026-08-19**, at a value
/// below one cell. The code was right and the comment was wrong, which is the
/// dangerous direction: a sub-cell distance quietly argued that the walk below
/// is redundant and a destination test would do. `FallingBlock.cpp` now asserts
/// the distance stays above one cell, sited beside `kTerminalVelocity` and
/// `kMaxDeltaSeconds` because this header cannot see either constant and so
/// cannot state the number honestly on its own.
///
/// The retired wording is deliberately **not** quoted here. A dead claim
/// repeated verbatim reads exactly like a live one to the next reader sweeping
/// for it - which is how one correction in this codebase got re-reported as an
/// open bug after it had already been fixed.
///
/// Pure and templated on the column so the probe drives this function rather
/// than a copy of it - the fall is decided here and applied by the caller,
/// which is the same "compute, then apply" split the mesher uses.
struct CubeRest {
    int cell = 0;
    bool landed = false;
};

template <typename ColumnAt>
constexpr CubeRest fallingCubeRest(int fromCell, int toCell, ColumnAt columnAt) {
    for (int y = fromCell; y >= toCell; --y) {
        // The cell has to be free as well as supported. Two blocks falling down
        // one shaft both aim at the same floor, and testing only what is
        // underneath let the second settle *into* the first and crush it.
        if (stopsAFallingCube(columnAt(y))) {
            return {y + 1, true};
        }
        if (y <= 0 || stopsAFallingCube(columnAt(y - 1))) {
            return {y, true};
        }
    }
    return {toCell, false};
}

/// **Damage in half-hearts a landing block deals to whatever is in the cell it
/// lands in**, given how far it fell in cells.
///
/// **The name is narrower than the function and cannot be widened**, because
/// `Main.cpp` spells it and this pass does not own that file. It answers for
/// every block that hurts on landing; an anvil is merely the first.
///
/// <https://minecraft.wiki/w/Anvil> (Falling anvils): two points of damage per
/// block fallen *after the first*, capped at forty.
/// <https://minecraft.wiki/w/Falling_Block>: *"The damage is dealt only on
/// landing, and is not dealt to players and mobs that collide with them in
/// mid-air."*
///
/// <https://minecraft.wiki/w/Pointed_Dripstone>: *"After 2 blocks' falling, the
/// amount of damage is 1HP per pointed dripstone falling (less than 6 will be
/// counted as 6) per each block of falling distance in Java Edition, or **1HP
/// per each block of falling distance in Bedrock Edition**. The damage is
/// capped at 40HP."* **The parenthesis is the Java rule and is deliberately not
/// implemented** - the whole of MCPE-184329 is that Bedrock does 1 per block
/// where Java does 6, and Bedrock is this project's reference. A reader who
/// "restores" the minimum makes a one-cell drip lethal.
///
/// **Where the wiki is ambiguous and what was chosen.** *"After 2 blocks'
/// falling ... 1HP per each block of falling distance"* does not say whether
/// the two free blocks come off the distance. They are subtracted here, which
/// is how the identical sentence shape is already read for the anvil one line
/// up (*"per block fallen after the first"* -> `fell - 1`). Reading it the
/// other way makes a three-cell fall hurt three times rather than once.
///
/// **The helmet is not applied here and must not be.** The reference takes a
/// quarter off for a helmet and charges that helmet double durability, and both
/// halves belong to whoever owns armour - applying one here and leaving the
/// other to the caller is this project's recorded failure shape five, a
/// derivation applied to one of a pair and not the other. It is the same
/// quarter for both blocks, so this stays one decision rather than two.
constexpr int kAnvilDamagePerCell = 2;   ///< half-hearts per cell fallen
constexpr int kAnvilDamageCap = 40;      ///< half-hearts, twenty hearts
constexpr int kAnvilFreeCells = 1;       ///< the first cell of the fall is free

constexpr int kDripstoneDamagePerCell = 1;  ///< half-hearts per cell fallen, Bedrock
constexpr int kDripstoneDamageCap = 40;     ///< half-hearts, twenty hearts
constexpr int kDripstoneFreeCells = 2;      ///< "after 2 blocks' falling"

/// The ramp both blocks share: free cells off the top, a flat rate per cell
/// after that, then a cap. Written once so a second falling hazard is three
/// constants rather than a fourth copy of the arithmetic.
///
/// **`fellCells` is integral by construction and arrives fractionally short of
/// it, so this floors with a tolerance rather than truncating.** A cube spawns
/// on an integer cell and `descendTo(restY)` lands it on an integer cell, so the
/// true descent is a whole number of blocks every time - but the value is
/// accumulated one frame at a time in `update`, and float addition does not
/// telescope exactly. Measured 2026-08-19 through the real physics loop at
/// 1/60 s: a nineteen-block drop arrives as **18.999998093**, which
/// `static_cast<int>` truncated to 18 and cost a whole block of damage.
///
/// **17 of the first 40 distances paid the wrong damage**, and the pattern was
/// worse than a constant offset because it came and went - a 20-block fall was
/// exact and correct while 17, 18 and 19 were each two half-hearts light. For
/// the anvil only falls under 21 cells are visible, since everything above is
/// capped anyway; **for a stalactite nothing is capped below 42 cells, so every
/// drifted distance under-damaged.**
///
/// **Why the existing asserts below could not catch it: they all pass exact
/// integers** - 1.0f, 4.0f, 21.0f, 42.0f - which is the one input the
/// accumulator never actually produces. A table tested only at values its real
/// caller cannot generate is tested against itself.
///
/// Sized from measurement, not taste: the worst drift observed over falls of 1
/// to 40 cells was 5.7e-6 blocks, so 1/512 leaves roughly 340x headroom while
/// staying far below any fraction a genuinely fractional distance would carry.
/// Floor-with-tolerance rather than round-to-nearest is deliberate - the
/// reference floors the fall distance, and if the integral-by-construction
/// invariant is ever broken by a fractional spawn this still answers the
/// reference's way instead of over-crediting by half a block.
constexpr float kFellCellsTolerance = 1.0f / 512.0f;  ///< blocks; absorbs per-frame float drift

constexpr int landingDamageRamp(int perCell, int freeCells, int cap, float fellCells) {
    const int cells = static_cast<int>(fellCells + kFellCellsTolerance) - freeCells;
    if (cells <= 0) {
        return 0;
    }
    const int damage = cells * perCell;
    return damage > cap ? cap : damage;
}

constexpr int anvilLandingDamage(BlockId block, float fellCells) {
    if (isAnvil(block)) {
        return landingDamageRamp(kAnvilDamagePerCell, kAnvilFreeCells, kAnvilDamageCap, fellCells);
    }
    if (block == BlockId::PointedDripstone) {
        return landingDamageRamp(kDripstoneDamagePerCell, kDripstoneFreeCells, kDripstoneDamageCap,
                                 fellCells);
    }
    return 0;
}

// Written against the published figures rather than against the constants, so
// halving `kAnvilDamagePerCell` fails here rather than silently making the trap
// useless. Twenty-one cells is the first fall that reaches the anvil's cap.
static_assert(anvilLandingDamage(BlockId::Anvil, 1.0f) == 0 &&
                  anvilLandingDamage(BlockId::Anvil, 4.0f) == 6 &&
                  anvilLandingDamage(BlockId::Anvil, 21.0f) == 40 &&
                  anvilLandingDamage(BlockId::Anvil, 300.0f) == 40 &&
                  anvilLandingDamage(BlockId::Sand, 40.0f) == 0,
              "two per block after the first, capped at forty, and only an anvil hurts anything");

// The stalactite's own figures. Forty-two cells is the first fall that reaches
// its cap, because two of them are free.
static_assert(anvilLandingDamage(BlockId::PointedDripstone, 2.0f) == 0 &&
                  anvilLandingDamage(BlockId::PointedDripstone, 3.0f) == 1 &&
                  anvilLandingDamage(BlockId::PointedDripstone, 10.0f) == 8 &&
                  anvilLandingDamage(BlockId::PointedDripstone, 42.0f) == 40 &&
                  anvilLandingDamage(BlockId::PointedDripstone, 300.0f) == 40,
              "one per block after the first two, capped at forty");

// **The negative twin, and it has teeth.** Handing the stalactite the anvil's
// row is the obvious way to widen this function and it is wrong twice over -
// double the rate and one free cell instead of two. Both wrong answers are
// named here against the case that separates them, so the mistake cannot be
// made silently. It also pins Java's discarded minimum: a one-cell-past-free
// drip does 1, not 6.
static_assert(anvilLandingDamage(BlockId::PointedDripstone, 10.0f) !=
                      anvilLandingDamage(BlockId::Anvil, 10.0f) &&
                  landingDamageRamp(kAnvilDamagePerCell, kAnvilFreeCells, kAnvilDamageCap, 10.0f) ==
                      18 &&
                  anvilLandingDamage(BlockId::PointedDripstone, 3.0f) != 6,
              "a stalactite must not be paid at the anvil's rate, nor at Java's minimum of six");

// **The drift assert, and it is the one that would have caught the bug.** The
// left-hand value is the exact figure the real physics loop delivered for a
// nineteen-block drop; the right-hand one is what that fall means. Every other
// assert in this file hands the ramp a whole number, which is the single input
// `update` never produces.
static_assert(anvilLandingDamage(BlockId::Anvil, 18.999998093f) ==
                      anvilLandingDamage(BlockId::Anvil, 19.0f) &&
                  anvilLandingDamage(BlockId::Anvil, 18.999998093f) == 36 &&
                  anvilLandingDamage(BlockId::PointedDripstone, 28.999996185f) ==
                      anvilLandingDamage(BlockId::PointedDripstone, 29.0f),
              "accumulated float drift is costing a whole block of landing damage again - see "
              "kFellCellsTolerance");

// **The negative twin: the tolerance must repair drift without becoming a
// round.** A genuinely fractional descent still floors, because that is what
// the reference does - so 10.6 blocks is worth ten blocks, not eleven. Naming
// `static_cast<int>` of the drifted value here pins the original trap too, so
// anyone who "simplifies" the tolerance away fails on the line that explains it.
static_assert(landingDamageRamp(kAnvilDamagePerCell, kAnvilFreeCells, kAnvilDamageCap, 10.6f) ==
                      landingDamageRamp(kAnvilDamagePerCell, kAnvilFreeCells, kAnvilDamageCap,
                                        10.0f) &&
                  landingDamageRamp(kAnvilDamagePerCell, kAnvilFreeCells, kAnvilDamageCap, 10.6f) !=
                      landingDamageRamp(kAnvilDamagePerCell, kAnvilFreeCells, kAnvilDamageCap,
                                        11.0f) &&
                  static_cast<int>(18.999998093f) == 18,
              "the tolerance must floor like the reference, not round to nearest");

/// Sand and gravel on the way down, drawn as they will look when they land and
/// moving smoothly.
///
/// **The world hands these over already detached.** `World` sets the cell to air
/// and reports it, exactly as it reports a plant a flow swept aside, because it
/// has no idea entities exist. That keeps the falling *block* in the world and
/// the falling *animation* out of it.
///
/// Stepping one cell per scheduled update would have been a tenth of this code
/// and it reads as teleporting: the whole point of a falling block is that you
/// can watch it accelerate.
class FallingBlocks {
public:
    void spawn(const glm::ivec3& cell, BlockId block);

    /// **A block that owes the world an item at this position**, for the caller
    /// to turn into a drop. Same shape and the same reason as
    /// `World::WashedBlock`.
    ///
    /// Two different things arrive through this one channel, and the widening is
    /// deliberate rather than lazy:
    ///
    /// - something the landing **displaced** - a fluid, a fire, a tuft of grass
    ///   the cube came to rest in; and
    /// - **the falling cube itself**, when the reference says it may not become
    ///   a block again: it landed on something that is not a full-height
    ///   surface, the cell it stopped in was already occupied by something that
    ///   is not replaceable, or it ran out its thirty seconds in the air. See
    ///   `landsAsABlock`, and read its warning before changing which of the two
    ///   ends up here.
    ///
    /// Both are "this position now owes an item", which is the only question the
    /// caller asks, so they are one list. The name is kept because
    /// `Main.cpp` spells it, not because it is the better word.
    ///
    /// **Never both at once.** A landing either places the cube and displaces
    /// what was there, or breaks the cube and leaves what was there alone -
    /// so one landing is at most one entry, and the item count is conserved
    /// either way.
    struct Crushed {
        glm::ivec3 position;
        BlockId block;

        /// **Which of the two arms above produced this**, because `block`
        /// alone cannot say and every caller was re-deriving it.
        ///
        /// `false` - something the landing *displaced*, which owes the mining
        /// drop table: sand landing in tall grass owes what breaking that grass
        /// owes. `true` - the falling cube *itself*, which owes itself.
        ///
        /// `Main.cpp` used to answer this with `isFalling(hit.block)`, which is
        /// correct only because nothing replaceable has gravity - a derivation
        /// of exactly the kind this project's first bug shape is about, and one
        /// that needed a strided sweep over the whole enum to hold up. The
        /// knowledge is free here and reconstructed there, so it is stated
        /// here. A caller that keeps the old derivation still gets the same
        /// answer; this only removes the need for it.
        bool wasTheFaller = false;
    };

    /// **Every cube that stopped flying this frame, and what became of it.**
    ///
    /// Separate from `Crushed` because it is a different question: `Crushed`
    /// asks *what owes an item*, this asks *what just hit this cell and how
    /// hard*. An anvil that lands as a block owes nothing and still has to hurt
    /// whatever was standing there; a cube that breaks owes an item and hurts
    /// nothing.
    ///
    /// `fellCells` is measured **from where the cube started**, not from the top
    /// of the frame, because that is what the anvil's damage is a function of -
    /// hand it to `anvilLandingDamage`.
    struct Landed {
        glm::ivec3 cell;
        BlockId block;
        float fellCells = 0.0f;
        /// Whether it went back to being a block, or broke into an item.
        bool becameBlock = false;
    };

    /// Accelerates, lands, and writes whatever landed back into the world.
    ///
    /// **`landed` is an out-parameter rather than a widened return, and that is
    /// a fact about ownership rather than taste.** The return is the item
    /// channel and its caller's drain is unconditional; changing its type would
    /// break `Main.cpp`, which this pass does not own. Passing nothing costs
    /// nothing and accumulates nothing - which is also why this is not a
    /// `takeLandings()` drain, since an undrained one would grow without bound.
    ///
    /// **`Main.cpp` passes `landed` and applies the damage**, so a falling
    /// anvil hurts what it lands on. That call site asks
    /// `anvilLandingDamage` for *every* landing rather than testing for an
    /// anvil first, which is what keeps falling gravel harmless without a
    /// caller having to remember it - so widening that function is all it
    /// takes to make a new falling block dangerous. Verified by reading the
    /// call site, 2026-08-19. Negative claims rot fastest, so this one is
    /// dated and says what would make it false: `update(` being called with
    /// only two arguments everywhere outside this file.
    std::vector<Crushed> update(World& world, float deltaSeconds,
                                std::vector<Landed>* landed = nullptr);

    /// **Lands every cube still in the air, right now, wherever it would have
    /// come to rest.** For the shutdown path: `World::updateFalls` has already
    /// air-written the source cell and flagged that chunk modified, so a cube
    /// in flight when the game is saved is a block deleted from a saved world -
    /// knock the base out of a twenty-high sand pillar, quit inside the two
    /// seconds it takes to fall, and twenty sand are gone.
    ///
    /// **Chosen over persisting the flight state**, which would need a new
    /// `WorldStore` section and a format-version bump for a state that lasts
    /// under two seconds and that no player can tell apart from this. Finishing
    /// the fall writes the sand into the chunk that is about to be saved, needs
    /// no format change, and is nearer the reference than deleting it.
    ///
    /// Returns the same item channel `update` does, for the same reason: a cube
    /// that finishes its fall on a torch is still an item.
    ///
    /// **`Main.cpp`'s save path calls this and drains the vector**, which is
    /// what the note here used to ask for; a caller that merely called it and
    /// dropped the result would lose the cube that finished its fall on a
    /// torch. Verified by grep rather than assumed, 2026-08-19. Negative claims
    /// rot fastest, so this sentence is dated and says what would make it
    /// false: `settleAll(` appearing nowhere outside this file again.
    std::vector<Crushed> settleAll(World& world);

    /// Rebuilt every frame rather than transformed, for the same reason the
    /// drops are: world meshes are drawn with an identity model matrix.
    engine::MeshData buildMesh(const World& world, const DrawRange& range = {}) const;

    std::size_t count() const { return m_blocks.size(); }

private:
    struct Falling {
        /// Minimum corner of the cube, so the cell it occupies is a floor away.
        glm::vec3 position{0.0f};
        float velocity = 0.0f;
        /// Seconds this cube has been in the air, so it can be given up on.
        ///
        /// **Advanced before every early-out, including the one for an absent
        /// chunk, and the velocity deliberately is not.** The two are different
        /// questions and treating them alike is what leaked: a cube whose column
        /// streamed out from under it was skipped without ageing, so it lived in
        /// `m_blocks` for ever and mining sand near the loaded edge and sprinting
        /// away grew the list without bound. A skipped frame is a frame that did
        /// not happen *to the physics* - resuming from rest reads as a hover -
        /// but it is a frame that happened to the clock, and the reference's
        /// own timeout is wall time.
        float age = 0.0f;
        /// **Cells descended since it was spawned**, which is what an anvil's
        /// damage is measured against - so an accumulator, not an age. Only
        /// real movement counts: a cube waiting out an absent chunk has not
        /// fallen any further.
        float fellCells = 0.0f;
        BlockId block = BlockId::Air;
    };

    std::vector<Falling> m_blocks;
};

/// **What a landing does to the world, and it is the inverse of what looks
/// obvious.** See `landsAsABlock` above for the reference rule and for the
/// trap; the short version is that a cube landing on a torch loses - it becomes
/// an item and the torch stands.
///
/// Shared by the frame update and by `settleAll` rather than written twice.
/// This project's costliest recurring shape is a rule that exists, is correct,
/// and travelled to only one of the two places that needed it.
///
/// **Templated on the world for the same reason `fallingCubeRest` is templated
/// on the column**: item conservation is decided in these fifteen lines, and a
/// probe that re-types them proves the probe. It needs exactly `blockAt`,
/// `setBlock` and `primeTnt`.
template <typename WorldLike>
void applyFallingLanding(WorldLike& world, const glm::ivec3& cell, BlockId block, float fellCells,
                         std::vector<FallingBlocks::Crushed>& crushed,
                         std::vector<FallingBlocks::Landed>* landed) {
    const BlockId occupying = world.blockAt(cell.x, cell.y, cell.z);
    const BlockId support = world.blockAt(cell.x, cell.y - 1, cell.z);

    // **A primed charge never breaks, and it is the one deliberate divergence
    // here.** The reference does not make primed TNT a falling block at all -
    // it is its own entity, which lands on whatever surface it finds and
    // explodes - so there is no "drops as an item" case to copy. Ours arrives
    // through this file because that is where the animation lives, and turning
    // a lit charge into a TNT item on the way down would put out a fuse the
    // player has already committed to.
    const bool becomesABlock = isTntBlock(block) || landsAsABlock(occupying, support);

    if (becomesABlock) {
        // **The same question the resting cell was picked by**: everything a
        // landing displaces is reported, and only an empty cell had nothing in
        // it to displace. What a displaced cell is worth is the drop table's
        // question, not this file's - see the `dropForBlock` assert in
        // `FallingBlock.cpp` for why the fluids are no longer named here.
        if (occupying != BlockId::Air) {
            crushed.push_back(FallingBlocks::Crushed{cell, occupying, false});
        }
        world.setBlock(cell.x, cell.y, cell.z, block);
        // A charge that was lit when it started falling is still lit when it
        // lands. Its old fuse entry points at the cell it left and finds
        // nothing there, so the countdown has to be started again here.
        if (isTntBlock(block)) {
            world.setBlock(cell.x, cell.y, cell.z, BlockId::TntPrimed);
            world.primeTnt(cell);
        }
    } else {
        // **The cube pays, and whatever it landed on is not touched.** Writing
        // `crushed.push_back({cell, occupying})` here as well would hand the
        // caller two items for one landing and delete the torch into the
        // bargain; the item count is conserved precisely because these two arms
        // report exactly one thing each.
        crushed.push_back(FallingBlocks::Crushed{cell, block, true});
    }

    if (landed != nullptr) {
        landed->push_back(FallingBlocks::Landed{cell, block, fellCells, becomesABlock});
    }
}

/// **`Crushed` is built positionally at both sites above, and its field *names*
/// appear at neither.**
///
/// That combination is why this assert exists. Insert one bool-convertible
/// member between `block` and `wasTheFaller` and both `push_back` calls keep
/// compiling while silently writing the new field instead; `wasTheFaller` then
/// takes its default `false` at *both* arms, so the faller's own cube reports
/// itself as something the landing displaced and pays the mining drop table for
/// the block it landed on. For sand that is invisible - mining sand drops sand -
/// and for gravel it is a 10% flint instead of a gravel, which is this project's
/// first bug shape wearing a compiler's blessing.
///
/// **Measured 2026-08-18, and it is a false-positive class worth naming:** a
/// bare-name search for `wasTheFaller` across both of this pair's files finds
/// exactly **one** hit - the declaration - while the field is *written twice*.
/// The session's standing rule ("a name is only truly unreached if a bare-name
/// search also finds nothing") does **not** clear this one, because the bare
/// name genuinely occurs once. Only reading the field order and counting
/// positional initialisers does. A field-reachability sweep will call this dead.
///
/// The two clauses below are not the substitution trap that hollowed out
/// `everyFaceKnowsWhichItIs`: the left side is built by *position* and read by
/// *name*, which are two independent derivations of the same fact, so neither
/// clause reduces to `x != x`.
constexpr bool crushedPositionsStillMeanWhatTheySay() {
    const FallingBlocks::Crushed occupant{glm::ivec3{4, 5, 6}, BlockId::Gravel, false};
    const FallingBlocks::Crushed faller{glm::ivec3{4, 5, 6}, BlockId::Gravel, true};
    const FallingBlocks::Crushed defaulted{glm::ivec3{4, 5, 6}, BlockId::Gravel};
    return occupant.position.x == 4 && occupant.position.y == 5 && occupant.position.z == 6
           && occupant.block == BlockId::Gravel
           && occupant.wasTheFaller == false   // slot 3 is the flag, not the block
           && faller.wasTheFaller == true      // and it is the slot that carries `true`
           && defaulted.wasTheFaller == false; // a dropped third argument reads "displaced"
}
static_assert(crushedPositionsStillMeanWhatTheySay(),
              "Crushed's third positional slot is no longer wasTheFaller. Both push_back "
              "sites in applyFallingLanding build it positionally - fix them together.");

/// **The negative twin, and the one that actually catches the edit above.**
///
/// The positive assert still passes after a fourth member is *appended*, because
/// appending does not disturb slots 1-3. This one pins the arity: a four-element
/// braced initialiser must remain ill-formed. Its positive control sits beside
/// it so a reader can see the detector distinguishes the two cases rather than
/// answering the same way to both - the failure mode that made three of tonight's
/// sweeps return a uniform value and look healthy while measuring nothing.
template <typename T>
concept CrushedTakesThree = requires { T{glm::ivec3{}, BlockId::Air, false}; };
template <typename T>
concept CrushedTakesFour = requires { T{glm::ivec3{}, BlockId::Air, false, false}; };

static_assert(CrushedTakesThree<FallingBlocks::Crushed>,
              "control: Crushed must still accept its three documented slots");
static_assert(!CrushedTakesFour<FallingBlocks::Crushed>,
              "Crushed grew a member. The two positional push_back sites in "
              "applyFallingLanding name none of its fields - re-read them before "
              "trusting wasTheFaller.");

} // namespace game
