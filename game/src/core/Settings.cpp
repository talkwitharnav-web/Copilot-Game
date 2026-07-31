#include "core/Settings.hpp"

#include <engine/core/JobSystem.hpp>
#include <engine/core/Log.hpp>

#include <algorithm>
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
        read("frame_cap", 1000, settings.frameCap);
        read("day_length_seconds", 86400, settings.dayLengthSeconds);
    }

    // A render distance of zero would mesh nothing at all.
    settings.renderDistance = std::max(1u, settings.renderDistance);
    settings.dayLengthSeconds = std::max(10u, settings.dayLengthSeconds);

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
        << "# frame_cap: frames per second to aim for; 0 means uncapped.\n"
        << "# day_length_seconds: real seconds for a full day and night.\n"
        << "#\n"
        << "# All of these take effect on restart.\n"
        << "worker_threads=" << settings.workerThreads << "\n"
        << "render_distance=" << settings.renderDistance << "\n"
        << "frame_cap=" << settings.frameCap << "\n"
        << "day_length_seconds=" << settings.dayLengthSeconds << "\n";
}

} // namespace game
