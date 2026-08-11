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
    /// Seconds of breath left. The bubble bar arrives with player health at
    /// M21; until then this is the only place the timer is visible.
    float air = 0.0f;
};

/// Builds the diagnostics panel: a frame-time history graph plus labelled rows.
///
/// `history` is frame times in milliseconds, oldest first. `aspect` is needed
/// because screen coordinates are relative to window height, so the left edge
/// sits at -aspect.
///
/// Frame *time* is the headline rather than frames per second: fps is an average
/// that hides the single long frame which is what actually felt bad.
engine::MeshData makeDebugOverlay(const OverlayStats& stats, const std::vector<float>& history, float aspect);

} // namespace game
