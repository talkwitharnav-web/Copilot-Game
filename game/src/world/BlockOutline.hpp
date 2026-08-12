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

class World;

/// The breaking cracks over one block, **in world space** so it needs no
/// transform - it is rebuilt only when the stage or the block changes, which is
/// ten times per block at most.
///
/// Built from `worldDrawnBoxes`, which is the set the chunk mesher actually
/// emitted. That is the whole difficulty of this: `collisionBoxes` would crack
/// a fence's posts rather than its rails, `selectionBoxes` would put a cage
/// round a flower, and the outline's own union box would hang cracks in the air
/// a foot away from a torch. A `Cross` plant has no box answer at all and gets
/// the mesher's two crossed blades instead.
engine::MeshData makeBlockCracks(const World& world, const glm::ivec3& cell, float progress);

} // namespace game
