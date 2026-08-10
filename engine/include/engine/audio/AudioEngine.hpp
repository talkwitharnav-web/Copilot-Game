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
    SoundHandle load(const std::filesystem::path& path);

    /// Starts a voice. Silently drops the request when every voice is busy,
    /// which is what stops a hundred simultaneous block breaks from queueing up
    /// into a roar seconds after the fact.
    void play(SoundHandle sound, const SoundPlay& how);

    /// Where the ears are. Forward is used for nothing but left/right panning.
    void setListener(float x, float y, float z, float forwardX, float forwardZ);

    /// Master volume, 0 to 1. Applied in the mixer rather than per voice, so
    /// changing it does not affect sounds already playing differently from new
    /// ones.
    void setMasterVolume(float volume);

    /// Music is its own bus so it can be turned down without silencing the
    /// world - the split every settings screen in this genre offers.
    void setMusicVolume(float volume);

    /// Starts a track on the music bus, replacing whatever was playing. Music
    /// is never positional and never dropped for want of a voice.
    void playMusic(SoundHandle sound);
    bool musicPlaying() const;

    /// How many voices are currently sounding. Exists so "is anything actually
    /// playing?" is answerable from the log rather than by listening.
    std::size_t activeVoices() const;

    /// Public only because the device callback is a free function that has to
    /// reach it. Opaque everywhere else.
    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace engine
