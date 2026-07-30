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

/// Ledges up to this high are climbed automatically. Without it, every
/// one-block rise stops you dead and uneven ground is miserable to walk on.
constexpr float kStepHeight = 0.6f;

constexpr float kWalkSpeed = 4.317f;
constexpr float kSprintSpeed = 5.612f;
constexpr float kSneakSpeed = 1.295f;
constexpr float kFlySpeed = 22.0f;

constexpr float kGravity = 32.0f;
constexpr float kTerminalVelocity = 78.4f;

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

    glm::vec3 eyePosition() const {
        return position + glm::vec3{0.0f, player_constants::kEyeHeight, 0.0f};
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

} // namespace game
