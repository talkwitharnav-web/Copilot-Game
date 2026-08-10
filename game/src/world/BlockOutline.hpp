#pragma once

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

namespace game {

/// A wireframe cage around a block at the origin, used to show which block the
/// player is aiming at. Moved into place with a transform rather than rebuilt
/// every frame.
///
/// `size` matches the block's shape in blocks, so a slab is cased by a
/// half-height cage and a double chest by one cage two wide rather than two
/// touching ones. Scaling a unit cage instead would thin its bars along with
/// it, so the mesh is rebuilt on the rare frames the targeted size changes.
///
/// Built from thin solid bars rather than actual lines, so it needs no extra
/// pipeline, no line-width support, and no second shader.
engine::MeshData makeBlockOutline(const glm::vec3& size = glm::vec3{1.0f});

} // namespace game
