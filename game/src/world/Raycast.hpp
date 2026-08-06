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
///
/// `stopAtWater` makes a water source count as something to hit. It is off by
/// default because water has no selection geometry - you aim *through* it at
/// the riverbed - and on only for a bucket, which is the reference's own
/// arrangement: fluids are invisible to a normal reach and solid to a bucket.
RaycastHit raycast(const World& world, const glm::vec3& origin, const glm::vec3& direction,
                   float maxDistance, bool stopAtWater = false);

/// Whether anything opaque stands between two points.
///
/// **Deliberately not `raycast`.** That one walks `selectionBoxes`, because it
/// answers "what am I aiming at" and you must be able to aim at a tuft of
/// grass. Vision is a different question and `Block.hpp` already owns the
/// predicate for it - `isOpaque`, which is documented as *blocks vision* and is
/// kept distinct from *blocks movement* for exactly this reason. Asking the
/// wrong one made tall grass hide the player from a creeper, which stuttered
/// its fuse every time it walked through a meadow.
bool hasLineOfSight(const World& world, const glm::vec3& from, const glm::vec3& to);

} // namespace game
