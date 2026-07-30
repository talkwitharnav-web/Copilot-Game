#include "world/Player.hpp"

#include "world/World.hpp"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

using namespace player_constants;

/// A single frame may not advance further than this. At 20 fps the player still
/// moves under a quarter of a block per step, which keeps the one-block-deep
/// assumption in the collision resolver valid.
constexpr float kMaxDeltaSeconds = 0.05f;

/// Keeps the box a hair away from surfaces it is resting against, so a resolved
/// contact does not immediately re-report as a collision.
constexpr float kSkin = 0.001f;

struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

Aabb boxAt(const glm::vec3& feet) {
    constexpr float half = kWidth * 0.5f;
    return Aabb{{feet.x - half, feet.y, feet.z - half}, {feet.x + half, feet.y + kHeight, feet.z + half}};
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
                if (world.isSolid(x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/// Moves along one axis and snaps to the blocking surface if something is hit.
///
/// Axes are resolved one at a time on purpose. Resolving all three together
/// leaves the maths unable to tell which direction to push out of a corner,
/// which shows up as jitter or as sliding through walls diagonally.
bool moveAxis(glm::vec3& position, const World& world, int axis, float amount) {
    if (amount == 0.0f) {
        return false;
    }

    glm::vec3 candidate = position;
    candidate[axis] += amount;

    if (!overlapsSolid(world, boxAt(candidate))) {
        position = candidate;
        return false;
    }

    // How far the box extends past `position` on this axis, in each direction.
    constexpr float half = kWidth * 0.5f;
    const float extentAbove = (axis == 1) ? kHeight : half;
    const float extentBelow = (axis == 1) ? 0.0f : half;

    if (amount > 0.0f) {
        const float blockingPlane = std::floor(candidate[axis] + extentAbove);
        candidate[axis] = blockingPlane - extentAbove - kSkin;
    } else {
        const float blockingPlane = std::floor(candidate[axis] - extentBelow) + 1.0f;
        candidate[axis] = blockingPlane + extentBelow + kSkin;
    }

    position = candidate;
    return true;
}

} // namespace

void updatePlayer(Player& player, const PlayerInput& input, const World& world, float deltaSeconds) {
    const float dt = std::min(deltaSeconds, kMaxDeltaSeconds);

    const float speed = player.flying ? kFlySpeed
                        : input.sneak ? kSneakSpeed
                        : input.sprint ? kSprintSpeed
                                       : kWalkSpeed;

    glm::vec3 wish = input.moveDirection;
    wish.y = 0.0f;
    if (glm::dot(wish, wish) > 0.0f) {
        wish = glm::normalize(wish);
    }

    player.velocity.x = wish.x * speed;
    player.velocity.z = wish.z * speed;

    if (player.flying) {
        player.velocity.y = input.verticalWish * kFlySpeed;
        player.onGround = false;
    } else {
        if (input.jump && player.onGround) {
            player.velocity.y = kJumpVelocity;
            player.onGround = false;
        }
        player.velocity.y = std::max(player.velocity.y - kGravity * dt, -kTerminalVelocity);
    }

    // Vertical first, so standing on ground is established before the horizontal
    // move decides whether a step-up is allowed.
    const bool movingDown = player.velocity.y <= 0.0f;
    if (moveAxis(player.position, world, 1, player.velocity.y * dt)) {
        player.onGround = movingDown;
        player.velocity.y = 0.0f;
    } else if (player.velocity.y != 0.0f) {
        player.onGround = false;
    }

    const glm::vec3 beforeHorizontal = player.position;

    const bool blockedX = moveAxis(player.position, world, 0, player.velocity.x * dt);
    const bool blockedZ = moveAxis(player.position, world, 2, player.velocity.z * dt);

    if ((blockedX || blockedZ) && player.onGround && !player.flying) {
        // Retry the same horizontal move from a step higher. Accepted only if it
        // clears the obstacle and there is something to land on.
        glm::vec3 stepped = beforeHorizontal;
        stepped.y += kStepHeight;

        if (!overlapsSolid(world, boxAt(stepped))) {
            moveAxis(stepped, world, 0, player.velocity.x * dt);
            moveAxis(stepped, world, 2, player.velocity.z * dt);

            const bool movedFurther = glm::distance(glm::vec2{stepped.x, stepped.z},
                                                    glm::vec2{beforeHorizontal.x, beforeHorizontal.z}) >
                                      glm::distance(glm::vec2{player.position.x, player.position.z},
                                                    glm::vec2{beforeHorizontal.x, beforeHorizontal.z});

            if (movedFurther) {
                // Settle back down onto whatever is under the new position.
                moveAxis(stepped, world, 1, -kStepHeight);
                player.position = stepped;
            }
        }
    }

    if (blockedX) {
        player.velocity.x = 0.0f;
    }
    if (blockedZ) {
        player.velocity.z = 0.0f;
    }
}

} // namespace game
