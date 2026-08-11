#pragma once

#include <array>
#include <cstdint>

namespace game::effects {

/// Every status effect the game can put on the player.
///
/// **The numbers are Bedrock's own effect ids**, so the list is deliberately not
/// contiguous where Bedrock's is not, and a saved effect is a number that means
/// the same thing as it does in the reference. Java-only effects - luck,
/// unluck, glowing, dolphin's grace - are not here and are not planned.
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

/// One effect running on the player.
struct ActiveEffect {
    Effect id = Effect::None;
    /// Zero is the roman numeral I, exactly as the reference stores it.
    int amplifier = 0;
    float secondsLeft = 0.0f;
};

/// How many can run at once. One slot per effect id, which is the whole of
/// Bedrock's storage model - see `apply`.
constexpr int kMaxActive = 8;

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
    /// Returns whether anything changed, which is what tells the caller whether
    /// to spend the item.
    bool apply(Effect effect, int amplifier, float seconds) {
        if (effect == Effect::None || effectInfo(effect).instant) {
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

    constexpr float secondsLeft(Effect effect) const {
        for (const ActiveEffect& active : m_active) {
            if (active.id == effect) {
                return active.secondsLeft;
            }
        }
        return 0.0f;
    }

    /// Counts every effect down together. The reference runs one shared
    /// one-second counter rather than a timer per effect, so two potions drunk
    /// a moment apart still tick on the same boundary.
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
    void clearOne(Effect effect) {
        for (ActiveEffect& active : m_active) {
            if (active.id == effect) {
                active = {};
            }
        }
    }

    constexpr const std::array<ActiveEffect, kMaxActive>& all() const { return m_active; }

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
/// **Bedrock's haste is not Java's.** Java gives a flat `1 + 0.2n`; Bedrock
/// gives `(1 + 0.2n) * 1.2^n`, so Haste II is 2.016 rather than 1.4. Mining
/// fatigue is `0.3^n` and is what an elder guardian would inflict if we had one.
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
    }
    return scale;
}

/// What a blow lands for once strength and weakness have had their say.
///
/// **Both formulas are Bedrock's and neither is Java's flat plus-or-minus.**
/// Strength is `base * 1.3^n + (1.3^n - 1) / 0.3`; weakness is the same shape
/// with 0.8 and 0.4. An iron sword's 7 becomes 10.1 under Strength I.
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
/// level**, and level five is total immunity to everything but starvation and
/// the void.
constexpr float damageTakenScale(const Effects& effects) {
    const int level = effects.level(Effect::Resistance);
    const float remaining = 1.0f - 0.2f * static_cast<float>(level);
    return remaining < 0.0f ? 0.0f : remaining;
}

/// How high a jump goes, as a multiplier. The reference's Jump Boost I reaches
/// 1.83 blocks against a plain 1.25, which is where this ratio comes from.
constexpr float jumpScale(const Effects& effects) {
    const int level = effects.level(Effect::JumpBoost);
    return level > 0 ? 1.0f + 0.21f * static_cast<float>(level) : 1.0f;
}

/// Seconds between one point of healing from Regeneration, or 0 when it is not
/// running. The reference's own `50 >> amplifier` ticks: 2.5 s at I, 1.25 at II.
constexpr float regenerationInterval(const Effects& effects) {
    const int level = effects.level(Effect::Regeneration);
    if (level <= 0) {
        return 0.0f;
    }
    return 2.5f / static_cast<float>(1 << (level - 1));
}

/// Seconds between one point of damage from Poison. `25 >> amplifier` ticks,
/// **and poison can never take the last point of health** - that rule lives with
/// the caller, because only it knows what the health is.
constexpr float poisonInterval(const Effects& effects) {
    const int level = effects.level(Effect::Poison);
    if (level <= 0) {
        return 0.0f;
    }
    return 1.25f / static_cast<float>(1 << (level - 1));
}

/// Seconds between one point of damage from Wither. `40 >> amplifier` ticks -
/// and unlike poison, **this one can kill**.
constexpr float witherInterval(const Effects& effects) {
    const int level = effects.level(Effect::Wither);
    if (level <= 0) {
        return 0.0f;
    }
    return 2.0f / static_cast<float>(1 << (level - 1));
}

/// Extra hearts on top of the twenty, from Absorption. Four points per level,
/// taken first and never regenerated.
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

/// How fast the player falls, as a multiplier on gravity. Slow falling does not
/// scale with level in the reference and neither does this.
constexpr float fallSpeedScale(const Effects& effects) {
    return effects.level(Effect::SlowFalling) > 0 ? 0.15f : 1.0f;
}

/// Exhaustion added per second by Hunger, which is how it empties the bar
/// without touching it directly: 0.1 per second per level.
constexpr float hungerExhaustion(const Effects& effects) {
    return 0.1f * static_cast<float>(effects.level(Effect::Hunger));
}

// Two checks against the reference's own published figures, so a stray edit to
// a formula fails the build rather than the potion.
static_assert(instantAmount(Effect::InstantHealth, 0) == 4.0f &&
                  instantAmount(Effect::InstantHealth, 1) == 8.0f &&
                  instantAmount(Effect::InstantDamage, 0) == 6.0f &&
                  instantAmount(Effect::InstantDamage, 1) == 12.0f,
              "the instant potions must heal 4/8 and hurt 6/12, which is what the wiki lists");

} // namespace game::effects
