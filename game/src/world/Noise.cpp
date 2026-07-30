#include "world/Noise.hpp"

#include <cmath>

namespace game::noise {
namespace {

/// Integer avalanche hash: one bit of input changes about half the output bits.
/// Deliberately integer-only so results cannot drift between compilers or
/// optimisation levels the way floating-point can.
std::uint32_t hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

std::uint32_t hashCoords(std::uint32_t seed, std::int32_t x, std::int32_t z) {
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uz = static_cast<std::uint32_t>(z);
    return hash(seed ^ hash(ux * 0x9e3779b9u) ^ hash(uz * 0x85ebca6bu));
}

/// Hash to [0, 1). Dropping the low bits keeps the better-mixed high bits.
float unitFloat(std::uint32_t h) {
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

/// Quintic fade. Its first and second derivatives are zero at both ends, so
/// interpolated cells meet without the faint grid creases a linear or cubic
/// blend leaves behind.
float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

} // namespace

float value2D(std::uint32_t seed, float x, float z) {
    const float floorX = std::floor(x);
    const float floorZ = std::floor(z);
    const auto cellX = static_cast<std::int32_t>(floorX);
    const auto cellZ = static_cast<std::int32_t>(floorZ);

    const float fadeX = fade(x - floorX);
    const float fadeZ = fade(z - floorZ);

    const float c00 = unitFloat(hashCoords(seed, cellX, cellZ));
    const float c10 = unitFloat(hashCoords(seed, cellX + 1, cellZ));
    const float c01 = unitFloat(hashCoords(seed, cellX, cellZ + 1));
    const float c11 = unitFloat(hashCoords(seed, cellX + 1, cellZ + 1));

    return lerp(lerp(c00, c10, fadeX), lerp(c01, c11, fadeX), fadeZ);
}

float fbm2D(std::uint32_t seed, float x, float z, int octaves) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float normalisation = 0.0f;

    for (int octave = 0; octave < octaves; ++octave) {
        // Offsetting the seed per octave stops the layers correlating, which
        // would otherwise produce visible repeating structure.
        total += amplitude * value2D(seed + static_cast<std::uint32_t>(octave) * 0x9e3779b9u, x * frequency,
                                     z * frequency);
        normalisation += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return normalisation > 0.0f ? total / normalisation : 0.0f;
}

} // namespace game::noise
