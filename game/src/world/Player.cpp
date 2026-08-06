#include "world/Player.hpp"

#include "world/Collision.hpp"
#include "world/Fluid.hpp"
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

/// What fraction of the camera's remaining step lag survives each 50 ms tick.
/// Half every tick is about a sixth of a second to settle - long enough to read
/// as a rise, short enough that aiming never feels behind the view.
constexpr float kStepSmoothPerTick = 0.5f;

/// Keeps the box a hair away from surfaces it is resting against, so a resolved
/// contact does not immediately re-report as a collision.
constexpr float kSkin = kCollisionSkin;

Aabb boxAt(const glm::vec3& feet, float height) {
    constexpr float half = kWidth * 0.5f;
    return Aabb{{feet.x - half, feet.y, feet.z - half}, {feet.x + half, feet.y + height, feet.z + half}};
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

/// Seconds of air, and the drowning clock, advanced from the head alone.
///
/// Nothing consumes the damage yet, because the player has no health until M21;
/// the meter is on screen so the state is visible rather than merely computed.
void updateBreath(Player& player, float dt) {
    using namespace fluid;

    if (player.underwater) {
        player.air = std::max(0.0f, player.air - dt);
        if (player.air <= 0.0f) {
            player.drowningSeconds += dt;
        }
        return;
    }

    // Refilling is a fixed time from empty to full rather than a fixed rate, so
    // `inhale_time` transfers straight across.
    player.air = std::min(kAirSeconds, player.air + dt * kAirSeconds / kInhaleSeconds);
    player.drowningSeconds = 0.0f;
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
    const fluid::FluidContact water = fluid::sampleFluid(world, boxAt(player.position, height));
    player.inWater = water.inWater;
    player.sinceWater = player.inWater ? 0.0f : player.sinceWater + dt;
    player.underwater =
        isWater(world.blockAt(static_cast<int>(std::floor(player.position.x)),
                              static_cast<int>(std::floor(player.position.y + player.eyeOffset)),
                              static_cast<int>(std::floor(player.position.z))));

    // Starting a sprint-swim needs the head under; keeping one only needs to be
    // in water at all, which is what lets you sprint along the surface without
    // dropping out of it every time the head breaks through.
    player.swimming =
        input.sprint && !player.flying && player.inWater && (player.underwater || player.swimming);

    updateBreath(player, dt);

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
    } else if (player.inWater) {
        // Water is not "gravity, but weaker". The reference multiplies velocity
        // by a drag factor every tick and adds a fixed impulse, so sinking,
        // swimming, being carried by a current and the long plunge after a dive
        // are all the same expression settling toward a different speed.
        using namespace fluid;

        const float drag = player.swimming ? kSprintSwimDrag : kWaterDrag;
        const float swimSpeed = player.swimming ? kSprintSwimSpeed : kSwimSpeed;

        // **Sprint-swimming goes where you look**, and that is most of what
        // makes it worth doing: flattened to the horizontal it can only skim,
        // so a dive or a climb is no quicker than paddling. Ordinary swimming
        // stays flat, or glancing down while treading water would pull you
        // under. Splitting the speed between the two axes by the look pitch
        // keeps the total the same whichever way you point.
        const bool steering = glm::dot(wish, wish) > 0.0f;
        const float lookY = player.swimming && steering ? input.lookY : 0.0f;
        const float flat = std::sqrt(std::max(0.0f, 1.0f - lookY * lookY));

        // A flowing cell's push is one more impulse under the same drag, so it
        // simply adds to wherever the input was already heading. Swimming
        // upstream works - it is only slower.
        const glm::vec3 target = wish * (swimSpeed * flat) + water.flow * kCurrentSpeed;
        const glm::vec3 next = approach(glm::vec3{player.velocity.x, 0.0f, player.velocity.z},
                                        glm::vec3{target.x, 0.0f, target.z}, drag, dt);
        player.velocity.x = next.x;
        player.velocity.z = next.z;

        // Ankle-deep water jumps like dry land, which is the reference's own
        // rule and stops a puddle starting a swim. Anything deeper falls
        // through to the climb below.
        const bool standing = player.onGround && water.depth < kShallowDepth;
        if (input.jump && standing) {
            player.velocity.y = kJumpVelocity;
            player.onGround = false;
        } else {
            // Holding jump does one of two quite different things, and running
            // them at the same speed is what threw the player clear of the
            // water: while the eyes are still under it is a **climb**, at the
            // full swim-up speed; once they are out it is a **stroke**, which
            // only has to beat the sink.
            //
            // The stroke is latched across `kStroke` rather than faded out,
            // because a drive that tapers to nothing as the head clears is
            // first-order - it settles dead and leaves you floating motionless.
            // Kicking below the low mark and drifting above the high one
            // cannot settle. Measured: a 0.16 m bob, eyes never less than 0.07
            // clear of the water, at most a quarter of the body out of it.
            const float headClear = player.eyeOffset - water.depth;
            if (headClear < kFloatEye - kStroke) {
                player.treading = true;
            } else if (headClear > kFloatEye + kStroke) {
                player.treading = false;
            }

            const float rise = player.swimming ? kSprintSwimUpSpeed : kSwimUpSpeed;
            const float dive = player.swimming ? kSprintSwimDownSpeed : kSwimDownSpeed;
            // Sprint-swimming skips water gravity outright, which is why it
            // holds depth with no input at all. That is not buoyancy - nothing
            // pushes a swimmer up; gravity is simply absent.
            const float idle = player.swimming ? 0.0f : -kSinkSpeed;

            // **The drift between strokes is the ordinary sink even while
            // sprint-swimming.** Skipping water gravity is a rule about holding
            // depth with your head under; at the surface it means nothing
            // brings you back down, so a sprint-swimmer hung motionless with
            // half a metre of air under their eyes.
            //
            // Pushing off the bottom is a **climb** at the water's own speed
            // rather than a leap: a single block of water used to launch you off
            // the floor like solid ground. At the climb speed a shallow pool
            // comes out indistinguishable from deep water - same bob, same
            // height - which is the whole point of it.
            const float held = (headClear <= 0.0f || player.onGround) ? rise
                               : player.treading                      ? kTreadSpeed
                                                                      : -kSinkSpeed;
            const float verticalTarget = input.jump  ? held
                                         : input.sneak ? -dive
                                         : lookY != 0.0f ? lookY * swimSpeed
                                                         : idle;
            player.velocity.y = approach(player.velocity.y, verticalTarget, drag, dt);
            player.onGround = false;
        }
    } else {
        // Eased as one horizontal vector, for the same reason flight is: per-axis
        // easing stops one axis dead while starting another, which turns a
        // direction change into a stutter instead of a curve.
        //
        // A bob at the water's surface lifts the whole box clear for a fraction
        // of a second, and this branch would spend it accelerating toward the
        // *walking* speed - so swimming forward came out as a lurch on every
        // bounce. A swimmer keeps a swimmer's speed until it has been out long
        // enough to have actually left.
        const float horizontal =
            !player.onGround && player.sinceWater < fluid::kSwimGrace
                ? (player.swimming ? fluid::kSprintSwimSpeed : fluid::kSwimSpeed)
                : speed;
        const glm::vec2 target{wish.x * horizontal, wish.z * horizontal};
        const glm::vec2 current{player.velocity.x, player.velocity.z};
        const glm::vec2 difference = target - current;
        const float distance = glm::length(difference);

        const bool wantsToMove = glm::dot(target, target) > 0.0f;
        const float rate = player.onGround ? (wantsToMove ? kGroundAcceleration : kGroundDeceleration)
                                           : (wantsToMove ? kAirAcceleration : kAirDeceleration);
        const float maxStep = rate * dt;

        const glm::vec2 next =
            distance <= maxStep ? target : current + difference * (maxStep / distance);
        player.velocity.x = next.x;
        player.velocity.z = next.y;

        if (input.jump && player.onGround) {
            player.velocity.y = kJumpVelocity;
            player.onGround = false;
        }
        player.velocity.y = std::max(player.velocity.y - kGravity * dt, -kTerminalVelocity);
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
                    // The box snaps; only the camera is given the rise to catch
                    // up on, which is what stops a staircase reading as a series
                    // of teleports.
                    player.stepSmooth = std::min(
                        player.stepSmooth + std::max(0.0f, stepped.y - player.position.y), kStepHeight);
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

    // Pressed against a ledge while swimming, with room over your head: climb.
    // The reference *sets* this rather than adding it, and it is the whole of
    // how you get out of a pool - and how you swim up a wall.
    if (player.inWater && !player.flying && (blockedX || blockedZ)) {
        glm::vec3 above = player.position;
        above.y += fluid::kSwimOutHeadroom;
        if (!overlapsSolid(world, boxAt(above, height))) {
            player.velocity.y = fluid::kSwimOutSpeed;
        }
    }

    // Exact at any frame rate, so the rise looks the same at 30 fps as at 240.
    player.stepSmooth *= std::pow(kStepSmoothPerTick, dt / 0.05f);
    if (player.stepSmooth < 0.001f) {
        player.stepSmooth = 0.0f;
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
