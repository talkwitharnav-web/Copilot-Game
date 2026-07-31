#pragma once

#include <engine/render/MeshData.hpp>

namespace game::hud {

/// Full-screen panel with a progress bar, shown while the world streams in.
///
/// `progress` is 0 to 1 and is the *displayed* value, already eased. The caller
/// smooths it toward whatever the loader reports, so the bar glides rather than
/// jumping between the coarse steps chunk loading actually arrives in.
///
/// `aspect` is width over height, so the panel covers the window whatever its
/// shape. Screen coordinates are relative to window height on both axes.
engine::MeshData makeLoadingScreen(float progress, float aspect);

} // namespace game::hud
