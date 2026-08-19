// Finding 1265 - Main.cpp must fill the effect and absorption fields that
// WorldStore.hpp grew for it (bug shape #15, a proved format with no caller).
//
// The interesting half is not the copy, it is `absorptionSeconds`. Player.cpp
// reconciles the absorption pool by comparing the Absorption effect's remaining
// seconds against the *previous frame's* value and treating a rise as "it was
// granted again". A load that restores the effect and the pool but leaves that
// companion at zero therefore looks, on the very first tick, exactly like a
// fresh golden apple - and refills a spent pool to full.
//
// This probe runs the real `game::effects::Effects` and the real
// `game::SavedPlayer` / `game::SavedEffect` records, transcribes Player.cpp's
// reconcile verbatim, and measures the pool across a save/load round trip
// three ways: the naive restore, the restore this fix actually landed, and a
// set of deliberately wrong rules that must all be caught.

#include "world/Effects.hpp"
#include "world/WorldStore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace fx = game::effects;

namespace {

int failures = 0;

void check(const char* what, bool ok) {
    std::printf("  %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

/// The player fields this question touches, and nothing else.
struct Pool {
    fx::Effects effects{};
    float absorption = 0.0f;
    float absorptionSeconds = 0.0f;
};

/// Transcribed from `Player::update` in `game/src/world/Player.cpp` - the
/// absorption reconcile, verbatim, including the order of the three steps.
void tickPool(Pool& p, float dt) {
    p.effects.tick(dt);
    const float absorptionLeft = p.effects.secondsLeft(fx::Effect::Absorption);
    if (absorptionLeft > p.absorptionSeconds) {
        p.absorption = std::max(p.absorption, fx::absorptionPoints(p.effects));
    }
    p.absorptionSeconds = absorptionLeft;
    if (absorptionLeft <= 0.0f) {
        p.absorption = 0.0f;
    }
}

/// Exactly what Main.cpp's `saveEverything` now writes.
game::SavedPlayer saveOf(const Pool& p) {
    game::SavedPlayer saved{};
    std::size_t written = 0;
    for (const fx::ActiveEffect& active : p.effects.all()) {
        if (active.id == fx::Effect::None || active.secondsLeft <= 0.0f) {
            continue;
        }
        saved.effects[written] = {static_cast<std::int32_t>(active.id), active.amplifier,
                                  active.secondsLeft};
        ++written;
    }
    saved.absorption = p.absorption;
    return saved;
}

/// Exactly what Main.cpp's startup now applies. `deriveSeconds` is the one
/// line under test: false is the naive restore, true is what landed.
Pool loadOf(const game::SavedPlayer& saved, bool deriveSeconds) {
    Pool p{};
    for (const game::SavedEffect& record : saved.effects) {
        if (record.id <= 0 || record.id >= static_cast<std::int32_t>(fx::Effect::Count)) {
            continue;
        }
        const auto id = static_cast<fx::Effect>(record.id);
        if (fx::effectInfo(id).name[0] == '\0') {
            continue;
        }
        if (!std::isfinite(record.secondsLeft) || record.secondsLeft <= 0.0f) {
            continue;
        }
        p.effects.apply(id, std::max(0, record.amplifier), record.secondsLeft);
    }
    p.absorption = std::clamp(std::isfinite(saved.absorption) ? saved.absorption : 0.0f, 0.0f,
                              fx::absorptionPoints(p.effects));
    if (deriveSeconds) {
        p.absorptionSeconds = p.effects.secondsLeft(fx::Effect::Absorption);
    }
    return p;
}

/// A player who ate an enchanted golden apple, took a beating, and has two
/// absorption points left out of the sixteen it gave.
Pool spentPlayer() {
    Pool p{};
    p.effects.apply(fx::Effect::Absorption, 3, 120.0f);
    p.effects.apply(fx::Effect::Regeneration, 1, 30.0f);
    p.effects.apply(fx::Effect::FireResistance, 0, 300.0f);
    tickPool(p, 0.05f); // the frame the apple was eaten: the grant fills the pool
    p.absorption = 2.0f; // and then sixteen points of it were spent
    return p;
}

void theExploitAndTheFix() {
    std::printf("A spent absorption pool across a quit and a reload\n");

    const Pool before = spentPlayer();
    std::printf("  before quitting: pool %.1f of a %.1f grant, %.2f s of effect left\n",
                static_cast<double>(before.absorption),
                static_cast<double>(fx::absorptionPoints(before.effects)),
                static_cast<double>(before.absorptionSeconds));
    check("the pool really is spent before the save", before.absorption == 2.0f);

    const game::SavedPlayer saved = saveOf(before);

    Pool naive = loadOf(saved, false);
    const float naiveAtLoad = naive.absorption;
    tickPool(naive, 0.05f);
    std::printf("  restored WITHOUT the derived companion: %.1f at load -> %.1f after one tick\n",
                static_cast<double>(naiveAtLoad), static_cast<double>(naive.absorption));
    check("BUG - the naive restore refills a spent pool to full", naive.absorption == 16.0f);

    Pool fixed = loadOf(saved, true);
    const float fixedAtLoad = fixed.absorption;
    tickPool(fixed, 0.05f);
    std::printf("  restored WITH it:                          %.1f at load -> %.1f after one tick\n",
                static_cast<double>(fixedAtLoad), static_cast<double>(fixed.absorption));
    check("FIXED - the pool comes back exactly as it was left", fixed.absorption == 2.0f);

    // And it must not have broken the thing the reconcile exists for: a real
    // grant after the load still has to fill the pool.
    fixed.effects.apply(fx::Effect::Absorption, 3, 120.0f);
    tickPool(fixed, 0.05f);
    std::printf("  then a fresh apple after the load:         %.1f\n",
                static_cast<double>(fixed.absorption));
    check("a genuine grant after a load still fills the pool", fixed.absorption == 16.0f);

    // Sixty reloads in a row must not accumulate anything.
    Pool repeated = spentPlayer();
    repeated.absorption = 2.0f;
    for (int i = 0; i < 60; ++i) {
        repeated = loadOf(saveOf(repeated), true);
        tickPool(repeated, 0.05f);
    }
    std::printf("  after sixty quit/reload cycles:            %.1f\n",
                static_cast<double>(repeated.absorption));
    check("sixty reloads conserve the pool exactly", repeated.absorption == 2.0f);
}

void everyStorableEffectSurvives() {
    std::printf("\nThe effect list itself, round-tripped\n");

    Pool p{};
    int applied = 0;
    for (int id = 1; id < static_cast<int>(fx::Effect::Count); ++id) {
        const auto effect = static_cast<fx::Effect>(id);
        // Only the storable set. `Effect` has two unnamed gaps at 24 and 25,
        // and `Effects::apply` will happily take one - `effectInfo` answers a
        // blank row whose `instant` flag is false, which is the only thing that
        // gate looks at. Two junk slots then crowd a real effect out of a table
        // sized at exactly one slot per *storable* id. Filed separately against
        // `Effects.hpp`; nothing in the game can reach it, because the loader
        // this probe is testing refuses an unnamed id by name.
        if (fx::effectInfo(effect).name[0] == '\0' || fx::effectInfo(effect).instant) {
            continue;
        }
        if (p.effects.apply(effect, id % 4, 10.0f + static_cast<float>(id))) {
            ++applied;
        }
    }
    std::printf("  applied %d of the 22 storable effects\n", applied);
    check("every storable effect fits on the live player", applied == 22);

    const Pool back = loadOf(saveOf(p), true);
    int matched = 0;
    int wrong = 0;
    for (int id = 1; id < static_cast<int>(fx::Effect::Count); ++id) {
        const auto effect = static_cast<fx::Effect>(id);
        const int wantLevel = p.effects.level(effect);
        const float wantSeconds = p.effects.secondsLeft(effect);
        if (back.effects.level(effect) != wantLevel) {
            std::printf("    MISMATCH %s: level %d -> %d\n", fx::effectInfo(effect).name,
                        wantLevel, back.effects.level(effect));
            ++wrong;
            continue;
        }
        if (std::fabs(back.effects.secondsLeft(effect) - wantSeconds) > 1e-4f) {
            std::printf("    MISMATCH %s: %.4f s -> %.4f s\n", fx::effectInfo(effect).name,
                        static_cast<double>(wantSeconds),
                        static_cast<double>(back.effects.secondsLeft(effect)));
            ++wrong;
            continue;
        }
        if (wantLevel > 0) {
            ++matched;
        }
    }
    std::printf("  %d effects came back with the same level and the same seconds, %d wrong\n",
                matched, wrong);
    check("all 22 survive with amplifier and duration intact", matched == 22 && wrong == 0);

    // The record has room for every slot a player can hold, which is the
    // static_assert the fix carries at the save site.
    check("the save record is at least as wide as the live effect table",
          static_cast<std::size_t>(fx::kMaxActive) <= game::kSavedEffectSlots);
}

void aCorruptFileIsRefused() {
    std::printf("\nBytes this run did not write\n");

    game::SavedPlayer saved{};
    saved.effects[0] = {0, 0, 50.0f};                                       // Effect::None
    saved.effects[1] = {static_cast<std::int32_t>(fx::Effect::Count), 0, 50.0f}; // out of range
    saved.effects[2] = {99999, 0, 50.0f};                                   // far out of range
    saved.effects[3] = {24, 0, 50.0f};                                      // a gap in the enum
    saved.effects[4] = {static_cast<std::int32_t>(fx::Effect::Speed), 0, -5.0f}; // expired
    saved.effects[5] = {static_cast<std::int32_t>(fx::Effect::Haste), 0,
                        std::numeric_limits<float>::quiet_NaN()};           // NaN
    saved.effects[6] = {static_cast<std::int32_t>(fx::Effect::InstantHealth), 0, 50.0f}; // instant
    saved.effects[7] = {static_cast<std::int32_t>(fx::Effect::Strength), 2, 50.0f};      // the one good row
    saved.absorption = 1.0e9f;

    const Pool p = loadOf(saved, true);
    int running = 0;
    for (int id = 1; id < static_cast<int>(fx::Effect::Count); ++id) {
        if (p.effects.level(static_cast<fx::Effect>(id)) > 0) {
            ++running;
        }
    }
    std::printf("  eight rows in, seven of them junk -> %d effect running, absorption %.1f\n",
                running, static_cast<double>(p.absorption));
    check("only the one good row survives", running == 1);
    check("and it is the right one, at the right level",
          p.effects.level(fx::Effect::Strength) == 3);
    check("a billion absorption hearts is clamped to the grant, which is none",
          p.absorption == 0.0f);

    // CONTROL: the same reader, fed a file that is entirely valid, must let it
    // all through - otherwise the zeros above would prove nothing but a broken
    // reader.
    game::SavedPlayer good{};
    good.effects[0] = {static_cast<std::int32_t>(fx::Effect::Speed), 1, 50.0f};
    good.effects[1] = {static_cast<std::int32_t>(fx::Effect::Absorption), 1, 50.0f};
    good.absorption = 1.0e9f;
    const Pool okay = loadOf(good, true);
    std::printf("  CONTROL, a valid file through the same reader: speed %d, absorption %.1f\n",
                okay.effects.level(fx::Effect::Speed), static_cast<double>(okay.absorption));
    check("CONTROL - the reader is not simply rejecting everything",
          okay.effects.level(fx::Effect::Speed) == 2 && okay.absorption == 8.0f);
}

void controlsOnTheSaveSide() {
    std::printf("\nControl: three wrong save rules, all of which must be caught\n");

    const Pool before = spentPlayer();

    // 1. Save the effects and forget the pool. The player comes back with the
    //    effect running and no hearts at all - which the reconcile then cannot
    //    refill, because the seconds did not rise.
    game::SavedPlayer noPool = saveOf(before);
    noPool.absorption = 0.0f;
    Pool a = loadOf(noPool, true);
    tickPool(a, 0.05f);
    std::printf("  pool not written:    %.1f (want 2.0)\n", static_cast<double>(a.absorption));
    check("CONTROL - forgetting the pool loses the hearts", a.absorption == 0.0f);

    // 2. Save the pool and forget the effects. Nothing is keeping the hearts
    //    alive, so the first tick takes them away.
    game::SavedPlayer noEffects{};
    noEffects.absorption = before.absorption;
    Pool b = loadOf(noEffects, true);
    tickPool(b, 0.05f);
    std::printf("  effects not written: %.1f (want 2.0)\n", static_cast<double>(b.absorption));
    check("CONTROL - forgetting the effects loses them too", b.absorption == 0.0f);

    // 3. Write the seconds as an absolute time rather than a remaining
    //    duration - the trap the finding names by name. A world reloaded four
    //    hours later comes back with the effect already expired.
    game::SavedPlayer asClock = saveOf(before);
    for (game::SavedEffect& record : asClock.effects) {
        if (record.id != 0) {
            record.secondsLeft -= 4.0f * 3600.0f;
        }
    }
    Pool c = loadOf(asClock, true);
    int stillRunning = 0;
    for (int id = 1; id < static_cast<int>(fx::Effect::Count); ++id) {
        if (c.effects.level(static_cast<fx::Effect>(id)) > 0) {
            ++stillRunning;
        }
    }
    std::printf("  seconds as a clock:  %d effects survive four hours away (want 3)\n",
                stillRunning);
    check("CONTROL - a time_point spelling loses every effect", stillRunning == 0);

    // And the rule actually used keeps all three, at the same four hours.
    const Pool d = loadOf(saveOf(before), true);
    int kept = 0;
    for (int id = 1; id < static_cast<int>(fx::Effect::Count); ++id) {
        if (d.effects.level(static_cast<fx::Effect>(id)) > 0) {
            ++kept;
        }
    }
    std::printf("  the rule used:       %d effects survive any absence\n", kept);
    check("the duration spelling survives an absence of any length", kept == 3);
}

} // namespace

int main() {
    std::printf("=== 1265: effects and absorption across a save ===\n\n");
    theExploitAndTheFix();
    everyStorableEffectSurvives();
    aCorruptFileIsRefused();
    controlsOnTheSaveSide();
    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
