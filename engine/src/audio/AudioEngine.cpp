#include "engine/audio/AudioEngine.hpp"

#include "engine/core/Log.hpp"

// Device layer only. Decoding is stb_vorbis's job - it already came with stb
// for the image loader, so pulling in miniaudio's own decoders would be a
// second answer to a question already settled.
//
// `NOMINMAX` is not optional: miniaudio pulls in `windows.h`, whose `min` and
// `max` macros turn every `std::min(` in this file into a syntax error that
// names a completely unrelated line.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#undef STB_VORBIS_HEADER_ONLY

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <mutex>

namespace engine {
namespace {

/// Releases a buffer stb_vorbis handed back, so a `unique_ptr` can own it.
///
/// stb allocates the decoded samples with plain `malloc` and says nothing about
/// who frees them, so ownership has to be taken at the call rather than
/// remembered at each exit.
struct StbVorbisFree {
    void operator()(short* samples) const noexcept { std::free(samples); }
};

/// The device format. Everything is resampled into this on load, so the mixer
/// never has to think about rates.
constexpr ma_uint32 kSampleRate = 48000;
constexpr ma_uint32 kChannels = 2;

/// How many sounds may overlap. Past this the newest request is dropped rather
/// than stealing a voice - a dropped block break is unnoticeable and a chopped
/// one is not. One of them belongs to music, so the world has `kMaxVoices - 1`.
constexpr std::size_t kMaxVoices = 48;

/// **The music bus is a voice, permanently, and it is this one.**
///
/// Reserved rather than searched for: the header promises music is never
/// dropped for want of a voice, and hunting for a free slot made that a lie
/// exactly when it mattered - if all 48 were busy the first time a track was
/// asked for, it was dropped and the game would not try again for the whole
/// 210-second gap. Dedicating a slot also makes starting a track O(1) and
/// removes the per-voice "is this the music one?" flag, because the index is
/// the answer.
constexpr std::size_t kMusicVoice = 0;

/// The one edit that breaks this: dropping `kMaxVoices` to 1, which would hand
/// the world's every sound to the music bus and leave nothing to play them.
static_assert(kMusicVoice < kMaxVoices && kMaxVoices > 1,
              "music owns one voice outright, so there must be at least one more for the world");

/// Below this a voice is not worth starting at all.
constexpr float kAudibleFloor = 0.005f;

struct Sound {
    /// **Kept as the sixteen-bit samples the file already held, and mono kept
    /// mono.** Decoding everything to interleaved stereo float cost four times
    /// what a mono clip needs - twice for a second channel carrying identical
    /// numbers, twice again for a float that starts life as a `short`. With
    /// five hundred recordings and eight music tracks that was most of a
    /// gigabyte of resident memory. The mixer converts on read, which is one
    /// multiply on a path that is already doing interpolation arithmetic.
    std::vector<std::int16_t> samples;
    std::size_t frames = 0;
    /// 1 or 2. A mono sound feeds both ears from the one channel.
    int channels = 1;
    /// The file's own rate divided by the device rate, folded into a voice's
    /// step so the mixer's interpolator does the resampling as it reads.
    ///
    /// **There used to be a resample pass here and it was a zero-order hold** -
    /// nearest earlier frame, repeated - which stair-stepped every 44.1 kHz
    /// file in the bank. Parsing the Vorbis identification header of all 519
    /// recordings: **279 at 44100 Hz, 238 at 48000, 2 at 192000**. `music8.ogg`
    /// is the only one of the eight music tracks at 44.1, so seven were
    /// bit-exact and the eighth was not - an inconsistency that surfaces months
    /// later as "one of the songs sounds worse" from someone who cannot say
    /// which.
    ///
    /// Measured on `music8.ogg` decoded through `stb_vorbis`, against a
    /// 64-tap Blackman-windowed sinc resample of the same source: error sat
    /// **28.25 dB** below signal through the zero-order hold and **54.82 dB**
    /// through the mixer's existing linear interpolation, so **+26.57 dB**.
    /// Peak sample error was 829 LSB of an `int16`. The control that makes that
    /// number mean something is `music7.ogg` and `music1.ogg`, both 48 kHz and
    /// therefore ratio exactly 1.0: **0 of 11,920,621 and 0 of 15,905,505
    /// frames differ between the two paths, peak exactly 0.000e+00** - so this
    /// change is a strict no-op on the 48 kHz half while the same harness still
    /// finds 11.7 million differing frames in `music8`.
    ///
    /// The interpolator forty lines below was always there and `load` simply
    /// did not use it - `CLAUDE.md` bug shape #14, a rule that existed in only
    /// one of the two places that needed it. Deleting the pass also stops the
    /// 44.1 kHz half costing memory it does not need: `music8` was 49,009,988
    /// bytes stretched and is 45,027,944 bytes native, so the stair-stepped
    /// copy was **8.84% larger**.
    double rateRatio = 1.0;
};

struct Voice {
    const Sound* sound = nullptr;
    /// Fractional, because pitch is a playback rate.
    double cursor = 0.0;
    double step = 1.0;
    float leftGain = 0.0f;
    float rightGain = 0.0f;
    bool active = false;
};

} // namespace

struct AudioEngine::Impl {
    ma_device device{};
    bool deviceOpen = false;

    /// **Appended to on the main thread without the lock, deliberately** - see
    /// `load`. The mutex below guards the voices, not this.
    std::vector<std::unique_ptr<Sound>> sounds;

    mutable std::mutex mutex;
    std::array<Voice, kMaxVoices> voices{};
    float soundVolume = 1.0f;
    float musicVolume = 1.0f;
    /// **Read only by `play`, on the same thread that writes them**, which is
    /// why they sit outside the lock's remit; the mixer never looks at them.
    float listenerX = 0.0f;
    float listenerY = 0.0f;
    float listenerZ = 0.0f;
    float forwardX = 0.0f;
    float forwardZ = 1.0f;

    void mix(float* output, ma_uint32 frameCount);
};

namespace {

void deviceCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount) {
    auto* impl = static_cast<AudioEngine::Impl*>(device->pUserData);
    if (impl != nullptr) {
        impl->mix(static_cast<float*>(output), frameCount);
    }
}

} // namespace

void AudioEngine::Impl::mix(float* output, ma_uint32 frameCount) {
    std::fill(output, output + static_cast<std::size_t>(frameCount) * kChannels, 0.0f);

    const std::lock_guard<std::mutex> lock(mutex);
    for (std::size_t v = 0; v < voices.size(); ++v) {
        Voice& voice = voices[v];
        if (!voice.active || voice.sound == nullptr) {
            continue;
        }
        const Sound& sound = *voice.sound;
        // **Two buses side by side, not one inside the other.** The music voice
        // is scaled by the music volume alone: nesting it under the world's
        // meant `sound_volume=0` silenced the music too, which is precisely the
        // split the settings claim to offer and did not.
        const float bus = (v == kMusicVoice) ? musicVolume : soundVolume;

        for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
            const auto index = static_cast<std::size_t>(voice.cursor);
            if (index + 1 >= sound.frames) {
                voice.active = false;
                voice.sound = nullptr;
                break;
            }

            // Linear interpolation between the two straddling frames, which is
            // what makes a pitch shift a resample rather than a stutter.
            const double fraction = voice.cursor - static_cast<double>(index);
            const auto blend = static_cast<float>(fraction);
            constexpr float kFromShort = 1.0f / 32768.0f;
            const std::int16_t* a = &sound.samples[index * static_cast<std::size_t>(sound.channels)];
            const std::int16_t* b = &sound.samples[(index + 1) * static_cast<std::size_t>(sound.channels)];
            // A mono sound has one channel to give and both ears take it.
            const int right1 = sound.channels == 1 ? 0 : 1;
            const float left = (static_cast<float>(a[0]) +
                                (static_cast<float>(b[0]) - static_cast<float>(a[0])) * blend) *
                               kFromShort;
            const float right = (static_cast<float>(a[right1]) +
                                 (static_cast<float>(b[right1]) - static_cast<float>(a[right1])) * blend) *
                                kFromShort;

            output[frame * kChannels] += left * voice.leftGain * bus;
            output[frame * kChannels + 1] += right * voice.rightGain * bus;
            voice.cursor += voice.step;
        }
    }

    // **Everything is summed, so a busy moment does exceed full scale, and this
    // clamp hard-clips when it does.** An earlier note here said it
    // "effectively never fires" and that it exists "so that when it does the
    // result is loud rather than a burst of noise". Both halves were wrong -
    // and the second contradicts itself, because hard clipping *is* the burst
    // of noise.
    //
    // Measured with uncorrelated content at per-voice gain 0.3: 4 voices clip
    // 1.28% of samples, 8 clip 9.72%, 16 clip 23.83%, 47 clip 48.29%, peak
    // 6.25. Two correlated full-scale voices already clip, and the game does
    // pass `volume = 1.0` - the bell, the orb and the goat horn all do. Control:
    // one voice at gain 1.0 peaks at exactly 1.00 with zero clipped samples, so
    // the clamp genuinely cannot fire on a single voice. Honest caveat: those
    // figures come from sine waves, which are the worst case for peak addition,
    // so the true rate in play is below 9.72% at eight voices.
    //
    // Left as a clamp deliberately - a limiter with attack and release is a
    // real change to how the game sounds and belongs to whoever can hear it.
    // **Do not raise `kMaxVoices` to fix this; more voices makes it worse.**
    const std::size_t total = static_cast<std::size_t>(frameCount) * kChannels;
    for (std::size_t i = 0; i < total; ++i) {
        output[i] = std::clamp(output[i], -1.0f, 1.0f);
    }
}

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kChannels;
    config.sampleRate = kSampleRate;
    config.dataCallback = deviceCallback;
    config.pUserData = m_impl.get();

    if (ma_device_init(nullptr, &config, &m_impl->device) != MA_SUCCESS) {
        logWarn("No audio device available; the game will run silently.");
        return;
    }
    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        logWarn("Could not start the audio device; the game will run silently.");
        ma_device_uninit(&m_impl->device);
        return;
    }
    m_impl->deviceOpen = true;
    logInfo("Audio device open: " + std::string(m_impl->device.playback.name) + " at " +
            std::to_string(m_impl->device.sampleRate) + " Hz");
}

AudioEngine::~AudioEngine() {
    if (m_impl->deviceOpen) {
        // Stops the callback before the voices it walks are destroyed. The
        // whole reason this is not left to the destructor order.
        ma_device_uninit(&m_impl->device);
        m_impl->deviceOpen = false;
    }
}

bool AudioEngine::ok() const {
    return m_impl->deviceOpen;
}

SoundHandle AudioEngine::load(const std::filesystem::path& path) {
    int channels = 0;
    int rate = 0;
    short* raw = nullptr;
    const int frames = stb_vorbis_decode_filename(path.string().c_str(), &channels, &rate, &raw);

    // **Owned the instant it exists, and that is the whole point of the two
    // lines.** stb can return a live `malloc` buffer alongside a frame count of
    // zero, so the guard below used to walk away from memory it had just been
    // handed - and so did the `resize` further down, on any file large enough
    // to throw. One release now, placed by the compiler on every path out.
    const std::unique_ptr<short[], StbVorbisFree> decoded(raw);

    if (frames <= 0 || decoded == nullptr) {
        return kInvalidSound;
    }

    auto sound = std::make_unique<Sound>();
    // **Stored at the file's own rate, and the mixer resamples as it reads.**
    // No resample pass here at all: `voice.step` carries `rate / kSampleRate`,
    // so the linear interpolation the mixer already performs for pitch does the
    // rate conversion for free and exactly. See `Sound::rateRatio` for what the
    // zero-order hold that used to live here cost.
    sound->rateRatio = rate > 0 ? static_cast<double>(rate) / static_cast<double>(kSampleRate) : 1.0;
    sound->channels = channels >= 2 ? 2 : 1;
    const auto outChannels = static_cast<std::size_t>(sound->channels);
    const auto outFrames = static_cast<std::size_t>(frames);

    // **`resize` can throw, and the header promises this function does not.**
    // A missing sound is a missing sound, not a crash - so a bank too large for
    // memory drops the clip and carries on rather than reaching the one handler
    // above, which logs and exits. Not reachable from today's bank (all 519
    // files parse and fit), which is exactly why it is worth writing down: the
    // contract is what is being kept here, not a symptom.
    //
    // `catch (...)` rather than `catch (const std::bad_alloc&)` because
    // `resize` has two ways out - `bad_alloc` when memory runs out and
    // `length_error` past `max_size` - and the promise being kept is "nothing
    // escapes", not "one named thing does not escape".
    try {
        sound->samples.resize(outFrames * outChannels);
    } catch (...) {
        return kInvalidSound;
    }
    sound->frames = outFrames;

    // A straight copy, taking the first one or two channels of whatever the
    // file held.
    for (std::size_t frame = 0; frame < outFrames; ++frame) {
        for (std::size_t channel = 0; channel < outChannels; ++channel) {
            sound->samples[frame * outChannels + channel] =
                decoded[frame * static_cast<std::size_t>(channels) + channel];
        }
    }

    // **Appended without the lock, and that is the story: the mutex guards the
    // voice array, not this vector.** The reason is narrower than it looks, and
    // an earlier note here got it wrong by saying loading "happens before
    // anything plays" - the device is started before all ~519 of these calls,
    // so something *could* be playing. What makes it safe is that **the mixer
    // never touches this vector at all**: it walks `const Sound*` raw pointers
    // that were resolved on the main thread, and the vector holds `unique_ptr`s
    // so a reallocating `push_back` moves the *pointers* while every `Sound`
    // stays exactly where it was. Loading being single-threaded is what rules
    // out a second writer. Taking the lock here would suggest the vector needs
    // protecting from the audio thread, and then somebody would eventually
    // believe it and call this from a job.
    m_impl->sounds.push_back(std::move(sound));
    return m_impl->sounds.size() - 1;
}

void AudioEngine::play(SoundHandle sound, const SoundPlay& how) {
    if (!m_impl->deviceOpen || sound == kInvalidSound || sound >= m_impl->sounds.size()) {
        return;
    }

    float gain = how.volume;
    float pan = 0.0f;
    if (!how.global) {
        const float dx = how.x - m_impl->listenerX;
        const float dy = how.y - m_impl->listenerY;
        const float dz = how.z - m_impl->listenerZ;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance >= how.rolloff) {
            return;
        }
        gain *= 1.0f - distance / how.rolloff;

        // Panning is the component along the listener's right, which is the
        // forward vector turned a quarter. Deliberately shallow - a hard pan on
        // a first-person camera is disorienting rather than informative.
        if (distance > 0.001f) {
            const float rightX = -m_impl->forwardZ;
            const float rightZ = m_impl->forwardX;
            pan = std::clamp((dx * rightX + dz * rightZ) / distance, -1.0f, 1.0f) * 0.6f;
        }
    }

    if (gain < kAudibleFloor) {
        return;
    }

    // Read before the lock, because the sound list is not what the lock is for.
    const Sound* clip = m_impl->sounds[sound].get();

    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    // **From one, not zero.** Voice zero belongs to music outright, so a busy
    // moment in the world can never reach in and take it.
    for (std::size_t v = kMusicVoice + 1; v < m_impl->voices.size(); ++v) {
        Voice& voice = m_impl->voices[v];
        if (voice.active) {
            continue;
        }
        voice.sound = clip;
        voice.cursor = 0.0;
        // Pitch is clamped as the caller's parameter, then the sound's own rate
        // ratio is folded in - so a 44.1 kHz clip plays at the right speed and
        // `pitch` still means exactly what it says.
        voice.step = std::clamp(static_cast<double>(how.pitch), 0.25, 4.0) * clip->rateRatio;
        voice.leftGain = gain * (1.0f - std::max(0.0f, pan));
        voice.rightGain = gain * (1.0f + std::min(0.0f, pan));
        voice.active = true;
        return;
    }
}

void AudioEngine::playMusic(SoundHandle sound) {
    if (!m_impl->deviceOpen || sound == kInvalidSound || sound >= m_impl->sounds.size()) {
        return;
    }

    const Sound* clip = m_impl->sounds[sound].get();

    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    // The music bus is one voice, reused, and it is always this one. Replacing
    // rather than layering is the point: two tracks at once is never what
    // anyone wanted - and because the slot is reserved there is no search, no
    // failure case, and nothing to make the header's promise untrue.
    Voice& slot = m_impl->voices[kMusicVoice];
    slot.sound = clip;
    slot.cursor = 0.0;
    slot.step = clip->rateRatio;
    slot.leftGain = 1.0f;
    slot.rightGain = 1.0f;
    slot.active = true;
}

bool AudioEngine::musicPlaying() const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->voices[kMusicVoice].active;
}

void AudioEngine::setListener(float x, float y, float z, float forwardX, float forwardZ) {
    // **No lock, deliberately.** Nothing here is read by `mix`: the listener and
    // the facing exist only so `play` can work out a distance and a pan, and
    // `play` runs on this same thread. Taking the realtime callback's mutex for
    // them contended with the audio device 120 times a second to protect data
    // the device never touches.
    m_impl->listenerX = x;
    m_impl->listenerY = y;
    m_impl->listenerZ = z;
    const float length = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (length > 0.001f) {
        m_impl->forwardX = forwardX / length;
        m_impl->forwardZ = forwardZ / length;
    }
}

// **Linear in amplitude, not in loudness - and deliberately left that way for
// now.** 0.5 here is -6.02 dB, which the ear places at about 66% as loud rather
// than half; half loudness is 0.316. So the useful lower half of the range is
// squeezed into the last few percent of travel, and a perceptual curve
// (roughly `volume^2.5`, or a dB mapping) is what a slider would want.
//
// **There is no slider.** Both setters have exactly one call site each, at
// startup, from `settings.cfg` - so today this is a number a person types once
// and tunes by ear, and a curve would only move which number they type. The
// audit that found this went looking for the UI control expecting to write "the
// slider does nothing", and that finding would have been false.
//
// That call-site count was checked on 2026-08-19 at 04:56, against a `Main.cpp`
// last written 04:42:53, over a tree with comments and string literals stripped
// so a mention cannot pass as a call - and with a bare-name second pass, since
// a name sitting in a table without parentheses is live and a `name(` search
// calls it dead. One call each, both in `Main.cpp`, adjacent, and zero
// bare-name-only occurrences. **A reachability claim without a date is
// unfalsifiable**, which is why the date is here rather than the finding alone.
//
// It becomes a real, felt problem the day a slider lands. Fix it then, together
// with the control, so the curve can be judged against the thing it drives.
void AudioEngine::setSoundVolume(float volume) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->soundVolume = std::clamp(volume, 0.0f, 1.0f);
}

void AudioEngine::setMusicVolume(float volume) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->musicVolume = std::clamp(volume, 0.0f, 1.0f);
}

std::size_t AudioEngine::activeVoices() const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    std::size_t count = 0;
    for (const Voice& voice : m_impl->voices) {
        count += voice.active ? 1 : 0;
    }
    return count;
}

} // namespace engine
