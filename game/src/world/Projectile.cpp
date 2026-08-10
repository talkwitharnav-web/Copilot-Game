#include "world/Projectile.hpp"

#include "item/SpriteModel.hpp"
#include "world/Creature.hpp"
#include "world/Raycast.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

/// The reference runs at twenty ticks a second and publishes every projectile
/// number per tick. Running this on `deltaSeconds` instead would make the drag
/// six times too strong at 120 fps, and would throw away the one cheap way to
/// check the physics: at a fixed step the closed form
/// `v(t) = inertia^t * (v0 + g/(1-inertia)) - g/(1-inertia)` holds exactly.
constexpr float kTickSeconds = 0.05f;

/// A frame that took longer than this drops the extra rather than catching up,
/// which is what stops a hitch firing forty ticks at once.
constexpr int kMaxTicksPerFrame = 6;

/// Ticks after launch during which a shot cannot hit whoever fired it. The
/// reference's `owner_launch_immunity_ticks`, and it is a window rather than a
/// permanent exemption - a ricochet is meant to be able to come back at you.
constexpr float kOwnerImmunitySeconds = 5.0f * kTickSeconds;

/// How far back from the surface a landed shot sits, so its nose is not buried
/// in the block it struck.
constexpr float kEmbedBackoff = 0.05f;

constexpr float kShakeSeconds = 0.35f;

/// One minute, the reference's figure for an arrow lying in a block, and the
/// same cap on one that never hits anything - which the reference leaves to
/// chunk unloading and we have no equivalent of.
constexpr float kDespawnSeconds = 60.0f;

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
}};

} // namespace

// Terminal speed is `gravity / (1 - inertia)`, and it is only that if the drag
// is applied before the acceleration. The other order gives 4.95, and the
// reference publishes 5.00 for an arrow and 4.95 for a thrown potion - so this
// one number is what pins `tick`'s update order to the right one.
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

// A pearl thrown straight up rises `v^2 / 2g` blocks. The reference publishes
// forty-five for Bedrock against twenty-eight for the drag-bearing Java one, so
// this is the check that its two overridden inertias were actually carried over
// rather than left at the family default.
static_assert(kProjectiles[1].inertia == 1.0f && kProjectiles[1].liquidInertia == 1.0f);
static_assert(kProjectiles[1].power * kProjectiles[1].power / (2.0f * kProjectiles[1].gravity) > 44.9f &&
                  kProjectiles[1].power * kProjectiles[1].power / (2.0f * kProjectiles[1].gravity) < 45.1f,
              "a pearl must reach forty-five blocks thrown straight up");

const ProjectileSpecies& projectileInfo(ProjectileKind kind) {
    return kProjectiles[static_cast<std::size_t>(kind)];
}

void Projectiles::spawn(ProjectileKind kind, const glm::vec3& position, const glm::vec3& velocity,
                        bool crit, bool collectable) {
    Shot shot;
    shot.kind = kind;
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

void Projectiles::update(const World& world, Creatures& creatures, float deltaSeconds) {
    if (m_shots.empty()) {
        m_accumulator = 0.0f;
        return;
    }

    for (Shot& shot : m_shots) {
        shot.age += deltaSeconds;
        shot.shake = std::max(0.0f, shot.shake - deltaSeconds);
    }

    m_accumulator += deltaSeconds;
    int steps = 0;
    while (m_accumulator >= kTickSeconds && steps < kMaxTicksPerFrame) {
        m_accumulator -= kTickSeconds;
        ++steps;
        tick(world, creatures);
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

void Projectiles::tick(const World& world, Creatures& creatures) {
    for (std::size_t i = m_shots.size(); i-- > 0;) {
        Shot& shot = m_shots[i];
        // Where this tick started, for the frames that will be drawn before the
        // next one. Taken before every early-out, so a shot that stops moving
        // has both ends of the blend equal and simply sits still.
        shot.previousPosition = shot.position;
        shot.previousHeading = shot.heading;
        if (shot.landed) {
            // A shot whose block was mined comes loose and drops in a sharp
            // arc, which is the reference's own behaviour and costs one test.
            const glm::ivec3 under{static_cast<int>(std::floor(shot.position.x)),
                                   static_cast<int>(std::floor(shot.position.y)),
                                   static_cast<int>(std::floor(shot.position.z))};
            if (!world.isSolid(under.x, under.y - 1, under.z) &&
                collisionBoxes(world.blockAt(under.x, under.y, under.z)).count == 0) {
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
        if (reach > 1e-5f && shot.age >= kOwnerImmunitySeconds && species.damagePerSpeed > 0.0f) {
            // Damage comes from the speed it is doing **now**, so a long shot
            // and a shot that has crossed water both land softer. The reference
            // stores no damage on the arrow at all - `impact_damage.damage` is
            // zero and `power_multiplier` is the whole of it.
            const float speed = glm::length(shot.velocity);
            int damage = static_cast<int>(std::ceil(species.damagePerSpeed * speed));
            if (shot.crit) {
                // Rounded up **before** the critical roll, not after, which the
                // reference spells out with its own `ceil_pre_critical_damage`.
                m_random = m_random * 1664525u + 1013904223u;
                damage += static_cast<int>((m_random >> 16) % static_cast<unsigned>(damage / 2 + 2));
            }
            if (creatures.strike(shot.position, leg / reach, reach, std::max(1, damage))) {
                m_shots[i] = m_shots.back();
                m_shots.pop_back();
                continue;
            }
        }

        if (blocked.hit) {
            shot.position = limit + glm::vec3{blocked.normal} * kEmbedBackoff;
            shot.velocity = glm::vec3{0.0f};
            shot.shake = kShakeSeconds;
            shot.landed = true;
            if (species.reportsImpact) {
                m_landings.push_back({shot.kind, shot.position, blocked.normal});
            }
            if (!species.sticksInGround) {
                m_shots[i] = m_shots.back();
                m_shots.pop_back();
            }
            continue;
        }

        // Position, then drag, then gravity - the reference's order for an
        // arrow, and the two are distinguishable: this one gives a terminal
        // speed of `g / (1 - inertia)` = 5 blocks a tick, where dragging the
        // acceleration too would give 4.95.
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

engine::MeshData Projectiles::buildMesh(const World& world, const SpriteMask& sprites,
                                        const glm::vec3& eye) const {
    engine::MeshData mesh;

    // How far into the tick that has not happened yet the frame is. Blending by
    // it draws the *previous* tick's position advanced by exactly that much,
    // which is one tick of latency and completely invisible - where drawing on
    // the tick alone shows twenty positions a second on a screen doing 120.
    const float blend = std::clamp(m_accumulator / kTickSeconds, 0.0f, 1.0f);

    for (const Shot& shot : m_shots) {
        const glm::vec3 drawnAt = glm::mix(shot.previousPosition, shot.position, blend);
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
