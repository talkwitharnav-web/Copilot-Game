#include "world/ItemEntity.hpp"

#include "world/World.hpp"

#include <algorithm>
#include <cmath>

namespace game {
namespace {

constexpr float kGravity = 22.0f;
constexpr float kHalfSize = 0.10f;
constexpr float kTerminalVelocity = 24.0f;

/// Stops a drop being collected the instant it leaves the block, which would
/// make breaking look like the item never existed. Thrown items pass a longer
/// one, so they clear the attraction radius before it applies.
constexpr float kAttractRadius = 2.0f;
constexpr float kCollectRadius = 0.7f;
constexpr float kAttractSpeed = 7.0f;

constexpr float kBobHeight = 0.07f;
constexpr float kBobSpeed = 2.2f;
constexpr float kSpinSpeed = 1.1f;

/// How much speed a landing keeps, and how slow an impact has to be before the
/// drop simply stops.
///
/// This is a *deliberate* bounce, unlike the accidental ones that come from
/// resting an object above its surface. It terminates because restitution is
/// below one and anything slower than the threshold settles outright, so do not
/// remove the threshold to make it bouncier.
constexpr float kRestitution = 0.42f;
constexpr float kSettleSpeed = 2.0f;
constexpr float kGroundFriction = 0.6f;

/// Five minutes, as the reference has it. Ours runs on wall time rather than
/// pausing when a chunk stops ticking, because drops are not saved anyway.
constexpr float kDespawnSeconds = 300.0f;

glm::vec3 eyeLevel(const glm::vec3& feet) {
    return {feet.x, feet.y + 0.9f, feet.z};
}

} // namespace

void ItemEntities::spawn(const glm::vec3& position, ItemId item, int count, const glm::vec3& impulse,
                         float pickupDelay) {
    if (item == ItemId::None || count <= 0) {
        return;
    }

    Drop drop;
    drop.position = position;
    // A small deterministic-looking scatter, so several drops from one spot do
    // not stack into a single sprite.
    const float jitter = static_cast<float>((m_drops.size() * 37) % 17) / 17.0f - 0.5f;
    drop.velocity = impulse + glm::vec3{jitter * 1.4f, 3.0f, jitter * -1.1f};
    drop.item = item;
    drop.count = count;
    drop.pickupDelay = pickupDelay;
    m_drops.push_back(drop);
}

void ItemEntities::update(const World& world, const glm::vec3& playerFeet, float deltaSeconds) {
    const glm::vec3 target = eyeLevel(playerFeet);

    // Anything nobody came back for is gone after five minutes, matching the
    // reference. Without it a long session accumulates every drop it ever made
    // - and each one rebuilds its geometry every frame.
    for (std::size_t i = m_drops.size(); i-- > 0;) {
        if (m_drops[i].age > kDespawnSeconds) {
            m_drops[i] = m_drops.back();
            m_drops.pop_back();
        }
    }

    for (Drop& drop : m_drops) {
        drop.age += deltaSeconds;

        const float distance = glm::length(target - drop.position);
        if (drop.age > drop.pickupDelay && distance < kAttractRadius) {
            // Drifts toward the player rather than snapping, so collecting reads
            // as the item coming to you.
            //
            // Note the missing lower bound on distance. Falling through to
            // gravity once the drop arrives makes it drop, re-attract and drop
            // again - an item bouncing off the player it is trying to reach.
            if (distance > 0.0001f) {
                const glm::vec3 pull = (target - drop.position) / distance;
                drop.position += pull * std::min(kAttractSpeed * deltaSeconds, distance);
            }
            drop.velocity = glm::vec3{0.0f};
            continue;
        }

        drop.velocity.y = std::max(drop.velocity.y - kGravity * deltaSeconds, -kTerminalVelocity);

        glm::vec3 next = drop.position + drop.velocity * deltaSeconds;

        // Only the vertical axis is resolved. A drop is small and slow, and
        // full collision on something purely decorative is not worth the cost.
        const int cellX = static_cast<int>(std::floor(next.x));
        const int cellZ = static_cast<int>(std::floor(next.z));

        // Probed just under the drop's underside. Resting it *above* the surface
        // leaves a gap it then falls through, and the landing pushes it back up:
        // a permanent bounce rather than a clean failure. The hover is the bob
        // animation's job, not the physics'.
        const float bottom = next.y - kHalfSize;
        const int below = static_cast<int>(std::floor(bottom - 0.01f));

        drop.onGround = false;
        if (world.isSolid(cellX, below, cellZ)) {
            const BlockBoxes shape = collisionBoxes(world.blockAt(cellX, below, cellZ));
            float surface = static_cast<float>(below);
            for (int i = 0; i < shape.count; ++i) {
                surface = std::max(surface, static_cast<float>(below) + shape.boxes[i].maxY);
            }
            if (bottom <= surface) {
                next.y = surface + kHalfSize;

                if (-drop.velocity.y > kSettleSpeed) {
                    drop.velocity.y = -drop.velocity.y * kRestitution;
                    drop.velocity.x *= kGroundFriction;
                    drop.velocity.z *= kGroundFriction;
                } else {
                    drop.velocity = glm::vec3{0.0f};
                    drop.onGround = true;
                }
            }
        }

        drop.position = next;
    }
}

std::vector<ItemEntities::Collectable> ItemEntities::collectable(const glm::vec3& playerFeet) const {
    const glm::vec3 target = eyeLevel(playerFeet);
    std::vector<Collectable> ready;

    for (std::size_t i = 0; i < m_drops.size(); ++i) {
        const Drop& drop = m_drops[i];
        if (drop.age > drop.pickupDelay && glm::length(target - drop.position) < kCollectRadius) {
            ready.push_back({i, drop.item, drop.count});
        }
    }
    return ready;
}

void ItemEntities::remove(std::size_t index) {
    if (index < m_drops.size()) {
        // Order carries no meaning, so the last one fills the hole.
        m_drops[index] = m_drops.back();
        m_drops.pop_back();
    }
}

void ItemEntities::reduce(std::size_t index, int taken) {
    if (index >= m_drops.size()) {
        return;
    }
    m_drops[index].count -= taken;
    if (m_drops[index].count <= 0) {
        remove(index);
    }
}

engine::MeshData ItemEntities::buildMesh(const World& world, float timeSeconds) const {
    engine::MeshData mesh;

    for (const Drop& drop : m_drops) {
        if (drop.item == ItemId::None) {
            continue;
        }
        // Anything that is not a block has no cube to build, so it draws as a
        // sprite. Until spawn eggs arrived nothing ever tested this: a dropped
        // pickaxe existed, fell, could be picked up and rendered **nothing at
        // all**, which is a bug you can only see by throwing one on the floor.
        const bool blockLike = isBlockItem(drop.item);
        const int spriteLayer = blockLike ? -1 : itemTextureLayer(drop.item);
        if (!blockLike && spriteLayer < 0) {
            continue;
        }
        const BlockId block = blockLike ? blockForItem(drop.item) : BlockId::Air;

        const float bob = drop.onGround ? (std::sin(timeSeconds * kBobSpeed + drop.position.x) * 0.5f + 0.5f) * kBobHeight
                                        : 0.0f;
        const glm::vec3 centre{drop.position.x, drop.position.y + bob, drop.position.z};

        const float angle = timeSeconds * kSpinSpeed + drop.position.z;
        const float c = std::cos(angle);
        const float s = std::sin(angle);

        // Lit by the cell it sits in, so a drop in a cave is as dark as its
        // surroundings instead of glowing.
        const int lx = static_cast<int>(std::floor(centre.x));
        const int ly = static_cast<int>(std::floor(centre.y));
        const int lz = static_cast<int>(std::floor(centre.z));
        const float sky = static_cast<float>(world.skyLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);
        const float blockLight = static_cast<float>(world.blockLightAt(lx, ly, lz)) / static_cast<float>(kMaxLight);

        // Faces of a spun cube. Only the four sides and the top are emitted; the
        // underside of something resting on the ground is never seen.
        const glm::vec3 right{c * kHalfSize, 0.0f, s * kHalfSize};
        const glm::vec3 forward{-s * kHalfSize, 0.0f, c * kHalfSize};
        const glm::vec3 up{0.0f, kHalfSize, 0.0f};

        const auto quad = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& d, const glm::vec3& e,
                              BlockFace face, float shade) {
            const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
            const glm::vec3 corners[4]{a, b, d, e};
            const glm::vec2 uvs[4]{{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};

            for (int i = 0; i < 4; ++i) {
                mesh.vertices.push_back(engine::Vertex{{corners[i].x, corners[i].y, corners[i].z},
                                                       {sky, blockLight, shade, 1.0f},
                                                       {uvs[i].x, uvs[i].y},
                                                       blockLike ? blockTextureLayer(block, face)
                                                                 : static_cast<float>(spriteLayer)});
            }
            // Both windings: the cube spins, so either side can face the camera.
            mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                                     base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
        };

        // A plant is two crossed quads, the same shape it has once placed and
        // the same reason the hotbar icon is flat: wrapping the artwork around a
        // cube shows a box of grass rather than the thing you are holding.
        // Crossed rather than one sprite because the drop spins, and a single
        // quad turns edge-on and disappears twice a revolution.
        //
        // A tool or a spawn egg takes the same path, for the same reason.
        if (!blockLike || blockShape(block) == BlockShape::Cross) {
            constexpr float kSquare = 0.70710678f; // 1/sqrt(2), so the diagonal matches a cube face
            const glm::vec3 a = (right + forward) * kSquare;
            const glm::vec3 b = (right - forward) * kSquare;
            quad(centre - a - up, centre + a - up, centre + a + up, centre - a + up, BlockFace::Side, 1.0f);
            quad(centre - b - up, centre + b - up, centre + b + up, centre - b + up, BlockFace::Side, 1.0f);
            continue;
        }

        quad(centre - right - forward - up, centre + right - forward - up, centre + right - forward + up,
             centre - right - forward + up, BlockFace::Side, 0.86f);
        quad(centre + right + forward - up, centre - right + forward - up, centre - right + forward + up,
             centre + right + forward + up, BlockFace::Side, 0.60f);
        quad(centre + right - forward - up, centre + right + forward - up, centre + right + forward + up,
             centre + right - forward + up, BlockFace::Side, 0.72f);
        quad(centre - right + forward - up, centre - right - forward - up, centre - right - forward + up,
             centre - right + forward + up, BlockFace::Side, 0.72f);
        quad(centre - right + forward + up, centre + right + forward + up, centre + right - forward + up,
             centre - right - forward + up, BlockFace::Top, 1.0f);
    }

    return mesh;
}

} // namespace game
