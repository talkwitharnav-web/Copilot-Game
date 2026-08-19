#pragma once

#include "world/Block.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace engine {
struct MeshData;
}

namespace game {

class World;

/// Short-lived bits of the world: the shower a broken block throws off, the
/// splash a raindrop makes, the puff under a hard landing.
///
/// **Simulated on the CPU, not on the GPU**, and that is a decision rather than
/// a shortcut. The milestone's wording came from a vision document rather than
/// from the player, and a compute-driven system means a storage buffer, an
/// indirect draw, a compaction pass and a whole second way of getting work onto
/// the GPU - for a few hundred quads that cost microseconds to build. When
/// something needs a hundred thousand of them at once, revisit it.
///
/// Everything here is plain data in one contiguous array, which is the same
/// shape every other hot system in this project uses.
struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float age = 0.0f;
    float life = 1.0f;
    /// Half-width in blocks. The reference's break particles are an eighth of a
    /// block across.
    float size = 0.0625f;
    /// Which layer of the block texture array to sample, and which quarter of
    /// it. **A quarter, not the whole sprite** - a particle carrying an entire
    /// 16x16 texture reads as a shrunken block rather than as a chip off one.
    float layer = 0.0f;
    glm::vec2 uvOrigin{0.0f};
    /// 0 floats, 1 falls at the world's own rate. Negative rises, which is what
    /// makes smoke smoke.
    float gravity = 1.0f;
    /// How darkly the sprite is drawn, in the same channel a block face's shade
    /// rides in. **This is the only colour control a particle has** - the other
    /// two vertex channels are sky and block light - so anything that wants to
    /// be grey has to be drawn from a white sprite and darkened here.
    float shade = 1.0f;
    /// Lights itself, ignoring the cell it happens to be in. A flame that goes
    /// dark because it drifted a block away from its own torch is worse than no
    /// flame at all.
    float glow = 0.0f;
    /// How hard the wind pushes it, in the same units the foliage bend uses.
    /// Smoke leans; a chip of stone does not.
    float drift = 0.0f;
    /// Fades out over the last of its life rather than vanishing.
    bool fades = true;
};

class Particles {
public:
    /// Past this the oldest are dropped. A hard ceiling rather than a growing
    /// vector, because the one thing a particle system must never do is make a
    /// frame spike when something dramatic happens.
    static constexpr std::size_t kMaxParticles = 3000;

    /// The shower a block throws off when it breaks.
    ///
    /// `count` is spawned in full, every time. **There is no automatic scaling
    /// against the remaining budget** - a comment here used to claim there was,
    /// and a reader trusting it would size a caller's `count` on the assumption
    /// that the system would trim it. What actually happens at the cap is in
    /// `emplace`.
    void spawnBlockBreak(const glm::ivec3& block, BlockId id, int count = 24);

    /// Rain hitting a surface: a short-lived upward fleck of water.
    void spawnSplash(const glm::vec3& at);

    /// The puff under a hard landing or a sprinting step.
    void spawnFootstep(const glm::vec3& at, BlockId under, int count);

    /// A rising grey puff. Drawn from the white sprite and darkened, because a
    /// particle carries no colour of its own.
    void spawnSmoke(const glm::vec3& at, float size, float spread, float life);

    /// A lick of flame, lighting itself so it reads at night, which is when a
    /// torch is worth looking at.
    void spawnFlame(const glm::vec3& at, float spread);

    /// Crumbs off whatever is being eaten, thrown from the mouth along the
    /// player's gaze. `layer` is the food's own sprite.
    void spawnEat(const glm::vec3& mouth, const glm::vec3& facing, float layer, int count);

    /// The smoke ball a blast leaves. `power` is the explosion's own, so a
    /// charged Bramble reads as bigger without a second table.
    void spawnExplosion(const glm::vec3& centre, float power);

    /// Ambient emitters - torches, fire, lit furnaces and lava.
    ///
    /// **The emitters near the player are cached and refreshed on a timer**,
    /// because the alternative is either a full scan every frame or a random
    /// stab that almost always lands on air. A torch is a rare block; finding
    /// one costs far more than drawing its smoke.
    void emitAmbient(const World& world, const glm::vec3& eye, float deltaSeconds);

    /// Moves everything, kills what is done, and stops anything that hits the
    /// ground. Collision is a point test against the block grid - a particle has
    /// no size worth resolving, and the alternative is a swept box per fleck.
    void update(const World& world, float deltaSeconds, const glm::vec3& wind = glm::vec3{0.0f});

    /// Camera-facing quads, in world space.
    ///
    /// **Built here rather than billboarded in a vertex shader**, because the
    /// mesh is rebuilt every frame anyway and the camera is known at that
    /// moment - so it costs two cross products for the whole set instead of a
    /// matrix multiply per vertex.
    engine::MeshData buildMesh(const World& world, const glm::vec3& right,
                               const glm::vec3& up) const;

    void clear() { m_particles.clear(); }
    std::size_t count() const { return m_particles.size(); }

private:
    Particle& emplace();

    std::vector<Particle> m_particles;
    std::vector<glm::ivec3> m_emitters;
    float m_emitterScan = 0.0f;
    std::uint32_t m_random = 0x9e3779b9u;
    float roll();
};

} // namespace game
