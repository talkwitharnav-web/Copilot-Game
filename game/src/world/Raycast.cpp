#include "world/Raycast.hpp"

#include "world/World.hpp"

#include <cmath>
#include <limits>

namespace game {

RaycastHit raycast(const World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    RaycastHit result;

    const float length = glm::length(direction);
    if (length <= 0.0f) {
        return result;
    }
    const glm::vec3 dir = direction / length;

    glm::ivec3 cell{static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)),
                    static_cast<int>(std::floor(origin.z))};

    if (world.isSolid(cell.x, cell.y, cell.z)) {
        result.hit = true;
        result.block = cell;
        result.adjacent = cell;
        return result;
    }

    constexpr float infinity = std::numeric_limits<float>::infinity();

    glm::ivec3 step{0};
    glm::vec3 tMax{infinity};
    glm::vec3 tDelta{infinity};

    for (int axis = 0; axis < 3; ++axis) {
        if (dir[axis] > 0.0f) {
            step[axis] = 1;
            tMax[axis] = (static_cast<float>(cell[axis] + 1) - origin[axis]) / dir[axis];
            tDelta[axis] = 1.0f / dir[axis];
        } else if (dir[axis] < 0.0f) {
            step[axis] = -1;
            tMax[axis] = (static_cast<float>(cell[axis]) - origin[axis]) / dir[axis];
            tDelta[axis] = -1.0f / dir[axis];
        }
    }

    while (true) {
        // Cross whichever cell boundary is nearest along the ray.
        int axis = 0;
        if (tMax.y < tMax[axis]) {
            axis = 1;
        }
        if (tMax.z < tMax[axis]) {
            axis = 2;
        }

        if (tMax[axis] > maxDistance) {
            return result;
        }

        const glm::ivec3 previous = cell;
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];

        if (world.isSolid(cell.x, cell.y, cell.z)) {
            result.hit = true;
            result.block = cell;
            result.adjacent = previous;
            return result;
        }
    }
}

} // namespace game
