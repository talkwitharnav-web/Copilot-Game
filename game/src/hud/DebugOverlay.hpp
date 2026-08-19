#pragma once

#include <engine/render/MeshData.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace game {

/// Everything the diagnostics overlay reports for the frame just finished.
struct OverlayStats {
    float frameMilliseconds = 0.0f;
    float gpuMilliseconds = 0.0f;
    int fps = 0;
    std::size_t loadedChunks = 0;
    std::size_t meshes = 0;
    std::size_t pending = 0;
    std::size_t retired = 0;
    std::uint32_t drawCalls = 0;
    std::uint32_t triangles = 0;
    unsigned workerThreads = 0;
    int renderDistance = 0;
    /// Live GPU allocations against what the device allows, and megabytes of
    /// pooled buffer memory in use. The allocation count is a hard limit whose
    /// spec floor is 4096, so it is worth a row of its own.
    std::uint32_t deviceAllocations = 0;
    std::uint32_t deviceAllocationLimit = 0;
    std::uint32_t gpuMegabytes = 0;
    /// The distance tier: how far out chunks keep their decoration, and how
    /// many drawn chunks currently do. Equal to or above `renderDistance`
    /// means off, which is what the row prints.
    int detailDistance = 0;
    std::size_t detailedChunks = 0;
    /// Chunks drawn at a tier they should no longer be at, and how far off the
    /// nearest of them is. **Standing still this must fall to zero**; while
    /// moving, the nearest should sit at the detail distance rather than
    /// anywhere near the player.
    std::size_t detailLagChunks = 0;
    int detailLagNearest = -1;
    const char* biome = "";
    /// Which of the four tone curves F10 is currently on, and which shadow
    /// quality G is on. Both are here because the only way to choose between
    /// them is to look at the screen, and the screen has to say which one it is
    /// showing.
    const char* toneMapper = "";
    const char* shadows = "";
    const char* clouds = "";
    /// Seconds of breath left.
    ///
    /// **This was documented as the only place the timer is visible "until the
    /// bubble bar arrives at M21". It arrived** - `hud::makeStatusBars` draws
    /// the row today, off `airRow(player.air / fluid::kAirSeconds)`, and the
    /// HUD's own rebuild trigger compares against the same function. So this
    /// row is now a convenience that reads the raw seconds rather than the only
    /// window onto them, and nobody should treat it as load-bearing or go and
    /// build the bar it says is missing (corrected 2026-08-19). What would make
    /// this note false: the air row leaving `StatusBars.cpp`.
    float air = 0.0f;
};

/// Builds the diagnostics panel: a frame-time history graph plus labelled rows.
///
/// `history` is frame times in milliseconds, oldest first. `aspect` is needed
/// because screen coordinates are relative to window height, so the left edge
/// sits at -aspect.
///
/// Frame *time* is the headline rather than frames per second, because a figure
/// in milliseconds can be held against a budget - 16.67 for 60 Hz, which is the
/// one guide line the graph draws - and a reciprocal cannot.
///
/// **What the `fps` row is not, is an average** (corrected 2026-08-19). The
/// header used to justify the ordering by saying fps is an average that hides
/// the single long frame; ours is filled as `1 / deltaSeconds` off the raw
/// frame delta, unsmoothed, from the *same* measurement as `frameMilliseconds`
/// - so the two rows are one number in two units and the fps row hides nothing
/// the cpu row shows. A reader who believed the old note would go looking for
/// smoothing that has never existed, or add some, and the graph beside it -
/// which is per-frame samples - would then disagree with the number above it.
engine::MeshData makeDebugOverlay(const OverlayStats& stats, const std::vector<float>& history, float aspect);

} // namespace game
