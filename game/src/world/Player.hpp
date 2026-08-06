#pragma once

#include "world/Fluid.hpp"

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

    /// True while any part of the body is in water. The reference switches its
    /// entire movement model on this, not on being fully under, so wading in
    /// the shallows is already swimming.
    bool inWater = false;

    /// True while the eye is under the surface. Separate from `inWater`,
    /// because breath and the swim state both ask about the head alone.
    bool underwater = false;

    /// Sprint-swimming: faster, slipperier, and it holds depth with no input
    /// because water gravity is skipped outright while it is on.
    ///
    /// Starting it needs the head under; keeping it only needs to be in water,
    /// which is what lets you sprint-swim along the surface.
    bool swimming = false;

    /// Seconds since any part of the body was last in water. Only the airborne
    /// horizontal reads it - see `fluid::kSwimGrace` for what it is for.
    float sinceWater = 0.0f;

    /// Mid-stroke while treading water. Latched across `fluid::kStroke` rather
    /// than recomputed, because a drive that fades out as the head clears is
    /// first-order and settles dead - the latch is what keeps the bob going.
    bool treading = false;

    /// Seconds of breath left, counting down only while the eye is submerged.
    float air = fluid::kAirSeconds;

    /// Counts on past empty, and every whole second of it is two health points
    /// once there is player health to take them from.
    float drowningSeconds = 0.0f;

    float eyeOffset = player_constants::kEyeHeight;

    /// Metres the camera still trails the feet after stepping up.
    ///
    /// The collision box snaps to the new height and only the *view* eases up
    /// after it. Ramping the box instead would leave a part-way body inside the
    /// block it is climbing, which is a whole family of stuck states.
    /// **Only the camera may read this** - reach, targeting and knockback all
    /// want the true eye.
    float stepSmooth = 0.0f;

    glm::vec3 eyePosition() const { return position + glm::vec3{0.0f, eyeOffset, 0.0f}; }

    /// Where the camera actually sits: the eye, trailing briefly after a step.
    glm::vec3 renderEyePosition() const {
        return eyePosition() - glm::vec3{0.0f, stepSmooth, 0.0f};
    }

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
    /// Vertical component of where the camera is pointing, positive up.
    /// **Only sprint-swimming reads it**: `moveDirection` is flattened so that
    /// looking down cannot drive you into the ground, and this is what puts the
    /// pitch back for the one case that wants it.
    float lookY = 0.0f;
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
