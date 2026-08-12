#pragma once

#include "world/Block.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>

#include <cmath>
#include <limits>

namespace game {

/// Keeps a box a hair away from surfaces it rests against, so a resolved
/// contact does not immediately re-report as a collision.
constexpr float kCollisionSkin = 0.001f;

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/// A block's collision geometry **where it stands**.
///
/// `collisionBoxes` can only see an id, so it has to assume a fence, wall or
/// pane grows every arm. That is right for a fence in a line and wrong for a
/// lone pane of glass, which is drawn as a two-texel post and was collided with
/// as a full cross - an invisible shell round it, which is exactly the
/// complaint the ladder had answered.
inline BlockBoxes worldCollisionBoxes(const World& world, int x, int y, int z) {
    const BlockId id = world.blockAt(x, y, z);
    const BlockShape shape = blockShape(id);
    if (!connectsToNeighbours(shape)) {
        return collisionBoxes(id);
    }
    return collisionBoxesWith(
        id, connectionBits(shape, world.blockAt(x, y, z - 1), world.blockAt(x, y, z + 1),
                           world.blockAt(x - 1, y, z), world.blockAt(x + 1, y, z)));
}

/// The same question for the crosshair. Kept beside its twin so the two cannot
/// drift apart, which is the whole reason the connection rule has one owner.
inline BlockBoxes worldSelectionBoxes(const World& world, int x, int y, int z) {
    const BlockId id = world.blockAt(x, y, z);
    const BlockShape shape = blockShape(id);
    if (!connectsToNeighbours(shape)) {
        return selectionBoxes(id);
    }
    return selectionBoxesWith(
        id, connectionBits(shape, world.blockAt(x, y, z - 1), world.blockAt(x, y, z + 1),
                           world.blockAt(x - 1, y, z), world.blockAt(x + 1, y, z)));
}

/// What the mesher actually put on screen for this block, in world terms.
///
/// Beside its two twins for the same reason they are beside each other: the
/// connection rule has one owner and all three have to ask it. This is the one
/// to use when drawing **onto** a block rather than colliding with it.
inline BlockBoxes worldDrawnBoxes(const World& world, int x, int y, int z) {
    const BlockId id = world.blockAt(x, y, z);
    const BlockShape shape = blockShape(id);
    const std::uint8_t connections =
        connectsToNeighbours(shape)
            ? connectionBits(shape, world.blockAt(x, y, z - 1), world.blockAt(x, y, z + 1),
                             world.blockAt(x - 1, y, z), world.blockAt(x + 1, y, z))
            : std::uint8_t{0};
    return drawnBoxes(id, connections, isOpaque(world.blockAt(x, y + 1, z)));
}

/// True if any block's collision geometry overlaps the box.
///
/// **Everything that collides with the world goes through here**, because
/// `collisionBoxes` is the single source of truth for a block's extent and a
/// second copy of that answer is the recurring bug in this project. Testing
/// whole cells is only correct while every solid block fills its cell, which
/// stopped being true at M17b.
///
/// Subtracting the skin from the upper bound stops a box resting exactly on a
/// boundary from counting the next block along.
inline bool overlapsSolid(const World& world, const Aabb& box) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kCollisionSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kCollisionSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kCollisionSkin));

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockBoxes shape = worldCollisionBoxes(world, x, y, z);
                for (int i = 0; i < shape.count; ++i) {
                    const BlockBox& b = shape.boxes[i];
                    if (box.min.x < static_cast<float>(x) + b.maxX &&
                        box.max.x > static_cast<float>(x) + b.minX &&
                        box.min.y < static_cast<float>(y) + b.maxY &&
                        box.max.y > static_cast<float>(y) + b.minY &&
                        box.min.z < static_cast<float>(z) + b.maxZ &&
                        box.max.z > static_cast<float>(z) + b.minZ) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/// Highest surface under `box` that its underside may come to rest on, or
/// negative infinity if there is none.
///
/// Landing is the one case where the blocking plane is not a block boundary: a
/// slab's top is halfway up its cell. Snapping to the boundary leaves the body
/// on thin air, the ground probe finds nothing, and it falls again - a bounce
/// that repeats forever.
inline float highestSurfaceBelow(const World& world, const Aabb& box, float notAbove) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kCollisionSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kCollisionSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(notAbove));

    float best = -std::numeric_limits<float>::infinity();
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockBoxes shape = worldCollisionBoxes(world, x, y, z);
                for (int i = 0; i < shape.count; ++i) {
                    const BlockBox& b = shape.boxes[i];
                    // Only boxes actually under the footprint can be landed on.
                    if (box.min.x >= static_cast<float>(x) + b.maxX ||
                        box.max.x <= static_cast<float>(x) + b.minX ||
                        box.min.z >= static_cast<float>(z) + b.maxZ ||
                        box.max.z <= static_cast<float>(z) + b.minZ) {
                        continue;
                    }
                    const float top = static_cast<float>(y) + b.maxY;
                    if (top <= notAbove + kCollisionSkin && top > best) {
                        best = top;
                    }
                }
            }
        }
    }
    return best;
}

} // namespace game
