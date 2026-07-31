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
    const char* biome = "";
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
