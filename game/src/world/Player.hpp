#pragma once

#include <glm/glm.hpp>

namespace game {

class World;

/// Player dimensions and motion, in metres and metres per second.
///
/// One block is one cubic metre, so these are directly comparable to real human
/// proportions. Values match the genre's established feel as a starting point;
/// they are tuning numbers, not part of the game's identity, and are expected to
/// change once there is real content to move through.
namespace player_constants {

constexpr float kWidth = 0.6f;
constexpr float kHeight = 1.8f;
constexpr float kEyeHeight = 1.62f;

/// Crouching shrinks the box from the top down, so the feet stay put.
constexpr float kSneakHeight = 1.5f;
constexpr float kSneakEyeHeight = 1.27f;

/// How fast the camera slides between standing and crouched eye level. The
/// collision box switches instantly; only the view is eased, because a camera
/// that teleports vertically reads as a glitch.
constexpr float kEyeAdjustSpeed = 8.0f;

/// Ledges up to this high are climbed automatically. Without it, every
/// one-block rise stops you dead and uneven ground is miserable to walk on.
constexpr float kStepHeight = 0.6f;

constexpr float kWalkSpeed = 4.317f;
constexpr float kSprintSpeed = 5.612f;
constexpr float kSneakSpeed = 1.295f;
constexpr float kFlySpeed = 11.0f;
/// Flying with sprint held. Fast enough to cross terrain quickly, but opt-in
/// rather than the default, which made ordinary flying uncontrollable.
constexpr float kFlySprintSpeed = 22.0f;

/// Flight eases into and out of motion rather than snapping to full speed.
/// Deceleration is the gentler of the two, which is what reads as gliding.
constexpr float kFlyAcceleration = 38.0f;
constexpr float kFlyDeceleration = 26.0f;

/// Walking ramps up and down too. Stopping is quicker than starting, so the
/// player still feels planted rather than skating.
constexpr float kGroundAcceleration = 30.0f;
constexpr float kGroundDeceleration = 42.0f;

/// Mid-air steering is deliberately feeble, and air drag is close to nothing:
/// that is what makes a jump commit to its arc instead of being flown.
constexpr float kAirAcceleration = 9.0f;
constexpr float kAirDeceleration = 2.0f;

constexpr float kGravity = 32.0f;
constexpr float kTerminalVelocity = 78.4f;

/// In water: gravity mostly cancels, everything slows, and holding jump swims
/// upward. Enough to make water survivable rather than a pit you drown in.
constexpr float kSwimGravityScale = 0.22f;
constexpr float kSwimSinkSpeed = 3.0f;
constexpr float kSwimRiseSpeed = 5.0f;
constexpr float kSwimSpeedScale = 0.55f;
constexpr float kSwimDrag = 6.0f;

/// Chosen so the jump apex is ~1.25 blocks: high enough to clear one block,
/// not high enough to clear two.
constexpr float kJumpVelocity = 8.944f;

} // namespace player_constants

/// Position is the centre of the player's feet, which is the natural anchor for
/// standing on a surface.
struct Player {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f, 0.0f, 0.0f};
    bool onGround = false;
    bool flying = false;

    /// Held across frames rather than read from input, because standing back up
    /// is refused when there is no headroom.
    bool sneaking = false;

    /// True while any part of the body is in water.
    bool inWater = false;

    float eyeOffset = player_constants::kEyeHeight;

    glm::vec3 eyePosition() const { return position + glm::vec3{0.0f, eyeOffset, 0.0f}; }

    float height() const {
        return sneaking ? player_constants::kSneakHeight : player_constants::kHeight;
    }
};

/// What the player is asking to do this frame. Produced from input by game code,
/// so the physics itself never reads the keyboard.
struct PlayerInput {
    /// Desired horizontal direction in world space; need not be normalised.
    glm::vec3 moveDirection{0.0f, 0.0f, 0.0f};
    bool jump = false;
    bool sprint = false;
    bool sneak = false;
    /// Only used while flying.
    float verticalWish = 0.0f;
};

/// Advances the player by one frame against the world.
///
/// Reads the world, writes only the player. `deltaSeconds` is clamped
/// internally, because a long stall must not let the player move far enough in
/// one step to pass straight through a wall.
void updatePlayer(Player& player, const PlayerInput& input, const World& world, float deltaSeconds);

/// True if a block at these coordinates would intersect the player's box.
/// Placing there would seal the player inside solid geometry.
bool playerOverlapsBlock(const Player& player, const glm::ivec3& block);

} // namespace game
