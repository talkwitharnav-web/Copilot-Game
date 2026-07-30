#pragma once

#include <glm/glm.hpp>

namespace game {

class World;

/// Where a look-ray met the world.
struct RaycastHit {
    bool hit = false;
    /// The solid block that was struck.
    glm::ivec3 block{0};
    /// The empty cell the ray was in immediately before, which is where a newly
    /// placed block goes.
    glm::ivec3 adjacent{0};
};

/// Walks the ray cell by cell and returns the first solid block within range.
///
/// Steps block to block rather than sampling at fixed intervals, so it cannot
/// skip a block no matter how the ray is angled and costs the same regardless
/// of precision.
RaycastHit raycast(const World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance);

} // namespace game
