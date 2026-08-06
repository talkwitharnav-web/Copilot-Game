#pragma once

#include "world/Collision.hpp"
#include "world/World.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>

namespace game::path {

/// Longest route a creature carries. A chase rebuilds its path twice a second,
/// so a truncated one is simply extended as the creature walks it - this bounds
/// the memory each animal holds, not how far it can travel.
inline constexpr int kMaxNodes = 32;

/// How far the search may spread from where it started, and how many cells it
/// may expand before giving up. Both exist so that one boxed-in animal cannot
/// cost a frame; the reference bounds its own search by the mob's follow range.
inline constexpr float kSearchRange = 24.0f;
inline constexpr int kMaxVisited = 384;

/// The reference's own node penalties (`RESEARCH.md` §8.5, confirmed against
/// the wiki's table). Only the two water entries can arise here - we have no
/// lava, fire, cactus, honey or doors - and the border one is the interesting
/// half: **a "danger" penalty applies to the neighbours of the hazard rather
/// than to the hazard itself**, which is what makes a mob give water a berth
/// instead of skimming its edge.
inline constexpr float kWaterPenalty = 8.0f;
inline constexpr float kWaterBorderPenalty = 8.0f;

/// What a body may traverse.
///
/// Bedrock keeps this apart from *how* the body travels - `navigation.*`
/// configures the search, `movement.*` the locomotion - and the split is worth
/// copying. The one rule that matters: **the planner and the legs have to agree
/// on what counts as passable.** A search that rejects a rise the legs could
/// climb routes around every hill, which is the bug the steering fan already
/// had once.
struct Agent {
    float halfWidth = 0.3f;
    float height = 1.0f;
    /// Highest rise it may reach, by stepping or by jumping. Deliberately the
    /// same `max(stepHeight, jumpHeight)` the steering fan probes with.
    float climbHeight = 0.6f;
    float maxDrop = 3.0f;
    /// Bedrock's `avoid_water`: pathing only. It routes around a pond, and is
    /// perfectly capable of being knocked into one.
    bool avoidsWater = false;
    /// Bedrock's `is_amphibious`: the seabed is a road, so water costs nothing.
    bool amphibious = false;
};

/// A found route: the cells to walk through, in order.
struct Route {
    std::array<glm::ivec3, kMaxNodes> nodes{};
    /// Feet height at each node, which is **not** the cell's floor on a slab or
    /// a stair - it is whatever `highestSurfaceBelow` answered.
    std::array<float, kMaxNodes> feetY{};
    std::uint8_t count = 0;
    std::uint8_t next = 0;

    bool empty() const { return count == 0; }
    bool finished() const { return next >= count; }
    void clear() {
        count = 0;
        next = 0;
    }
};

/// Centre of a node's cell horizontally, at that node's own feet height.
glm::vec3 waypoint(const Route& route, int index);

/// A* over walkable cells.
///
/// Owns its scratch buffers, so repeated searches do not allocate and there is
/// no global mutable state for a future thread to trip over. One instance lives
/// on `Creatures`; searching is main-thread work like light and water.
class Pathfinder {
public:
    /// Fills `out` with a route from `from` toward `goal`.
    ///
    /// Returns false only when there was nowhere at all to go. **A search that
    /// cannot reach the goal still returns the route to the closest cell it
    /// reached**, which is the reference's behaviour and the reason a mob walks
    /// up to the near side of a wall rather than standing still in the open.
    bool find(const World& world, const Agent& agent, const glm::vec3& from, const glm::vec3& goal,
              Route& out);

private:
    struct Node {
        glm::ivec3 cell{0};
        float feetY = 0.0f;
        /// What it took to get here, and that plus the guess at what remains.
        float cost = 0.0f;
        float estimate = 0.0f;
        int parent = -1;
        bool closed = false;
    };

    int openNode(const glm::ivec3& cell, float feetY, float cost, float estimate, int parent);
    int popBest();

    std::vector<Node> m_nodes;
    std::vector<int> m_open;
    std::unordered_map<std::int64_t, int> m_lookup;
};

} // namespace game::path
