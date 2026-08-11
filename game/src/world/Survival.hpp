#pragma once

#include "item/Item.hpp"

#include <algorithm>
#include <cstdint>

/// Health, hunger and the things that take them away.
///
/// **The single owner of every survival constant**, the same way `Fluid.hpp`
/// owns every water constant - and for the same reason, which is that a second
/// copy of a number that already has a home is the most repeated bug in this
/// codebase. The player reads these, the HUD reads these, and nothing writes
/// its own.
///
/// Numbers are Bedrock's, from `RESEARCH.md` §3. Where a figure is recalled
/// from the wiki rather than quoted in that section it is marked, because a
/// plausible number with no source is exactly the kind that survives being
/// wrong.
namespace game::survival {

/// Ten hearts, twenty half-hearts. Health is counted in half-hearts because
/// that is the unit every damage figure in the reference is quoted in.
constexpr int kMaxHealth = 20;
constexpr int kMaxFood = 20;

/// **The rule that makes melee survivable, and the first thing to build.**
///
/// After any damage an entity is invulnerable for ten ticks. During that window
/// a blow less than or equal to the original is ignored outright, and a larger
/// one deals only the *difference*. The timer is not reset by either.
///
/// Without it a creature standing inside you kills in a fraction of a second,
/// and single-target damage is capped at two hits a second however fast
/// anything swings.
constexpr float kInvulnerableSeconds = 0.5f;

/// How long the screen stays tinted after a hit. Cosmetic, and deliberately
/// shorter than the invulnerability so the two are not confused for each other.
constexpr float kHurtFlashSeconds = 0.35f;

// --- Falling. §1.10.

/// Fall damage is `floor((distance - safe) * multiplier)` and is measured in
/// **change in Y, not speed** - so a long fall broken by water costs nothing at
/// all, and slow descent is no defence on its own.
constexpr float kSafeFallDistance = 3.0f;
constexpr float kFallDamagePerBlock = 1.0f;

// --- Hazards. §3.1's table, converted from "per half second" to an interval.

/// Two health a second while the breath meter is past empty, and the interval
/// never shortens.
constexpr float kDrownDamagePerSecond = 2.0f;

/// A cell whose block overlaps the eye. One point every half second.
constexpr int kSuffocationDamage = 1;
constexpr float kSuffocationInterval = 0.5f;

/// Standing in fire. Separate from *being on fire*, which continues after you
/// leave it.
constexpr int kFireDamage = 1;
constexpr float kFireInterval = 0.5f;

/// Lava is four points every half second, which is eight a second - by a wide
/// margin the most dangerous thing in the world.
constexpr int kLavaDamage = 4;
constexpr float kLavaInterval = 0.5f;

/// Being alight after leaving the flame. One point a second, and the reference
/// gives lava a far longer burn than fire does.
constexpr int kBurnDamage = 1;
constexpr float kBurnInterval = 1.0f;
constexpr float kFireBurnSeconds = 8.0f;
constexpr float kLavaBurnSeconds = 15.0f;

/// Touching a cactus. The reference charges this per contact tick; ours uses
/// the same half-second cadence as the other contact hazards, because a
/// per-frame charge is frame-rate damage and that trap has been sprung here
/// before.
constexpr int kCactusDamage = 1;
constexpr float kCactusInterval = 0.5f;

/// **The one cadence every contact hazard is actually charged on.**
///
/// Four separate timers would interleave into a much faster stream than any of
/// them is meant to be, so there is one - but the code used to run it off
/// `kLavaInterval`, which made lava's figure silently the owner of drowning's
/// pace as well. The per-hazard constants above stay because each records what
/// the reference charges for that hazard; the assertion is what stops one of
/// them being changed and quietly doing nothing.
constexpr float kHazardInterval = 0.5f;
static_assert(kSuffocationInterval == kHazardInterval && kFireInterval == kHazardInterval &&
                  kLavaInterval == kHazardInterval && kCactusInterval == kHazardInterval,
              "every contact hazard shares one timer, so they must all be charged at one rate");

/// Below the world. Instant and unconditional - not reduced, not survivable.
constexpr float kVoidDepth = -8.0f;

// --- Hunger. §3.2, and the design note there is worth keeping in mind: hunger
// --- is not a food timer, it is a tax on healing and hurrying.

/// Each time exhaustion reaches this it resets and costs one saturation, or one
/// food if saturation is already gone.
constexpr float kExhaustionPerLevel = 4.0f;

constexpr float kExhaustSprintPerMetre = 0.1f;
constexpr float kExhaustSwimPerMetre = 0.01f;
constexpr float kExhaustJump = 0.05f;
constexpr float kExhaustSprintJump = 0.2f;
constexpr float kExhaustBreakBlock = 0.005f;
constexpr float kExhaustAttack = 0.1f;
constexpr float kExhaustDamaged = 0.1f;
/// **By far the most expensive thing a player can do.** Healing ten hearts
/// costs sixty exhaustion, which is fifteen food.
constexpr float kExhaustPerHealed = 6.0f;

/// Walking is free. Removed from the reference in 1.11 and not reinstated.
constexpr float kExhaustWalkPerMetre = 0.0f;

/// Sprinting needs more than six food; regeneration needs eighteen; nothing at
/// all starves.
constexpr int kSprintFoodFloor = 6;
constexpr int kRegenFoodFloor = 18;

/// Half a health point every four seconds while fed.
constexpr float kRegenInterval = 4.0f;
constexpr int kRegenAmount = 1;

/// **Saturation healing**: at a full food bar with saturation left, one point
/// every half second at a cost of one and a half saturation. It is why cooked
/// meat feels so much better than its hunger number suggests.
constexpr float kSaturatedRegenInterval = 0.5f;
constexpr float kSaturatedRegenCost = 1.5f;

/// Starving takes a point every four seconds and **stops at one** on Normal,
/// which is the difficulty we have. It cannot kill.
constexpr float kStarveInterval = 4.0f;
constexpr int kStarveFloor = 1;

/// How long a death lasts before the world hands control back.
constexpr float kRespawnSeconds = 1.6f;

/// What eating one of something is worth.
struct FoodValue {
    int hunger = 0;
    float saturation = 0.0f;
};

/// The food table. Hunger and saturation for everything edible.
///
/// The first block is quoted directly in `RESEARCH.md` §3.3. The rest are
/// recalled wiki figures for foods that section does not list, and are marked
/// as such - they are the ordinary members of families whose other members *are*
/// quoted, so they are low-risk, but they are not sourced the same way.
constexpr FoodValue foodValue(ItemId item) {
    switch (item) {
    // §3.3, quoted.
    case ItemId::CookedPorkchop:
    case ItemId::CookedBeef:
        return {8, 12.8f};
    case ItemId::CookedChicken:
    case ItemId::CookedMutton:
    case ItemId::CookedSalmon:
        return {6, 9.6f};
    case ItemId::Bread:
    case ItemId::BakedPotato:
    case ItemId::CookedCod:
    case ItemId::CookedRabbit:
        return {5, 6.0f};
    case ItemId::GoldenApple:
        return {4, 9.6f};
    case ItemId::Apple:
        return {4, 2.4f};
    case ItemId::RottenFlesh:
        return {4, 0.8f};
    case ItemId::Carrot:
        return {3, 3.6f};
    case ItemId::RawBeef:
    case ItemId::RawPorkchop:
        return {3, 1.8f};
    case ItemId::MelonSlice:
        return {2, 1.2f};
    case ItemId::RawChicken:
        return {2, 1.2f};
    case ItemId::Potato:
        return {1, 0.6f};

    // Wiki figures, not in §3.3.
    case ItemId::RawMutton:
        return {2, 1.2f};
    case ItemId::RawCod:
        return {2, 0.4f};
    case ItemId::Cookie:
        return {2, 0.4f};
    case ItemId::SweetBerries:
    case ItemId::GlowBerries:
        return {2, 0.4f};
    case ItemId::Beetroot:
        return {1, 1.2f};
    case ItemId::PumpkinPie:
        return {8, 4.8f};
    case ItemId::RawRabbit:
        return {3, 1.8f};
    case ItemId::SpiderEye:
        return {2, 3.2f};
    case ItemId::HoneyBottle:
        return {6, 1.2f};
    case ItemId::DriedKelp:
        return {1, 0.6f};
    case ItemId::RawSalmon:
        return {2, 0.4f};
    case ItemId::RawTropicalFish:
    case ItemId::RawPufferfish:
        return {1, 0.2f};
    case ItemId::ChorusFruit:
        return {4, 2.4f};
    // The appended foods. The four bowls are the reference's own, and all four
    // are worth more than anything you can eat without cooking - which is the
    // whole reason to keep a bowl.
    case ItemId::MushroomStew:
    case ItemId::RabbitStew:
    case ItemId::BeetrootSoup:
    case ItemId::SuspiciousStew:
        return {6, 7.2f};
    case ItemId::EnchantedGoldenApple:
        return {4, 9.6f};
    // Deliberately still worth something: the reference's poisonous potato
    // feeds you and then poisons you, so a zero here would make it inert
    // rather than a gamble.
    case ItemId::PoisonousPotato:
        return {2, 1.2f};
    case ItemId::GoldenCarrot:
        return {6, 14.4f};
    default:
        return {0, 0.0f};
    }
}

/// Whether eating this would do anything. **Not the same question as
/// `isFood`**: that one answers "is this edible" for the catalogue and the
/// smelting table, and a food with no value here would be silently swallowed
/// for nothing.
constexpr bool isEdible(ItemId item) {
    return foodValue(item).hunger > 0;
}

/// Proves the two answers name the same set, at compile time.
///
/// `isFood` decides a catalogue tab and what a furnace will cook; `isEdible`
/// decides whether holding right-click does anything. They are different
/// questions asked of the same items, which is precisely the arrangement where
/// one gets a new row and the other does not - an item that reads as food and
/// cannot be eaten, or one that can be eaten and never appears among the foods.
/// Twenty lines of loop instead of a bug nobody would look for.
constexpr bool foodTablesAgree() {
    for (int raw = 0; raw <= static_cast<int>(ItemId::kLastItem); ++raw) {
        const auto item = static_cast<ItemId>(raw);
        if (item == ItemId::None) {
            continue;
        }
        if (isFood(item) != isEdible(item)) {
            return false;
        }
    }
    return true;
}

static_assert(foodTablesAgree(),
              "every item the catalogue calls food must restore hunger, and the other way round");

/// How long holding right-click takes to finish a meal. The reference's own,
/// and the same for everything except the two it makes instant, neither of
/// which exists here.
constexpr float kEatSeconds = 1.6f;

/// Saturation can never exceed the food bar, which is what stops a golden apple
/// on an empty stomach banking more than it should.
constexpr float clampSaturation(float saturation, int food) {
    return std::min(saturation, static_cast<float>(food));
}

} // namespace game::survival
