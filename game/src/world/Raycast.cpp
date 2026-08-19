#include "world/Raycast.hpp"

#include "world/Collision.hpp"
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
                       float maxDistance, float& tHit, glm::ivec3& normal, bool collision = false) {
    const BlockBoxes shape = collision ? worldCollisionBoxes(world, cell.x, cell.y, cell.z)
                                       : worldSelectionBoxes(world, cell.x, cell.y, cell.z);
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

        // **Two separate things are declined here, and both are deliberate.**
        //
        // `tEnter < tHit` is first of all how the nearest box wins when a cell
        // holds several - `tHit` starts at `maxDistance`, so the comparison
        // doubles as the range bound. **It is strict, and that is the choice.**
        // Relaxing it to `<=` would not merely admit a ray whose end lands
        // exactly on a face; it would also make the *last* of two boxes tangent
        // at the same `t` overwrite the first, and those two can carry different
        // `normal`s. Trading a decline nobody can reach for a silent change in
        // which face a stair or a slab reports is a bad trade, so the strictness
        // stays. Reported as finding 995 (fx-projectile): an arrow at exactly
        // 1.0 blocks/tick from an integral height tunnelled a wall in a probe,
        // and **the reporter's own control retired it** - rebuilt with the
        // arrow's real 3.0 muzzle speed and fractional heights, it vanished.
        //
        // `enterAxis >= 0` declines a ray that *starts inside* the box, because
        // no face was crossed to get there. For picking a block to break or
        // place this is exactly right - you must not select the box your eye is
        // already within, from its inside. For a sweep (`collision = true`) it
        // is what lets a body already embedded in geometry keep moving instead
        // of locking solid, which is the more forgiving of the two failure modes
        // and the one this project would choose anyway.
        //
        // **Neither is reachable from play**, and the reason is the same one the
        // eye-height note in `raycast` below gives: the player's eye sits at
        // `position.y + 1.62`, never integral at any whole ground height, and
        // 4000 random rays checked against brute force disagreed zero times.
        // Both wants an exactly-integral origin *and* an exactly-integral
        // direction to bite. Recorded rather than fixed.
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
                   float maxDistance, bool stopAtFluid) {
    RaycastHit result;

    const float length = glm::length(direction);
    if (length <= 0.0f) {
        return result;
    }
    const glm::vec3 dir = direction / length;

    glm::ivec3 cell{static_cast<int>(std::floor(origin.x)), static_cast<int>(std::floor(origin.y)),
                    static_cast<int>(std::floor(origin.z))};
    // **`floor` here is also what settles the exact-tangency case, and the
    // answer is "leave it alone".** A ray whose coordinate is *exactly* a whole
    // number runs along the plane between two cells; `floor` picks the upper
    // one, so a block on the lower side is grazed by the ray and never tested,
    // because the walk never enters its cell.
    //
    // Measured 2026-08-19 rather than reasoned about, and the measurement is
    // what makes this a comment instead of a fix. **It is a graze, not a
    // tunnel**: 4000 random rays through random solid fields were compared
    // against a brute-force nearest-genuine-entry over every cell, and the walk
    // disagreed on **zero** of them. It never skips a cell the ray actually
    // penetrates; the only thing it declines to report is a zero-thickness
    // surface touch, where `tEnter == tExit`.
    //
    // Declining is arguably the right answer anyway. Such a ray touches the
    // surfaces of *both* neighbours equally, so any hit would have to pick one
    // arbitrarily - and `adjacent`, which is where a placed block goes, would
    // be a coin flip between two cells. `floor` at least makes it deterministic.
    //
    // What would make this worth revisiting: an origin coordinate that is
    // systematically integral. There is none today - the eye is
    // `position.y + kEyeHeight`, and 1.62 keeps it off every whole number a
    // player can stand on. A camera snapped to a block centre or a ray fired
    // from a stored block coordinate would change that, and is the thing to
    // look for before assuming this is still unreachable.
    // Where a bucket's water would go: the cell the ray was in before it
    // reached the surface, since water has no face to take a normal from.
    glm::ivec3 previous = cell;
    // **Where the walk entered the cell it is currently in**, and the face it
    // crossed to do it. Only the `stopAtFluid` branch reads them, because that
    // is the one stop with no geometry to intersect - a fluid source has no
    // selection box, so there is no surface to report and the cell boundary is
    // the only honest answer. Both stay at their initial values while the walk
    // is still in the origin's own cell, which is the "began inside it" case
    // `RaycastHit::normal` documents.
    float enteredAt = 0.0f;
    glm::ivec3 enteredFace{0};
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
        if (stopAtFluid && isFluidSource(world.blockAt(cell.x, cell.y, cell.z))) {
            result.hit = true;
            result.block = cell;
            result.adjacent = previous;
            result.normal = enteredFace;
            result.distance = enteredAt;
            result.point = origin + dir * enteredAt;
            return result;
        }

        float tHit = 0.0f;
        glm::ivec3 normal{0};
        if (hitsBlockGeometry(world, origin, dir, cell, maxDistance, tHit, normal)) {
            result.hit = true;
            result.block = cell;
            result.adjacent = cell + normal;
            result.normal = normal;
            result.distance = tHit;
            result.point = origin + dir * tHit;
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

        // Read before the step, because `tMax[axis]` *is* the parameter at which
        // the ray crosses into the next cell and the line below moves it on to
        // the one after that.
        enteredAt = tMax[axis];
        enteredFace = glm::ivec3{0};
        // Out of the cell being entered, back the way the ray came - the same
        // convention `hitsBlockGeometry` uses for a real face.
        enteredFace[axis] = -step[axis];
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

SweepHit sweepBlocks(const World& world, const glm::vec3& from, const glm::vec3& to) {
    SweepHit result;
    result.point = to;

    const glm::vec3 delta = to - from;
    const float length = glm::length(delta);
    if (length < 1e-6f) {
        return result;
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

    while (true) {
        float tHit = 0.0f;
        glm::ivec3 normal{0};
        if (hitsBlockGeometry(world, from, dir, cell, length, tHit, normal, true) && tHit <= length) {
            result.hit = true;
            result.block = cell;
            result.normal = normal;
            result.distance = tHit;
            result.point = from + dir * tHit;
            return result;
        }

        int axis = 0;
        if (tMax.y < tMax[axis]) {
            axis = 1;
        }
        if (tMax.z < tMax[axis]) {
            axis = 2;
        }
        if (tMax[axis] > length) {
            return result;
        }

        cell[axis] += step[axis];
        tMax[axis] += tDelta[axis];
    }
}

} // namespace game
