#include "world/Weather.hpp"

#include "world/Climate.hpp"
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

    // `freezesAt`'s arithmetic without its jitter, off the constants that
    // function reads too - see `kWarmthPerBlock` for the unit and for what
    // quoting the reference's own figure here used to cost.
    const float lapse =
        std::max(0.0f, static_cast<float>(surfaceY) - kWarmthBaseHeight) * kWarmthPerBlock;
    const float warmth = biomeInfo(biome).warmth - lapse;
    return warmth < kFreezingWarmth ? Precipitation::Snow : Precipitation::Rain;
}

Precipitation precipitationFor(BiomeId biome, int surfaceY, std::uint32_t seed, int worldX,
                               int worldZ) {
    // The dry test has one owner and this is not it; only the temperature
    // question differs between the two forms.
    if (precipitationFor(biome, surfaceY) == Precipitation::None) {
        return Precipitation::None;
    }
    return freezesAt(seed, biomeInfo(biome).warmth, worldX, surfaceY, worldZ) ? Precipitation::Snow
                                                                             : Precipitation::Rain;
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
    // **Floored, not truncated.** C++'s `%` takes the sign of its left operand,
    // so a negative argument left `m_forced` negative - which is not 0, so
    // every `m_forced == 0` guard in `update` fails and the weather cycle
    // freezes for the rest of the session with no way back to it. Neither live
    // caller can reach it today (`Main.cpp` casts a settings enum, and the
    // debug key passes `forced() + 1` off a value this function already
    // clamped into 0-3), so it was a trap for the next caller rather than a
    // live bug. It costs one token to close and the `+ 4` is only correct
    // because the modulus is 4; if that ever changes, change both.
    m_forced = ((state % 4) + 4) % 4;
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

void Weather::restore(bool raining, bool thundering, float rainSeconds, float thunderSeconds) {
    // Off disk, so the two timers are not trusted. Negative, infinite and NaN
    // all become zero, which makes `update` roll a fresh countdown on the next
    // tick - the same thing a genuinely expired timer does, so a corrupt save
    // degrades into an ordinary cycle rather than into a frozen one. NaN is the
    // clause that earns this: `m_rainSeconds <= 0.0f` is *false* for NaN, so an
    // unguarded NaN counts down forever and the weather never changes again.
    const auto sane = [](float seconds) {
        return std::isfinite(seconds) && seconds > 0.0f ? seconds : 0.0f;
    };
    m_rainOn = raining;
    // **Taken as given, not filtered against `raining`.** The two countdowns
    // above are independent, so thunder-on with rain-off is a state `update`
    // reaches by itself; `storming()` merely reports false for it. Silencing it
    // here would drop the flag, and the thunder timer would then expire and
    // turn it *on*, putting a reloaded world in the opposite phase to the one
    // it was saved in.
    m_thunderOn = thundering;
    m_rainSeconds = sane(rainSeconds);
    m_thunderSeconds = sane(thunderSeconds);

    // Bolts are a second of screen flash, not save state, and anything left in
    // the air belongs to the world being left rather than the one being loaded.
    m_strikes.clear();

    // **The ramps are snapped, not faded, and this reversed an earlier call.**
    //
    // A load is a resumption, not a transition: the storm was already at full
    // when the player saved and they did not watch it arrive, so fading in from
    // calm animates an event that never happened.
    //
    // The first version of `restore` left these to fade, and the note that
    // defended it gave two supports. Both have since failed. It said snapping
    // "needed a second copy of `update`'s target expressions" - answered by
    // hoisting them into `rainLevelTarget`, `thunderLevelTarget` and
    // `windSpeedTarget`, which is the better structure anyway. And it said the
    // fade "bought a fade nobody objected to", which was true only while
    // **nothing read the wind**; `Main.cpp`'s `weatherWind` and its cloud deck
    // now do.
    //
    // That second one is a real defect and not a tidy-up. Wind ramps at
    // `kWindRampPerSecond`, about six times slower than the levels, so loading
    // into a saved thunderstorm gave **heavy rain over perfectly still trees
    // for roughly half a minute** - `windSpeed()` starts at its calm default of
    // 1.0 and `Main.cpp` subtracts exactly 1.0 as its calm floor, so the sway
    // it computes starts at literally zero while the rain is at full in five
    // seconds.
    //
    // Order matters: `windSpeedTarget` is a function of the two levels rather
    // than the two flags, so the levels are assigned first and the wind last.
    m_rainLevel = rainLevelTarget();
    m_thunderLevel = thunderLevelTarget();
    // Cover sits at the rain's level, which is where `update`'s own floor would
    // drag it on the next tick regardless. The lead that makes the deck gather
    // *before* rain starts is a prediction about a countdown, not a stored
    // quantity, so it is not reconstructed here - if the save happened inside
    // that window `update` re-enters it on the next tick by itself.
    m_cloudLevel = m_rainLevel;
    m_windSpeed = windSpeedTarget();
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

    const float rainTarget = rainLevelTarget();
    const float thunderTarget = thunderLevelTarget();
    m_rainLevel = approach(m_rainLevel, rainTarget, kLevelRampPerSecond, deltaSeconds);
    m_thunderLevel = approach(m_thunderLevel, thunderTarget, kLevelRampPerSecond, deltaSeconds);

    // **Staggered on purpose, and the stagger is a lead rather than a limp.**
    // If the sky, the light and the deck all move over the same five seconds it
    // reads as someone dragging a slider; the cover taking half a minute is
    // what makes the weather feel like it has a cause. But a slow ramp that
    // *starts* with the first drop is still lockstep, because the floor below
    // drags it up - so the deck is told the rain is coming and begins gathering
    // `kCoverageLeadSeconds` before the flag turns over.
    //
    // Only while the cycle is actually running: a frozen or forced countdown is
    // not a prediction of anything, and the debug key wants weather now.
    const bool gathering =
        cycle && m_forced == 0 && !m_rainOn && m_rainSeconds <= kCoverageLeadSeconds;
    m_cloudLevel = approach(m_cloudLevel, m_rainOn || gathering ? 1.0f : 0.0f,
                            kCoverageRampPerSecond, deltaSeconds);
    // Rain cannot fall out of a clear sky. With the lead doing its job the deck
    // is already ahead and this never fires; it is here for the forced path,
    // which turns the rain on with no warning at all.
    if (m_rainOn && m_cloudLevel < m_rainLevel) {
        m_cloudLevel = m_rainLevel;
    }

    // Calm, breezy, gale. Deliberately a single number: the deck's drift, the
    // rain's slant and anything added later all read it, so they cannot
    // disagree about which way the weather is going. The expression itself now
    // lives in `windSpeedTarget`, so `restore` can snap to it without keeping a
    // second copy - which is what it had to do before, and why it did not.
    const float windTarget = windSpeedTarget();
    m_windSpeed = approach(m_windSpeed, windTarget, kWindRampPerSecond, deltaSeconds);

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
    // **Before the early-out, deliberately.** This is not about lightning: it is
    // the whole of what the world needs to know about the weather, and `strike`
    // is the only member that is handed a `World&` and the only one `Main.cpp`
    // calls unconditionally every frame. Everything below returns on the first
    // `if`, and snow settles in ordinary snowfall, so a push placed after any of
    // them would only ever fire during a thunderstorm.
    //
    // Kind-agnostic on purpose. `World::m_precipitating` already carries "rain
    // is wetting the player's own column", computed in `Main.cpp` and *false*
    // by construction wherever it is cold enough to snow; this is the other
    // fact, "something is falling out of the sky, world-wide", and the world
    // decides per column which of the two it is by asking `precipitationFor`.
    // One of those bits cannot do the other's job, which is why there are two.
    world.setWeatherFalling(m_rainOn && m_rainLevel > kFallingLevel);

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
    // is why lightning never strikes a cold or dry biome. Asked of the column,
    // so it is `freezesAt`'s own answer including the jitter - the jitter-free
    // form put bolts on frozen peaks within a couple of blocks of the snow line.
    if (precipitationFor(sampleBiome(world.seed(), x, z).dominant, top, world.seed(), x, z) !=
        Precipitation::Rain) {
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
