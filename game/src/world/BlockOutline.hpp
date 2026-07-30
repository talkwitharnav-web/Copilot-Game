#pragma once

#include <engine/render/MeshData.hpp>

namespace game {

/// A wireframe cage around a unit cube at the origin, used to show which block
/// the player is aiming at. Moved into place with a transform rather than
/// rebuilt, so it costs one upload for the whole session.
///
/// Built from thin solid bars rather than actual lines, so it needs no extra
/// pipeline, no line-width support, and no second shader.
engine::MeshData makeBlockOutline();

} // namespace game
