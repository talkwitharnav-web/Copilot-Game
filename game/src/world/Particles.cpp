#include "world/Particles.hpp"

#include "world/Survival.hpp"
#include "world/World.hpp"

#include <engine/render/MeshData.hpp>
#include <engine/render/Vertex.hpp>

#include <algorithm>
#include <cmath>

namespace game {
namespace {

/// Slower than a falling body, which is what the reference does too: a chip of
/// stone is light enough that air resistance matters at this scale, and at full
/// gravity the shower is over before the eye registers it.
constexpr float kParticleGravity = 12.0f;

/// Speed lost per second to drag, as a fraction. Applied per axis so a
/// horizontal fling slows without the fall being cancelled.
constexpr float kDrag = 3.2f;

/// How far into its life a particle starts fading.
constexpr float kFadeFrom = 0.65f;

/// How often the emitters near the player are looked for, and how far.
constexpr float kEmitterScanSeconds = 0.5f;
constexpr int kEmitterReach = 8;
constexpr int kEmitterHeight = 6;

/// Past this the nearest are kept and the rest ignored. A wall of torches is a
/// thing players build, and it must not become a wall of particles.
constexpr std::size_t kMaxEmitters = 48;

/// A torch's flame sits near the top of its own cell.
constexpr float kTorchFlameHeight = 0.62f;

/// How grey smoke is. Drawn from the white sprite, so this is the whole of its
/// colour.
constexpr float kSmokeShade = 0.34f;

} // namespace

float Particles::roll() {
    m_random = m_random * 1664525u + 1013904223u;
    return static_cast<float>((m_random >> 8) & 0xffffffu) / 16777215.0f;
}

Particle& Particles::emplace() {
    if (m_particles.size() >= kMaxParticles) {
        // The oldest goes, not the newest: whatever just happened is what the
        // player is looking at.
        m_particles.erase(m_particles.begin());
    }
    return m_particles.emplace_back();
}

void Particles::spawnBlockBreak(const glm::ivec3& block, BlockId id, int count) {
    if (id == BlockId::Air) {
        return;
    }
    const float layer = blockTextureLayer(id, BlockFace::Side);
    for (int i = 0; i < count; ++i) {
        Particle& p = emplace();
        p.position = glm::vec3{block} + glm::vec3{roll(), roll(), roll()};
        // Thrown outward from the block's centre, so the shower opens up rather
        // than dropping straight down as one clump.
        const glm::vec3 outward = p.position - (glm::vec3{block} + glm::vec3{0.5f});
        p.velocity = outward * 4.0f + glm::vec3{0.0f, 1.5f + roll() * 1.5f, 0.0f};
        p.age = 0.0f;
        p.life = 0.6f + roll() * 0.5f;
        p.size = 0.045f + roll() * 0.025f;
        p.layer = layer;
        // A quarter of the sprite, chosen at random, so no two chips carry the
        // same pixels.
        p.uvOrigin = glm::vec2{std::floor(roll() * 2.0f) * 0.5f, std::floor(roll() * 2.0f) * 0.5f};
        p.gravity = 1.0f;
        p.fades = false;
    }
}

void Particles::spawnSplash(const glm::vec3& at) {
    Particle& p = emplace();
    p.position = at;
    p.velocity = glm::vec3{(roll() - 0.5f) * 0.6f, 1.4f + roll() * 0.9f, (roll() - 0.5f) * 0.6f};
    p.age = 0.0f;
    p.life = 0.28f + roll() * 0.16f;
    p.size = 0.022f;
    p.layer = static_cast<float>(TextureLayer::Water);
    p.uvOrigin = glm::vec2{std::floor(roll() * 2.0f) * 0.5f, std::floor(roll() * 2.0f) * 0.5f};
    p.gravity = 1.0f;
    p.fades = true;
}

void Particles::spawnFootstep(const glm::vec3& at, BlockId under, int count) {
    if (under == BlockId::Air) {
        return;
    }
    const float layer = blockTextureLayer(under, BlockFace::Top);
    for (int i = 0; i < count; ++i) {
        Particle& p = emplace();
        p.position = at + glm::vec3{(roll() - 0.5f) * 0.6f, 0.05f, (roll() - 0.5f) * 0.6f};
        p.velocity = glm::vec3{(roll() - 0.5f) * 1.6f, roll() * 0.9f, (roll() - 0.5f) * 1.6f};
        p.age = 0.0f;
        p.life = 0.35f + roll() * 0.25f;
        p.size = 0.05f;
        p.layer = layer;
        p.uvOrigin = glm::vec2{std::floor(roll() * 2.0f) * 0.5f, std::floor(roll() * 2.0f) * 0.5f};
        p.gravity = 0.6f;
        p.fades = true;
    }
}

void Particles::spawnSmoke(const glm::vec3& at, float size, float spread, float life) {
    Particle& p = emplace();
    p.position = at + glm::vec3{(roll() - 0.5f) * spread, roll() * spread * 0.5f,
                               (roll() - 0.5f) * spread};
    p.velocity = glm::vec3{(roll() - 0.5f) * 0.16f, 0.30f + roll() * 0.25f, (roll() - 0.5f) * 0.16f};
    p.age = 0.0f;
    p.life = life * (0.75f + roll() * 0.5f);
    p.size = size * (0.8f + roll() * 0.4f);
    p.layer = static_cast<float>(TextureLayer::White);
    p.uvOrigin = glm::vec2{0.0f};
    // Rises, and slowly - a puff that shoots upward reads as a rocket.
    p.gravity = -0.045f;
    p.shade = kSmokeShade * (0.8f + roll() * 0.4f);
    p.drift = 0.6f;
    p.fades = true;
}

void Particles::spawnFlame(const glm::vec3& at, float spread) {
    Particle& p = emplace();
    p.position = at + glm::vec3{(roll() - 0.5f) * spread, (roll() - 0.3f) * spread,
                               (roll() - 0.5f) * spread};
    p.velocity = glm::vec3{(roll() - 0.5f) * 0.10f, 0.16f + roll() * 0.18f, (roll() - 0.5f) * 0.10f};
    p.age = 0.0f;
    p.life = 0.34f + roll() * 0.28f;
    p.size = 0.022f + roll() * 0.016f;
    p.layer = blockTextureLayer(BlockId::Fire, BlockFace::Side);
    p.uvOrigin = glm::vec2{std::floor(roll() * 2.0f) * 0.5f, std::floor(roll() * 2.0f) * 0.5f};
    p.gravity = -0.02f;
    // Its own light. A flame lit by the cell it drifted into is not a flame.
    p.glow = 1.0f;
    p.drift = 0.15f;
    p.fades = true;
}

void Particles::spawnEat(const glm::vec3& mouth, const glm::vec3& facing, float layer, int count) {
    for (int i = 0; i < count; ++i) {
        Particle& p = emplace();
        p.position = mouth + glm::vec3{(roll() - 0.5f) * 0.12f, (roll() - 0.5f) * 0.12f,
                                       (roll() - 0.5f) * 0.12f};
        p.velocity = facing * (1.1f + roll() * 0.9f) +
                     glm::vec3{(roll() - 0.5f) * 0.9f, roll() * 0.5f, (roll() - 0.5f) * 0.9f};
        p.age = 0.0f;
        p.life = 0.35f + roll() * 0.3f;
        p.size = 0.020f + roll() * 0.014f;
        p.layer = layer;
        p.uvOrigin = glm::vec2{std::floor(roll() * 2.0f) * 0.5f, std::floor(roll() * 2.0f) * 0.5f};
        p.gravity = 1.0f;
        p.fades = true;
    }
}

void Particles::spawnExplosion(const glm::vec3& centre, float power) {
    const int puffs = std::min(70, 16 + static_cast<int>(power * 9.0f));
    const float reach = 0.55f + power * 0.32f;
    for (int i = 0; i < puffs; ++i) {
        Particle& p = emplace();
        // Through the volume rather than off its shell: a hollow ball of smoke
        // reads as a ring the moment it thins out.
        const glm::vec3 offset{roll() - 0.5f, roll() - 0.5f, roll() - 0.5f};
        p.position = centre + offset * (reach * 2.0f);
        p.velocity = offset * (2.4f + power) + glm::vec3{0.0f, 0.5f + roll(), 0.0f};
        p.age = 0.0f;
        p.life = 0.7f + roll() * 0.8f;
        p.size = 0.10f + roll() * 0.16f;
        p.layer = static_cast<float>(TextureLayer::White);
        p.uvOrigin = glm::vec2{0.0f};
        p.gravity = -0.03f;
        // Paler than a torch's smoke and unevenly so, which is what stops the
        // ball reading as one solid object.
        p.shade = 0.45f + roll() * 0.45f;
        p.drift = 0.35f;
        p.fades = true;
    }
    for (int i = 0; i < puffs / 4; ++i) {
        spawnFlame(centre, reach);
    }
}

void Particles::emitAmbient(const World& world, const glm::vec3& eye, float deltaSeconds) {
    m_emitterScan -= deltaSeconds;
    if (m_emitterScan <= 0.0f) {
        m_emitterScan = kEmitterScanSeconds;
        m_emitters.clear();
        const auto ex = static_cast<int>(std::floor(eye.x));
        const auto ey = static_cast<int>(std::floor(eye.y));
        const auto ez = static_cast<int>(std::floor(eye.z));
        // Wrapped so hitting the cap leaves the scan without leaving the
        // function - the emitters already found still have to be drawn.
        [&] {
            for (int y = ey - kEmitterHeight; y <= ey + kEmitterHeight; ++y) {
                for (int z = ez - kEmitterReach; z <= ez + kEmitterReach; ++z) {
                    for (int x = ex - kEmitterReach; x <= ex + kEmitterReach; ++x) {
                        const BlockId id = world.blockAt(x, y, z);
                        if (id == BlockId::Torch || id == BlockId::SoulTorch ||
                            id == BlockId::Fire || isFurnaceLit(id) || isLava(id)) {
                            if (m_emitters.size() >= kMaxEmitters) {
                                return;
                            }
                            m_emitters.push_back({x, y, z});
                        }
                    }
                }
            }
        }();
    }

    for (const glm::ivec3& cell : m_emitters) {
        const BlockId id = world.blockAt(cell.x, cell.y, cell.z);
        const glm::vec3 centre = glm::vec3{cell} + glm::vec3{0.5f, 0.0f, 0.5f};

        // Rates per second, per emitter. A torch is the one there are most of,
        // so it is also the quietest.
        float flames = 0.0f;
        float smoke = 0.0f;
        float height = kTorchFlameHeight;
        float spread = 0.06f;
        if (id == BlockId::Torch || id == BlockId::SoulTorch) {
            flames = 3.0f;
            smoke = 1.1f;
        } else if (id == BlockId::Fire) {
            flames = 9.0f;
            smoke = 3.5f;
            height = 0.45f;
            spread = 0.34f;
        } else if (isFurnaceLit(id)) {
            // Out of the top, like a chimney - the mouth is where the light is,
            // but the smoke has to leave somewhere it can.
            smoke = 2.2f;
            height = 1.02f;
            spread = 0.22f;
        } else if (isLava(id)) {
            smoke = 0.30f;
            height = 0.95f;
            spread = 0.5f;
        }

        if (flames > 0.0f && roll() < flames * deltaSeconds) {
            spawnFlame(centre + glm::vec3{0.0f, height, 0.0f}, spread);
        }
        if (smoke > 0.0f && roll() < smoke * deltaSeconds) {
            spawnSmoke(centre + glm::vec3{0.0f, height + 0.08f, 0.0f}, 0.028f, spread, 1.4f);
        }
    }
}

void Particles::update(const World& world, float deltaSeconds, const glm::vec3& wind) {
    for (Particle& p : m_particles) {
        p.age += deltaSeconds;
        if (p.age >= p.life) {
            continue;
        }

        p.velocity.y -= kParticleGravity * p.gravity * deltaSeconds;
        if (p.drift > 0.0f) {
            p.velocity += wind * (p.drift * deltaSeconds);
        }
        const float drag = std::max(0.0f, 1.0f - kDrag * deltaSeconds);
        p.velocity *= drag;

        const glm::vec3 step = p.velocity * deltaSeconds;
        // One axis at a time, so a chip that hits a wall slides along it rather
        // than stopping dead in mid-air.
        const auto solidAt = [&world](const glm::vec3& at) {
            return world.isSolid(static_cast<int>(std::floor(at.x)), static_cast<int>(std::floor(at.y)),
                                 static_cast<int>(std::floor(at.z)));
        };
        glm::vec3 moved = p.position;
        moved.x += step.x;
        if (solidAt(moved)) {
            moved.x = p.position.x;
            p.velocity.x = 0.0f;
        }
        moved.z += step.z;
        if (solidAt(moved)) {
            moved.z = p.position.z;
            p.velocity.z = 0.0f;
        }
        moved.y += step.y;
        if (solidAt(moved)) {
            moved.y = p.position.y;
            // Rests rather than bounces. A bouncing chip reads as a dropped item
            // and there is already a system that looks like that.
            p.velocity = glm::vec3{0.0f};
        }
        p.position = moved;
    }

    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
                                     [](const Particle& p) { return p.age >= p.life; }),
                      m_particles.end());
}

engine::MeshData Particles::buildMesh(const World& world, const glm::vec3& right,
                                      const glm::vec3& up) const {
    engine::MeshData mesh;
    if (m_particles.empty()) {
        return mesh;
    }

    mesh.vertices.reserve(m_particles.size() * 4);
    mesh.indices.reserve(m_particles.size() * 6);

    for (const Particle& p : m_particles) {
        float scale = 1.0f;
        if (p.fades) {
            const float t = p.age / p.life;
            // **Shrunk, not faded.** These go through the cutout path, where a
            // fragment is either fully present or thrown away - so an alpha ramp
            // would do nothing until it crossed the threshold and then blink the
            // whole particle out. Shrinking reads as the same thing and costs
            // nothing.
            scale = t < kFadeFrom ? 1.0f : 1.0f - (t - kFadeFrom) / (1.0f - kFadeFrom);
        }
        if (scale <= 0.05f) {
            continue;
        }

        const auto bx = static_cast<int>(std::floor(p.position.x));
        const auto by = static_cast<int>(std::floor(p.position.y));
        const auto bz = static_cast<int>(std::floor(p.position.z));
        const float sky = std::max(
            p.glow, static_cast<float>(world.skyLightAt(bx, by, bz)) / static_cast<float>(kMaxLight));
        const float block = std::max(
            p.glow, static_cast<float>(world.blockLightAt(bx, by, bz)) / static_cast<float>(kMaxLight));

        const glm::vec3 r = right * (p.size * scale);
        const glm::vec3 u = up * (p.size * scale);
        const glm::vec3 corners[4] = {p.position - r - u, p.position + r - u, p.position + r + u,
                                      p.position - r + u};
        // Half the sprite in each axis, which is the quarter the particle
        // carries.
        const glm::vec2 uvs[4] = {p.uvOrigin + glm::vec2{0.0f, 0.5f},
                                  p.uvOrigin + glm::vec2{0.5f, 0.5f},
                                  p.uvOrigin + glm::vec2{0.5f, 0.0f}, p.uvOrigin};

        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int i = 0; i < 4; ++i) {
            mesh.vertices.push_back(engine::Vertex{
                {corners[i].x, corners[i].y, corners[i].z},
                // Sky, block and a face shade - the same three channels world
                // geometry uses, so the shared lit path needs no case for
                // particles at all.
                engine::packVertexColor(sky, block, p.shade, 1.0f),
                {uvs[i].x, uvs[i].y},
                p.layer,
                engine::kVertexSurfaceDefault});
        }
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    return mesh;
}

} // namespace game
