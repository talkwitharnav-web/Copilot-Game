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

    /// How far the world is drawn, in chunks (32 blocks each). Everything else
    /// derives from this: chunks are generated one ring further out so borders
    /// mesh correctly, and unloaded two rings beyond that.
    ///
    /// 12 is comfortable rather than maximal — measured at roughly 330 MB and
    /// far above the frame budget on this machine. Raise it freely.
    unsigned renderDistance = 12;

    /// Frames per second to aim for. Zero means uncapped. Adjustable at runtime
    /// with F1/F2; this is only the starting value.
    unsigned frameCap = 120;

    /// Real seconds for one full day and night. Short values are useful for
    /// watching the cycle without waiting.
    unsigned dayLengthSeconds = 600;

    /// One of everything placeable in the hotbar, and placing never runs a
    /// stack down. Useful while developing anything that is not the inventory.
    ///
    /// **Breaking still drops, and drops are still collected** - the mode
    /// changes what placing costs, not whether items exist. Suppressing either
    /// half of that produced bugs twice.
    bool creativeMode = true;

    /// Where the player starts, in world blocks. Height is still found from the
    /// terrain, so this only chooses the column.
    ///
    /// Exists so a specific place can be reached without flying there. Testing a
    /// cave otherwise means several minutes of travel before every single
    /// attempt, which is enough friction that the test stops being run.
    int spawnX = 8;
    int spawnZ = 8;

    /// Drops the player onto the floor of the deepest open space in the spawn
    /// column instead of the surface. Underground lighting is impossible to
    /// judge from above ground.
    bool spawnUnderground = false;

    /// Lines the roster up in front of the spawn point and stops the spawner
    /// touching them. Comparing a model against its reference net otherwise
    /// means waiting for the right biome to produce the right animal.
    ///
    /// 0 is off, 1 shows every species in a grid, and anything higher shows
    /// `creatureShowcase - 2` on its own, close enough to judge.
    int creatureShowcase = 0;

    /// Highest hardware thread count worth offering, so a settings screen has a
    /// sane upper bound and a corrupt file cannot ask for ten thousand threads.
    static constexpr unsigned kMaxWorkerThreads = 64;

    /// Beyond this the chunk count grows faster than anything can feed it.
    static constexpr unsigned kMaxRenderDistance = 32;
};

/// Reads `file`, filling anything missing with defaults for this machine. A
/// missing or unreadable file is not an error: it yields the defaults, and the
/// file is written back so there is something to edit.
Settings loadSettings(const std::filesystem::path& file);

void saveSettings(const std::filesystem::path& file, const Settings& settings);

} // namespace game
