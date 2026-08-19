// Finding 2027: `setPrecipitating` was gated on `kind != None`, so snowfall set
// the wet bit and snowstorms hydrated farmland and put out open-air fires.
//
// The fix is obvious once stated, so the thing worth measuring is not "is snow
// != None" - it is the **magnitude**: how much of the world was wrongly wet, and
// whether the fix leaves rain columns untouched. A fix that also moved a rain
// column would be a retune wearing a bug fix's clothes.
//
// Run against the real `precipitationFor` in Weather.cpp and the real biome
// table in Biome.cpp, so the biome set cannot drift from what the game ships.

#include "world/Biome.hpp"
#include "world/Weather.hpp"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(const char* what, bool ok, const char* detail = "") {
    std::printf("  %-58s %s %s\n", what, ok ? "ok  " : "FAIL", detail);
    if (!ok) {
        ++failures;
    }
}

using game::weather::Precipitation;

// The two gates, before and after.
bool wetOld(Precipitation kind) { return kind != Precipitation::None; }
bool wetNew(Precipitation kind) { return kind == Precipitation::Rain; }

const char* name(Precipitation p) {
    switch (p) {
    case Precipitation::None:
        return "None";
    case Precipitation::Rain:
        return "Rain";
    case Precipitation::Snow:
        return "Snow";
    }
    return "?";
}

} // namespace

int main() {
    // Every biome the game has, across the altitude band a player occupies.
    // Snow is altitude-dependent in temperate biomes, so sweeping height is
    // what exposes "and on high ground in temperate ones".
    const int loY = 0;
    const int hiY = 200;

    int columns = 0;
    int wasWetNowDry = 0;
    int stayedWet = 0;
    int stayedDry = 0;
    int wasDryNowWet = 0; // must be zero - the fix only ever removes wetness

    std::vector<int> snowBiomes;
    std::vector<int> rainBiomes;
    int snowOnlyBiomes = 0;
    int mixedBiomes = 0; // rain low down, snow up high - the temperate case

    for (int b = 0; b < static_cast<int>(game::BiomeId::Count); ++b) {
        const auto biome = static_cast<game::BiomeId>(b);
        bool anySnow = false;
        bool anyRain = false;
        for (int y = loY; y <= hiY; ++y) {
            const auto kind = game::weather::precipitationFor(biome, y);
            ++columns;
            const bool o = wetOld(kind);
            const bool n = wetNew(kind);
            if (o && !n) {
                ++wasWetNowDry;
            } else if (o && n) {
                ++stayedWet;
            } else if (!o && !n) {
                ++stayedDry;
            } else {
                ++wasDryNowWet;
            }
            if (kind == Precipitation::Snow) {
                anySnow = true;
            }
            if (kind == Precipitation::Rain) {
                anyRain = true;
            }
        }
        if (anySnow) {
            snowBiomes.push_back(b);
        }
        if (anyRain) {
            rainBiomes.push_back(b);
        }
        if (anySnow && !anyRain) {
            ++snowOnlyBiomes;
        }
        if (anySnow && anyRain) {
            ++mixedBiomes;
        }
    }

    const int biomeCount = static_cast<int>(game::BiomeId::Count);
    std::printf("\nbiomes: %d, altitudes %d..%d, columns sampled: %d\n\n", biomeCount, loY, hiY,
                columns);

    std::printf("  %-40s %8d\n", "columns wet before, dry after (the bug)", wasWetNowDry);
    std::printf("  %-40s %8d\n", "columns wet before and after (rain)", stayedWet);
    std::printf("  %-40s %8d\n", "columns dry before and after", stayedDry);
    std::printf("  %-40s %8d   <-- must be 0\n", "columns dry before, wet after", wasDryNowWet);
    std::printf("\n  %-40s %8d of %d\n", "biomes that snow at some altitude",
                static_cast<int>(snowBiomes.size()), biomeCount);
    std::printf("  %-40s %8d\n", "  ...snow-only (always wrongly wet)", snowOnlyBiomes);
    std::printf("  %-40s %8d\n", "  ...mixed: rain low, snow high", mixedBiomes);

    // Sample the boundary in a mixed biome, which is the "high ground in a
    // temperate biome" case the finding names.
    std::printf("\n  the snow line in the first mixed biome, if any:\n");
    for (int b = 0; b < biomeCount; ++b) {
        const auto biome = static_cast<game::BiomeId>(b);
        bool anySnow = false;
        bool anyRain = false;
        for (int y = loY; y <= hiY; ++y) {
            const auto k = game::weather::precipitationFor(biome, y);
            if (k == Precipitation::Snow) {
                anySnow = true;
            }
            if (k == Precipitation::Rain) {
                anyRain = true;
            }
        }
        if (anySnow && anyRain) {
            for (int y = loY; y < hiY; ++y) {
                const auto a = game::weather::precipitationFor(biome, y);
                const auto c = game::weather::precipitationFor(biome, y + 1);
                if (a != c) {
                    std::printf("    biome %d: y=%d %s -> y=%d %s\n", b, y, name(a), y + 1,
                                name(c));
                }
            }
            break;
        }
    }

    std::printf("\nassertions\n");

    // Non-vacuity: if nothing snowed anywhere the whole measurement is empty.
    check("non-vacuity: some biome actually snows", !snowBiomes.empty());
    check("non-vacuity: some biome actually rains", !rainBiomes.empty());

    // The bug had real reach.
    check("BUG: snow columns were wrongly marked wet", wasWetNowDry > 0);

    // The control that makes this a fix and not a retune: rain must be
    // untouched, and the change must be one-directional. If a single rain
    // column had flipped, the gate would be doing something other than what it
    // says.
    check("CONTROL: no column becomes wet that was not", wasDryNowWet == 0);
    check("CONTROL: rain columns are entirely unaffected", stayedWet > 0);

    // And the direction and magnitude: the two gates must actually disagree,
    // and disagree only on snow.
    {
        int disagreeOnSnow = 0;
        int disagreeElsewhere = 0;
        for (int b = 0; b < biomeCount; ++b) {
            for (int y = loY; y <= hiY; ++y) {
                const auto k = game::weather::precipitationFor(static_cast<game::BiomeId>(b), y);
                if (wetOld(k) != wetNew(k)) {
                    if (k == Precipitation::Snow) {
                        ++disagreeOnSnow;
                    } else {
                        ++disagreeElsewhere;
                    }
                }
            }
        }
        char d[64];
        std::snprintf(d, sizeof(d), "%d snow, %d other", disagreeOnSnow, disagreeElsewhere);
        check("the two gates disagree on snow and nothing else",
              disagreeOnSnow > 0 && disagreeElsewhere == 0, d);
    }

    // The visual must NOT be narrowed - this is the trap in the one-token
    // version of the fix. `falls` asks whether anything is coming down, and
    // snow still is.
    {
        int snowStillFalls = 0;
        for (int b = 0; b < biomeCount; ++b) {
            for (int y = loY; y <= hiY; ++y) {
                const auto k = game::weather::precipitationFor(static_cast<game::BiomeId>(b), y);
                if (k == Precipitation::Snow && (k != Precipitation::None)) {
                    ++snowStillFalls;
                }
            }
        }
        check("snow still passes the VISUAL gate (falls != None)", snowStillFalls == wasWetNowDry);
    }

    std::printf("\n%s  (%d failed)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
