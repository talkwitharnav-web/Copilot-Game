// Finding: two independent wind models, named by Weather.hpp's own comment
// ("The fix is three lines in `Main.cpp`, not here"). This measures the
// disagreement against the REAL Weather.cpp rather than a transcription of it,
// so the numbers cannot drift from the code they describe.
//
// The claim under test is not "Main.cpp computes wind differently" - that is
// visible by reading. It is the sharper one: **the cloud deck ignores the wind
// entirely**, so the deck and the rain slant are free to disagree, which is
// CLAUDE.md bug shape #1 (a value derived somewhere other than the table that
// owns it).

#include "world/Weather.hpp"

#include <algorithm>
#include <cmath>
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

// Main.cpp today, transcribed from the two lines under test.
constexpr float kCloudDriftPerSecond = 1.1f;
float oldWeatherWind(float rain, float thunder) { return rain * 4.0f + thunder * 5.0f; }
float oldCloudRate(float /*windSpeed*/) { return kCloudDriftPerSecond; }

// Main.cpp after the fix: one owner, read twice, each reader with its own gain
// and cap - which is what the slant, the bend and the particle push already do.
constexpr float kCalmWindSpeed = 1.0f;
constexpr float kMaxDeckWindFactor = 4.0f;
float newWeatherWind(float windSpeed) { return std::max(0.0f, windSpeed - kCalmWindSpeed); }
float newCloudRate(float windSpeed) {
    return kCloudDriftPerSecond * std::min(windSpeed, kMaxDeckWindFactor);
}

// The three existing readers, transcribed so the player-visible delta can be
// stated rather than guessed at. Rain falls at 10.0, snow at 1.2.
float slantOf(float windStrength, float fallSpeed) {
    return std::clamp(windStrength * 0.45f / fallSpeed, 0.0f, 0.35f);
}
float bendOf(float windStrength) { return std::min(0.22f, 0.016f * windStrength); }
float pushOf(float windStrength) { return windStrength * 0.35f; }

struct Series {
    std::vector<float> v;
    void add(float x) { v.push_back(x); }
    float min() const { return *std::min_element(v.begin(), v.end()); }
    float max() const { return *std::max_element(v.begin(), v.end()); }
    float span() const { return max() - min(); }
    double mean() const {
        double s = 0.0;
        for (float x : v) {
            s += x;
        }
        return s / static_cast<double>(v.size());
    }
    double variance() const {
        const double m = mean();
        double s = 0.0;
        for (float x : v) {
            s += (x - m) * (x - m);
        }
        return s / static_cast<double>(v.size());
    }
};

// Pearson r. Returns 2.0 as an out-of-band "undefined" when either side is
// constant - reporting a real number there would be the vacuous answer.
double correlation(const Series& a, const Series& b) {
    if (a.variance() == 0.0 || b.variance() == 0.0) {
        return 2.0;
    }
    const double ma = a.mean();
    const double mb = b.mean();
    double num = 0.0;
    for (std::size_t i = 0; i < a.v.size(); ++i) {
        num += (a.v[i] - ma) * (b.v[i] - mb);
    }
    num /= static_cast<double>(a.v.size());
    return num / (std::sqrt(a.variance()) * std::sqrt(b.variance()));
}

} // namespace

int main() {
    using game::weather::Weather;

    const float dt = 1.0f / 60.0f;
    Weather w(12345u);

    Series wind, rainS, thunderS;
    Series oldWind, oldCloud, newWind, newCloud;

    float minWindSpeed = 1e9f;

    auto run = [&](int state, float seconds) {
        w.force(state);
        const int steps = static_cast<int>(seconds / dt);
        for (int i = 0; i < steps; ++i) {
            w.update(dt, true);
            const float r = w.rainLevel();
            const float t = w.thunderLevel();
            const float s = w.windSpeed();
            minWindSpeed = std::min(minWindSpeed, s);
            rainS.add(r);
            thunderS.add(t);
            wind.add(s);
            oldWind.add(oldWeatherWind(r, t));
            oldCloud.add(oldCloudRate(s));
            newWind.add(newWeatherWind(s));
            newCloud.add(newCloudRate(s));
        }
    };

    run(2, 120.0f); // forced storm, long enough for the 0.35/s limiter to settle
    run(3, 120.0f); // forced clear, long enough to settle all the way back

    std::printf("\nsamples: %zu frames at %.4f s\n\n", wind.v.size(), dt);

    std::printf("signal ranges over the run\n");
    std::printf("  %-24s %8s %8s %8s\n", "", "min", "max", "span");
    std::printf("  %-24s %8.4f %8.4f %8.4f\n", "rainLevel", rainS.min(), rainS.max(), rainS.span());
    std::printf("  %-24s %8.4f %8.4f %8.4f\n", "thunderLevel", thunderS.min(), thunderS.max(),
                thunderS.span());
    std::printf("  %-24s %8.4f %8.4f %8.4f\n", "windSpeed() [the owner]", wind.min(), wind.max(),
                wind.span());
    std::printf("\n");
    std::printf("  %-24s %8.4f %8.4f %8.4f\n", "OLD wind (Main.cpp)", oldWind.min(), oldWind.max(),
                oldWind.span());
    std::printf("  %-24s %8.4f %8.4f %8.4f   <-- the bug\n", "OLD cloud rate", oldCloud.min(),
                oldCloud.max(), oldCloud.span());
    std::printf("  %-24s %8.4f %8.4f %8.4f\n", "NEW wind", newWind.min(), newWind.max(),
                newWind.span());
    std::printf("  %-24s %8.4f %8.4f %8.4f\n", "NEW cloud rate", newCloud.min(), newCloud.max(),
                newCloud.span());

    const double rOld = correlation(oldCloud, oldWind);
    const double rNew = correlation(newCloud, newWind);
    std::printf("\ncorrelation(cloud rate, wind strength)\n");
    if (rOld == 2.0) {
        std::printf("  today : UNDEFINED - the cloud rate is constant, zero variance\n");
    } else {
        std::printf("  today : %.6f\n", rOld);
    }
    std::printf("  fixed : %.6f\n", rNew);

    std::printf("\nassertions\n");

    // Non-vacuity first. Every number below is worthless if the weather never
    // actually moved - this is the check that the run exercised a real storm
    // AND a real calm, not a flat line either side.
    check("non-vacuity: storm reached full thunder", thunderS.max() > 0.99f);
    check("non-vacuity: run settled all the way back to clear", rainS.min() < 0.001f);
    check("non-vacuity: the owner's wind actually swung", wind.span() > 8.0f);

    // The bug, stated as a number: a 9-unit swing in wind that the deck does
    // not see at all.
    check("BUG: cloud rate has exactly zero variance today", oldCloud.variance() == 0.0);
    check("BUG: ...while wind strength swings over 8+ units", oldWind.span() > 8.0f);
    check("BUG: correlation is undefined today (constant deck)", rOld == 2.0);

    // The fix, in the expected direction and magnitude. The deck now saturates
    // at its cap, so the correlation is monotone-with-a-ceiling rather than a
    // straight line - the exact-1.0 claim belongs to the unsaturated region and
    // is asserted separately below.
    check("FIX: cloud rate now varies", newCloud.variance() > 0.0);
    check("FIX: deck and slant move together (r >= 0.9)", rNew >= 0.9);
    check("FIX: deck caps at 1.1 x 4 = 4.4, not a time-lapse",
          std::abs(newCloud.max() - 4.4f) < 1e-5f);

    // Below the cap the two are affine in the same variable, so they must agree
    // exactly. Measured only on frames where the deck is not saturated - a
    // correlation taken over the saturated frames too would understate a fix
    // that is in fact perfect where it is free to act.
    {
        Series a, b;
        for (std::size_t i = 0; i < wind.v.size(); ++i) {
            if (wind.v[i] < kMaxDeckWindFactor) {
                a.add(newCloud.v[i]);
                b.add(newWind.v[i]);
            }
        }
        const bool enough = a.v.size() > 100;
        check("FIX: below the cap, deck and slant agree exactly (r == 1)",
              enough && std::abs(correlation(a, b) - 1.0) < 1e-9);
    }

    // The identity that makes this a fix rather than a retune. `windSpeed`
    // rests at exactly 1.0 on a clear day, so both readers must land on
    // precisely today's clear-weather numbers - bit-identical, not merely
    // close. This is the control that mattered for 1038.
    const float sCalm = w.windSpeed();
    check("clear day: windSpeed() rests at exactly 1.0", sCalm == 1.0f);
    check("clear day: new wind == old wind, bit-identical",
          newWeatherWind(sCalm) == oldWeatherWind(w.rainLevel(), w.thunderLevel()));
    check("clear day: new cloud rate == old, bit-identical",
          newCloudRate(sCalm) == oldCloudRate(sCalm));

    // Safety of the subtraction: if windSpeed could dip below 1 the max() would
    // be clamping real signal rather than guarding an impossibility.
    check("windSpeed() never dips below 1.0, so max() never clamps", minWindSpeed >= 1.0f);

    // And the control that stops this being a rename: the owner's signal is
    // rate-limited at 0.35/s while Main.cpp's tracks the level ramps, so the
    // two are genuinely different shapes. If they matched, adopting the owner
    // would be cosmetic and this finding would not be worth an edit.
    double maxGap = 0.0;
    const double sOld = std::sqrt(oldWind.variance());
    const double sNew = std::sqrt(newWind.variance());
    for (std::size_t i = 0; i < oldWind.v.size(); ++i) {
        const double a = (oldWind.v[i] - oldWind.mean()) / sOld;
        const double b = (newWind.v[i] - newWind.mean()) / sNew;
        maxGap = std::max(maxGap, std::abs(a - b));
    }
    char detail[64];
    std::snprintf(detail, sizeof(detail), "max normalised gap %.3f", maxGap);
    check("CONTROL: owner's wind is a different shape, not a rename", maxGap > 0.25, detail);

    std::printf("\n%s  (%d failed)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);

    // What a player actually sees, at the two states that matter. Every one of
    // these is a number the fix moves or deliberately does not move.
    std::printf("\nplayer-visible delta, calm (windSpeed 1.0) then full storm (12.0)\n");
    std::printf("  %-22s %10s %10s   %10s %10s\n", "", "calm old", "calm new", "storm old",
                "storm new");
    const float cOld = oldWeatherWind(0.0f, 0.0f);
    const float cNew = newWeatherWind(1.0f);
    const float sOldW = oldWeatherWind(1.0f, 1.0f);
    const float sNewW = newWeatherWind(12.0f);
    std::printf("  %-22s %10.4f %10.4f   %10.4f %10.4f\n", "wind strength (no gust)", cOld, cNew,
                sOldW, sNewW);
    std::printf("  %-22s %10.4f %10.4f   %10.4f %10.4f\n", "cloud drift blocks/s",
                oldCloudRate(1.0f), newCloudRate(1.0f), oldCloudRate(12.0f), newCloudRate(12.0f));
    std::printf("  %-22s %10.4f %10.4f   %10.4f %10.4f\n", "rain slant", slantOf(cOld, 10.0f),
                slantOf(cNew, 10.0f), slantOf(sOldW, 10.0f), slantOf(sNewW, 10.0f));
    std::printf("  %-22s %10.4f %10.4f   %10.4f %10.4f\n", "snow slant", slantOf(cOld, 1.2f),
                slantOf(cNew, 1.2f), slantOf(sOldW, 1.2f), slantOf(sNewW, 1.2f));
    std::printf("  %-22s %10.4f %10.4f   %10.4f %10.4f\n", "foliage bend", bendOf(cOld),
                bendOf(cNew), bendOf(sOldW), bendOf(sNewW));
    std::printf("  %-22s %10.4f %10.4f   %10.4f %10.4f\n", "particle push", pushOf(cOld),
                pushOf(cNew), pushOf(sOldW), pushOf(sNewW));
    std::printf("\n  A cloud crosses a 64-block view in %.1f s calm, %.1f s in a full storm\n",
                64.0f / newCloudRate(1.0f), 64.0f / newCloudRate(12.0f));

    return failures == 0 ? 0 : 1;
}
