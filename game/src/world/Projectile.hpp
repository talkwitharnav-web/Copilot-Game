#pragma once

#include "item/Item.hpp"
#include "item/SpriteMask.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <cstdint>
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
    /// shot through water both land softer. **Zero means it passes creatures
    /// by entirely**, which is the reference's `impact_damage: null`.
    float damagePerSpeed = 2.0f;
    /// Half the collision box, in blocks.
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
    /// `velocity` is in blocks per **tick**, along the direction of travel.
    ///
    /// `crit` and `collectable` are decided here and never looked up again -
    /// which is what stops a creative-fired arrow becoming collectable because
    /// the player changed mode while it was in the air.
    void spawn(ProjectileKind kind, const glm::vec3& position, const glm::vec3& velocity, bool crit,
               bool collectable);

    /// Flies, hits, sticks and expires. Takes the roster because a projectile
    /// has to ask what is in the way, and the world because a hit can matter to
    /// a block.
    void update(const World& world, Creatures& creatures, float deltaSeconds);

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
    };
    std::vector<Landing> takeLandings();

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
    engine::MeshData buildMesh(const World& world, const SpriteMask& sprites, const glm::vec3& eye) const;

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
        float age = 0.0f;
        /// Counts down the landing wobble, the reference's own 0.35 s.
        float shake = 0.0f;
        bool crit = false;
        bool collectable = true;
        bool landed = false;
    };

    void tick(const World& world, Creatures& creatures);

    std::vector<Shot> m_shots;
    std::vector<Landing> m_landings;
    /// Leftover time toward the next fixed tick.
    float m_accumulator = 0.0f;
    /// Only the critical roll needs randomness, and only its size.
    std::uint32_t m_random = 0x9E3779B9u;
};

} // namespace game
