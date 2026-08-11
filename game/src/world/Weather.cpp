#include "world/Weather.hpp"

#include "world/World.hpp"

#include <engine/render/MeshData.hpp>
#include <engine/render/Vertex.hpp>

#include <algorithm>
#include <cmath>

namespace game::weather {
namespace {

/// Splitmix64. One line, good enough for weather, and it is a pure function of
/// its state so a strike can be replayed from its seed.
std::uint64_t nextRandom(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ull;
    std::uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

float hash01(std::uint32_t x, std::uint32_t z) {
    std::uint32_t h = x * 374761393u + z * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>((h ^ (h >> 16)) & 0xffffffu) / 16777215.0f;
}

/// Moves `value` toward `target` at `rate` a second, never past it.
float approach(float value, float target, float rate, float deltaSeconds) {
    const float step = rate * deltaSeconds;
    if (value < target) {
        return std::min(target, value + step);
    }
    return std::max(target, value - step);
}

} // namespace

Precipitation precipitationFor(BiomeId biome, int surfaceY) {
    // A dry biome gets nothing at all, which is the reference's downfall-of-zero
    // test wearing our tags. Asked first, because a desert is also hot enough to
    // pass every temperature test below.
    if (biomeHasAny(biome, BiomeTag::Dry | BiomeTag::Badlands)) {
        return Precipitation::None;
    }

    const float lapse =
        std::max(0.0f, static_cast<float>(surfaceY) - kWarmthBaseHeight) * kWarmthPerBlock;
    const float warmth = biomeInfo(biome).warmth - lapse;
    return warmth < kFreezingWarmth ? Precipitation::Snow : Precipitation::Rain;
}

Weather::Weather(std::uint64_t seed) : m_random(seed ^ 0x5745415448455230ull) {
    // Both flags start off with a fresh countdown, so a new world opens clear
    // and the first front arrives on its own schedule.
    m_rainSeconds = static_cast<float>(rollTicks(kRainOffMinTicks, kRainOffMaxTicks)) * kTickSeconds;
    m_thunderSeconds =
        static_cast<float>(rollTicks(kThunderOffMinTicks, kThunderOffMaxTicks)) * kTickSeconds;
}

float Weather::roll() {
    return static_cast<float>(nextRandom(m_random) >> 40) / 16777216.0f;
}

int Weather::rollTicks(int minTicks, int maxTicks) {
    const int span = maxTicks - minTicks + 1;
    return minTicks + static_cast<int>(nextRandom(m_random) % static_cast<std::uint64_t>(span));
}

void Weather::force(int state) {
    m_forced = state % 4;
    switch (m_forced) {
    case 1:
        m_rainOn = true;
        m_thunderOn = false;
        break;
    case 2:
        m_rainOn = true;
        m_thunderOn = true;
        break;
    case 3:
        m_rainOn = false;
        m_thunderOn = false;
        break;
    default:
        break;
    }
}

void Weather::update(float deltaSeconds, bool cycle) {
    if (cycle && m_forced == 0) {
        // Two countdowns that never consult each other. A storm is the overlap.
        m_rainSeconds -= deltaSeconds;
        if (m_rainSeconds <= 0.0f) {
            m_rainOn = !m_rainOn;
            m_rainSeconds = static_cast<float>(m_rainOn
                                                   ? rollTicks(kRainOnMinTicks, kRainOnMaxTicks)
                                                   : rollTicks(kRainOffMinTicks, kRainOffMaxTicks)) *
                            kTickSeconds;
        }
        m_thunderSeconds -= deltaSeconds;
        if (m_thunderSeconds <= 0.0f) {
            m_thunderOn = !m_thunderOn;
            m_thunderSeconds =
                static_cast<float>(m_thunderOn ? rollTicks(kThunderOnMinTicks, kThunderOnMaxTicks)
                                               : rollTicks(kThunderOffMinTicks, kThunderOffMaxTicks)) *
                kTickSeconds;
        }
    }

    const float rainTarget = m_rainOn ? 1.0f : 0.0f;
    const float thunderTarget = storming() ? 1.0f : 0.0f;
    m_rainLevel = approach(m_rainLevel, rainTarget, kLevelRampPerSecond, deltaSeconds);
    m_thunderLevel = approach(m_thunderLevel, thunderTarget, kLevelRampPerSecond, deltaSeconds);

    // **Staggered on purpose.** If the sky, the light and the deck all move over
    // the same five seconds it reads as someone dragging a slider; the cloud
    // cover taking half a minute is what makes the weather feel like it has a
    // cause.
    m_cloudLevel = approach(m_cloudLevel, rainTarget, kCoverageRampPerSecond, deltaSeconds);
    if (m_rainOn && m_cloudLevel < m_rainLevel) {
        m_cloudLevel = m_rainLevel;
    }

    // Calm, breezy, gale. Deliberately a single number: the deck's drift, the
    // rain's slant and anything added later all read it, so they cannot
    // disagree about which way the weather is going.
    const float windTarget = 1.0f + m_rainLevel * 5.0f + m_thunderLevel * 6.0f;
    m_windSpeed = approach(m_windSpeed, windTarget, 0.35f, deltaSeconds);

    m_flash = 0.0f;
    for (Strike& bolt : m_strikes) {
        bolt.age += deltaSeconds;
        if (bolt.age >= bolt.duration) {
            continue;
        }
        // **Two to four return strokes, not one ramp.** A single fade reads as a
        // camera flash going off; the stutter is what reads as lightning.
        const float t = bolt.age / bolt.duration;
        const float pulse = std::abs(std::sin(t * 3.14159265f * static_cast<float>(bolt.pulses)));
        m_flash = std::max(m_flash, pulse * (1.0f - t) * 1.6f);
    }
    m_strikes.erase(std::remove_if(m_strikes.begin(), m_strikes.end(),
                                   [](const Strike& bolt) { return bolt.age >= bolt.duration; }),
                    m_strikes.end());
}

void Weather::strike(World& world, const glm::vec3& around, float deltaSeconds) {
    if (!storming() || m_thunderLevel < 0.5f) {
        return;
    }
    if (roll() > kStrikeChancePerSecond * deltaSeconds) {
        return;
    }

    const float angle = roll() * 6.2831853f;
    // Square-rooted, or every strike clusters at the middle of the disc.
    const float distance = std::sqrt(roll()) * kSimulationRadius;
    const int x = static_cast<int>(std::floor(around.x + std::cos(angle) * distance));
    const int z = static_cast<int>(std::floor(around.z + std::sin(angle) * distance));

    const int top = world.highestSolid(x, z);
    if (top < 0) {
        return;
    }
    // The reference re-validates that **rain, not snow**, is falling there, which
    // is why lightning never strikes a cold or dry biome.
    if (precipitationFor(sampleBiome(world.seed(), x, z).dominant, top) != Precipitation::Rain) {
        return;
    }

    Strike bolt;
    bolt.position = glm::vec3{static_cast<float>(x) + 0.5f, static_cast<float>(top) + 1.0f,
                              static_cast<float>(z) + 0.5f};
    bolt.pulses = 1 + static_cast<int>(nextRandom(m_random) % 3);
    bolt.duration = 0.16f + roll() * 0.24f;
    bolt.seed = static_cast<std::uint32_t>(nextRandom(m_random));
    m_strikes.push_back(bolt);
}

engine::MeshData buildPrecipitationMesh(const World& world, const glm::vec3& eye, int radius,
                                        float level, const glm::vec2& windDirection) {
    engine::MeshData mesh;
    if (level <= 0.0f || radius <= 0) {
        return mesh;
    }

    const int eyeX = static_cast<int>(std::floor(eye.x));
    const int eyeY = static_cast<int>(std::floor(eye.y));
    const int eyeZ = static_cast<int>(std::floor(eye.z));
    const auto span = static_cast<float>(radius);

    const std::size_t columns = static_cast<std::size_t>(2 * radius + 1);
    mesh.vertices.reserve(columns * columns * 4);
    mesh.indices.reserve(columns * columns * 6);

    for (int dz = -radius; dz <= radius; ++dz) {
        for (int dx = -radius; dx <= radius; ++dx) {
            // **Radial, not per-axis.** The region is a square, so its corners
            // are 1.41 times further out than its edges; fading per axis leaves
            // four visible spikes of rain hanging in the air.
            const float away = std::sqrt(static_cast<float>(dx * dx + dz * dz)) / span;
            if (away > 1.0f) {
                continue;
            }

            const int x = eyeX + dx;
            const int z = eyeZ + dz;
            const int top = world.highestSolid(x, z);
            // A column with nothing in it is not loaded; drawing rain down an
            // unloaded shaft would put a curtain through the floor.
            if (top < 0) {
                continue;
            }

            // **This one line is the whole roof and cave test.** The curtain
            // starts above whatever stands in that column, so standing under an
            // overhang leaves the rain above it with nothing else to check.
            const float bottom = std::max(static_cast<float>(top + 1),
                                          static_cast<float>(eyeY) - span);
            const float ceiling = static_cast<float>(eyeY) + span;
            if (bottom >= ceiling) {
                continue;
            }

            // Turned to face the camera's column rather than the camera itself,
            // so the quad does not spin while you look around.
            float ax = static_cast<float>(-dz);
            float az = static_cast<float>(dx);
            const float length = std::sqrt(ax * ax + az * az);
            if (length < 0.001f) {
                ax = 1.0f;
                az = 0.0f;
            } else {
                ax /= length;
                az /= length;
            }

            const float centreX = static_cast<float>(x) + 0.5f;
            const float centreZ = static_cast<float>(z) + 0.5f;
            const float sky = static_cast<float>(world.skyLightAt(x, top + 1, z)) /
                              static_cast<float>(kMaxLight);
            const float phase = hash01(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(z));
            const float fade = level * (1.0f - away * away);
            // **How much of the wind runs along this quad**, which is the whole
            // of what makes the slant a world direction. Rescaled into 0-1
            // because a vertex colour channel cannot carry a sign.
            const float alongWind = (windDirection.x * ax + windDirection.y * az) * 0.5f + 0.5f;
            // The quad's own position measured along its tangent, in blocks, so
            // the drop pattern is anchored to the world rather than to the quad.
            const float alongCentre = centreX * ax + centreZ * az;

            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            const float corners[4][2] = {{-0.5f, 0.0f}, {0.5f, 0.0f}, {0.5f, 1.0f}, {-0.5f, 1.0f}};
            for (const auto& corner : corners) {
                const float height = corner[1] > 0.5f ? ceiling : bottom;
                mesh.vertices.push_back(engine::Vertex{
                    {centreX + ax * corner[0], height, centreZ + az * corner[0]},
                    engine::packVertexColor(sky, phase, alongWind, fade),
                    {alongCentre + corner[0], height},
                    0.0f,
                    engine::kVertexSurfaceDefault});
            }
            mesh.indices.insert(mesh.indices.end(),
                                {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }

    return mesh;
}

engine::MeshData buildBoltMesh(const std::vector<Strike>& strikes) {
    engine::MeshData mesh;

    for (const Strike& bolt : strikes) {
        if (bolt.age >= bolt.duration) {
            continue;
        }

        std::uint64_t random = bolt.seed;
        const auto next = [&]() { return static_cast<float>(nextRandom(random) >> 40) / 16777216.0f; };

        // Three strands: the one that reaches the ground and two that branch
        // off it and stop short, which is where real bolts branch.
        for (int strand = 0; strand < 3; ++strand) {
            std::vector<glm::vec3> path;
            path.push_back(glm::vec3{bolt.position.x, kBoltTop, bolt.position.z});
            path.push_back(bolt.position);

            float spread = (kBoltTop - bolt.position.y) * 0.10f;
            for (int generation = 0; generation < 5; ++generation) {
                std::vector<glm::vec3> split;
                split.reserve(path.size() * 2);
                for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                    split.push_back(path[i]);
                    glm::vec3 middle = (path[i] + path[i + 1]) * 0.5f;
                    middle.x += (next() - 0.5f) * spread;
                    middle.z += (next() - 0.5f) * spread;
                    split.push_back(middle);
                }
                split.push_back(path.back());
                path = std::move(split);
                spread *= 0.55f;
            }

            // A branch leaves partway down and ends in the air.
            std::size_t first = 0;
            std::size_t last = path.size() - 1;
            if (strand > 0) {
                first = path.size() / 4 + static_cast<std::size_t>(next() * 0.3f * static_cast<float>(path.size()));
                last = std::min(path.size() - 1, first + path.size() / 6);
                if (last <= first + 1) {
                    continue;
                }
            }

            const float width = strand == 0 ? 0.32f : 0.16f;
            for (std::size_t i = first; i + 1 <= last; ++i) {
                const glm::vec3 a = path[i];
                glm::vec3 b = path[i + 1];
                if (strand > 0) {
                    // Branches drift away from the trunk rather than tracking it.
                    const float lean = static_cast<float>(i - first) * 0.35f;
                    b.x += lean;
                    b.z += lean * 0.6f;
                }
                // Tapered toward the strike, which is what makes it read as
                // travelling downward rather than as a stripe.
                const float taper = 0.35f + 0.65f * static_cast<float>(last - i) /
                                                static_cast<float>(std::max<std::size_t>(1, last - first));
                const float half = width * taper;

                for (int plane = 0; plane < 2; ++plane) {
                    const float ox = plane == 0 ? half : 0.0f;
                    const float oz = plane == 0 ? 0.0f : half;
                    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                    const glm::vec3 corners[4] = {a - glm::vec3{ox, 0.0f, oz},
                                                  a + glm::vec3{ox, 0.0f, oz},
                                                  b + glm::vec3{ox, 0.0f, oz},
                                                  b - glm::vec3{ox, 0.0f, oz}};
                    for (const glm::vec3& corner : corners) {
                        mesh.vertices.push_back(
                            engine::Vertex{{corner.x, corner.y, corner.z},
                                           engine::packVertexColor(1.0f, 1.0f, 1.0f, 1.0f),
                                           {0.5f, 0.5f},
                                           kBoltLayer,
                                           engine::kVertexSurfaceDefault});
                    }
                    // Both windings, so it is never culled whichever side you
                    // are standing on.
                    mesh.indices.insert(mesh.indices.end(),
                                        {base, base + 1, base + 2, base, base + 2, base + 3,
                                         base, base + 2, base + 1, base, base + 3, base + 2});
                }
            }
        }
    }

    return mesh;
}

} // namespace game::weather
