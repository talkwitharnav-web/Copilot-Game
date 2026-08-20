#pragma once

#include "item/Item.hpp"
#include "item/Smelting.hpp"
#include "world/Block.hpp"
#include "world/Tick.hpp"

#include <array>
#include <cstddef>

namespace game {

/// **The fourth cooker, and the one that is deliberately not a `Furnace`.**
///
/// A campfire cooks four things at once, each on its own thirty-second clock,
/// and it burns for nothing. Not one of those three sentences is true of a
/// furnace, and the overlap between the two is the single word "cook":
///
///  * **Four independent timers.** `Furnace` has exactly one `cookElapsed`,
///    because a furnace cooks exactly one thing. Items go onto a campfire one
///    tap at a time and therefore finish at different moments, so there is
///    nothing to share.
///  * **No fuel at all.** `Furnace::fuel`, `burnRemaining` and `burnTotal` are
///    the majority of that struct and every one of them would sit at zero
///    forever - and `tickFurnace`'s whole shape is "is it alight", which for a
///    campfire is always yes.
///  * **`sizeof(Furnace) == 48` is asserted in `Furnace.cpp` on purpose**, with
///    the member sum beside it in `WorldStore.hpp`, precisely to stop a field
///    being added to a struct that goes to disk as raw bytes. Widening it for
///    four timers would be arguing with a guard that is right.
///
/// So it is its own block entity, on the same arrangement as `Furnace` and
/// `Chest`: state that belongs to a particular block and could never fit in the
/// block id. Which campfire it is - ordinary or soul - stays in the id, because
/// that *is* its identity, and both cook (wiki `[[Soul Campfire]]`: "Just like
/// normal campfires, they can be used to cook food").
///
/// **Header-only, and that is not laziness.** `game/CMakeLists.txt` lists its
/// sources one by one rather than globbing them, and that file has another
/// owner right now; forty lines of `inline` costs nothing and touches nothing.
///
/// ---
///
/// **THE CAMPFIRE IS FULLY WIRED AND WORKS END TO END. Verified against
/// `Main.cpp` on 2026-08-19 at 11:02, by reading it rather than by trusting a
/// brief.** Interaction at `Main.cpp:7653-7676`, which asks `campfireCooks`
/// then `addToCampfire` on a trial copy before spending anything; the tick at
/// `:9944-9956` calling `tickCampfire` and spawning what it returns; break
/// handling at `:8024` and `:10931`; and a held-item hint at `:8179`.
///
/// **An earlier version of this very comment said "NOTHING ON THIS PAGE HAS A
/// CALLER" and that was false when written.** It is left described rather than
/// silently deleted, because the mistake is instructive and this file is where
/// the next person will look. Two audits (findings 178 and 584) reported "there
/// is no cooking path, no timer, nothing can be placed on it". That was true
/// when *they* were written and had since been closed at both ends - the
/// cooking side here and the call sites there - and nothing told either author.
/// I checked the half of the claim that concerned my own file, found it stale,
/// and then repeated the other half unchecked in the same edit.
///
/// **So the rule this file now carries, earned twice over: a claim that
/// something is ABSENT is the claim most likely to be wrong, and it rots in one
/// direction only** - things get built, and nothing goes back to update the
/// note that said they were missing. A negative claim in a header outlives
/// every ledger entry because the next reader has the file and not the ledger.
/// **Date them, name what would falsify them, and re-read source before
/// repeating one.** Acting on the version of this comment that stood for four
/// minutes would have meant writing a second interaction branch and a second
/// tick call for state that already has exactly one writer.
///
/// **Contact damage: BUILT 2026-08-19, for the player.** Findings 178 and 584
/// disagreed - one said a campfire deals no contact damage, the other that it
/// already burned creatures standing in it. 178 was right about the tree as it
/// stood, and the gap it named is now closed on the player's side and still
/// open on the creature's. The paragraphs below are corrected in place rather
/// than deleted, because the *reasoning* is what stopped this being built the
/// wrong way, and the wrong way is still available to anyone who reopens it.
///
/// > **What landed, by symbol, so it can be checked rather than believed.**
/// > `world/Player.cpp` declares `bool inCampfire` beside `inFire` (:803),
/// > fills it in the same occupied-cell scan as `inCampfire || isCampfire(block)`
/// > (:849), and reads it in exactly one damage branch (:1028) as
/// > `damagePlayer(player, kCampfireDamage, false, kNoArmour)`.
/// > `Survival.hpp:387` defines `kCampfireDamage = kFireDamage` - derived from
/// > the fire figure rather than written as a second literal 1.
/// > **Falsified by** `inCampfire` appearing on any line of `Player.cpp` other
/// > than those three, or by `kCampfireDamage` acquiring a literal of its own.
///
/// **The measurement that stood here is now stale in exactly the direction
/// this file warns about, and is replaced rather than quietly dropped.** It
/// read *zero* occurrences of "campfire" in `world/Player.cpp` and exactly one
/// in `world/Creature.cpp`. Re-measured 2026-08-19, case-insensitively, so
/// that `isCampfire`, `BlockId::Campfire` and `BlockId::SoulCampfire` all
/// count: **`Player.cpp` 20, `Creature.cpp` 2** - against an invented token in
/// the same files returning 0 and `damage` returning 176, so the counter was
/// looking rather than broken. Neither of `Creature.cpp`'s two is a damage
/// rule: one is the old doc comment about **a creature's drops being cooked
/// when it dies to fire**, the other a note that powder snow borrowed this
/// hazard's *shape* for its own flag.
///
/// **The older of those two lines is almost certainly where 584 came from, and
/// the mechanism is worth naming.** A sweep for "campfire" in the creature
/// code returns a sentence containing "campfire", "cook" and a list of fire
/// sources
/// - which reads exactly like a burn rule to anything short of opening it. It
/// is the reverse of the arithmetic/cast trap that has cost this project five
/// wrong claims tonight: there the accessor hid a real use, here a real
/// mention hid the absence of one. **A name search answers "is this word
/// present", never "is this behaviour present", and the two differ in both
/// directions.**
///
/// **The second-answer objection was withdrawn before the work, and the
/// withdrawal held.** The oldest text here warned that a damage rule written
/// in this file might duplicate a general in-fire rule. There was no such rule
/// to duplicate; what landed is the first answer, and it lives in `Player.cpp`
/// beside the other hazards rather than in this header.
///
/// **Where the caller lives - and the one clause of this paragraph that was
/// wrong.** `world/Player.cpp` carries the hazard family this belongs beside:
/// the block whose own comment reads *"The four hazards armour reduces, and
/// the only four"*, applying `kLavaDamage`, `kFireDamage`, `kMagmaDamage` and
/// `kCactusDamage` through `damagePlayer(player, ..., armour)`. A campfire is
/// a fifth member of the *fire* family and sits beside them, which is exactly
/// where it went - not in this header and not in `Main.cpp`.
///
/// > **It is NOT a fifth member of the armour-reduced family, and the sentence
/// > that used to stand here told the implementer to make it one.**
/// > minecraft.wiki, *Campfire*: *"Damage taken is considered fire damage, so
/// > armor itself does not reduce damage caused by campfire; to do so, the
/// > player needs the Resistance potion effect, or the Protection or Fire
/// > Protection enchantments."* It is therefore applied with `kNoArmour`.
/// > Enchantments do not exist in this project, so Fire Resistance is the
/// > entire counterplay - which the existing fireproof gate already provides.
/// >
/// > **Following the old clause would have broken a true comment in order to
/// > obey a false one.** `Survival.hpp`'s "the only four" would have quietly
/// > become false while the player was silently over-protected, and that
/// > comment is what the next reader trusts. `CLAUDE.md` bug shape #16 in its
/// > most expensive form: a confident comment, in this codebase's voice,
/// > arguing for a breaking edit - and no compiler, sweep or soak can catch
/// > one. It was caught because the implementer fetched the source instead of
/// > trusting the handoff, which is the only instrument that works here.
///
/// **Creatures still take none of this, measured 2026-08-19 by following the
/// data rather than the name.** `Creature.cpp`'s occupied-cell walk builds its
/// heat flag as `inFire = inFire || (!immuneToHeat && here == BlockId::Fire)`
/// - one equality against one enumerator - plus a separate lava *fluid*
/// sample. **No campfire id can satisfy either**, so the absence is structural
/// and not merely unobserved. The walk beside it already carries a second flag
/// for powder snow, so the cost of a third is one comparison in a loop that is
/// running anyway. That file has another owner. **Falsified by** `isCampfire`
/// or either campfire enumerator appearing anywhere in `Creature.cpp`.
///
/// **The absence was proved by following the data rather than the name, and
/// the difference is why the claim survived contact with the code.** "Zero
/// occurrences of campfire in `Player.cpp`" was weak evidence on its own: a
/// campfire could have burned the player through a hazard table or a predicate
/// that never spells the word, which is the trap that produced five wrong
/// claims elsewhere in one night. The strong form was that the gate was a
/// **single equality** - `inFire` built as `inFire || block == BlockId::Fire`,
/// one comparison against one enumerator, in the scan over the blocks the
/// player occupies - so no campfire id could satisfy it, and the absence was
/// structural rather than merely unobserved.
///
/// > **That scan is where the fifth hazard was read, and it did NOT widen
/// > `inFire` - which is the part of this a later tidy-up would undo first.**
/// > `inFire` has **three** readers, not one: the armour-reduced fire damage,
/// > a `burningSeconds` branch further down, and an `!inLava && !inFire` test
/// > later still. Widening it would have silently changed all three -
/// > `CLAUDE.md` bug shape #2, the `isFurnace` case, where growing a family
/// > predicate to cover smokers turned eight `case` labels into dead code. So
/// > the campfire got **its own boolean beside `inFire`, gated into the damage
/// > branch alone**, and the other two readers kept the meaning they were
/// > written against. Merging the two flags would reintroduce all three
/// > regressions at once, on a green build, with nothing to report it.
/// >
/// > **Whether a campfire also *ignites* the player was the separate
/// > reference question, and the answer is no.** minecraft.wiki, *Campfire*:
/// > *"Campfires do not cause lasting burning."* Nothing touches
/// > `burningSeconds` - which the three-line footprint recorded at the top of
/// > this block also demonstrates, since none of those lines is the ignition
/// > site or the burn guard.
///
/// **This header still carries no magnitude, and that is deliberate rather
/// than an omission.** `kCampfireDamage` lives in `Survival.hpp` beside every
/// other hazard figure. `bedrock-samples` publishes no `blocks/` directory at
/// all - block behaviour is engine-side and unpublished - so the number is
/// wiki-sourced, and it is stated *where it is spent* rather than restated
/// here in the voice of a value with provenance it would not have.
///
/// > **The unit was the trap, and the fetch that landed this disarmed it.** An
/// > earlier draft of this paragraph offered "1 HP per tick (2 for a soul
/// > campfire), throttled by the ordinary half-second damage immunity" - two
/// > clauses that do not agree. One HP per *tick*, taken literally, is 20 HP a
/// > second and kills a full-health player in half of one; one HP per
/// > *application*, throttled at half a second, is 2 HP a second. A reader
/// > implementing the first as written would produce an instant-death block
/// > and could reasonably believe they had followed the reference: bug shape
/// > #3, a number ported into a field measured in a different unit.
/// > minecraft.wiki, *Campfire*, settles it - *"Campfires deal 1HP every tick
/// > (although damage immunity reduces this to once every half-second)"* - and
/// > the parenthesis is the meaning. **One point per application**, throttled
/// > by the immunity window this project already owns as
/// > `kInvulnerableSeconds = 0.5f` and `kHazardInterval = 0.5f`, so it needs
/// > no interval of its own, exactly as `kMagmaDamage` needs none.
///
/// **The soul campfire's magnitude is still unsourced, and both ids are
/// charged the same point on purpose.** The wiki's damage paragraph says
/// "Campfires deal 1HP" and distinguishes the variants nowhere, so the scan
/// spends one figure through the family predicate `isCampfire`. That is a
/// refusal to invent a number, **not** a claim that the two are equal. If a
/// separate figure is ever sourced, the split belongs beside `kCampfireDamage`
/// in `Survival.hpp`, and the scan must then tell the two ids apart.
///
/// **No lit state exists anywhere in this project**, verified 2026-08-19:
/// `BlockId` has `Campfire` and `SoulCampfire` and no unlit twin, and this
/// header holds no lit-state predicate. The reference lights a campfire by
/// default on placement, so unconditional damage is correct *today* - and that
/// constraint is written into `Player.cpp` at the scan line, the place that
/// would become wrong, rather than only here.
struct Campfire;

/// Ordinary or soul, and nothing else.
///
/// **This belongs in `Block.hpp` beside `isFurnace`**, which is where every
/// other family predicate lives, and it is here only because that file has
/// another owner this session. Move it when that is free - the point of a
/// family predicate is that there is one of it, and a second spelling of
/// "which ids are campfires" is exactly the drift `isFurnace`'s own comment
/// warns about.
constexpr bool isCampfire(BlockId id) {
    return id == BlockId::Campfire || id == BlockId::SoulCampfire;
}

/// How many things cook at once. Wiki `[[Cooking]]`: "Up to four uncooked food
/// items can be placed on a campfire, and they are cooked simultaneously."
constexpr std::size_t kCampfireSlots = 4;

/// One item's stay on the fire, in **wall-clock seconds**.
///
/// Wiki `[[Campfire]]`: "Food items take 30 seconds (600 ticks) to cook,
/// compared to 10 seconds for furnaces or 5 seconds for smokers." The wiki
/// states it in ticks and this is seconds, so `tick::kSeconds` is where the
/// unit changes hands - the same division `kHopperTransferSeconds` does, rather
/// than a bare `30.0f` that no longer says where it came from.
///
/// **There is no cooker speed multiplier here and there must never be one.**
/// `cookSpeed` exists because a smoker runs its *whole tick* at double rate;
/// a campfire has one rate, it is slower than everything, and that slowness is
/// the price of needing no fuel.
constexpr float kCampfireCookSeconds = 600.0f * tick::kSeconds;

/// > Fails if: the tick rate is changed, which is never the fix for anything.
static_assert(kCampfireCookSeconds == 30.0f,
              "600 ticks at twenty a second is thirty seconds - the reference's own number");
/// The published relationship between the three cookers, in the one expression
/// that spans all of them. A campfire is three furnaces and six smokers.
///
/// > Fails if: `cookSeconds` is given a campfire branch, or this stops being
/// > the slowest way to cook anything.
static_assert(kCampfireCookSeconds == 3.0f * cookSeconds(BlockId::Furnace) &&
                  kCampfireCookSeconds == 6.0f * cookSeconds(BlockId::Smoker),
              "30 s against the furnace's 10 and the smoker's 5, so a campfire is the slow "
              "way to cook and the fuel it saves is what it is paid with");

/// What is sitting on one campfire and how far through each item is.
///
/// **A block entity**, for exactly the reason `Furnace`'s and `Chest`'s are.
struct Campfire {
    std::array<ItemStack, kCampfireSlots> items{};

    /// Seconds each slot has spent cooking. Meaningless where the matching slot
    /// is empty, and zeroed there by `tickCampfire` so a stale value can never
    /// be inherited by whatever is put down next.
    std::array<float, kCampfireSlots> elapsed{};

    /// Nothing on it, so it can be forgotten rather than written to disk.
    bool idle() const {
        for (const ItemStack& slot : items) {
            if (!slot.empty()) {
                return false;
            }
        }
        return true;
    }
};

/// Whether a campfire will take this item.
///
/// **The same expression as `smokerAccepts` in `Furnace.cpp`, and that is the
/// rule rather than a coincidence.** Wiki `[[Smelting]]`: "All food recipes can
/// be used in a furnace or smoker. Food can alternatively be cooked on a
/// campfire." So the set is "everything that cooks into food", which is asked
/// of the *result* and therefore needs no second table to drift from - the ten
/// raw foods, the potato and the kelp, and nothing made of stone or ore.
///
/// **It is a restatement all the same, and it should not stay one.** It wants
/// to live in `Smelting.hpp` next to `fuelRemainder` so both cookers read the
/// one copy; that file has another owner this session, which is the only
/// reason it is here.
inline bool campfireCooks(ItemId item) {
    return isFood(smeltResult(item).item);
}

/// Puts one item on the fire, and says whether there was room.
///
/// One per use, which is the reference's interaction: wiki `[[Cooking]]`,
/// "Items are added to the campfire slots by pressing the use button", with no
/// screen anywhere in it. **The caller must not spend the item unless this
/// returns true**, or a campfire with four things on it quietly eats the fifth.
inline bool addToCampfire(Campfire& campfire, ItemId item) {
    if (item == ItemId::None || !campfireCooks(item)) {
        return false;
    }
    for (std::size_t i = 0; i < kCampfireSlots; ++i) {
        if (!campfire.items[i].empty()) {
            continue;
        }
        campfire.items[i] = ItemStack{item, 1, 0};
        campfire.elapsed[i] = 0.0f;
        return true;
    }
    return false;
}

/// What came off the fire this tick. Up to four, because four could finish
/// together if they went on together.
struct CampfireDone {
    std::array<ItemStack, kCampfireSlots> stacks{};
    int count = 0;
};

/// Advances one campfire and **returns** what popped off it.
///
/// **Computed here, applied by the caller**, which is the rule the whole engine
/// is shaped by: this function has no idea what an `ItemEntities` is, so it can
/// never spawn a drop into a chunk that is not loaded. Wiki `[[Cooking]]`: "The
/// items drop from the campfire automatically after they finish cooking" - they
/// do not go to the player and there is no output slot to take them from.
///
/// **Nothing is ever destroyed here.** If an item somehow has no cooked form -
/// an id that stopped smelting between one version and the next - it comes back
/// off the fire as itself rather than vanishing. Three separate item losses
/// this session were a path that assumed a lookup would succeed.
inline CampfireDone tickCampfire(Campfire& campfire, float deltaSeconds) {
    CampfireDone done;
    for (std::size_t i = 0; i < kCampfireSlots; ++i) {
        ItemStack& cooking = campfire.items[i];
        if (cooking.empty()) {
            campfire.elapsed[i] = 0.0f;
            continue;
        }
        campfire.elapsed[i] += deltaSeconds;
        if (campfire.elapsed[i] < kCampfireCookSeconds) {
            continue;
        }
        campfire.elapsed[i] -= kCampfireCookSeconds;
        const ItemStack result = smeltResult(cooking.item);
        // **One item leaves, not the slot.** A slot holds one by construction,
        // but a count out of a file need not, and consuming the whole stack for
        // one result is the shape of loss this project has already paid for
        // three times. Decrementing conserves whatever the number turns out to
        // be.
        done.stacks[static_cast<std::size_t>(done.count)] =
            result.empty() ? ItemStack{cooking.item, 1, 0} : result;
        ++done.count;
        if (--cooking.count <= 0) {
            cooking = ItemStack{};
            campfire.elapsed[i] = 0.0f;
        }
    }
    return done;
}

} // namespace game
