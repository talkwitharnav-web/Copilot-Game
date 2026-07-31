#include "world/Player.hpp"

#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game {
namespace {

using namespace player_constants;

/// A single frame may not advance further than this. At 20 fps the player still
/// moves under a quarter of a block per step, which keeps the one-block-deep
/// assumption in the collision resolver valid.
constexpr float kMaxDeltaSeconds = 0.05f;

/// Longest distance any one collision step may cover. Movement is split into
/// however many steps this requires.
constexpr float kMaxStepDistance = 0.4f;

/// Keeps the box a hair away from surfaces it is resting against, so a resolved
/// contact does not immediately re-report as a collision.
constexpr float kSkin = 0.001f;

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

Aabb boxAt(const glm::vec3& feet, float height) {
    constexpr float half = kWidth * 0.5f;
    return Aabb{{feet.x - half, feet.y, feet.z - half}, {feet.x + half, feet.y + height, feet.z + half}};
}

/// True if any solid block overlaps the box. Subtracting the skin from the upper
/// bound stops a box resting exactly on a boundary from counting the next block.
bool overlapsSolid(const World& world, const Aabb& box) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kSkin));

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                // Not every solid block fills its cell, and stairs do not even
                // fill one box, so the cell test is only the first step.
                const BlockBoxes shape = collisionBoxes(world.blockAt(x, y, z));
                for (int i = 0; i < shape.count; ++i) {
                    const BlockBox& b = shape.boxes[i];
                    if (box.min.x < static_cast<float>(x) + b.maxX && box.max.x > static_cast<float>(x) + b.minX &&
                        box.min.y < static_cast<float>(y) + b.maxY && box.max.y > static_cast<float>(y) + b.minY &&
                        box.min.z < static_cast<float>(z) + b.maxZ && box.max.z > static_cast<float>(z) + b.minZ) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

/// True if solid ground sits directly under the player's footprint.
///
/// Probes a thin slab just below the feet rather than a single point, so
/// standing with only a corner over a block still counts as supported.
bool hasGroundBelow(const World& world, const glm::vec3& feet, float probeDepth = 0.1f) {
    Aabb probe = boxAt(feet, 0.0f);
    probe.min.y = feet.y - probeDepth;
    probe.max.y = feet.y;
    return overlapsSolid(world, probe);
}

/// True if any block the body occupies is water.
bool submerged(const World& world, const glm::vec3& feet, float height) {
    const Aabb box = boxAt(feet, height);
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kSkin));

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                if (isWater(world.blockAt(x, y, z))) {
                    return true;
                }
            }
        }
    }
    return false;
}

/// Highest surface under `box` that the feet may come to rest on.
///
/// Not every solid block fills its cell, so the landing plane is not simply the
/// block boundary. Snapping to the boundary above a slab drops the player onto
/// thin air, the ground probe finds nothing, and they fall again - a bounce that
/// repeats forever.
float highestSurfaceBelow(const World& world, const Aabb& box, float notAbove) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(notAbove));

    float best = -std::numeric_limits<float>::infinity();
    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockBoxes shape = collisionBoxes(world.blockAt(x, y, z));
                for (int i = 0; i < shape.count; ++i) {
                    const BlockBox& b = shape.boxes[i];
                    // Only boxes actually under the footprint can be landed on.
                    if (box.min.x >= static_cast<float>(x) + b.maxX || box.max.x <= static_cast<float>(x) + b.minX ||
                        box.min.z >= static_cast<float>(z) + b.maxZ || box.max.z <= static_cast<float>(z) + b.minZ) {
                        continue;
                    }
                    const float top = static_cast<float>(y) + b.maxY;
                    if (top <= notAbove + kSkin && top > best) {
                        best = top;
                    }
                }
            }
        }
    }
    return best;
}

/// The face that stopped motion along `axis`, or infinity if nothing did.
///
/// Resolution has to read the same shape table the overlap test does. Snapping
/// to the cell boundary instead is only correct for full cubes: a stair's step
/// starts halfway across its cell, so walking into one from the high side threw
/// the player a whole block backwards. Same failure as landing on a slab, one
/// axis over.
float blockingPlaneAlong(const World& world, const Aabb& box, int axis, bool positive) {
    const int minX = static_cast<int>(std::floor(box.min.x));
    const int maxX = static_cast<int>(std::floor(box.max.x - kSkin));
    const int minY = static_cast<int>(std::floor(box.min.y));
    const int maxY = static_cast<int>(std::floor(box.max.y - kSkin));
    const int minZ = static_cast<int>(std::floor(box.min.z));
    const int maxZ = static_cast<int>(std::floor(box.max.z - kSkin));

    float best = positive ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity();

    for (int y = minY; y <= maxY; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                const BlockBoxes shape = collisionBoxes(world.blockAt(x, y, z));
                const glm::vec3 cell{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};

                for (int i = 0; i < shape.count; ++i) {
                    const BlockBox& b = shape.boxes[i];
                    const glm::vec3 lo = cell + glm::vec3{b.minX, b.minY, b.minZ};
                    const glm::vec3 hi = cell + glm::vec3{b.maxX, b.maxY, b.maxZ};

                    // Only boxes overlapping on the other two axes can block
                    // this one; the rest are beside the player, not in the way.
                    bool blocks = true;
                    for (int other = 0; other < 3 && blocks; ++other) {
                        if (other == axis) {
                            continue;
                        }
                        blocks = box.min[other] < hi[other] && box.max[other] > lo[other];
                    }
                    if (!blocks) {
                        continue;
                    }

                    if (positive) {
                        best = std::min(best, lo[axis]);
                    } else {
                        best = std::max(best, hi[axis]);
                    }
                }
            }
        }
    }
    return best;
}

/// Moves along one axis and snaps to the blocking surface if something is hit.
///
/// Axes are resolved one at a time on purpose. Resolving all three together
/// leaves the maths unable to tell which direction to push out of a corner,
/// which shows up as jitter or as sliding through walls diagonally.
bool moveAxis(glm::vec3& position, const World& world, int axis, float amount, float height) {
    if (amount == 0.0f) {
        return false;
    }

    glm::vec3 candidate = position;
    candidate[axis] += amount;

    if (!overlapsSolid(world, boxAt(candidate, height))) {
        position = candidate;
        return false;
    }

    // Landing is the one case where the blocking plane is not a block boundary:
    // a slab's top is halfway up its cell. Its sides and underside still are.
    if (axis == 1 && amount < 0.0f) {
        const float surface = highestSurfaceBelow(world, boxAt(candidate, height), position.y);
        if (std::isfinite(surface)) {
            candidate.y = surface + kSkin;
            position = candidate;
            return true;
        }
    }

    // How far the box extends past `position` on this axis, in each direction.
    constexpr float half = kWidth * 0.5f;
    const float extentAbove = (axis == 1) ? height : half;
    const float extentBelow = (axis == 1) ? 0.0f : half;

    const float plane = blockingPlaneAlong(world, boxAt(candidate, height), axis, amount > 0.0f);
    if (!std::isfinite(plane)) {
        // Overlapped but nothing squarely in the way, which the skin makes
        // possible at a corner. Refusing the move is the safe answer.
        return true;
    }

    candidate[axis] = amount > 0.0f ? plane - extentAbove - kSkin : plane + extentBelow + kSkin;

    position = candidate;
    return true;
}

} // namespace

void updatePlayer(Player& player, const PlayerInput& input, const World& world, float deltaSeconds) {
    const float dt = std::min(deltaSeconds, kMaxDeltaSeconds);

    if (input.sneak) {
        player.sneaking = true;
    } else if (player.sneaking && !overlapsSolid(world, boxAt(player.position, kHeight))) {
        // Standing up inside a low gap would push the head into a block.
        player.sneaking = false;
    }

    const float height = player.height();
    player.inWater = submerged(world, player.position, height);

    const float targetEye = player.sneaking ? kSneakEyeHeight : kEyeHeight;
    const float maxEyeChange = kEyeAdjustSpeed * dt;
    player.eyeOffset += std::clamp(targetEye - player.eyeOffset, -maxEyeChange, maxEyeChange);

    const float flySpeed = input.sprint ? kFlySprintSpeed : kFlySpeed;
    const float speed = player.flying   ? flySpeed
                        : player.sneaking ? kSneakSpeed
                        : input.sprint    ? kSprintSpeed
                                          : kWalkSpeed;

    glm::vec3 wish = input.moveDirection;
    wish.y = 0.0f;
    if (glm::dot(wish, wish) > 0.0f) {
        wish = glm::normalize(wish);
    }

    if (player.flying) {
        // Eased as a single vector rather than per axis, so changing direction
        // curves through the turn instead of stopping one axis and starting
        // another.
        const glm::vec3 target{wish.x * speed, input.verticalWish * flySpeed, wish.z * speed};
        const glm::vec3 difference = target - player.velocity;
        const float distance = glm::length(difference);
        const float rate = glm::dot(target, target) > 0.0f ? kFlyAcceleration : kFlyDeceleration;
        const float maxStep = rate * dt;

        player.velocity = distance <= maxStep ? target : player.velocity + difference * (maxStep / distance);
        player.onGround = false;
    } else {
        // Eased as one horizontal vector, for the same reason flight is: per-axis
        // easing stops one axis dead while starting another, which turns a
        // direction change into a stutter instead of a curve.
        const float horizontalSpeed = player.inWater ? speed * kSwimSpeedScale : speed;
        const glm::vec2 target{wish.x * horizontalSpeed, wish.z * horizontalSpeed};
        const glm::vec2 current{player.velocity.x, player.velocity.z};
        const glm::vec2 difference = target - current;
        const float distance = glm::length(difference);

        const bool wantsToMove = glm::dot(target, target) > 0.0f;
        float rate = player.onGround ? (wantsToMove ? kGroundAcceleration : kGroundDeceleration)
                                     : (wantsToMove ? kAirAcceleration : kAirDeceleration);
        if (player.inWater) {
            rate = kSwimDrag;
        }
        const float maxStep = rate * dt;

        const glm::vec2 next =
            distance <= maxStep ? target : current + difference * (maxStep / distance);
        player.velocity.x = next.x;
        player.velocity.z = next.y;

        if (player.inWater) {
            // Buoyancy nearly cancels gravity, so holding jump climbs and doing
            // nothing drifts slowly down rather than dropping like a stone.
            if (input.jump) {
                player.velocity.y = kSwimRiseSpeed;
            } else {
                player.velocity.y =
                    std::max(player.velocity.y - kGravity * kSwimGravityScale * dt, -kSwimSinkSpeed);
            }
            player.onGround = false;
        } else {
            if (input.jump && player.onGround) {
                player.velocity.y = kJumpVelocity;
                player.onGround = false;
            }
            player.velocity.y = std::max(player.velocity.y - kGravity * dt, -kTerminalVelocity);
        }
    }

    // Vertical first, so standing on ground is established before the horizontal
    // move decides whether a step-up is allowed.
    const glm::vec3 displacement = player.velocity * dt;

    // The resolver snaps out of at most one block of penetration, so no single
    // step may cross more than a fraction of a block. Flying and long falls both
    // exceed a whole block per frame otherwise, and the snap then lands on the
    // wrong side of the wall.
    const float longest =
        std::max({std::abs(displacement.x), std::abs(displacement.y), std::abs(displacement.z)});
    const int steps = std::max(1, static_cast<int>(std::ceil(longest / kMaxStepDistance)));
    const glm::vec3 stepDelta = displacement / static_cast<float>(steps);

    bool blockedX = false;
    bool blockedZ = false;

    for (int i = 0; i < steps; ++i) {
        const bool movingDown = stepDelta.y <= 0.0f;
        if (moveAxis(player.position, world, 1, stepDelta.y, height)) {
            player.onGround = movingDown;
            player.velocity.y = 0.0f;

            // Descending onto solid ground ends flight. Only a downward landing
            // counts: clipping a wall while flying sideways leaves you airborne,
            // and hovering still has no vertical movement to collide at all.
            if (player.flying && movingDown) {
                player.flying = false;
            }
        } else if (stepDelta.y != 0.0f) {
            player.onGround = false;
        }

        const glm::vec3 beforeHorizontal = player.position;

        // Crouching on solid ground refuses any step that would leave nothing
        // underfoot. Checked per axis, so you can still slide along an edge
        // instead of being pinned in place.
        const bool guardEdges = player.sneaking && player.onGround && !player.flying &&
                                hasGroundBelow(world, player.position);

        const bool hitX = moveAxis(player.position, world, 0, stepDelta.x, height);
        if (guardEdges && !hasGroundBelow(world, player.position)) {
            player.position.x = beforeHorizontal.x;
        }

        const bool hitZ = moveAxis(player.position, world, 2, stepDelta.z, height);
        if (guardEdges && !hasGroundBelow(world, player.position)) {
            player.position.z = beforeHorizontal.z;
        }

        blockedX = blockedX || hitX;
        blockedZ = blockedZ || hitZ;

        if ((hitX || hitZ) && player.onGround && !player.flying) {
            // Retry the same horizontal move from a step higher. Accepted only
            // if it clears the obstacle and there is something to land on.
            glm::vec3 stepped = beforeHorizontal;
            stepped.y += kStepHeight;

            if (!overlapsSolid(world, boxAt(stepped, height))) {
                moveAxis(stepped, world, 0, stepDelta.x, height);
                moveAxis(stepped, world, 2, stepDelta.z, height);

                const auto travelled = [&](const glm::vec3& p) {
                    return glm::distance(glm::vec2{p.x, p.z},
                                         glm::vec2{beforeHorizontal.x, beforeHorizontal.z});
                };

                if (travelled(stepped) > travelled(player.position)) {
                    // Settle back down onto whatever is under the new position.
                    moveAxis(stepped, world, 1, -kStepHeight, height);
                    player.position = stepped;
                }
            }
        }

        // Backstop for every way this step could have moved the player,
        // including the step-up above, which resolves its own position and
        // would otherwise skip the per-axis checks entirely.
        if (guardEdges && !hasGroundBelow(world, player.position)) {
            player.position = beforeHorizontal;
        }
    }

    if (blockedX) {
        player.velocity.x = 0.0f;
    }
    if (blockedZ) {
        player.velocity.z = 0.0f;
    }
}

bool playerOverlapsBlock(const Player& player, const glm::ivec3& block) {
    const Aabb box = boxAt(player.position, player.height());
    const glm::vec3 blockMin{block};
    const glm::vec3 blockMax = blockMin + 1.0f;

    return box.min.x < blockMax.x && box.max.x > blockMin.x && box.min.y < blockMax.y && box.max.y > blockMin.y &&
           box.min.z < blockMax.z && box.max.z > blockMin.z;
}

} // namespace game
