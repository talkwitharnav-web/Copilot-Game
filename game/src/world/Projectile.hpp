#pragma once

#include "item/Item.hpp"
#include "item/SpriteMask.hpp"
#include "world/DrawRange.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace game {

class World;
class Creatures;

/// Every projectile in the game. One so far, and the table exists anyway
/// because the reference's own numbers are per-kind and a second copy of a
/// launch speed is exactly the bug this project keeps paying for.
enum class ProjectileKind : std::uint8_t {
    Arrow,
    /// The thrown pearl. **Its numbers are not the arrow's** - half the launch
    /// speed, half the gravity, and `inertia` of exactly 1, which is the
    /// reference overriding the family default so a pearl has no drag and
    /// therefore no terminal speed at all.
    Pearl,
    /// The thrown egg. Same launch speed as the pearl and the same 0.25 box,
    /// but it keeps the thrown-item family's drag - which is the whole reason
    /// the two are separate rows rather than one "thrown item".
    Egg,
    /// A splash potion, and a lingering one. Both fly like a thrown egg and
    /// neither damages what it hits - what they do happens where they land,
    /// which is the owner's business rather than this system's.
    SplashPotion,
    LingeringPotion,
    Count,
};

/// **Every number here is per *tick*, at twenty ticks a second**, because that
/// is the unit the reference publishes them in and mixing the two is the
/// mistake `CLAUDE.md` records as the third-most-expensive shape of bug.
/// `Projectiles::update` runs a fixed-rate accumulator for the same reason:
/// multiplying by `inertia` once a frame at 120 fps is six times the drag it is
/// meant to be, and the closed form the physics can be checked against stops
/// working the moment the step is not a tick.
struct ProjectileSpecies {
    /// What it is in the inventory, and what it hands back when picked up.
    ItemId item = ItemId::None;
    /// Launch speed at full power, in blocks per tick.
    float power = 3.0f;
    /// Downward acceleration, blocks per tick squared.
    float gravity = 0.05f;
    /// Fraction of the velocity kept each tick. Terminal speed is
    /// `gravity / (1 - inertia)` and is never clamped - it emerges.
    float inertia = 0.99f;
    float liquidInertia = 0.6f;
    /// Damage is `ceil(multiplier * current speed)` - a function of how fast it
    /// is going *now*, never of how fast it was launched, so a long shot and a
    /// shot through water both land softer.
    ///
    /// **Zero means it does no damage, not that it ignores what it hits.** The
    /// reference draws exactly that line: `splash_potion.json` and
    /// `ender_pearl.json` publish `impact_damage: null` and still burst on a
    /// mob - a potion thrown at a zombie is *supposed* to break on the zombie -
    /// and `egg.json` publishes `impact_damage` with `"damage": 0` outright.
    /// Reading zero as "passes creatures by" is what had every thrown item fly
    /// straight through an animal and burst on the wall behind it.
    float damagePerSpeed = 2.0f;
    /// Half the collision box, in blocks. `0.125` is the reference's 0.25-wide
    /// `minecraft:collision_box`, shared by the arrow, the egg, the pearl and
    /// both potions.
    ///
    /// **Nothing reads this yet, and the honest reason is that both collision
    /// tests take a ray rather than a box**: blocks go through
    /// `sweepBlocks`, and entities through `Creatures::strike`, neither of
    /// which can be told a girth. A shot is therefore a moving point, and the
    /// visible cost is that a grazing shot slips through a gap its box would
    /// not fit. Giving it a reader means widening one of those two, which is a
    /// `Raycast.cpp`/`Creature.cpp` change rather than one here.
    ///
    /// > Checked 2026-08-18: zero readers in this file or any other.
    float halfWidth = 0.125f;
    /// Whether it stays where it landed instead of vanishing.
    bool sticksInGround = true;
    /// Whether landing is reported to whoever owns this system.
    ///
    /// **Mechanism, not policy.** A pearl moves the thrower and an egg may
    /// leave a chick, and neither belongs here: this system reads the world and
    /// never writes it, and it has no idea a player or a roster exists. Same
    /// hand-off the creature system uses for its blasts.
    bool reportsImpact = false;
};

const ProjectileSpecies& projectileInfo(ProjectileKind kind);

/// How long the bow takes to reach full draw, in seconds, and the charge curve.
///
/// The curve is quadratic, not linear, and that is the part everyone gets
/// wrong: a bow held half a second is at 0.42 power rather than 0.5. Below
/// `kMinBowCharge` nothing is fired at all - no arrow spent, no wear taken.
constexpr float kBowDrawSeconds = 1.0f;
constexpr float kMinBowCharge = 0.1f;

/// Fraction of full power for a bow held `seconds`.
constexpr float bowCharge(float seconds) {
    const float f = seconds >= kBowDrawSeconds ? 1.0f : seconds / kBowDrawSeconds;
    const float p = (f * f + 2.0f * f) / 3.0f;
    return p > 1.0f ? 1.0f : p;
}

/// Which of the three drawn-bow pictures to show. The visual pull finishes in
/// half the time the physics charge does, which is the reference's own split -
/// so this must never be used to decide how fast the arrow leaves.
constexpr int bowPullStage(float seconds) {
    if (seconds >= kBowDrawSeconds * 0.5f) {
        return 2;
    }
    return seconds >= kBowDrawSeconds * 0.25f ? 1 : 0;
}

// The reference publishes a damage per draw time, and `ceil(2 * 3 * charge)`
// reproduces all four rows of it exactly - 1 at a tenth of a second, 5 at
// eight tenths, 6 at nine, 6 plus a critical roll at a full second. That is
// four independent checks that the curve is the quadratic and not a ramp, and
// they cost nothing at run time.
static_assert(bowCharge(0.0f) == 0.0f);
static_assert(bowCharge(0.10f) > 0.069f && bowCharge(0.10f) < 0.071f);
static_assert(bowCharge(0.80f) > 0.746f && bowCharge(0.80f) < 0.747f);
static_assert(bowCharge(0.90f) > 0.869f && bowCharge(0.90f) < 0.871f);
static_assert(bowCharge(1.00f) == 1.0f && bowCharge(5.0f) == 1.0f);
static_assert(bowCharge(0.5f) < 0.5f, "the charge curve is quadratic, not linear");

/// Where a segment first **enters** an axis-aligned box, in blocks along
/// `direction`, or a negative number if it never does.
///
/// `direction` must be a unit vector, or the answer is not in blocks.
///
/// **Entering, not overlapping**, and the difference is the whole reason this
/// is exported: a shot that starts *inside* the box reports a miss, which is
/// what stops an arrow leaving the player's own eye from hitting the player on
/// the tick it is fired - without the caller having to know about launch
/// windows or owner ids. The same slab test `Creatures::findAimed` runs
/// against a creature; it is spelled out here because the player is not on the
/// roster and cannot be asked the same way.
float segmentEntersBox(const glm::vec3& from, const glm::vec3& direction, float reach,
                       const glm::vec3& boxMin, const glm::vec3& boxMax);

/// Arrows and anything else in flight.
///
/// A flat list beside `ItemEntities`, `Creatures` and `FallingBlock` rather
/// than a shared base class for them all: there is no second implementation in
/// sight, and the four genuinely differ in how they move.
///
/// What is *not* shared with dropped items, and why each one had to be built
/// fresh: a drop resolves collision per axis at the point it reached, which is
/// correct at a fraction of a block per frame and sails through walls at three
/// blocks a tick; a drop's drag is an approach toward a terminal speed, where a
/// projectile's terminal speed falls out of a plain multiply; and a drop never
/// asks about creatures.
class Projectiles {
public:
    /// Who fired a shot. **Creature ids start at one, so zero names the
    /// player** - the same convention `Creatures::strike` and
    /// `Creature::threatId` already use, which is what lets an arrow's blame be
    /// handed straight to them.
    static constexpr std::uint32_t kPlayerOwner = 0u;
    /// Nobody said. A caller that does not name an owner exempts no one by id
    /// and blames the player for the damage, which is exactly how this system
    /// behaved before it could ask - so an un-updated call site keeps its old
    /// behaviour rather than quietly acquiring a new one.
    static constexpr std::uint32_t kNoOwner = 0xFFFFFFFFu;

    /// `velocity` is in blocks per **tick**, along the direction of travel.
    ///
    /// `crit` and `collectable` are decided here and never looked up again -
    /// which is what stops a creative-fired arrow becoming collectable because
    /// the player changed mode while it was in the air.
    ///
    /// `ownerId` is who fired it, and it answers two different questions: who
    /// to blame for the damage, and who the launch window exempts. A creature's
    /// shot must carry `Creatures::Launch::fromId`, or a skeleton shooting past
    /// you and clipping your wolf turns that wolf on **you**.
    void spawn(ProjectileKind kind, const glm::vec3& position, const glm::vec3& velocity, bool crit,
               bool collectable, ItemId payload = ItemId::None,
               std::uint32_t ownerId = kNoOwner);

    /// How far along a shot's remaining segment the player's own box is
    /// entered, or a negative number for a miss. `segmentEntersBox` answers it
    /// in one line.
    ///
    /// **A test, never an effect.** The player is not in the roster and this
    /// system has no idea it exists, so it asks the caller where the player is
    /// and reports the hit back through `takePlayerHits` - the same
    /// mechanism-not-policy hand-off `takeLandings` already uses. Damage,
    /// immunity frames and knockback stay with whoever owns the player.
    using PlayerReach =
        std::function<float(const glm::vec3& from, const glm::vec3& direction, float reach)>;

    /// Flies, hits, sticks and expires. Takes the roster because a projectile
    /// has to ask what is in the way, and the world because a hit can matter to
    /// a block.
    ///
    /// Without a `playerReach` the player cannot be hit at all, which is what
    /// the four archer species have been doing since they arrived.
    void update(const World& world, Creatures& creatures, float deltaSeconds,
                const PlayerReach& playerReach = {});

    /// Arrows lying about that a player has walked over. Same arrangement as
    /// dropped items: the caller decides whether there is room, so nothing
    /// disappears into a full inventory.
    struct Collectable {
        std::size_t index;
        ItemId item;
        int count;
    };
    std::vector<Collectable> collectable(const glm::vec3& playerFeet) const;
    void remove(std::size_t index);

    /// Where a shot that reports its impact came to rest.
    ///
    /// **Drained rather than acted on**, the same hand-off the creature system
    /// uses for its blasts: this system reads the world and never writes it,
    /// and it has no idea the player exists.
    struct Landing {
        ProjectileKind kind;
        /// Where the projectile stopped, already backed off the surface.
        glm::vec3 position;
        /// Unit step out of the block that was struck, so the owner knows which
        /// way is clear.
        glm::ivec3 normal;
        /// Which potion it was, for the two kinds that are one. **Carried
        /// rather than looked up**: forty-one brews share two projectile kinds,
        /// and the shot is the only thing that knows which one it is.
        ItemId payload = ItemId::None;
    };
    std::vector<Landing> takeLandings();

    /// A shot that reached the player.
    ///
    /// **Drained rather than applied**, for the same reason a landing is: this
    /// system cannot see the player, does not know what armour or effects are
    /// on it, and must not invent a second damage-immunity rule beside the one
    /// the player already owns.
    struct PlayerHit {
        ProjectileKind kind;
        /// Where the segment entered the player's box.
        glm::vec3 position;
        /// Unit direction of travel, for whoever wants to push the player along
        /// it. **No knockback is applied here and none is implied**: there is no
        /// sourced number for an arrow's push in this project's tables, and
        /// inventing one is forbidden.
        glm::vec3 direction;
        /// Already through `ceil(multiplier * current speed)` and the critical
        /// roll, so it is the same damage a creature would have taken.
        int damage;
    };
    std::vector<PlayerHit> takePlayerHits();

    /// A shot that reached a **creature**.
    ///
    /// Named for the `Creatures::strike` call that produces it, and **not**
    /// `CreatureHit` - `Creature.hpp` already owns a struct by that name for a
    /// blow one creature landed on another. Two different things wearing one
    /// name is a bug this codebase has already paid for twice.
    ///
    /// **Drained rather than acted on**, for the third time in this class and
    /// the same reason as the other two: this system reads the world and never
    /// writes it, it cannot see the mixer, and `Sounds.hpp` has no business
    /// being included by a physics file. The frame loop decides what a hit
    /// sounds like, exactly as `World::takeWashedBlocks` lets it decide what a
    /// washed-away plant drops.
    ///
    /// The damage itself is **already applied** by `Creatures::strike` before
    /// this is recorded - unlike `PlayerHit`, which the frame loop must act on
    /// because the player is not on the roster. This one is a notification, and
    /// the creature's own hurt voice is reported separately by `Creatures`.
    ///
    /// **Must be drained every frame**, like its two siblings: nothing in here
    /// expires on its own.
    struct CreatureStrike {
        ProjectileKind kind;
        /// The near end of the segment the strike was found in - where the shot
        /// stood when this tick began.
        ///
        /// **Not the point of impact, and it cannot be**: `Creatures::strike`
        /// reports only *whether* it hit, never *where*, so the contact point is
        /// knowable only inside `Creature.cpp`. The impact lies somewhere in
        /// `[0, reach]` along `direction` from here - at most one tick of
        /// travel, which is three blocks for an arrow at full draw and much
        /// less for one that has flown any distance.
        glm::vec3 position;
        /// Unit direction of travel, and the axis `reach` is measured along.
        /// Carried for the same reason `PlayerHit` carries it, and **no
        /// knockback is applied here or implied**.
        glm::vec3 direction;
        /// How far along `direction` the strike was searched, in blocks. The
        /// other half of `position`: without it the caller has a ray with no
        /// end and no way to know how wrong the point may be.
        float reach;
        /// Already through `ceil(multiplier * current speed)` and the critical
        /// roll, so it is the number the creature actually lost.
        int damage;
        /// **Who fired it, carried rather than re-derived.** This is the exact
        /// value handed to `Creatures::strike` as its `fromId` - the owner's id
        /// where one was named and `kPlayerOwner` where none was - so the frame
        /// loop never has to work out blame a second time from `ownerId` and
        /// the launch window. Re-deriving an answer somewhere other than the
        /// one place that owns it is this project's most expensive bug shape,
        /// and this field exists to make that impossible.
        ///
        /// **The victim's id is deliberately absent**, because it is not
        /// knowable here: `strike` returns a `bool`. See the note on
        /// `takeCreatureStrikes`.
        std::uint32_t blameId;
    };
    /// **Extends the existing hand-off rather than adding a second kind.** The
    /// shape is `takeLandings`' and `takePlayerHits`', down to the swap.
    ///
    /// **`Creatures::strike` reports the struck index and the entry distance
    /// today** - it grew `hitDistance` and `hitIndex` out-parameters after this
    /// note was first written saying it did not. So a caller that needs *which*
    /// creature was hit, for a per-species sound say, is one pair of arguments
    /// and two fields away, with no `Creature.hpp` change at all. What is still
    /// true is the reason this does not ask `findAimed` itself: that would run
    /// the ray twice and work the answer out in a second place, which is the
    /// thing `fromId`/`ignoreId` were added to stop.
    ///
    /// > Checked 2026-08-18. Falsified if `Creatures::strike` loses either
    /// > out-parameter.
    std::vector<CreatureStrike> takeCreatureStrikes();

    /// Rebuilt every frame rather than transformed, because world meshes draw
    /// with an identity model matrix - the shader recovers its normals from
    /// screen-space derivatives of world position, which only holds while
    /// vertex positions *are* world positions.
    ///
    /// **Drawn between ticks, not on them.** The physics runs at the
    /// reference's twenty a second and must keep doing so, but a screen at 120
    /// shows each of those twenty positions six frames running - which is
    /// exactly what "choppy" looks like. Every shot keeps where it was at the
    /// last tick, and this blends toward where it is now by however much of the
    /// next tick has elapsed. That costs one vec3 per shot and leaves the
    /// simulation untouched.
    ///
    /// `eye` is where the camera is. A thrown item is **billboarded** toward
    /// it, which is what the reference's own thrown-item renderer does - it
    /// takes the camera's orientation outright.
    engine::MeshData buildMesh(const World& world, const SpriteMask& sprites, const glm::vec3& eye,
                               const DrawRange& range = {}) const;

    std::size_t count() const { return m_shots.size(); }

private:
    struct Shot {
        glm::vec3 position{0.0f};
        /// Where it was at the last tick, for drawing only. **Never read by the
        /// simulation** - a physics step that looked at it would be reading a
        /// value the renderer owns.
        glm::vec3 previousPosition{0.0f};
        /// Blocks per tick. Frozen at zero once it has landed.
        glm::vec3 velocity{0.0f};
        /// The direction it is drawn pointing along, kept when it stops so a
        /// landed arrow does not snap flat.
        glm::vec3 heading{0.0f, 0.0f, 1.0f};
        /// Its heading at the last tick, so the shaft turns as smoothly as it
        /// travels rather than snapping twenty times a second.
        glm::vec3 previousHeading{0.0f, 0.0f, 1.0f};
        ProjectileKind kind = ProjectileKind::Arrow;
        /// Which brew a thrown potion is carrying, and nothing for everything
        /// else.
        ItemId payload = ItemId::None;
        /// Who fired it. `kNoOwner` until a caller says otherwise.
        std::uint32_t ownerId = kNoOwner;
        float age = 0.0f;
        /// Ticks simulated since launch. **The unit every hit decision is made
        /// in**, because `age` counts wall time and a single long frame can
        /// consume a five-tick window before the first tick has run. Skipped
        /// ticks - a shot held over an unloaded column - do not count, which is
        /// the point: the window is five ticks of *flight*.
        std::uint16_t ticksAlive = 0;
        /// Counts down the landing wobble, the reference's own 0.35 s.
        float shake = 0.0f;
        /// **The cell it actually struck**, carried rather than re-derived.
        ///
        /// A landed shot sits `kEmbedBackoff` *outside* the face it hit, so the
        /// cell its own position falls in is the empty one in front of the
        /// block for a floor hit, beside it for a wall and below it for a
        /// ceiling. Working the block out from the position again therefore
        /// reads air on two faces in three, which is exactly the bug this field
        /// exists to close: an arrow shot into a wall or a ceiling came loose on
        /// its very next tick and dropped to the floor. Only meaningful while
        /// `landed` is true.
        glm::ivec3 landedIn{0};
        bool crit = false;
        bool collectable = true;
        bool landed = false;
    };

    void tick(const World& world, Creatures& creatures, const PlayerReach& playerReach);

    std::vector<Shot> m_shots;
    std::vector<Landing> m_landings;
    std::vector<PlayerHit> m_playerHits;
    std::vector<CreatureStrike> m_creatureStrikes;
    /// Leftover time toward the next fixed tick.
    float m_accumulator = 0.0f;
    /// Only the critical roll needs randomness, and only its size.
    std::uint32_t m_random = 0x9E3779B9u;
};

} // namespace game
