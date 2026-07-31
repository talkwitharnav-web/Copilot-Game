#include "world/Sky.hpp"

#include "world/Block.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game::sky {
namespace {

constexpr float kPi = 3.14159265358979323846f;

/// Sun elevation at which full daylight is reached, and the one below which it
/// is fully night. The band between them is dawn and dusk.
constexpr float kDayElevation = 0.25f;
constexpr float kNightElevation = -0.12f;

constexpr glm::vec3 kDaySky{0.45f, 0.62f, 0.80f};
constexpr glm::vec3 kDuskSky{0.80f, 0.52f, 0.34f};
constexpr glm::vec3 kNightSky{0.04f, 0.05f, 0.10f};

/// 0 below `low`, 1 above `high`, eased in between.
float ramp(float value, float low, float high) {
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

glm::vec3 sunDirection(float timeOfDay) {
    const float angle = timeOfDay * 2.0f * kPi;
    // +X is east, so the sun starts there, climbs, and sets toward -X.
    return glm::normalize(glm::vec3{std::cos(angle), std::sin(angle), 0.28f});
}

glm::vec3 skyColor(const glm::vec3& direction) {
    const float elevation = direction.y;
    const glm::vec3 lit = glm::mix(kDuskSky, kDaySky, ramp(elevation, 0.02f, kDayElevation));
    return glm::mix(kNightSky, lit, ramp(elevation, kNightElevation, 0.06f));
}

void sunLighting(const glm::vec3& direction, float& ambient, float& sun) {
    const float daylight = ramp(direction.y, kNightElevation, kDayElevation);

    // Night keeps a usable floor rather than going black: this is a placeholder
    // sun, and a world nobody can see in is not worth shipping over realism.
    ambient = glm::mix(0.22f, 0.55f, daylight);
    sun = glm::mix(0.0f, 0.5f, daylight);
}

engine::MeshData makeSunQuad() {
    engine::MeshData mesh;

    const float layer = static_cast<float>(TextureLayer::Sun);
    const glm::vec2 corners[4]{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    const glm::vec2 uvs[4]{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

    for (int i = 0; i < 4; ++i) {
        mesh.vertices.push_back(engine::Vertex{
            {corners[i].x, corners[i].y, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {uvs[i].x, uvs[i].y}, layer});
    }

    // Both windings, like the HUD: the quad is billboarded, so which side faces
    // the camera depends on where the sun is, and backface culling would hide it
    // for half the day.
    mesh.indices = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
    return mesh;
}

glm::mat4 sunTransform(const glm::vec3& cameraPosition, const glm::vec3& direction) {
    const glm::vec3 centre = cameraPosition + direction * kSunDistance;

    // Billboard: any two axes perpendicular to the view direction will do, since
    // the sun is radially symmetric.
    const glm::vec3 reference = std::abs(direction.y) > 0.95f ? glm::vec3{1.0f, 0.0f, 0.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::vec3 right = glm::normalize(glm::cross(reference, direction)) * kSunRadius;
    const glm::vec3 up = glm::normalize(glm::cross(direction, right)) * kSunRadius;

    glm::mat4 transform{1.0f};
    transform[0] = glm::vec4{right, 0.0f};
    transform[1] = glm::vec4{up, 0.0f};
    transform[2] = glm::vec4{direction, 0.0f};
    transform[3] = glm::vec4{centre, 1.0f};
    return transform;
}

} // namespace game::sky
