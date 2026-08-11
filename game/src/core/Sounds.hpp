#pragma once

#include "world/Block.hpp"
#include "world/Creature.hpp"

#include <engine/audio/AudioEngine.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace game {

/// Everything the game knows how to make a noise about.
///
/// Names an *event*, not a file - which is the whole reason this enum exists
/// rather than paths scattered through the loop. Most events have several
/// recordings and one is picked at random, so a row of blocks broken in a line
/// does not read as a machine.
///
/// **The order must match `kStems` in the .cpp and the staging script's own
/// table**, and a mismatch fails silently as an event that never sounds - which
/// is why the loader reports a total and the sweep asserts every event found
/// something.
enum class SoundEvent : std::uint8_t {
    DigStone,
    DigWood,
    DigGrass,
    DigGravel,
    DigSand,
    DigCloth,
    DigSnow,
    DigGlass,
    DigCoral,
    DigWet,

    StepStone,
    StepWood,
    StepGrass,
    StepGravel,
    StepSand,
    StepCloth,
    StepSnow,
    StepLadder,
    StepCoral,
    StepWet,

    Hurt,
    FallBig,
    FallSmall,
    Breath,

    Bow,
    BowHit,
    HitLand,
    Explode,
    Fuse,

    Eat,
    Burp,
    Pop,
    Orb,
    Click,
    WoodClick,
    ItemBreak,
    Fizz,
    Splash,
    SplashBig,
    Swim,
    Drink,
    LevelUp,
    ChestOpen,
    ChestClose,
    DoorOpen,
    DoorClose,

    BucketFill,
    BucketEmpty,
    BucketFillLava,
    BucketEmptyLava,

    Fire,
    Ignite,
    Lava,
    LavaPop,
    Water,

    Cave,
    Music,

    Rain,
    Thunder,

    Bell,

    Count,
};

/// Which voice a species speaks with.
///
/// **A family rather than a species**, for the same reason blocks are grouped
/// by material: fifty-seven creatures share far fewer voices, and a new one
/// inherits an existing family by naming it rather than by needing recordings
/// of its own. The staging script's `$voices` table is the other half of this
/// and the two must name the same families in the same order.
enum class CreatureVoice : std::uint8_t {
    Sheep,
    Cow,
    Pig,
    Chicken,
    Horse,
    Llama,
    Cat,
    Wolf,
    Fox,
    Panda,
    Bear,
    Rabbit,
    Goat,
    Bee,
    Turtle,
    Dolphin,
    Squid,
    Villager,
    Trader,
    Zombie,
    Husk,
    Drowned,
    ZombieVillager,
    Skeleton,
    Stray,
    Bogged,
    Blackbone,
    Spider,
    Creeper,
    Slime,
    Magma,
    Silverfish,
    Princepin,
    Fish,
    Golem,
    Count,
    /// Says nothing at all. The frog and the axolotl have voices in the
    /// reference that nothing here has been mapped to yet, and silence is a
    /// better answer than the wrong animal.
    None = Count,
};

/// What a creature is doing when it makes a noise.
enum class VoiceState : std::uint8_t {
    Idle,
    Hurt,
    Death,
    Count,
};

CreatureVoice voiceFamilyFor(CreatureKind kind);

/// What a block is made of, as far as the ears are concerned.
///
/// **Grouped by material rather than by block**, which is the reference's own
/// arrangement and the only one that scales: there are eleven hundred blocks
/// and ten sounds, so a new stone variant inherits stone without a line being
/// added anywhere.
enum class SoundMaterial : std::uint8_t {
    Stone,
    Wood,
    Grass,
    Gravel,
    Sand,
    Cloth,
    Snow,
    Glass,
    Coral,
    Wet,
    /// Makes no noise at all - air, water, and anything else with no surface.
    None,
};

SoundMaterial soundMaterialFor(BlockId block);
SoundEvent digSoundFor(SoundMaterial material);
SoundEvent stepSoundFor(SoundMaterial material);

/// Loads the staged sound bank and turns game events into voices.
///
/// **It owns the mapping and the engine owns the mixing**, which is the same
/// split every other system here follows: `engine::AudioEngine` has no idea
/// what a block is, and this has no idea what a sample rate is.
class Sounds {
public:
    /// `directory` is `sounds-reference/` beside the executable. A missing
    /// folder is not an error - every event simply has no recordings and every
    /// call becomes a no-op, because a silent game is far better than one that
    /// refuses to start over a missing sound.
    void load(engine::AudioEngine& audio, const std::filesystem::path& directory);

    /// Plays one of an event's recordings at a place in the world.
    void play(engine::AudioEngine& audio, SoundEvent event, const glm::vec3& at,
              float volume = 1.0f, float pitch = 1.0f);

    /// Plays it without a position: something that happened *to you* rather
    /// than near you.
    void playGlobal(engine::AudioEngine& audio, SoundEvent event, float volume = 1.0f,
                    float pitch = 1.0f);

    /// A creature's own voice. **A baby speaks higher and quicker**, which the
    /// reference gets from the same pitch multiplier rather than from a second
    /// set of recordings, so `scale` is all this needs to know about the
    /// individual.
    void playVoice(engine::AudioEngine& audio, CreatureKind kind, VoiceState state,
                   const glm::vec3& at, float scale = 1.0f);

    /// Starts a random track if none is playing. Called every frame; it keeps
    /// its own long timer, so music comes and goes rather than running
    /// continuously.
    void tickMusic(engine::AudioEngine& audio, float deltaSeconds);

    /// How many recordings were found. Zero means the bank is missing, which is
    /// worth one line in the log rather than a warning per event.
    std::size_t loaded() const { return m_loaded; }

    /// Whether an event found any recordings at all. The stem list and the
    /// staging script are two places that must agree, and a mismatch fails
    /// silently as an event that never makes a noise.
    bool has(SoundEvent event) const {
        return event != SoundEvent::Count && !m_banks[static_cast<std::size_t>(event)].empty();
    }
    bool hasVoice(CreatureVoice family, VoiceState state) const {
        return family != CreatureVoice::None &&
               !m_voices[static_cast<std::size_t>(family)][static_cast<std::size_t>(state)].empty();
    }

private:
    engine::SoundHandle pick(SoundEvent event);
    engine::SoundHandle pickVoice(CreatureVoice family, VoiceState state);

    std::array<std::vector<engine::SoundHandle>, static_cast<std::size_t>(SoundEvent::Count)>
        m_banks;
    std::array<std::array<std::vector<engine::SoundHandle>,
                          static_cast<std::size_t>(VoiceState::Count)>,
               static_cast<std::size_t>(CreatureVoice::Count)>
        m_voices;
    std::size_t m_loaded = 0;
    std::uint32_t m_random = 0x9E3779B9u;
    float m_musicTimer = 0.0f;
};

} // namespace game
