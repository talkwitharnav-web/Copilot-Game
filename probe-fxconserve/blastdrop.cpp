// Finding 1038 - a charge drops one block in four. Probe before edit.
//
// The current roll site in Main.cpp is:
//     roll * blast.power < 1.0f
// which is P = 1/power for EVERYTHING. Correct for a creeper, wrong for a
// charge (the reference drops all of it) and wrong for the three blocks that
// always survive a blast.
//
// fx-mining landed `game::blast::explosionDropChance(BlockId, float, bool)` in
// Explosion.hpp with four static_asserts and NO caller. This measures the swap
// against the real header before a line of Main.cpp is touched.
//
// The control that matters is NOT "does TNT improve" - it obviously will. It is
// "does the CREEPER stay exactly where it was", because a fix that quietly
// retunes creepers while fixing charges is the bug shape #2 this project keeps
// paying for: widening a rule and killing the case already behind it.

#include "world/Explosion.hpp"

#include <cstdint>
#include <cstdio>

namespace {

// Transcribed from Main.cpp's blast loop, byte for byte, including the mask and
// the divisor - a probe that rolls differently from the game measures nothing.
std::uint32_t g_random = 12345u | 1u;

float nextRoll() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return static_cast<float>(g_random & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

bool currentRule(float roll, float power) { return roll * power < 1.0f; }

bool proposedRule(float roll, game::BlockId removed, float power, bool fromTnt) {
    return roll < game::blast::explosionDropChance(removed, power, fromTnt);
}

struct Result {
    double current;
    double proposed;
};

Result measure(game::BlockId block, float power, bool fromTnt, int trials) {
    int cur = 0;
    int prop = 0;
    for (int i = 0; i < trials; ++i) {
        const float roll = nextRoll();
        if (currentRule(roll, power)) {
            ++cur;
        }
        if (proposedRule(roll, block, power, fromTnt)) {
            ++prop;
        }
    }
    return {static_cast<double>(cur) / trials, static_cast<double>(prop) / trials};
}

int g_fail = 0;

void check(const char* what, bool ok) {
    std::printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        ++g_fail;
    }
}

} // namespace

int main() {
    constexpr int kTrials = 2000000;
    constexpr float kTnt = 4.0f;
    constexpr float kCreeper = 3.0f;
    constexpr float kCharged = 6.0f;

    std::printf("finding 1038 - blast drop identity, %d rolls per row\n\n", kTrials);
    std::printf("  %-34s %10s %10s   %s\n", "case", "current", "proposed", "reference");
    std::printf("  %s\n", "------------------------------------------------------------------------");

    const Result tnt = measure(game::BlockId::Stone, kTnt, true, kTrials);
    std::printf("  %-34s %10.4f %10.4f   %s\n", "stone, charge (power 4)", tnt.current,
                tnt.proposed, "1.0000 - a charge drops all of it");

    const Result creeper = measure(game::BlockId::Stone, kCreeper, false, kTrials);
    std::printf("  %-34s %10.4f %10.4f   %s\n", "stone, creeper (power 3)", creeper.current,
                creeper.proposed, "0.3333 - unchanged");

    const Result charged = measure(game::BlockId::Stone, kCharged, false, kTrials);
    std::printf("  %-34s %10.4f %10.4f   %s\n", "stone, charged creeper (power 6)",
                charged.current, charged.proposed, "0.1667 - unchanged");

    const Result egg = measure(game::BlockId::DragonEgg, kCreeper, false, kTrials);
    std::printf("  %-34s %10.4f %10.4f   %s\n", "dragon egg, creeper", egg.current, egg.proposed,
                "1.0000 - always survives");

    const Result beacon = measure(game::BlockId::Beacon, kCreeper, false, kTrials);
    std::printf("  %-34s %10.4f %10.4f   %s\n", "beacon, creeper", beacon.current, beacon.proposed,
                "1.0000 - always survives");

    const Result obsidian = measure(game::BlockId::Obsidian, kCreeper, false, kTrials);
    std::printf("  %-34s %10.4f %10.4f   %s\n", "obsidian, creeper", obsidian.current,
                obsidian.proposed, "0.3333 - takes its chances");

    std::printf("\n  what a 26-block charge yields: %.1f now -> %.1f after\n\n",
                26.0 * tnt.current, 26.0 * tnt.proposed);

    std::printf("assertions\n");
    check("a charge now drops everything it breaks", tnt.proposed > 0.9999);
    check("...and did not before - the bug is real", tnt.current < 0.30);

    // The control. If either of these moves, the fix has taken the creeper with
    // it and must not land.
    check("CONTROL creeper is UNCHANGED to 4 decimal places",
          std::abs(creeper.proposed - creeper.current) < 0.0001);
    check("CONTROL charged creeper is UNCHANGED",
          std::abs(charged.proposed - charged.current) < 0.0001);
    check("CONTROL creeper is still about one in three",
          creeper.proposed > 0.331 && creeper.proposed < 0.336);

    check("dragon egg always drops under the new rule", egg.proposed > 0.9999);
    check("beacon always drops under the new rule", beacon.proposed > 0.9999);

    // A second control: the exception list must not have been widened into
    // "everything", which is the failure a list of positives cannot detect.
    //
    // Compared against obsidian's OWN current rate, not against stone's. The
    // first version of this line compared the two blocks to each other and
    // FAILED at 0.3338 vs 0.3340 - a 0.6-sigma sampling difference, because
    // each measure() call consumes a different segment of the shared xorshift
    // stream. The expectation was wrong, not the code. Same-stream comparison
    // is exact and asks the real question: did the rule change for obsidian?
    check("CONTROL obsidian is NOT on the exception list",
          obsidian.proposed == obsidian.current);
    check("CONTROL obsidian differs from the dragon egg", obsidian.proposed < 0.5);
    check("CONTROL stone is likewise untouched for a creeper",
          creeper.proposed == creeper.current);

    // And a control on the probe itself: the two rules must be capable of
    // disagreeing, or every "unchanged" above is vacuous.
    check("CONTROL the two rules DO disagree somewhere (tnt)",
          std::abs(tnt.proposed - tnt.current) > 0.5);

    std::printf("\n%s\n", g_fail == 0 ? "ALL PASS" : "FAILURES ABOVE");
    return g_fail;
}
