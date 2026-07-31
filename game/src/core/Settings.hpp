#pragma once

#include <cstdint>
#include <filesystem>

namespace game {

/// Player-chosen options that live outside any particular world.
///
/// Everything here is read **once at startup**. Nothing re-reads the file while
/// playing, and nothing here may be changed on a running game — see
/// `workerThreads` for why that is a deliberate limit rather than laziness.
struct Settings {
    /// Background threads used for world generation and meshing.
    ///
    /// Zero means no worker threads at all: every job runs on the main thread,
    /// which is the lowest-resource mode and what somebody heavily multitasking
    /// would want. The default is half the machine's hardware threads.
    ///
    /// **Changing this requires a restart.** The thread pool is fixed once it is
    /// built, because resizing one while jobs are in flight buys a whole class
    /// of bugs for nothing.
    unsigned workerThreads = 0;

    /// Highest hardware thread count worth offering, so a settings screen has a
    /// sane upper bound and a corrupt file cannot ask for ten thousand threads.
    static constexpr unsigned kMaxWorkerThreads = 64;
};

/// Reads `file`, filling anything missing with defaults for this machine. A
/// missing or unreadable file is not an error: it yields the defaults, and the
/// file is written back so there is something to edit.
Settings loadSettings(const std::filesystem::path& file);

void saveSettings(const std::filesystem::path& file, const Settings& settings);

} // namespace game
