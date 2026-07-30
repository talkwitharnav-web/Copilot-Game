#include "engine/render/Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace engine {
namespace {

constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
constexpr float kMaxPitch = 1.55f; // Just under 90 degrees.

} // namespace

void Camera::addLook(float yawDelta, float pitchDelta) {
    yaw += yawDelta;
    pitch = std::clamp(pitch + pitchDelta, -kMaxPitch, kMaxPitch);
}

glm::vec3 Camera::forward() const {
    const float cosPitch = std::cos(pitch);
    return glm::normalize(glm::vec3{cosPitch * std::cos(yaw), std::sin(pitch), cosPitch * std::sin(yaw)});
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), kWorldUp));
}

glm::mat4 Camera::viewMatrix() const {
    return glm::lookAt(position, position + forward(), kWorldUp);
}

} // namespace engine
