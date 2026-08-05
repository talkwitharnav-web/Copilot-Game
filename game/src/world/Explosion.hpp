#pragma once

#include "world/Block.hpp"
#include "world/Collision.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace game {

class World;

/// How well a block resists a blast.
///
/// **This is not mining hardness and must never be confused with it** — stone
/// is 1.5 to a pickaxe and 6 to an explosion, and obsidian is 50 against 1200.
/// `Tool.hpp` owns the mining number; this owns the blast one, and neither is
/// derived from the other.
float blastResistance(BlockId block);

/// Every block a blast of this power removes.
///
/// The reference's algorithm exactly: 1352 rays toward the surface of a 16³
/// index grid, each starting at `power × [0.7, 1.3)` and advancing 0.3 blocks
/// at a time, losing a flat 0.225 per step plus `(resistance + 0.3) × 0.3` for
/// whatever it is passing through. A block is taken only if the ray still has
/// intensity left *after* paying for it, so the one that finally stops a ray
/// survives — which is what makes a crater ragged rather than spherical.
///
/// `seed` varies the per-ray roll. Two blasts at the same spot should not carve
/// the same hole.
std::vector<glm::ivec3> explosionBlocks(const World& world, const glm::vec3& centre, float power,
                                        std::uint32_t seed);

/// The fraction of a box the blast can actually see, 0 to 1.
///
/// Rays from the centre to a grid of sample points across the box; the share
/// that reach it unobstructed. Water does not shield, which is the reference's
/// behaviour and falls out for free because the ray test asks for solid blocks.
float explosionExposure(const World& world, const glm::vec3& centre, const Aabb& box);

/// The one quantity distance and shelter feed into. Damage and knockback both
/// read it, which is why it is computed once rather than twice.
float explosionImpact(const glm::vec3& centre, float power, const glm::vec3& feet, float exposure);

/// Damage from that impact. **Everything inside twice the power takes at least
/// one point even fully shielded**, which is the trailing `+ 1`.
int explosionDamage(float power, float impact);

} // namespace game
