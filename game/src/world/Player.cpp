#include "world/Player.hpp"

#include "world/Campfire.hpp"
#include "world/Collision.hpp"
#include "world/Fluid.hpp"
#include "world/Tick.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game {
namespace {

using namespace player_constants;

/// A single frame may not advance further than this. At 20 fps the player still
/// moves under a quarter of a block per step, which keeps the one-block-deep
/// assumption in the collision resolver valid.
constexpr float kMaxDeltaSeconds = 0.05f;

/// **It is one tick, and until now nothing said so.** The same clamp is spelled
/// out as a bare `0.05f` in five files - here, `ItemEntity.cpp`,
/// `FallingBlock.cpp`, `Particles.cpp` and `Creature.cpp` - and only the last of
/// those tied its copy to anything. This is that assert, copied verbatim from
/// `Creature.cpp`'s own `kMaxDeltaSeconds` so the two read identically:
///
/// The first half pins it to the one owner (`Tick.hpp`), so the clamp follows a
/// tick-rate change instead of being left behind. The second half is the
/// **absolute anchor**, and it is the half that does the work: both sides of the
/// first are derived, so moving the tick rate coherently would satisfy it, and
/// this clamp is not free to move - it is what makes `kMaxStepDistance` above
/// sufficient at terminal velocity.
///
/// The remaining three copies are still unpinned and are not this pass's files.
static_assert(kMaxDeltaSeconds == tick::kSeconds && kMaxDeltaSeconds == 0.05f,
              "a frame may advance the player by at most one simulation tick, and that tick is "
              "50 ms - the figure every ported constant in this file is quoted against");

/// Longest distance any one collision step may cover. Movement is split into
/// however many steps this requires.
constexpr float kMaxStepDistance = 0.4f;

/// **What makes the resolver non-tunnelling, and the only thing that does.** A
/// sub-step shorter than the narrowest span of the body means the swept region
/// is always covered by the destination box, at terminal velocity or any other
/// speed. Raise `kMaxStepDistance` past `kWidth` and a player falling at 78.4
/// m/s starts passing through single blocks - that is the edit this fires on.
static_assert(kMaxStepDistance < kWidth && kMaxStepDistance < kSneakHeight,
              "a collision sub-step must be shorter than the body it moves");

/// What fraction of the camera's remaining step lag survives each 50 ms tick.
/// Half every tick is about a sixth of a second to settle - long enough to read
/// as a rise, short enough that aiming never feels behind the view.
constexpr float kStepSmoothPerTick = 0.5f;

/// Keeps the box a hair away from surfaces it is resting against, so a resolved
/// contact does not immediately re-report as a collision.
constexpr float kSkin = kCollisionSkin;

Aabb boxAt(const glm::vec3& feet, float height) {
    constexpr float half = kWidth * 0.5f;
    return Aabb{{feet.x - half, feet.y, feet.z - half}, {feet.x + half, feet.y + height, feet.z + half}};
}

/// True if solid ground sits within `probeDepth` of the player's footprint.
///
/// Probes a slab below the feet rather than a single point, so standing with
/// only a corner over a block still counts as supported.
///
/// **The default is the step height and that is not a coincidence.** Its only
/// caller is the sneak edge guard, and the reference states that rule as
/// "sneaking prevents drops higher than `step_height` - it uses the same
/// attribute, not a separate constant" (`RESEARCH.md` §1.6, the sneak-drop line
/// under the `step_height` table). At the 0.1 m
/// this used to hold, a crouched player was stopped dead by every slab, stair
/// and path block: you could sneak *up* a staircase and not back down it, which
/// is the opposite of what crouching is for.
bool hasGroundBelow(const World& world, const glm::vec3& feet, float probeDepth = kStepHeight) {
    Aabb probe = boxAt(feet, 0.0f);
    probe.min.y = feet.y - probeDepth;
    probe.max.y = feet.y;
    return overlapsSolid(world, probe);
}

/// The ladder or vine the body is standing in, or `Air`.
///
/// Whole cells rather than the block's own geometry, and deliberately: neither
/// has a collision box at all, so testing the shape would never report a touch
/// and nothing would ever climb.
BlockId climbableAt(const World& world, const Player& player) {
    const Aabb box = boxAt(player.position, kHeight);
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kSkin));
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kSkin));
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockId id = world.blockAt(x, y, z);
                if (isClimbable(id)) {
                    return id;
                }
            }
        }
    }
    return BlockId::Air;
}

/// Whether any cell the body occupies holds a given block.
///
/// **Whole cells, for exactly the reason `climbableAt` above uses them**: the
/// two blocks that ask this - a cobweb and powder snow - both have no collision
/// box, so asking the block's *shape* would never report a touch and nothing
/// would ever be slowed. That is the same reason, written once.
///
/// **One function rather than two near-identical loops.** The second caller
/// arrived with powder snow on 2026-08-19 and differed from the first by a
/// single `BlockId`, which is the signal that the id is the argument. A third
/// copy of this six-line bound calculation would have been the third place for
/// an off-by-one in the `- kSkin` that keeps a body from claiming the cell it is
/// merely touching.
bool bodyHolds(const World& world, const glm::vec3& feet, float height, BlockId want) {
    const Aabb box = boxAt(feet, height);
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kSkin));
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kSkin));
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                if (world.blockAt(x, y, z) == want) {
                    return true;
                }
            }
        }
    }
    return false;
}

/// Whether any cell the body occupies holds a cobweb.
bool inCobweb(const World& world, const glm::vec3& feet, float height) {
    return bodyHolds(world, feet, height, BlockId::Cobweb);
}

/// Whether any cell the body occupies holds powder snow.
///
/// **This is the whole of "am I in powder snow", for the freezing clock and for
/// the movement branch both** - one owner, because the alternative is the shape
/// that already cost this project finding 878, where standing in lava is decided
/// in two places by two slightly different bounds.
///
/// **It does not test whether you *fell in*, and it must not.** Bedrock's rule
/// is that leather boots keep you on the surface, so a player wearing them
/// stands on top and this returns false because the body is in the cell above.
/// That falls out of the geometry rather than needing a flag - as long as the
/// block itself has no collision box, which is `Block.hpp`'s side of this and is
/// filed, not done.
bool inPowderSnow(const World& world, const glm::vec3& feet, float height) {
    return bodyHolds(world, feet, height, BlockId::PowderSnow);
}

/// Whether the body is up against the *side* of a honey block.
///
/// The box is widened horizontally by a few skins and only horizontally: the
/// resolver parks a body exactly `kSkin` short of whatever it is touching, so
/// nothing narrower would ever reach the cell it is resting against, and
/// widening the vertical would catch the block you are standing **on** - which
/// is the walk-and-jump rule, not this one.
bool againstSticky(const World& world, const glm::vec3& feet, float height) {
    constexpr float kReach = kSkin * 4.0f;
    Aabb box = boxAt(feet, height);
    box.min.x -= kReach;
    box.max.x += kReach;
    box.min.z -= kReach;
    box.max.z += kReach;
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kSkin));
    for (int y = minY; y <= maxY; ++y) {
        for (int z = static_cast<int>(std::floor(box.min.z));
             z <= static_cast<int>(std::floor(box.max.z)); ++z) {
            for (int x = static_cast<int>(std::floor(box.min.x));
                 x <= static_cast<int>(std::floor(box.max.x)); ++x) {
                if (isSticky(world.blockAt(x, y, z))) {
                    return true;
                }
            }
        }
    }
    return false;
}

/// Which way you have to push to climb: toward whatever the block hangs on.
///
/// **A vine can hang on several sides at once, and pushing toward any of them
/// counts.** So this returns a mask rather than a single direction, and a vine
/// with no side at all - a curtain hanging from the one above - is climbed from
/// any direction, which is the reference's own behaviour.
glm::vec3 climbPush(BlockId block, const glm::vec2& wish, bool& anyDirection) {
    anyDirection = false;
    if (isLadder(block)) {
        switch (ladderFacing(block)) {
        case FaceDirection::PosX:
            return {1.0f, 0.0f, 0.0f};
        case FaceDirection::NegX:
            return {-1.0f, 0.0f, 0.0f};
        case FaceDirection::PosZ:
            return {0.0f, 0.0f, 1.0f};
        default:
            return {0.0f, 0.0f, -1.0f};
        }
    }

    const std::uint8_t sides = vineSides(block);
    if (sides == 0) {
        anyDirection = true;
        return {0.0f, 0.0f, 0.0f};
    }
    glm::vec3 best{0.0f};
    float bestPush = 0.0f;
    const std::array<std::pair<std::uint8_t, glm::vec3>, 4> options{{
        {ConnectNorth, {0.0f, 0.0f, -1.0f}},
        {ConnectSouth, {0.0f, 0.0f, 1.0f}},
        {ConnectWest, {-1.0f, 0.0f, 0.0f}},
        {ConnectEast, {1.0f, 0.0f, 0.0f}},
    }};
    for (const auto& [bit, direction] : options) {
        if ((sides & bit) == 0) {
            continue;
        }
        const float push = wish.x * direction.x + wish.y * direction.z;
        if (push > bestPush) {
            bestPush = push;
            best = direction;
        }
    }
    return best;
}

/// Seconds of air, and the drowning clock, advanced from the head alone.
///
/// The damage is applied on the shared hazard cadence in `applyHazards` below,
/// two health a second once the meter is past empty.
void updateBreath(Player& player, float dt) {
    using namespace fluid;

    if (player.underwater) {
        // Water breathing freezes the bar rather than refilling it, which is
        // what the reference does: surfacing still tops it up the usual way.
        if (player.effects.level(effects::Effect::WaterBreathing) > 0) {
            return;
        }
        player.air = std::max(0.0f, player.air - dt);
        if (player.air <= 0.0f) {
            player.drowningSeconds += dt;
        }
        return;
    }

    // Refilling is a fixed time from empty to full rather than a fixed rate, so
    // `inhale_time` transfers straight across.
    player.air = std::min(kAirSeconds, player.air + dt * kAirSeconds / kInhaleSeconds);
    player.drowningSeconds = 0.0f;
}

/// Drowning damage, expressed on the shared hazard cadence.
///
/// **`Fluid.hpp` says it owns the water constants and it means it**, so the rate
/// is read from there - `kDrownDamage` points every `kDrownInterval`. The hazard
/// cadence is faster than the drown interval, so one tick is the rate divided by
/// however many hazard ticks fit inside one interval.
///
/// `survival::kDrownDamagePerSecond` used to be a second copy of the same figure
/// and was **deleted on 2026-08-18** once this became its only would-be reader;
/// `Survival.hpp`'s hazard block carries the note saying it must not come back.
constexpr int kDrownHazardTicks =
    static_cast<int>(fluid::kDrownInterval / survival::kHazardInterval);
constexpr int kDrownDamagePerHazardTick = fluid::kDrownDamage / kDrownHazardTicks;

/// **Set `fluid::kDrownDamage` to 3 and this fires**, because one point twice a
/// second is no longer three points a second - the division would silently lose
/// the odd one. Halving `kDrownInterval` fires it the same way.
static_assert(kDrownDamagePerHazardTick * kDrownHazardTicks == fluid::kDrownDamage &&
                  static_cast<float>(kDrownHazardTicks) * survival::kHazardInterval ==
                      fluid::kDrownInterval,
              "drowning must divide evenly into the shared hazard cadence");

/// What a bed leaves of the fall it catches. **The distance, not the damage**
/// (`RESEARCH.md` §1.10, beside hay and honey), so the three free blocks come
/// off *after* the halving: 40 blocks are charged as 20 - 3, not as (40 - 3)/2.
constexpr float kBedFallScale = 0.5f;

/// What a honey block leaves of the **damage**, and the whole reason
/// `chargeFall` takes two scales rather than one. "As with hay bales, falling
/// onto a honey block reduces fall damage by 80%" (minecraft.wiki, *Honey
/// Block*) - which `RESEARCH.md` §1.10 records on the *same line* as the bed's
/// halving of the distance, and only one of the two blocks named there ever got
/// its rule. Applying honey's figure as a distance scale instead would make a
/// 40-block drop cost 5 rather than 7.
constexpr float kHoneyFallDamageScale = 0.2f;

/// How high you may jump off honey, in blocks. "Players, who can ordinarily
/// jump about 1 1/4 blocks high, can jump about 3/16 blocks high on honey; this
/// is an 85% reduction" (minecraft.wiki, *Honey Block*).
constexpr float kHoneyJumpHeight = 0.1875f;

/// The ordinary jump height, in blocks, **re-derived rather than written down
/// again**. `Player.hpp`'s apex assert on `kJumpVelocity` already proves this
/// pair gives the reference's 1.25, so a
/// second copy of 1.25 here would be a number with two owners; honey's scale is
/// taken against this so that retuning either constant keeps the 3/16 exact.
constexpr float kPlainJumpHeight = kJumpVelocity * kJumpVelocity / (2.0f * kGravity);

/// A ladder's climb and slide - **hoisted out of the climb branch that used to
/// own them**, because the side of a honey block now needs the second one:
/// "Entities pressed against the sides of a honey block slide down at a slow
/// speed and do not take fall damage, similar to going down a ladder"
/// (minecraft.wiki, *Honey Block*). The wiki publishes no separate figure for
/// the honey slide, and "similar to a ladder" is the source for using this one
/// rather than inventing a second.
///
/// **Both are blocks per second**, the unit `velocity.y` is already in and the
/// one `kGravity` and `kJumpVelocity` are measured against, which is what lets
/// `kPlainJumpHeight` above divide them cleanly. The unit is named because
/// `CLAUDE.md` bug shape #3 is a number landing in a field measured in a
/// different one, and a bare speed is exactly the shape that hides it.
///
/// **Source and its tag status:** minecraft.wiki *Ladder* - the player "moves
/// upward at about 2.35 blocks per second", and "the player's maximum downward
/// speed is reduced to a `descending ladder` speed, at about 3 blocks per
/// second". **Neither bullet carries an edition tag.** The page states its
/// Bedrock-only differences separately - holding jump climbs faster, and a
/// waterlogged ladder cannot be climbed - so the untagged figures are the best
/// available for both editions rather than a Java reading. The first of those
/// Bedrock rules is the one implemented below, so the page is describing the
/// ladder this game actually has.
///
/// **The descent figure was wrong by more than half, and wrong in its ordering.**
/// It was an unsourced `kSlideSpeed = 1.4f` sitting inline in the climb branch
/// before 2026-08-19 and it came through the hoist unexamined. At 1.4 you slid
/// DOWN a ladder more slowly than you climbed UP it - no reference supports
/// that, and the assert below now forbids it outright. Fixing it also speeds
/// the honey slide, which is correct: it shares this number by the quotation
/// above, and sharing is the point rather than an accident.
///
/// **This one is feel and the playtester owns feel** - a ladder descent is now
/// a little over twice as quick. The number is the published one; whether it
/// *plays* right is a judgement only the user can make.
constexpr float kLadderClimbSpeed = 2.35f;
constexpr float kLadderSlideSpeed = 3.0f;

/// **You descend faster than you ascend.** The structural claim the old pair
/// violated, asserted rather than commented because it survives someone
/// retuning either number for feel: a retune that crosses this line is a typo
/// rather than a taste.
static_assert(kLadderSlideSpeed > kLadderClimbSpeed,
              "you descend a ladder faster than you climb it - 3.0 against 2.35 blocks per "
              "second (minecraft.wiki, Ladder); the pair before 2026-08-19 had this backwards");
/// The negative twin, so the assert above cannot be satisfied by both being
/// equal, by either going negative, or by someone zeroing the climb.
static_assert(!(kLadderClimbSpeed > kLadderSlideSpeed) && kLadderClimbSpeed > 0.0f &&
                  kLadderSlideSpeed > 0.0f,
              "both are positive magnitudes - the descending sign is applied at the call "
              "site, which is what lets the honey slide reuse this figure unchanged");
/// What a cobweb leaves of your speed: "the player can move at a speed of about
/// 25% of the normal walking speed" (minecraft.wiki, *Cobweb*).
constexpr float kCobwebSpeedScale = 0.25f;

/// And how fast anything may move vertically inside one, in metres per second.
/// The same page: mobs "fall approximately 0.078 blocks/second while within it,
/// taking nearly 13 seconds to fall 1 full block."
///
/// **Clamped in both directions from that one published figure.** The wiki says
/// only that jump height is "severely reduced" and gives no number for it; the
/// reference gets the slow fall and the crushed jump from a *single* per-tick
/// vertical multiplier, so one crawl speed applied either way reproduces the
/// pair without a second number being invented.
constexpr float kCobwebCrawlSpeed = 0.078f;

// --- Powder snow. **Its own branch, its own numbers, and deliberately not a
// --- fluid** - `Fluid.hpp` carries the `static_assert` that keeps it out of
// --- `FluidKind`, and the ledge-climb comment at the foot of `updatePlayer`
// --- explains why its climb-out is a different mechanic rather than a variant
// --- of water's.
//
// These live here rather than in `Survival.hpp` because they are movement, and
// this is where every other block-that-changes-how-you-move keeps its figures:
// the cobweb pair above, the ladder pair above that, honey's jump and fall
// scales. `Survival.hpp` owns the freezing clock, which is health.

/// **What powder snow leaves of your horizontal speed, per tick.**
///
/// **This is a drag factor, not a speed scale, and the distinction is the whole
/// of why the number is 0.9 and not 0.25.** Bedrock publishes no figure at all -
/// `player.json` has nothing about powder snow and the wiki gives only the prose
/// "move much slower in it, similar to cobwebs". Java publishes it, in this unit:
/// `PowderSnowBlock` calls `makeStuckInBlock` with `(0.9, 1.5, 0.9)` where the
/// cobweb beside it in the same table uses `(0.25, 0.05, 0.25)`. So it is
/// `[JE]`-sourced, and it is applied through `fluid::dragOver` - the same
/// converter lava's fall halving uses - because a per-tick retention factor
/// dropped into a metres-per-second field is `CLAUDE.md` bug shape #3 exactly.
///
/// **The wiki's "similar to cobwebs" is not the source and must not become
/// one.** The two numbers come from one table and differ by more than a factor
/// of three, which is the evidence that the phrase is a loose comparison rather
/// than an equality. Building this at the cobweb's 0.25 because a sentence said
/// "similar" is the mistake that sentence invites, and the previous owner of
/// this file wrote that warning down before the physics existed. Honoured.
///
/// **Feel, and the playtester owns feel.** What this settles at is not
/// calculable from this line alone - it is a drag against the movement code's
/// own easing toward the target speed, so the steady state depends on both -
/// but the intent is a noticeable clinginess and nothing like a web. If powder
/// snow plays as too mild or too fierce, **this is the one dial**, and it is the
/// one figure in the whole mechanic that is not published for Bedrock. Every
/// other number here is sourced and should not be touched for feel.
constexpr float kPowderSnowDrag = 0.9f;
static_assert(kPowderSnowDrag > kCobwebSpeedScale && kPowderSnowDrag < 1.0f,
              "powder snow is milder than a cobweb and is still a slowdown - the two figures "
              "come from one table in the reference and are not the same number");

/// **How fast you sink through it, in metres per second**, and it is a constant
/// descent rather than a fall: you do not accelerate inside powder snow, which
/// is what makes a two-block drift feel like sinking rather than like falling.
///
/// **Derived from constants this file already owns rather than written down.**
/// The reference reaches the same behaviour by zeroing the entity's momentum
/// every tick and scaling that tick's own movement by 1.5 (`[JE]`, the vertical
/// component of the `makeStuckInBlock` triple above), so what an entity travels
/// in a tick is one tick of gravity from rest, times that scale - and one tick
/// of gravity from rest is `kGravity * tick::kSeconds`, both of which are here.
/// 32 x 0.05 x 1.5 = 2.4 m/s. Writing `2.4f` instead would be a third owner of
/// the gravity constant.
constexpr float kPowderSnowVerticalScale = 1.5f;
constexpr float kPowderSnowSinkSpeed = kGravity * tick::kSeconds * kPowderSnowVerticalScale;

/// **You sink faster than you sink in water and slower than you fall in air.**
/// The structural claim, asserted because it is what a retune would break and
/// because both bounds are real: water's `kSinkSpeed` is 0.5 m/s and free fall
/// tops out at `kTerminalVelocity`. Powder snow between the two is what makes it
/// a trap you can climb out of with the right boots and drown in without them.
static_assert(kPowderSnowSinkSpeed > fluid::kSinkSpeed &&
                  kPowderSnowSinkSpeed < kTerminalVelocity,
              "sinking through powder snow is quicker than sinking in water and far slower than "
              "falling through air");
/// The scale is a real one, and it is bounded rather than compared against
/// itself. **An assert reading `kPowderSnowSinkSpeed == kGravity *
/// tick::kSeconds * kPowderSnowVerticalScale` would be `X == X`** - one side of
/// a derivation against itself, which this project has already paid for eleven
/// times over and which proves nothing about the world. What is asserted
/// instead is a claim with content: the sink speed sits strictly between one
/// unscaled tick of gravity and two of them, which pins the scale into (1, 2)
/// without restating it. **The single edit it fires on: a literal written in
/// place of the derivation that is not between 1.6 and 3.2.**
static_assert(kPowderSnowVerticalScale > 1.0f &&
                  kPowderSnowSinkSpeed > kGravity * tick::kSeconds &&
                  kPowderSnowSinkSpeed < kGravity * tick::kSeconds * 2.0f,
              "the sink speed is one tick of gravity scaled by more than one and less than two - "
              "a literal here would be a second owner of kGravity");

/// The whole of the damage rule, plus the one dial the public `damagePlayer`
/// cannot expose: whether Resistance is allowed to touch this blow.
///
/// **Three sources in the reference ignore Resistance entirely** - "Resistance
/// reduces incoming damage by 20% x level from all sources except for
/// starvation, the void, and /kill" (`minecraft.wiki/w/Resistance`), and
/// `Food`'s starvation section says the same from the other end: it "ignores
/// armor and armor toughness, the Protection enchantment, and the Resistance
/// effect". We have two of those three and no `/kill`.
///
/// **It is a separate flag from `bypassInvulnerability` and must stay one.**
/// Every unresistable source happens to bypass the window, but the reverse is
/// not true: poison and wither bypass it too and are resisted normally, so
/// folding the two into one bool would quietly make Turtle Master a poison
/// cure. That is the whole reason this is not simply read off the existing
/// argument.
///
/// **Everything else is shared, which is the point of routing both through
/// here.** `damageTakenScale` returns zero at Resistance V, so before this
/// split a player under it could not starve and could not die in the void:
/// they fell forever, at full health, with no way out but a respawn they were
/// never offered. Resistance III already reached zero on starvation's single
/// point, because 1 x 0.4 rounds to nothing.
bool damageWithResistance(Player& player, int amount, bool bypassInvulnerability, bool resistable,
                          const survival::ArmourSet& armour) {
    if (amount <= 0 || !player.alive()) {
        return false;
    }

    // **The overwrite rule, and it is what makes melee survivable** - now in
    // `Survival.hpp`, because `Creature.cpp` carried the identical rule and
    // neither file could own the other. What comes back is the **raw** amount
    // that got through: zero when the blow was no larger than the one that armed
    // the window, the difference when it was larger, the whole of it when
    // nothing was running. It arms on the raw blow and never restarts a running
    // window (§2.4).
    const int landed = survival::chargeDamageWindow(player.invulnerableSeconds, player.lastDamage,
                                                    amount, bypassInvulnerability);
    if (landed <= 0) {
        return false;
    }

    // **Armour, and it sits exactly here: after the window, before Resistance.**
    // minecraft.wiki's *Armor* page puts enchantment protection on "only the
    // damage that got through the armor", and *Resistance* puts itself "after
    // all other damage reductions (armor and enchantments)". We have no
    // enchantments, so those two sentences collapse to armour-then-Resistance.
    // **Both statements are untagged**, so this is the documented order rather
    // than a Bedrock-confirmed one, and it is recorded as such - no page in
    // either edition tags the pipeline order at all.
    //
    // It must be after `chargeDamageWindow` for the reason already written
    // above: the window compares the *raw* blow, "before accounting for armor,
    // enchantments, or status effects". Moving armour ahead of it would let a
    // well-armoured player be hit repeatedly by blows the window should have
    // swallowed.
    const float afterArmour = survival::armourDamageTaken(landed, armour);

    // **Durability is charged on what came in, not on what survived** - and on
    // `landed` rather than `amount`, which is the one place those two differ.
    // A blow the window refused outright never reaches here, so it costs
    // nothing; a blow the window only partly swallowed costs what it actually
    // delivered. MCPE-165149 states the raw-not-mitigated half explicitly, and
    // the *Resistance* page confirms it from the other end: armour "still
    // lose[s] the usual amount of durability" at Resistance V, where nothing
    // lands at all. Charging `afterArmour` here would more than double how long
    // a set lasts - `Survival.hpp` has the negative twin for exactly that slip.
    //
    // **Guarded on there being armour to wear**, which is the same test as
    // "armour applies to this blow" because a source that ignores armour passes
    // `kNoArmour`. That is what keeps suffocation and drowning from grinding a
    // set away for damage they never reduced.
    if (armour.defence > 0) {
        player.armourWear += survival::armourDurabilityCost(landed);
    }

    // **Resistance is applied after the comparison, never before it** - §2.4:
    // "the comparison is made *before* armour, enchantments and effects". So the
    // window still remembers the raw blow, and a blow resisted down to nothing
    // still arms it and still flashes, which is the reference's own split
    // between deciding a hit happened and deciding what it costs. Without this
    // line Resistance did nothing whatsoever, and Turtle Master - the only brew
    // that grants it - was a pure downside: a fraction of your speed in exchange
    // for no protection at all. **It stays here rather than behind the window,
    // because a creature has no effects to scale by** and the shared rule has to
    // serve both.
    const float scale = resistable ? effects::damageTakenScale(player.effects) : 1.0f;
    const float scaled = afterArmour * scale;
    const int taken = static_cast<int>(std::lround(scaled));

    // **Absorption is spent before health, and after everything that decides
    // how big the blow is.** "The absorption health points are depleted first,
    // followed by the standard health points ... cannot be replenished by
    // natural regeneration or other effects" (`minecraft.wiki/w/Absorption`).
    // Last in the chain is the whole point: an enchanted apple's sixteen points
    // buy sixteen *resisted* points, not sixteen raw ones, and putting this
    // ahead of `damageTakenScale` would quietly make the game's most expensive
    // consumable worth 2.5x less under Turtle Master.
    //
    // The balance is whole by construction - four points a level, asserted in
    // `Effects.hpp` - so the integer conversion cannot shed a fraction into
    // health. The seeding and the clearing live in `tickPassiveTimers`, because
    // that is the only place that can tell a fresh grant from a spent one.
    const int fromAbsorption = std::min(taken, static_cast<int>(player.absorption));
    player.absorption -= static_cast<float>(fromAbsorption);

    player.health = std::max(0, player.health - (taken - fromAbsorption));
    player.hurtFlash = survival::kHurtFlashSeconds;
    // Being hurt costs hunger, the same way hurrying and healing do.
    player.exhaustion += survival::kExhaustDamaged;
    return true;
}

/// Charges whatever fall the accumulator has banked, and clears it.
///
/// **One owner for the formula**, because coming to rest is not the only place
/// a fall is paid for: the reference treats bouncing off a bed as a landing in
/// its own right - half the distance, charged and reset there and then, and
/// only then the bounce. A second copy of `floor((distance - safe) * multiplier)`
/// beside the bounce is exactly how the two would drift apart.
///
/// **Two scales, because the surfaces that catch you do not all scale the same
/// thing.** `distanceScale` is what the surface leaves of the *fall* - 1 for
/// ordinary ground, `kBedFallScale` for a bed, so the three free blocks come off
/// after it. `damageScale` is what it leaves of the *damage*, which is how hay
/// and honey are stated, and the free blocks come off before it. Collapsing
/// them into one parameter is the "ported into a field measured in a different
/// unit" mistake: on a 40-block drop the same 0.2 gives 5 as a distance and 7
/// as a damage.
///
/// Nothing here re-checks Creative, flight or death, and it does not need to -
/// every early-out in `updateSurvival` zeroes the accumulator on the frame it
/// takes, so anyone exempt arrives with nothing banked, and `damagePlayer`
/// refuses a corpse on its own.
void chargeFall(Player& player, float distanceScale, float damageScale = 1.0f) {
    const float raw = std::floor((player.fallDistance * distanceScale -
                                  survival::kSafeFallDistance) *
                                 survival::kFallDamagePerBlock);
    const int hurt = static_cast<int>(std::floor(std::max(0.0f, raw) * damageScale));
    player.fallDistance = 0.0f;
    if (hurt > 0) {
        // **`kNoArmour`, explicitly, and it is not a change** - the default
        // third argument here has always been "no reduction". Spelled out
        // because every other unreduced source in this file spells it out at its
        // own call site, and a reader auditing "which hazards ignore armour?"
        // was reading a list with a hole in it.
        //
        // **Armour genuinely does not reduce fall damage in either edition.**
        // minecraft.wiki, *Armor*: falling is among the damage types armour does
        // not protect against. What reduces it is Feather Falling and
        // Protection, which are enchantments, and this game has none - so when
        // enchanting arrives the reduction belongs *there*, not in an
        // `ArmourSet`, and this line is where that reader should land.
        damagePlayer(player, hurt, false, survival::kNoArmour);
    }
}

/// The clocks that owe nothing to the world: the damage window, the hurt flash,
/// every potion's duration and the death timer.
///
/// **Split out because it has two callers, and the second one is why it exists.**
/// `updatePlayer` refuses to move a body standing over a column that has not
/// finished loading, and returning there skipped the whole of `updateSurvival` -
/// so while you waited nothing expired: not the invulnerability window, not the
/// hurt flash, not a potion, and **not `deathSeconds`**, which is the one clock
/// the respawn screen is waiting on. Dying on a frame the ground had gone left a
/// run with no way back. `Creature.cpp`'s equivalent guard skips only the
/// movement and keeps its own timers running, which is the right shape; this is
/// the player catching up with it.
void tickPassiveTimers(Player& player, float dt) {
    // The window and `lastDamage` run down together, in `Survival.hpp`, for the
    // same reason the charge does: `Creature.cpp` had written out the same four
    // lines. A `lastDamage` that outlives its window swallows the next real hit.
    survival::tickDamageWindow(player.invulnerableSeconds, player.lastDamage, dt);

    player.hurtFlash = std::max(0.0f, player.hurtFlash - dt);

    // **Counted down here rather than beside the hazards, because every early
    // return below used to skip it.** Creative was the visible half: a potion
    // drunk there ran for ever and was still running after the switch back to
    // Survival.
    player.effects.tick(dt);

    // **The absorption pool, reconciled against the effect that grants it.**
    // Nothing else can. The grant happens at the eat site in `Main.cpp`, which
    // has no business reaching into a field on the player, and a pool merely
    // *read* from `absorptionPoints` would refill itself the instant it was
    // spent - the effect is still running at full level when the points are
    // gone, which is exactly what makes this a balance and not a level.
    //
    // Only `Effects::apply` can push the remaining seconds **up** and only the
    // tick above brings them down, so a rise is precisely "it was granted
    // again": a first apple, an upgrade from I to IV, or a second apple topping
    // the same level back up. Take that comparison out and a spent pool is
    // never refilled for as long as the effect runs.
    const float absorptionLeft = player.effects.secondsLeft(effects::Effect::Absorption);
    if (absorptionLeft > player.absorptionSeconds) {
        // `max`, because a grant may only ever raise the balance: an enchanted
        // apple over a nearly-spent Absorption I has to leave sixteen, and a
        // plain apple under a running IV must not cut sixteen down to four.
        // `Effects::apply` refuses the weaker grant outright so the seconds
        // never rise for it - this is what makes that belt as well as braces.
        player.absorption = std::max(player.absorption, effects::absorptionPoints(player.effects));
    }
    player.absorptionSeconds = absorptionLeft;

    // "Absorption health cannot be replenished by natural regeneration or other
    // effects and vanishes when the effect ends" (`minecraft.wiki/w/Absorption`)
    // - so the pool dies with the effect rather than being carried into the next
    // one. Drop this and two apples two minutes apart would stack.
    if (absorptionLeft <= 0.0f) {
        player.absorption = 0.0f;
    }

    if (!player.alive()) {
        player.deathSeconds += dt;
    }
}

/// Health, hunger and every hazard, once the frame's movement has settled.
///
/// Runs at the end of `updatePlayer` on purpose: fall damage is a function of
/// where the body ended up, and hunger is a function of how far it travelled.
void updateSurvival(Player& player, const World& world, float dt, const glm::vec3& from,
                    bool invulnerable, const survival::ArmourSet& armour, bool wearingLeather) {
    using namespace survival;

    tickPassiveTimers(player, dt);

    if (!player.alive()) {
        return;
    }

    // Creative pays none of this. Held here rather than at each call site so
    // there is one gate rather than a dozen, and so a mode change cannot leave
    // half the hazards armed.
    if (invulnerable) {
        player.fallDistance = 0.0f;
        player.burningSeconds = 0.0f;
        player.exhaustion = 0.0f;
        // Creative thaws rather than merely stopping - leaving these banked
        // would freeze a player the instant they switched back, from a clock
        // that ran while they were exempt.
        player.freezeSeconds = 0.0f;
        player.freezeTimer = 0.0f;
        return;
    }

    // **Out of the world.** Unconditional and unreducible, and it bypasses both
    // the invulnerability window - there is nothing to be invulnerable to - and
    // Resistance, which the reference exempts the void from by name. Without
    // that second flag a player under Resistance V fell forever at full health.
    if (player.position.y < kVoidDepth) {
        damageWithResistance(player, kMaxHealth * 2, true, false, kNoArmour);
        return;
    }

    // --- Falling. **Grounded, then damage, then accumulate** - the reference's
    // own order. What is deliberately *not* reproduced is its half-block
    // rounding: it tests the ground before adding the tick it is landing on, so
    // a 23-block drop needs 23.5 to kill. Ours used to lose the last **frame**
    // instead, which is the same artefact measured in a unit that changes with
    // the frame rate - 3.92 blocks of it at 20 fps against 0.54 at 144, so how
    // far you could fall and live depended on how busy the machine was. The
    // landing frame's own descent is banked below and §1.10's formula is
    // charged exactly: 23 blocks kills, at any frame rate.
    //
    // Water, a ladder and a vine all reset it outright, which is why a dive
    // from any height is free.
    //
    // **Powder snow is the fourth, and it is the whole reason people build with
    // it.** minecraft.wiki, *Powder Snow*: "Entities that fall into powder snow
    // do not take fall damage ... the fall distance is reset, similar to water."
    // Asked once, here, through the one helper - both this and the movement
    // branch in `updatePlayer` call `inPowderSnow`, so there is a single owner
    // of "am I in it". Two owners with two slightly different cell bounds is
    // finding 878, already paid for once with lava.
    const bool climbing = climbableAt(world, player) != BlockId::Air;
    const bool inSnow = inPowderSnow(world, player.position, player.height());
    if (player.onGround || player.inWater || player.flying || climbing || inSnow) {
        if (player.onGround && !player.inWater && !player.flying && !climbing && !inSnow) {
            // **The frame that ends in a landing still descended**, and by the
            // time this runs the resolver has snapped the feet to the surface
            // and zeroed `velocity.y` - so the velocity no longer knows about
            // it and nothing else banks it. Measured as the change in Y, which
            // is what §1.10 says fall damage is measured in. On any other
            // grounded frame this is zero, and walking downhill banks a
            // fraction of a block that is charged and cleared the same frame.
            player.fallDistance += std::max(0.0f, from.y - player.position.y);
            // Ordinary ground leaves the whole fall to be paid for; only the
            // surfaces that catch you scale it, and each of those does its own
            // scaling where it catches you.
            //
            // **Honey is the one that is charged here rather than at a bounce.**
            // It does not throw you back, so it comes to rest through this path
            // like stone does - and it scales the *damage*, not the distance,
            // which is why `chargeFall` takes both. Probed a tenth of a block
            // under the feet, the same depth the bounce block below uses to ask
            // what it landed on, because by now the resolver has snapped the
            // feet onto the surface.
            const BlockId landedOn =
                world.blockAt(static_cast<int>(std::floor(player.position.x)),
                              static_cast<int>(std::floor(player.position.y - 0.1f)),
                              static_cast<int>(std::floor(player.position.z)));
            chargeFall(player, 1.0f, isSticky(landedOn) ? kHoneyFallDamageScale : 1.0f);
        }
        player.fallDistance = 0.0f;
    } else if (player.velocity.y < 0.0f) {
        // **Slow Falling is two rules and this is the second one.** Gravity
        // drops (applied with the fall itself, in `updatePlayer`) *and the
        // distance already fallen is cleared every tick you are descending*, so
        // the potion is not a discount at the bottom - it is a fall that never
        // accumulates. Vetoing the damage at the landing instead reads the
        // effect at the one instant it may already have lapsed: a 60-block drop
        // whose potion runs out six blocks up banked all 60 and killed, where
        // the reference charges the six.
        //
        // Cleared *before* this frame's descent is added, which is the
        // reference's own order - the reset sits with gravity at the top of the
        // tick and the distance is accumulated after the move. So a landing
        // still under the potion is charged one frame of slowed descent - 78.4
        // m/s x `fallSpeedScale` x the 50 ms frame cap, 0.49 blocks at the
        // published 0.125 - against the three that are free, which is why it
        // still cancels fall damage outright without a second rule saying so.
        if (player.effects.level(effects::Effect::SlowFalling) > 0) {
            player.fallDistance = 0.0f;
        }
        player.fallDistance += -player.velocity.y * dt;
    }

    // --- Drowning. Spent on the shared cadence below, at two points a second.

    // --- Contact hazards. Sampled over the cells the body actually occupies,
    // so a corner clipping a cactus counts and standing beside one does not.
    const Aabb body = boxAt(player.position, player.height());
    bool inLava = false;
    bool inFire = false;
    bool inCampfire = false;
    bool onCactus = false;
    bool onWitherRose = false;
    for (int x = static_cast<int>(std::floor(body.min.x));
         x <= static_cast<int>(std::floor(body.max.x)); ++x) {
        for (int y = static_cast<int>(std::floor(body.min.y));
             y <= static_cast<int>(std::floor(body.max.y)); ++y) {
            for (int z = static_cast<int>(std::floor(body.min.z));
                 z <= static_cast<int>(std::floor(body.max.z)); ++z) {
                const BlockId block = world.blockAt(x, y, z);
                inLava = inLava || isLava(block);
                inFire = inFire || block == BlockId::Fire;
                // **Its own boolean, deliberately, and not a widening of
                // `inFire`.** That predicate has three readers - the
                // armour-reduced damage below, the ignition that sets
                // `burningSeconds`, and the `!inLava && !inFire` guard inside
                // the burn loop - and a campfire belongs to exactly one of
                // them. Widening `inFire` would silently change all three,
                // which is `CLAUDE.md` bug shape #2, the `isFurnace` case that
                // turned eight `case` labels into dead code. `Campfire.hpp`
                // asked for it in these terms and this is that boolean.
                //
                // **`isCampfire` rather than the two comparisons spelled out
                // again** - a second spelling of "which ids are campfires" is
                // the drift its own comment warns about. It lives in
                // `Campfire.hpp` today and that comment says it belongs in
                // `Block.hpp` beside `isFurnace`; if it moves, this call site
                // follows it and nothing else here changes.
                //
                // **Every campfire in this game is lit**, because no lit state
                // is modelled anywhere - `BlockId` has `Campfire` and
                // `SoulCampfire` and no unlit twin - and the reference lights
                // them by default on placement. If an unlit state is ever
                // added, this line is where it has to be asked about, or an
                // extinguished campfire will keep burning people.
                //
                // **This branch is reachable, and by crafting only.**
                // `Recipe.cpp` emits `itemForBlock(BlockId::Campfire)` as a
                // recipe output, and `itemForBlock` is a `static_cast` from
                // the block id, so an item form exists *without* any
                // `ItemId::Campfire` enumerator - searching for that token
                // returns zero and the zero means nothing. What no campfire
                // has is a **natural** source: village and structure
                // generation place none, so a player who never crafts one
                // never meets this code. Checked 2026-08-19; falsified by a
                // `BlockId::Campfire` appearing in any generation path.
                inCampfire = inCampfire || isCampfire(block);
                onCactus = onCactus || block == BlockId::Cactus;
                // A planted rose only. The potted one is a different block and
                // is harmless in the reference, which is why this names the
                // enumerator rather than asking `isFlower`.
                onWitherRose = onWitherRose || block == BlockId::WitherRose;
            }
        }
    }

    // **A magma block is stood *on*, not stood *in***, so it is the one contact
    // hazard the body scan above cannot answer - and it is the difference
    // between a trap you can walk over and one you can walk beside. "Touching
    // the sides of a magma block does not deal damage" and "sneaking on top of
    // a magma block prevents the damage"; it works underwater, and Fire
    // Resistance negates it (minecraft.wiki, *Magma Block*). Probed a tenth of a
    // block under the feet, the same depth every other "what am I standing on"
    // question in this file uses.
    const BlockId standingOn =
        world.blockAt(static_cast<int>(std::floor(player.position.x)),
                      static_cast<int>(std::floor(player.position.y - 0.1f)),
                      static_cast<int>(std::floor(player.position.z)));
    const bool onMagma =
        player.onGround && !player.sneaking && standingOn == BlockId::MagmaBlock;

    // **Lava eats the fall you are carrying, a half at a time.** "For each tick
    // an entity spends inside of lava, its fall distance is halved"
    // (minecraft.wiki, *Lava*; `RESEARCH.md` §1.10 lists it as "-50% of
    // accumulated distance per tick"). Per *tick*, so it is a drag factor and
    // `dragOver` is what converts one to this frame - writing it as a plain
    // `* 0.5` per frame would make how far you may dive through lava depend on
    // the frame rate, which is the unit mistake §1.10's own note is about.
    if (inLava) {
        player.fallDistance *= fluid::dragOver(fluid::kLavaFallHalving, dt);
    }

    // Suffocation is the eye specifically, not the body: a block at your feet
    // is something to stand on and a block in your face is not.
    const BlockId eyeBlock =
        world.blockAt(static_cast<int>(std::floor(player.position.x)),
                      static_cast<int>(std::floor(player.position.y + player.eyeOffset)),
                      static_cast<int>(std::floor(player.position.z)));
    // **Powder snow is named out, and it has to be named rather than derived.**
    // minecraft.wiki, *Powder Snow*: "Powder snow does not cause suffocation
    // damage." You are meant to be able to stand inside it up to the eyes,
    // freezing slowly, for as long as it takes to climb out - suffocation would
    // kill in a fifth of the time and make the freezing clock unreachable.
    //
    // **The exception became redundant at 2026-08-19 ~11:00 and is kept
    // deliberately.** It was written while powder snow still had a full
    // collision box, when it was the only thing standing between the mechanic
    // and a two-second death. `Block.hpp` now asserts
    // `collisionBoxes(BlockId::PowderSnow).count == 0` and
    // `!isSolid(BlockId::PowderSnow)`, so
    // the middle clause already rejects it and this one changes no outcome
    // today. **Do not delete it as dead weight** - it costs one comparison, and
    // it is the whole of what keeps suffocation right if that box is ever
    // restored; without it this file's correctness depends silently on another
    // file's block table, which is the rule-that-did-not-travel shape. It goes
    // back to being load-bearing the moment `isSolid(PowderSnow)` is true
    // again, and that grep is the falsifier.
    const bool suffocating =
        isOpaque(eyeBlock) && isSolid(eyeBlock) && eyeBlock != BlockId::PowderSnow;

    // --- Freezing. **The last missing damage source**, and the only hazard here
    // that has to remember how long you have been in it rather than just whether
    // you are.
    //
    // The clock is Bedrock's `TicksFrozen` in this file's own unit: it rises at
    // real time while the body is in powder snow, falls at
    // `kFreezeRecoveryRate` times real time once out, and is capped at
    // `kFreezeOnsetSeconds`. Damage starts at the cap, which is what makes
    // re-entering resume rather than restart - "the entity's TicksFrozen value
    // does not reset immediately ... instead it decreases at a rate of 2 per
    // tick" (minecraft.wiki, *Powder Snow*).
    //
    // **Any leather piece is full immunity *and* thaws you**, which is one rule
    // and not two: "Wearing any piece of leather armor stops the freezing
    // effect", and the counter drains while it does. `wearingLeather` is
    // `leatherArmour || leatherBoots` OR'd at the call site, so a caller that
    // fills only the narrower flag still gets the wider rule.
    //
    // **Armour does not reduce the damage** - `kNoArmour`, and it is not an
    // oversight. Bedrock's freezing is `hurt_on_condition`-shaped magic damage
    // and leather's protection is the on/off rule above, not a defence value;
    // routing it through `armour` would mean a full set of netherite made you
    // freeze more slowly, which is exactly backwards from a mechanic that
    // punishes not wearing the *weakest* armour in the game.
    if (inSnow && !wearingLeather) {
        player.freezeSeconds = std::min(kFreezeOnsetSeconds, player.freezeSeconds + dt);
    } else {
        player.freezeSeconds = std::max(0.0f, player.freezeSeconds - dt * kFreezeRecoveryRate);
    }
    if (player.freezeSeconds >= kFreezeOnsetSeconds) {
        // Its own cadence, because 40 ticks is not the 10 every contact hazard
        // shares - `Survival.hpp` asserts the two apart so folding this onto
        // `hazardTimer` fails the build rather than quadrupling the rate.
        player.freezeTimer += dt;
        while (player.freezeTimer >= kFreezeInterval) {
            player.freezeTimer -= kFreezeInterval;
            damagePlayer(player, kFreezeDamage, false, kNoArmour);
        }
    } else {
        // **Primed rather than zeroed, so the first point lands *at* seven
        // seconds and not at nine.** minecraft.wiki, *Powder Snow*: "After seven
        // seconds (140 game ticks) ... the player begins taking damage at a rate
        // of 1HP every two seconds" - the seven seconds is when damage begins,
        // not when a fresh two-second wait begins. Starting this at zero would
        // silently add a whole cadence to the onset and make the published 140
        // read as 180. Priming it also means stepping out and back in cannot
        // land a free hit, which is what a plain reset was for.
        player.freezeTimer = kFreezeInterval;
    }

    // One shared cadence, because every one of these is a half second and
    // running four separate timers would let them interleave into a much
    // faster stream than any of them is supposed to be.
    player.hazardTimer += dt;
    while (player.hazardTimer >= kHazardInterval) {
        player.hazardTimer -= kHazardInterval;

        // **How late this tick is**, and it has to come off the window the tick
        // arms for itself. The cadence and `kInvulnerableSeconds` are both half
        // a second, so a window dated from the end of the frame that *noticed*
        // the tick outlives the next tick by exactly this residue - and an
        // equal blow inside a window is refused outright. Two accumulators
        // free-running against each other then swallowed about a third of every
        // hazard's ticks, which third depending on the frame rate: lava came
        // out at roughly 5.3 health a second against the 8 it is meant to be.
        // Dating the window from the tick's own instant makes expiry and the
        // next tick coincide exactly at any frame rate, which is what the
        // reference gets for free by counting both in whole ticks.
        //
        // **This is a rule about *when* a window starts, not about the overwrite
        // rule itself**, which is why it sits here rather than inside
        // `survival::chargeDamageWindow` - that function owns the comparison and
        // the arming, and this only back-dates what it armed. Anything else that
        // puts a periodic hazard through the window at the window's own cadence
        // needs the same correction or it will lose ticks the same way.
        const float lateBy = player.hazardTimer;
        const float windowBefore = player.invulnerableSeconds;

        // Fire resistance covers every heat source there is, which is the whole
        // of what the reference gives it - no scaling, no partial protection.
        const bool fireproof = player.effects.level(effects::Effect::FireResistance) > 0;
        // **The four hazards armour reduces, and the only four.** Bedrock's own
        // cause enum has `lava`, `fire` and `magma` as separate causes and a
        // cactus as `contact`; the wiki's *Armor* "Damage sources" list has all
        // four under "reduced". Everything else in this function passes
        // `kNoArmour` at its own call site rather than being filtered here, so
        // that a reader sees each source's verdict beside the source.
        if (inLava && !fireproof) {
            damagePlayer(player, kLavaDamage, false, armour);
        } else if (inFire && !fireproof) {
            damagePlayer(player, kFireDamage, false, armour);
        } else if (onMagma && !fireproof) {
            // **Chained onto fire's `else`, not charged beside it.** All three
            // are fire damage in the reference and the damage window takes the
            // largest blow inside it anyway, so a separate charge would be a
            // second call that could only ever be swallowed - and reading as
            // though standing in lava on a magma block cost five.
            damagePlayer(player, kMagmaDamage, false, armour);
        } else if (inCampfire && !fireproof) {
            // **`kNoArmour`, and it is the only fire source here that gets
            // it.** minecraft.wiki, *Campfire*: "Damage taken is considered
            // fire damage, so armor itself does not reduce damage caused by
            // campfire; to do so, the player needs the Resistance potion
            // effect, or the Protection or Fire Protection enchantments."
            // Enchantments do not exist here yet, so Fire Resistance - the
            // `fireproof` test this branch already sits behind - is the whole
            // of the counterplay, which is exactly what the reference says.
            // The comment above still reads "the four hazards armour reduces,
            // and the only four" and is still true: this is a fifth fire
            // hazard and it is not one of them.
            //
            // **Chained onto the same `else` for the reason magma is**, not
            // charged beside it: the damage window keeps the largest blow
            // inside it, so a second call while already standing in lava could
            // only ever be swallowed.
            damagePlayer(player, kCampfireDamage, false, kNoArmour);
        }
        if (onCactus) {
            damagePlayer(player, kCactusDamage, false, armour);
        }
        // Not fire, so `fireproof` says nothing about it. Wither Resistance is
        // not a thing the reference has either: only the three named immune
        // mobs escape it, and the player is not one of them.
        // **Not armour-reduced, and the wither rose is the one that looks like
        // it should be.** It is contact damage to the eye, but Bedrock routes it
        // through the `wither` cause - a status effect, and so magic, which
        // "bypasses armor" outright. Suffocation and drowning are on the wiki's
        // not-reduced list by name.
        if (onWitherRose) {
            damagePlayer(player, kWitherRoseDamage, false, kNoArmour);
        }
        if (suffocating) {
            damagePlayer(player, kSuffocationDamage, false, kNoArmour);
        }
        if (player.air <= 0.0f && player.underwater &&
            player.drowningSeconds > fluid::kPlayerSuffocateGrace) {
            // **One second of grace past empty**, which is the player's own
            // `suffocate_time: -1` in the reference where a mob's is 0. The
            // constant is exported and documented in `Fluid.hpp` and was read
            // by nobody, so the player has been taking the first hit a whole
            // second early. `drowningSeconds` is the clock `updateBreath`
            // starts the moment the meter empties and clears the moment the
            // head is out, so this is seconds under water past empty and not
            // seconds under water.
            damagePlayer(player, kDrownDamagePerHazardTick, false, kNoArmour);
        }

        if (player.invulnerableSeconds > windowBefore) {
            player.invulnerableSeconds = std::max(0.0f, player.invulnerableSeconds - lateBy);
        }
    }

    // --- What the potions are doing. Their durations were counted down in
    // `tickPassiveTimers`, before any of the above, so an effect that runs out
    // this frame does not get one last tick out of it. Regeneration, poison and
    // wither each keep their own clock because each one's interval is set by
    // how strong it is.

    if (const float interval = effects::regenerationInterval(player.effects); interval > 0.0f) {
        player.effectHealTimer += dt;
        while (player.effectHealTimer >= interval) {
            player.effectHealTimer -= interval;
            healPlayer(player, 1);
        }
    } else {
        player.effectHealTimer = 0.0f;
    }

    const float poison = effects::poisonInterval(player.effects);
    const float wither = effects::witherInterval(player.effects);
    if (poison > 0.0f || wither > 0.0f) {
        // Whichever is running faster sets the pace; both together is the
        // reference's own behaviour of two independent counters, and the
        // difference is a fraction of a heart nobody could see.
        const float interval = poison > 0.0f && (wither <= 0.0f || poison < wither) ? poison : wither;
        player.effectHurtTimer += dt;
        while (player.effectHurtTimer >= interval) {
            player.effectHurtTimer -= interval;
            // **Poison cannot take the last point of health; wither can.** That
            // is the whole difference between them and it is why this is not one
            // branch.
            // Poison and wither are magic, which bypasses armour entirely.
            if (wither > 0.0f) {
                damagePlayer(player, 1, true, kNoArmour);
            } else if (player.health > 1) {
                damagePlayer(player, 1, true, kNoArmour);
            }
        }
    } else {
        player.effectHurtTimer = 0.0f;
    }

    // Hunger empties the bar by adding exhaustion rather than by touching it,
    // which is what lets saturation absorb the first of it.
    player.exhaustion += effects::hungerExhaustion(player.effects) * dt;

    // --- Burning, which outlives the flame that started it. Water puts it out.
    if (inLava) {
        player.burningSeconds = kLavaBurnSeconds;
    } else if (inFire) {
        player.burningSeconds = std::max(player.burningSeconds, kFireBurnSeconds);
    }
    if (player.inWater || player.effects.level(effects::Effect::FireResistance) > 0) {
        player.burningSeconds = 0.0f;
    }
    if (player.burningSeconds > 0.0f) {
        player.burningSeconds = std::max(0.0f, player.burningSeconds - dt);
        player.burnTimer += dt;
        while (player.burnTimer >= kBurnInterval) {
            player.burnTimer -= kBurnInterval;
            // Standing *in* the fire already charges the higher rate above.
            // **`kNoArmour`, and this is the split that catches people.**
            // Standing *in* fire is reduced by armour; still burning afterwards
            // is not - four separate wiki statements agree, and Bedrock's own
            // cause enum carries `fire` and `fire_tick` as distinct causes. The
            // two are `kFireDamage` above and `kBurnDamage` here, which is what
            // `Survival.hpp`'s `kFireInterval != kBurnInterval` assert protects.
            if (!inLava && !inFire) {
                damagePlayer(player, kBurnDamage, false, kNoArmour);
            }
        }
    } else {
        player.burnTimer = 0.0f;
    }

    // --- Hunger. Walking is free; only hurrying and healing cost anything,
    // which is the whole design of the system rather than an accident of the
    // numbers.
    const float travelled = glm::distance(glm::vec2{player.position.x, player.position.z},
                                          glm::vec2{from.x, from.z});
    if (player.inWater) {
        player.exhaustion += travelled * kExhaustSwimPerMetre;
    } else if (player.onGround && (player.velocity.x != 0.0f || player.velocity.z != 0.0f)) {
        // **Only what is paid for on the ground.** The reference bills sprinting
        // by the metre from the walking half of its movement statistics, which
        // an airborne player never reaches, and bills the arc separately - §3.2
        // lists "sprint-jumping 0.2" as a row of its own precisely because the
        // flight is not also charged by the metre. Charging both put a
        // sprint-jump cycle at about 0.6 exhaustion against the reference's
        // 0.25, so the bar emptied roughly two and a half times too fast for
        // the one thing players do most: travelling.
        //
        // **Against the speed the mover actually used, not the bare constant.**
        // `updatePlayer` scales every walk and sprint by `moveSpeedScale`, so
        // under Speed an ordinary walk clears `kWalkSpeed` and was charged as a
        // sprint, and under Slowness II a real sprint falls under it and was
        // charged as free. The 0.1 is the same margin as before, now above the
        // modified walk.
        const float walkSpeed = kWalkSpeed * effects::moveSpeedScale(player.effects);
        const bool sprinting =
            glm::length(glm::vec2{player.velocity.x, player.velocity.z}) > walkSpeed + 0.1f;
        player.exhaustion +=
            travelled * (sprinting ? kExhaustSprintPerMetre : kExhaustWalkPerMetre);
    }

    while (player.exhaustion >= kExhaustionPerLevel) {
        player.exhaustion -= kExhaustionPerLevel;
        if (player.saturation > 0.0f) {
            player.saturation = std::max(0.0f, player.saturation - 1.0f);
        } else {
            player.food = std::max(0, player.food - 1);
        }
    }
    player.saturation = clampSaturation(player.saturation, player.food);

    // --- Healing and starving, which are the two ends of the same bar.
    //
    // **The gate is food alone, and the wiki's own phrasing invites getting
    // this wrong.** minecraft.wiki, *Food*: "If the hunger value is at 18 or
    // above, **or the saturation value is non-zero**, the player's health
    // naturally regenerates every 4 seconds (80 ticks)." Read literally that
    // second clause would heal a starving player who happened to carry
    // saturation - but the same paragraph closes "When the hunger value drops
    // to 17 or below, natural regeneration stops", which is unambiguous and
    // contradicts it. The clause is describing what *pays* for the heal, not
    // what permits it, and `player.food >= kRegenFoodFloor` is the correct
    // reading. Do not add `|| player.saturation > 0.0f` here.
    if (player.food >= kRegenFoodFloor && player.health < kMaxHealth) {
        // **Two rates, and which one is live is one named constant.** Bedrock
        // has exactly one: a point every `kRegenInterval`, paid for in
        // exhaustion. Java adds a second, eight times faster, at a full bar
        // with saturation behind it - "saturation boost" - and that is what
        // `survival::kJavaSaturationBoost` selects. It is `false`, because
        // Bedrock is this project's reference; the wiki is explicit that
        // "saturation boost is only in *Java Edition*"
        // (`minecraft.wiki/w/Food`).
        //
        // It is `if constexpr` rather than a deleted branch on purpose. The
        // discarded arm of an `if constexpr` in a non-template function still
        // has to be well-formed, so the Java path is compiled and type-checked
        // at every build and cannot quietly rot - which is the failure mode a
        // commented-out or deleted alternative always has. Flipping the
        // constant is the whole edit. See its definition for why it is not a
        // setting.
        //
        // **Saturation matters either way, and in the reference's own way** -
        // it is spent before food by the exhaustion loop above, so a well-fed
        // player keeps their bar full for longer. What Bedrock does not give it
        // is a second, faster heal.
        bool saturated = false;
        if constexpr (kJavaSaturationBoost) {
            saturated = player.food >= kMaxFood && player.saturation > 0.0f;
        }
        player.regenTimer += dt;
        if (saturated) {
            if (player.regenTimer >= kSaturatedRegenInterval) {
                player.regenTimer = 0.0f;
                healPlayer(player, kRegenAmount);
                player.saturation = std::max(0.0f, player.saturation - kSaturatedRegenCost);
            }
        } else if (player.regenTimer >= kRegenInterval) {
            player.regenTimer = 0.0f;
            healPlayer(player, kRegenAmount);
            // **Six exhaustion a point, which is a point and a half of food.**
            // minecraft.wiki, *Healing*: "This increases the player's
            // exhaustion by 6, of which 4 cost 1 hunger." Healing ten hearts
            // eats fifteen food, so a fight is expensive twice over.
            player.exhaustion += kExhaustPerHealed;
        }
    } else {
        player.regenTimer = 0.0f;
    }

    if (player.food <= 0) {
        player.starveTimer += dt;
        if (player.starveTimer >= kStarveInterval) {
            player.starveTimer = 0.0f;
            // Normal difficulty stops at one point rather than killing, which
            // is the reference's own floor.
            //
            // **Unresistable, by name.** Starvation "ignores armor and armor
            // toughness, the Protection enchantment, and the Resistance effect"
            // (`minecraft.wiki/w/Food`), and it is a single point - so before
            // this flag existed, Resistance III rounded it away and Turtle
            // Master was an answer to having no food.
            if (player.health > kStarveFloor) {
                damageWithResistance(player, kStarveDamage, true, false, kNoArmour);
            }
        }
    } else {
        player.starveTimer = 0.0f;
    }
}

/// The face that stopped motion along `axis`, or infinity if nothing did.
///
/// Resolution has to read the same shape table the overlap test does. Snapping
/// to the cell boundary instead is only correct for full cubes: a stair's step
/// starts halfway across its cell, so walking into one from the high side threw
/// the player a whole block backwards. Same failure as landing on a slab, one
/// axis over.
///
/// `from` is where the body was **before** the move, and it is what makes this
/// safe for a thin box. A door leaf is three texels thick, so the player can
/// legitimately stand on either side of it inside the same cell - and taking
/// the nearest face without asking which side they came from picked the one
/// behind them and threw them backwards through it. That is the teleport a
/// doorway used to do, and it can only ever happen to a box the player is able
/// to be past.
float blockingPlaneAlong(const World& world, const Aabb& box, int axis, bool positive,
                         const Aabb& from) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kSkin));

    float best = positive ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity();

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockBoxes shape = worldCollisionBoxes(world, x, y, z);
                const glm::vec3 cell{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};

                for (int i = 0; i < shape.count; ++i) {
                    const BlockBox& b = shape.boxes[i];
                    const glm::vec3 lo = cell + glm::vec3{b.minX, b.minY, b.minZ};
                    const glm::vec3 hi = cell + glm::vec3{b.maxX, b.maxY, b.maxZ};

                    // Only boxes overlapping on the other two axes can block
                    // this one; the rest are beside the player, not in the way.
                    bool blocks = true;
                    for (int other = 0; other < 3 && blocks; ++other) {
                        if (other == axis) {
                            continue;
                        }
                        blocks = box.min[other] < hi[other] && box.max[other] > lo[other];
                    }
                    if (!blocks) {
                        continue;
                    }

                    if (positive) {
                        if (lo[axis] + kSkin < from.max[axis]) {
                            continue;
                        }
                        best = std::min(best, lo[axis]);
                    } else {
                        if (hi[axis] - kSkin > from.min[axis]) {
                            continue;
                        }
                        best = std::max(best, hi[axis]);
                    }
                }
            }
        }
    }
    return best;
}

/// Moves along one axis and snaps to the blocking surface if something is hit.
///
/// Axes are resolved one at a time on purpose. Resolving all three together
/// leaves the maths unable to tell which direction to push out of a corner,
/// which shows up as jitter or as sliding through walls diagonally.
bool moveAxis(glm::vec3& position, const World& world, int axis, float amount, float height) {
    if (amount == 0.0f) {
        return false;
    }

    glm::vec3 candidate = position;
    candidate[axis] += amount;

    if (!overlapsSolid(world, boxAt(candidate, height))) {
        position = candidate;
        return false;
    }

    // Landing is the one case where the blocking plane is not a block boundary:
    // a slab's top is halfway up its cell. Its sides and underside still are.
    if (axis == 1 && amount < 0.0f) {
        const float surface = highestSurfaceBelow(world, boxAt(candidate, height), position.y);
        if (std::isfinite(surface)) {
            candidate.y = surface + kSkin;
            position = candidate;
            return true;
        }
    }

    // How far the box extends past `position` on this axis, in each direction.
    constexpr float half = kWidth * 0.5f;
    const float extentAbove = (axis == 1) ? height : half;
    const float extentBelow = (axis == 1) ? 0.0f : half;

    const float plane =
        blockingPlaneAlong(world, boxAt(candidate, height), axis, amount > 0.0f,
                           boxAt(position, height));
    if (!std::isfinite(plane)) {
        // Overlapped but nothing squarely in the way, which the skin makes
        // possible at a corner. Refusing the move is the safe answer.
        return true;
    }

    candidate[axis] = amount > 0.0f ? plane - extentAbove - kSkin : plane + extentBelow + kSkin;

    position = candidate;
    return true;
}

/// How a surface answers the controls, derived from its slipperiness alone.
///
/// The reference has no separate "ice rules". It multiplies horizontal velocity
/// by `0.91 * S` every tick and adds `0.1 * (0.6/S)^3` of acceleration, and all
/// three things a player notices on ice fall out of those two expressions: slow
/// to get going, a long glide when you let go, and a **higher** top speed than
/// dry land, because the drag falls away faster than the acceleration does.
///
/// Ours eases toward a target at a fixed rate instead of decaying, so each of
/// those becomes a scale on what dry land already does. At `S == 0.6` all three
/// are exactly 1, which is why ordinary ground is untouched by any of this.
struct SurfaceMotion {
    float speedScale;
    float accelScale;
    float decelScale;
};

SurfaceMotion surfaceMotion(float slipperiness) {    const float keep = 0.91f * slipperiness;
    const float base = 0.91f * kDefaultSlipperiness;
    const float ratio = kDefaultSlipperiness / slipperiness;

    SurfaceMotion motion{};
    motion.accelScale = ratio * ratio * ratio;
    // Steady state of `v = (v + a) * keep`, against dry land's own steady state.
    motion.speedScale = (motion.accelScale * keep / (1.0f - keep)) / (base / (1.0f - base));
    // A decay has a time constant and our model has a rate; the rate that eases
    // out over the same time is the honest translation between them.
    motion.decelScale = std::log(keep) / std::log(base);
    return motion;
}

} // namespace

void updatePlayer(Player& player, const PlayerInput& input, const World& world, float deltaSeconds) {
    const float dt = std::min(deltaSeconds, kMaxDeltaSeconds);

    // Ground that has not arrived yet reads as air, so stepping over it drops
    // the player out of the world and the chunk buries them wherever they got
    // to. Creatures have asked this since the day they were written and the
    // player never did; respawning at a world spawn you have since walked away
    // from is the way in.
    if (!world.columnResident(static_cast<int>(std::floor(player.position.x)),
                              static_cast<int>(std::floor(player.position.z)))) {
        player.velocity = glm::vec3{0.0f};
        // **Frozen in place is not frozen in time.** Everything that does not
        // read the world keeps counting down - see `tickPassiveTimers`, which
        // exists for this one line.
        tickPassiveTimers(player, dt);
        return;
    }

    // And a body that is already inside terrain climbs out instead of living
    // there, because every move it tries overlaps something and it is stuck for
    // good. The other half of the same bug: the chunk arrives around whoever
    // fell through it. Creatures have had this since they were written.
    if (overlapsSolid(world, boxAt(player.position, player.height()))) {
        // **The reach has to clear the body's own height.** A player embedded in
        // stone has a block holding its head, so a lift shorter than the body
        // cannot get the head out of it, and the pass that exists to free it
        // hands back the position it was given. Make the player taller than
        // `collision::kUnstickReach`, or shorten the reach below `kHeight`, and
        // this fires - which is the guarantee the reach is there to give, and
        // the one thing about it a caller can check at compile time.
        static_assert(collision::kUnstickReach >= kHeight,
                      "the unstick pass must be able to lift a standing player clear of itself");
        for (float lift = collision::kUnstickStep; lift <= collision::kUnstickReach;
             lift += collision::kUnstickStep) {
            const glm::vec3 freed = player.position + glm::vec3{0.0f, lift, 0.0f};
            if (!overlapsSolid(world, boxAt(freed, player.height()))) {
                player.position = freed;
                break;
            }
        }
        player.velocity = glm::vec3{0.0f};
        player.onGround = false;
    }

    const glm::vec3 startedAt = player.position;

    if (input.sneak) {
        player.sneaking = true;
    } else if (player.sneaking && !overlapsSolid(world, boxAt(player.position, kHeight))) {
        // Standing up inside a low gap would push the head into a block.
        player.sneaking = false;
    }

    const float height = player.height();
    const fluid::FluidContact water = fluid::sampleFluid(world, boxAt(player.position, height));
    player.inWater = water.inFluid;
    // **Lava is a fluid you move through, and until this sample existed it was
    // not one.** `updateSurvival` looked for it only to charge damage, so the
    // movement chain below fell straight past water's branch into the walking
    // one: a body in lava kept air's terminal velocity, full walking control
    // and a full jump. minecraft.wiki, *Lava*: "An entity moving in lava has its
    // horizontal movement speed reduced by 50% and its vertical movement speed
    // reduced by 20%. A player cannot sprint-swim in lava."
    const fluid::FluidContact lava =
        fluid::sampleFluid(world, boxAt(player.position, height), fluid::FluidKind::Lava);
    player.sinceWater = player.inWater ? 0.0f : player.sinceWater + dt;
    // **Waterlogging counts, and leaving it out was a real hole.** `World`'s own
    // `waterloggedAt` says so at its declaration - "Anything asking 'am I in
    // water?' has to ask this too, or a swimmer drowns in a patch of seagrass" -
    // and `fluid::sampleFluid`, which sets `inWater` a few lines above, already
    // obeys it. This test did not, so the two disagreed exactly where a
    // waterlogged cell was involved: `inWater` true and `underwater` false meant
    // you swam through kelp, seagrass or a waterlogged stair with your head
    // under the surface while your air quietly refilled and drowning never
    // started. The rule existed, was correct, and was commented - in one of the
    // two places that needed it.
    const glm::ivec3 eyeCell{static_cast<int>(std::floor(player.position.x)),
                             static_cast<int>(std::floor(player.position.y + player.eyeOffset)),
                             static_cast<int>(std::floor(player.position.z))};
    player.underwater = isWater(world.blockAt(eyeCell.x, eyeCell.y, eyeCell.z)) ||
                        world.waterloggedAt(eyeCell.x, eyeCell.y, eyeCell.z);

    // **Sprinting needs more than six food.** The reference's own gate, and it
    // is what turns an empty bar from a slow trickle of damage into something
    // that changes how you move.
    const bool sprinting =
        input.sprint && (player.flying || player.food > survival::kSprintFoodFloor);

    // Starting a sprint-swim needs the head under; keeping one only needs to be
    // in water at all, which is what lets you sprint along the surface without
    // dropping out of it every time the head breaks through.
    player.swimming =
        sprinting && !player.flying && player.inWater && (player.underwater || player.swimming);

    updateBreath(player, dt);

    const float targetEye = player.sneaking ? kSneakEyeHeight : kEyeHeight;
    const float maxEyeChange = kEyeAdjustSpeed * dt;
    player.eyeOffset += std::clamp(targetEye - player.eyeOffset, -maxEyeChange, maxEyeChange);

    const float flySpeed = sprinting ? kFlySprintSpeed : kFlySpeed;
    // Honey drags. **Not slipperiness**, which is the obvious lever and the
    // wrong one: the surface model raises acceleration as slipperiness falls,
    // so a low value comes out *faster* than dry land. The reference scales
    // movement directly here too.
    const BlockId underfoot =
        world.blockAt(static_cast<int>(std::floor(player.position.x)),
                      static_cast<int>(std::floor(player.position.y - 0.5f)),
                      static_cast<int>(std::floor(player.position.z)));
    const float stickScale =
        (player.onGround && !player.flying && isSticky(underfoot)) ? kStickySpeedScale : 1.0f;
    const float speed = (player.flying   ? flySpeed
                         : player.sneaking ? kSneakSpeed
                         : sprinting       ? kSprintSpeed
                                           : kWalkSpeed) *
                        stickScale * effects::moveSpeedScale(player.effects) *
                        std::clamp(input.moveScale, 0.0f, 1.0f);

    // **Jump Boost multiplies the impulse, and the height goes as its square.**
    // `jumpScale` is 1.21 at level I, so the apex is 1.21² = 1.464 times the
    // plain 1.25 blocks, which is the reference's own 1.83 - that ratio is where
    // the 1.21 came from in the first place. It returns exactly 1 with no potion
    // running, so this is the bare constant then.
    //
    // **Honey scales the same impulse, and by the square root of its height
    // ratio for the same reason.** The reference cuts the jump to 3/16 of a
    // block, an 85% reduction, and the walk slowdown beside it was already
    // here - so a honey pit you hopped straight out of was half a mechanic.
    // Read off `isSticky` and `player.onGround`, exactly like `stickScale`
    // above, so the two halves cannot disagree about what honey is.
    const float honeyJumpScale =
        stickScale < 1.0f ? std::sqrt(kHoneyJumpHeight / kPlainJumpHeight) : 1.0f;
    const float jumpVelocity =
        kJumpVelocity * effects::jumpScale(player.effects) * honeyJumpScale;

    glm::vec3 wish = input.moveDirection;
    wish.y = 0.0f;
    if (glm::dot(wish, wish) > 0.0f) {
        wish = glm::normalize(wish);
    }

    if (player.flying) {
        // Eased as a single vector rather than per axis, so changing direction
        // curves through the turn instead of stopping one axis and starting
        // another.
        const glm::vec3 target{wish.x * speed, input.verticalWish * flySpeed, wish.z * speed};
        const glm::vec3 difference = target - player.velocity;
        const float distance = glm::length(difference);
        const float rate = glm::dot(target, target) > 0.0f ? kFlyAcceleration : kFlyDeceleration;
        const float maxStep = rate * dt;

        player.velocity = distance <= maxStep ? target : player.velocity + difference * (maxStep / distance);
        player.onGround = false;
    } else if (player.inWater) {
        // Water is not "gravity, but weaker". The reference multiplies velocity
        // by a drag factor every tick and adds a fixed impulse, so sinking,
        // swimming, being carried by a current and the long plunge after a dive
        // are all the same expression settling toward a different speed.
        using namespace fluid;

        const float drag = player.swimming ? kSprintSwimDrag : kWaterDrag;
        const float swimSpeed = player.swimming ? kSprintSwimSpeed : kSwimSpeed;

        // **Sprint-swimming goes where you look**, and that is most of what
        // makes it worth doing: flattened to the horizontal it can only skim,
        // so a dive or a climb is no quicker than paddling. Ordinary swimming
        // stays flat, or glancing down while treading water would pull you
        // under. Splitting the speed between the two axes by the look pitch
        // keeps the total the same whichever way you point.
        const bool steering = glm::dot(wish, wish) > 0.0f;
        const float lookY = player.swimming && steering ? input.lookY : 0.0f;
        const float flat = std::sqrt(std::max(0.0f, 1.0f - lookY * lookY));

        // A flowing cell's push is one more impulse under the same drag, so it
        // simply adds to wherever the input was already heading. Swimming
        // upstream works - it is only slower.
        const glm::vec3 target = wish * (swimSpeed * flat) + water.flow * kCurrentSpeed;
        const glm::vec3 next = approach(glm::vec3{player.velocity.x, 0.0f, player.velocity.z},
                                        glm::vec3{target.x, 0.0f, target.z}, drag, dt);
        player.velocity.x = next.x;
        player.velocity.z = next.z;

        // Ankle-deep water jumps like dry land, which is the reference's own
        // rule and stops a puddle starting a swim. Anything deeper falls
        // through to the climb below.
        const bool standing = player.onGround && water.depth < kShallowDepth;
        if (input.jump && standing) {
            player.velocity.y = jumpVelocity;
            player.onGround = false;
        } else {
            // Holding jump does one of two quite different things, and running
            // them at the same speed is what threw the player clear of the
            // water: while the eyes are still under it is a **climb**, at the
            // full swim-up speed; once they are out it is a **stroke**, which
            // only has to beat the sink.
            //
            // The stroke is latched across `kStroke` rather than faded out,
            // because a drive that tapers to nothing as the head clears is
            // first-order - it settles dead and leaves you floating motionless.
            // Kicking below the low mark and drifting above the high one
            // cannot settle. Measured: a 0.16 m bob, eyes never less than 0.07
            // clear of the water, at most a quarter of the body out of it.
            const float headClear = player.eyeOffset - water.depth;
            if (headClear < kFloatEye - kStroke) {
                player.treading = true;
            } else if (headClear > kFloatEye + kStroke) {
                player.treading = false;
            }

            const float rise = player.swimming ? kSprintSwimUpSpeed : kSwimUpSpeed;
            const float dive = player.swimming ? kSprintSwimDownSpeed : kSwimDownSpeed;
            // Sprint-swimming skips water gravity outright, which is why it
            // holds depth with no input at all. That is not buoyancy - nothing
            // pushes a swimmer up; gravity is simply absent.
            const float idle = player.swimming ? 0.0f : -kSinkSpeed;

            // **The drift between strokes is the ordinary sink even while
            // sprint-swimming.** Skipping water gravity is a rule about holding
            // depth with your head under; at the surface it means nothing
            // brings you back down, so a sprint-swimmer hung motionless with
            // half a metre of air under their eyes.
            //
            // Pushing off the bottom is a **climb** at the water's own speed
            // rather than a leap: a single block of water used to launch you off
            // the floor like solid ground. At the climb speed a shallow pool
            // comes out indistinguishable from deep water - same bob, same
            // height - which is the whole point of it.
            const float held = (headClear <= 0.0f || player.onGround) ? rise
                               : player.treading                      ? kTreadSpeed
                                                                      : -kSinkSpeed;
            const float verticalTarget = input.jump  ? held
                                         : input.sneak ? -dive
                                         : lookY != 0.0f ? lookY * swimSpeed
                                                         : idle;
            // **The downward half of a current, which the model had no axis
            // for.** `flowVector` now carries a y, so a waterfall pulls you
            // down it and a cell over a hole pulls you toward the hole - "A
            // downward current in a water block is caused by the block below
            // it... Falling water blocks have a downward current by default"
            // (minecraft.wiki, *Water*). Added to the target rather than to the
            // velocity, exactly as the horizontal push is above, so the same
            // drag governs both and neither can outrun the water.
            player.velocity.y = approach(player.velocity.y,
                                         verticalTarget + water.flow.y * kCurrentSpeed, drag, dt);
            player.onGround = false;
        }
    } else if (lava.inFluid) {
        // **Lava, built from the same three parts water is**: a drag factor, a
        // settling speed, and a current. Every constant is `fluid::`'s, derived
        // there from the published drag/gravity pair rather than written down
        // beside it, so this branch names no number of its own.
        //
        // It sits *after* water on purpose. Nothing can be in both - a cell
        // holds one fluid - but if the two ever overlapped, water is the one
        // you can swim out of.
        using namespace fluid;

        // Sprinting is refused outright rather than scaled: "A player cannot
        // sprint-swim in lava." So the target is the reference's measured walk
        // and nothing else, plus whatever the flow is carrying - "Flowing lava
        // can push entities, including those that do not take lava damage."
        const glm::vec3 target = wish * kLavaWalkSpeed + lava.flow * kLavaCurrentSpeed;
        const glm::vec3 next = approach(glm::vec3{player.velocity.x, 0.0f, player.velocity.z},
                                        glm::vec3{target.x, 0.0f, target.z}, kLavaDrag, dt);
        player.velocity.x = next.x;
        player.velocity.z = next.z;

        // Ankle-deep lava is jumped out of like dry land, which is water's rule
        // and the only thing that makes a one-block spill survivable.
        const bool standing = player.onGround && lava.depth < kShallowDepth;
        if (input.jump && standing) {
            player.velocity.y = jumpVelocity;
        } else {
            // Holding jump comes out at `kLavaRiseSpeed`, which is **exactly
            // zero** and is the answer rather than a gap - see its definition.
            // You hold level in lava; you do not climb out of it.
            const float verticalTarget = input.jump    ? kLavaRiseSpeed
                                         : input.sneak ? -kLavaDiveSpeed
                                                       : -kLavaSinkSpeed;
            player.velocity.y = approach(player.velocity.y,
                                         verticalTarget + lava.flow.y * kLavaCurrentSpeed,
                                         kLavaDrag, dt);
        }
        player.onGround = false;
    } else {
        // Eased as one horizontal vector, for the same reason flight is: per-axis
        // easing stops one axis dead while starting another, which turns a
        // direction change into a stutter instead of a curve.
        //
        // A bob at the water's surface lifts the whole box clear for a fraction
        // of a second, and this branch would spend it accelerating toward the
        // *walking* speed - so swimming forward came out as a lurch on every
        // bounce. A swimmer keeps a swimmer's speed until it has been out long
        // enough to have actually left.
        const float horizontal =
            !player.onGround && player.sinceWater < fluid::kSwimGrace
                ? (player.swimming ? fluid::kSprintSwimSpeed : fluid::kSwimSpeed)
                : speed;

        // Slipperiness comes from half a metre under the feet, so a slab or a
        // snow layer over ice is as slippery as the ice. Airborne, the
        // reference applies no ground drag at all.
        const SurfaceMotion surface = surfaceMotion(
            player.onGround
                ? slipperiness(world.blockAt(static_cast<int>(std::floor(player.position.x)),
                                             static_cast<int>(std::floor(player.position.y - 0.5f)),
                                             static_cast<int>(std::floor(player.position.z))))
                : kDefaultSlipperiness);

        const glm::vec2 current{player.velocity.x, player.velocity.z};

        // **Never brake toward the walking speed while off the ground.** Nothing
        // slows a jump horizontally in the reference, and speed carried off ice
        // is the whole reason sprint-jumping along it is fast; pulling down to
        // the walk target here would stop you dead the instant you left the
        // surface, which on ice is every jump.
        const float top = horizontal * surface.speedScale;
        const float targetSpeed = player.onGround ? top : std::max(top, glm::length(current));

        const glm::vec2 target{wish.x * targetSpeed, wish.z * targetSpeed};
        const glm::vec2 difference = target - current;
        const float distance = glm::length(difference);

        const bool wantsToMove = glm::dot(target, target) > 0.0f;
        const float rate = player.onGround
                               ? (wantsToMove ? kGroundAcceleration * surface.accelScale
                                              : kGroundDeceleration * surface.decelScale)
                               : (wantsToMove ? kAirAcceleration : kAirDeceleration);
        const float maxStep = rate * dt;

        const glm::vec2 next =
            distance <= maxStep ? target : current + difference * (maxStep / distance);
        player.velocity.x = next.x;
        player.velocity.z = next.y;

        if (input.jump && player.onGround) {
            player.velocity.y = jumpVelocity;
            player.onGround = false;
            // A sprint-jump costs four times an ordinary one, which is what
            // makes travelling fast genuinely expensive rather than merely
            // quicker.
            player.exhaustion +=
                sprinting ? survival::kExhaustSprintJump : survival::kExhaustJump;
        }
        // **Slow Falling scales gravity and the terminal speed by the same
        // factor.** The reference changes both together - 0.08 → 0.01 blocks
        // per tick² and 78.4 → 9.8 m/s (`RESEARCH.md` §1.3) - so one multiplier
        // has to reach both or the terminal speed alone decides how fast you
        // land. `fallSpeedScale` is this project's published figure for it and
        // is not touched here: both of those pairs are a ratio of exactly 0.125
        // and the constant returns 0.125, pinned by an assert in `Effects.hpp`
        // that `78.4 * scale == 9.8`. **Closed, and deliberately stated rather
        // than left silent** - it read 0.15 when this line became its first
        // caller, and the note asking for it to be corrected outlived the
        // correction by an hour. Compensating for the ratio *here* would put a
        // second owner on the same figure, which is the one thing that must not
        // happen to it.
        //
        // **Only while descending** - that is what
        // "falling" means there, and scaling the rise as well would turn a jump
        // under it into a far higher one. It returns exactly 1 with no potion
        // running, so this is the old line unchanged then.
        const float fallScale =
            player.velocity.y <= 0.0f ? effects::fallSpeedScale(player.effects) : 1.0f;
        player.velocity.y = std::max(player.velocity.y - kGravity * fallScale * dt,
                                     -kTerminalVelocity * fallScale);

        // A ladder replaces gravity rather than fighting it: push into one and
        // you go up, let go and you slide down slowly. **Applied after the
        // fall**, because a climb is not a weaker fall - it is a different
        // rule, and the same mistake in water once made every animal sink.
        //
        // **You climb by pushing *into* the ladder**, not by moving at all.
        // That is the reference's rule and it is what leaves strafing free:
        // walking along a wall has no component into it, so A and D move you
        // sideways off the ladder instead of hauling you up it.
        if (const BlockId climbable = climbableAt(world, player); climbable != BlockId::Air) {
            bool anyDirection = false;
            const glm::vec3 into = climbPush(climbable, target, anyDirection);
            const bool pushingIn =
                anyDirection ? glm::dot(target, target) > 0.01f
                             : glm::dot(target, glm::vec2{into.x, into.z}) > 0.01f;
            player.velocity.y = (pushingIn || input.jump) ? kLadderClimbSpeed : -kLadderSlideSpeed;
            // Sneaking holds you still, which is what makes a ladder somewhere
            // you can stop and look around from.
            if (input.sneak) {
                player.velocity.y = 0.0f;
            }
        } else if (player.velocity.y < 0.0f && !player.onGround &&
                   againstSticky(world, player.position, height)) {
            // **The side of a honey block is a ladder you did not climb onto.**
            // "Entities pressed against the sides of a honey block slide down at
            // a slow speed and do not take fall damage, similar to going down a
            // ladder" (minecraft.wiki, *Honey Block*) - so it borrows the
            // ladder's own slide rather than inventing a speed, and sneaking
            // holds you still for the same reason it does on a ladder.
            //
            // Only while already descending: rising past one on a jump is not a
            // slide, and catching the rise would cap every hop taken beside a
            // honey wall.
            player.velocity.y = input.sneak ? 0.0f : -kLadderSlideSpeed;
            player.fallDistance = 0.0f;
        }
    }

    // **A cobweb is one rule over whatever branch just ran**, and that is the
    // shape it has to have: it slows you in air, in water and on the ground
    // alike, and writing it into three branches is how a rule ends up correct
    // in one of the places that need it.
    //
    // "While in contact with it, the player can move at a speed of about 25% of
    // the normal walking speed, and their jumping height is severely reduced...
    // Like water, falling into a cobweb prevents a player from taking fall
    // damage" (minecraft.wiki, *Cobweb*). The vertical is clamped rather than
    // scaled because a scale on a jump that has already been taken is a
    // different number every frame.
    //
    // Creative flight is exempt, which is the reference's own treatment of
    // currents: something that ignores the water's push should not be stopped
    // by a web.
    if (!player.flying && inCobweb(world, player.position, height)) {
        const glm::vec2 flat{player.velocity.x, player.velocity.z};
        const float crawl = kWalkSpeed * kCobwebSpeedScale;
        if (const float speedNow = glm::length(flat); speedNow > crawl) {
            const glm::vec2 held = flat * (crawl / speedNow);
            player.velocity.x = held.x;
            player.velocity.z = held.y;
        }
        player.velocity.y = std::clamp(player.velocity.y, -kCobwebCrawlSpeed, kCobwebCrawlSpeed);
        // Cleared every frame you are inside one rather than at the landing:
        // `RESEARCH.md` §1.10 lists a cobweb with water and slime under "fully
        // resets fall distance", and a fall paid for on the way *out* of a web
        // is the whole of what the block is for undone.
        player.fallDistance = 0.0f;
    }

    // **Powder snow is the second overlay, and it is a different shape from the
    // cobweb above on purpose.** Both sit over whatever branch just ran, for the
    // same reason - you can be in one while walking, swimming or falling - but
    // the cobweb *clamps* your horizontal speed to a crawl and this *drags* it,
    // because the two blocks' figures are published in different units. Java's
    // one table gives the cobweb 0.25 and powder snow 0.9 as per-tick retention
    // factors; ours holds the cobweb as a speed scale because that is how the
    // wiki states it in prose ("about 25% of the normal walking speed") and
    // holds this as a drag because there is no prose figure at all. See
    // `kPowderSnowDrag`, which carries the sourcing and the warning against
    // building this at the cobweb's strength.
    //
    // **`fluid::dragOver` is what makes the per-tick factor frame-rate
    // independent** - the same converter lava's fall halving already uses. A
    // bare `*= 0.9f` per frame would make how fast you cross powder snow depend
    // on how busy the machine was, which is the unit mistake this file has paid
    // for before.
    //
    // Creative flight is exempt, exactly as it is for the cobweb and for
    // currents: something that ignores the water's push is not stopped by snow.
    if (!player.flying && inPowderSnow(world, player.position, height)) {
        const float drag = fluid::dragOver(kPowderSnowDrag, dt);
        player.velocity.x *= drag;
        player.velocity.z *= drag;

        if (input.leatherBoots) {
            // **Leather boots turn it into scaffolding, and that rule lives here
            // rather than in the block table.** minecraft.wiki, *Powder Snow*:
            // "Entities wearing leather boots ... do not fall through powder
            // snow", and a player so equipped "can jump inside the block to
            // ascend, or sneak to descend, similar to scaffolding". The
            // reference implements this as a collision box that exists for some
            // entities and not others - a per-entity shape our block table has
            // no way to express and should not grow one for, since this is the
            // only block in the game that wants it. Expressed as motion instead:
            // you do not sink, jump lifts you, sneak lowers you.
            //
            // It borrows the ladder's two speeds rather than inventing a pair,
            // which is the same borrowing the honey-block slide above does and
            // for the same reason - "similar to scaffolding" is a comparison to
            // a climb, so use the climb's numbers.
            if (input.jump) {
                player.velocity.y = kLadderClimbSpeed;
            } else if (input.sneak) {
                player.velocity.y = -kLadderSlideSpeed;
            } else {
                player.velocity.y = std::max(player.velocity.y, 0.0f);
            }
        } else {
            // **A constant descent, not a fall**, which is what makes sinking
            // feel like sinking. Clamped rather than scaled for the reason the
            // cobweb's vertical is clamped: a scale on a jump already taken is a
            // different number every frame. Symmetric, because you cannot simply
            // hop out of powder snow either - at 2.4 m/s against `kJumpVelocity`
            // this is most of what makes it a trap.
            player.velocity.y =
                std::clamp(player.velocity.y, -kPowderSnowSinkSpeed, kPowderSnowSinkSpeed);
        }

        // **The fall reset is deliberately *not* here**, unlike the cobweb's
        // directly above. `updateSurvival` clears it beside water, ladders and
        // vines, which is where the other three "falling into this is free"
        // blocks are handled and where the block is already being asked about
        // for the freezing clock. One question, one owner, one answer.
    }

    // Vertical first, so standing on ground is established before the horizontal
    // move decides whether a step-up is allowed.
    const glm::vec3 displacement = player.velocity * dt;

    // The resolver snaps out of at most one block of penetration, so no single
    // step may cross more than a fraction of a block. Flying and long falls both
    // exceed a whole block per frame otherwise, and the snap then lands on the
    // wrong side of the wall.
    const float longest =
        std::max({std::abs(displacement.x), std::abs(displacement.y), std::abs(displacement.z)});
    const int steps = std::max(1, static_cast<int>(std::ceil(longest / kMaxStepDistance)));
    const glm::vec3 stepDelta = displacement / static_cast<float>(steps);

    bool blockedX = false;
    bool blockedZ = false;

    // **A bounce ends this frame's vertical motion, and it has to.** `stepDelta`
    // was worked out from the velocity *before* the bounce reversed it, so every
    // remaining sub-step still drives the body downward - straight back into the
    // block it has just left, where the second contact is no longer travelling
    // fast enough to qualify, sets `onGround` and zeroes the velocity. The
    // bounce is eaten outright. Only a fast fall is split into more than one
    // sub-step, so a slime block was a trampoline that stopped working the
    // harder you hit it: above about a nine-block drop at 60 fps, or a one-block
    // drop at 20. The horizontal half of the remaining sub-steps still runs, so
    // nothing is lost but the vertical, which the new velocity applies next
    // frame.
    bool bounced = false;

    for (int i = 0; i < steps; ++i) {
        if (!bounced) {
            const bool movingDown = stepDelta.y <= 0.0f;
            if (moveAxis(player.position, world, 1, stepDelta.y, height)) {
                // Slime throws you back up instead of stopping you. The
                // reference returns the speed you arrived with unless you are
                // sneaking, which is what makes a slime block both a trampoline
                // and something you can walk across deliberately.
                const BlockId landedOn =
                    world.blockAt(static_cast<int>(std::floor(player.position.x)),
                                  static_cast<int>(std::floor(player.position.y - 0.1f)),
                                  static_cast<int>(std::floor(player.position.z)));
                const bool fastEnoughToBounce = movingDown && !player.sneaking &&
                                                player.velocity.y < -kBounceThreshold;

                if (fastEnoughToBounce && landedOn == BlockId::SlimeBlock) {
                    player.velocity.y = -player.velocity.y * kSlimeBounce;
                    // **Slime clears the fall outright** - `RESEARCH.md` §1.10
                    // lists it with water and cobweb, not with the bed's half.
                    // Without this the accumulator survived the bounce and every
                    // descent between bounces was added to it, so coming to rest
                    // charged the whole chain at once: measured, a 5-block drop
                    // onto slime cost 9-10 health against the 2 the same drop
                    // costs on stone, and a 40-block one killed.
                    player.fallDistance = 0.0f;
                    player.onGround = false;
                    bounced = true;
                } else if (fastEnoughToBounce && isBed(landedOn)) {
                    // A bed bounces too, at about a third of the speed you
                    // arrived with, and **halves the fall rather than swallowing
                    // it** - the reference reduces the accumulated distance by
                    // 50 % (`RESEARCH.md` §1.10, alongside hay and honey), where
                    // clearing it outright made a bed a fall-damage switch you
                    // could drop onto from any height.
                    //
                    // **The impact is charged here, because it is a landing.**
                    // That is the reference's own model - half the distance is
                    // paid and the accumulator reset at the moment of contact,
                    // and only then does the bounce happen. Scaling the
                    // accumulator and leaving it for the eventual rest instead
                    // halved it once *per bounce* - three or four times down a
                    // tall drop, with only the small bounce heights added back -
                    // so 40 blocks cost about a quarter of what they should.
                    // Charging each impact makes one drop cost exactly its half,
                    // once, and leaves the next fall measured from zero rather
                    // than from whatever is left of this one.
                    //
                    // The descent within this frame is banked first, for the
                    // same reason an ordinary landing banks it: the resolver has
                    // already snapped the feet to the mattress, so the velocity
                    // no longer knows about it. It cannot double count, because
                    // `bounced` ends the vertical for the rest of the frame.
                    player.fallDistance += std::max(0.0f, startedAt.y - player.position.y);
                    player.velocity.y = -player.velocity.y * kBedBounce;
                    chargeFall(player, kBedFallScale);
                    player.onGround = false;
                    bounced = true;
                } else {
                    player.onGround = movingDown;
                    player.velocity.y = 0.0f;

                    // Descending onto solid ground ends flight. Only a downward
                    // landing counts: clipping a wall while flying sideways
                    // leaves you airborne, and hovering still has no vertical
                    // movement to collide at all.
                    if (player.flying && movingDown) {
                        player.flying = false;
                    }
                }
            } else if (stepDelta.y != 0.0f) {
                player.onGround = false;
            }
        }

        const glm::vec3 beforeHorizontal = player.position;

        // Crouching on solid ground refuses any step that would leave nothing
        // underfoot. Checked per axis, so you can still slide along an edge
        // instead of being pinned in place.
        const bool guardEdges = player.sneaking && player.onGround && !player.flying &&
                                hasGroundBelow(world, player.position);

        const bool hitX = moveAxis(player.position, world, 0, stepDelta.x, height);
        if (guardEdges && !hasGroundBelow(world, player.position)) {
            player.position.x = beforeHorizontal.x;
        }

        const bool hitZ = moveAxis(player.position, world, 2, stepDelta.z, height);
        if (guardEdges && !hasGroundBelow(world, player.position)) {
            player.position.z = beforeHorizontal.z;
        }

        blockedX = blockedX || hitX;
        blockedZ = blockedZ || hitZ;

        if ((hitX || hitZ) && player.onGround && !player.flying) {
            // Retry the same horizontal move from a step higher. Accepted only
            // if it clears the obstacle and there is something to land on.
            glm::vec3 stepped = beforeHorizontal;
            stepped.y += kStepHeight;

            if (!overlapsSolid(world, boxAt(stepped, height))) {
                moveAxis(stepped, world, 0, stepDelta.x, height);
                moveAxis(stepped, world, 2, stepDelta.z, height);

                const auto travelled = [&](const glm::vec3& p) {
                    return glm::distance(glm::vec2{p.x, p.z},
                                         glm::vec2{beforeHorizontal.x, beforeHorizontal.z});
                };

                if (travelled(stepped) > travelled(player.position)) {
                    // Settle back down onto whatever is under the new position.
                    moveAxis(stepped, world, 1, -kStepHeight, height);
                    // The box snaps; only the camera is given the rise to catch
                    // up on, which is what stops a staircase reading as a series
                    // of teleports.
                    player.stepSmooth = std::min(
                        player.stepSmooth + std::max(0.0f, stepped.y - player.position.y), kStepHeight);
                    player.position = stepped;
                }
            }
        }

        // Backstop for every way this step could have moved the player,
        // including the step-up above, which resolves its own position and
        // would otherwise skip the per-axis checks entirely.
        if (guardEdges && !hasGroundBelow(world, player.position)) {
            player.position = beforeHorizontal;
        }
    }

    if (blockedX) {
        player.velocity.x = 0.0f;
    }
    if (blockedZ) {
        player.velocity.z = 0.0f;
    }

    // Pressed against a ledge while swimming, with room over your head: climb.
    // The reference *sets* this rather than adding it, and it is the whole of
    // how you get out of a pool - and how you swim up a wall.
    //
    // **Either fluid, not water.** This asked `player.inWater` for twenty
    // milestones, and `kLavaRiseSpeed` is exactly zero by construction - the
    // published swim impulse under lava's heavier drag balances its heavier
    // gravity precisely - so a player who fell into a lava pool with a
    // one-block lip could not rise, could not climb, and could not get out by
    // any means at all. They sank at `kLavaSinkSpeed` and died. `Fluid.hpp`'s
    // own comment beside `kSwimOutSpeed` describes this as the fluid's rule and
    // `kLavaRiseSpeed`'s says you "leave a lava pool at its edge rather than by
    // swimming up it" - which is precisely the mechanic lava was denied.
    // `CLAUDE.md` bug shape 14: a rule that exists, is correct and is commented
    // in only one of the two places that need it.
    //
    // The reference agrees, though it takes history to say so: minecraft.wiki
    // documents no ledge-climb mechanic for *either* fluid, but *Pocket Edition
    // v0.12.1 alpha* lists **MCPE-3671, "You can't get out of lava", as a bug
    // that was fixed**, in the same version that added "The player can now swim
    // in lava" and "Improved water climbing against a wall". Being unable to
    // leave a lava pool is a bug in Bedrock by name.
    //
    // **Powder snow is deliberately not here, and asking the two fluids by name
    // is what keeps it out.** minecraft.wiki, *Fluid*: "There are only two fluid
    // blocks: water and lava."
    //
    // That exclusion is structural rather than a convention anyone has to
    // remember: `fluid::FluidKind` has exactly two enumerators and `isKind` is a
    // ternary between `isWater` and `isLava`, so there is no third value
    // `sampleFluid` could be handed. Powder snow cannot reach this test even by
    // accident. **`Fluid.hpp` now carries that as a `static_assert` beside
    // `isKind`, with its own non-vacuity control** - added the day the physics
    // landed, because "cannot reach it even by accident" was true and unproven.
    //
    // **It got its physics on 2026-08-19 and it did not come here**, which is
    // what the previous owner of this comment asked for. Its climb-out is a
    // *different mechanic* rather than a variant of this one: leather boots make
    // it behave like scaffolding - hold jump to ascend, sneak to descend - and
    // **without leather boots there is no climb-out at all**, you sink and dig
    // out sideways. Bedrock beta 1.16.210.53: "Wearing Leather Boots now allows
    // entities to climb powder snow Blocks (MCPE-105410)." That rule is an
    // overlay of its own, four hundred lines up beside the cobweb's, which is
    // the right shape for it: a thing that modifies whatever branch ran, not a
    // third branch alongside water and lava.
    //
    // An earlier draft of this comment called the slowdown "cobweb-like",
    // repeating minecraft.wiki's *Powder Snow* phrase "move much slower in it,
    // similar to cobwebs". **That comparison is misleading and is corrected
    // here**: a cobweb holds you to about a quarter of walking speed, powder
    // snow is far milder, and Bedrock publishes no multiplier for it at all.
    // Building it at cobweb strength because a comment said so is precisely
    // what that phrase invites. The warning was heeded - `kPowderSnowDrag` is
    // 0.9 against the cobweb's 0.25, sourced from the one Java table that holds
    // both, and there is an assert keeping them apart.
    if ((player.inWater || lava.inFluid) && !player.flying && (blockedX || blockedZ)) {
        glm::vec3 above = player.position;
        above.y += fluid::kSwimOutHeadroom;
        if (!overlapsSolid(world, boxAt(above, height))) {
            player.velocity.y = fluid::kSwimOutSpeed;
        }
    }

    // Exact at any frame rate, so the rise looks the same at 30 fps as at 240.
    player.stepSmooth *= std::pow(kStepSmoothPerTick, dt / 0.05f);
    if (player.stepSmooth < 0.001f) {
        player.stepSmooth = 0.0f;
    }

    updateSurvival(player, world, dt, startedAt, input.invulnerable, input.armour,
                   // **OR'd here, once.** Boots imply leather, so a caller that
                   // fills only the narrower flag still gets freezing immunity -
                   // `Player.hpp` states the belt-and-braces rule at the two
                   // fields and this is the brace.
                   input.leatherArmour || input.leatherBoots);
}

bool damagePlayer(Player& player, int amount, bool bypassInvulnerability,
                  survival::ArmourSet armour) {
    return damageWithResistance(player, amount, bypassInvulnerability, true, armour);
}

void healPlayer(Player& player, int amount) {
    player.health = std::min(survival::kMaxHealth, player.health + amount);
}

void feedPlayer(Player& player, const survival::FoodValue& value) {
    player.food = std::min(survival::kMaxFood, player.food + value.hunger);
    player.saturation =
        survival::clampSaturation(player.saturation + value.saturation, player.food);
    // **What a food takes away, which until now nothing in the game could do.**
    // `Effects::clearOne` had existed with no caller since it was written, so a
    // honey bottle drunk while poisoned fed you and left the poison running -
    // `CLAUDE.md` bug shape #15, a complete feature one call site short.
    //
    // **It belongs here rather than at the eat site, unlike `grants`.** A grant
    // has to be applied by `Main.cpp` because `grantEffect` is a lambda local to
    // that file which does more than mutate state; a removal is pure state on a
    // member this function already holds, so exporting it would have left the
    // table's new field unread until somebody else's file changed. The row
    // states what happens and the one function every meal passes through does
    // it.
    //
    // **Guarded on `None` rather than on the item.** Naming the honey bottle
    // here would be the food table's knowledge kept somewhere other than the
    // food table - the mistake the `grants` comment in `Main.cpp` calls out by
    // name - and it would go stale the day a second food removes something.
    if (value.removes != effects::Effect::None) {
        player.effects.clearOne(value.removes);
    }
}

void respawnPlayer(Player& player, const glm::vec3& at) {
    player.position = at;
    player.velocity = glm::vec3{0.0f};
    player.health = survival::kMaxHealth;
    player.food = survival::kMaxFood;
    player.saturation = 5.0f;
    player.exhaustion = 0.0f;
    player.air = fluid::kAirSeconds;
    player.drowningSeconds = 0.0f;
    player.fallDistance = 0.0f;
    player.burningSeconds = 0.0f;
    // **Thawed, and both halves of it.** A respawn at a mountaintop bed with
    // 6.9 seconds of freezing still banked would take its first hit within a
    // tenth of a second of arriving, from a clock that ran while you were dead.
    // Bedrock zeroes `TicksFrozen` on death like every other per-entity counter.
    player.freezeSeconds = 0.0f;
    player.freezeTimer = 0.0f;
    // Death takes every effect with it, which is the reference's rule and the
    // reason a potion of harming is survivable rather than a life sentence.
    player.effects.clear();
    player.effectHealTimer = 0.0f;
    player.effectHurtTimer = 0.0f;
    // With the effect, because the pool outliving it would hand the respawn
    // free hearts - the pool is spent health, not a property of the effect.
    player.absorption = 0.0f;
    player.absorptionSeconds = 0.0f;
    player.invulnerableSeconds = 0.0f;
    player.lastDamage = 0;
    // **The killing blow's durability dies with the blow.** `armourWear` is a
    // per-life accumulator: the simulation banks what is owed and `Main.cpp`'s
    // `inventory.wearArmour(player.armourWear)` collects it on the next frame.
    // Carrying a balance across a death charges the last hit of the previous
    // life to the next one, silently, because that drain is guarded on `> 0`
    // and reports nothing.
    //
    // **Correct however armour behaves on death, and that is the point of
    // clearing it here rather than relying on the wear having nowhere to land.**
    // Zeroing a per-life counter at the start of a life is right in every
    // version of the surrounding rules and needs no coordination with them.
    //
    // Finding 9635 has since landed in `Main.cpp`: **death now drops worn
    // armour.** So an uncleared balance would be charged to whatever the
    // player next puts on. Before 9635 it would have been charged to the very
    // pieces they respawned still wearing. Both are wrong, and this line is
    // why neither happens. Corrected 2026-08-19 - the previous version of this
    // comment stated that death did *not* drop armour, which was true when
    // written and read as an invitation to go and implement it. Three separate
    // agents filed it as a stale negative. **There is one drop site and it is
    // in `Main.cpp`; do not add a second.**
    //
    // Whether this line is load-bearing today depends on whether
    // `mining::toolProperties` carries armour rows, which is landing under the
    // armour interlock rather than being a settled no. **Do not delete it on a
    // finding that it changes nothing right now** - that is a claim about the
    // world, and the world has already moved twice underneath this comment.
    player.armourWear = 0;
    player.deathSeconds = 0.0f;
    player.eatingSeconds = 0.0f;
    player.flying = false;
    player.sneaking = false;
}

bool playerOverlapsBlock(const Player& player, const glm::ivec3& block) {
    const Aabb box = boxAt(player.position, player.height());
    const glm::vec3 blockMin{block};
    const glm::vec3 blockMax = blockMin + 1.0f;

    return box.min.x < blockMax.x && box.max.x > blockMin.x && box.min.y < blockMax.y && box.max.y > blockMin.y &&
           box.min.z < blockMax.z && box.max.z > blockMin.z;
}

} // namespace game
