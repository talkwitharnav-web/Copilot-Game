#pragma once

#include <engine/render/MeshData.hpp>

namespace game::hud {

/// Full-screen panel with a progress bar, shown while the world streams in.
///
/// `progress` is 0 to 1 and is the *displayed* value, already eased. The caller
/// smooths it toward whatever the loader reports, so the bar glides rather than
/// jumping between the coarse steps chunk loading actually arrives in.
///
/// `phase` names the checkpoint being worked on, so a slow load says which part
/// is slow instead of leaving a bar sitting still.
///
/// `aspect` is width over height, so the panel covers the window whatever its
/// shape. Screen coordinates are relative to window height on both axes.
engine::MeshData makeLoadingScreen(float progress, const char* phase, float aspect);

} // namespace game::hud
