#pragma once

#include "engine/render/MeshData.hpp"
#include "world/Block.hpp"
#include "world/DrawRange.hpp"

#include <glm/vec3.hpp>

#include <cstddef>
#include <vector>

namespace game {

class World;

/// Sand and gravel on the way down, drawn as full cubes that move smoothly.
///
/// **The world hands these over already detached.** `World` sets the cell to air
/// and reports it, exactly as it reports a plant a flow swept aside, because it
/// has no idea entities exist. That keeps the falling *block* in the world and
/// the falling *animation* out of it.
///
/// Stepping one cell per scheduled update would have been a tenth of this code
/// and it reads as teleporting: the whole point of a falling block is that you
/// can watch it accelerate.
class FallingBlocks {
public:
    void spawn(const glm::ivec3& cell, BlockId block);

    /// Something the landing block destroyed, for the caller to turn into a
    /// drop. Same shape and the same reason as `World::WashedBlock`.
    struct Crushed {
        glm::ivec3 position;
        BlockId block;
    };

    /// Accelerates, lands, and writes whatever landed back into the world.
    std::vector<Crushed> update(World& world, float deltaSeconds);

    /// Rebuilt every frame rather than transformed, for the same reason the
    /// drops are: world meshes are drawn with an identity model matrix.
    engine::MeshData buildMesh(const World& world, const DrawRange& range = {}) const;

    std::size_t count() const { return m_blocks.size(); }

private:
    struct Falling {
        /// Minimum corner of the cube, so the cell it occupies is a floor away.
        glm::vec3 position{0.0f};
        float velocity = 0.0f;
        BlockId block = BlockId::Air;
    };

    std::vector<Falling> m_blocks;
};

} // namespace game
