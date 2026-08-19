// Armour was inert: `PlayerInput::armour` had exactly one reference in the whole
// tree and it was `updateSurvival` READING it, so every blow landed against
// `kNoArmour`. This drives the real headers through both call sites Main.cpp was
// missing and measures what a blow costs on either side of the wiring.
//
// It reuses `Inventory.hpp`'s own `wearingSet` rather than rebuilding the
// `helmet + i` walk, because a second copy of a derivation is CLAUDE.md #5.

#include "item/Inventory.hpp"
#include "world/Survival.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(const std::string& what, bool ok) {
    std::printf("  %-62s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

bool nearly(float a, float b) { return (a > b ? a - b : b - a) < 0.001f; }

/// What Main.cpp does now, and what it did before. `wired == false` is the old
/// behaviour exactly: nothing ever assigned the field, so the default reached
/// `updateSurvival` and the default is `kNoArmour`.
float blowCost(const game::Inventory& inv, int damage, bool wired) {
    const game::survival::ArmourSet set =
        wired ? inv.armourSet() : game::survival::kNoArmour;
    return game::survival::armourDamageTaken(damage, set);
}

int wornTotal(const game::Inventory& inv) {
    int total = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        total += inv.armourAt(i).damage;
    }
    return total;
}

} // namespace

int main() {
    std::printf("=== armour: the two call sites Main.cpp was missing ===\n\n");

    struct Row {
        const char* name;
        game::ItemId helmet;
    };
    const Row rows[] = {{"leather", game::ItemId::LeatherHelmet},
                        {"iron", game::ItemId::IronHelmet},
                        {"diamond", game::ItemId::DiamondHelmet}};

    std::printf("A ten-point blow, before the wiring and after:\n");
    const float naked = blowCost(game::Inventory{}, 10, true);
    std::printf("  %-9s before %5.2f   after %5.2f\n", "naked", 10.0f, naked);
    check("a naked player is unchanged by the wiring", nearly(naked, 10.0f));

    for (const Row& row : rows) {
        const game::Inventory inv = game::wearingSet(row.helmet);
        const float before = blowCost(inv, 10, false);
        const float after = blowCost(inv, 10, true);
        std::printf("  %-9s before %5.2f   after %5.2f\n", row.name, before, after);
        check(std::string{row.name} + " took the full blow before the wiring",
              nearly(before, 10.0f));
        check(std::string{row.name} + " now takes less", after < before);
    }

    std::printf("\nCONTROLS\n");

    // Without this, every "after < before" above would only prove that filling
    // any slot helps.
    game::Inventory wrong;
    wrong.armourAt(3) = game::ItemStack{game::ItemId::DiamondHelmet, 1, 0};
    const float wrongCost = blowCost(wrong, 10, true);
    std::printf("  a diamond helmet in the boots slot: %5.2f\n", wrongCost);
    check("a piece in the wrong slot arms nobody", nearly(wrongCost, 10.0f));

    // And without this the equalities above could all be vacuous.
    check("the curve can say something other than 10.00",
          !nearly(blowCost(game::wearingSet(game::ItemId::DiamondHelmet), 10, true), 10.0f));

    std::printf("\nThe wear drain - nothing drained it, so it climbed forever:\n");
    int banked = 0;
    for (int blow = 0; blow < 5; ++blow) {
        banked += game::survival::armourDurabilityCost(10);
    }
    std::printf("  five ten-point blows bank %d points per piece\n", banked);
    check("a blow banks something to pay", banked > 0);

    game::Inventory worn = game::wearingSet(game::ItemId::IronHelmet);
    std::printf("  before the drain: %d damage across the set\n", wornTotal(worn));
    check("undrained is exactly today's behaviour - a set never wears",
          wornTotal(worn) == 0);
    const int broke = worn.wearArmour(banked);
    std::printf("  after  the drain: %d damage across the set, %d pieces broke\n",
                wornTotal(worn), broke);
    check("the drain wears the set", wornTotal(worn) > 0);
    check("and charges every piece, not one shared point",
          wornTotal(worn) == banked * 4);

    // CONTROL for the drain: zero must wear nothing, or the number above could
    // have come from the constructor.
    game::Inventory untouched = game::wearingSet(game::ItemId::IronHelmet);
    untouched.wearArmour(0);
    std::printf("  CONTROL draining zero: %d damage\n", wornTotal(untouched));
    check("draining zero wears nothing", wornTotal(untouched) == 0);

    // Armour carries no durability rows yet, so `wearArmour` cannot destroy and
    // the hudDirty branch at the call site is inert. Stated so a later reader
    // does not read the 0 as a bug.
    std::printf("  pieces destroyed by a %d-point charge: %d (no durability rows yet)\n",
                banked, broke);

    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
