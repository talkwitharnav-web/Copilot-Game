#include "world/FallingBlock.hpp"

#include "world/FaceShading.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

/// The reference's falling-block gravity is 0.04 blocks per tick squared with a
/// 0.98 drag applied *after* it, which is 16 blocks/s^2 and a terminal speed of
/// 1.96 blocks/tick. Both are quoted per second here because nothing else in
/// this codebase counts in ticks.
///
/// Worth knowing: this is **half** the player's gravity. A falling block is
/// noticeably lazier than a falling player, and matching the player's 32 makes
/// sand look like it is being sucked down.
constexpr float kGravity = 16.0f;
constexpr float kTerminalVelocity = 39.2f;

/// Below this the cube is close enough to its resting cell to snap into it
/// without the last fraction of a block reading as a hover.
constexpr float kSettleEpsilon = 0.001f;

} // namespace

void FallingBlocks::spawn(const glm::ivec3& cell, BlockId block) {
    if (block == BlockId::Air) {
        return;
    }
    m_blocks.push_back(Falling{glm::vec3{cell}, 0.0f, block});
}

std::vector<FallingBlocks::Crushed> FallingBlocks::update(World& world, float deltaSeconds) {
    std::vector<Crushed> crushed;

    for (std::size_t i = m_blocks.size(); i-- > 0;) {
        Falling& falling = m_blocks[i];

        falling.velocity = std::max(falling.velocity - kGravity * deltaSeconds, -kTerminalVelocity);

        const int cellX = static_cast<int>(std::floor(falling.position.x));
        const int cellZ = static_cast<int>(std::floor(falling.position.z));
        const int fromCell = static_cast<int>(std::floor(falling.position.y));
        const float wantedY = falling.position.y + falling.velocity * deltaSeconds;
        const int toCell = static_cast<int>(std::floor(wantedY));

        // Every cell the cube would pass through this frame is tested, not just
        // the one it ends in. At terminal speed a frame covers half a metre, and
        // a fast block that only checked its destination would drop straight
        // through a one-block floor.
        int restCell = toCell;
        bool landed = false;
        for (int y = fromCell; y >= toCell; --y) {
            // The cell has to be free as well as supported. Two blocks falling
            // down one shaft both aim at the same floor, and testing only what
            // is underneath let the second settle *into* the first and crush it.
            if (!isReplaceable(world.blockAt(cellX, y, cellZ))) {
                restCell = y + 1;
                landed = true;
                break;
            }
            if (y <= 0 || !isReplaceable(world.blockAt(cellX, y - 1, cellZ))) {
                restCell = y;
                landed = true;
                break;
            }
        }

        if (!landed) {
            falling.position.y = wantedY;
            continue;
        }

        const float restY = static_cast<float>(restCell);
        if (falling.position.y > restY + kSettleEpsilon) {
            // Still above its resting cell, so it keeps falling toward it and
            // lands on a later frame. Without this the block snaps down the
            // instant its landing site is known, which is the teleport again.
            falling.position.y = std::max(wantedY, restY);
            continue;
        }

        const BlockId occupying = world.blockAt(cellX, restCell, cellZ);
        if (occupying != BlockId::Air && !isWater(occupying)) {
            crushed.push_back(Crushed{{cellX, restCell, cellZ}, occupying});
        }
        world.setBlock(cellX, restCell, cellZ, falling.block);
        // A charge that was lit when it started falling is still lit when it
        // lands. Its old fuse entry points at the cell it left and finds
        // nothing there, so the countdown has to be started again here.
        if (isTntBlock(falling.block)) {
            world.setBlock(cellX, restCell, cellZ, BlockId::TntPrimed);
            world.primeTnt({cellX, restCell, cellZ});
        }

        m_blocks[i] = m_blocks.back();
        m_blocks.pop_back();
    }

    return crushed;
}

engine::MeshData FallingBlocks::buildMesh(const World& world, const DrawRange& range) const {
    engine::MeshData mesh;

    for (const Falling& falling : m_blocks) {
        if (!range.contains(falling.position)) {
            continue;
        }
        const glm::vec3 min = falling.position;
        const glm::vec3 max = min + glm::vec3{1.0f};

        // Lit by the cell the cube is passing through, so a block dropping into
        // a cave darkens as it goes rather than staying lit by the sky it left.
        const int lx = static_cast<int>(std::floor(min.x));
        const int ly = static_cast<int>(std::floor(min.y));
        const int lz = static_cast<int>(std::floor(min.z));
        const float sky =
            static_cast<float>(world.skyLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);
        const float blockLight =
            static_cast<float>(world.blockLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);

        const auto quad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                              const glm::vec3& d, BlockFace face, AxisFace axis) {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            const glm::vec3 corners[4]{a, b, c, d};
            const glm::vec2 uvs[4]{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

            for (int i = 0; i < 4; ++i) {
                mesh.vertices.push_back(
                    engine::Vertex{{corners[i].x, corners[i].y, corners[i].z},
                                   engine::packVertexColor(sky, blockLight, faceShade(axis), 1.0f),
                                   {uvs[i].x, uvs[i].y},
                                   blockTextureLayer(falling.block, face),
                                   engine::packVertexSurface(faceNormalCode(axis), 1.0f)});
            }
            mesh.indices.insert(mesh.indices.end(),
                                {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
        };

        // Shades come from the shared table rather than being written out here.
        // They were written out here, and five of the six had drifted - a bottom
        // of 0.5 against the mesher's 0.45, sides of 0.8/0.6 against 0.86, 0.60
        // and 0.72 - so a falling block *was* lit differently from the block it
        // had just been, under a comment saying it was not.
        quad({min.x, max.y, min.z}, {max.x, max.y, min.z}, {max.x, max.y, max.z},
             {min.x, max.y, max.z}, BlockFace::Top, AxisFace::PosY);
        quad({min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, min.y, min.z},
             {min.x, min.y, min.z}, BlockFace::Bottom, AxisFace::NegY);
        quad({min.x, max.y, max.z}, {max.x, max.y, max.z}, {max.x, min.y, max.z},
             {min.x, min.y, max.z}, BlockFace::Side, AxisFace::PosZ);
        quad({max.x, max.y, min.z}, {min.x, max.y, min.z}, {min.x, min.y, min.z},
             {max.x, min.y, min.z}, BlockFace::Side, AxisFace::NegZ);
        quad({max.x, max.y, max.z}, {max.x, max.y, min.z}, {max.x, min.y, min.z},
             {max.x, min.y, max.z}, BlockFace::Side, AxisFace::PosX);
        quad({min.x, max.y, min.z}, {min.x, max.y, max.z}, {min.x, min.y, max.z},
             {min.x, min.y, min.z}, BlockFace::Side, AxisFace::NegX);
    }

    return mesh;
}

} // namespace game
