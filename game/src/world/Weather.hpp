#pragma once

#include "world/Biome.hpp"

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace engine {
struct MeshData;
}

namespace game {

class World;

/// Weather, as the reference models it.
///
/// **Two independent boolean flags with their own countdowns, not a three-way
/// state.** Rain on, thunder on, and a thunderstorm is simply both at once. The
/// thunder flag keeps cycling while it is dry, which is exactly why storms are
/// rare and why one can start partway through a rainstorm or end before it
/// does. Writing it as an enum with transitions would need a probability table
/// that does not exist in the reference and would get the frequencies wrong.
namespace weather {

constexpr float kTickSeconds = 1.0f / 20.0f;

/// The reference's timers, in ticks, drawn uniformly.
constexpr int kRainOnMinTicks = 12000;
constexpr int kRainOnMaxTicks = 24000;
constexpr int kRainOffMinTicks = 12000;
constexpr int kRainOffMaxTicks = 180000;
constexpr int kThunderOnMinTicks = 3600;
constexpr int kThunderOnMaxTicks = 15600;
constexpr int kThunderOffMinTicks = 12000;
constexpr int kThunderOffMaxTicks = 180000;

/// One in a hundred thousand per loaded chunk per tick, against the reference's
/// own **128-block simulation radius** rather than our render distance.
///
/// Porting the per-chunk probability straight across would have been wrong
/// twice over: our chunks are 32 blocks where the reference's columns are 16,
/// and our render distance reaches three times further. Either mistake alone
/// multiplies the strike rate by an order of magnitude. This is the reference's
/// resulting **rate**, which is the number that was actually measured.
constexpr float kStrikeChancePerSecond = 201.0f * 20.0f / 100000.0f;
constexpr float kSimulationRadius = 128.0f;

/// Below this the column freezes and snow falls instead of rain. The reference's
/// own threshold, against a biome's fixed temperature.
constexpr float kFreezingWarmth = 0.15f;

/// Temperature lost per block above y 81. Our world is short enough that this
/// only ever matters on a peak, which is exactly where it should.
constexpr float kWarmthPerBlock = 0.00125f;
constexpr float kWarmthBaseHeight = 81.0f;

/// How fast the levels move toward their targets - the reference's 0.01 a tick.
constexpr float kLevelRampPerSecond = 0.2f;

/// **Coverage leads the rain.** Clouds gather, then it rains; a deck that
/// thickens at the same moment the first drop falls reads as a slider being
/// dragged rather than as weather having a cause.
constexpr float kCoverageRampPerSecond = 0.03f;
constexpr float kCoverageLeadSeconds = 30.0f;

enum class Precipitation : std::uint8_t {
    None,
    Rain,
    Snow,
};

/// What falls in a column, or nothing.
///
/// **The dry test is a tag, not a list of biome names.** The reference asks
/// whether the biome's downfall is zero; ours asks whether it is tagged dry or
/// badlands, which is the same set and survives a new biome being added.
Precipitation precipitationFor(BiomeId biome, int surfaceY);

/// A strike, while it is on screen.
struct Strike {
    glm::vec3 position{0.0f};
    /// Counts up. The bolt and the flash both read it.
    float age = 0.0f;
    float duration = 0.0f;
    /// One to three return strokes, which is what stops a flash reading as a
    /// camera going off.
    int pulses = 1;
    std::uint32_t seed = 0;
    /// Damage is dealt once, on the frame it appears.
    bool resolved = false;
};

/// The whole weather state, ticked by the game loop.
class Weather {
public:
    explicit Weather(std::uint64_t seed);

    /// `cycle` false freezes the flags where they are, which is the reference's
    /// `doWeatherCycle` game rule and how the debug key holds a storm open.
    void update(float deltaSeconds, bool cycle);

    /// Clear, rain, storm - what the debug key steps through. **Rain first**,
    /// because the whole point of the key is seeing weather on demand and a
    /// first press that produces clear skies is a wasted one.
    void force(int state);
    int forced() const { return m_forced; }

    bool raining() const { return m_rainOn; }
    bool storming() const { return m_rainOn && m_thunderOn; }

    /// 0 to 1, ramped. Everything visual reads these rather than the flags.
    float rainLevel() const { return m_rainLevel; }
    float thunderLevel() const { return m_thunderLevel; }
    /// Runs ahead of the rain and settles far more slowly, so the sky thickens
    /// before the first drop and stays heavy after the last.
    float cloudLevel() const { return m_cloudLevel; }

    /// Blocks per second, at cloud height. One number drives the deck's drift,
    /// the rain's slant and anything else that should agree with them.
    float windSpeed() const { return m_windSpeed; }

    /// How much of the sky a strike is lighting, 0 when none is. Already
    /// includes the multi-pulse envelope.
    float flash() const { return m_flash; }
    const std::vector<Strike>& strikes() const { return m_strikes; }
    std::vector<Strike>& strikes() { return m_strikes; }

    /// Rolls strikes and places them on real columns. Separate from `update`
    /// because it needs the world, and `update` deliberately does not.
    void strike(World& world, const glm::vec3& around, float deltaSeconds);

private:
    float roll();
    int rollTicks(int minTicks, int maxTicks);

    std::uint64_t m_random;
    bool m_rainOn = false;
    bool m_thunderOn = false;
    float m_rainSeconds = 0.0f;
    float m_thunderSeconds = 0.0f;
    float m_rainLevel = 0.0f;
    float m_thunderLevel = 0.0f;
    float m_cloudLevel = 0.0f;
    float m_windSpeed = 1.0f;
    float m_flash = 0.0f;
    /// 0 follows the cycle, 1 forces rain, 2 forces a storm, 3 forces clear.
    int m_forced = 0;
    std::vector<Strike> m_strikes;
};

/// The falling curtain, as one mesh of camera-following quads.
///
/// **One vertical quad per column, exactly as the reference does it**, because
/// that single choice answers the hardest question for free: the quad starts at
/// the top of whatever blocks the rain in that column, so a roof, a cave and an
/// overhang all work with no shelter test anywhere. A screen-space version
/// would have needed the whole heightmap on the GPU to answer the same thing.
///
/// The streaks themselves are procedural in the fragment shader rather than a
/// texture, so they snap to the same pixel grid the blocks do.
///
/// Vertex channels, which `precipitation.frag` must agree with:
/// - colour r: sky light at the column top. g: a per-column phase. **b: the
///   wind's component along this quad**, rescaled into 0-1. a: the radial fade.
/// - uv x: **the world-space coordinate along the quad**, not a 0-1 span - the
///   slant has to be a direction in the world, and a quad-local one rotates
///   with each quad's own facing, which reads as a spiral when you look up.
///   uv y: the world height of that corner.
/// - layer: unused; rain and snow are told apart by a uniform.
engine::MeshData buildPrecipitationMesh(const World& world, const glm::vec3& eye, int radius,
                                        float level, const glm::vec2& windDirection);

/// Where a bolt starts, which is the base of the cloud deck.
constexpr float kBoltTop = 140.0f;

/// The agreed signal for "no artwork, just white". Must stay below every other
/// negative sentinel `triangle.frag` tests for.
constexpr float kBoltLayer = -6.0f;

/// The bolts currently in the sky, as one mesh.
///
/// Random midpoint displacement: a line from the deck to the ground, split at
/// its midpoint, the midpoint pushed sideways, and the push halved each
/// generation. Five generations, which is where it stops reading as a polyline
/// and before the extra detail goes sub-pixel.
///
/// **Each segment is a cross of two vertical quads, not a camera-facing one.**
/// The mesh is built without knowing where the camera is, and a cross reads the
/// same from every angle - the same trick a plant blade already uses here.
engine::MeshData buildBoltMesh(const std::vector<Strike>& strikes);

} // namespace weather
} // namespace game
