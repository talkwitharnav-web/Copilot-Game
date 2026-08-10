#include "core/Sounds.hpp"

#include <engine/core/Log.hpp>

#include <string>

namespace game {
namespace {

/// The file stem each event's recordings are staged under, matching
/// `tools/make-reference-sounds.ps1` exactly. **One order, two places.**
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
};

/// And the voice families, in `CreatureVoice` order.
constexpr std::array<const char*, static_cast<std::size_t>(CreatureVoice::Count)> kVoiceStems{
    "sheep",   "cow",       "pig",      "chicken", "horse",     "llama",     "cat",
    "wolf",    "fox",       "panda",    "bear",    "rabbit",    "goat",      "bee",
    "turtle",  "dolphin",   "squid",    "villager","trader",    "zombie",    "husk",
    "drowned", "zvillager", "skeleton", "stray",   "bogged",    "blackbone", "spider",
    "creeper", "slime",     "magma",    "silverfish", "princepin", "fish",
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

/// How long between tracks. The reference leaves long gaps deliberately - music
/// that never stops stops being music.
constexpr float kMusicGapSeconds = 210.0f;

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
    default:
        // The frog is the one land animal left silent on purpose: it has its
        // own voice in the dump and no family here fits it, and the wrong
        // animal is worse than none.
        return CreatureVoice::None;
    }
}

SoundMaterial soundMaterialFor(BlockId block) {
    // Asked as families rather than as a list of ids, so eleven hundred blocks
    // are covered by a dozen tests and a new one inherits the right sound the
    // day it is added.
    if (block == BlockId::Air || isFluid(block)) {
        return SoundMaterial::None;
    }
    if (isLeafBlock(block)) {
        return SoundMaterial::Grass;
    }
    if (isLogBlock(block) || block == BlockId::Planks || block == BlockId::Bookshelf ||
        block == BlockId::CraftingTable) {
        return SoundMaterial::Wood;
    }
    if (block == BlockId::Glass || isPane(block) || block == BlockId::Glowstone) {
        return SoundMaterial::Glass;
    }
    if (block == BlockId::Sand || block == BlockId::Sandstone) {
        return SoundMaterial::Sand;
    }
    if (block == BlockId::Gravel || block == BlockId::Clay) {
        return SoundMaterial::Gravel;
    }
    if (block == BlockId::Snow) {
        return SoundMaterial::Snow;
    }
    // Anything that lives in water squelches rather than crunches, which is the
    // reference's `wet_grass` and is what a seabed should sound like.
    if (block == BlockId::Kelp || block == BlockId::Seagrass || block == BlockId::LilyPad) {
        return SoundMaterial::Wet;
    }
    if (block == BlockId::Grass || block == BlockId::Dirt || isCrossBlock(block)) {
        return SoundMaterial::Grass;
    }
    if (isCarpet(block)) {
        return SoundMaterial::Cloth;
    }

    // A cut shape sounds like whatever it was cut from, which falls out of
    // `shapedParent` rather than needing six hundred rows of its own.
    const BlockId parent = shapedParent(block);
    if (parent != block) {
        return soundMaterialFor(parent);
    }

    // Wood is the only family broad enough to be worth a second test; every
    // remaining block is some kind of rock, which is also the safest thing for
    // an unknown to be.
    return isFlammable(block) ? SoundMaterial::Wood : SoundMaterial::Stone;
}

SoundEvent digSoundFor(SoundMaterial material) {
    switch (material) {
    case SoundMaterial::Stone:
        return SoundEvent::DigStone;
    case SoundMaterial::Wood:
        return SoundEvent::DigWood;
    case SoundMaterial::Grass:
        return SoundEvent::DigGrass;
    case SoundMaterial::Gravel:
        return SoundEvent::DigGravel;
    case SoundMaterial::Sand:
        return SoundEvent::DigSand;
    case SoundMaterial::Cloth:
        return SoundEvent::DigCloth;
    case SoundMaterial::Snow:
        return SoundEvent::DigSnow;
    case SoundMaterial::Glass:
        return SoundEvent::DigGlass;
    case SoundMaterial::Coral:
        return SoundEvent::DigCoral;
    case SoundMaterial::Wet:
        return SoundEvent::DigWet;
    case SoundMaterial::None:
        break;
    }
    return SoundEvent::Count;
}

SoundEvent stepSoundFor(SoundMaterial material) {
    switch (material) {
    case SoundMaterial::Stone:
        return SoundEvent::StepStone;
    case SoundMaterial::Wood:
        return SoundEvent::StepWood;
    case SoundMaterial::Grass:
        return SoundEvent::StepGrass;
    case SoundMaterial::Gravel:
        return SoundEvent::StepGravel;
    case SoundMaterial::Sand:
        return SoundEvent::StepSand;
    case SoundMaterial::Cloth:
        return SoundEvent::StepCloth;
    case SoundMaterial::Snow:
        return SoundEvent::StepSnow;
    // Glass has no footstep of its own in the reference either; it walks like
    // stone.
    case SoundMaterial::Glass:
        return SoundEvent::StepStone;
    case SoundMaterial::Coral:
        return SoundEvent::StepCoral;
    case SoundMaterial::Wet:
        return SoundEvent::StepWet;
    case SoundMaterial::None:
        break;
    }
    return SoundEvent::Count;
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
    how.pitch = pitch * (0.92f + static_cast<float>(nextRandom(m_random) % 1000) * 0.00016f);
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
    how.pitch = pitch;
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

} // namespace game
