#pragma once

#include <engine/render/MeshData.hpp>

namespace game {

/// A wireframe cage around a block at the origin, used to show which block the
/// player is aiming at. Moved into place with a transform rather than rebuilt
/// every frame.
///
/// `height` matches the block's shape, so a slab is cased by a half-height cage.
/// Scaling a unit cage down instead would thin its horizontal bars along with
/// it, so the mesh is rebuilt on the rare frames the targeted height changes.
///
/// Built from thin solid bars rather than actual lines, so it needs no extra
/// pipeline, no line-width support, and no second shader.
engine::MeshData makeBlockOutline(float height = 1.0f);

} // namespace game
