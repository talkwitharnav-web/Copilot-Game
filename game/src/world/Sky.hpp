#pragma once

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

namespace game {

/// Placeholder day cycle: one sun on a fixed arc, no seasons.
///
/// The sun and moon are drawn and both light the world; what is still crude is
/// the sky itself, which is three colours mixed by the sun's height rather than
/// anything atmospheric. That is M25.
namespace sky {

/// The axis the sun's arc turns about: horizontal, along north-south, so the
/// arc runs due east, through the zenith, and down to due west.
///
/// ⛔ **That the arc reaches the zenith is load-bearing, not incidental.** It is
/// also the single reference the sky bodies are squared against, so the two
/// cannot be separated. Tilt the arc out of this plane and the camera's own
/// vertical plane stops containing it, which rolls the square by the tilt as
/// the body climbs - a 0.28 tilt measured as far as 36 degrees off upright by
/// mid-morning, which is exactly what the player reported as the sun
/// "rotating". Every arc that misses the zenith does this and none of it is
/// tunable.
///
/// The cost is real and was accepted: at the moment of noon nothing vertical
/// faces the sun, so walls take only ambient light. That is the reference's own
/// arc, and it lasts about a minute of a day.
constexpr glm::vec3 kOrbitAxis{0.0f, 0.0f, 1.0f};

/// Where the arc starts: due east on the horizon. The rest of the arc is built
/// by turning this about `kOrbitAxis`, so the two can never disagree.
constexpr glm::vec3 kSunrise{1.0f, 0.0f, 0.0f};

/// Half-width of the sun quad, **as a fraction of how far away it is drawn**.
///
/// **This is the quad, not the disc.** The reference's celestial art puts the
/// body itself in the middle half of the texture and fills the rest with a very
/// dim halo, so the quad has to be about twice the size the body should look -
/// which is also what gives it something to bloom into.
///
/// A fraction rather than a length, because the distance is the far plane's and
/// the far plane moves with the render distance. Anything else makes the sun
/// change size when the player changes a setting.
constexpr float kSunSize = 0.094f;

/// The moon reads slightly smaller than the sun, which is what the reference
/// does and what stops the two being mistaken for each other at a glance.
constexpr float kMoonSize = 0.081f;

/// Direction *toward* the sun for a time of day in [0,1), where 0 is sunrise.
///
/// Due east, overhead, due west - see `kOrbitAxis` for why the arc may not be
/// tilted off that plane.
glm::vec3 sunDirection(float timeOfDay);

/// Directly opposite the sun, which is what the reference does - the two are one
/// object rotating, so the moon rises exactly as the sun sets and no second
/// clock can drift away from the first.
glm::vec3 moonDirection(float timeOfDay);

/// Which body is actually lighting the world, and how strongly.
///
/// One directional light, not two. Whichever is above the horizon wins, so
/// moonlight casts real shadows through the same cascades the sun uses and the
/// night stops being a flat wash. `strength` is what a surface facing it gains;
/// `ambient` is the floor everything receives regardless of facing.
struct Sunlight {
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    float ambient = 0.0f;
    float strength = 0.0f;
};
Sunlight lighting(float timeOfDay);

/// Background colour for a given sun direction: blue overhead, warm near the
/// horizon, dark once the sun is down.
glm::vec3 skyColor(const glm::vec3& sunDirection);

/// Floor brightness, so a surface no light reaches is dim rather than pure
/// black. Fully black geometry reads as a hole in the world rather than shadow.
/// Low, because the moon is the thing meant to make a night navigable.
constexpr float kAmbientFloor = 0.03f;

/// A unit quad on the XY plane, textured with the sun. Built once; the transform
/// does the positioning.
engine::MeshData makeSunQuad();

/// The same for the moon, at whichever phase the given day has reached. Phases
/// run 0 to 7 in the reference's own order, so the index is the day count
/// modulo eight.
engine::MeshData makeMoonQuad(int phase);

/// Where to draw a sky body, as a transform for its quad.
///
/// **Spanned by `kOrbitAxis` and the arc's own tangent.** Both are perpendicular
/// to the body at every point of the arc, so the quad is face-on with nothing
/// projected away and no angle at which the pair collapses - and because the
/// axis is fixed, the square is rigidly attached to the sky rather than to the
/// camera or to the body.
///
/// That is what the player asked for, and each half of it falls out of one
/// choice. Facing a body near the horizon, the axis lies along the screen's
/// horizontal and the tangent along its vertical, so the square is upright at
/// sunrise looking east and at sunset looking west. Overhead, the same pair are
/// the world's two horizontal axes, so the square is upright facing north,
/// south, east or west and stands on a corner facing the four between them.
///
/// **Do not span it by the camera's own right and up.** That is a square on
/// screen no matter where you look, which is the version the player rejected -
/// it makes the sun a sticker on the lens rather than a thing in the sky.
glm::mat4 skyTransform(const glm::vec3& cameraPosition, const glm::vec3& direction, float size,
                       float distance);

} // namespace sky
} // namespace game
