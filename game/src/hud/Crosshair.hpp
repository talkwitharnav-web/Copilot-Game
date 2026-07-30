#pragma once

#include <engine/render/MeshData.hpp>

namespace game {

/// The aiming reticle, in screen space.
///
/// Two bars with a dark border behind them, so it stays readable against both
/// bright sky and dark stone. Uploaded once and drawn every frame; it never
/// moves, because it marks the exact centre of the screen.
engine::MeshData makeCrosshair();

} // namespace game
