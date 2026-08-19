#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace engine {

/// A loaded sound, addressed by handle so the game never holds a pointer into
/// something the audio thread is reading.
using SoundHandle = std::size_t;
constexpr SoundHandle kInvalidSound = static_cast<SoundHandle>(-1);

/// How a sound is placed in the world.
struct SoundPlay {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    /// Ignore the position entirely. Music, the hurt sound and anything else
    /// that happens *to you* rather than near you.
    bool global = false;
    float volume = 1.0f;
    /// Multiplies the playback rate, so it changes speed as well as pitch -
    /// which is what the reference does too, and is why a baby's voice is also
    /// a quicker one.
    float pitch = 1.0f;
    /// Beyond this the sound is inaudible, and it fades linearly to it.
    float rolloff = 16.0f;
};

/// Plays sounds. Knows nothing about blocks, creatures or what a footstep is.
///
/// **The mixing runs on the audio device's own thread**, which is the one place
/// in this project where a lock is the right answer rather than a design
/// smell: the callback has a hard deadline measured in milliseconds and cannot
/// wait on the main thread, and the main thread must not tear a voice list the
/// callback is walking. The critical section is a handful of writes into a
/// fixed array, taken a few times a second.
///
/// A failure to open a device is **not fatal**. `ok()` reports it and every
/// call becomes a no-op, because a game that refuses to start because a laptop
/// has no sound card is worse than a silent one.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool ok() const;

    /// Decodes an Ogg Vorbis file into memory and keeps it. Returns
    /// `kInvalidSound` if it cannot be read, which callers are expected to
    /// tolerate - a missing sound is a missing sound, not a crash.
    ///
    /// **Main thread only, and before anything is playing.** The list it
    /// appends to is the one piece of state here the mixer's lock does not
    /// cover; see the comment on the `push_back` for why that is safe and what
    /// would break it.
    SoundHandle load(const std::filesystem::path& path);

    /// Starts a voice. Silently drops the request when every voice is busy,
    /// which is what stops a hundred simultaneous block breaks from queueing up
    /// into a roar seconds after the fact. **Music's voice is not one of them**
    /// and can never be taken by the world.
    void play(SoundHandle sound, const SoundPlay& how);

    /// Where the ears are. Forward is used for nothing but left/right panning.
    ///
    /// Main thread only: `play` reads this and nothing else does.
    void setListener(float x, float y, float z, float forwardX, float forwardZ);

    /// The world's volume, 0 to 1 - everything except music. Applied in the
    /// mixer rather than per voice, so changing it does not affect sounds
    /// already playing differently from new ones.
    ///
    /// **There is no master, and there was never really one.** This was
    /// `setMasterVolume` and it scaled music as well, so turning the world's
    /// sound off silenced the music the player had asked to keep. The alias
    /// that kept the old name compiling went with the last call site.
    void setSoundVolume(float volume);

    /// Music is its own bus, **independent of the world's** rather than nested
    /// under it, so either can be silenced without touching the other - the
    /// split every settings screen in this genre offers.
    void setMusicVolume(float volume);

    /// Starts a track on the music bus, replacing whatever was playing. Music
    /// is never positional and never dropped for want of a voice: one voice is
    /// reserved for it outright, so there is no free slot to fail to find.
    void playMusic(SoundHandle sound);
    bool musicPlaying() const;

    /// How many voices are currently sounding. Exists so "is anything actually
    /// playing?" is answerable from the log rather than by listening.
    ///
    /// **Counts the reserved music voice along with the effects**, so the answer
    /// runs 0 to 48 while `play` can only ever fill 47 of them - during a track
    /// with nothing else happening this returns 1, not 0. Effects alone are
    /// `activeVoices() - (musicPlaying() ? 1 : 0)`. Left counting everything
    /// because a "voices in use against capacity" reading is the useful one, and
    /// the capacity to compare it against is 48. No caller as of 2026-08-19,
    /// re-verified that date by a bare-name search over `engine/` and
    /// `game/src`: three hits, all of them this declaration, its own doc comment
    /// above and the definition in `AudioEngine.cpp`. Falsified by any hit
    /// outside those three.
    std::size_t activeVoices() const;

    /// Public only because the device callback is a free function that has to
    /// reach it. Opaque everywhere else.
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace engine
