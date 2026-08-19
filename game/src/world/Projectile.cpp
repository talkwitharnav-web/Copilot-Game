#include "world/Projectile.hpp"

#include "item/SpriteModel.hpp"
#include "world/Collision.hpp"
#include "world/Creature.hpp"
#include "world/Raycast.hpp"
#include "world/Tick.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace game {
namespace {

/// The reference runs at twenty ticks a second and publishes every projectile
/// number per tick. Running this on `deltaSeconds` instead would make the drag
/// six times too strong at 120 fps, and would throw away the one cheap way to
/// check the physics: at a fixed step the closed form
/// `v(t) = inertia^t * (v0 + g/(1-inertia)) - g/(1-inertia)` holds exactly.
///
/// **The rate comes from `Tick.hpp`, which is the one owner**; the local name
/// stays because every reader below is written in ticks and reads better for
/// it. It was a literal 0.05 here, one of seven identical copies wearing four
/// different spellings.
constexpr float kTickSeconds = tick::kSeconds;

/// A frame that took longer than this drops the extra rather than catching up,
/// which is what stops a hitch firing forty ticks at once.
constexpr int kMaxTicksPerFrame = 6;

/// Ticks after launch during which a shot cannot hit whoever fired it. The
/// reference's `owner_launch_immunity_ticks`, and it is a window rather than a
/// permanent exemption - a ricochet is meant to be able to come back at you.
///
/// **Counted in ticks, never in wall time.** It used to be `5 * kTickSeconds`
/// compared against `age`, which advances by a whole frame *before* the tick
/// loop runs - so a shot fired on a 0.25 s frame (alt-tabbing, a chunk-streaming
/// hitch, or dragging the window, which blocks `pollEvents` for the whole drag)
/// had the window already shut on its very first tick, while it was still at
/// the muzzle. The player was saved by geometry - `segmentEntersBox` reports a
/// box *entered* and a shot leaving an eye starts inside - but a creature is
/// saved only by `exemptId`, and that expression had just been handed "skips
/// nobody". A skeleton firing during a hitch shot itself.
constexpr std::uint16_t kOwnerImmunityTicks = 5;

/// How far back from the surface a landed shot sits, so its nose is not buried
/// in the block it struck. **Blocks**, and ours - the reference publishes no
/// figure, because it stops an arrow with a box rather than with a ray.
constexpr float kEmbedBackoff = 0.05f;

/// **Seconds**, and the reference's own: `arrow.json`'s
/// `stick_in_ground.shake_time` is 0.35. It is wall time rather than ticks
/// deliberately - the wobble is decay a viewer sees, not a decision a hit
/// depends on. See `kOwnerImmunityTicks` for the line between the two.
constexpr float kShakeSeconds = 0.35f;

/// **Seconds.** One minute, the reference's figure for an arrow lying in a
/// block, and the same cap on one that never hits anything - which the
/// reference leaves to chunk unloading and we have no equivalent of.
///
/// > Divergence, deliberate and small: the reference counts its minute from the
/// > moment the arrow *lands*, where `age` starts at launch. A shot that flies
/// > for ten seconds is therefore collectable for fifty rather than sixty.
constexpr float kDespawnSeconds = 60.0f;

/// How close a player's chest has to come to a landed arrow to pick it up, in
/// **blocks**. Ours: the reference collects with a box overlap, which has no
/// single radius to port.
constexpr float kCollectRadius = 0.9f;

/// Half a thrown item's drawn extent. **The dropped item's own size**, because
/// the whole point is that a pearl in the air is the same object it is on the
/// floor.
constexpr float kThrownHalf = 0.15f;

constexpr std::array<ProjectileSpecies, static_cast<std::size_t>(ProjectileKind::Count)> kProjectiles{{
    {.item = ItemId::Arrow,
     .power = 3.0f,
     .gravity = 0.05f,
     .inertia = 0.99f,
     .liquidInertia = 0.6f,
     .damagePerSpeed = 2.0f,
     .halfWidth = 0.125f,
     .sticksInGround = true},
    // `ender_pearl.json`, verbatim: power 1.5, gravity 0.025, and **both
    // inertias set to 1**, which is the reference overriding the thrown-item
    // family's 0.99 air and 0.6 water. No drag means no terminal speed, and it
    // is what makes a pearl thrown straight up reach forty-five blocks where a
    // snowball on the family default reaches twenty-eight.
    {.item = ItemId::VoidPearl,
     .power = 1.5f,
     .gravity = 0.025f,
     .inertia = 1.0f,
     .liquidInertia = 1.0f,
     .damagePerSpeed = 0.0f,
     .halfWidth = 0.125f,
     .sticksInGround = false,
     .reportsImpact = true},
    // `egg.json`: the same 1.5 launch and 0.25 box, but the family's own drag
    // rather than the pearl's override, and 0.03 gravity rather than 0.025. It
    // therefore falls shorter and lands sooner, which is exactly the difference
    // a single shared "thrown item" row would have thrown away.
    {.item = ItemId::Egg,
     .power = 1.5f,
     .gravity = 0.03f,
     .inertia = 0.99f,
     .liquidInertia = 0.6f,
     .damagePerSpeed = 0.0f,
     .halfWidth = 0.125f,
     .sticksInGround = false,
     .reportsImpact = true},
    // A thrown potion. The reference's `splash_potion.json` publishes a **0.5**
    // launch against the egg's 1.5 - it is lobbed rather than thrown - and the
    // arrow's own 0.05 gravity, which is why it drops as steeply as it does for
    // so slow a throw. Neither form damages what it hits: `impact_damage` is
    // `null`, which in the reference means no damage, **not** that it passes
    // through - a potion thrown at a zombie breaks on the zombie, and `tick`
    // now does the same.
    //
    // `item` stays `None` for both, because forty-one brews share each row and
    // which one this is rides on the shot instead.
    //
    // > Not carried over: `angle_offset: -20.0`, the twenty degrees above the
    // > aim a thrown potion actually leaves at. That belongs to whoever builds
    // > the launch velocity, which is the frame loop, not this table.
    {.item = ItemId::None,
     .power = 0.5f,
     .gravity = 0.05f,
     .inertia = 0.99f,
     .liquidInertia = 0.6f,
     .damagePerSpeed = 0.0f,
     .halfWidth = 0.125f,
     .sticksInGround = false,
     .reportsImpact = true},
    {.item = ItemId::None,
     .power = 0.5f,
     .gravity = 0.05f,
     .inertia = 0.99f,
     .liquidInertia = 0.6f,
     .damagePerSpeed = 0.0f,
     .halfWidth = 0.125f,
     .sticksInGround = false,
     .reportsImpact = true},
}};

} // namespace

// Terminal speed is `gravity / (1 - inertia)`, which is what this file's
// update order (position, then drag, then acceleration) produces. It is a
// derived check on the two constants above it, not a claim about any published
// figure: `tick` explains why the wiki's per-family order column cannot be used
// as evidence here, and why a potion of ours settles at 5.00 rather than the
// 4.95 that column implies.
//
// **The arrow only.** A pearl's inertia is exactly 1, so the expression divides
// by zero and the quantity genuinely does not exist for it.
constexpr float terminalSpeed(float gravity, float inertia) { return gravity / (1.0f - inertia); }
static_assert(terminalSpeed(kProjectiles[0].gravity, kProjectiles[0].inertia) > 4.999f &&
                  terminalSpeed(kProjectiles[0].gravity, kProjectiles[0].inertia) < 5.001f,
              "an arrow's terminal speed must be five blocks a tick");
static_assert(terminalSpeed(kProjectiles[0].gravity, kProjectiles[0].liquidInertia) > 0.1249f &&
                  terminalSpeed(kProjectiles[0].gravity, kProjectiles[0].liquidInertia) < 0.1251f,
              "and an eighth of a block a tick in water");

// **The negative twin, and it is the one that catches a real edit.** The
// positive asserts above pass on the constants we already have; this one fails
// the moment somebody "corrects" the potion rows toward the wiki's 4.95 by
// scaling their gravity, because that is the shape the stale comment this
// replaced was arguing for. Every species that has drag must agree with the
// arrow's own drag, and only the gravity may differ between rows.
static_assert(kProjectiles[static_cast<std::size_t>(ProjectileKind::SplashPotion)].gravity ==
                      kProjectiles[0].gravity &&
                  kProjectiles[static_cast<std::size_t>(ProjectileKind::LingeringPotion)].gravity ==
                      kProjectiles[0].gravity,
              "splash_potion.json publishes an arrow's 0.05 gravity, not a scaled one");
static_assert(terminalSpeed(kProjectiles[static_cast<std::size_t>(ProjectileKind::Egg)].gravity,
                            kProjectiles[static_cast<std::size_t>(ProjectileKind::Egg)].inertia) >
                      terminalSpeed(kProjectiles[static_cast<std::size_t>(ProjectileKind::Egg)].gravity,
                                    kProjectiles[0].liquidInertia),
              "drag in air must be gentler than drag in water, or the two are swapped");

// A pearl thrown straight up rises `v^2 / 2g` blocks. The reference publishes
// forty-five for Bedrock against twenty-eight for the drag-bearing Java one, so
// this is the check that its two overridden inertias were actually carried over
// rather than left at the family default.
static_assert(kProjectiles[1].inertia == 1.0f && kProjectiles[1].liquidInertia == 1.0f);
static_assert(kProjectiles[1].power * kProjectiles[1].power / (2.0f * kProjectiles[1].gravity) > 44.9f &&
                  kProjectiles[1].power * kProjectiles[1].power / (2.0f * kProjectiles[1].gravity) < 45.1f,
              "a pearl must reach forty-five blocks thrown straight up");

// Every `power` above is blocks per **tick**, and this is the one place that
// says so in a unit anybody can check: three blocks a tick is the reference's
// published 60 m/s muzzle speed for a fully drawn bow. It is exact rather than
// bracketed because `3.0f / 0.05f` rounds to exactly 60.
//
// > Fails if: `kTickSeconds` is re-literalised here instead of derived from
// > `tick::kSeconds`, or the tick rate is changed - at 25 Hz the same arrow
// > leaves the bow at 75 m/s and every number in this file is silently wrong.
static_assert(kProjectiles[0].power / kTickSeconds == 60.0f,
              "a fully drawn arrow leaves the bow at the reference's three blocks a tick, "
              "which is 60 m/s only at a twenty-tick second");

const ProjectileSpecies& projectileInfo(ProjectileKind kind) {
    return kProjectiles[static_cast<std::size_t>(kind)];
}

float segmentEntersBox(const glm::vec3& from, const glm::vec3& direction, float reach,
                       const glm::vec3& boxMin, const glm::vec3& boxMax) {
    // The standard slab test, kept as the **unclamped** interval so the sign of
    // the entry survives: clamping it at zero - which is the natural way to
    // write this, and the way `Creatures::findAimed` writes it - loses the one
    // fact the caller needs, which is whether the segment started inside.
    float enter = -std::numeric_limits<float>::infinity();
    float leave = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1e-6f) {
            // Parallel to this pair of faces: either always between them or
            // never, and dividing would give an infinity.
            if (from[axis] < boxMin[axis] || from[axis] > boxMax[axis]) {
                return -1.0f;
            }
            continue;
        }
        float first = (boxMin[axis] - from[axis]) / direction[axis];
        float second = (boxMax[axis] - from[axis]) / direction[axis];
        if (first > second) {
            std::swap(first, second);
        }
        enter = std::max(enter, first);
        leave = std::min(leave, second);
    }
    if (enter > leave || enter <= 0.0f || enter > reach) {
        return -1.0f;
    }
    return enter;
}

void Projectiles::spawn(ProjectileKind kind, const glm::vec3& position, const glm::vec3& velocity,
                        bool crit, bool collectable, ItemId payload, std::uint32_t ownerId) {
    Shot shot;
    shot.kind = kind;
    shot.payload = payload;
    shot.ownerId = ownerId;
    shot.position = position;
    // Both ends of the first frame's blend are the muzzle, or a shot would be
    // drawn streaking in from wherever the previous one happened to be.
    shot.previousPosition = position;
    shot.velocity = velocity;
    const float speed = glm::length(velocity);
    if (speed > 1e-5f) {
        shot.heading = velocity / speed;
    }
    shot.previousHeading = shot.heading;
    shot.crit = crit;
    shot.collectable = collectable;
    m_shots.push_back(shot);
}

void Projectiles::update(const World& world, Creatures& creatures, float deltaSeconds,
                         const PlayerReach& playerReach) {
    if (m_shots.empty()) {
        m_accumulator = 0.0f;
        return;
    }

    for (Shot& shot : m_shots) {
        // **Wall time on purpose, and only for these two.** The shake is a
        // visual decay and the despawn is a cleanup bound that has to keep
        // running for a shot parked over unloaded ground - which ticks
        // deliberately do not. Nothing that decides a *hit* may read `age`;
        // see `kOwnerImmunityTicks`.
        shot.age += deltaSeconds;
        shot.shake = std::max(0.0f, shot.shake - deltaSeconds);
    }

    m_accumulator += deltaSeconds;
    int steps = 0;
    while (m_accumulator >= kTickSeconds && steps < kMaxTicksPerFrame) {
        m_accumulator -= kTickSeconds;
        ++steps;
        tick(world, creatures, playerReach);
    }
    if (steps == kMaxTicksPerFrame) {
        m_accumulator = 0.0f;
    }

    for (std::size_t i = m_shots.size(); i-- > 0;) {
        if (m_shots[i].age > kDespawnSeconds) {
            m_shots[i] = m_shots.back();
            m_shots.pop_back();
        }
    }
}

void Projectiles::tick(const World& world, Creatures& creatures, const PlayerReach& playerReach) {
    for (std::size_t i = m_shots.size(); i-- > 0;) {
        Shot& shot = m_shots[i];
        // Where this tick started, for the frames that will be drawn before the
        // next one. Taken before every early-out, so a shot that stops moving
        // has both ends of the blend equal and simply sits still.
        shot.previousPosition = shot.position;
        shot.previousHeading = shot.heading;

        // **The row this file was missing from the guard table.** The player,
        // dropped items and falling blocks all refuse to simulate over ground
        // that has not arrived; projectiles had the delta clamp and the sweep
        // and not this. An absent chunk reads as air (`World.hpp` says so
        // outright), so a landed arrow found no block under it and came loose,
        // and a pearl thrown past the loaded edge flew on through the terrain
        // it should have struck - spent, with no teleport. Standing still until
        // the column is back is the same answer `ItemEntity.cpp` gives.
        //
        // **Velocity is kept, where a drop's is zeroed.** A skipped tick is a
        // tick that did not happen, and a shot's velocity *is* its trajectory:
        // zeroing it would drop a paused arrow out of the sky the moment its
        // chunk returned. `age` still advances in `update`, so a shot stranded
        // over unloaded ground despawns rather than hanging there forever.
        if (!world.columnResident(static_cast<int>(std::floor(shot.position.x)),
                                  static_cast<int>(std::floor(shot.position.z)))) {
            continue;
        }

        // Incremented before this tick's own hit tests, so the tick a shot is
        // launched on counts as its first - which is why the window below is
        // `<=` and not `<`. Five ticks either way; writing one without the
        // other silently gives four or six.
        if (shot.ticksAlive < std::numeric_limits<std::uint16_t>::max()) {
            ++shot.ticksAlive;
        }

        if (shot.landed) {
            // A shot whose block was mined comes loose and drops in a sharp
            // arc, which is the reference's own behaviour and costs one test.
            //
            // **The cell it struck, not the cell it is standing in.** Those are
            // the same thing only for a top-face hit: `shot.position` is backed
            // off `kEmbedBackoff` along the face normal, so a wall hit leaves it
            // in the empty cell *beside* the block and a ceiling hit in the one
            // *below*. Deriving the block from the position therefore read air
            // on two faces in three, and an arrow shot into a wall or a ceiling
            // came loose on its very next tick and fell to the floor - where the
            // reference sticks it to whatever face it hit. `landedIn` is what
            // `sweepBlocks` already reported, carried rather than re-derived.
            //
            // **`worldCollisionBoxes`, not `collisionBoxes`.** The latter can
            // only see an id, so it assumes a lone pane grows every arm - the
            // exact bug the world-aware form was added to fix.
            if (worldCollisionBoxes(world, shot.landedIn.x, shot.landedIn.y, shot.landedIn.z)
                    .count == 0) {
                shot.landed = false;
            }
            continue;
        }

        const ProjectileSpecies& species = projectileInfo(shot.kind);
        const glm::vec3 target = shot.position + shot.velocity;

        // **Blocks first, then whatever is inside the shortened segment.** The
        // other order lets an arrow hit something standing behind a wall.
        const SweepHit blocked = sweepBlocks(world, shot.position, target);
        const glm::vec3 limit = blocked.hit ? blocked.point : target;

        const glm::vec3 leg = limit - shot.position;
        const float reach = glm::length(leg);
        if (reach > 1e-5f) {
            const glm::vec3 heading = leg / reach;

            // **The launch window is the owner's, not everyone's.** It used to
            // gate every entity test, which is why the four archer species drew,
            // aimed, loosed and shot straight through you: at three blocks a
            // tick a skeleton's arrow is exempt for its first fifteen blocks,
            // and a skeleton engages from sixteen.
            //
            // A shot that never named an owner keeps the old, safe answer - it
            // exempts the whole roster for those five ticks, because it may
            // have left a creature's own hand and there is no id to skip.
            const bool ownerKnown = shot.ownerId != kNoOwner;
            const bool windowOpen = shot.ticksAlive <= kOwnerImmunityTicks;
            const bool canHitCreature = ownerKnown || !windowOpen;
            // The player needs no such window: `segmentEntersBox` reports only
            // a box **entered**, so a shot leaving the player's own eye starts
            // inside and misses, and by the next tick it is three blocks away
            // and travelling. An arrow you fired straight up can still come
            // back down on you, which is the reference's behaviour.
            const bool canHitPlayer =
                static_cast<bool>(playerReach) && !(windowOpen && shot.ownerId == kPlayerOwner);
            const float playerAt = canHitPlayer ? playerReach(shot.position, heading, reach) : -1.0f;

            if (canHitCreature || playerAt >= 0.0f) {
                // **Damage and collision are two questions, not one.** This
                // whole block used to be gated on `damagePerSpeed > 0`, which
                // is a damage test standing in for a "does it collide" test -
                // so every thrown item flew straight through an animal and
                // burst on the wall behind it. The reference draws the line the
                // other way round: `splash_potion.json` and `ender_pearl.json`
                // publish `impact_damage: null` and still stop on a mob, and
                // `egg.json` publishes `impact_damage` with `"damage": 0`.
                const bool hurts = species.damagePerSpeed > 0.0f;

                // Damage comes from the speed it is doing **now**, so a long
                // shot and a shot that has crossed water both land softer. The
                // reference stores no damage on the arrow at all -
                // `impact_damage.damage` is zero and `power_multiplier` is the
                // whole of it.
                const float speed = glm::length(shot.velocity);
                int damage = 0;
                if (hurts) {
                    damage = static_cast<int>(std::ceil(species.damagePerSpeed * speed));
                    if (shot.crit) {
                        // Rounded up **before** the critical roll, not after,
                        // which the reference spells out with its own
                        // `ceil_pre_critical_damage`.
                        m_random = m_random * 1664525u + 1013904223u;
                        damage += static_cast<int>((m_random >> 16) %
                                                   static_cast<unsigned>(damage / 2 + 2));
                    }
                    damage = std::max(1, damage);
                }

                // **Nearest wins, and one entity at a time.** The creature test
                // is the one that can be shortened, so handing it the player's
                // distance as its reach puts anything standing behind you out
                // of range - the same trick the block sweep above already plays
                // on both of them. Either branch retires the shot, so it can
                // never spend itself twice in a tick.
                const float entityReach = playerAt >= 0.0f ? std::min(reach, playerAt) : reach;
                // Who the damage is blamed on, and who may not be hit by it.
                // **Zero skips nobody**, because creature ids start at one - so
                // an unattributed shot blames the player exactly as it always
                // has, and the exemption lasts only as long as the window.
                const std::uint32_t blameId = ownerKnown ? shot.ownerId : kPlayerOwner;
                const std::uint32_t exemptId = windowOpen ? blameId : 0u;
                // What a shot that carries no damage does on contact: it stops
                // where it touched, and whoever owns this system is told, which
                // is the same hand-off a block hit already uses. Deliberately
                // **not** a `CreatureStrike` - that record means "a creature
                // lost health", and the frame loop turns every one of them into
                // a bow-hit noise.
                const auto burstOn = [&](const glm::vec3& at) {
                    shot.position = at;
                    shot.velocity = glm::vec3{0.0f};
                    if (species.reportsImpact) {
                        // The face it came in through, as a whole axis, so a
                        // reader that wants a direction gets a true one rather
                        // than a placeholder.
                        glm::ivec3 normal{0};
                        const glm::vec3 back = -heading;
                        const int axis = std::abs(back.x) >= std::abs(back.y)
                                             ? (std::abs(back.x) >= std::abs(back.z) ? 0 : 2)
                                             : (std::abs(back.y) >= std::abs(back.z) ? 1 : 2);
                        normal[axis] = back[axis] >= 0.0f ? 1 : -1;
                        m_landings.push_back({shot.kind, shot.position, normal, shot.payload});
                    }
                };
                // Where along the segment the entity was met. Only meaningful
                // when `strike` says yes, and only read there.
                float struckAt = entityReach;
                if (canHitCreature &&
                    creatures.strike(shot.position, heading, entityReach, damage, blameId, exemptId,
                                     &struckAt)) {
                    if (hurts) {
                        // **Reported, not sounded.** The damage is already done
                        // - `strike` applied it - so this is a notification the
                        // frame loop turns into a noise, the same hand-off
                        // `takeLandings` and `takePlayerHits` use. Pushed
                        // exactly once and immediately before the shot is
                        // retired, so it can neither be missed nor recorded
                        // twice: `strike` is reached once per shot per tick and
                        // this branch ends the shot.
                        //
                        // `blameId` rides along rather than being worked out
                        // again by the reader. It is the identical expression
                        // `strike` was just handed.
                        m_creatureStrikes.push_back(
                            {shot.kind, shot.position, heading, entityReach, damage, blameId});
                    } else {
                        // A zero-damage `strike` is a pure query: `damageCreature`
                        // refuses anything at or below zero *before* the blow is
                        // recorded, so nothing was hurt, angered, shoved or made
                        // to cry out - only the ray was answered.
                        burstOn(shot.position + heading * struckAt);
                    }
                    m_shots[i] = m_shots.back();
                    m_shots.pop_back();
                    continue;
                }
                if (playerAt >= 0.0f) {
                    if (hurts) {
                        m_playerHits.push_back(
                            {shot.kind, shot.position + heading * playerAt, heading, damage});
                    } else {
                        burstOn(shot.position + heading * playerAt);
                    }
                    m_shots[i] = m_shots.back();
                    m_shots.pop_back();
                    continue;
                }
            }
        }

        if (blocked.hit) {
            shot.position = limit + glm::vec3{blocked.normal} * kEmbedBackoff;
            shot.velocity = glm::vec3{0.0f};
            shot.shake = kShakeSeconds;
            shot.landed = true;
            // **The block, not where the shot came to rest.** The two differ by
            // one cell on every face but the top; see the release test at the
            // head of this loop.
            shot.landedIn = blocked.block;
            if (species.reportsImpact) {
                m_landings.push_back({shot.kind, shot.position, blocked.normal, shot.payload});
            }
            if (!species.sticksInGround) {
                m_shots[i] = m_shots.back();
                m_shots.pop_back();
            }
            continue;
        }

        // Position, then drag, then gravity, and **one order for every species**
        // - which is what the reference itself does, since a Bedrock entity's
        // `minecraft:projectile` component carries constants and nothing else.
        //
        // > The wiki's `Entity#Motion` table lists thrown items under
        // > "Acceleration, Drag, Position", which would put an egg's terminal
        // > speed at 2.97 rather than the 3.00 this order gives. **That table is
        // > Java's**, and it is checkable rather than assumed: it puts an ender
        // > pearl on the thrown-item family's 0.03 gravity and 0.99 drag, where
        // > `ender_pearl.json` publishes 0.025 with *both* inertias overridden
        // > to 1 - and its own footnote dates the order change to *Java
        // > Edition* 1.21.2. Bedrock publishes no order, so moving this needs
        // > Bedrock evidence rather than that table.
        shot.position = target;

        const BlockId inCell = world.blockAt(static_cast<int>(std::floor(shot.position.x)),
                                             static_cast<int>(std::floor(shot.position.y)),
                                             static_cast<int>(std::floor(shot.position.z)));
        shot.velocity *= isFluid(inCell) ? species.liquidInertia : species.inertia;
        shot.velocity.y -= species.gravity;

        const float speed = glm::length(shot.velocity);
        if (speed > 1e-5f) {
            shot.heading = shot.velocity / speed;
        }
    }
}

std::vector<Projectiles::Collectable> Projectiles::collectable(const glm::vec3& playerFeet) const {
    std::vector<Collectable> found;
    const glm::vec3 middle{playerFeet.x, playerFeet.y + 0.9f, playerFeet.z};
    for (std::size_t i = 0; i < m_shots.size(); ++i) {
        const Shot& shot = m_shots[i];
        if (!shot.landed || !shot.collectable) {
            continue;
        }
        if (glm::length(middle - shot.position) < kCollectRadius) {
            found.push_back({i, projectileInfo(shot.kind).item, 1});
        }
    }
    return found;
}

void Projectiles::remove(std::size_t index) {
    if (index >= m_shots.size()) {
        return;
    }
    m_shots[index] = m_shots.back();
    m_shots.pop_back();
}

std::vector<Projectiles::Landing> Projectiles::takeLandings() {
    std::vector<Landing> taken;
    taken.swap(m_landings);
    return taken;
}

std::vector<Projectiles::PlayerHit> Projectiles::takePlayerHits() {
    std::vector<PlayerHit> taken;
    taken.swap(m_playerHits);
    return taken;
}

std::vector<Projectiles::CreatureStrike> Projectiles::takeCreatureStrikes() {
    std::vector<CreatureStrike> taken;
    taken.swap(m_creatureStrikes);
    return taken;
}

engine::MeshData Projectiles::buildMesh(const World& world, const SpriteMask& sprites,
                                        const glm::vec3& eye, const DrawRange& range) const {
    engine::MeshData mesh;

    // How far into the tick that has not happened yet the frame is. Blending by
    // it draws the *previous* tick's position advanced by exactly that much,
    // which is one tick of latency and completely invisible - where drawing on
    // the tick alone shows twenty positions a second on a screen doing 120.
    const float blend = std::clamp(m_accumulator / kTickSeconds, 0.0f, 1.0f);

    for (const Shot& shot : m_shots) {
        const glm::vec3 drawnAt = glm::mix(shot.previousPosition, shot.position, blend);
        if (!range.contains(drawnAt)) {
            continue;
        }
        const int lx = static_cast<int>(std::floor(drawnAt.x));
        const int ly = static_cast<int>(std::floor(drawnAt.y));
        const int lz = static_cast<int>(std::floor(drawnAt.z));
        const float sky = static_cast<float>(world.skyLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);
        const float blockLight =
            static_cast<float>(world.blockLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);

        // A thrown pearl or egg is **the same solid object it would be lying on
        // the floor**: its sprite extruded a texel deep with a wall on every
        // boundary, built by the one function a dropped item uses. Two crossed
        // flat quads read as two pearls passing through each other, which is
        // what they were. Billboarded, because the reference's thrown-item
        // renderer takes the camera's orientation outright.
        if (shot.kind != ProjectileKind::Arrow) {
            const int layer = itemTextureLayer(projectileInfo(shot.kind).item);
            if (layer < 0) {
                continue;
            }
            glm::vec3 toEye = eye - drawnAt;
            const float distance = glm::length(toEye);
            toEye = distance > 1e-4f ? toEye / distance : glm::vec3{0.0f, 0.0f, 1.0f};
            // Any axis not parallel to the view will do; the second candidate
            // only ever matters when looking straight up or down.
            const glm::vec3 aside =
                std::abs(toEye.y) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
            const glm::vec3 right = glm::normalize(glm::cross(aside, toEye)) * kThrownHalf;
            const glm::vec3 up = glm::normalize(glm::cross(toEye, right)) * kThrownHalf;
            appendSpriteModel(mesh, sprites, layer, drawnAt, right, up, toEye * kThrownHalf, sky,
                              blockLight);
            continue;
        }

        // A sine that decays to nothing over the shake, on pitch only, exactly
        // as the reference's `shake_power` does. It is most of what makes an
        // arrow thudding into a wall read as an impact rather than a stop.
        glm::vec3 forward = glm::mix(shot.previousHeading, shot.heading, blend);
        if (glm::length(forward) < 1e-5f) {
            forward = shot.heading;
        }
        forward = glm::normalize(forward);
        if (shot.shake > 0.0f) {
            const float wobble = -std::sin(shot.shake * 10.0f) * shot.shake * 0.6f;
            forward = glm::normalize(forward + glm::vec3{0.0f, wobble, 0.0f});
        }

        // Any two perpendiculars will do: the model is two flat quads crossed
        // at a right angle through the shaft, and it has no other orientation
        // to keep - the reference rolls both 45 degrees purely so the pair
        // reads as an X rather than as a plus.
        const glm::vec3 aside = std::abs(forward.y) > 0.9f ? glm::vec3{1.0f, 0.0f, 0.0f}
                                                           : glm::vec3{0.0f, 1.0f, 0.0f};
        const glm::vec3 a = glm::normalize(glm::cross(forward, aside));
        const glm::vec3 b = glm::normalize(glm::cross(forward, a));

        // The reference's own numbers, scaled by the 0.7/0.7/0.9 its animation
        // applies: a body 16 texels long running from 3 ahead of the pivot to
        // 13 behind it, and 5 texels across.
        constexpr float kUnit = 1.0f / 16.0f;
        const float ahead = 3.0f * kUnit * 0.9f;
        const float behind = 13.0f * kUnit * 0.9f;
        const float half = 2.5f * kUnit * 0.7f;

        const glm::vec3 tip = drawnAt + forward * ahead;
        const glm::vec3 tail = drawnAt - forward * behind;

        const auto quad = [&](const glm::vec3& across, float u0, float u1, float v0, float v1,
                              const glm::vec3& from, const glm::vec3& to) {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            const glm::vec3 corners[4]{from - across, to - across, to + across, from + across};
            const glm::vec2 uvs[4]{{u0, v1}, {u1, v1}, {u1, v0}, {u0, v0}};
            for (int c = 0; c < 4; ++c) {
                mesh.vertices.push_back(engine::Vertex{{corners[c].x, corners[c].y, corners[c].z},
                                                       engine::packVertexColor(sky, blockLight, 1.0f, 1.0f),
                                                       {uvs[c].x, uvs[c].y},
                                                       static_cast<float>(kArrowEntitySprite),
                                                       engine::kVertexSurfaceDefault});
            }
            // Both windings, because a flat quad is seen from either side.
            mesh.indices.insert(mesh.indices.end(),
                                {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                 base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
        };

        // Shaft and fletching together are one 16x5 strip in the corner of the
        // sheet, painted rather than modelled - so the two crossed quads *are*
        // the whole arrow.
        constexpr float kStripV = 5.0f / 16.0f;
        quad(a * half, 0.0f, 1.0f, 0.0f, kStripV, tip, tail);
        quad(b * half, 0.0f, 1.0f, 0.0f, kStripV, tip, tail);

        // The nock, a 5x5 square seen end on at the tail.
        constexpr float kNockU = 5.0f / 16.0f;
        const glm::vec3 nock = tail + forward * kUnit;
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const glm::vec3 corners[4]{nock - a * half - b * half, nock + a * half - b * half,
                                   nock + a * half + b * half, nock - a * half + b * half};
        const glm::vec2 uvs[4]{{0.0f, kNockU * 2.0f}, {kNockU, kNockU * 2.0f}, {kNockU, kStripV},
                               {0.0f, kStripV}};
        for (int c = 0; c < 4; ++c) {
            mesh.vertices.push_back(engine::Vertex{{corners[c].x, corners[c].y, corners[c].z},
                                                   engine::packVertexColor(sky, blockLight, 0.9f, 1.0f),
                                                   {uvs[c].x, uvs[c].y},
                                                   static_cast<float>(kArrowEntitySprite),
                                                   engine::kVertexSurfaceDefault});
        }
        mesh.indices.insert(mesh.indices.end(),
                            {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                             base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
    }

    return mesh;
}

} // namespace game
