#pragma once

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

namespace game {

/// Placeholder day cycle: one sun on a fixed arc, no moon, no seasons.
///
/// Deliberately crude. Real directional lighting with cast shadows is M24 and
/// needs the renderer restructure at M23 first; this exists so the world has a
/// visible light source and stops reading as flat.
namespace sky {

/// How far from the camera the sun disc is drawn. Far enough to sit behind all
/// terrain, near enough to stay inside the far plane.
constexpr float kSunDistance = 320.0f;

/// Half-width of the sun quad at that distance, chosen so it subtends roughly
/// the same angle as a real sun rather than filling the sky.
constexpr float kSunRadius = 17.0f;

/// Direction *toward* the sun for a time of day in [0,1), where 0 is sunrise.
///
/// The arc is tilted slightly off the north-south plane so the sun does not pass
/// exactly overhead, which would leave vertical faces evenly lit at noon and
/// look wrong for the same reason a light directly above a face does.
glm::vec3 sunDirection(float timeOfDay);

/// Background colour for a given sun direction: blue overhead, warm near the
/// horizon, dark once the sun is down.
glm::vec3 skyColor(const glm::vec3& sunDirection);

/// How strongly the sun lights surfaces, fading out below the horizon.
/// `ambient` is the floor everything receives regardless of facing.
void sunLighting(const glm::vec3& sunDirection, float& ambient, float& sun);

/// Floor brightness, so a surface no light reaches is dim rather than pure
/// black. Fully black geometry reads as a hole in the world rather than shadow.
constexpr float kAmbientFloor = 0.06f;

/// A unit quad on the XY plane, textured with the sun. Built once; the transform
/// does the positioning.
engine::MeshData makeSunQuad();

/// Places and orients the sun quad so it faces the camera.
glm::mat4 sunTransform(const glm::vec3& cameraPosition, const glm::vec3& sunDirection);

} // namespace sky
} // namespace game
