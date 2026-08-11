#include "core/Settings.hpp"

#include <engine/core/JobSystem.hpp>
#include <engine/core/Log.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace game {
namespace {

/// Deliberately not a real config format. One key per line, `key=value`, `#`
/// starts a comment. A parser small enough to read in one sitting cannot hide a
/// bug, and there is exactly one setting so far.
std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// Returns false rather than throwing on anything unexpected: this file is
/// hand-editable, so bad input is ordinary and should fall back to a default.
bool parseUnsigned(const std::string& text, unsigned limit, unsigned& out) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    try {
        const unsigned long value = std::stoul(text);
        if (value > limit) {
            return false;
        }
        out = static_cast<unsigned>(value);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

/// Signed, because world coordinates run either side of the origin.
bool parseInt(const std::string& text, int& out) {
    if (text.empty()) {
        return false;
    }
    const std::size_t digitsFrom = (text[0] == '-' || text[0] == '+') ? 1 : 0;
    if (digitsFrom >= text.size()) {
        return false;
    }
    if (!std::all_of(text.begin() + static_cast<std::string::difference_type>(digitsFrom), text.end(),
                     [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    try {
        out = std::stoi(text);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

/// `std::atof` answers 0 for anything it cannot read, so a typo silently means
/// silence for a volume and a black screen for an exposure. This says no
/// instead, and the caller keeps the default.
bool parseFloat(const std::string& text, float& out) {
    if (text.empty()) {
        return false;
    }
    try {
        std::size_t consumed = 0;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size() || !std::isfinite(value)) {
            return false;
        }
        out = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

Settings loadSettings(const std::filesystem::path& file) {
    Settings settings;
    settings.workerThreads = engine::defaultWorkerThreadCount();

    std::ifstream in(file);
    if (!in) {
        saveSettings(file, settings);
        engine::logInfo("Wrote default settings to " + file.string());
        return settings;
    }

    std::string line;
    while (std::getline(in, line)) {
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));

        const auto read = [&](const char* name, unsigned limit, unsigned& target) {
            if (key != name) {
                return;
            }
            unsigned parsed = 0;
            if (parseUnsigned(value, limit, parsed)) {
                target = parsed;
            } else {
                engine::logWarn(std::string("settings: ") + name + " '" + value +
                                "' is not usable; keeping the default");
            }
        };

        read("worker_threads", Settings::kMaxWorkerThreads, settings.workerThreads);
        read("render_distance", Settings::kMaxRenderDistance, settings.renderDistance);
        read("detail_distance", Settings::kMaxRenderDistance, settings.detailDistance);
        read("frame_cap", 1000, settings.frameCap);
        read("day_length_seconds", 86400, settings.dayLengthSeconds);
        read("tone_map", Settings::kToneMapperCount - 1, settings.toneMapper);
        read("shadows", Settings::kShadowQualityCount - 1, settings.shadows);
        read("clouds", Settings::kCloudQualityCount - 1, settings.clouds);
        read("rain_distance", 48, settings.rainDistance);
        read("start_weather", 3, settings.startWeather);

        const auto readInt = [&](const char* name, int& target) {
            if (key != name) {
                return;
            }
            int parsed = 0;
            if (parseInt(value, parsed)) {
                target = parsed;
            } else {
                engine::logWarn(std::string("settings: ") + name + " '" + value +
                                "' is not usable; keeping the default");
            }
        };

        readInt("spawn_x", settings.spawnX);
        readInt("spawn_z", settings.spawnZ);

        const auto readFloat = [&](const char* name, float& target) {
            if (key != name) {
                return;
            }
            float parsed = 0.0f;
            if (parseFloat(value, parsed)) {
                target = parsed;
            } else {
                engine::logWarn(std::string("settings: ") + name + " '" + value +
                                "' is not usable; keeping the default");
            }
        };

        readFloat("sound_volume", settings.soundVolume);
        readFloat("music_volume", settings.musicVolume);
        readFloat("exposure", settings.exposure);
        readFloat("bloom_strength", settings.bloomStrength);
        readFloat("handheld_light", settings.handheldLight);
        readFloat("cloud_coverage", settings.cloudCoverage);
        readFloat("cloud_shadow", settings.cloudShadow);
        readFloat("shadow_darkness", settings.shadowDarkness);
        readFloat("water_waves", settings.waterWaves);
        readFloat("water_reflection", settings.waterReflection);
        readFloat("water_foam", settings.waterFoam);
        readFloat("water_caustics", settings.waterCaustics);
        readFloat("water_refraction", settings.waterRefraction);
        readFloat("foliage_sway", settings.foliageSway);
        readFloat("anti_alias", settings.antiAlias);
        readFloat("render_scale", settings.renderScale);
        readFloat("contact_shadows", settings.contactShadows);

        if (key == "bloom") {
            settings.bloom = (value == "1" || value == "true");
        }

        if (key == "weather") {
            settings.weather = (value == "1" || value == "true");
        }

        if (key == "particles") {
            settings.particles = (value == "1" || value == "true");
        }

        if (key == "spawn_underground") {
            settings.spawnUnderground = (value == "1" || value == "true");
        }
        if (key == "creature_showcase") {
            settings.creatureShowcase = std::atoi(value.c_str());
        }
        if (key == "drop_showcase") {
            settings.dropShowcase = (value == "1" || value == "true");
        }
        if (key == "creative_mode") {
            settings.creativeMode = (value == "1" || value == "true");
        }
        if (key == "worldgen_probe") {
            settings.worldgenProbe = (value == "1" || value == "true");
        }
        if (key == "block_probe") {
            settings.blockProbe = (value == "1" || value == "true");
        }
    }

    // A render distance of zero would mesh nothing at all.
    settings.renderDistance = std::max(1u, settings.renderDistance);
    settings.detailDistance = std::max(1u, settings.detailDistance);
    settings.dayLengthSeconds = std::max(10u, settings.dayLengthSeconds);
    settings.soundVolume = std::clamp(settings.soundVolume, 0.0f, 1.0f);
    settings.musicVolume = std::clamp(settings.musicVolume, 0.0f, 1.0f);
    settings.exposure = std::clamp(settings.exposure, 0.05f, 8.0f);
    settings.bloomStrength = std::clamp(settings.bloomStrength, 0.0f, 1.0f);
    settings.handheldLight = std::clamp(settings.handheldLight, 0.0f, 1.0f);
    settings.cloudCoverage = std::clamp(settings.cloudCoverage, 0.0f, 1.0f);
    settings.cloudShadow = std::clamp(settings.cloudShadow, 0.0f, 1.0f);
    settings.shadowDarkness = std::clamp(settings.shadowDarkness, 0.0f, 1.0f);
    settings.waterWaves = std::clamp(settings.waterWaves, 0.0f, 4.0f);
    settings.waterReflection = std::clamp(settings.waterReflection, 0.0f, 1.0f);
    settings.waterFoam = std::clamp(settings.waterFoam, 0.0f, 1.0f);
    settings.waterCaustics = std::clamp(settings.waterCaustics, 0.0f, 1.0f);
    settings.waterRefraction = std::clamp(settings.waterRefraction, 0.0f, 1.0f);
    settings.foliageSway = std::clamp(settings.foliageSway, 0.0f, 3.0f);
    settings.antiAlias = std::clamp(settings.antiAlias, 0.0f, 1.0f);
    settings.contactShadows = std::clamp(settings.contactShadows, 0.0f, 64.0f);
    settings.renderScale = std::clamp(settings.renderScale, 0.5f, 1.0f);

    return settings;
}

void saveSettings(const std::filesystem::path& file, const Settings& settings) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::ofstream out(file, std::ios::trunc);
    if (!out) {
        engine::logWarn("Could not write settings to " + file.string());
        return;
    }

    out << "# Voxel Game settings. Edit by hand; the game reads this at startup.\n"
        << "#\n"
        << "# worker_threads: background threads for world generation and meshing.\n"
        << "#   0        every job runs on the main thread (lowest resource use)\n"
        << "#   1        a single background worker\n"
        << "#   default  half this machine's hardware threads\n"
        << "#\n"
        << "# render_distance: how far the world is drawn, in 32-block chunks.\n"
        << "# detail_distance: how far out plants, creatures, dropped items\n"
        << "#   and particles are drawn, in the same chunks. Past it the world\n"
        << "#   is drawn as terrain only - identical blocks, identical\n"
        << "#   lighting, no decoration. Everything out there is still there\n"
        << "#   and still running; it is only not turned into triangles. Set\n"
        << "#   it to render_distance or higher to turn it off.\n"
        << "# frame_cap: frames per second to aim for; 0 means uncapped.\n"
        << "# day_length_seconds: real seconds for a full day and night.\n"
        << "#\n"
        << "# spawn_x / spawn_z: which column to start in.\n"
        << "# spawn_underground: start on the floor of the deepest cave in that\n"
        << "#   column instead of on the surface. Only useful for testing.\n"
        << "#\n"
        << "# creature_showcase: 0 off, 1 lines every species up in front of\n"
        << "#   you, 2 and up show one species on its own (CreatureKind index\n"
        << "#   plus 2). The spawner is frozen and the world's own animals are\n"
        << "#   neither loaded nor saved while it is on. Reviewing models only.\n"
        << "#\n"
        << "# drop_showcase: 1 throws one of every awkward block on the floor in\n"
        << "#   front of you at startup, so what a dropped item looks like can be\n"
        << "#   judged without mining for it. Dropped items are never saved, so\n"
        << "#   this leaves nothing behind.\n"
        << "#\n"
        << "# creative_mode: 1 starts you with a full hotbar and placing never\n"
        << "#   runs a stack down. 0 starts you empty-handed. Blocks drop when\n"
        << "#   broken either way.\n"
        << "#\n"
        << "# sound_volume / music_volume: 0 to 1. Two buses, so the music can\n"
        << "#   be turned down without silencing the world.\n"
        << "#\n"
        << "# tone_map: which curve squashes the bright end of the image into\n"
        << "#   what a monitor can show. 0 Khronos PBR Neutral (keeps colours\n"
        << "#   as painted), 1 Hable filmic, 2 Reinhard on luminance, 3 ACES.\n"
        << "#   F10 cycles it while playing, which is the way to compare them.\n"
        << "# exposure: multiplies the scene before that curve. 1 is neutral.\n"
        << "# bloom: light bleeding out of bright surfaces. F11 toggles it.\n"
        << "# bloom_strength: how far the image is mixed toward the blurred\n"
        << "#   copy. 0.12 is subtle; past about 0.3 everything looks hazy.\n"
        << "#\n"
        << "# shadows: cast shadows from the sun. 0 off, 1 low, 2 medium,\n"
        << "#   3 high. Higher is sharper and reaches further, and costs one\n"
        << "#   extra pass over the world per slice. G cycles it while playing.\n"
        << "# shadow_darkness: how far a shadow darkens the sky light as well\n"
        << "#   as the sun, 0 to 1. Cutting the sun alone leaves shadows pale.\n"
        << "# handheld_light: a lamp on the camera, 0 to 1. Off by default -\n"
        << "#   it lights whatever you look at, which reads as a torch you are\n"
        << "#   not holding. 0.5 is a reasonable value if you want one.\n"
        << "#\n"
        << "# clouds: 0 off, 1 fast, 2 fancy - how many steps each ray takes\n"
        << "#   through the deck. C cycles it while playing.\n"
        << "# cloud_coverage: how much of the sky is cloud, 0 to 1.\n"
        << "# cloud_shadow: how far a cloud darkens the ground under it.\n"
        << "#\n"
        << "# water_waves: how steeply the ripples tilt a water surface. 0 is a\n"
        << "#   flat mirror; past about 2 it reads as choppy sea.\n"
        << "# water_reflection: how much sky the surface returns, 0 to 1. 1 is\n"
        << "#   what real water does - nearly a mirror edge-on, nearly clear\n"
        << "#   looking straight down.\n"
        << "# water_foam: the line where water meets land, 0 to 1.\n"
        << "# water_caustics: the moving net of light on a lake bed and on\n"
        << "#   everything around you while swimming, 0 to 1.\n"
        << "# water_refraction: how far the surface bends what is seen through\n"
        << "#   it, 0 to 1.\n"
        << "#\n"
        << "# weather: 1 lets rain and thunderstorms come and go on their own.\n"
        << "#   0 freezes whatever is happening. V forces clear/rain/storm\n"
        << "#   while playing either way, which is how to see one on demand.\n"
        << "# rain_distance: how far the falling curtain is drawn, in columns.\n"
        << "# start_weather: what the world opens with. 0 whatever the cycle\n"
        << "#   says, 1 rain, 2 storm, 3 clear.\n"
        << "#\n"
        << "# particles: breaking showers, rain splashes and footstep puffs.\n"
        << "# foliage_sway: how far the wind bends grass and leaves, as a\n"
        << "#   multiplier on the weather's own wind. 0 leaves the world still.\n"
        << "#\n"
        << "# anti_alias: softens hard edges, 0 to 1. Off by default - this\n"
        << "#   world is made of squares and softening diagonals softens every\n"
        << "#   texel boundary too. 0.5 is a reasonable value if you want it.\n"
        << "# contact_shadows: radius in screen pixels for the darkening where\n"
        << "#   surfaces meet. 0 turns it off; 24 is the default.\n"
        << "#\n"
        << "# worldgen_probe: 1 censuses the generated world to the log and\n"
        << "#   exits without opening a window. Reports the surface range, each\n"
        << "#   biome's share and top block, cave volume, ore counts, and how\n"
        << "#   many solid cells sit above the terrain - which is floating land\n"
        << "#   and must read zero. Run it after any change to terrain.\n"
        << "#\n"
        << "# block_probe: 1 writes block-shapes.txt beside the exe - one line\n"
        << "#   per block giving its shape and the box its geometry occupies -\n"
        << "#   and exits without opening a window. tools/check-models.ps1 sets\n"
        << "#   this itself and compares the result against the reference's own\n"
        << "#   models/block/*.json, which is the only thing that can say\n"
        << "#   whether a block is the size it is supposed to be.\n"
        << "#\n"
        << "# All of these take effect on restart.\n"
        << "worker_threads=" << settings.workerThreads << "\n"
        << "render_distance=" << settings.renderDistance << "\n"
        << "detail_distance=" << settings.detailDistance << "\n"
        << "frame_cap=" << settings.frameCap << "\n"
        << "day_length_seconds=" << settings.dayLengthSeconds << "\n"
        << "spawn_x=" << settings.spawnX << "\n"
        << "spawn_z=" << settings.spawnZ << "\n"
        << "spawn_underground=" << (settings.spawnUnderground ? 1 : 0) << "\n"
        << "creature_showcase=" << settings.creatureShowcase << "\n"
        << "drop_showcase=" << (settings.dropShowcase ? 1 : 0) << "\n"
        << "creative_mode=" << (settings.creativeMode ? 1 : 0) << "\n"
        << "sound_volume=" << settings.soundVolume << "\n"
        << "music_volume=" << settings.musicVolume << "\n"
        << "tone_map=" << settings.toneMapper << "\n"
        << "exposure=" << settings.exposure << "\n"
        << "bloom=" << (settings.bloom ? 1 : 0) << "\n"
        << "bloom_strength=" << settings.bloomStrength << "\n"
        << "shadows=" << settings.shadows << "\n"
        << "shadow_darkness=" << settings.shadowDarkness << "\n"
        << "handheld_light=" << settings.handheldLight << "\n"
        << "clouds=" << settings.clouds << "\n"
        << "cloud_coverage=" << settings.cloudCoverage << "\n"
        << "cloud_shadow=" << settings.cloudShadow << "\n"
        << "water_waves=" << settings.waterWaves << "\n"
        << "water_reflection=" << settings.waterReflection << "\n"
        << "water_foam=" << settings.waterFoam << "\n"
        << "water_caustics=" << settings.waterCaustics << "\n"
        << "water_refraction=" << settings.waterRefraction << "\n"
        << "weather=" << (settings.weather ? 1 : 0) << "\n"
        << "rain_distance=" << settings.rainDistance << "\n"
        << "start_weather=" << settings.startWeather << "\n"
        << "particles=" << (settings.particles ? 1 : 0) << "\n"
        << "foliage_sway=" << settings.foliageSway << "\n"
        << "anti_alias=" << settings.antiAlias << "\n"
        << "contact_shadows=" << settings.contactShadows << "\n"
        << "render_scale=" << settings.renderScale << "\n"
        << "worldgen_probe=" << (settings.worldgenProbe ? 1 : 0) << "\n"
        << "block_probe=" << (settings.blockProbe ? 1 : 0) << "\n";
}

} // namespace game
