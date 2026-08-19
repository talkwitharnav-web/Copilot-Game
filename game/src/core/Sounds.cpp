#include "core/Sounds.hpp"

#include <engine/core/Log.hpp>

#include <string>
#include <string_view>

namespace game {
namespace {

/// The file stem each event's recordings are staged under.
///
/// **`kStems[i]` is the stem for `SoundEvent(i)`, and that is the only order
/// that matters.** The old comment here claimed "one order, two places" and
/// named `tools/make-reference-sounds.ps1` as the second - which is false in
/// both directions: `Sounds::load` addresses every file by *stem name*, so the
/// script may write its table in any order it likes and nothing here would
/// notice. What it must not do is spell a stem differently, and `sweep()` is
/// what catches that. Do not "repair" the script's order to match this.
constexpr std::array<const char*, static_cast<std::size_t>(SoundEvent::Count)> kStems{
    "dig_stone",   "dig_wood",    "dig_grass",   "dig_gravel",  "dig_sand",
    "dig_cloth",   "dig_snow",    "dig_glass",   "dig_coral",   "dig_wet",

    "step_stone",  "step_wood",   "step_grass",  "step_gravel", "step_sand",
    "step_cloth",  "step_snow",   "step_ladder", "step_coral",  "step_wet",

    "hurt",        "fall_big",    "fall_small",  "breath",

    "bow",         "bow_hit",     "hit_land",    "explode",     "fuse",

    "eat",         "burp",        "pop",         "orb",         "click",
    "wood_click",  "item_break",  "fizz",        "splash",      "splash_big",
    "swim",        "drink",       "levelup",     "chest_open",  "chest_close",
    "door_open",   "door_close",

    "bucket_fill", "bucket_empty", "bucket_fill_lava", "bucket_empty_lava",

    "fire",        "ignite",      "lava",        "lava_pop",    "water",

    "cave",        "music",

    "rain",        "thunder",

    "bell",
};

/// Whether the stem sitting at an event's index is the one it should be.
///
/// **Not a restatement of the table**: it compares the enum against the array,
/// which are two different things written in two different places. The single
/// edit that makes it fail is inserting a `SoundEvent` enumerator without
/// inserting its stem at the same position - the failure this cannot otherwise
/// see, because everything after the insertion still loads perfectly and simply
/// plays the wrong recording. Anchored at both ends and once in the middle, so
/// an insertion anywhere is caught.
constexpr bool stemMatches(SoundEvent event, std::string_view stem) {
    return std::string_view(kStems[static_cast<std::size_t>(event)]) == stem;
}

static_assert(stemMatches(SoundEvent::DigStone, "dig_stone"), "the stem table has shifted at the top");
static_assert(stemMatches(SoundEvent::ItemBreak, "item_break"), "the stem table has shifted in the middle");
static_assert(stemMatches(SoundEvent::Bell, "bell"), "the stem table has shifted at the bottom");

/// And the voice families, in `CreatureVoice` order.
constexpr std::array<const char*, static_cast<std::size_t>(CreatureVoice::Count)> kVoiceStems{
    "sheep",   "cow",       "pig",      "chicken", "horse",     "llama",     "cat",
    "wolf",    "fox",       "panda",    "bear",    "rabbit",    "goat",      "bee",
    "turtle",  "dolphin",   "squid",    "villager","trader",    "zombie",    "husk",
    "drowned", "zvillager", "skeleton", "stray",   "bogged",    "blackbone", "spider",
    "creeper", "slime",     "magma",    "silverfish", "princepin", "fish", "golem",
};

constexpr std::array<const char*, static_cast<std::size_t>(VoiceState::Count)> kVoiceStates{
    "idle", "hurt", "death",
};

/// At most this many variants per event. The staging script writes them as
/// `<stem>1.ogg` upward, so loading stops at the first gap.
constexpr int kMaxVariants = 8;

std::uint32_t nextRandom(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

/// The pitch wobble every one-shot gets, and **one owner of it**, because the
/// positional and the global paths both need it and only one of them had it: a
/// levelling-up, a burp and a chest lid all played at exactly the same pitch
/// every time, which is the mechanical repetition the wobble exists to stop.
/// A little under a semitone either way - the reference's own range.
float jitterPitch(float pitch, std::uint32_t& state) {
    return pitch * (0.92f + static_cast<float>(nextRandom(state) % 1000) * 0.00016f);
}

/// How long between tracks. The reference leaves long gaps deliberately - music
/// that never stops stops being music.
constexpr float kMusicGapSeconds = 210.0f;

/// Whether anything in `game/` actually names an event.
///
/// **A hand-kept table, and it has to be**: no tool can see from here that a
/// bank loaded twenty-one recordings which no call site will ever ask for. That
/// is exactly how thirteen banks - a snapping tool, a drowning gasp, lava being
/// quenched - were decoded at every startup for twenty milestones while the
/// game played none of them, and every one of them looked perfectly healthy to
/// `has()`. Twelve of the thirteen were wired on 2026-08-18 and this table is
/// now true again, which is the only state in which it is worth anything: a
/// row that lies about a wired event is worse than no table at all, because
/// the next person will believe it and stop reading.
///
/// **It paid for itself on the way in.** Wiring `ItemBreak` turned up a fourth
/// hand-rolled tool-wear path - the bow, reading `kBowDurability` and zeroing
/// its own slot - which `Mining.hpp` already had in the tool table and which
/// `wearTool` should always have owned. The rule was correct in three places
/// and had not travelled to the fourth, and what found it was a *sound* being
/// missing: the silence was the symptom of a missing call to the owner.
///
/// **There is no `default:`, but do not trust that on its own.** MSVC's C4062 -
/// the warning for an unhandled enumerator - is **off by default even at
/// `/W4`**, and this build does not enable it (`/w44062` would; measured, not
/// assumed). So a new `SoundEvent` falls out of the switch below and lands on
/// `Unclassified`, which `sweep` names in the log at every startup. That is the
/// net: a compile-time one is not available here without a build change.
enum class EventUse : std::uint8_t {
    /// A call site names it. `sweep` complains if the recordings are missing.
    Played,
    /// Deliberately silent, with a reason. `sweep` says nothing either way.
    SilentOnPurpose,
    /// Staged, decoded, and nothing plays it yet. `sweep` says so every start.
    /// **Empty today, and that is the goal state** - it is where a newly staged
    /// bank sits while its call site is still being written, not a parking bay.
    NotYetWired,
    /// Nobody said. Only reachable by adding a `SoundEvent` and not adding a
    /// row below, which is precisely the edit this table exists to catch.
    Unclassified,
};

constexpr EventUse eventUse(SoundEvent event) {
    switch (event) {
    // Every dig and step bank is reachable through `digSoundFor` and
    // `stepSoundFor`, which is the only path the game uses to name one.
    case SoundEvent::DigStone:
    case SoundEvent::DigWood:
    case SoundEvent::DigGrass:
    case SoundEvent::DigGravel:
    case SoundEvent::DigSand:
    case SoundEvent::DigCloth:
    case SoundEvent::DigSnow:
    case SoundEvent::DigGlass:
    case SoundEvent::DigCoral:
    case SoundEvent::DigWet:
    case SoundEvent::StepStone:
    case SoundEvent::StepWood:
    case SoundEvent::StepGrass:
    case SoundEvent::StepGravel:
    case SoundEvent::StepSand:
    case SoundEvent::StepCloth:
    case SoundEvent::StepSnow:
    case SoundEvent::StepLadder:
    case SoundEvent::StepCoral:
    case SoundEvent::StepWet:
    case SoundEvent::Hurt:
    case SoundEvent::FallBig:
    case SoundEvent::FallSmall:
    case SoundEvent::Bow:
    case SoundEvent::HitLand:
    case SoundEvent::Explode:
    case SoundEvent::Fuse:
    case SoundEvent::Eat:
    case SoundEvent::Burp:
    case SoundEvent::Pop:
    case SoundEvent::Orb:
    case SoundEvent::WoodClick:
    case SoundEvent::Splash:
    case SoundEvent::ChestOpen:
    case SoundEvent::ChestClose:
    case SoundEvent::DoorOpen:
    case SoundEvent::DoorClose:
    case SoundEvent::BucketFill:
    case SoundEvent::BucketEmpty:
    case SoundEvent::BucketFillLava:
    case SoundEvent::BucketEmptyLava:
    case SoundEvent::Ignite:
    case SoundEvent::Cave:
    case SoundEvent::Music:
    case SoundEvent::Rain:
    case SoundEvent::Thunder:
    case SoundEvent::Bell:

    // Wired 2026-08-18, and five of them differ from what was specified for
    // them - written down here because this table is only worth reading while
    // it is true.
    //
    // **One owner each, which is the half that matters.** `ItemBreak` goes
    // through `wearTool`, which covers all four wear paths rather than the two
    // that were obvious - the fourth being the bow, which was zeroing its own
    // slot and snapping in silence until this sweep found it. `Swim` is an
    // `else if (player.inWater)` branch of the footstep chain rather than a
    // test in front of it, so `lastStepAt` keeps its single owner, OR'd with
    // the treading rising edge so bobbing in place still strokes; the old
    // treading-only trigger was a double-fire and is gone. `Drink` also sounds
    // at the *start* of a meal, because the opening bite was playing `Eat` for
    // potions.
    case SoundEvent::BowHit:
    case SoundEvent::Click:
    case SoundEvent::ItemBreak:
    case SoundEvent::Fizz:
    case SoundEvent::SplashBig:
    case SoundEvent::Swim:
    case SoundEvent::Drink:

    // Through `tickAmbient`, so the call site owns the condition and this file
    // owns the cadence. **`Water` is not part of the fire-and-lava block scan**
    // and must not be folded into it: its row is `global`, which reads it as
    // the muffling of *being* underwater rather than a river heard from the
    // bank, so it is gated on `player.underwater` alone. `Fire`, `Lava` and
    // `LavaPop` share one 5x5x5 scan on a quarter-second timer, nearest cell
    // wins.
    case SoundEvent::Breath:
    case SoundEvent::Fire:
    case SoundEvent::Lava:
    case SoundEvent::LavaPop:
    case SoundEvent::Water:
        return EventUse::Played;

    // **No experience system exists**, so there is no moment for this to mark.
    // `BlockDrops.hpp` and `Recipe.hpp` both say so where they would otherwise
    // award it, and it is still the one event in the enum with no call site.
    // The recording stays staged for the day one lands.
    case SoundEvent::LevelUp:
        return EventUse::SilentOnPurpose;

    case SoundEvent::Count:
        break;
    }
    // Reached only by a `SoundEvent` with no row above. Deliberately *not*
    // `SilentOnPurpose` - that would file a forgotten event under "we meant it".
    return EventUse::Unclassified;
}

/// One continuing condition: what it plays, how often, and how loudly.
struct AmbientRow {
    SoundEvent event = SoundEvent::Count;
    /// Roughly the recording's own length, so a held condition sounds
    /// continuous without stacking on itself.
    float seconds = 1.0f;
    float volume = 1.0f;
    /// Happens *to* you rather than near you, so it carries no position.
    bool global = false;
};

constexpr std::array<AmbientRow, static_cast<std::size_t>(AmbientCue::Count)> kAmbientCues{{
    /* Fire    */ {SoundEvent::Fire, 1.6f, 0.50f, false},
    /* Lava    */ {SoundEvent::Lava, 2.4f, 0.50f, false},
    /* LavaPop */ {SoundEvent::LavaPop, 2.8f, 0.60f, false},
    /* Water   */ {SoundEvent::Water, 1.9f, 0.40f, true},
    /* Breath  */ {SoundEvent::Breath, 1.00f, 0.70f, true},
}};

/// Whether every row could actually be heard as an ambience.
///
/// The single edit that makes it fail is giving a row an interval of zero,
/// which retriggers it every frame - forty copies of the same recording a
/// second, which is a roar rather than a fire. A volume outside the range or a
/// row that names no event fails it too.
constexpr bool ambientRowsAreSane() {
    for (const AmbientRow& row : kAmbientCues) {
        if (row.event == SoundEvent::Count) {
            return false;
        }
        if (row.seconds <= 0.0f || row.volume <= 0.0f || row.volume > 1.0f) {
            return false;
        }
    }
    return true;
}

static_assert(ambientRowsAreSane(),
              "every ambient cue needs a real event, an interval above zero and a volume within "
              "0 to 1");

} // namespace

CreatureVoice voiceFamilyFor(CreatureKind kind) {
    switch (kind) {
    case CreatureKind::Sheep:
        return CreatureVoice::Sheep;
    case CreatureKind::Cow:
    case CreatureKind::MushroomCow:
        return CreatureVoice::Cow;
    case CreatureKind::Pig:
        return CreatureVoice::Pig;
    case CreatureKind::Chicken:
        return CreatureVoice::Chicken;
    // One rig, one voice: the whole equine family plus the camel, which the
    // reference does give its own noises but which nothing here has mapped.
    case CreatureKind::Horse:
    case CreatureKind::Mule:
    case CreatureKind::Donkey:
    case CreatureKind::Camel:
    case CreatureKind::SkeletonHorse:
    case CreatureKind::ZombieHorse:
        return CreatureVoice::Horse;
    case CreatureKind::Llama:
    case CreatureKind::TraderLlama:
        return CreatureVoice::Llama;
    case CreatureKind::Cat:
    case CreatureKind::Ocelot:
        return CreatureVoice::Cat;
    case CreatureKind::Wolf:
        return CreatureVoice::Wolf;
    case CreatureKind::Fox:
        return CreatureVoice::Fox;
    case CreatureKind::Panda:
        return CreatureVoice::Panda;
    case CreatureKind::PolarBear:
        return CreatureVoice::Bear;
    case CreatureKind::Rabbit:
        return CreatureVoice::Rabbit;
    case CreatureKind::Goat:
        return CreatureVoice::Goat;
    case CreatureKind::Bee:
        return CreatureVoice::Bee;
    case CreatureKind::Turtle:
        return CreatureVoice::Turtle;
    case CreatureKind::Dolphin:
        return CreatureVoice::Dolphin;
    case CreatureKind::Squid:
    case CreatureKind::GlowSquid:
        return CreatureVoice::Squid;
    // The witch has no voice of its own in the dump and wears the villager's
    // rig, so it wears its voice too.
    case CreatureKind::Villager:
    case CreatureKind::Witch:
        return CreatureVoice::Villager;
    case CreatureKind::WanderingTrader:
        return CreatureVoice::Trader;
    case CreatureKind::Zombie:
        return CreatureVoice::Zombie;
    case CreatureKind::Husk:
        return CreatureVoice::Husk;
    case CreatureKind::Drowned:
        return CreatureVoice::Drowned;
    case CreatureKind::ZombieVillager:
        return CreatureVoice::ZombieVillager;
    case CreatureKind::Skeleton:
        return CreatureVoice::Skeleton;
    case CreatureKind::Stray:
        return CreatureVoice::Stray;
    case CreatureKind::Bogged:
        return CreatureVoice::Bogged;
    case CreatureKind::Blackbone:
        return CreatureVoice::Blackbone;
    case CreatureKind::Spider:
    case CreatureKind::CaveSpider:
        return CreatureVoice::Spider;
    case CreatureKind::Bramble:
        return CreatureVoice::Creeper;
    case CreatureKind::SlimeSmall:
    case CreatureKind::SlimeMedium:
    case CreatureKind::SlimeLarge:
        return CreatureVoice::Slime;
    case CreatureKind::MagmaCubeSmall:
    case CreatureKind::MagmaCubeMedium:
    case CreatureKind::MagmaCubeLarge:
        return CreatureVoice::Magma;
    case CreatureKind::Silverfish:
    case CreatureKind::Voidmite:
        return CreatureVoice::Silverfish;
    case CreatureKind::Princepin:
    case CreatureKind::PrincepinBrute:
    case CreatureKind::ZombiePrincepin:
        return CreatureVoice::Princepin;
    case CreatureKind::Cod:
    case CreatureKind::Salmon:
    case CreatureKind::Pufferfish:
    case CreatureKind::TropicalFish:
    case CreatureKind::Axolotl:
        return CreatureVoice::Fish;
    case CreatureKind::IronGolem:
        return CreatureVoice::Golem;
    default:
        // The frog is the one land animal left silent on purpose: it has its
        // own voice in the dump and no family here fits it, and the wrong
        // animal is worse than none.
        return CreatureVoice::None;
    }
}

void Sounds::load(engine::AudioEngine& audio, const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) {
        engine::logInfo("No sound bank at " + directory.string() + "; running silently.");
        return;
    }

    const auto loadRun = [&](const std::string& stem, std::vector<engine::SoundHandle>& into) {
        for (int variant = 1; variant <= kMaxVariants; ++variant) {
            const std::filesystem::path path = directory / (stem + std::to_string(variant) + ".ogg");
            if (!std::filesystem::exists(path)) {
                break;
            }
            const engine::SoundHandle handle = audio.load(path);
            if (handle == engine::kInvalidSound) {
                engine::logWarn("Could not decode " + path.string());
                break;
            }
            into.push_back(handle);
            ++m_loaded;
        }
    };

    for (std::size_t event = 0; event < kStems.size(); ++event) {
        loadRun(kStems[event], m_banks[event]);
    }
    for (std::size_t family = 0; family < kVoiceStems.size(); ++family) {
        for (std::size_t state = 0; state < kVoiceStates.size(); ++state) {
            loadRun(std::string("voice_") + kVoiceStems[family] + "_" + kVoiceStates[state],
                    m_voices[family][state]);
        }
    }

    engine::logInfo("Loaded " + std::to_string(m_loaded) + " sounds from " + directory.string());
}

engine::SoundHandle Sounds::pick(SoundEvent event) {
    if (event == SoundEvent::Count) {
        return engine::kInvalidSound;
    }
    const auto& bank = m_banks[static_cast<std::size_t>(event)];
    if (bank.empty()) {
        return engine::kInvalidSound;
    }
    return bank[nextRandom(m_random) % bank.size()];
}

engine::SoundHandle Sounds::pickVoice(CreatureVoice family, VoiceState state) {
    if (family == CreatureVoice::None) {
        return engine::kInvalidSound;
    }
    const auto& bank =
        m_voices[static_cast<std::size_t>(family)][static_cast<std::size_t>(state)];
    if (bank.empty()) {
        return engine::kInvalidSound;
    }
    return bank[nextRandom(m_random) % bank.size()];
}

void Sounds::play(engine::AudioEngine& audio, SoundEvent event, const glm::vec3& at, float volume,
                  float pitch) {
    const engine::SoundHandle handle = pick(event);
    if (handle == engine::kInvalidSound) {
        return;
    }
    engine::SoundPlay how;
    how.x = at.x;
    how.y = at.y;
    how.z = at.z;
    how.volume = volume;
    // A little variation on every one, which is most of what stops a repeated
    // sound reading as a loop. The reference does the same.
    how.pitch = jitterPitch(pitch, m_random);
    audio.play(handle, how);
}

void Sounds::playGlobal(engine::AudioEngine& audio, SoundEvent event, float volume, float pitch) {
    const engine::SoundHandle handle = pick(event);
    if (handle == engine::kInvalidSound) {
        return;
    }
    engine::SoundPlay how;
    how.global = true;
    how.volume = volume;
    // The same wobble a positional sound gets, from the same function. Held
    // apart, the two drifted and everything that happens *to you* rather than
    // near you came out at one fixed pitch.
    how.pitch = jitterPitch(pitch, m_random);
    audio.play(handle, how);
}

void Sounds::playVoice(engine::AudioEngine& audio, CreatureKind kind, VoiceState state,
                       const glm::vec3& at, float scale) {
    const engine::SoundHandle handle = pickVoice(voiceFamilyFor(kind), state);
    if (handle == engine::kInvalidSound) {
        return;
    }
    engine::SoundPlay how;
    how.x = at.x;
    how.y = at.y;
    how.z = at.z;
    how.volume = 0.85f;
    // **A baby is higher and quicker from the same recording.** The reference
    // does exactly this rather than shipping a second set, which is why `scale`
    // is the only thing about the individual this needs.
    const float babyPitch = scale < 1.0f ? 1.5f : 1.0f;
    how.pitch = babyPitch * (0.9f + static_cast<float>(nextRandom(m_random) % 1000) * 0.0002f);
    how.rolloff = 20.0f;
    audio.play(handle, how);
}

void Sounds::tickMusic(engine::AudioEngine& audio, float deltaSeconds) {
    if (m_banks[static_cast<std::size_t>(SoundEvent::Music)].empty()) {
        return;
    }
    if (audio.musicPlaying()) {
        m_musicTimer = 0.0f;
        return;
    }
    m_musicTimer += deltaSeconds;
    if (m_musicTimer < kMusicGapSeconds) {
        return;
    }
    m_musicTimer = 0.0f;
    audio.playMusic(pick(SoundEvent::Music));
}

void Sounds::tickAmbient(engine::AudioEngine& audio, AmbientCue cue, bool sounding,
                         const glm::vec3& at, float deltaSeconds) {
    const auto index = static_cast<std::size_t>(cue);
    if (index >= kAmbientCues.size()) {
        return;
    }
    const AmbientRow& row = kAmbientCues[index];

    if (!sounding) {
        // **Forgotten rather than paused.** Walking back to a fire should be
        // heard at once; a half-spent timer would leave up to an interval of
        // silence standing right beside it.
        m_ambientTimers[index] = 0.0f;
        return;
    }

    m_ambientTimers[index] -= deltaSeconds;
    if (m_ambientTimers[index] > 0.0f) {
        return;
    }
    // A little either way on the interval, for the same reason every one-shot
    // gets a pitch wobble: an exact cadence reads as a machine, not a fire.
    m_ambientTimers[index] =
        row.seconds * (0.85f + static_cast<float>(nextRandom(m_random) % 1000) * 0.0003f);

    if (row.global) {
        playGlobal(audio, row.event, row.volume);
    } else {
        play(audio, row.event, at, row.volume);
    }
}

std::vector<std::string> Sounds::sweep() const {
    std::vector<std::string> findings;

    if (loaded() == 0) {
        // Everything below would restate this fact a hundred and sixty times.
        findings.push_back("Sound sweep: no recordings loaded at all - the game will run silently.");
        return findings;
    }

    const auto append = [](std::string& list, const std::string& name) {
        if (!list.empty()) {
            list += ", ";
        }
        list += name;
    };

    std::string unstaged;
    std::string orphaned;
    std::string unclassified;
    std::size_t orphanedRecordings = 0;
    for (std::size_t event = 0; event < kStems.size(); ++event) {
        const bool staged = has(static_cast<SoundEvent>(event));
        switch (eventUse(static_cast<SoundEvent>(event))) {
        case EventUse::Played:
            if (!staged) {
                append(unstaged, kStems[event]);
            }
            break;
        case EventUse::NotYetWired:
            if (staged) {
                append(orphaned, kStems[event]);
                orphanedRecordings += m_banks[event].size();
            }
            break;
        case EventUse::Unclassified:
            append(unclassified, kStems[event]);
            break;
        case EventUse::SilentOnPurpose:
            break;
        }
    }

    std::string mute;
    for (std::size_t family = 0; family < kVoiceStems.size(); ++family) {
        for (std::size_t state = 0; state < kVoiceStates.size(); ++state) {
            if (!hasVoice(static_cast<CreatureVoice>(family), static_cast<VoiceState>(state))) {
                append(mute, std::string(kVoiceStems[family]) + " " + kVoiceStates[state]);
            }
        }
    }

    // One line per kind of fault rather than one per event: a missing bank
    // usually means a missing *run* of them, and sixty warnings hide the one
    // line that says which.
    if (!unstaged.empty()) {
        findings.push_back("Sound sweep: the game plays these and nothing was staged for them: " +
                           unstaged);
    }
    if (!orphaned.empty()) {
        findings.push_back("Sound sweep: " + std::to_string(orphanedRecordings) +
                           " recordings decoded that nothing in the game ever plays: " + orphaned);
    }
    if (!mute.empty()) {
        findings.push_back("Sound sweep: creature voices with no recordings: " + mute);
    }
    // Last because it is a fault in the *table*, not in the bank: someone added
    // an event and never said whether the game plays it, so every line above is
    // answering the wrong question for it.
    if (!unclassified.empty()) {
        findings.push_back("Sound sweep: no row in eventUse for: " + unclassified);
    }

    return findings;
}

} // namespace game
