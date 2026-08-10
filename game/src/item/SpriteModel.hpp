#pragma once

#include "item/SpriteMask.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

namespace game {

/// Builds one sprite as a **solid object**: the picture extruded a texel deep,
/// with a wall raised on every boundary between a solid texel and an empty one.
///
/// That wall is the whole difference. Two flat faces a texel apart read as two
/// pictures side by side, because nothing joins them - edge on there is a gap
/// and there is no width to catch the light. Crossing two of them instead reads
/// as two objects passing through each other.
///
/// `right`, `up` and `forward` are **half-extent** vectors of equal length: the
/// model spans one of each either side of `centre`, and its thickness is
/// derived from the sprite's own grid, so it follows the art rather than a
/// constant that has to be kept in step with it.
///
/// **One owner, because there are two callers now** - a dropped item and a
/// thrown one - and this is a hundred lines of boundary walking that would
/// otherwise exist twice.
void appendSpriteModel(engine::MeshData& mesh, const SpriteMask& sprites, int spriteLayer,
                       const glm::vec3& centre, const glm::vec3& right, const glm::vec3& up,
                       const glm::vec3& forward, float sky, float blockLight);

} // namespace game
