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
/// **Contact damage: RESOLVED 2026-08-19 11:28, and it is still not built.**
/// Findings 178 and 584 disagreed - one said a campfire deals no contact
/// damage, the other that it already "burns creatures standing in it".
/// **178 is right. There is no campfire contact damage anywhere in this tree.**
///
/// Measured by searching the two files where damage actually lives, rather
/// than by re-reading `Main.cpp`: **`world/Player.cpp` contains zero
/// occurrences of "campfire" in any case**, and `world/Creature.cpp` contains
/// exactly one - a doc comment reading *"so lava, a campfire and an undead
/// caught at dawn all cook it alike"*, which is about **a creature's drops
/// being cooked when it dies to fire**, not about being damaged by standing in
/// one. Nothing in either file reads a campfire block.
///
/// **That single line is almost certainly where 584 came from, and the
/// mechanism is worth naming.** A sweep for "campfire" in the creature code
/// returns a sentence containing "campfire", "cook" and a list of fire sources
/// - which reads exactly like a burn rule to anything short of opening it. It
/// is the reverse of the arithmetic/cast trap that has cost this project five
/// wrong claims tonight: there the accessor hid a real use, here a real
/// mention hid the absence of one. **A name search answers "is this word
/// present", never "is this behaviour present", and the two differ in both
/// directions.**
///
/// **So the second-answer objection is now withdrawn.** The old text here
/// warned that a damage rule written in this file might duplicate a general
/// in-fire rule. There is no such rule to duplicate, so whoever builds this is
/// writing the first answer, not a second.
///
/// **Where the caller must live, named by symbol.** `world/Player.cpp` already
/// carries the hazard family this belongs to - the block whose own comment
/// reads *"The four hazards armour reduces, and the only four"*, applying
/// `kLavaDamage`, `kFireDamage`, `kMagmaDamage` and `kCactusDamage` through
/// `damagePlayer(player, ..., armour)`. **A campfire is a fifth member of that
/// family and belongs beside them**, armour-reduced like the other fire
/// sources, not in this header and not in `Main.cpp`. Creatures need the
/// equivalent in `Creature.cpp`.
///
/// **The absence above is proved by following the data, not by the name
/// search, and the difference matters.** "Zero occurrences of campfire in
/// `Player.cpp`" is weak evidence: a campfire could burn the player through a
/// hazard table or a predicate that never spells the word, which is the trap
/// that produced five wrong claims elsewhere tonight. The strong form is that
/// the gate is a **single equality**: `Player.cpp` builds `inFire` as
/// `inFire || block == BlockId::Fire`, one comparison against one enumerator,
/// in the scan over blocks the player occupies. **No campfire id can satisfy
/// it**, so the absence is structural rather than merely unobserved.
///
/// > **And that scan is where the fifth hazard has to be read - but do NOT
/// > simply widen `inFire` to accept campfires.** It has **three** readers,
/// > not one: the armour-reduced fire damage, a second branch further down,
/// > and an `!inLava && !inFire` test later still. Widening the predicate
/// > silently changes all three, which is `CLAUDE.md` bug shape #2 - the
/// > `isFurnace` case, where growing a family predicate to cover smokers
/// > turned eight `case` labels into dead code. **A campfire needs its own
/// > boolean beside `inFire`, gated into the damage branch only**, so that the
/// > other two readers keep the meaning they were written against. Whether a
/// > campfire should also *ignite* the player is a separate reference question
/// > and must be answered on its own evidence, not inherited by sharing a
/// > variable.
///
/// **What I deliberately did NOT write here, and why it is not laziness.** No
/// `kCampfireDamage` constant. `bedrock-samples` publishes no `blocks/`
/// directory at all - block behaviour is engine-side and unpublished - so any
/// magnitude would be wiki-sourced or remembered, and this file would then
/// carry a number with no provenance in the voice of one that has some.
///
/// > **And the number the previous version of this paragraph offered was
/// > already dangerous.** It said *"1 HP per tick (2 for a soul campfire) ...
/// > throttled by the ordinary half-second damage immunity"*. **Those two
/// > clauses do not agree.** One HP per tick, applied literally, is 20 HP per
/// > second and kills a full-health player in half a second; one HP per
/// > *application*, throttled at half a second, is 2 HP per second. A reader
/// > implementing the first sentence as written would produce an instant-death
/// > block and could reasonably believe they had followed the reference. That
/// > is bug shape #3 - a number ported into a field measured in a different
/// > unit - caught before anyone spent it. **Whoever lands this must source
/// > the magnitude *and* its unit together, and state which it is: damage per
/// > application, not damage per tick.**
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
