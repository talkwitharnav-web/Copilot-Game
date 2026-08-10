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

constexpr glm::vec3 kDaySky{0.29f, 0.50f, 0.82f};
constexpr glm::vec3 kDuskSky{0.80f, 0.52f, 0.34f};
/// **Night is dark, and only slightly blue.** It was three times this and blue
/// enough to read as an overcast afternoon - the player's words were that it
/// gave a false feeling of daytime. Distant terrain fades to this too, so it is
/// also what stops a hillside at midnight being picked out against the sky.
constexpr glm::vec3 kNightSky{0.005f, 0.006f, 0.011f};

/// 0 below `low`, 1 above `high`, eased in between.
float ramp(float value, float low, float high) {
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

/// One billboarded quad. The sun and moon differ only in which layer they
/// sample, how big the transform makes them, and how brightly they burn.
engine::MeshData makeCelestialQuad(float layer, float brightness) {
    engine::MeshData mesh;

    const glm::vec2 corners[4]{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    const glm::vec2 uvs[4]{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

    for (int i = 0; i < 4; ++i) {
        mesh.vertices.push_back(engine::Vertex{{corners[i].x, corners[i].y, 0.0f},
                                               engine::packVertexColor(brightness, brightness,
                                                                       brightness, 1.0f),
                                               {uvs[i].x, uvs[i].y},
                                               layer,
                                               engine::kVertexSurfaceDefault});
    }

    // Both windings, like the HUD: the quad is billboarded, so which side faces
    // the camera depends on where the body is, and backface culling would hide
    // it for half the day.
    mesh.indices = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
    return mesh;
}

} // namespace

glm::vec3 sunDirection(float timeOfDay) {
    const float angle = timeOfDay * 2.0f * kPi;
    // Turned about the axis rather than written out, so the plane the arc lies
    // in and the axis the quad is squared against cannot drift apart. If they
    // ever did the sun would be sheared rather than square.
    const glm::vec3 overhead = glm::cross(kOrbitAxis, kSunrise);
    return glm::normalize(kSunrise * std::cos(angle) + overhead * std::sin(angle));
}

glm::vec3 moonDirection(float timeOfDay) {
    return -sunDirection(timeOfDay);
}

glm::vec3 skyColor(const glm::vec3& direction) {
    const float elevation = direction.y;
    const glm::vec3 lit = glm::mix(kDuskSky, kDaySky, ramp(elevation, 0.02f, kDayElevation));
    return glm::mix(kNightSky, lit, ramp(elevation, kNightElevation, 0.06f));
}

Sunlight lighting(float timeOfDay) {
    const glm::vec3 sun = sunDirection(timeOfDay);
    const float daylight = ramp(sun.y, kNightElevation, kDayElevation);

    Sunlight out;
    // Night is genuinely dark now. It was 0.22 and read as an overcast
    // afternoon; what makes it navigable is the moon having a direction rather
    // than the ambient being raised until everything is visible.
    out.ambient = glm::mix(0.065f, 0.55f, daylight);

    if (sun.y > 0.0f) {
        out.direction = sun;
        out.strength = 0.5f * daylight;
        return out;
    }

    // The moon. Weak enough that night is still night, strong enough to be
    // plainly the thing lighting the world - which is what a bright moon is.
    // It fades toward the horizon exactly as the sun does, so the handover at
    // dawn and dusk has no step in it.
    out.direction = -sun;
    out.strength = 0.10f * ramp(-sun.y, kNightElevation, kDayElevation);
    return out;
}

engine::MeshData makeSunQuad() {
    return makeCelestialQuad(static_cast<float>(TextureLayer::Sun), 1.0f);
}

engine::MeshData makeMoonQuad(int phase) {
    const int wrapped = ((phase % kMoonPhases) + kMoonPhases) % kMoonPhases;
    // **A quarter of the sun's.** Both go through the same emissive multiply,
    // and at full strength the moon burns a hole in the night sky - it is a lit
    // rock, not a second star.
    return makeCelestialQuad(static_cast<float>(kMoonPhaseFirst + wrapped), 0.26f);
}

glm::mat4 skyTransform(const glm::vec3& cameraPosition, const glm::vec3& direction, float size,
                       float distance) {
    const glm::vec3 centre = cameraPosition + direction * distance;
    const float radius = size * distance;

    // The axis is perpendicular to the arc, so it is perpendicular to the body
    // wherever the body is - no projection, and no angle at which the two
    // collapse into each other.
    const glm::vec3 right = kOrbitAxis * radius;
    const glm::vec3 up = glm::normalize(glm::cross(kOrbitAxis, direction)) * radius;

    glm::mat4 transform{1.0f};
    transform[0] = glm::vec4{right, 0.0f};
    transform[1] = glm::vec4{up, 0.0f};
    // Facing back down the arc at the camera, which is what the other two
    // spanning it leaves.
    transform[2] = glm::vec4{-direction, 0.0f};
    transform[3] = glm::vec4{centre, 1.0f};
    return transform;
}

} // namespace game::sky
