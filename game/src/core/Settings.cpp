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
bool parseUnsigned(const std::string& text, unsigned& out) {
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return false;
    }
    try {
        const unsigned long value = std::stoul(text);
        if (value > Settings::kMaxWorkerThreads) {
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

        if (key == "worker_threads") {
            unsigned parsed = 0;
            if (parseUnsigned(value, parsed)) {
                settings.workerThreads = parsed;
            } else {
                engine::logWarn("settings: worker_threads '" + value + "' is not usable; keeping the default");
            }
        }
    }

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
        << "# Takes effect on restart. The thread pool is fixed once the game starts.\n"
        << "worker_threads=" << settings.workerThreads << "\n";
}

} // namespace game
