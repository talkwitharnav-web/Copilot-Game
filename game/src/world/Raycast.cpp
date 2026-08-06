#include "world/Raycast.hpp"

#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game {
namespace {

/// Nearest entry into the geometry of the block in `cell`, as a ray parameter,
/// along with the face the ray crossed to get in.
///
/// Testing the *cell* instead is what put a placed slab beside its neighbour
/// rather than on top of it: a slab fills only half its cell, so a ray aimed at
/// its top from a distance crosses the empty upper half first and would be
/// reported as entering through the side.
bool hitsBlockGeometry(const World& world, const glm::vec3& origin, const glm::vec3& dir, const glm::ivec3& cell,
                       float maxDistance, float& tHit, glm::ivec3& normal) {
    const BlockBoxes shape = selectionBoxes(world.blockAt(cell.x, cell.y, cell.z));
    bool found = false;
    tHit = maxDistance;

    for (int i = 0; i < shape.count; ++i) {
        const BlockBox& b = shape.boxes[i];
        const glm::vec3 lo{static_cast<float>(cell.x) + b.minX, static_cast<float>(cell.y) + b.minY,
                           static_cast<float>(cell.z) + b.minZ};
        const glm::vec3 hi{static_cast<float>(cell.x) + b.maxX, static_cast<float>(cell.y) + b.maxY,
                           static_cast<float>(cell.z) + b.maxZ};

        float tEnter = 0.0f;
        float tExit = maxDistance;
        int enterAxis = -1;
        bool miss = false;

        for (int axis = 0; axis < 3 && !miss; ++axis) {
            if (std::abs(dir[axis]) < 1e-8f) {
                // Parallel to this pair of faces: either always between them or
                // never.
                miss = origin[axis] < lo[axis] || origin[axis] > hi[axis];
                continue;
            }

            float near = (lo[axis] - origin[axis]) / dir[axis];
            float far = (hi[axis] - origin[axis]) / dir[axis];
            if (near > far) {
                std::swap(near, far);
            }
            if (near > tEnter) {
                tEnter = near;
                enterAxis = axis;
            }
            tExit = std::min(tExit, far);
            miss = tEnter > tExit;
        }

        if (!miss && enterAxis >= 0 && tEnter < tHit) {
            tHit = tEnter;
            normal = glm::ivec3{0};
            // Entered against the direction of travel on that axis.
            normal[enterAxis] = dir[enterAxis] > 0.0f ? -1 : 1;
            found = true;
        }
    }
    return found;
}

} // namespace

RaycastHit raycast(const World& world, const glm::vec3& origin, const glm::vec3& direction,
                   float maxDistance, bool stopAtWater) {
    RaycastHit result;

    const float length = glm::length(direction);
    if (length <= 0.0f) {
        return result;
    }
    const glm::vec3 dir = direction / length;

    glm::ivec3 cell{static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)),
                    static_cast<int>(std::floor(origin.z))};
    // Where a bucket's water would go: the cell the ray was in before it
    // reached the surface, since water has no face to take a normal from.
    glm::ivec3 previous = cell;
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
        // A source only, never a flowing cell: scooping a stream would leave a
        // gap its own source refills a moment later, which reads as the bucket
        // having done nothing.
        if (stopAtWater && isWaterSource(world.blockAt(cell.x, cell.y, cell.z))) {
            result.hit = true;
            result.block = cell;
            result.adjacent = previous;
            return result;
        }

        float tHit = 0.0f;
        glm::ivec3 normal{0};
        if (hitsBlockGeometry(world, origin, dir, cell, maxDistance, tHit, normal)) {
            result.hit = true;
            result.block = cell;
            result.adjacent = cell + normal;
            return result;
        }

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

        previous = cell;
        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
    }
}

bool hasLineOfSight(const World& world, const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 delta = to - from;
    const float length = glm::length(delta);
    if (length < 1e-4f) {
        return true;
    }
    const glm::vec3 dir = delta / length;

    glm::ivec3 cell{static_cast<int>(std::floor(from.x)), static_cast<int>(std::floor(from.y)),
                    static_cast<int>(std::floor(from.z))};

    constexpr float infinity = std::numeric_limits<float>::infinity();
    glm::ivec3 step{0};
    glm::vec3 tMax{infinity};
    glm::vec3 tDelta{infinity};

    for (int axis = 0; axis < 3; ++axis) {
        if (dir[axis] > 0.0f) {
            step[axis] = 1;
            tMax[axis] = (static_cast<float>(cell[axis] + 1) - from[axis]) / dir[axis];
            tDelta[axis] = 1.0f / dir[axis];
        } else if (dir[axis] < 0.0f) {
            step[axis] = -1;
            tMax[axis] = (static_cast<float>(cell[axis]) - from[axis]) / dir[axis];
            tDelta[axis] = -1.0f / dir[axis];
        }
    }

    // Whole cells rather than block geometry: sight is blocked by a cube being
    // there at all, and a slab or a fence post is a gap you can see past.
    while (true) {
        int axis = 0;
        if (tMax.y < tMax[axis]) {
            axis = 1;
        }
        if (tMax.z < tMax[axis]) {
            axis = 2;
        }
        if (tMax[axis] > length) {
            return true;
        }

        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];

        if (isOpaque(world.blockAt(cell.x, cell.y, cell.z))) {
            return false;
        }
    }
}

} // namespace game
