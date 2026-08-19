#pragma once

#include "item/Item.hpp"
#include "world/Effects.hpp"
#include "world/Tick.hpp"

#include <algorithm>
#include <array>
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
/// **Ten ticks, in seconds** - minecraft.wiki, *Damage*: "a mob or player turns
/// red for 10 game ticks (0.5 seconds)". It is pinned to that tick count beside
/// the hazards below, because every contact hazard's cadence *is* this window.
///
/// Without it a creature standing inside you kills in a fraction of a second,
/// and single-target damage is capped at two hits a second however fast
/// anything swings.
constexpr float kInvulnerableSeconds = 0.5f;

/// How long the screen stays tinted after a hit. Cosmetic, and deliberately
/// shorter than the invulnerability so the two are not confused for each other.
constexpr float kHurtFlashSeconds = 0.35f;

/// **The damage-immunity rule itself, in the one file both entities can depend
/// on.**
///
/// **This is the rule's new shared home, and it is not a de-duplication - read
/// that carefully, because the difference is a behaviour change.** At HEAD only
/// the *player* had a damage window: `damagePlayer` carried the whole rule and
/// `Player` carried the `{invulnerableSeconds, lastDamage}` pair. Creatures had
/// nothing of the kind - every creature damage site subtracted from `health`
/// unconditionally - and `Creature` gains both fields in the same round as this
/// function. So there was one copy to move here, not two to merge.
///
/// **What that means for anything hitting a creature:** within
/// `kInvulnerableSeconds` of a hit landing, a second blow no larger than the
/// first now does nothing at all, and a larger one lands only the difference.
/// Fast weapons therefore lose roughly half their effect against mobs, a pack
/// converging on one target has most of its blows swallowed, and a creature that
/// has just taken fall damage is briefly immune. That is the reference's
/// behaviour - Bedrock mobs have the same ten-tick window, `RESEARCH.md` §2.4 -
/// and it is wanted, but it is new here rather than merely relocated, and the
/// thing to measure after touching this function is creature time-to-kill.
///
/// **Takes the two fields by reference, not the entity.** A `Player&` or
/// `Creature&` parameter would make this file depend on both and invert the
/// dependency. The two parameters are different types, so they cannot be passed
/// the wrong way round.
///
/// **Returns the raw amount that got through, and zero when the blow is
/// swallowed.** §2.4: the comparison is made *before* armour, enchantments and
/// effects - so the window remembers the raw blow, and a hit resisted down to
/// nothing still arms the window and still flashes. Scaling stays with the
/// caller on purpose: `effects::damageTakenScale` reads a `Player`'s effects and
/// a `Creature` has no effects at all, so a hook here would be one half the
/// callers could not satisfy.
///
/// `bypass` is for damage that ignores the window entirely - the void, and
/// anything else that must not become survivable by standing still. It neither
/// reads the pair nor arms it.
///
/// `bypass` is for damage that ignores the window entirely - the void, and
/// anything else that must not become survivable by standing still. It neither
/// reads the pair nor arms it.
constexpr int chargeDamageWindow(float& invulnerableSeconds, int& lastDamage, int amount,
                                 bool bypass = false) {
    if (amount <= 0) {
        return 0;
    }
    if (bypass) {
        return amount;
    }

    // **Captured before the comparison**, because the arming below depends on
    // whether a window was already running and the comparison does not change
    // that.
    const bool inWindow = invulnerableSeconds > 0.0f;
    int landed = amount;
    if (inWindow) {
        // §2.4, quoted: no larger than the blow that armed the window is
        // ignored outright, larger lands only the difference. **The timer is
        // not restarted by either** - it used to be, and a stream of escalating
        // blows, each larger than the last so each landing its difference,
        // re-armed the full half second every time and held the victim immune
        // indefinitely.
        if (amount <= lastDamage) {
            return 0;
        }
        landed = amount - lastDamage;
    } else {
        invulnerableSeconds = kInvulnerableSeconds;
    }

    // **Assigned rather than `max`ed, and that is a fix, not a tidy-up.** Inside
    // the window the two agree, because we only get here when the blow is the
    // larger. Outside it, a fresh window must remember the blow that armed
    // *it*, and both call sites wrote `max`.
    //
    // That was not merely theoretical. `Player.cpp`'s hazard loop back-dates
    // the window it just armed by however late the tick was
    // (`invulnerableSeconds -= lateBy`) and **does not touch `lastDamage`**; a
    // frame hitch worth a second of hazard time runs that loop twice, so
    // `lateBy` reaches `kInvulnerableSeconds` and drives the window to exactly
    // zero with a large `lastDamage` still standing. The pair really does come
    // apart, in shipped code, and under `max` the next weaker blow - the poison
    // tick immediately below it, an arrow, a zombie - was swallowed by a window
    // that had already expired. The reference lands it.
    lastDamage = amount;
    return landed;
}

/// The other half of the same rule: running the window down.
///
/// **The pair is cleared together or not at all.** `lastDamage` outliving its
/// window suppresses the next real hit, so both fields belong to one function
/// for the same reason `chargeDamageWindow` exists. **Also not a
/// de-duplication**: `tickPassiveTimers` in `Player.cpp` ran this pair down at
/// HEAD, and the creature half is new alongside the window it serves.
constexpr void tickDamageWindow(float& invulnerableSeconds, int& lastDamage,
                                float deltaSeconds) {
    invulnerableSeconds = std::max(0.0f, invulnerableSeconds - deltaSeconds);
    if (invulnerableSeconds <= 0.0f) {
        lastDamage = 0;
    }
}

/// Walks §2.4's own worked example, plus the three ways the rule has been got
/// wrong here before.
///
/// Fails on:
///
/// - **Restarting the timer on the overwrite path** - moving
///   `invulnerableSeconds = kInvulnerableSeconds;` out of the `else`. This is
///   what `damagePlayer` did until this session, and it holds a victim immune
///   forever under escalating blows.
/// - **`lastDamage = std::max(lastDamage, amount)`** instead of the assignment,
///   which is what `damagePlayer` carried at HEAD and what the creature code
///   written beside this function copied from it. It is tempting to read that as
///   equivalent; it is not, and the caller that breaks it is real rather than
///   imagined - `Player.cpp`'s hazard loop zeroes the window without clearing
///   `lastDamage`, which is the shape the last case below stands in for. A
///   stale larger value then swallows a blow the window has no business
///   stopping.
/// - Charging the window for a bypassing blow, or forgetting to clear
///   `lastDamage` when the clock runs out.
constexpr bool damageWindowFollowsTheRule() {
    float seconds = 0.0f;
    int last = 0;

    // A blow with no window running lands whole and arms one.
    if (chargeDamageWindow(seconds, last, 7) != 7 || seconds != kInvulnerableSeconds ||
        last != 7) {
        return false;
    }

    // Half the window has run off. `kInvulnerableSeconds` halves exactly in
    // binary, so these comparisons are exact rather than lucky.
    tickDamageWindow(seconds, last, kInvulnerableSeconds * 0.5f);
    if (seconds != kInvulnerableSeconds * 0.5f || last != 7) {
        return false;
    }

    // §2.4's worked example: hit for 7, then for 12 while invulnerable, and the
    // second deals 5 - with the clock left exactly where it was.
    if (chargeDamageWindow(seconds, last, 12) != 5 || seconds != kInvulnerableSeconds * 0.5f ||
        last != 12) {
        return false;
    }

    // Equal is ignored, and so is smaller, and neither disturbs the pair.
    if (chargeDamageWindow(seconds, last, 12) != 0 || chargeDamageWindow(seconds, last, 1) != 0 ||
        seconds != kInvulnerableSeconds * 0.5f || last != 12) {
        return false;
    }

    // A bypassing blow lands whole and touches nothing.
    float voidSeconds = kInvulnerableSeconds;
    int voidLast = 20;
    if (chargeDamageWindow(voidSeconds, voidLast, 3, true) != 3 ||
        voidSeconds != kInvulnerableSeconds || voidLast != 20) {
        return false;
    }

    // Running out clears both halves.
    tickDamageWindow(seconds, last, kInvulnerableSeconds);
    if (seconds != 0.0f || last != 0) {
        return false;
    }

    // And the charge does not lean on that having happened. This is
    // `Player.cpp` after a frame hitch: the hazard loop armed a window for a
    // big blow, then back-dated it to zero for being late, leaving the 19
    // behind. The next blow must land in full and the window must remember
    // *it*.
    float stale = 0.0f;
    int staleLast = 19;
    if (chargeDamageWindow(stale, staleLast, 4) != 4 || stale != kInvulnerableSeconds ||
        staleLast != 4) {
        return false;
    }

    // ...and this is the harm that follows from getting the line above wrong: a
    // 5 now lands the 1 it should. Had the stale 19 survived, the whole blow
    // would have been swallowed by a window that had already expired.
    if (chargeDamageWindow(stale, staleLast, 5) != 1 || staleLast != 5) {
        return false;
    }

    return true;
}

static_assert(damageWindowFollowsTheRule(),
              "inside the window a blow no larger than the last is ignored and a larger one "
              "lands only the difference, the timer is never restarted, and a window that has "
              "run out remembers nothing");

// --- Falling. §1.10.

/// Fall damage is `floor((distance - safe) * multiplier)` and is measured in
/// **change in Y, not speed** - so a long fall broken by water costs nothing at
/// all, and slow descent is no defence on its own.
///
/// **Blocks, and health per block.** minecraft.wiki, *Damage*:
/// `falldamage = Max(0, Floor[(falldistance - safe_fall_distance) *
/// fall_damage_multiplier])`, with `safe_fall_distance` 3 and the multiplier 1.
/// No edition tag, so it is Bedrock's formula as well. Already printed against
/// three Bedrock anchors and verified - 3 to 0, 10 to 7, 23 to 20 and lethal -
/// so **do not re-derive this curve**; the two scales that modify it are kept
/// deliberately distinct in `Player.cpp` (a bed scales the *distance*, honey
/// the *damage*) and swapping them gives 5 and 18 where it should give 17 and 7.
constexpr float kSafeFallDistance = 3.0f;
constexpr float kFallDamagePerBlock = 1.0f;

// --- Hazards. §3.1's table, converted from "per half second" to an interval.

// **Drowning is deliberately not here, and a constant for it must not come
// back.** `Fluid.hpp` owns the breath meter, so it owns what running out of one
// costs: `fluid::kDrownDamage` every `fluid::kDrownInterval`. This file carried
// a second copy of that figure - a bare `kDrownDamagePerSecond = 2.0f` - and it
// stayed right only because nothing read it. `Player.cpp` derives the per-tick
// hit from the `fluid::` pair and asserts it divides evenly into
// `kHazardInterval`; a constant restored here would be a copy again the moment
// either side moved, and this file's own header says why that is the most
// repeated bug in the codebase.

/// **The wiki publishes every one of these as "N health every T *ticks*" and
/// this file counts in seconds**, so the conversion gets a function rather than
/// happening in somebody's head. Every interval below is written down twice -
/// once as the seconds the code uses, once as the published tick count in the
/// assert - and `CLAUDE.md` bug shape 3 is exactly the slip that catches: a
/// number ported into a field measured in a different unit compiles, validates,
/// and is invisible.
constexpr float fromTicks(int ticks) { return static_cast<float>(ticks) * tick::kSeconds; }

/// What a hazard costs per second, which is the figure the wiki quotes in prose
/// ("lava deals 4 damage every half-second") and the only one a player feels.
/// Used for the absolute anchors below.
constexpr float perSecond(int damage, float intervalSeconds) {
    return static_cast<float>(damage) / intervalSeconds;
}

/// A cell whose block overlaps the eye. **1 health every 10 ticks**, seconds.
/// minecraft.wiki, *Damage*: "Suffocation occurs when a player or mob's eyeline
/// is inside of certain blocks, receiving 1 damage every half-second." No
/// edition tag, so Bedrock's too.
constexpr int kSuffocationDamage = 1;
constexpr float kSuffocationInterval = 0.5f;

/// Standing in fire. Separate from *being on fire*, which continues after you
/// leave it. **1 health every 10 ticks**, seconds. minecraft.wiki, *Fire*:
/// inside a fire block it is 1 per tick, "reduced to once every half-second" by
/// the damage-immunity window - which is `kHazardInterval` below.
constexpr int kFireDamage = 1;
constexpr float kFireInterval = 0.5f;

/// Lava is four points every half second, which is eight a second - by a wide
/// margin the most dangerous thing in the world.
///
/// **The amount is Mojang's own**: `player.json` in `Mojang/bedrock-samples`
/// carries `minecraft:hurt_on_condition` with a single condition -
/// `{"cause": "lava", "damage_per_tick": 4, "filters": {"test": "in_lava"}}`.
///
/// **The interval is not in that field and must not be read out of its name.**
/// `damage_per_tick` says 4 and the player does not take 80 a second; what
/// paces it is the shared damage window, `kInvulnerableSeconds`, which is why
/// this sits at `kHazardInterval` with every other contact hazard rather than
/// carrying a rate of its own. minecraft.wiki, *Lava*, says the same thing the
/// long way round: "4 damage every tick (although damage immunity reduces this
/// to once every half-second)."
constexpr int kLavaDamage = 4;
constexpr float kLavaInterval = 0.5f;

/// Being alight after leaving the flame. **1 health every 20 ticks**, seconds -
/// minecraft.wiki, *Fire*, "on fire but outside a fire block: 1 damage per
/// second" - and the reference gives lava a far longer burn than fire does.
///
/// The two burn lengths are **seconds**, from the reference's own tick fields:
/// a fire sets `Fire` to **160 ticks** and lava sets `remainingFireTicks` to
/// **300** (minecraft.wiki, *Fire* and *Lava*). Both are asserted below against
/// those tick counts rather than left as bare 8 and 15.
constexpr int kBurnDamage = 1;
constexpr float kBurnInterval = 1.0f;
constexpr float kFireBurnSeconds = 8.0f;
constexpr float kLavaBurnSeconds = 15.0f;

/// Touching a cactus. **1 health every 10 ticks**, seconds. The reference
/// charges this per contact tick and the immunity window reduces it to the half
/// second (minecraft.wiki, *Cactus*); ours uses the same half-second cadence as
/// the other contact hazards, because a per-frame charge is frame-rate damage
/// and that trap has been sprung here before.
constexpr int kCactusDamage = 1;
constexpr float kCactusInterval = 0.5f;

/// Standing **on** a magma block.
///
/// "Standing on a magma block deals 1 fire damage every tick... in practice
/// reduced to once every half-second because of the damage-immunity window";
/// **sneaking prevents it**, touching the sides does not hurt at all, and it
/// works perfectly well underwater (minecraft.wiki, *Magma Block*).
///
/// **Derived from `kFireDamage` rather than written down as another 1.** The
/// reference calls this fire damage and Fire Resistance negates it, so it is
/// the same point fire charges - and one edit to fire's figure should not leave
/// a magma block quietly disagreeing with it. It gets no interval of its own
/// for the same reason: the half second it lands on is the shared cadence
/// below, which *is* the immunity window the wiki is describing.
constexpr int kMagmaDamage = kFireDamage;

/// Standing **in** a lit campfire's own block.
///
/// **The unit is damage per APPLICATION, not per tick, and getting that wrong
/// is a one-second death.** minecraft.wiki, *Campfire*: "Campfires deal 1HP
/// every tick (although damage immunity reduces this to once every
/// half-second)." Read literally the first clause is 20 HP a second and kills a
/// full-health player in half of one; the parenthesis is the whole meaning, and
/// what it describes is `kInvulnerableSeconds` - a window this file already
/// owns. So the charge is one point per application on the shared cadence, the
/// same shape as magma above, and it needs no interval of its own.
/// `Campfire.hpp` flagged this exact contradiction before anyone spent it and
/// asked that whoever landed the number state its unit; this is that statement.
///
/// **Derived from `kFireDamage`, not written as another 1**, for magma's
/// reason: the reference calls it fire damage and Fire Resistance negates it
/// outright, so it is the same point fire charges.
///
/// **But it is NOT armour-reduced, and that is the one place it parts company
/// with the other four.** Same source: "Damage taken is considered fire damage,
/// so **armor itself does not reduce damage caused by campfire**; to do so, the
/// player needs the Resistance potion effect, or the Protection or Fire
/// Protection enchantments." So `Player.cpp` charges this with `kNoArmour`
/// while lava, fire and magma pass `armour` - which is why the comment there
/// reading "the four hazards armour reduces, and the only four" is still exactly
/// true with a fifth fire hazard now present.
///
/// **It does not set you alight either.** "Campfires do not cause lasting
/// burning" - so nothing here touches `burningSeconds`, and a campfire is the
/// one fire source you can step off and simply stop taking damage from.
///
/// **Occupying, not standing on.** "The campfire deals damage only to entities
/// occupying its block", so this is read in the block scan beside `inFire`
/// rather than from the block underfoot the way magma is.
constexpr int kCampfireDamage = kFireDamage;

/// Touching a wither rose.
///
/// "When not in Peaceful difficulty, wither roses inflict the Wither effect to
/// any players or non-immune mobs touching it, dealing 1 damage every half
/// second" (minecraft.wiki, *Wither Rose*). A potted one is harmless, and this
/// is **not** fire damage, so Fire Resistance does nothing to it.
///
/// The reference's own 1-second lingering after you step off is *not* modelled:
/// that is the Wither effect outliving the contact, and the player record
/// carries no clock for it. Contact itself is charged exactly, so the rose
/// hurts - it just stops hurting the instant you leave it.
constexpr int kWitherRoseDamage = 1;
constexpr float kWitherRoseInterval = 0.5f;

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
                  kLavaInterval == kHazardInterval && kCactusInterval == kHazardInterval &&
                  kWitherRoseInterval == kHazardInterval,
              "every contact hazard shares one timer, so they must all be charged at one rate");

/// **Is every interval above the published *tick* count converted to seconds?**
///
/// Taken as arguments rather than read off the constants, so the negative twin
/// below can hand it the tick counts themselves and be refused - which is the
/// whole argument, and the shape `world/FaceGeometry.hpp` already proves. An
/// assert that only compared each constant against `fromTicks(...)` of a number
/// typed beside it would pass just as happily with both halves wrong.
constexpr bool hazardIntervalsAreSeconds(float suffocation, float fire, float lava, float cactus,
                                         float witherRose, float burn, float fireBurn,
                                         float lavaBurn, float window) {
    return suffocation == fromTicks(10) && fire == fromTicks(10) && lava == fromTicks(10) &&
           cactus == fromTicks(10) && witherRose == fromTicks(10) && burn == fromTicks(20) &&
           fireBurn == fromTicks(160) && lavaBurn == fromTicks(300) && window == fromTicks(10);
}

static_assert(hazardIntervalsAreSeconds(kSuffocationInterval, kFireInterval, kLavaInterval,
                                        kCactusInterval, kWitherRoseInterval, kBurnInterval,
                                        kFireBurnSeconds, kLavaBurnSeconds, kInvulnerableSeconds),
              "every hazard interval is the wiki's published tick count converted at 20 Hz: ten "
              "ticks for the contact hazards and for the damage window they share, twenty for "
              "burning, and 160/300 for how long a fire and lava leave you alight");
// **The negative twin, and it is the whole argument.** Hand the same function
// the wiki's own tick counts, which is precisely the slip being guarded
// against, and it has to refuse every one of them. Fails the moment somebody
// "simplifies" `fromTicks` into an identity.
static_assert(!hazardIntervalsAreSeconds(10.0f, 10.0f, 10.0f, 10.0f, 10.0f, 20.0f, 160.0f, 300.0f,
                                         10.0f),
              "a published tick count dropped straight into a seconds field must be refused");

/// **The absolute anchors**, in health per second, which is the figure the wiki
/// states in prose and the one a player actually feels. Every relation above is
/// between two things this file owns and would survive both of them moving
/// together; these will not, which is what makes them worth having.
static_assert(perSecond(kLavaDamage, kLavaInterval) == 8.0f &&
                  perSecond(kFireDamage, kFireInterval) == 2.0f &&
                  perSecond(kSuffocationDamage, kSuffocationInterval) == 2.0f &&
                  perSecond(kCactusDamage, kCactusInterval) == 2.0f &&
                  perSecond(kWitherRoseDamage, kWitherRoseInterval) == 2.0f &&
                  perSecond(kMagmaDamage, kHazardInterval) == 2.0f &&
                  perSecond(kBurnDamage, kBurnInterval) == 1.0f,
              "lava is eight health a second and the rest of the contact hazards two, with "
              "burning alone at one - the reference's own published rates");

// --- Freezing. **Landed 2026-08-19**, and it was the last damage source the
// --- reference had and this file did not - suffocation, drowning, lava, fire,
// --- burning, falling, starvation, cactus, magma and the void were all already
// --- here and verified. `Player.cpp` reads every constant below, and as of
// --- ~11:00 the same evening the block table and the worldgen landed too, so
// --- this is live in a generated world rather than waiting on anything. The
// --- leather exemption was the last unwired input; `Main.cpp` filled it at
// --- ~11:27, so the feature is now complete end to end.

/// **WIRED 2026-08-19 (evening). `Player.cpp` is the caller.** This note read
/// "NOTHING CALLS THESE, AND THAT IS DELIBERATE" until freezing was implemented,
/// and a negative claim is the class that rots fastest - so it is replaced
/// rather than amended. `updateSurvival` accumulates `Player::freezeSeconds`
/// while any cell the body occupies holds `BlockId::PowderSnow` and spends
/// `kFreezeDamage` on `Player::freezeTimer` once the onset is reached.
///
/// **That wait is over, and this note claimed otherwise for about an hour.** It
/// read "`Block.hpp` gives powder snow a full collision box ... the branch below
/// is correct and unreachable". Both halves are false as of 2026-08-19 ~11:00,
/// checked in source rather than carried from a ledger: `Block.hpp` asserts
/// `collisionBoxes(BlockId::PowderSnow).count == 0` with a non-empty selection
/// box and `!isSolid`, and `Biome.cpp` places it on two biome rows as
/// `.patch = BlockId::PowderSnow` with `.patchThreshold = 0.30f`. **So the
/// branch below runs in a generated world**, and a reader who believed the old
/// sentence would not have gone looking for a freezing death they could not
/// explain. Cited by symbol, not by line, deliberately - the line numbers that
/// stood here were written this evening and the file they point into is another
/// agent's. Falsified if a grep for `.patch = BlockId::PowderSnow` returns
/// fewer than two rows, or if that collision-box assert is relaxed.
///
/// **The exemption landed at ~11:27, and this paragraph said it had not.** It
/// read *"Nothing writes `PlayerInput::leatherArmour` or `::leatherBoots` ...
/// Powder snow has no counterplay at all today"* - a negative claim about a
/// gameplay hole, which is the shape that sends the next reader off to build a
/// second one. `Main.cpp` now sets both beside `move.armour =
/// inventory.armourSet();`; grep `move.leatherArmour` for the pair. So
/// `wearingLeather` is genuinely variable, a leather cap stops the clock and
/// thaws it, and boots hold you up. **Do not add a second filling site.**
///
/// **Still true, and the reason the two flags exist:** do *not* infer leather
/// from `ArmourSet`, which carries only `defence` and `toughness` - full leather
/// and iron leggings-plus-boots are both 7, so the set cannot name its material.
///
/// **The instrument that produced the old "zero" is worth recording, and this
/// note's first diagnosis of it was itself wrong.** The claim was about two
/// `PlayerInput` fields; the control counted 194 member reads on the *raycast*
/// struct, and this paragraph first called that "a true number about the wrong
/// symbol". **That is the wrong criticism.** An absence claim can never have a
/// same-symbol control - the entire content of the claim is that there is
/// nothing there to measure - so a different-symbol control is not merely
/// permitted, it is the only evidence available. The diagnostic that sorts the
/// two kinds: *if this control came out the other way, would the CLAIM be false
/// or the INSTRUMENT broken?* "Claim false" means it must test the same symbol;
/// "instrument broken" means it must test different ones.
///
/// **Its real defects were three, and each is cheap to avoid.** It carried no
/// known-answer negative probe, so a zero carried no information; it printed no
/// count of how many probes landed, so a blind detector could not be told from
/// an absent thing; and its subjects were *reads* on another struct elsewhere in
/// the tree when the claim was about *writes* to `move.<field>` in one function.
/// Re-run with the nine sibling `PlayerInput` members - same file, same query,
/// same access shape, differing only in the member name - **11 of 11 probes land
/// and the two claim symbols return 1 each, not 0.** A control must test the
/// same symbol kind as the claim, or it is vacuous while every digit checks out.
///
/// **What would falsify this note:** more than one assignment to
/// `move.leatherArmour`, or `Player.cpp` ceasing to OR the two flags.
///
/// **Sources, and the split matters.** `Mojang/bedrock-samples` does *not*
/// publish these, and that is a checked negative rather than a failed lookup:
/// `behavior_pack/entities/player.json` was fetched again on 2026-08-19 and its
/// `minecraft:hurt_on_condition` carries **exactly one** condition, `cause:
/// "lava"` with `damage_per_tick: 4`. There is no freezing entry and no leather
/// rule anywhere in that file. (The same fetch re-confirmed
/// `minecraft:exhaustion_values` row for row, including `walk: 0.0`, and
/// `minecraft:breathable` - so the file is readable and the absence is real.)
/// So freezing is engine-side in Bedrock and the numbers below come from the
/// **Bedrock-only changelog** for beta 1.16.230.54, which transcribes the
/// official release notes and so cannot be edition-confused: *"Freeze time has
/// decreased to 7 seconds and hurt frequency has decreased to 2 seconds."*
/// minecraft.wiki *Powder Snow* states the same pair in both units - "After
/// seven seconds (140 game ticks) ... the player begins taking damage at a rate
/// of 1HP every two seconds (40 game ticks)" - and Java is identical, so there
/// is no edition to be wrong about here.
constexpr float kFreezeOnsetSeconds = 7.0f;
constexpr float kFreezeInterval = 2.0f;
/// **The one figure here that is not Bedrock-primary.** The 1-health magnitude
/// is stated only in untagged wiki prose (*Powder Snow*, "the player begins
/// taking damage at a rate of 1 every two seconds"), never in a Bedrock-only
/// source. It is strongly corroborated - the same Bedrock changelog gives
/// blaze, magma cube and strider *five*, a figure that is only meaningful
/// relative to a base of one - but it is labelled here rather than passed off
/// as confirmed.
constexpr int kFreezeDamage = 1;

/// **How fast the freeze wears off: twice as fast as it went on.** Published as
/// a tick rate rather than a duration, and it is the one part of this mechanic a
/// reader would otherwise invent. minecraft.wiki, *Powder Snow*: the
/// `TicksFrozen` tag "increases by 1 every tick (to a maximum of 140) for an
/// entity within the powder snow block. It decreases at a rate of 2 per tick
/// after the entity leaves" - so this is a **ratio of two tick rates**, which is
/// dimensionless and therefore the one number in this block that survives a tick
/// -rate change untouched.
///
/// It is what makes freezing a hazard you walk out of rather than one you
/// escape: step out at full freeze and you are clear in three and a half
/// seconds, and step back in before that and you resume from where you were,
/// which is the sentence immediately after it on the same page.
constexpr float kFreezeRecoveryRate = 2.0f;
static_assert(kFreezeRecoveryRate > 1.0f,
              "the freeze counter must fall faster than it rises - 2 ticks off against 1 tick "
              "on (minecraft.wiki, Powder Snow); equal rates would make a full freeze cost as "
              "long to shed as it did to earn");

/// The same tick-versus-seconds guard the hazards above get, and freezing needs
/// it more than they do because **its two intervals are the ones a reader is
/// most likely to meet as bare tick counts**: every wiki sentence about
/// freezing quotes "140 game ticks" and "40 game ticks" alongside the seconds.
///
/// Taken as arguments so the negative twin can feed it those very numbers and
/// be refused, per `world/FaceGeometry.hpp`.
constexpr bool freezeIntervalsAreSeconds(float onset, float interval) {
    return onset == fromTicks(140) && interval == fromTicks(40);
}
static_assert(freezeIntervalsAreSeconds(kFreezeOnsetSeconds, kFreezeInterval),
              "freezing's onset and cadence are the published 140 and 40 tick counts converted "
              "at 20 Hz, not the tick counts themselves");
static_assert(!freezeIntervalsAreSeconds(140.0f, 40.0f),
              "the published tick counts dropped straight into seconds fields must be refused");
/// The absolute anchor, in health per second, so the pair cannot drift together.
static_assert(perSecond(kFreezeDamage, kFreezeInterval) == 0.5f,
              "freezing is half a health point a second - a quarter of fire's rate, which is what "
              "makes it a slow hazard you can walk out of rather than a contact hazard");
/// **Freezing is NOT on `kHazardInterval` and must never be folded into it.**
/// Every contact hazard shares one 10-tick window; freezing is 40 and is timed
/// from its own 140-tick onset. Adding it to that assert would force one of the
/// two to change.
static_assert(kFreezeInterval != kHazardInterval,
              "freezing runs on its own cadence and must not be swept onto the shared contact "
              "hazard timer");

/// **Who does not freeze, and it is two different rules wearing one word.**
/// minecraft.wiki, *Powder Snow*, both sentences untagged and both stated of
/// entities generally rather than of a player:
///
///   - "Wearing any piece of leather armor stops the freezing effect **and
///     reverses the damage**" - so a single leather cap is full immunity, and
///     putting it on mid-freeze sheds the counter "the same as if the entity
///     exited the powder snow", which is `kFreezeRecoveryRate` and not a second
///     rule.
///   - "Entities wearing **leather boots** ... do not fall through powder snow"
///     and may use it "like scaffolding, descending by sneaking ... and jumping
///     while inside of the block."
///
/// **Boots are the narrower of the two and are not interchangeable with it.**
/// A leather cap and iron boots is immune to freezing and still falls in; boots
/// alone are both. `PlayerInput` (`world/Player.hpp`) therefore carries two
/// flags rather than one, and the reason is written at each of them.
///
/// **Armour does not *reduce* freezing damage** - it either stops it outright
/// (leather) or does nothing (everything else), which is why `Player.cpp` spends
/// it through `kNoArmour` rather than through the worn set. Freezing is absent
/// from the wiki *Armor* page's list of reduced damage sources, alongside
/// suffocation, drowning, starvation and the void.
///
/// **Not implemented and deliberately not guessed at**, recorded so the next
/// reader does not read the absence as a claim: the reference gives fire mobs
/// five points per freeze tick rather than one, exempts a named list outright
/// (snow golem, stray, polar bear, wither, ender dragon, and bogged in Bedrock),
/// turns a skeleton into a stray instead of hurting it, and has a `freezeDamage`
/// game rule. All four are `Creature.cpp`'s and none is the player's.
///
/// **The entities that do not fall in are a different list again** and it is not
/// the one this project's brief carried: minecraft.wiki names rabbits,
/// endermites, silverfish, shulkers, vexes and foxes. A **strider is not on it**
/// - it falls through like anything else and is one of the three fire mobs that
/// take five a tick. `Creature.cpp`'s, when creatures meet powder snow.

/// **The pair's ratio, stated once - and it is documentation rather than a
/// second guard.** Dimensionless, so it is 3.5 whether you read the reference in
/// ticks (140 against 40) or in seconds (7 against 2), which is the one thing
/// the conversions above cannot show you at a glance.
///
/// **Be honest about what it adds: nothing that `freezeIntervalsAreSeconds` at
/// the top of this block does not already catch.** That assert pins each
/// constant against its own published tick count, so every edit that breaks the
/// ratio breaks it too, and a tick-rate change breaks it while leaving the ratio
/// untouched. This is a strict subset and is kept for the reader, not the build.
/// Saying so here is the point: an assert whose reach is overstated is worse
/// than one that is absent, because the overstatement is what stops the next
/// person looking for the guard that is actually missing.
///
/// A third assert used to sit below this one claiming that "the ratio alone is
/// not enough" and pinning the magnitudes itself. **It was decorative and its
/// message was false**, found by review on 2026-08-19 and removed the same day.
/// It read `!freezeOnsetIsThreeAndAHalfCadences() || freezeIntervalsAreSeconds(
/// 7.0f, 2.0f)` - and because the line above already asserts the left operand
/// true, it collapsed to its right operand, which is handed *literals* and so
/// mentions neither freezing constant. Substituting the retune it advertised
/// catching, `kFreezeOnsetSeconds = 3.5f` with `kFreezeInterval = 1.0f`: the
/// ratio assert passes, `freezeIntervalsAreSeconds` above fires correctly, and
/// the third one **passed**. It would also have gone vacuously true the moment
/// this ratio was relaxed. It is not replaced by a literal pin of 7 and 2,
/// deliberately - that would be a second, separately-editable statement of two
/// numbers the top of this block already owns.
constexpr bool freezeOnsetIsThreeAndAHalfCadences() {
    return kFreezeOnsetSeconds > kFreezeInterval && kFreezeOnsetSeconds / kFreezeInterval == 3.5f;
}
static_assert(freezeOnsetIsThreeAndAHalfCadences(),
              "you stand in powder snow for three and a half damage cadences before the first "
              "point lands - 140 ticks against 40");
// --- Armour. **Wired end to end as of 2026-08-19**, and the block below is a
// --- correction: it spent fifty lines explaining why armour was inert, and
// --- every one of those claims had already been overtaken. What remains true is
// --- the division of labour - the arithmetic lives here and in `item/Item.hpp`,
// --- the store lives in `item/Inventory.hpp`, and the wiring lives in
// --- `Main.cpp`.

/// **Armour is worn, is read and wears out - and this block said otherwise
/// until 2026-08-19 evening.** What stood here was a careful, correct account of
/// a state that had ended: it opened "Armour cannot be worn, which is why it
/// reduces nothing", explained at length that this was a missing *store* rather
/// than a missing call site, and then handed the reader a **numbered work order
/// for two lines that already exist**. That is the worst class of stale comment
/// in this codebase's own ranking - not a wrong line number, an instruction that
/// lands a duplicate assignment.
///
/// **What is actually true, verified by name in `Main.cpp` on 2026-08-19:**
///
///   1. `PlayerInput::armour` **is** set - `move.armour = inventory.armourSet();`,
///      beside the `input.invulnerable` the old note pointed at.
///   2. `Player::armourWear` **is** drained - `if (inventory.wearArmour(
///      player.armourWear) > 0)`, guarded on positive.
///   3. Armour **can be put on**, by two routes: a click in the armour cells
///      (`inventory.equipArmour(hit->index)`) and a right-click with a piece
///      held (`inventory.equipArmour(selectedSlot)`).
///   4. Five separate `hurtPlayer` sites pass `inventory.armourSet()`, so the
///      reduction runs on real damage rather than on a test path.
///
/// **Deliberately cited by symbol and not by line number.** An earlier version
/// of this list carried `Main.cpp` line numbers, and every one of the ten was
/// already wrong when checked hours later - they had drifted between +49 and
/// +91 lines, because `Main.cpp` is a 700 KB file under active edit by another
/// owner and finding 9635 (worn armour is never dropped on death) will move
/// them again. A line number into somebody else's live file is the most
/// rot-prone citation this project has, and it fails silently: the name it
/// points at is still *somewhere*, so a reader who checks the line sees
/// unrelated code and cannot tell whether the claim or the number is the wrong
/// half. Every name above is unique and greppable, which is a claim that
/// survives any edit that does not actually remove the call.
///
/// **The zero-construction measurement that justified all of it is also dead**,
/// and is recorded here rather than deleted because it is the reason this
/// arrived unwired and because its instrument is worth remembering. It counted
/// `SlotHit{Region::X, i}` constructions and found `Region::Armour` built zero
/// times, which was true then. It is false now. And an *earlier* version of that
/// same count was taken with a broken regex - `(return|=\s*)Region::X`, which
/// cannot match `return SlotHit{Region::X, i}` at all because the `SlotHit{`
/// sits in between, so what it really counted was the second `=` of
/// `== Region::X`. It reported 2, 5 and 2 against true counts of 2, 1 and 1. The
/// conclusion survived because zero is zero either way; the evidence did not.
///
/// **The general rule these four claims broke, and it is the point of keeping
/// this much text:** every one of them was a *negative* claim, every one was
/// written accurately, and every one was false within the hour. Negative claims
/// rot fastest. If you write one here, date it and name what would make it
/// false - `Inventory.hpp`'s `m_armour` does exactly that and did not rot.
///
/// What this file still owns, and all of it is asserted below: the curve, the
/// durability cost, and which damage sources may touch either. Two of those
/// asserts are the first callers `armourDefence` ever had, and a runtime probe
/// drove the whole chain - a ten-point blow costs 10 naked, 9 in leather, 6 in
/// iron and 3 in diamond, and a thirty-point blow separates Emberite from
/// diamond 13 to 15 on toughness alone.

/// The ceiling on effective defence points, and the divisor that turns them
/// into a fraction. **Both dimensionless.** Twenty points at 4% each is the 80%
/// ceiling, and 20/25 is 80% - so these two are one statement written twice and
/// have to move together.
constexpr float kArmourPointsCap = 20.0f;
constexpr float kArmourReductionDivisor = 25.0f;

/// Toughness is clamped before use. **Unreachable in survival** - a full
/// Emberite set is 12 - and carried only because the reference states it, so
/// that a `/give`-style grant cannot invert the curve.
constexpr float kArmourToughnessCap = 20.0f;

/// Float comparison for this file's own asserts. **A deliberate twin of
/// `fluid::agrees`, not an oversight**: `Fluid.hpp` includes `World.hpp` and
/// `Block.hpp`, and making this file depend on that one to borrow a three-line
/// comparison would invert the dependency and drag the world into everything
/// that needs a food value.
constexpr bool nearly(float a, float b, float tolerance) {
    const float difference = a - b;
    return (difference < 0.0f ? -difference : difference) <= tolerance;
}

/// What a blow costs after armour, **in health points**, given the wearer's
/// summed defence points and summed toughness.
///
/// **Both terms take one signature so that they cannot be wired separately.**
/// Defence without toughness is `CLAUDE.md` bug shape #5 - "a derivation
/// applied to one of a pair and not the other" - and it is not a hypothetical
/// here: it would silently make Emberite and diamond identical, which is the
/// exact regression Mojang's 1.18.30 note was written to announce the end of.
///
///     taken = d * (1 - min(20, max(A/5, A - 4d/(min(T,20)+8))) / 25)
///
/// **This is Bedrock's curve and has only been Bedrock's since 1.18.30**
/// (19 April 2022). Before that Bedrock was a flat 4% per point, with no
/// penetration term and no toughness at all. Mojang's own changelog for that
/// version - feedback.minecraft.net, titled "(Bedrock)", section "Combat and
/// Damage" - reads "Added armor toughness", "Diamond Armor and Netherite Armor
/// now have a toughness value of 2 and 3 respectively" and "Tweaked armor
/// reduction calculation to account for toughness". minecraft.wiki's *Bedrock
/// Edition 1.18.30*, a Bedrock-only page and so edition-unambiguous, states the
/// same rule from the player's side: "each 2 HP done by the attack reduces the
/// effective defense points by 1, but not below 20% of the armor points. Each
/// armor point now defends 0.8% to 4% instead of always 4%." Those two
/// sentences are exactly the `4d/(T+8)` penetration term and the `A/5` floor.
///
/// **The identity with Java's spelling is algebraic and does not make this a
/// Java number.** Java is usually written `d / (2 + T/4)`; since
/// `2 + T/4 == (8 + T)/4`, that is `4d / (8 + T)`. One curve, two spellings.
/// Mojang's changelog says "changed to match Java Edition" only of the
/// Protection enchantment, which this game does not have and will not get, so
/// the identity rests on the arithmetic and on the anchor below rather than on
/// a Mojang sentence claiming it.
///
/// Returns a float on purpose: `damageWithResistance` already rounds once, with
/// `std::lround`, after Resistance has scaled the blow. Rounding here as well
/// would round twice and lose to the reference on half-points.
constexpr float armourDamageTaken(int damage, int defencePoints, float toughness) {
    if (damage <= 0) {
        return 0.0f;
    }
    const float d = static_cast<float>(damage);
    const float a = static_cast<float>(defencePoints);
    const float t = toughness > kArmourToughnessCap ? kArmourToughnessCap : toughness;
    const float penetrated = a - 4.0f * d / (t + 8.0f);
    const float floor = a / 5.0f;
    const float effective = penetrated > floor ? penetrated : floor;
    const float capped = effective > kArmourPointsCap ? kArmourPointsCap : effective;
    return d * (1.0f - capped / kArmourReductionDivisor);
}

/// **The Bedrock-native anchor, and the strongest evidence in this block.**
/// minecraft.wiki's Bedrock-only *Bedrock Edition 1.18.30* page states that a
/// warden "does 30 damage on Normal difficulty, bringing players wearing full
/// netherite armor from full health to 7 health points". A full Emberite set is
/// 20 points and 12 toughness, so the curve above must leave 20 - 13.2 = 6.8,
/// which is the 7 that page reports. **A number published by a Bedrock page,
/// reproduced by this formula, is what carries the edition claim** - not the
/// wiki's untagged *Armor* article.
static_assert(nearly(armourDamageTaken(30, 20, 12.0f), 13.2f, 0.001f),
              "a warden's 30-damage blow must leave a full Emberite set 13.2 to absorb, which is "
              "the 6.8 health the Bedrock 1.18.30 page reports as 7");

/// The points table itself, checked through `armourDefence` rather than against
/// hand-copied literals. **These two asserts are that function's first callers
/// in the history of the tree**, and being `constexpr` on both sides they can
/// never rot - the shape `CLAUDE.md` names in `foodTablesAgree`.
constexpr int fullSetDefence(ItemId helmet) {
    return armourDefence(helmet) + armourDefence(static_cast<ItemId>(static_cast<int>(helmet) + 1)) +
           armourDefence(static_cast<ItemId>(static_cast<int>(helmet) + 2)) +
           armourDefence(static_cast<ItemId>(static_cast<int>(helmet) + 3));
}
constexpr float fullSetToughness(ItemId helmet) {
    return armourToughness(helmet) +
           armourToughness(static_cast<ItemId>(static_cast<int>(helmet) + 1)) +
           armourToughness(static_cast<ItemId>(static_cast<int>(helmet) + 2)) +
           armourToughness(static_cast<ItemId>(static_cast<int>(helmet) + 3));
}
static_assert(fullSetDefence(ItemId::LeatherHelmet) == 7 &&
                  fullSetDefence(ItemId::ChainmailHelmet) == 12 &&
                  fullSetDefence(ItemId::IronHelmet) == 15 &&
                  fullSetDefence(ItemId::GoldenHelmet) == 11 &&
                  fullSetDefence(ItemId::DiamondHelmet) == 20 &&
                  fullSetDefence(ItemId::EmberiteHelmet) == 20,
              "the reference's published full-set totals: leather 7, chainmail 12, iron 15, gold "
              "11, diamond 20 and netherite 20");
static_assert(fullSetToughness(ItemId::DiamondHelmet) == 8.0f &&
                  fullSetToughness(ItemId::EmberiteHelmet) == 12.0f &&
                  fullSetToughness(ItemId::IronHelmet) == 0.0f,
              "toughness is summed across pieces, not averaged - two a piece for diamond and three "
              "for netherite, and nothing at all for any other material");

/// The rest of the curve, at points and toughness read back out of the table
/// above rather than typed here a second time.
static_assert(nearly(armourDamageTaken(10, fullSetDefence(ItemId::DiamondHelmet),
                                       fullSetToughness(ItemId::DiamondHelmet)),
                     3.0f, 0.001f) &&
                  nearly(armourDamageTaken(10, fullSetDefence(ItemId::EmberiteHelmet),
                                           fullSetToughness(ItemId::EmberiteHelmet)),
                         2.8f, 0.001f) &&
                  nearly(armourDamageTaken(10, fullSetDefence(ItemId::IronHelmet), 0.0f), 6.0f,
                         0.001f) &&
                  nearly(armourDamageTaken(4, fullSetDefence(ItemId::LeatherHelmet), 0.0f), 3.2f,
                         0.001f),
              "a ten-point blow costs 3.0 through full diamond, 2.8 through full Emberite and 6.0 "
              "through full iron, and a four-point blow 3.2 through full leather");

/// **Emberite must beat diamond at equal points, and only toughness can make it
/// do so.** Both sets are 20 defence points; the whole of the difference is the
/// 12-versus-8 toughness. Mojang's 1.18.30 changelog announced exactly this -
/// "Netherite Armor will now reduce more damage than Diamond Armor" - so a
/// build in which these two came out equal would have dropped the toughness
/// term, which is the failure this assert exists to catch.
static_assert(armourDamageTaken(10, 20, 12.0f) < armourDamageTaken(10, 20, 8.0f),
              "toughness is what separates Emberite from diamond at identical defence points");

/// **The floor, which is the half of the curve a reader is least likely to
/// expect.** Past the breaking point `A * (T + 8) / 5` the penetration term has
/// eaten every point and `A/5` takes over, so armour stops scaling with the
/// blow: full iron against 30 keeps only 12%, which is the "0.8% per point" end
/// of the Bedrock page's stated range. Without the `max` this returns *more*
/// than the raw damage.
static_assert(nearly(armourDamageTaken(30, 15, 0.0f), 26.4f, 0.001f),
              "past the breaking point the A/5 floor governs, leaving full iron only 12% off a "
              "30-point blow - the 0.8%-per-point end of the reference's published range");

/// Bare skin must cost exactly the blow. The `max` against `A/5` is what stops
/// a negative penetration term turning into a *bonus* here.
static_assert(nearly(armourDamageTaken(10, 0, 0.0f), 10.0f, 0.001f),
              "no armour must take the whole blow and never more than it");

/// **The negative twin, and it is the retired rule rather than an invented
/// one.** Bedrock's pre-1.18.30 curve was a flat 4% per point, so a full
/// diamond set took 20% of any blow whatever its size. Feed that to the same
/// case the anchors use and it must disagree: 2.0 against our 3.0. A build
/// where these two agree has silently reverted four years of Bedrock.
constexpr float retiredFlatFourPercent(int damage, int defencePoints) {
    return static_cast<float>(damage) * (1.0f - 0.04f * static_cast<float>(defencePoints));
}
static_assert(!nearly(armourDamageTaken(10, 20, 8.0f), retiredFlatFourPercent(10, 20), 0.001f),
              "the flat 4%-per-point rule Bedrock retired in 1.18.30 must not reproduce the curve "
              "that replaced it");
/// And the twin for the argument order, which is the slip this signature is
/// most exposed to: defence is an integer count and toughness a float, so
/// swapping them compiles silently. Swapped, the diamond case must not survive.
static_assert(!nearly(armourDamageTaken(10, 8, 20.0f), 3.0f, 0.001f),
              "defence points and toughness are not interchangeable and swapping them must change "
              "the answer");

/// **What the player is wearing, as the two numbers the curve above needs.**
///
/// A pair rather than two loose arguments because they are two terms of one
/// formula and separating them is how one gets wired and the other forgotten -
/// `CLAUDE.md` bug shape #5, "a derivation applied to one of a pair and not the
/// other", which is the shape that mirrored every block in the game for four
/// milestones. `item/Inventory.hpp` derives one of these from the worn pieces
/// and is the only thing that may.
///
/// **The defaulted value is not "no armour worn", it is "armour does not apply
/// to this blow", and the two are deliberately the same number.** A source
/// armour cannot reduce passes `kNoArmour`; so does a naked player. That is not
/// a conflation - the arithmetic and the durability cost are identical in both
/// cases, and collapsing them means a call site cannot get the pairing wrong by
/// passing a set to a source that should ignore it.
struct ArmourSet {
    /// Summed defence points across the worn pieces. **Summed, not averaged** -
    /// the reference's own full-set totals (leather 7, diamond 20) are sums,
    /// and `fullSetDefence` above asserts exactly that.
    int defence = 0;
    /// Summed armour toughness, likewise. Only diamond and Emberite carry any.
    float toughness = 0.0f;
};

/// Bare skin, and equally "this blow ignores armour". Named so a call site
/// reads as a statement rather than as an empty brace nobody can interpret.
inline constexpr ArmourSet kNoArmour{};

constexpr float armourDamageTaken(int damage, const ArmourSet& armour) {
    return armourDamageTaken(damage, armour.defence, armour.toughness);
}
static_assert(nearly(armourDamageTaken(30, ArmourSet{20, 12.0f}), 13.2f, 0.001f) &&
                  nearly(armourDamageTaken(10, kNoArmour), 10.0f, 0.001f),
              "the pair overload must be the same curve as the loose one, and kNoArmour must cost "
              "the whole blow");

/// **What one blow costs each worn piece, in durability points.**
///
/// "Any time the player takes damage that can be reduced by armor, each piece of
/// armor they are wearing loses 1 durability for every 4 of incoming damage
/// (rounded down, but never below 1)" - minecraft.wiki *Durability*, "Armor
/// durability". **Untagged**, so the formula itself is not affirmatively
/// Bedrock's; it is recorded here as the best available and flagged rather than
/// dressed up.
///
/// **The one part of this that IS Bedrock-sourced is the argument**, and it is
/// the part most likely to be got wrong. MCPE-165149, resolved by a Mojira
/// moderator 2023-01-09: "The amount of armor durability loss is based on the
/// total amount of *Incoming* damage. So with more incoming damage, durability
/// will be used up faster." An MCPE ticket is Bedrock-unambiguous. So this
/// takes the **raw** blow, before armour, before Resistance and before
/// absorption - which is `CLAUDE.md` bug shape #3 waiting to happen, because
/// post-armour damage is right there in the same function and is the same type.
/// A 30-point blow through full Emberite costs 7 per piece, not the 3 that
/// 13.2 would give.
///
/// Corroborated from the other end by the *Resistance* page: armour "still
/// lose[s] the usual amount of durability, even at level 5+ where the potion
/// effect grants complete immunity" - i.e. the cost cannot be read off what
/// actually landed, because at Resistance V nothing lands and armour still
/// wears.
///
/// **Returns per piece, not per set.** Every worn piece pays this, which is why
/// `Inventory::wearArmour` applies one number to four slots rather than
/// dividing it among them.
constexpr int armourDurabilityCost(int incomingDamage) {
    if (incomingDamage <= 0) {
        return 0;
    }
    const int quarter = incomingDamage / 4;
    return quarter < 1 ? 1 : quarter;
}
static_assert(armourDurabilityCost(0) == 0 && armourDurabilityCost(-3) == 0,
              "a blow that did not happen costs no durability, which is what stops a refused hit "
              "inside the invulnerability window from grinding armour away for free");
static_assert(armourDurabilityCost(1) == 1 && armourDurabilityCost(3) == 1 &&
                  armourDurabilityCost(4) == 1 && armourDurabilityCost(7) == 1 &&
                  armourDurabilityCost(8) == 2 && armourDurabilityCost(30) == 7,
              "one point per four of incoming damage, floored, but never less than one");
/// The negative twin, and it is the unit slip rather than an invented rule:
/// **fed the damage that actually landed instead of the raw blow**, a 30-point
/// hit through full Emberite would cost 3 rather than 7 - armour lasting more
/// than twice as long as the reference allows.
static_assert(armourDurabilityCost(30) !=
                  armourDurabilityCost(static_cast<int>(
                      armourDamageTaken(30, ArmourSet{20, 12.0f}))),
              "durability is charged on the raw incoming blow, never on what survived armour - "
              "MCPE-165149 states this explicitly and the two answers differ by more than double");

/// **The falling-anvil helmet reduction is deliberately NOT implemented, and
/// this is the sourcing rather than a shrug.**
///
/// The claim is that a helmet takes 25% off a falling anvil or stalactite and
/// pays double durability for it. `fx-falling` deferred both halves to whoever
/// owned armour on the grounds that splitting them is bug shape #5. The answer,
/// having gone looking, is **neither half** - and for a better reason than
/// "unsourced":
///
///   1. **minecraft.wiki contradicts itself on it.** *Anvil* says helmets
///      "provide a 25% damage reduction to falling anvils"; *Damage* says a
///      helmet "does not provide any special protection other than the normal
///      armor damage reduction". Both untagged. Cross-reading the pages is what
///      exposed this - trusting either one alone would have shipped a number.
///   2. **Both cite the same footnote, and it is a talk-page archive**, not an
///      article: `Talk:Damage/Archive 1`, "Falling Block". Reading it, the
///      whole discussion is Java: MC-251027 and MC-248961 (MC-, not MCPE-),
///      decompiled Java 1.18.1 and 1.20.1, and the `damages_helmet` damage-type
///      tag, which is a Java 1.19.4 data-driven construct that **does not exist
///      in Bedrock** - Bedrock uses a flat integer cause enum.
///   3. **In the Java code the discussion quotes, the multiplier is a no-op.**
///      The 0.75 is applied *after* the damage has already been dealt. So the
///      25% is not merely unconfirmed for Bedrock, it appears not to work in
///      the edition it was measured in.
///   4. There is **no MCPE ticket, no Bedrock changelog entry and no parity
///      issue** in either direction about it, and nothing in
///      `Mojang/bedrock-samples`.
///
/// **What Bedrock does publish** is that `anvil`, `falling_block`, `stalactite`
/// and `stalagmite` are four separate causes in its own
/// `ActorDamageCause` schema - so if this is ever revisited, it is four
/// questions and not one. A falling anvil is armour-reducible by the ordinary
/// rule above and that is all this game gives it.
/// **Which damage sources armour may touch, which is the half of this that maps
/// onto constants this file already owns.** Armour is not a global multiplier
/// and wiring it as one would armour-plate starvation and the void.
///
/// Source: minecraft.wiki *Damage* "Reductions" and *Armor* "Damage sources",
/// cross-checked against each other. **Both are untagged**, so they are the
/// weakest link in this block and are named as such; the cause names are
/// Bedrock's own, from `@minecraft/server.EntityDamageCause` on
/// learn.microsoft.com, which is edition-unambiguous.
///
///   Reduced: lava, fire (standing in the block), cactus and sweet berries
///            (`contact`), magma block, explosions, projectiles, lightning,
///            thorns, a falling anvil.
///   NOT reduced: suffocation, drowning, burning over time (`fireTick`),
///            falling, starvation, freezing, the void, `/kill`, magic - and
///            the wither rose, which the Bedrock cause enum routes through
///            `wither`, the status effect, rather than through contact.
///
/// **The split that will catch someone is fire.** Bedrock has two causes where
/// this file has two constants: `fire` for standing in the block, which armour
/// reduces, and `fireTick` for burning afterwards, which it does not. Anything
/// that treats "fire" as one source is wrong for one of the two halves. This
/// file already separates them - `kFireDamage` on `kFireInterval` against
/// `kBurnDamage` on `kBurnInterval` - so the distinction survives, and the
/// assert below is what keeps it surviving.
static_assert(kFireInterval != kBurnInterval,
              "standing in fire and burning afterwards are two damage sources in the reference, "
              "reduced by armour and not reduced respectively, and folding them onto one cadence "
              "would leave no way to tell them apart when armour is wired");
/// Below the world, **in metres of world Y**, and the one hazard measured in a
/// coordinate rather than in a cadence. Instant and unconditional - not
/// reduced, not survivable.
///
/// **Not Bedrock's own number, and deliberately.** The reference puts the void
/// "below the Y-axis of -64" (minecraft.wiki, *Damage*; Java's -128 is `[JE]`)
/// - which is its **world floor**, so what it actually says is "as soon as you
/// are below the lowest block layer". Our world floor is Y = 0, so the figure
/// that transfers is the clearance below it and not the -64 itself. Porting the
/// -64 across would put the void sixty-four blocks *inside* the world, which is
/// the "ported into a field measured against a different origin" shape.
///
/// **The *shape* diverges too, and that is also on the record rather than an
/// oversight.** Bedrock deals 4 health every half second below the floor,
/// unreduced by armour, Protection, Resistance or the invulnerability window;
/// ours spends `kMaxHealth * 2` once, unresistable and window-bypassing, at the
/// same instant. The outcome is identical for a player - there is nothing under
/// our world to fall through and no way back up - and the two exemptions that
/// make the reference's version interesting are both honoured. Creative is
/// exempt in both. What a repeating version would buy is a player who could be
/// rescued mid-fall, which nothing in this game can do.
constexpr float kVoidDepth = -8.0f;

// --- Hunger. §3.2, and the design note there is worth keeping in mind: hunger
// --- is not a food timer, it is a tax on healing and hurrying.

/// Each time exhaustion reaches this it resets and costs one saturation, or one
/// food if saturation is already gone. **Exhaustion points**, dimensionless.
/// minecraft.wiki, *Food*: "Once the exhaustion level reaches 4.0, it resets to
/// 0.0 and reduces the saturation by 1 ... If the saturation is 0, it reduces
/// the hunger by 1 instead." No edition tag, so Bedrock's too.
constexpr float kExhaustionPerLevel = 4.0f;

// **Exhaustion points, per metre or per event as each name says**, and these
// are Mojang's own numbers rather than the wiki's: `player.json` in
// `Mojang/bedrock-samples` publishes the whole table as
// `minecraft:exhaustion_values`, in the Bedrock engine's own units -
//   "sprint": 0.1, "swim": 0.01, "jump": 0.05, "sprint_jump": 0.2,
//   "mine": 0.005, "attack": 0.1, "damage": 0.1, "heal": 6.0, "walk": 0.0
// - which is every constant below, exactly. A published behaviour pack is a
// primary source and beats minecraft.wiki, which documents Java often enough
// and marks the edition split inconsistently enough that it has misled this
// project more than once.
//
// The one field in that list we do not model is `"lunge": 4.0`, which has no
// counterpart here.
constexpr float kExhaustSprintPerMetre = 0.1f;
constexpr float kExhaustSwimPerMetre = 0.01f;
constexpr float kExhaustJump = 0.05f;
constexpr float kExhaustSprintJump = 0.2f;
constexpr float kExhaustBreakBlock = 0.005f;
constexpr float kExhaustAttack = 0.1f;
/// **Confirmed at 0.1 by `player.json`'s `"damage": 0.1`, and the wiki says
/// otherwise.** The *Parity issue list* carries MCPE-165424, "Hunger drains
/// faster when taking damage than in Java Edition", with no replacement number
/// - which reads as "our 0.1 is Java's and Bedrock's is unknown", and that was
/// written here as a caveat until the behaviour pack was checked. Mojang's own
/// Bedrock file says 0.1. Whatever that parity report is about, it is not this
/// per-instance cost, so there is nothing here to approximate.
constexpr float kExhaustDamaged = 0.1f;
/// **By far the most expensive thing a player can do.** Healing ten hearts
/// costs sixty exhaustion, which is fifteen food. `player.json`: `"heal": 6.0`,
/// and minecraft.wiki *Healing* agrees - "increases the player's exhaustion by
/// 6, of which 4 cost 1 hunger".
constexpr float kExhaustPerHealed = 6.0f;

/// Walking is free, and this is the strong kind of confirmation rather than the
/// weak one. The wiki's table merely *omits* a walking row, which is an
/// argument from silence; `player.json` states `"walk": 0.0` outright.
constexpr float kExhaustWalkPerMetre = 0.0f;

/// Sprinting needs more than six food; regeneration needs eighteen; nothing at
/// all starves.
constexpr int kSprintFoodFloor = 6;
constexpr int kRegenFoodFloor = 18;

/// One health point - half a heart - every **80 ticks**, in seconds, and the
/// point healed costs `kExhaustPerHealed`. minecraft.wiki, *Food*: "the
/// player's health naturally regenerates every 4 seconds (80 ticks)"; *Healing*
/// gives the same figure with the exhaustion beside it - "they regenerate 1
/// every 4 seconds. This increases the player's exhaustion by 6". Neither
/// sentence carries an edition tag, so both are Bedrock's as well as Java's.
constexpr float kRegenInterval = 4.0f;
constexpr int kRegenAmount = 1;

/// **Java's saturation boost, and the one switch that turns it on.**
///
/// **What it changes.** With this true, a player at a *full* food bar with any
/// saturation left heals one point every `kSaturatedRegenInterval` instead of
/// every `kRegenInterval` - eight times as fast - and pays
/// `kSaturatedRegenCost` of saturation for it instead of `kExhaustPerHealed` of
/// exhaustion. After a decent meal that is the difference between getting ten
/// hearts back in five seconds and in forty, so it is the single biggest lever
/// on how a fight feels afterwards.
///
/// **What Java does.** Exactly that. minecraft.wiki, *Food*: saturation boost
/// "heals 1 by consuming 1.5 saturation, and activates every 0.5 seconds (10
/// ticks) when at full hunger".
///
/// **Why it is off.** The same page, verbatim: "While the saturation mechanic
/// as a whole is in both *Bedrock Edition* and *Java Edition*, saturation boost
/// is only in *Java Edition*." Bedrock is this project's reference by direct
/// user instruction, so it is off - and the *Parity issue list* carries it a
/// second time, under "In Java Edition but not in Bedrock Edition".
///
/// **Flipping it back is this one line**, and nothing else: `Player.cpp`'s
/// natural-regeneration branch reads it through `if constexpr`, so the Java
/// path is still compiled and type-checked at false and cannot rot. It is a
/// compile-time constant on purpose and must not become a setting - this is a
/// question that gets settled once, by the person who plays the game, not a
/// dial to leave in the options screen.
///
/// **Saturation still matters either way**, and in the reference's own way: the
/// exhaustion loop spends it before food, so a well-fed player keeps their bar
/// full for longer. What this buys is the second, faster heal on top.
constexpr bool kJavaSaturationBoost = false;

/// **Saturation healing's two numbers, and they are `[JE]` - Java only.** At a
/// full food bar with saturation left, one point every **10 ticks** at a cost
/// of one and a half saturation. Live only while `kJavaSaturationBoost` above
/// is true; marked rather than deleted so nobody re-derives the pair as
/// Bedrock's, which is exactly how a `[JE]` number becomes load-bearing.
///
/// **"These have zero readers and can be deleted" is wrong, and was filed twice
/// - deleting them breaks the build.** `Player.cpp`'s regeneration branch reads
/// both, unconditionally as far as the compiler is concerned: only the *bool*
/// sits behind `if constexpr (kJavaSaturationBoost)`, and the branch that spends
/// these is an ordinary runtime `if` below it. That is the whole design and it
/// is stated at `kJavaSaturationBoost` above - the discarded arm of an
/// `if constexpr` in a non-template function is still parsed and type-checked,
/// which is what stops the Java path rotting silently. A grep for the names does
/// find the two call sites; a grep that concluded otherwise was reading the
/// `if constexpr` as a `#if`.
constexpr float kSaturatedRegenInterval = 0.5f;
constexpr float kSaturatedRegenCost = 1.5f;

/// Starving takes a point every **80 ticks**, in seconds, and **stops at one**
/// on Normal, which is the difficulty we have. It cannot kill. minecraft.wiki,
/// *Food*: "Starvation damages the player by 1 every 4 seconds (80 ticks)";
/// *Difficulty* gives the floors - Easy stops at 10, Normal at 1, and Hard
/// kills outright.
constexpr float kStarveInterval = 4.0f;
constexpr int kStarveFloor = 1;
/// **A named point rather than a bare 1 at the call site**, which is what every
/// other hazard in this file already gets. It is not fire, not resistable and
/// not stopped by the damage window, so it has no interval constant of its own
/// beyond `kStarveInterval`.
constexpr int kStarveDamage = 1;

/// Regeneration and starvation are the two ends of one bar and share a cadence,
/// so they get the same tick-count check the hazards above do - with the same
/// negative twin, for the same reason.
constexpr bool hungerIntervalsAreSeconds(float regen, float starve, float saturated) {
    return regen == fromTicks(80) && starve == fromTicks(80) && saturated == fromTicks(10);
}
static_assert(hungerIntervalsAreSeconds(kRegenInterval, kStarveInterval,
                                        kSaturatedRegenInterval),
              "healing and starving are both the published 80 ticks, and Java's saturation "
              "boost the published 10");
static_assert(!hungerIntervalsAreSeconds(80.0f, 80.0f, 10.0f),
              "and a tick count dropped straight into a seconds field must be refused");
/// The absolute anchor beside them: a quarter of a health point a second, each
/// way. Fails if either interval or either amount moves on its own.
static_assert(perSecond(kRegenAmount, kRegenInterval) == 0.25f &&
                  perSecond(kStarveDamage, kStarveInterval) == 0.25f,
              "a fed player gains a quarter of a health point a second and a starving one loses "
              "the same");

/// How long a death lasts before the world hands control back.
constexpr float kRespawnSeconds = 1.6f;

/// One status effect a food hands out on top of the hunger it restores.
///
/// **The amplifier is zero-based, the way `Effects` counts it**, so Absorption
/// IV is `3`. The reference writes levels in roman numerals and this field does
/// not, which is the same kind of gap as a duration written `2:00` landing in a
/// field measured in seconds - both are pinned below against functions that
/// already own the unit, rather than against a number typed twice.
struct EffectGrant {
    effects::Effect effect = effects::Effect::None;
    int amplifier = 0;
    float seconds = 0.0f;

    constexpr bool operator==(const EffectGrant&) const = default;
};

/// How many effects one food may hand out. Four, because the enchanted golden
/// apple grants four and nothing in the reference grants more.
constexpr int kFoodEffects = 4;

/// What eating one of something is worth.
///
/// **Carries the item it describes**, which it did not for its first thirty-odd
/// rows. A `FoodValue` was two bare numbers, so a caller holding one could not
/// ask *which* food it had - and everything the reference hangs off eating
/// something in particular needs exactly that.
///
/// **It is not a second copy of the `case` label.** `foodValue` stamps it once,
/// at its single exit, from the argument it was asked about - so no row states
/// its own id and no row can state it wrongly. `None` when there is nothing to
/// eat, so "carries an id" and "is edible" are the same question.
///
/// **`grants` is the channel a food's side effects travel down**, and it is
/// here rather than beside the effect that needs it because the reference keys
/// these by item and not by potion: `absorptionPoints` had no grant path in the
/// game at all until this field existed. `feedPlayer` carries hunger and
/// saturation and nothing else, so the eat site applies these itself - the
/// table states what happens and the call site does it, which is the way round
/// that keeps the knowledge in one place.
///
/// **Packed from the front and terminated by `Effect::None`**, so a reader
/// stops at the first empty slot. `foodGrantsAreUsable()` proves that, proves
/// nothing inedible grants anything, and proves no row asks for an effect that
/// `Effects::apply` would silently refuse.
struct FoodValue {
    int hunger = 0;
    float saturation = 0.0f;
    ItemId item = ItemId::None;
    std::array<EffectGrant, kFoodEffects> grants{};

    /// **The opposite channel to `grants`, and it is one effect rather than a
    /// set on purpose.** `Mojang/bedrock-samples`
    /// `behavior_pack/items/honey_bottle.json` publishes
    /// `"remove_effects": ["poison"]` inside `minecraft:food` - a list with
    /// exactly one entry - so a single field states what the primary source
    /// states. Widening it to a set would be structure invented ahead of a
    /// second user, and would invite the natural wrong version of this rule,
    /// "clear the harmful ones", which cures Wither, Hunger and Slowness off
    /// one bottle.
    ///
    /// **Milk is not this field.** Milk clears everything, is not food, and is
    /// drunk down the bucket path in `Main.cpp`, which already calls
    /// `Effects::clear`. `behavior_pack/items/milk_bucket.json` is a 404 -
    /// checked against `honey_bottle.json` returning 200, so that is the
    /// repository's edge rather than a bad URL - and milk's rule is engine-side
    /// with the wiki as its only authority.
    ///
    /// **Unlike `grants`, this one is applied by `feedPlayer` and not by the
    /// eat site, and the asymmetry is deliberate.** A grant has to travel to
    /// the call site because `grantEffect` is a lambda local to `Main.cpp` that
    /// does more than mutate state. A removal needs none of that: it is
    /// `Effects::clearOne` on a member `feedPlayer` is already holding, so
    /// routing it through the call site would export knowledge for no reason
    /// and leave the field unread until another file changed.
    effects::Effect removes = effects::Effect::None;
};

/// The table itself - hunger and saturation, and **nothing else**.
///
/// The first block is quoted directly in `RESEARCH.md` §3.3. The rest are
/// recalled wiki figures for foods that section does not list, and are marked
/// as such - they are the ordinary members of families whose other members *are*
/// quoted, so they are low-risk, but they are not sourced the same way.
///
/// **Call `foodValue` instead.** This one answers without the id, so a value
/// taken from here reads as "not edible" to anything that checks the id, and
/// `foodCarriesItsItem()` is what pins the difference to exactly that.
/// It is separate only so the stamp below has one place to happen: folding it
/// back in would mean thirty-five rows each repeating their own `case` label.
constexpr FoodValue foodAmounts(ItemId item) {
    switch (item) {
    // §3.3, quoted.
    case ItemId::CookedPorkchop:
    case ItemId::CookedBeef:
        return {8, 12.8f};
    case ItemId::CookedMutton:
    case ItemId::CookedSalmon:
        return {6, 9.6f};
    // **Its own case, not the 0.8 modifier the other cooked meats carry.**
    // Cooked chicken is deliberately the *worst* of them in the reference -
    // same six hunger, a 0.6 modifier - and grouping it above quietly made it
    // as good as mutton. https://minecraft.wiki/w/Cooked_Chicken: hunger 6,
    // saturation 7.2.
    case ItemId::CookedChicken:
        return {6, 7.2f};
    case ItemId::Bread:
    case ItemId::BakedPotato:
    case ItemId::CookedCod:
    case ItemId::CookedRabbit:
        return {5, 6.0f};
    // https://minecraft.wiki/w/Golden_Apple: hunger 4, saturation 9.6,
    // Absorption I for 2:00 and Regeneration II for 0:05. Both editions agree
    // on this one. Amplifiers are zero-based, so I is 0 and II is 1; durations
    // are the wiki's mm:ss written out in seconds.
    case ItemId::GoldenApple:
        return {.hunger = 4,
                .saturation = 9.6f,
                .grants = {{{effects::Effect::Absorption, 0, 120.0f},
                            {effects::Effect::Regeneration, 1, 5.0f}}}};
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
    // **Bedrock's own numbers, and they were Java's until 2026-08-19.**
    // `Mojang/bedrock-samples` `behavior_pack/items/sweet_berries.json` and
    // `glow_berries.json` both publish `nutrition: 2` and
    // `saturation_modifier: "low"`, and Bedrock's saturation is
    // `nutrition * modifier * 2`, so 2 x 0.3 x 2 = 1.2. Both rows read 0.4 -
    // three times too stingy, and exactly Java's figure. These are the forage
    // you live on before a farm, and at 0.4 the bar started draining almost as
    // soon as you had eaten.
    //
    // **The modifier keywords are proved by foods this table already agrees
    // with, not assumed**, and here is the whole proof rather than a count of
    // it: raw beef 3/1.8 gives low = 0.3, cooked beef 8/12.8 gives good = 0.8,
    // cookie 2/0.4 gives poor = 0.1 and golden carrot 6/14.4 gives
    // supernatural = 1.2. Four keywords, four rows, each one a row nobody
    // touched. `bedrockSaturation` below turns those four into asserts.
    //
    // **What is NOT claimed: that the rest of the table was re-derived.** An
    // earlier version of this comment said "33 of 36 rows already matched",
    // and that arithmetic was wrong and is withdrawn - review measured the
    // table at **40 `case ItemId::` labels, all distinct, on 2026-08-19**, and
    // 33 + 3 does not reach it. Three rows here were checked against published
    // JSONs and corrected (these two and dried kelp); the other thirty-seven
    // were read for plausibility, not fetched. **A bare count in prose is the
    // wrong instrument for this** - it cannot fail when a row is added, which
    // is precisely how the 36 went stale. If the agreement is ever worth
    // stating as a fact, state it the way `Biome.cpp` states its own row count,
    // as a `static_assert` over a counting function, so that adding the
    // thirty-eighth food breaks the build instead of a sentence.
    case ItemId::SweetBerries:
    case ItemId::GlowBerries:
        return {2, 1.2f};
    case ItemId::Beetroot:
        return {1, 1.2f};
    case ItemId::PumpkinPie:
        return {8, 4.8f};
    case ItemId::RawRabbit:
        return {3, 1.8f};
    case ItemId::SpiderEye:
        return {2, 3.2f};
    case ItemId::HoneyBottle:
        // `honey_bottle.json`: `nutrition: 6`, `saturation_modifier: "poor"`
        // and `"remove_effects": ["poison"]`. All three from the one primary
        // file, which is why the row states all three. **"poor" is the keyword
        // for 0.1**, so the saturation is 6 x 0.1 x 2 = 1.2 - the comment here
        // used to gloss the keyword itself as "1.2", conflating the modifier
        // with the product and leaving a reader to conclude that "poor" and
        // "supernatural" were the same number.
        return {.hunger = 6, .saturation = 1.2f, .removes = effects::Effect::Poison};
    // The same correction as the berries above, in the other direction.
    // `dried_kelp.json`: `nutrition: 1`, `saturation_modifier: "poor"`, so
    // 1 x 0.1 x 2 = 0.2. It read 0.6 - Java's figure, three times too generous -
    // which made the reference's deliberately-worthless bulk food better than a
    // potato.
    case ItemId::DriedKelp:
        return {1, 0.2f};
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
    case ItemId::BeetrootSoup:
    case ItemId::SuspiciousStew:
        return {6, 7.2f};
    // **Not one of the soups, and that is the entire point of the recipe.**
    // Five ingredients including a cooked rabbit, and what it buys is the most
    // hunger any non-cake food in the reference restores - grouped with the
    // bowls it was worth no more than a mushroom picked off the ground.
    // https://minecraft.wiki/w/Rabbit_Stew: hunger 10, saturation 12.
    case ItemId::RabbitStew:
        return {10, 12.0f};
    // https://minecraft.wiki/w/Enchanted_Golden_Apple: hunger 4, saturation 9.6,
    // Absorption IV for 2:00, Regeneration II, Fire Resistance I and Resistance
    // I for 5:00.
    //
    // **Regeneration's duration is the one place the editions differ** - 0:20
    // in Java, 0:30 in Bedrock - and Bedrock is the reference, so 30. All four
    // fit because `kFoodEffects` is four; nothing in the reference grants more.
    case ItemId::EnchantedGoldenApple:
        return {.hunger = 4,
                .saturation = 9.6f,
                .grants = {{{effects::Effect::Absorption, 3, 120.0f},
                            {effects::Effect::Regeneration, 1, 30.0f},
                            {effects::Effect::FireResistance, 0, 300.0f},
                            {effects::Effect::Resistance, 0, 300.0f}}}};
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

/// What eating one of something is worth, **and which something it was**.
///
/// The one exit the id is stamped at. `None` for anything the table did not
/// answer for, so a `FoodValue` that names an item is exactly a `FoodValue`
/// worth eating - a caller never has to check two fields to know it has a real
/// row. Every reader wants this one; `foodAmounts` is the table underneath it.
constexpr FoodValue foodValue(ItemId item) {
    FoodValue value = foodAmounts(item);
    value.item = value.hunger > 0 ? item : ItemId::None;
    return value;
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

/// Proves the id `foodValue` stamps is the item it was asked about, and that
/// stamping it changed no number.
///
/// Two claims, both of which a new field invites someone to break:
///
/// - **The id is the argument, or `None`.** Stamping it unconditionally -
///   `value.item = item;` - is the obvious simplification and it fires this,
///   because a stick would then come back naming itself while restoring
///   nothing, and every caller that tests the id for a real row would believe
///   it. Dropping the stamp entirely, so `foodValue` just forwards
///   `foodAmounts`, fires it the same way from the other side.
/// - **The wrapper is transparent.** It re-asks the table and compares, so a
///   number touched on the way through fails here rather than in play.
///
/// The loop is the shape of `foodTablesAgree` above deliberately: both sides
/// are `constexpr`, so neither can rot the way a comment does.
constexpr bool foodCarriesItsItem() {
    for (int raw = 0; raw <= static_cast<int>(ItemId::kLastItem); ++raw) {
        const auto item = static_cast<ItemId>(raw);
        if (item == ItemId::None) {
            continue;
        }
        const FoodValue value = foodValue(item);
        const FoodValue amounts = foodAmounts(item);
        if (value.item != (amounts.hunger > 0 ? item : ItemId::None)) {
            return false;
        }
        if (value.hunger != amounts.hunger || value.saturation != amounts.saturation ||
            value.grants != amounts.grants) {
            return false;
        }
    }
    return true;
}

static_assert(foodCarriesItsItem(),
              "a FoodValue names the food it describes, and names nothing when there is nothing "
              "to eat - anything else and the id is a worse answer than no id at all");

/// Proves every grant in the table is one the game can actually hand out.
///
/// Four claims, each of which a plausible new row breaks while still compiling:
///
/// - **Packed from the front.** A hole - `{{}, {Regeneration, 1, 5.0f}}` - makes
///   every reader that stops at the first `None` skip the rest, and this fires.
/// - **Nothing inedible grants anything.** A row with no hunger never reaches
///   the eat site at all, so an effect hung on one is dead on arrival.
/// - **No zero-length grant.** Writing `2:00` as `2.0f` instead of `120.0f` is
///   the mm:ss-into-a-seconds-field slip; a duration of nothing is the same
///   mistake taken all the way, and this catches the end of that range.
/// - **Nothing instant.** `Effects::apply` returns false and does nothing for
///   the two instant effects, so a food granting `InstantHealth` would look
///   entirely finished and silently do nothing - which is exactly how Strength
///   and Weakness sat unimplemented in `Effects.hpp` for milestones.
constexpr bool foodGrantsAreUsable() {
    for (int raw = 0; raw <= static_cast<int>(ItemId::kLastItem); ++raw) {
        const auto item = static_cast<ItemId>(raw);
        if (item == ItemId::None) {
            continue;
        }
        const FoodValue value = foodValue(item);
        bool ended = false;
        for (const EffectGrant& grant : value.grants) {
            if (grant.effect == effects::Effect::None) {
                ended = true;
                continue;
            }
            if (ended || value.hunger <= 0 || grant.seconds <= 0.0f ||
                effects::effectInfo(grant.effect).instant) {
                return false;
            }
        }
    }
    return true;
}

static_assert(foodGrantsAreUsable(),
              "a food's effects are packed from the front, last longer than no time at all, hang "
              "only on something edible, and are effects `Effects::apply` will actually take");

/// **Exactly one food removes anything, and it removes exactly poison.**
/// Counted over the whole item range rather than asserted of the honey bottle
/// alone, so the interesting failure is caught: not "did honey lose its rule"
/// but "did a second food quietly acquire one", which is what a reader
/// implementing the natural-but-wrong "clear the harmful ones" would produce.
constexpr int foodsThatRemoveSomething() {
    int found = 0;
    for (int raw = 0; raw <= static_cast<int>(ItemId::kLastItem); ++raw) {
        if (foodValue(static_cast<ItemId>(raw)).removes != effects::Effect::None) {
            ++found;
        }
    }
    return found;
}
static_assert(foodValue(ItemId::HoneyBottle).removes == effects::Effect::Poison,
              "a honey bottle cures poison and nothing else, which is the one-entry "
              "`remove_effects` list `honey_bottle.json` publishes");
static_assert(foodsThatRemoveSomething() == 1,
              "the honey bottle is the only food in the reference that removes an effect; milk "
              "clears everything but is drunk down the bucket path and is not food");
/// The control for the pair above: an ordinary meal must remove nothing, or the
/// count would be satisfied by the field simply defaulting wrong.
static_assert(foodValue(ItemId::Bread).removes == effects::Effect::None &&
                  foodValue(ItemId::GoldenApple).removes == effects::Effect::None,
              "eating ordinary food must not cure anything");

/// The two apples' Absorption, **measured through the function that owns the
/// unit** rather than compared against a number written twice.
///
/// Fails on: writing the enchanted apple's amplifier as `4` because the wiki
/// says "IV". That is a level, this field is a zero-based amplifier, and the
/// difference is twenty points of absorption instead of sixteen - a value
/// ported into a field measured in something else, which compiles, validates
/// and plays. `absorptionPoints` and its own asserts live in `Effects.hpp`.
static_assert(effects::absorptionPoints(effects::running(
                  effects::Effect::Absorption,
                  foodValue(ItemId::GoldenApple).grants[0].amplifier)) == 4.0f &&
                  effects::absorptionPoints(effects::running(
                      effects::Effect::Absorption,
                      foodValue(ItemId::EnchantedGoldenApple).grants[0].amplifier)) == 16.0f,
              "a golden apple is worth four absorption points and an enchanted one sixteen - the "
              "amplifier is zero-based and the roman numeral is not");

/// Durations, in seconds, pinned by name.
///
/// Fails on: writing `2:00` as `2.0f`. Nothing else in the file would notice -
/// a two-second Absorption is a plausible-looking number in a plausible-looking
/// field, and the only symptom is the effect ending before the player looks up.
static_assert(foodValue(ItemId::GoldenApple).grants[0].seconds == 120.0f &&
                  foodValue(ItemId::GoldenApple).grants[1].seconds == 5.0f &&
                  foodValue(ItemId::EnchantedGoldenApple).grants[0].seconds == 120.0f &&
                  foodValue(ItemId::EnchantedGoldenApple).grants[1].seconds == 30.0f &&
                  foodValue(ItemId::EnchantedGoldenApple).grants[3].seconds == 300.0f,
              "two minutes is 120 seconds, five is 5, and the enchanted apple's regeneration is "
              "Bedrock's 0:30 rather than Java's 0:20");

// **Two apples are filled in and the rest of the reference's food effects are
// not, deliberately.** The table now has the channel, so the gap is a list of
// missing rows rather than a missing mechanism - but the ones left out are not
// simply more of the same:
//
//   rotten flesh, raw chicken   Hunger and Poison **with a probability** (80%
//                               and 30%). A chance is a second field and a
//                               source of randomness at the eat site, which is
//                               a design decision nobody has made.
//   pufferfish, poisonous       Poison, Nausea and Hunger together, and the
//   potato, spider eye          potato's is a 60% chance of the same kind.
//   suspicious stew             One effect chosen by the flower it was made
//                               from, so the grant is keyed by the *stack*
//                               rather than by the item - `damage` again.
//   honey bottle, milk          **Remove** effects rather than add them, which
//                               this channel cannot express at all.
//   chorus fruit                Teleports. Not an effect in any edition.
//
// Filling any of them means answering one of those questions first, and each
// answer is a feature. What must not happen is a second food-to-effect table
// somewhere else because this one looked closed.

/// The two rows a shared `case` label had swallowed, pinned by name.
///
/// Both were wrong because they sat in a group rather than in a row of their
/// own, and a group is exactly the shape a wrong value hides in: nothing about
/// `{6, 9.6f}` looks like a claim about chicken. Folding either back under a
/// neighbouring label is what makes these fail.
static_assert(foodValue(ItemId::CookedChicken).hunger == 6 &&
                  foodValue(ItemId::CookedChicken).saturation == 7.2f,
              "cooked chicken is the worst of the cooked meats - a 0.6 modifier, not mutton's 0.8");
static_assert(foodValue(ItemId::RabbitStew).hunger == 10 &&
                  foodValue(ItemId::RabbitStew).saturation == 12.0f,
              "rabbit stew restores more hunger than anything else you can eat, which is what "
              "five ingredients buy - it is not one of the soups");

/// **The Bedrock saturation formula, written once and applied to the three rows
/// that had Java's numbers until 2026-08-19.**
///
/// Bedrock states a food as `nutrition` plus a `saturation_modifier` keyword and
/// derives the saturation as `nutrition * modifier * 2`; Java states the
/// saturation outright, and for these three the two editions differ. So the
/// interesting failure is not a typo, it is somebody "correcting" a row back to
/// the number the Java wiki prints - and this fires on exactly that.
///
/// **Written as the formula rather than as three literals**, so a reader can see
/// which keyword each row claims. The keyword values are not asserted against
/// themselves either: each one is proved by a food this table already agreed
/// with before any of this changed, which is what makes them a measurement
/// rather than a definition.
constexpr float bedrockSaturation(int nutrition, float modifier) {
    return static_cast<float>(nutrition) * modifier * 2.0f;
}
/// **Compared with `nearly`, not with `==`, and that is not slackness.** Every
/// modifier keyword is a value like 0.3 that no float holds exactly, so
/// `3 * 0.3f * 2` and the literal `1.8f` are one unit in the last place apart -
/// they are the same number written two ways and the compiler knows it is not.
/// The tolerance is the file's own 0.001, three orders of magnitude tighter than
/// the smallest difference any of these rows could carry (0.2 against 0.4).
static_assert(nearly(bedrockSaturation(3, 0.3f), foodValue(ItemId::RawBeef).saturation, 0.001f) &&
                  nearly(bedrockSaturation(8, 0.8f), foodValue(ItemId::CookedBeef).saturation,
                         0.001f) &&
                  nearly(bedrockSaturation(2, 0.1f), foodValue(ItemId::Cookie).saturation,
                         0.001f) &&
                  nearly(bedrockSaturation(6, 1.2f), foodValue(ItemId::GoldenCarrot).saturation,
                         0.001f),
              "the four modifier keywords, each read off a row that was already right: low 0.3, "
              "good 0.8, poor 0.1, supernatural 1.2");
static_assert(nearly(foodValue(ItemId::SweetBerries).saturation, bedrockSaturation(2, 0.3f),
                     0.001f) &&
                  nearly(foodValue(ItemId::GlowBerries).saturation, bedrockSaturation(2, 0.3f),
                         0.001f) &&
                  nearly(foodValue(ItemId::DriedKelp).saturation, bedrockSaturation(1, 0.1f),
                         0.001f) &&
                  nearly(foodValue(ItemId::HoneyBottle).saturation, bedrockSaturation(6, 0.1f),
                         0.001f),
              "sweet and glow berries are nutrition 2 / \"low\" and dried kelp is 1 / \"poor\", "
              "and honey is 6 / \"poor\" - sweet_berries.json, glow_berries.json, dried_kelp.json "
              "and honey_bottle.json in bedrock-samples");
/// The negative twin, and it is the whole point of the block: Java's figures for
/// the same three rows must be refused. Without it the assert above could be
/// satisfied by editing `bedrockSaturation` to match whatever the table said,
/// and the tolerance above could be widened until everything agreed with
/// everything. **The margin is real rather than nominal** - 1.2 against 0.4 and
/// 0.2 against 0.6 are both a factor of three, so nothing here is a rounding
/// argument.
static_assert(!nearly(foodValue(ItemId::SweetBerries).saturation, 0.4f, 0.001f) &&
                  !nearly(foodValue(ItemId::GlowBerries).saturation, 0.4f, 0.001f) &&
                  !nearly(foodValue(ItemId::DriedKelp).saturation, 0.6f, 0.001f),
              "0.4, 0.4 and 0.6 are Java's numbers for these three and are what this table used "
              "to carry - Bedrock is the reference");

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
