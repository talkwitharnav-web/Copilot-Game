#include "world/Pathfinder.hpp"

#include <algorithm>
#include <cmath>

namespace game::path {
namespace {

/// Exact packing, never a hash: a collision here would silently splice two
/// different cells into one node and the route would jump. Twenty-one bits of
/// x and z reaches about a million blocks either side of the origin, and twelve
/// of y covers a world 96 tall many times over.
std::int64_t cellKey(const glm::ivec3& cell) {
    return (static_cast<std::int64_t>(cell.x & 0x1FFFFF) << 33) |
           (static_cast<std::int64_t>(cell.z & 0x1FFFFF) << 12) |
           static_cast<std::int64_t>(cell.y & 0xFFF);
}

Aabb footprint(const Agent& agent, float x, float feetY, float z) {
    return Aabb{{x - agent.halfWidth, feetY, z - agent.halfWidth},
                {x + agent.halfWidth, feetY + agent.height, z + agent.halfWidth}};
}

/// The same `overlapsSolid` the player, the creatures and the spawner use.
/// Testing whole cells here would be a second copy of a block's extent, which
/// is the recurring bug in this project.
bool fits(const World& world, const Agent& agent, float x, float feetY, float z) {
    return !overlapsSolid(world, footprint(agent, x, feetY, z));
}

/// Where a body would come to rest in this column, searching down from
/// `ceiling`. Reads `highestSurfaceBelow` - the same answer landing uses - so a
/// slab's top is half a block up rather than the cell floor.
bool surfaceIn(const World& world, const Agent& agent, float x, float z, float ceiling,
               float lowest, float& outFeet) {
    const Aabb probe{{x - agent.halfWidth, lowest, z - agent.halfWidth},
                     {x + agent.halfWidth, ceiling + 1.0f, z + agent.halfWidth}};
    const float top = highestSurfaceBelow(world, probe, ceiling);
    if (!std::isfinite(top)) {
        return false;
    }
    outFeet = top;
    return true;
}

/// What entering this cell costs beyond the distance travelled.
float penaltyFor(const World& world, const Agent& agent, const glm::ivec3& cell) {
    if (agent.amphibious) {
        // The undead walk the bottom, so water is not a hazard to route around.
        return 0.0f;
    }
    if (isWater(world.blockAt(cell.x, cell.y, cell.z))) {
        return kWaterPenalty;
    }
    if (!agent.avoidsWater) {
        return 0.0f;
    }
    if (isWater(world.blockAt(cell.x + 1, cell.y, cell.z)) ||
        isWater(world.blockAt(cell.x - 1, cell.y, cell.z)) ||
        isWater(world.blockAt(cell.x, cell.y, cell.z + 1)) ||
        isWater(world.blockAt(cell.x, cell.y, cell.z - 1))) {
        return kWaterBorderPenalty;
    }
    return 0.0f;
}

float horizontalDistance(const glm::vec3& a, const glm::vec3& b) {
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

constexpr glm::ivec2 kSteps[8] = {{1, 0}, {-1, 0}, {0, 1},  {0, -1},
                                  {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
constexpr float kDiagonalCost = 1.41421356f;

} // namespace

glm::vec3 waypoint(const Route& route, int index) {
    const glm::ivec3& cell = route.nodes[static_cast<std::size_t>(index)];
    return {static_cast<float>(cell.x) + 0.5f, route.feetY[static_cast<std::size_t>(index)],
            static_cast<float>(cell.z) + 0.5f};
}

int Pathfinder::openNode(const glm::ivec3& cell, float feetY, float cost, float estimate,
                         int parent) {
    const int index = static_cast<int>(m_nodes.size());
    m_nodes.push_back(Node{cell, feetY, cost, estimate, parent, false});
    m_lookup.emplace(cellKey(cell), index);
    m_open.push_back(index);
    std::push_heap(m_open.begin(), m_open.end(),
                   [this](int a, int b) { return m_nodes[a].estimate > m_nodes[b].estimate; });
    return index;
}

int Pathfinder::popBest() {
    std::pop_heap(m_open.begin(), m_open.end(),
                  [this](int a, int b) { return m_nodes[a].estimate > m_nodes[b].estimate; });
    const int index = m_open.back();
    m_open.pop_back();
    return index;
}

bool Pathfinder::find(const World& world, const Agent& agent, const glm::vec3& from,
                      const glm::vec3& goal, Route& out) {
    out.clear();
    m_nodes.clear();
    m_open.clear();
    m_lookup.clear();

    glm::ivec3 goalCell{static_cast<int>(std::floor(goal.x)), static_cast<int>(std::floor(goal.y)),
                        static_cast<int>(std::floor(goal.z))};
    // "If an entity's target is located inside a block (including water), the
    // target is moved to the nearest air block above it." Without it a goal
    // buried in terrain can never be matched, so every search would run its
    // whole budget before giving up.
    for (int lift = 0; lift < 4; ++lift) {
        if (fits(world, agent, goal.x, static_cast<float>(goalCell.y), goal.z)) {
            break;
        }
        ++goalCell.y;
    }
    const glm::vec3 goalCentre{static_cast<float>(goalCell.x) + 0.5f,
                               static_cast<float>(goalCell.y),
                               static_cast<float>(goalCell.z) + 0.5f};

    const glm::ivec3 startCell{static_cast<int>(std::floor(from.x)),
                               static_cast<int>(std::floor(from.y)),
                               static_cast<int>(std::floor(from.z))};
    openNode(startCell, from.y, 0.0f, horizontalDistance(from, goalCentre), -1);

    // The closest the search ever got, so an unreachable goal still produces a
    // route to the near side of whatever is in the way.
    int nearest = 0;
    float nearestDistance = horizontalDistance(from, goalCentre);
    int reached = -1;
    int expanded = 0;

    while (!m_open.empty() && expanded < kMaxVisited) {
        const int current = popBest();
        if (m_nodes[static_cast<std::size_t>(current)].closed) {
            continue;
        }
        m_nodes[static_cast<std::size_t>(current)].closed = true;
        ++expanded;

        const glm::ivec3 cell = m_nodes[static_cast<std::size_t>(current)].cell;
        const float feetY = m_nodes[static_cast<std::size_t>(current)].feetY;
        const glm::vec3 centre{static_cast<float>(cell.x) + 0.5f, feetY,
                               static_cast<float>(cell.z) + 0.5f};

        if (cell.x == goalCell.x && cell.z == goalCell.z) {
            reached = current;
            break;
        }
        const float remaining = horizontalDistance(centre, goalCentre);
        if (remaining < nearestDistance) {
            nearestDistance = remaining;
            nearest = current;
        }

        for (const glm::ivec2& step : kSteps) {
            const int nx = cell.x + step.x;
            const int nz = cell.z + step.y;
            // An absent chunk reads as air, so a route through one is fiction.
            if (!world.columnResident(nx, nz)) {
                continue;
            }
            const float nxCentre = static_cast<float>(nx) + 0.5f;
            const float nzCentre = static_cast<float>(nz) + 0.5f;
            const bool diagonal = step.x != 0 && step.y != 0;
            // No cutting a corner through a gap the body cannot pass: both
            // cardinal cells beside a diagonal have to be clear as well.
            if (diagonal &&
                (!fits(world, agent, nxCentre, feetY, static_cast<float>(cell.z) + 0.5f) ||
                 !fits(world, agent, static_cast<float>(cell.x) + 0.5f, feetY, nzCentre))) {
                continue;
            }

            float landing = 0.0f;
            if (!surfaceIn(world, agent, nxCentre, nzCentre, feetY + agent.climbHeight,
                           feetY - agent.maxDrop - 1.0f, landing)) {
                continue;
            }
            if (landing < feetY - agent.maxDrop - kCollisionSkin) {
                continue;
            }
            if (!fits(world, agent, nxCentre, landing, nzCentre)) {
                continue;
            }

            const glm::vec3 landed{nxCentre, landing, nzCentre};
            if (horizontalDistance(from, landed) > kSearchRange) {
                continue;
            }

            const glm::ivec3 nextCell{nx, static_cast<int>(std::floor(landing + kCollisionSkin)),
                                      nz};
            const float cost = m_nodes[static_cast<std::size_t>(current)].cost +
                               (diagonal ? kDiagonalCost : 1.0f) +
                               penaltyFor(world, agent, nextCell);

            const auto found = m_lookup.find(cellKey(nextCell));
            if (found != m_lookup.end()) {
                Node& existing = m_nodes[static_cast<std::size_t>(found->second)];
                if (existing.closed || cost >= existing.cost) {
                    continue;
                }
                existing.cost = cost;
                existing.estimate = cost + horizontalDistance(landed, goalCentre);
                existing.parent = current;
                existing.feetY = landing;
                m_open.push_back(found->second);
                std::push_heap(m_open.begin(), m_open.end(), [this](int a, int b) {
                    return m_nodes[static_cast<std::size_t>(a)].estimate >
                           m_nodes[static_cast<std::size_t>(b)].estimate;
                });
                continue;
            }
            openNode(nextCell, landing, cost, cost + horizontalDistance(landed, goalCentre),
                     current);
        }
    }

    const int tail = reached >= 0 ? reached : nearest;
    if (tail == 0) {
        // Never left the starting cell: nothing to walk.
        return false;
    }

    // The chain runs goal-to-start, so its length has to be known before it can
    // be written out forwards. **Keeping the first `kMaxNodes` matters** - the
    // near end is the part about to be walked, and truncating the other end
    // would hand back a route that begins somewhere the creature is not.
    int length = 0;
    for (int at = tail; at > 0; at = m_nodes[static_cast<std::size_t>(at)].parent) {
        ++length;
    }
    const int keep = std::min(length, kMaxNodes);

    int write = length - 1;
    for (int at = tail; at > 0; at = m_nodes[static_cast<std::size_t>(at)].parent) {
        if (write < keep) {
            out.nodes[static_cast<std::size_t>(write)] = m_nodes[static_cast<std::size_t>(at)].cell;
            out.feetY[static_cast<std::size_t>(write)] =
                m_nodes[static_cast<std::size_t>(at)].feetY;
        }
        --write;
    }
    out.count = static_cast<std::uint8_t>(keep);
    out.next = 0;
    return keep > 0;
}

} // namespace game::path
