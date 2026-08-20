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

/// Trims a value or a key down to something safe to put in a log line.
///
/// **Because a corrupt `settings.cfg` is a file of arbitrary bytes.** The
/// warnings below quote what they refused, which is the right thing to do for
/// the typo they were written for and the wrong thing for a file that came back
/// from a bad shutdown as two megabytes of one line: measured, a single junk
/// value put a 2,097,232-character line through `logWarn`. Control characters
/// go too, since a stray escape sequence in a terminal does more than look bad.
std::string forLog(const std::string& text) {
    constexpr std::size_t kMost = 48;
    std::string safe;
    safe.reserve(std::min(text.size(), kMost));
    for (std::size_t i = 0; i < text.size() && i < kMost; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        safe.push_back(c >= 0x20 && c < 0x7f ? text[i] : '?');
    }
    if (text.size() > kMost) {
        safe += "... (" + std::to_string(text.size()) + " characters)";
    }
    return safe;
}

/// Deliberately not a real config format. One key per line, `key=value`, `#`
/// starts a comment. A parser small enough to read in one sitting cannot hide a
/// bug, and the file is meant to be edited by hand.
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

/// The same "say no rather than guess" rule as the three above, applied to the
/// nine boolean keys - which had it in neither of the two places that needed
/// it.
///
/// **This used to be `value == "1" || value == "true"` written out nine
/// times**, so every other spelling meant `false` with nothing in the log. That
/// is silent for the keys that default to off, and actively wrong for the four
/// that default to on: `creative_mode=yes` put the player into survival, and
/// `bloom=TRUE` turned bloom off. Both are exactly what a person editing a file
/// whose own banner says "Edit by hand" would write.
///
/// Case-insensitive, because `True` is the other thing they would write.
bool parseBool(const std::string& text, bool& out) {
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") {
        out = true;
        return true;
    }
    if (lower == "0" || lower == "false" || lower == "no" || lower == "off") {
        out = false;
        return true;
    }
    return false;
}

/// Writes `text` over `file` without ever leaving a half-file behind.
///
/// **The world save has had this rule since it was written; this file did
/// not.** `saveSettings` opened the real file with `trunc` and streamed into
/// it, so the entire write was a window in which `settings.cfg` was incomplete
/// - and it is written during play, not only at shutdown: six hotkeys in the
/// frame loop save on every render-distance, tone-map, bloom, shadow, cloud and
/// input-mode change.
///
/// It matters more than the size of the file suggests. Measured on a file this
/// writer produced: 6,479 of its 7,245 bytes are the comment banner, ahead of
/// every key. So a file torn anywhere in the first nine tenths loads as *every*
/// setting back at its default, with nothing in the log - and the next hotkey
/// save writes that back as a complete, healthy-looking 44-key config. The
/// damage launders itself into something indistinguishable from a deliberate
/// choice, which is worse than a file that fails to open.
///
/// **Unlike `WorldStore`'s equivalent this falls back to writing in place** when
/// the rename is refused, which on Windows it is whenever anything holds the
/// target open. Measured rather than assumed: `std::filesystem::rename` onto a
/// file open for reading answers "Access is denied", and a text editor left
/// open on a file whose own banner says "Edit by hand" is not a strange thing
/// to have. For 7 KB of settings, losing the change the player just made is the
/// worse outcome; for a 100 KB world table it would not be, which is why the
/// two are allowed to differ here.
///
/// Text mode, not binary, so the line endings stay what this file has always
/// had on this platform.
bool writeWholeFile(const std::filesystem::path& file, const std::string& text) {
    const std::filesystem::path temporary = file.string() + ".tmp";

    {
        std::ofstream out(temporary, std::ios::trunc);
        if (out) {
            out << text;
            out.close();
            if (out) {
                std::error_code error;
                std::filesystem::rename(temporary, file, error);
                if (!error) {
                    return true;
                }
                engine::logWarn("Could not replace " + file.string() + ": " + error.message() +
                                "; writing in place instead");
            } else {
                // A full disk fails here, on the flush, rather than at open.
                engine::logWarn("Could not finish writing " + temporary.string() +
                                "; writing in place instead");
            }
        }
    }

    std::error_code cleanup;
    std::filesystem::remove(temporary, cleanup);

    std::ofstream direct(file, std::ios::trunc);
    if (!direct) {
        engine::logWarn("Could not write settings to " + file.string());
        return false;
    }
    direct << text;
    direct.close();
    if (!direct) {
        engine::logWarn("Could not finish writing settings to " + file.string());
        return false;
    }
    return true;
}

} // namespace

/// **What stops the writer and the reader drifting apart.**
///
/// `loadSettings` and `saveSettings` are two hand-written lists of the same 44
/// keys, in a file with no shared table between them, which is the classic
/// shape for one of them quietly gaining a field the other never learns about.
/// Two things now catch that, and it is worth being precise about which:
///
///   * This assert fires the moment a 45th setting is added - every one of the
///     44 is four bytes wide or a `bool`, and a new one of either kind moves
///     the total. Whoever re-derives the number then has both function names in
///     front of them. It does **not** catch a `bool` slipped into one of the
///     existing three-byte padding holes; nothing here does, and saying it does
///     would be worse than not having it.
///   * The unknown-key warning in `loadSettings` catches the direction that
///     actually matters. `saveSettings` writes all 44 keys and the game writes
///     its own config, so a setting added to the writer but forgotten in the
///     reader announces itself as `settings: unknown key 'new_thing'` on the
///     very next launch, on the machine of whoever made the mistake.
///
/// The number is the compiler's, not a wish: 35 four-byte members and 9 bools
/// interleaved between them, so the padding is part of the answer.
static_assert(sizeof(Settings) == 172,
              "Settings changed size - update loadSettings AND saveSettings, both of them, "
              "then re-derive this number");

Settings loadSettings(const std::filesystem::path& file) {
    Settings settings;
    settings.workerThreads = engine::defaultWorkerThreadCount();

    std::ifstream in(file);
    if (!in) {
        // **The truth, not the intention.** This said "Wrote default settings"
        // whatever happened, so a read-only directory produced a warning that
        // the write had failed and a confirmation that it had succeeded, one
        // line apart, and only the cheerful one named the file.
        if (saveSettings(file, settings)) {
            engine::logInfo("Wrote default settings to " + file.string());
        }
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

        // Set by whichever reader below owns this key. Derived from the readers
        // themselves rather than from a second list of names, because a list of
        // names kept beside the code that already names them is one more thing
        // to forget - and forgetting it would make the warning lie.
        bool known = false;

        const auto read = [&](const char* name, unsigned limit, unsigned& target) {
            if (key != name) {
                return;
            }
            known = true;
            unsigned parsed = 0;
            if (parseUnsigned(value, limit, parsed)) {
                target = parsed;
            } else {
                // **Name the bound, and name the value actually kept.** "Keeping
                // the default" does not tell a player who walked render_distance
                // up to 40 with F7 that they are about to be given 12 - not the
                // 32 this key tops out at, but the whole way back to the struct
                // initialiser, because a rejected value leaves the field
                // untouched. That reset is silent everywhere except here, so
                // this line is the only instrument the player has.
                //
                // Both halves earn their place: `parseUnsigned` fails on a
                // non-number *and* on anything above `limit`, so the range is
                // phrased as what was expected rather than as what was wrong,
                // and stays true for either cause.
                engine::logWarn(std::string("settings: ") + name + " '" + forLog(value) +
                                "' is not usable (expected 0.." + std::to_string(limit) +
                                "); keeping " + std::to_string(target));
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
        read("rain_distance", Settings::kMaxRainDistance, settings.rainDistance);
        read("start_weather", Settings::kStartWeatherCount - 1, settings.startWeather);
        read("input_mode", Settings::kInputModeCount - 1, settings.inputMode);

        const auto readInt = [&](const char* name, int& target) {
            if (key != name) {
                return;
            }
            known = true;
            int parsed = 0;
            if (parseInt(value, parsed)) {
                target = parsed;
            } else {
                engine::logWarn(std::string("settings: ") + name + " '" + forLog(value) +
                                "' is not usable; keeping the default");
            }
        };

        readInt("spawn_x", settings.spawnX);
        readInt("spawn_z", settings.spawnZ);
        // **Through the validated parser like everything else.** This was the
        // one key still on `std::atoi`, which answers 0 for anything it cannot
        // read - so `creature_showcase=on` silently meant "off" with nothing in
        // the log to say a setting had been ignored.
        readInt("creature_showcase", settings.creatureShowcase);
        const auto readFloat = [&](const char* name, float& target) {
            if (key != name) {
                return;
            }
            known = true;
            float parsed = 0.0f;
            if (parseFloat(value, parsed)) {
                target = parsed;
            } else {
                engine::logWarn(std::string("settings: ") + name + " '" + forLog(value) +
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
        readFloat("controller_look_sensitivity", settings.controllerLookSensitivity);
        readFloat("controller_cursor_sensitivity", settings.controllerCursorSensitivity);
        readFloat("controller_deadzone", settings.controllerDeadzone);
        readFloat("controller_rumble", settings.controllerRumble);

        const auto readBool = [&](const char* name, bool& target) {
            if (key != name) {
                return;
            }
            known = true;
            bool parsed = false;
            if (parseBool(value, parsed)) {
                target = parsed;
            } else {
                engine::logWarn(std::string("settings: ") + name + " '" + forLog(value) +
                                "' is not usable; keeping the default");
            }
        };

        readBool("controller_invert_y", settings.controllerInvertY);
        readBool("bloom", settings.bloom);
        readBool("weather", settings.weather);
        readBool("particles", settings.particles);
        readBool("spawn_underground", settings.spawnUnderground);
        readBool("drop_showcase", settings.dropShowcase);
        readBool("creative_mode", settings.creativeMode);
        readBool("worldgen_probe", settings.worldgenProbe);
        readBool("block_probe", settings.blockProbe);

        // **A misspelled key used to do nothing, and say nothing.** A bad value
        // has warned since this parser was written; a bad *name* was silent, so
        // `render_distence=24` looked exactly like a setting the game ignores.
        // Same rule, the other half of the same line.
        //
        // An empty key is a malformed line rather than a misspelled setting -
        // `=16` names nothing to correct - so it stays quiet as it always has.
        if (!known && !key.empty()) {
            engine::logWarn("settings: unknown key '" + forLog(key) + "' ignored");
        }
    }

    // A render distance of zero would mesh nothing at all.
    settings.renderDistance = std::max(1u, settings.renderDistance);
    settings.detailDistance = std::max(1u, settings.detailDistance);
    settings.dayLengthSeconds = std::max(10u, settings.dayLengthSeconds);
    // **The two numeric keys that had no bound at all until now.** Nothing here
    // is about taste: world code derives a chunk base from the spawn column and
    // then adds constants to it, so a spawn near `INT_MAX` overflows a signed
    // int - undefined behaviour that `/W4` cannot see and that would show up as
    // terrain generated somewhere else entirely rather than as a crash.
    settings.spawnX = std::clamp(settings.spawnX, -Settings::kMaxSpawn, Settings::kMaxSpawn);
    settings.spawnZ = std::clamp(settings.spawnZ, -Settings::kMaxSpawn, Settings::kMaxSpawn);
    // Negative would be a showcase of nothing; the upper end is only a sanity
    // bound, since the roster index is clamped again against the real species
    // count where it is used.
    settings.creatureShowcase =
        std::clamp(settings.creatureShowcase, 0, Settings::kMaxCreatureShowcase);
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
    settings.controllerLookSensitivity = std::clamp(settings.controllerLookSensitivity, 0.1f, 5.0f);
    settings.controllerCursorSensitivity = std::clamp(settings.controllerCursorSensitivity, 0.1f, 5.0f);
    // Never the whole stick: a dead zone of one would leave the pad inert with
    // nothing on screen to say why. **The bound is `Settings`' own**, because
    // `Gamepad.cpp` clamps this a second time and used to carry a different
    // number for it.
    settings.controllerDeadzone =
        std::clamp(settings.controllerDeadzone, 0.0f, Settings::kMaxControllerDeadzone);
    settings.controllerRumble = std::clamp(settings.controllerRumble, 0.0f, 1.0f);

    return settings;
}

bool saveSettings(const std::filesystem::path& file, const Settings& settings) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::ostringstream out;
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
        << "# spawn_x / spawn_z: which column to start in. Either way from the\n"
        << "#   origin, up to 30,000,000 blocks.\n"
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
        << "#   judged without mining for it. The world's own dropped items are\n"
        << "#   neither loaded nor saved while it is on. Reviewing models only.\n"
        << "#\n"
        << "# creative_mode: 1 starts you with a full hotbar and placing never\n"
        << "#   runs a stack down. 0 starts you empty-handed. Blocks drop when\n"
        << "#   broken either way.\n"
        << "#\n"
        << "# sound_volume / music_volume: 0 to 1. Two buses, and neither one\n"
        << "#   scales the other: silencing the world leaves the music playing\n"
        << "#   and silencing the music leaves the world alone.\n"
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
        << "# input_mode: 0 follows whichever device you last touched, 1 pins\n"
        << "#   the keyboard and mouse, 2 pins the gamepad. F cycles it while\n"
        << "#   playing. Pin it if a worn stick drifts, or if a pad is plugged\n"
        << "#   in and you want the keyboard anyway.\n"
        << "# controller_look_sensitivity: right-stick camera speed, as a\n"
        << "#   multiple of the default. 1 turns about 155 degrees a second at\n"
        << "#   full deflection; raise it if turning feels slow.\n"
        << "# controller_cursor_sensitivity: pointer speed in menus, likewise.\n"
        << "# controller_invert_y: 1 pitches up when the stick goes down.\n"
        << "# controller_deadzone: how much of each stick's travel from centre\n"
        << "#   counts as no movement, 0 to 0.6. Raise it if a worn stick walks\n"
        << "#   on its own; lower it for finer aim on a new pad.\n"
        << "# controller_rumble: how hard the pad vibrates, 0 to 1. 0 is off.\n"
        << "#   Cues fire on landing a hit, taking damage, a heavy landing, a\n"
        << "#   block breaking, loosing an arrow, an explosion and dying.\n"
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
        << "input_mode=" << settings.inputMode << "\n"
        << "controller_look_sensitivity=" << settings.controllerLookSensitivity << "\n"
        << "controller_cursor_sensitivity=" << settings.controllerCursorSensitivity << "\n"
        << "controller_invert_y=" << (settings.controllerInvertY ? 1 : 0) << "\n"
        << "controller_deadzone=" << settings.controllerDeadzone << "\n"
        << "controller_rumble=" << settings.controllerRumble << "\n"
        << "worldgen_probe=" << (settings.worldgenProbe ? 1 : 0) << "\n"
        << "block_probe=" << (settings.blockProbe ? 1 : 0) << "\n";

    return writeWholeFile(file, out.str());
}

} // namespace game
