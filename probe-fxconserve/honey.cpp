// Does a honey bottle actually reach `feedPlayer`, and does drinking one while
// poisoned cure the poison? The cure lives in `Player.cpp`'s `feedPlayer`, not
// at Main.cpp's eat site - so the only question this file owns is whether the
// dispatch gets it there. Main.cpp branches:
//
//     if (drinking || (isEdible(held.item) && ...))     <- the gate
//         if (drinking) { potion path, never feeds }
//         else          { feedPlayer(player, foodValue(item)) }
//
// so a honey bottle that answers true to `isDrinkablePotion` would be fed to
// nobody. This drives the real predicates.

#include "item/Item.hpp"
#include "world/Effects.hpp"
#include "world/Survival.hpp"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(const std::string& what, bool ok) {
    std::printf("  %-64s %s\n", what.c_str(), ok ? "ok" : "FAIL");
    if (!ok) {
        ++failures;
    }
}

} // namespace

int main() {
    std::printf("=== honey bottle: does the cure reach the drinker? ===\n\n");

    constexpr game::ItemId honey = game::ItemId::HoneyBottle;

    const bool drinkable = game::isDrinkablePotion(honey);
    const bool edible = game::survival::isEdible(honey);
    std::printf("  isDrinkablePotion(HoneyBottle) = %s\n", drinkable ? "true" : "false");
    std::printf("  isEdible(HoneyBottle)          = %s\n", edible ? "true" : "false");

    check("a honey bottle is NOT taken by the potion branch", !drinkable);
    check("a honey bottle IS taken by the food branch", edible);
    check("so it reaches feedPlayer", !drinkable && edible);

    const game::survival::FoodValue meal = game::survival::foodValue(honey);
    std::printf("\n  foodValue(HoneyBottle).hunger  = %d\n", meal.hunger);
    std::printf("  foodValue(HoneyBottle).removes = %d (Poison is %d)\n",
                static_cast<int>(meal.removes),
                static_cast<int>(game::effects::Effect::Poison));
    check("the row names Poison, which is what feedPlayer clears",
          meal.removes == game::effects::Effect::Poison);

    // The whole chain, driven end to end: poison a player's effect set, run what
    // feedPlayer runs, and see whether the poison is gone.
    game::effects::Effects fx;
    fx.apply(game::effects::Effect::Poison, 0, 30.0f);
    fx.apply(game::effects::Effect::Speed, 0, 30.0f);
    std::printf("\n  before: poison %.0fs, speed %.0fs\n",
                fx.secondsLeft(game::effects::Effect::Poison),
                fx.secondsLeft(game::effects::Effect::Speed));
    check("the probe can actually poison somebody",
          fx.secondsLeft(game::effects::Effect::Poison) > 0.0f);

    if (meal.removes != game::effects::Effect::None) {
        fx.clearOne(meal.removes);
    }
    std::printf("  after : poison %.0fs, speed %.0fs\n",
                fx.secondsLeft(game::effects::Effect::Poison),
                fx.secondsLeft(game::effects::Effect::Speed));
    check("the poison is gone", fx.secondsLeft(game::effects::Effect::Poison) == 0.0f);
    check("and it is NOT milk's clear-everything - speed survives",
          fx.secondsLeft(game::effects::Effect::Speed) > 0.0f);

    std::printf("\nCONTROLS\n");

    // Without this, "the poison is gone" could just mean clearOne wipes all.
    game::effects::Effects wither;
    wither.apply(game::effects::Effect::Wither, 0, 30.0f);
    wither.clearOne(game::effects::Effect::Poison);
    check("clearing poison leaves an unnamed harmful effect alone",
          wither.secondsLeft(game::effects::Effect::Wither) > 0.0f);

    // And without this, every `removes == Poison` above could be vacuous because
    // every food answers Poison.
    int removers = 0;
    for (int i = 0; i <= static_cast<int>(game::ItemId::kLastItem); ++i) {
        const game::ItemId id = static_cast<game::ItemId>(i);
        if (game::survival::foodValue(id).removes != game::effects::Effect::None) {
            ++removers;
            std::printf("  a food that removes something: id %d\n", i);
        }
    }
    std::printf("  foods with a removes row: %d\n", removers);
    check("the removes column is not just true of everything", removers >= 1 && removers < 8);

    std::printf("\n%s (%d failures)\n", failures == 0 ? "ALL PASS" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
