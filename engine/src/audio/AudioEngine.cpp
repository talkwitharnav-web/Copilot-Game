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
#include <mutex>

namespace engine {
namespace {

/// The device format. Everything is resampled into this on load, so the mixer
/// never has to think about rates.
constexpr ma_uint32 kSampleRate = 48000;
constexpr ma_uint32 kChannels = 2;

/// How many sounds may overlap. Past this the newest request is dropped rather
/// than stealing a voice - a dropped block break is unnoticeable and a chopped
/// one is not.
constexpr std::size_t kMaxVoices = 48;

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
};

struct Voice {
    const Sound* sound = nullptr;
    /// Fractional, because pitch is a playback rate.
    double cursor = 0.0;
    double step = 1.0;
    float leftGain = 0.0f;
    float rightGain = 0.0f;
    bool active = false;
    bool music = false;
};

} // namespace

struct AudioEngine::Impl {
    ma_device device{};
    bool deviceOpen = false;

    std::vector<std::unique_ptr<Sound>> sounds;

    mutable std::mutex mutex;
    std::array<Voice, kMaxVoices> voices{};
    float masterVolume = 1.0f;
    float musicVolume = 1.0f;
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
    for (Voice& voice : voices) {
        if (!voice.active || voice.sound == nullptr) {
            continue;
        }
        const Sound& sound = *voice.sound;
        const float bus = voice.music ? musicVolume : 1.0f;

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

            output[frame * kChannels] += left * voice.leftGain * masterVolume * bus;
            output[frame * kChannels + 1] += right * voice.rightGain * masterVolume * bus;
            voice.cursor += voice.step;
        }
    }

    // Everything is summed, so a busy moment can exceed full scale. Clamping is
    // ugly and clipping is uglier; at this voice count it effectively never
    // fires, and it is here so that when it does the result is loud rather than
    // a burst of noise.
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
    short* decoded = nullptr;
    const int frames = stb_vorbis_decode_filename(path.string().c_str(), &channels, &rate, &decoded);
    if (frames <= 0 || decoded == nullptr) {
        return kInvalidSound;
    }

    auto sound = std::make_unique<Sound>();
    // Resampled to the device rate on load rather than in the mixer, so the
    // hot path never has to know a sound's original rate. Nearest-frame is
    // enough: the reference's own audio is already 44.1 or 48 kHz, so this is
    // at worst a 9% stretch and usually a no-op.
    const double ratio = static_cast<double>(rate) / static_cast<double>(kSampleRate);
    const auto outFrames = static_cast<std::size_t>(static_cast<double>(frames) / ratio);
    sound->channels = channels >= 2 ? 2 : 1;
    const auto outChannels = static_cast<std::size_t>(sound->channels);
    sound->samples.resize(outFrames * outChannels);
    sound->frames = outFrames;

    for (std::size_t frame = 0; frame < outFrames; ++frame) {
        const auto source =
            std::min(static_cast<std::size_t>(static_cast<double>(frame) * ratio),
                     static_cast<std::size_t>(frames - 1));
        for (std::size_t channel = 0; channel < outChannels; ++channel) {
            sound->samples[frame * outChannels + channel] =
                decoded[source * static_cast<std::size_t>(channels) + channel];
        }
    }
    free(decoded);

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

    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (Voice& voice : m_impl->voices) {
        if (voice.active) {
            continue;
        }
        voice.sound = m_impl->sounds[sound].get();
        voice.cursor = 0.0;
        voice.step = std::clamp(static_cast<double>(how.pitch), 0.25, 4.0);
        voice.leftGain = gain * (1.0f - std::max(0.0f, pan));
        voice.rightGain = gain * (1.0f + std::min(0.0f, pan));
        voice.music = false;
        voice.active = true;
        return;
    }
}

void AudioEngine::playMusic(SoundHandle sound) {
    if (!m_impl->deviceOpen || sound == kInvalidSound || sound >= m_impl->sounds.size()) {
        return;
    }

    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    // The music bus is one voice, reused. Replacing rather than layering is the
    // point: two tracks at once is never what anyone wanted.
    Voice* slot = nullptr;
    for (Voice& voice : m_impl->voices) {
        if (voice.music) {
            slot = &voice;
            break;
        }
    }
    if (slot == nullptr) {
        for (Voice& voice : m_impl->voices) {
            if (!voice.active) {
                slot = &voice;
                break;
            }
        }
    }
    if (slot == nullptr) {
        return;
    }

    slot->sound = m_impl->sounds[sound].get();
    slot->cursor = 0.0;
    slot->step = 1.0;
    slot->leftGain = 1.0f;
    slot->rightGain = 1.0f;
    slot->music = true;
    slot->active = true;
}

bool AudioEngine::musicPlaying() const {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (const Voice& voice : m_impl->voices) {
        if (voice.music && voice.active) {
            return true;
        }
    }
    return false;
}

void AudioEngine::setListener(float x, float y, float z, float forwardX, float forwardZ) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->listenerX = x;
    m_impl->listenerY = y;
    m_impl->listenerZ = z;
    const float length = std::sqrt(forwardX * forwardX + forwardZ * forwardZ);
    if (length > 0.001f) {
        m_impl->forwardX = forwardX / length;
        m_impl->forwardZ = forwardZ / length;
    }
}

void AudioEngine::setMasterVolume(float volume) {
    const std::lock_guard<std::mutex> lock(m_impl->mutex);
    m_impl->masterVolume = std::clamp(volume, 0.0f, 1.0f);
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
