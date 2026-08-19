#include "world/Explosion.hpp"

#include "world/Raycast.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace game {
namespace {

/// The march's two fixed costs. `0.22500001` rather than `0.225` is the
/// reference's own literal, nudged off the boundary to dodge a float edge case;
/// divided by the 0.3 step it means a ray loses 0.75 intensity per block of
/// open air travelled.
constexpr float kStep = 0.3f;
constexpr float kDecayPerStep = 0.22500001f;

/// A ray samples the same cell about three times over, because the step is 0.3
/// and a cell is 1. Paying the resistance every sample is deliberate and is why
/// the reference notes that blasts inside a non-full block are heavily damped.
constexpr float kResistanceOffset = 0.3f;
constexpr float kResistanceScale = 0.3f;

/// Sample points across an entity box are spaced `1/(2·size + 1)` apart.
constexpr float kExposureSpacing = 2.0f;

/// The published damage curve is Java's. Bedrock hits measurably softer — 27.5
/// point-blank against Java's 43 on Normal — and does not publish a formula, so
/// the curve is scaled to land on the edition we follow.
constexpr float kBedrockDamageScale = 27.5f / 43.0f;

std::uint32_t nextRandom(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float randomUnit(std::uint32_t& state) {
    return static_cast<float>(nextRandom(state) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

/// The 1352 ray directions, built once.
///
/// Points on the *surface* of a 16³ index grid, normalised. Uniform on a cube
/// rather than on a sphere, so rays crowd toward the six face centres — that
/// unevenness is the reference's and is part of why craters look the way they
/// do.
const std::vector<glm::vec3>& rayDirections() {
    static const std::vector<glm::vec3> directions = [] {
        std::vector<glm::vec3> built;
        built.reserve(1352);
        for (int j = 0; j < 16; ++j) {
            for (int k = 0; k < 16; ++k) {
                for (int l = 0; l < 16; ++l) {
                    if (j != 0 && j != 15 && k != 0 && k != 15 && l != 0 && l != 15) {
                        continue;
                    }
                    glm::vec3 direction{static_cast<float>(j) / 15.0f * 2.0f - 1.0f,
                                        static_cast<float>(k) / 15.0f * 2.0f - 1.0f,
                                        static_cast<float>(l) / 15.0f * 2.0f - 1.0f};
                    built.push_back(glm::normalize(direction));
                }
            }
        }
        return built;
    }();
    return directions;
}

/// **Exact packing, never a hash** — the same standard `Pathfinder.cpp`'s
/// `cellKey` is held to, and for the same reason: a collision here folds two
/// different cells into one and a block quietly survives a blast that took it.
///
/// This used to be a shift-and-XOR, where x lost its top ten bits off the end
/// and the y and z fields overlapped at bits 21-31. It could not collide at
/// crater scale, so it was safe *by accident* — two near-identical cell keys
/// held to two different standards is exactly the shape that costs a session.
/// Twenty-one bits of x and z reach about a million blocks either side of the
/// origin and twelve of y cover a world 96 tall many times over.
std::uint64_t packCell(const glm::ivec3& cell) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.x) & 0x1FFFFFu) << 33) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.z) & 0x1FFFFFu) << 12) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.y) & 0xFFFu));
}

/// True if anything solid stands between the two points.
///
/// **Collision geometry, not selection geometry**, and that one word is the
/// whole of a finding. `raycast` asks `worldSelectionBoxes`, which hands a
/// plant a 0.75-wide column *precisely so the crosshair can pick it out* — so a
/// tuft of grass, a flower, a torch, a rail, a button, a sign, a ladder or a
/// vine all sheltered you from a blast that walks straight through them, and
/// exposure feeds a squared damage curve. `sweepBlocks` asks
/// `worldCollisionBoxes` instead, which is the question the reference's own
/// exposure test asks and the one the projectile system next door already asks
/// out of this same header.
///
/// Water still shelters nothing, because water collides with nothing — the
/// reference's rule, and now it really does fall out for free rather than by
/// coincidence.
bool shielded(const World& world, const glm::vec3& from, const glm::vec3& to) {
    // A zero-length segment is a sample point sitting on the centre, which
    // `sweepBlocks` already reports as unobstructed.
    return sweepBlocks(world, from, to).hit;
}

} // namespace

// `blastResistance` is a `constexpr` table in the header rather than a function
// here. It is a per-block table like `blockName` and `miningRow`, and putting
// it where a `static_assert` can walk it is what lets the sweep beside it prove
// the ordering rather than hope for it.

std::vector<glm::ivec3> explosionBlocks(const World& world, const glm::vec3& centre, float power,
                                        std::uint32_t seed) {
    std::vector<glm::ivec3> destroyed;
    std::unordered_set<std::uint64_t> seen;
    std::uint32_t random = seed | 1u;

    for (const glm::vec3& direction : rayDirections()) {
        // Rolled fresh per ray, not once per blast. That is what stops the
        // crater being a clean sphere.
        float intensity = power * (0.7f + randomUnit(random) * 0.6f);
        glm::vec3 position = centre;

        while (intensity > 0.0f) {
            const glm::ivec3 cell{static_cast<int>(std::floor(position.x)),
                                  static_cast<int>(std::floor(position.y)),
                                  static_cast<int>(std::floor(position.z))};
            const BlockId block = world.blockAt(cell.x, cell.y, cell.z);
            if (block != BlockId::Air) {
                intensity -= (blastResistance(block) + kResistanceOffset) * kResistanceScale;
                // Taken only if the ray survived paying for it, so whatever
                // finally stops a ray is left standing.
                if (intensity > 0.0f) {
                    if (seen.insert(packCell(cell)).second) {
                        destroyed.push_back(cell);
                    }
                }
            }
            position += direction * kStep;
            intensity -= kDecayPerStep;
        }
    }
    return destroyed;
}

float explosionExposure(const World& world, const glm::vec3& centre, const Aabb& box) {
    const glm::vec3 size = box.max - box.min;

    // **Counted, not accumulated.** The reference takes
    // `ceil(2 x size + 1)` sample planes per axis, spaced `1 / (2 x size + 1)`
    // apart across the box. Walking `u` from 0 while `u <= 1` is the same thing
    // for every entity in the game today - a player, a creeper, a cow and a
    // chicken all measure identically either way - but it is the same thing by
    // luck, and it goes wrong in two directions at once.
    //
    // Where `2 x size + 1` lands on a whole number, `u` reaches exactly 1.0 and
    // the loop takes **one extra plane per axis**: a frog is 0.5 by 0.5, so it
    // was sampled 27 times against the reference's 8, and a polar bear is
    // exactly one block wide. And because `u += spacing` accumulates rounding,
    // whether that plane appears at all depends on which way the last addition
    // rounded - a 1/3 spacing overshoots and drops it, a 1/2 spacing lands on
    // it and keeps it. **A count cannot do either.**
    const auto planes = [](float extent) {
        return static_cast<int>(std::ceil(2.0f * extent + 1.0f));
    };
    const glm::ivec3 count{planes(size.x), planes(size.y), planes(size.z)};
    const glm::vec3 spacing{1.0f / (kExposureSpacing * size.x + 1.0f),
                            1.0f / (kExposureSpacing * size.y + 1.0f),
                            1.0f / (kExposureSpacing * size.z + 1.0f)};

    int total = 0;
    int clear = 0;
    for (int i = 0; i < count.x; ++i) {
        for (int j = 0; j < count.y; ++j) {
            for (int k = 0; k < count.z; ++k) {
                const glm::vec3 sample{box.min.x + size.x * (static_cast<float>(i) * spacing.x),
                                       box.min.y + size.y * (static_cast<float>(j) * spacing.y),
                                       box.min.z + size.z * (static_cast<float>(k) * spacing.z)};
                ++total;
                if (!shielded(world, centre, sample)) {
                    ++clear;
                }
            }
        }
    }
    return total == 0 ? 0.0f : static_cast<float>(clear) / static_cast<float>(total);
}

float explosionImpact(const glm::vec3& centre, float power, const glm::vec3& feet, float exposure) {
    // **The same reach `withinBlast` publishes, read out of it rather than
    // written again.** A second copy of `2 × power` here is how the pre-filter
    // and the impact drift apart, and then something takes damage the filter
    // said it could not.
    if (!withinBlast(centre, power, feet)) {
        return 0.0f;
    }
    const float distance = glm::length(feet - centre);
    return (1.0f - distance / (2.0f * power)) * exposure;
}

int explosionDamage(float power, float impact) {
    // **Zero is a real answer here, not a reason to leave.** A fully sheltered
    // entity inside twice the power produces an impact of exactly zero, and the
    // reference still charges it one point - the trailing `+ 1` *is* that
    // floor, and guarding against `impact <= 0` was the one edit that threw it
    // away. Only a negative impact returns nothing, because the curve turns
    // downward below zero; `explosionImpact` clamps to zero outside the reach,
    // so no caller can produce one.
    if (impact < 0.0f) {
        return 0;
    }
    const float raw = 7.0f * power * (impact * impact + impact) + 1.0f;
    return static_cast<int>(std::round(raw * kBedrockDamageScale));
}

} // namespace game
