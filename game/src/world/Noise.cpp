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

std::uint32_t hashCoords3(std::uint32_t seed, std::int32_t x, std::int32_t y, std::int32_t z) {
    const auto ux = static_cast<std::uint32_t>(x);
    const auto uy = static_cast<std::uint32_t>(y);
    const auto uz = static_cast<std::uint32_t>(z);
    return hash(seed ^ hash(ux * 0x9e3779b9u) ^ hash(uy * 0xc2b2ae35u) ^ hash(uz * 0x85ebca6bu));
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

float value3D(std::uint32_t seed, float x, float y, float z) {
    const float floorX = std::floor(x);
    const float floorY = std::floor(y);
    const float floorZ = std::floor(z);
    const auto cellX = static_cast<std::int32_t>(floorX);
    const auto cellY = static_cast<std::int32_t>(floorY);
    const auto cellZ = static_cast<std::int32_t>(floorZ);

    const float fadeX = fade(x - floorX);
    const float fadeY = fade(y - floorY);
    const float fadeZ = fade(z - floorZ);

    const auto corner = [&](int dx, int dy, int dz) {
        return unitFloat(hashCoords3(seed, cellX + dx, cellY + dy, cellZ + dz));
    };

    const float z0 = lerp(lerp(corner(0, 0, 0), corner(1, 0, 0), fadeX),
                          lerp(corner(0, 1, 0), corner(1, 1, 0), fadeX), fadeY);
    const float z1 = lerp(lerp(corner(0, 0, 1), corner(1, 0, 1), fadeX),
                          lerp(corner(0, 1, 1), corner(1, 1, 1), fadeX), fadeY);

    return lerp(z0, z1, fadeZ);
}

float fbm3D(std::uint32_t seed, float x, float y, float z, int octaves) {
    float total = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float normalisation = 0.0f;

    for (int octave = 0; octave < octaves; ++octave) {
        total += amplitude * value3D(seed + static_cast<std::uint32_t>(octave) * 0x9e3779b9u, x * frequency,
                                     y * frequency, z * frequency);
        normalisation += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return normalisation > 0.0f ? total / normalisation : 0.0f;
}

float octaves2D(std::uint32_t seed, float x, float z, const float* amplitudes, int count) {
    float total = 0.0f;
    float normalisation = 0.0f;
    float frequency = 1.0f;

    for (int i = 0; i < count; ++i) {
        const float amplitude = amplitudes[i];
        // A skipped octave still advances the frequency, or the array would
        // mean something different depending on where the zeros fall.
        if (amplitude != 0.0f) {
            const float sample = value2D(seed + static_cast<std::uint32_t>(i) * 0x9e3779b9u, x * frequency,
                                         z * frequency);
            total += amplitude * (sample * 2.0f - 1.0f);
            normalisation += amplitude;
        }
        frequency *= 2.0f;
    }

    return normalisation > 0.0f ? total / normalisation : 0.0f;
}

float octaves3D(std::uint32_t seed, float x, float y, float z, const float* amplitudes, int count) {
    float total = 0.0f;
    float normalisation = 0.0f;
    float frequency = 1.0f;

    for (int i = 0; i < count; ++i) {
        const float amplitude = amplitudes[i];
        if (amplitude != 0.0f) {
            const float sample = value3D(seed + static_cast<std::uint32_t>(i) * 0x9e3779b9u, x * frequency,
                                         y * frequency, z * frequency);
            total += amplitude * (sample * 2.0f - 1.0f);
            normalisation += amplitude;
        }
        frequency *= 2.0f;
    }

    return normalisation > 0.0f ? total / normalisation : 0.0f;
}

std::uint32_t hash2D(std::uint32_t seed, std::int32_t x, std::int32_t z) {
    return hashCoords(seed, x, z);
}

float hashUnit2D(std::uint32_t seed, std::int32_t x, std::int32_t z) {
    return unitFloat(hashCoords(seed, x, z));
}

float hashUnit3D(std::uint32_t seed, std::int32_t x, std::int32_t y, std::int32_t z) {
    return unitFloat(hashCoords3(seed, x, y, z));
}

} // namespace game::noise
