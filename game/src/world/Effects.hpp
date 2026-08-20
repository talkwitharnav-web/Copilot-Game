#pragma once

#include "world/Tick.hpp"

#include <array>
#include <cstdint>

namespace game::effects {

/// Every status effect the game can put on the player.
///
/// **The numbers are Bedrock's own effect ids**, so the list is deliberately not
/// contiguous where Bedrock's is not, and a saved effect is a number that means
/// the same thing as it does in the reference. **Slow Falling is the proof it is
/// Bedrock's numbering and not Java's**: Bedrock gives it 26 and Java gives it
/// 27.
///
/// The two gaps are 24 and 25, and **neither of them is a Java-only effect** -
/// that claim stood here until 2026-08-19 and is backwards. 24 is Levitation,
/// which both editions have and we do not model because nothing here shoots a
/// shulker bullet or grows a chorus fruit. 25 is **Fatal Poison, which is
/// Bedrock-only** - poison that is not floored at one health - and it is
/// missing because nothing inflicts it yet, not because the reference lacks it.
/// The genuinely Java-only effects - luck, unluck, glowing, dolphin's grace -
/// are not here and are not planned.
enum class Effect : std::uint8_t {
    None = 0,
    Speed = 1,
    Slowness = 2,
    Haste = 3,
    MiningFatigue = 4,
    Strength = 5,
    InstantHealth = 6,
    InstantDamage = 7,
    JumpBoost = 8,
    Nausea = 9,
    Regeneration = 10,
    Resistance = 11,
    FireResistance = 12,
    WaterBreathing = 13,
    Invisibility = 14,
    Blindness = 15,
    NightVision = 16,
    Hunger = 17,
    Weakness = 18,
    Poison = 19,
    Wither = 20,
    HealthBoost = 21,
    Absorption = 22,
    Saturation = 23,
    SlowFalling = 26,
    Count = 27,
};

/// What an effect is called and what colour its bottle and its particles take.
///
/// The colours are the reference's own post-1.19.80 values, packed `0xRRGGBB`.
/// They are what the potion sprites are tinted with at staging time, so a
/// potion of swiftness is the right blue without a per-item tint at run time.
struct EffectInfo {
    const char* name;
    bool harmful;
    std::uint32_t colour;
    /// Whether it lands once and is gone rather than counting down. The two
    /// instant ones are the whole of this, and they are the reason `apply`
    /// cannot simply push a timer for everything.
    bool instant = false;
};

/// The name, the colour and whether it lands once.
///
/// **One caller outside this file** - `survival::foodGrantsAreUsable`
/// (`Survival.hpp`) reads `instant` to prove no food row asks for an effect
/// that cannot be stored.
///
/// **`name` is read, and this comment claimed the opposite until 2026-08-19.**
/// It read "Nothing reads `name` or `colour` anywhere". Half of that stayed
/// true and half was falsified the same evening by an edit three hundred lines
/// below it: `Effects::apply` now tests `info.name[0] == '\0'` as a structural
/// guard, which is how it refuses an `Effect` whose `effectInfo` row was never
/// written. The field is therefore load-bearing rather than decorative, and
/// the trap for the next reader is specific - **a new effect given an empty
/// name is now silently refused rather than merely nameless**, so name it even
/// if nothing will ever print it. Falsified the day that test leaves `apply`.
///
/// **`colour` still reaches nothing**, and that half is true as of 2026-08-19:
/// no effect colour is drawn anywhere, and the potion sprites are tinted from
/// these values by hand at staging time rather than by this. Falsified the day
/// a status-effect HUD or a tinted potion icon reads it. See
/// `Effects::secondsLeft` for the rest of that story.
constexpr EffectInfo effectInfo(Effect effect) {
    switch (effect) {
    case Effect::Speed:
        return {"Speed", false, 0x33EBFF};
    case Effect::Slowness:
        return {"Slowness", true, 0x8BAFE0};
    case Effect::Haste:
        return {"Haste", false, 0xD9C043};
    case Effect::MiningFatigue:
        return {"Mining Fatigue", true, 0x4A4217};
    case Effect::Strength:
        return {"Strength", false, 0xFFC700};
    case Effect::InstantHealth:
        return {"Instant Health", false, 0xF82423, true};
    case Effect::InstantDamage:
        return {"Instant Damage", true, 0xA9656A, true};
    case Effect::JumpBoost:
        return {"Jump Boost", false, 0xFDFF84};
    case Effect::Nausea:
        return {"Nausea", true, 0x551D4A};
    case Effect::Regeneration:
        return {"Regeneration", false, 0xCD5CAB};
    case Effect::Resistance:
        return {"Resistance", false, 0x9146F0};
    case Effect::FireResistance:
        return {"Fire Resistance", false, 0xFF9900};
    case Effect::WaterBreathing:
        return {"Water Breathing", false, 0x98DAC0};
    case Effect::Invisibility:
        return {"Invisibility", false, 0xF6F6F6};
    case Effect::Blindness:
        return {"Blindness", true, 0x1F1F23};
    case Effect::NightVision:
        return {"Night Vision", false, 0xC2FF66};
    case Effect::Hunger:
        return {"Hunger", true, 0x587653};
    case Effect::Weakness:
        return {"Weakness", true, 0x484D48};
    case Effect::Poison:
        return {"Poison", true, 0x87A363};
    case Effect::Wither:
        return {"Wither", true, 0x736156};
    case Effect::HealthBoost:
        return {"Health Boost", false, 0xF87D23};
    case Effect::Absorption:
        return {"Absorption", false, 0x2552A5};
    case Effect::Saturation:
        return {"Saturation", false, 0xF82423};
    case Effect::SlowFalling:
        return {"Slow Falling", false, 0xF3CFB9};
    default:
        return {"", false, 0xFFFFFF};
    }
}

/// How many ids `effectInfo` actually names.
///
/// **This exists because the `default:` above returns a real value.** An
/// enumerator added without a row falls into it and comes back white, unnamed
/// and - the part that bites - `instant = false`, so `apply` would push a
/// countdown for something meant to land once and be gone. Counting the named
/// rows here and asserting the total is what stops that being silent: the
/// empty name is the one thing a missing row cannot fake.
///
/// The loop runs over raw ids rather than the enumerators because Bedrock's
/// numbering has gaps - 24 and 25, for the reasons on `Effect` itself - and
/// those gaps land in the `default:` too, which is exactly what makes this
/// count the real rows.
constexpr int namedEffectCount() {
    int named = 0;
    for (int id = 1; id < static_cast<int>(Effect::Count); ++id) {
        if (effectInfo(static_cast<Effect>(id)).name[0] != '\0') {
            ++named;
        }
    }
    return named;
}

/// The ones that can be *running* - everything named except the two that land
/// once. This is how many slots the player needs, and nothing else.
constexpr int storableEffectCount() {
    int storable = 0;
    for (int id = 1; id < static_cast<int>(Effect::Count); ++id) {
        const EffectInfo info = effectInfo(static_cast<Effect>(id));
        if (info.name[0] != '\0' && !info.instant) {
            ++storable;
        }
    }
    return storable;
}

/// Twenty-four named, two of them instant. **Add an enumerator to `Effect` and
/// this fails until `effectInfo` names it**, which is the entire point - and
/// the second number failing on its own means the new row was marked instant.
static_assert(namedEffectCount() == 24 && storableEffectCount() == 22,
              "every Effect enumerator needs a row in effectInfo");

/// One effect running on the player.
struct ActiveEffect {
    Effect id = Effect::None;
    /// Zero is the roman numeral I, exactly as the reference stores it.
    int amplifier = 0;
    float secondsLeft = 0.0f;
};

/// How many can run at once: **one slot per storable id, derived rather than
/// guessed.**
///
/// It read 8 while the comment beside it claimed one slot per effect id, and
/// the two cannot both be true - there are 22 storable ids. `apply` returns
/// false when every slot is taken, so a ninth effect was refused in silence,
/// and a beacon plus a brewing stand plus a golden apple gets there. Deriving
/// it from `effectInfo` makes the comment true and keeps it true.
///
/// Free to widen? **No longer, and this comment used to say the opposite.** It
/// read "Free to widen: `Effects` is not in `SavedPlayer` (`WorldStore.hpp`), so
/// no file on disk is measured in these - which also means no effect survives a
/// quit". Both halves became false on 2026-08-19: `SavedPlayer` carries
/// `std::array<SavedEffect, kSavedEffectSlots>` with `kSavedEffectSlots` = 32,
/// `Main.cpp` fills it, and effects do survive a quit. Recorded at length
/// because it is the dangerous kind of wrong comment - it argued *for* an edit
/// (widen this freely) that would now silently truncate every save.
///
/// **What constrains it now.** The save site holds
/// `static_assert(effects::kMaxActive <= game::kSavedEffectSlots)`, so this may
/// grow to 32 and no further without a `kChunkFormatVersion`-style bump on the
/// player record. 22 today, ten spare. `Player.hpp` says the same thing from the
/// other side.
///
/// `everyStorableFits` at the foot of this file is what proves the count is
/// enough, by applying one of each through the real `apply` rather than
/// comparing this against itself.
constexpr int kMaxActive = storableEffectCount();

/// Everything currently on the player.
///
/// **One slot per effect id, overwrite-or-ignore, and nothing hidden.** Java
/// keeps a weaker effect underneath a stronger one and hands it back when the
/// stronger expires; Bedrock deletes it outright, and that is both the reference
/// here and much the simpler thing to store.
class Effects {
public:
    /// The reference's own rule, and all three branches matter:
    /// a **higher amplifier** overwrites whatever is there, the **same
    /// amplifier with a longer time** overwrites it, and anything weaker or
    /// shorter is **discarded entirely** - drinking Regeneration I while
    /// Regeneration II is running does nothing at all.
    ///
    /// "Higher levels overwrite lower levels, and higher durations overwrite
    /// lower durations of the same level" (`minecraft.wiki/w/Effect`), and the
    /// deletion is Bedrock's: "In *Bedrock Edition*, when a stronger effect
    /// overrides a weaker effect, the weaker effect is deleted and does not
    /// return."
    ///
    /// **The fourth case - a *lower* amplifier with a *longer* duration - is
    /// not stated anywhere in the reference**, so it is ours: we discard it,
    /// which is the published rule read literally. Written down because it is
    /// the branch a future reader is most likely to "fix" from memory.
    ///
    /// `seconds` is in seconds and so is `ActiveEffect::secondsLeft`; the
    /// reference publishes durations in ticks and the potion table converts
    /// once, at `item::kPotions`.
    ///
    /// Returns whether anything changed, which is what tells the caller whether
    /// to spend the item.
    ///
    /// `constexpr` only so the asserts at the foot of the file can build a
    /// player with an effect running and check what the formulas do to them.
    constexpr bool apply(Effect effect, int amplifier, float seconds) {
        // **Three refusals, and the middle one is new.** `None` is not an
        // effect; an *unnamed* id is a gap in Bedrock's numbering or an
        // enumerator whose `effectInfo` row was never written; an instant effect
        // lands once and is never stored.
        //
        // **The unnamed test is what makes this agree with `storableEffectCount`
        // by construction.** That function counts exactly
        // `name[0] != '\0' && !instant`, and this now refuses exactly its
        // complement - so the slot count and the set of things that can occupy a
        // slot are two readings of one predicate rather than two predicates that
        // happen to match today. Without it, `Effect::Count` grows past a gap,
        // the slot count does not, and `apply` cheerfully banks a nameless
        // effect into a slot the HUD cannot draw and the save cannot round-trip.
        // The `static_assert` at `namedEffectCount` fires on a *missing* row;
        // this handles the ids that are deliberately absent.
        const EffectInfo info = effectInfo(effect);
        if (effect == Effect::None || info.name[0] == '\0' || info.instant) {
            return false;
        }
        for (ActiveEffect& active : m_active) {
            if (active.id != effect) {
                continue;
            }
            if (amplifier > active.amplifier ||
                (amplifier == active.amplifier && seconds > active.secondsLeft)) {
                active.amplifier = amplifier;
                active.secondsLeft = seconds;
                return true;
            }
            return false;
        }
        for (ActiveEffect& active : m_active) {
            if (active.id == Effect::None) {
                active = {effect, amplifier, seconds};
                return true;
            }
        }
        return false;
    }

    /// The strength this effect is running at, as a roman numeral: **0 when it
    /// is not running at all**, 1 for I, 2 for II. Every consumer wants it this
    /// way round, because zero then means "no effect" without a second test.
    constexpr int level(Effect effect) const {
        for (const ActiveEffect& active : m_active) {
            if (active.id == effect) {
                return active.amplifier + 1;
            }
        }
        return 0;
    }

    /// How long this effect has left, in **seconds**, or zero when it is not
    /// running.
    ///
    /// **One gameplay caller and no display caller**, and the distinction is
    /// the whole of why this note exists. `Player::tickPassiveTimers`
    /// (`Player.cpp`) reads it every frame to tell a fresh Absorption grant
    /// from a spent pool - a *rise* in this number is the only evidence that
    /// `apply` accepted a new grant - so **deleting this breaks the absorption
    /// hearts**, not a HUD that was never built.
    ///
    /// What is still true is the display half: together with `all` and
    /// `count`, nothing in the game can enumerate what is running, so there is
    /// no effect list on the HUD and no timer beside it. Wanted, and it belongs
    /// in `Main.cpp` beside the hunger and health bars - not here.
    ///
    /// **This note read "No caller, 2026-08-18" for a day.** It was written
    /// about the display side and was true then; the absorption reconcile
    /// arrived afterwards and nothing came back to update it, which left an
    /// invitation to delete a function the game depends on. That is the shape
    /// to watch for in every other dormancy note in this file.
    constexpr float secondsLeft(Effect effect) const {
        for (const ActiveEffect& active : m_active) {
            if (active.id == effect) {
                return active.secondsLeft;
            }
        }
        return 0.0f;
    }

    /// Counts every effect's remaining time down by one frame, in seconds.
    ///
    /// **Duration only.** What a running effect *does* on its own cadence -
    /// regeneration healing, poison and wither hurting - is not here: each of
    /// those keeps a timer at the caller, in `Player.cpp`, because the interval
    /// is set by the amplifier and only the caller owns health. So this is a
    /// clock, not a scheduler, and the effect that expires this frame has
    /// already stopped acting by the time the caller looks.
    void tick(float deltaSeconds) {
        for (ActiveEffect& active : m_active) {
            if (active.id == Effect::None) {
                continue;
            }
            active.secondsLeft -= deltaSeconds;
            if (active.secondsLeft <= 0.0f) {
                active = {};
            }
        }
    }

    /// What milk does, and what dying does.
    void clear() {
        for (ActiveEffect& active : m_active) {
            active = {};
        }
    }

    /// What a honey bottle does, which is poison and nothing else.
    ///
    /// **It has a caller, and this comment said it did not.** It read "Unused
    /// today ... Swept again on 2026-08-19 and still uncalled". `feedPlayer`
    /// (`Player.cpp`) calls it - `if (value.removes != Effect::None)
    /// clearOne(value.removes)` - driven by `FoodValue::removes` in the food
    /// table, and `Survival.hpp` asserts that exactly one row sets it. So the
    /// gap this described is closed and the note was the third dormancy claim in
    /// this file to rot after the thing it described started being used. **A
    /// negative claim is the fastest-rotting kind**, which is why the two below
    /// carry the date they were last swept and this one now names its caller
    /// instead of counting them.
    ///
    /// **The rule, from the primary source rather than the wiki** (checked
    /// 2026-08-20): `Mojang/bedrock-samples`
    /// `behavior_pack/items/honey_bottle.json` publishes
    /// `"remove_effects": ["poison"]` inside `minecraft:food` - **poison alone,
    /// a list with exactly one entry.** So the call is `clearOne(Effect::Poison)`
    /// and *not* "clear the harmful ones", which is the natural thing to write
    /// and would also cure Wither, Hunger and Slowness off one bottle. That is
    /// what the food table encodes, one effect per row rather than a mask.
    ///
    /// **Milk is not sourced the same way and must not be assumed to be.**
    /// `behavior_pack/items/milk_bucket.json` is a 404 - checked the same day,
    /// against `honey_bottle.json` returning 200, so that is the repository's
    /// edge and not a bad URL. Milk clearing everything is engine-side and the
    /// wiki is its authority; `clear()` above already implements it and already
    /// has a caller.
    void clearOne(Effect effect) {
        for (ActiveEffect& active : m_active) {
            if (active.id == effect) {
                active = {};
            }
        }
    }

    /// Every slot, running or empty, for a caller that wants to enumerate.
    ///
    /// **Unused today, and kept because the HUD effect list is wanted and this
    /// is the shape it needs.** Re-swept 2026-08-19: still nothing draws it.
    /// **Not an invitation to delete** - the same sentence sat over
    /// `secondsLeft` while a gameplay caller was using it, so treat a dormancy
    /// note here as a claim to re-check rather than a licence.
    constexpr const std::array<ActiveEffect, kMaxActive>& all() const { return m_active; }

    /// How many slots are occupied.
    ///
    /// **No runtime caller, and it cannot go stale anyway**: it is driven at
    /// compile time by `everyStorableFits` at the foot of this file, which is
    /// what a dormancy note should look like when one is available.
    constexpr int count() const {
        int n = 0;
        for (const ActiveEffect& active : m_active) {
            if (active.id != Effect::None) {
                ++n;
            }
        }
        return n;
    }

private:
    std::array<ActiveEffect, kMaxActive> m_active{};
};

// ---------------------------------------------------------------------------
// What each effect actually does. Every number here is Bedrock's, and where
// Bedrock and Java disagree the divergence is stated at the function.
//
// **Five of the twenty-four have no consumer in this file and no reader in the
// three files that would hold one** - `Main.cpp`, `item/Item.hpp` and this one,
// swept function by function on 2026-08-18 after `meleeDamage` was found with no
// caller and Strength and Weakness turned out to have been doing nothing since
// the day they were brewable. **Re-measured 2026-08-19** and it was six then:
//
//   Nausea, Blindness      - a post-process each, so they belong to the
//                            renderer and nothing here would help.
//   Invisibility           - skips drawing the player and held item.
//                            **Brewable today** (`item::kPotions`, base and
//                            extended rows), so two potions are drinkable and
//                            do literally nothing.
//   HealthBoost            - +4 max health per level, which is `Survival.hpp`'s
//                            twenty to widen, not a number this file owns.
//   Saturation             - refills the food bar directly; `Survival.hpp`.
//
// **NightVision was the sixth row and it is now wired**, which is why this list
// is five (finding 10358). `Main.cpp` reads
// `player.effects.level(Effect::NightVision)` and swaps `sky::kAmbientFloor`
// for `sky::lighting(0.25f).ambient` - noon's own ambient, off the day curve
// rather than a literal - in the `setSunLighting` call. Search for
// `Effect::NightVision` rather than trusting a line number.
//
// **A row that lies about a wired effect is worse than no table at all, and
// this one did for a while**, so: the sweep above is `Effect::<name>` over
// those three files, graded rather than asserted - `Absorption`,
// `InstantHealth`, `InstantDamage`, `None` and `Count` all fire in `Main.cpp`
// while an invented enumerator returns 0, so a zero for the five above carries
// information. **What it cannot see** is a reader in a file not on that list;
// `Creature.cpp` is the obvious candidate and contains no `effects.level` at
// all, so the player's effect state is not plumbed into creatures - wiring
// Invisibility is a plumbing job rather than one call site.
//
// No accessor is written for any of the five, deliberately: an accessor with no
// caller is the exact shape this sweep exists to find. What is written down is
// where each one goes, so the next reader does not have to sweep again.
//
// Absorption is of a different kind again - granted by the two apples, and
// spent by a pool on `Player` rather than by anything in this file. See
// `absorptionPoints`.
// ---------------------------------------------------------------------------

/// Multiplier on how fast the player walks, runs and swims.
///
/// Speed is **+20% per level and Slowness -15% per level**, and both apply, so
/// drinking one while the other runs leaves you with the product rather than
/// with whichever was drunk last.
constexpr float moveSpeedScale(const Effects& effects) {
    float scale = 1.0f;
    for (int level = effects.level(Effect::Speed); level > 0; --level) {
        scale *= 1.2f;
    }
    for (int level = effects.level(Effect::Slowness); level > 0; --level) {
        scale *= 0.85f;
    }
    return scale;
}

/// Multiplier on how fast blocks come apart.
///
/// **Neither of Bedrock's two effects is Java's, and both of them are two terms
/// rather than one.** Bedrock multiplies the speed *and* the damage: haste is
/// `(1 + 0.2n)` on the speed and `1.2^n` on the damage, so Haste II is 2.016
/// rather than Java's flat 1.4; mining fatigue is `0.3^n` on the speed and
/// `0.7^n` on the damage, so one level is **0.21** and not 0.3. Both terms land
/// on the same product here, because the only thing either of them can change
/// is how long the block takes. The fatigue term was half-written - the speed
/// side alone - which made every level of it 43% too kind.
///
/// **The source is `minecraft.wiki/w/Breaking`, and specifically its
/// pseudo-code, not the two effect pages.** Written down because an auditor
/// checked `Haste` and `Mining Fatigue` on 2026-08-19, found only `0.3^level`
/// with no edition tag, and reported the fatigue figure unverified. `Breaking`
/// carries both halves and tags both: "Mining Fatigue decreases the speed by
/// multiplying by 0.3^min(level,4) in *Java Edition* or by (0.3^level)x(0.7^level)
/// in *Bedrock Edition*", and its pseudo-code marks `damage *= 0.7 ^ level` and
/// `damage *= 1.2 ^ level` Bedrock-only. Checking the effect page alone gets
/// this wrong in the direction of deleting a correct term.
constexpr float miningSpeedScale(const Effects& effects) {
    float scale = 1.0f;
    const int haste = effects.level(Effect::Haste);
    if (haste > 0) {
        scale *= 1.0f + 0.2f * static_cast<float>(haste);
        for (int i = 0; i < haste; ++i) {
            scale *= 1.2f;
        }
    }
    for (int level = effects.level(Effect::MiningFatigue); level > 0; --level) {
        scale *= 0.3f;
        scale *= 0.7f;
    }
    return scale;
}

/// What a blow lands for once strength and weakness have had their say.
///
/// **Both formulas are Bedrock's and neither is Java's flat plus-or-minus.**
/// Strength is `base * 1.3^n + (1.3^n - 1) / 0.3`; weakness is the same shape
/// with 0.8 and 0.4. An iron sword's 7 becomes 10.1 under Strength I.
///
/// **The order is the reference's own and it is not commutative**: "first
/// calculate the damage with the Strength formula, then plug that in as the
/// BaseDamage in this Weakness formula" (`minecraft.wiki/w/Weakness`), which is
/// why strength runs first here and weakness takes its result rather than the
/// bare `base`.
constexpr float meleeDamage(const Effects& effects, float base) {
    float damage = base;
    const int strength = effects.level(Effect::Strength);
    if (strength > 0) {
        float factor = 1.0f;
        for (int i = 0; i < strength; ++i) {
            factor *= 1.3f;
        }
        damage = damage * factor + (factor - 1.0f) / 0.3f;
    }
    const int weakness = effects.level(Effect::Weakness);
    if (weakness > 0) {
        float factor = 1.0f;
        for (int i = 0; i < weakness; ++i) {
            factor *= 0.8f;
        }
        damage = damage * factor + (factor - 1.0f) / 0.4f;
    }
    return damage < 0.0f ? 0.0f : damage;
}

/// How much of an incoming blow survives Resistance. **Twenty per cent per
/// level**, and level five is total immunity to everything *except* starvation,
/// the void and `/kill` - "Resistance reduces incoming damage by 20% x level
/// from all sources except for starvation, the void, and /kill"
/// (`minecraft.wiki/w/Resistance`).
///
/// **Those three exceptions are the caller's to honour and cannot be honoured
/// here**, because this function is handed effects and not a reason. The player
/// path enforces them in `Player.cpp`'s `damagePlayer`, which takes a flag for
/// the two sources it can actually produce; a new unresistable source has to
/// set it too, and a source that merely bypasses the invulnerability window is
/// **not** one of them - poison and wither do that and are resisted normally.
///
/// This note read "everything but starvation and the void" for a month while
/// the caller scaled both, which made Resistance III enough to live in the void
/// forever. The rule was right and only unenforced, which is the worst place
/// for a rule to be.
constexpr float damageTakenScale(const Effects& effects) {
    const int level = effects.level(Effect::Resistance);
    const float remaining = 1.0f - 0.2f * static_cast<float>(level);
    return remaining < 0.0f ? 0.0f : remaining;
}

/// Multiplier on **how fast the jump leaves the ground**, not on how high it
/// gets - the height goes as the *square* of this, and reading it the other way
/// is the mistake this sentence exists to stop.
///
/// `RESEARCH.md` §1.5's jump table publishes three absolute heights against
/// three launch speeds: 0.42 blocks per tick reaches **1.2522 m**, Jump Boost I
/// at 0.52 reaches **1.8361**, II at 0.62 reaches **2.5168**. The height ratio
/// at one level is therefore 1.4663 and the *velocity* ratio whose square that
/// is comes to 1.2109 - which is where 0.21 a level comes from, and the reason
/// it is not 0.46. `Player.cpp` feeds the return value straight into
/// `kJumpVelocity`, so a reader who "corrected" this to the height ratio would
/// launch Jump Boost I to 2.69 m and clear two blocks with it.
///
/// Anchored at the foot of this file against those published metres rather than
/// against itself.
constexpr float jumpScale(const Effects& effects) {
    const int level = effects.level(Effect::JumpBoost);
    return level > 0 ? 1.0f + 0.21f * static_cast<float>(level) : 1.0f;
}

/// The reference publishes every periodic effect as a **shift on a tick count**
/// - `50 >> amplifier` for regeneration, `25 >> amplifier` for poison,
/// `40 >> amplifier` for wither - and this file hands its callers **seconds**.
/// That conversion is what this function owns, and it owns it because doing it
/// by hand got the reachable levels wrong: `25 / 2` is 12 ticks in the
/// reference's integer shift and 12.5 in a float division, so a brewed Poison
/// II ticked at 0.625 s where the wiki's own table says 0.6, which is 4% of the
/// damage of the one potion in the tree that lands more than one point.
///
/// The floor is the reference's own: its tables bottom out at one tick a point
/// and never at zero, and a zero here would be read as "not running" by every
/// caller and hang the `while` loop that spends it. The clamp on the shift is
/// what keeps the shift itself defined - `1 << 31` is undefined behaviour and
/// a `constexpr` context turns it into a build failure.
constexpr float shiftedInterval(int ticks, int amplifier) {
    const int shift = amplifier < 0 ? 0 : (amplifier > 30 ? 30 : amplifier);
    const int shifted = ticks >> shift;
    return static_cast<float>(shifted < 1 ? 1 : shifted) * tick::kSeconds;
}

/// Seconds between one point of healing from Regeneration, or 0 when it is not
/// running. The reference's own `50 >> amplifier` **ticks**: 2.5 s at I,
/// 1.25 at II, 0.6 at III.
constexpr float regenerationInterval(const Effects& effects) {
    const int level = effects.level(Effect::Regeneration);
    if (level <= 0) {
        return 0.0f;
    }
    return shiftedInterval(50, level - 1);
}

/// Seconds between one point of damage from Poison. `25 >> amplifier` **ticks**
/// - 1.25 s at I, 0.6 at II - **and poison can never take the last point of
/// health**; that rule lives with the caller, because only it knows what the
/// health is.
constexpr float poisonInterval(const Effects& effects) {
    const int level = effects.level(Effect::Poison);
    if (level <= 0) {
        return 0.0f;
    }
    return shiftedInterval(25, level - 1);
}

/// Seconds between one point of damage from Wither. `40 >> amplifier` **ticks**
/// - 2 s at I, 1 at II - and unlike poison, **this one can kill**.
///
/// Bedrock has a second poison that can kill too, `Fatal Poison`, and we do not
/// model it; see the gap note on `Effect`.
constexpr float witherInterval(const Effects& effects) {
    const int level = effects.level(Effect::Wither);
    if (level <= 0) {
        return 0.0f;
    }
    return shiftedInterval(40, level - 1);
}

/// Extra hearts on top of the twenty, from Absorption. Four points per level,
/// taken first and never regenerated (`minecraft.wiki/w/Absorption`; the count
/// is per level and identical in both editions, so nothing to convert).
///
/// **Live since 2026-08-18, end to end.** `survival::foodValue` carries the two
/// apples' grants - Absorption I for 2:00 from a golden apple, IV for 2:00 from
/// an enchanted one - the eat site in `Main.cpp` applies them,
/// `Player::tickPassiveTimers` fills the pool from this function whenever the
/// effect is granted again, `damagePlayer` spends the pool before health, and
/// `hud::makeStatusBars` draws what is left as gold hearts above the health row.
///
/// **The consuming side is not a call, which is why it is not here.** Absorption
/// points are spent and never refilled, so they are a pool that has to live on
/// the player and be drained before health - a field on `Player` plus a first
/// bite out of `damagePlayer`, both in `Player.cpp`, which has another owner.
/// This function is the number that pool is *filled* to when the level changes;
/// it cannot be the pool, and **nothing that reports the player's padding may
/// read it** - it is a grant's worth, not a balance.
///
/// Two earlier versions of this comment are worth remembering rather than
/// deleting, because each was true when written and became a trap: the first
/// said nothing granted Absorption, and the second said the pool that spends it
/// did not exist. Both stopped an audit one step short of the live gap - the
/// second one while the HUD row it claimed had been built did not exist at all.
constexpr float absorptionPoints(const Effects& effects) {
    return 4.0f * static_cast<float>(effects.level(Effect::Absorption));
}

/// What an instant potion does the moment it is drunk: `4 * 2^amplifier` points
/// healed, or `6 * 2^amplifier` taken. **The sign flips for the undead**, which
/// is why the caller passes who it landed on rather than this deciding.
constexpr float instantAmount(Effect effect, int amplifier) {
    const float base = effect == Effect::InstantDamage ? 6.0f : 4.0f;
    return base * static_cast<float>(1 << amplifier);
}

/// How fast the player falls, as a multiplier on gravity **and on the terminal
/// speed** - one number reaching both, because the reference changes both
/// together: 0.08 → 0.01 blocks per tick² and 78.4 → 9.8 m/s (`RESEARCH.md`
/// §1.3). Each of those pairs is a ratio of exactly one eighth. Slow falling
/// does not scale with level in the reference and neither does this.
///
/// **0.15 stood here until the day it got a caller**, and nothing could have
/// seen it: the figure is reachable only through a potion, and `Player.cpp`'s
/// fall branch was the first line ever to ask for it. That is a fifth of a
/// descent, and the reason it survived is that the derivation was not written
/// down - so it is written down now, and asserted at the foot of the file.
constexpr float fallSpeedScale(const Effects& effects) {
    return effects.level(Effect::SlowFalling) > 0 ? 0.125f : 1.0f;
}

/// Exhaustion added per second by Hunger, which is how it empties the bar
/// without touching it directly.
///
/// The reference publishes **0.005 exhaustion per tick per level**
/// (`minecraft.wiki/w/Food`'s exhaustion table, the same in both editions), and
/// everything upstream of this counts in **seconds**, so the conversion happens
/// here and exactly once - `tick::kPerSecond` is what does it, rather than a
/// hand-written 0.1 that would be a second owner of the tick rate. It comes to
/// 0.1 per second per level, which the reference's own prose confirms from the
/// other end: one food point costs 4.0 exhaustion, so Hunger I empties one
/// every 40 seconds.
///
/// **Derived from the effect's own per-tick figure and from nothing else.**
/// Worth stating, because `RESEARCH.md` §3.2 contradicts itself on the movement
/// costs a reader might reach for instead - its prose says sprinting 100 blocks
/// is 30 exhaustion while its own table says 0.1 per metre, which is 10. Those
/// belong to `Survival.hpp` and neither one is upstream of this.
constexpr float hungerExhaustion(const Effects& effects) {
    constexpr float kPerTickPerLevel = 0.005f;
    return kPerTickPerLevel * tick::kPerSecond * static_cast<float>(effects.level(Effect::Hunger));
}

/// Float comparison for the asserts below.
constexpr bool agrees(float a, float b, float tolerance) {
    const float difference = a - b;
    return (difference < 0.0f ? -difference : difference) <= tolerance;
}

/// A player with one effect running, built at compile time. What makes the
/// asserts below check the formula a real caller evaluates - amplifier, level
/// and all - rather than one side of a derivation against itself.
constexpr Effects running(Effect effect, int amplifier) {
    Effects effects;
    effects.apply(effect, amplifier, 1.0f);
    return effects;
}

// Checks against the reference's own published figures, so a stray edit to a
// formula fails the build rather than the potion.
static_assert(instantAmount(Effect::InstantHealth, 0) == 4.0f &&
                  instantAmount(Effect::InstantHealth, 1) == 8.0f &&
                  instantAmount(Effect::InstantDamage, 0) == 6.0f &&
                  instantAmount(Effect::InstantDamage, 1) == 12.0f,
              "the instant potions must heal 4/8 and hurt 6/12, which is what the wiki lists");

// The figure `meleeDamage`'s own comment quotes. The single edit that fails it:
// writing Java's flat +3 in place of Bedrock's two-term formula, which lands
// within a point of the same answer at level I and diverges after it.
static_assert(agrees(meleeDamage(running(Effect::Strength, 0), 7.0f), 10.1f, 0.001f),
              "an iron sword's 7 must become 10.1 under Strength I");
// And that weakness can never hand back a negative blow, which the two-term
// formula reaches at level IV on a bare fist.
static_assert(meleeDamage(running(Effect::Weakness, 3), 1.0f) >= 0.0f,
              "weakness must floor at zero rather than heal what it hits");

// Twenty per cent a level, and level five is total immunity - the published
// rule, stated where a caller can see it.
static_assert(damageTakenScale(running(Effect::Resistance, 4)) == 0.0f &&
                  agrees(damageTakenScale(running(Effect::Resistance, 0)), 0.8f, 0.001f),
              "Resistance must take a fifth of the blow per level and all of it at level five");

// The single edit that fails this: putting `fallSpeedScale`'s old 0.15 back.
// 78.4 m/s is the reference's terminal speed and 9.8 is its terminal speed
// under the effect, so the multiplier has to carry one exactly to the other.
static_assert(agrees(78.4f * fallSpeedScale(running(Effect::SlowFalling, 0)), 9.8f, 0.001f),
              "slow falling must take the reference's 78.4 m/s terminal speed to its 9.8");

// Absorption's two published grants. A golden apple is level I and worth two
// extra hearts, an enchanted one is level IV and worth eight
// (`minecraft.wiki/w/Absorption`). Both are live now - the apples grant them,
// the pool spends them and the HUD draws them - so this pins the unit three
// systems away rather than merely waiting for a source. The single edit that
// fails it is writing 2.0f per level - the hearts rather than the points.
static_assert(absorptionPoints(running(Effect::Absorption, 0)) == 4.0f &&
                  absorptionPoints(running(Effect::Absorption, 3)) == 16.0f,
              "Absorption must be four health points a level, not two hearts");

// **Jump Boost is a multiplier on the launch speed and the height goes as its
// square**, which is the one thing about it a reader can get backwards while
// every number in sight still looks plausible. `RESEARCH.md` §1.5's jump table
// publishes three absolute heights in metres - 1.2522 plain, 1.8361 under Jump
// Boost I, 2.5168 under II - so the relation is anchored to real figures at
// both ends rather than compared against itself.
//
// The single edit that fails it: returning the height ratio (1.4663 at one
// level, so 0.4663 a level) from a function whose caller feeds it to
// `kJumpVelocity`. That reads perfectly and clears two blocks.
constexpr float jumpApexMetres(int level) {
    const float scale = jumpScale(running(Effect::JumpBoost, level - 1));
    return 1.2522f * scale * scale;
}
static_assert(agrees(jumpApexMetres(0), 1.2522f, 0.001f) &&
                  agrees(jumpApexMetres(1), 1.8361f, 0.01f) &&
                  agrees(jumpApexMetres(2), 2.5168f, 0.01f),
              "Jump Boost scales the launch speed, so its published heights must come out squared");

// Bedrock's two mining effects at the three levels a player can actually reach,
// against `minecraft.wiki/w/Haste`'s own multiplier table: 1.44 at Haste I,
// 2.016 at II, and 0.21 at Mining Fatigue I. The single edit that fails it:
// dropping either effect's second term, which is exactly what an auditor
// reading only the effect pages would recommend.
static_assert(agrees(miningSpeedScale(running(Effect::Haste, 0)), 1.44f, 0.001f) &&
                  agrees(miningSpeedScale(running(Effect::Haste, 1)), 2.016f, 0.001f) &&
                  agrees(miningSpeedScale(running(Effect::MiningFatigue, 0)), 0.21f, 0.001f),
              "Bedrock's haste and mining fatigue are each two terms, not one");

// **The periodic effects are published in ticks and spent in seconds**, and the
// conversion is only right if it shifts the tick count rather than dividing the
// seconds. These are the reference's own tables read straight off - regeneration
// 50/25/12 ticks, poison 25/12 ticks, wither 40/20 - and the two that a float
// division gets wrong are the ones written second in each pair.
//
// The single edit that fails it: going back to `2.5f / (1 << amplifier)`, which
// puts Poison II at 0.625 s against the reference's 0.6.
static_assert(agrees(regenerationInterval(running(Effect::Regeneration, 0)),
                     50.0f * tick::kSeconds, 0.0001f) &&
                  agrees(regenerationInterval(running(Effect::Regeneration, 1)),
                         25.0f * tick::kSeconds, 0.0001f) &&
                  agrees(regenerationInterval(running(Effect::Regeneration, 2)),
                         12.0f * tick::kSeconds, 0.0001f) &&
                  agrees(poisonInterval(running(Effect::Poison, 0)), 25.0f * tick::kSeconds,
                         0.0001f) &&
                  agrees(poisonInterval(running(Effect::Poison, 1)), 12.0f * tick::kSeconds,
                         0.0001f) &&
                  agrees(witherInterval(running(Effect::Wither, 0)), 40.0f * tick::kSeconds,
                         0.0001f) &&
                  agrees(witherInterval(running(Effect::Wither, 1)), 20.0f * tick::kSeconds,
                         0.0001f),
              "regeneration, poison and wither must shift a tick count, not divide a second");

// And that no amplifier can ask for a zero or negative interval, whatever a
// future grant puts in the field. Every caller spends these in a `while` loop
// against a rising timer, so a zero is not a slow effect - it is a hang.
static_assert(poisonInterval(running(Effect::Poison, 30)) >= tick::kSeconds &&
                  witherInterval(running(Effect::Wither, 60)) >= tick::kSeconds &&
                  regenerationInterval(running(Effect::Regeneration, 60)) >= tick::kSeconds,
              "a periodic effect must bottom out at one tick, never at zero");

/// Applies one of every storable effect to a fresh player, through the real
/// `apply`, and reports whether they all fit.
///
/// **The point is that it drives the thing being checked.** The assert this
/// replaced read `kMaxActive >= storableEffectCount()` while `kMaxActive` *is*
/// `storableEffectCount()` - `X >= X`, a derivation compared against itself,
/// which is the twelfth time this project has written one. This runs the
/// overwrite-or-ignore loop and the slot array for real, so the old hand-written
/// 8 fails it at the ninth effect.
///
/// It cannot see the *spelling*, and no `static_assert` can: a literal 22 is
/// still correct by value. What it holds is the property that matters - that
/// nothing gets refused in silence.
constexpr bool everyStorableFits() {
    Effects effects;
    int applied = 0;
    for (int id = 1; id < static_cast<int>(Effect::Count); ++id) {
        const EffectInfo info = effectInfo(static_cast<Effect>(id));
        if (info.name[0] == '\0' || info.instant) {
            continue;
        }
        if (effects.apply(static_cast<Effect>(id), 0, 1.0f)) {
            ++applied;
        }
    }
    return applied == storableEffectCount() && effects.count() == storableEffectCount();
}

// A beacon, a brewing stand and a golden apple between them get past eight.
// The single edit that fails this: narrowing `kMaxActive` to anything under the
// number of storable ids - `apply` then returns false and says nothing.
static_assert(everyStorableFits(),
              "every effect that can be running must have a slot, or one is refused in silence");

} // namespace game::effects
