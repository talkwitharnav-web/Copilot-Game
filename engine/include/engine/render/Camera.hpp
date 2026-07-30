#pragma once

#include <glm/glm.hpp>

namespace engine {

/// A free-flying camera described by a position and two angles.
///
/// Holds state and the maths to turn it into a view matrix, nothing else. Which
/// keys move it is a game decision and stays in game code.
class Camera {
public:
    glm::vec3 position{0.0f, 0.0f, 0.0f};

    /// Radians. Yaw turns around the world's vertical axis; pitch looks up/down.
    float yaw = 0.0f;
    float pitch = 0.0f;

    /// Applies a look delta and clamps pitch just short of vertical. Looking
    /// exactly straight up makes the view matrix's up vector ambiguous, which
    /// shows up as the view violently flipping over.
    void addLook(float yawDelta, float pitchDelta);

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::mat4 viewMatrix() const;
};

} // namespace engine
