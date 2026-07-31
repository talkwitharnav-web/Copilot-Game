#include "world/Biome.hpp"

#include "world/Noise.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

// Very low frequency: regions should be hundreds of blocks across, so crossing
// one is a journey rather than a step.
constexpr float kSelectionFrequency = 0.0022f;
constexpr int kSelectionOctaves = 2;

/// How far in selection space a biome still contributes. Wide enough that
/// neighbours overlap and blend, narrow enough that each keeps its character.
constexpr float kBlendRadius = 0.55f;

/// Out of reach for biomes that should never see snow.
constexpr int kNoSnow = 4096;

constexpr std::array<Biome, static_cast<std::size_t>(BiomeId::Count)> kBiomes{{
    // name           top               filler            depth base   amp   snow     temp   humid  trees
    {"Ocean", BlockId::Gravel, BlockId::Stone, 3, 6.0f, 12.0f, kNoSnow, 0.50f, 0.95f, 0.00f},
    {"Beach", BlockId::Sand, BlockId::Sand, 4, 23.0f, 3.0f, kNoSnow, 0.70f, 0.80f, 0.00f},
    {"Plains", BlockId::Grass, BlockId::Dirt, 4, 26.0f, 11.0f, kNoSnow, 0.58f, 0.50f, 0.16f},
    {"Desert", BlockId::Sand, BlockId::Sand, 5, 26.0f, 9.0f, kNoSnow, 0.92f, 0.12f, 0.00f},
    {"Rocky", BlockId::Gravel, BlockId::Stone, 3, 28.0f, 20.0f, 58, 0.34f, 0.22f, 0.02f},
    {"Mountains", BlockId::Stone, BlockId::Stone, 3, 32.0f, 41.0f, 52, 0.26f, 0.66f, 0.05f},
    {"Snowy Peaks", BlockId::Snow, BlockId::Dirt, 4, 36.0f, 46.0f, 34, 0.06f, 0.40f, 0.00f},
}};

/// Value noise clusters around the middle, so the raw field would never reach
/// the extreme corners of selection space and the hot-dry and cold-wet biomes
/// would never appear. This stretches it back out.
float spread(float value) {
    return std::clamp((value - 0.5f) * 1.9f + 0.5f, 0.0f, 1.0f);
}

} // namespace

const Biome& biomeInfo(BiomeId id) {
    const auto index = static_cast<std::size_t>(id);
    return kBiomes[std::min(index, kBiomes.size() - 1)];
}

float maxTreeDensity() {
    // Derived rather than written down, so adding a leafier biome cannot
    // silently make the placement rejection wrong.
    static const float highest = [] {
        float best = 0.0f;
        for (const Biome& biome : kBiomes) {
            best = std::max(best, biome.treeDensity);
        }
        return best;
    }();
    return highest;
}

BiomeSample sampleBiome(std::uint32_t seed, int worldX, int worldZ) {
    const float x = static_cast<float>(worldX) * kSelectionFrequency;
    const float z = static_cast<float>(worldZ) * kSelectionFrequency;

    // Two independent fields rather than one: a single value could only ever
    // order biomes along a line, which is why temperature alone cannot separate
    // desert from plains from tundra convincingly.
    const float temperature = spread(noise::fbm2D(seed ^ 0xb10a5e11u, x, z, kSelectionOctaves));
    const float humidity = spread(noise::fbm2D(seed ^ 0x4d01c3a7u, x, z, kSelectionOctaves));

    float totalWeight = 0.0f;
    float baseHeight = 0.0f;
    float amplitude = 0.0f;
    float bestWeight = -1.0f;
    auto dominant = BiomeId::Plains;

    for (std::size_t i = 0; i < kBiomes.size(); ++i) {
        const Biome& candidate = kBiomes[i];
        const float dt = temperature - candidate.temperature;
        const float dh = humidity - candidate.humidity;
        const float distance = std::sqrt(dt * dt + dh * dh);

        // Cubed so influence falls away sharply near the edge of the radius;
        // a linear falloff leaves every biome faintly present everywhere and
        // averages all of them into the same middling terrain.
        const float falloff = std::max(0.0f, 1.0f - distance / kBlendRadius);
        const float weight = falloff * falloff * falloff;

        if (weight > bestWeight) {
            bestWeight = weight;
            dominant = static_cast<BiomeId>(i);
        }
        if (weight <= 0.0f) {
            continue;
        }

        totalWeight += weight;
        baseHeight += weight * candidate.baseHeight;
        amplitude += weight * candidate.amplitude;
    }

    const Biome& winner = biomeInfo(dominant);
    if (totalWeight <= 0.0f) {
        // Nothing in range, which the radius makes unlikely but not impossible.
        return BiomeSample{dominant, winner.baseHeight, winner.amplitude, winner.snowLine};
    }

    return BiomeSample{dominant, baseHeight / totalWeight, amplitude / totalWeight, winner.snowLine};
}

} // namespace game
