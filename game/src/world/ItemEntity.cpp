#include "world/ItemEntity.hpp"

#include "item/SpriteModel.hpp"

#include "world/Collision.hpp"
#include "world/Fluid.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game {
namespace {

constexpr float kGravity = 22.0f;
/// Half a drop's extent. The reference renders one at a quarter of a block;
/// ours is larger on purpose, because a dropped tool at that size is hard to
/// pick out of grass. It is the collision box as well as the model, so the two
/// cannot disagree about how big the thing is.
constexpr float kHalfSize = 0.15f;
constexpr float kTerminalVelocity = 24.0f;

/// How fast a drop rises to the surface. Gentle on purpose - the reference's
/// buoyancy bobs an item up rather than firing it out of the water.
constexpr float kFloatSpeed = 0.6f;

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
                         float pickupDelay, int damage) {
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
    drop.damage = damage;
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

        // Resolved on every axis, through the same helpers the player and the
        // creatures use. It used to be vertical only, on the reasoning that a
        // drop is small and decorative - but nothing stopped one entering a
        // block sideways, and once inside, the ground probe found *that* block
        // and lifted the drop onto its top. So an item nudged against a step
        // climbed it.
        const auto boxAt = [](const glm::vec3& centre) {
            return Aabb{{centre.x - kHalfSize, centre.y - kHalfSize, centre.z - kHalfSize},
                        {centre.x + kHalfSize, centre.y + kHalfSize, centre.z + kHalfSize}};
        };

        // A dropped item genuinely floats in the reference - it carries
        // `minecraft:buoyant`, which the player does not - so anything lost in a
        // lake washes up rather than being gone. It is carried by the current
        // too, under the same drag as everything else in the water.
        //
        // Water **replaces** gravity here rather than following it. Applying
        // both offsets the settling speed by `g dt k / (1 - k)`, which is metres
        // per second at any real frame rate - enough to sink an item that every
        // constant says should float.
        const fluid::FluidContact water = fluid::sampleFluid(world, boxAt(drop.position));
        if (water.inWater) {
            drop.velocity.y =
                fluid::approach(drop.velocity.y, kFloatSpeed, fluid::kWaterDrag, deltaSeconds);
            const glm::vec3 carried = water.flow * fluid::kCurrentSpeed;
            drop.velocity.x =
                fluid::approach(drop.velocity.x, carried.x, fluid::kWaterDrag, deltaSeconds);
            drop.velocity.z =
                fluid::approach(drop.velocity.z, carried.z, fluid::kWaterDrag, deltaSeconds);
        } else {
            drop.velocity.y = std::max(drop.velocity.y - kGravity * deltaSeconds, -kTerminalVelocity);
        }

        const glm::vec3 step = drop.velocity * deltaSeconds;
        glm::vec3 next = drop.position;

        // One axis at a time, so a corner pushes out along one of them rather
        // than being refused entirely.
        next.x += step.x;
        if (overlapsSolid(world, boxAt(next))) {
            next.x = drop.position.x;
            drop.velocity.x = 0.0f;
        }
        next.z += step.z;
        if (overlapsSolid(world, boxAt(next))) {
            next.z = drop.position.z;
            drop.velocity.z = 0.0f;
        }

        drop.onGround = false;
        next.y += step.y;
        if (step.y <= 0.0f && overlapsSolid(world, boxAt(next))) {
            // Landing reads the shape table, so a slab's top is halfway up its
            // cell rather than the cell boundary - the same rule that stopped
            // creatures hovering over slabs.
            const float surface = highestSurfaceBelow(world, boxAt(next), drop.position.y - kHalfSize);
            if (std::isfinite(surface)) {
                next.y = surface + kHalfSize;

                if (-drop.velocity.y > kSettleSpeed) {
                    drop.velocity.y = -drop.velocity.y * kRestitution;
                    drop.velocity.x *= kGroundFriction;
                    drop.velocity.z *= kGroundFriction;
                } else {
                    drop.velocity = glm::vec3{0.0f};
                    drop.onGround = true;
                }
            } else {
                next.y = drop.position.y;
                drop.velocity.y = 0.0f;
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
            ready.push_back({i, drop.item, drop.count, drop.damage});
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

engine::MeshData ItemEntities::buildMesh(const World& world, float timeSeconds,
                                         const SpriteMask& sprites, const DrawRange& range,
                                         engine::MeshData* blended) const {
    engine::MeshData mesh;

    for (const Drop& drop : m_drops) {
        if (drop.item == ItemId::None) {
            continue;
        }
        if (!range.contains(drop.position)) {
            continue;
        }
        // Anything without a cube to build is drawn from its sprite: every
        // non-block item, and every plant. Until spawn eggs arrived nothing
        // ever tested the first: a dropped pickaxe existed, fell, could be
        // picked up and rendered **nothing at all**, which is a bug you can
        // only see by throwing one on the floor.
        const bool blockLike = isBlockItem(drop.item);
        const BlockId block = blockLike ? blockForItem(drop.item) : BlockId::Air;
        const bool flat = !blockLike || usesFlatIcon(blockShape(block));
        const int spriteLayer =
            !flat ? -1
                  : (blockLike ? static_cast<int>(blockTextureLayer(block, BlockFace::Side))
                               : itemTextureLayer(drop.item));
        if (flat && spriteLayer < 0) {
            continue;
        }

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

        // A tool, a spawn egg or a flower is its sprite made solid, built by
        // `appendSpriteModel` - shared with thrown items, which have to look
        // like the same object in the air as they do on the floor.
        //
        // **A plant takes the same path deliberately.** In the ground it is two
        // crossed quads, which is the shape it grows in; lying on the floor it
        // is one flower with real thickness, exactly like everything else that
        // has been dropped.
        if (flat) {
            appendSpriteModel(mesh, sprites, spriteLayer, centre, right, up, forward, sky, blockLight);
            continue;
        }

        // Everything else is a **miniature of the block itself**, box for box.
        // It used to be a single cube wearing the block's side texture, so a
        // dropped bell was a gold brick, a dropped fence a plank and a dropped
        // anvil a black cube. The boxes come from the same owner the slot
        // picture reads, so what is on the floor and what is in the hotbar
        // cannot disagree.
        const auto corner = [&](float x, float y, float z) {
            return centre + right * (x * 2.0f - 1.0f) + up * (y * 2.0f - 1.0f) +
                   forward * (z * 2.0f - 1.0f);
        };
        const auto part = [&](const BlockBox& b, const ModelBox* model) {
            // **Each side quad has to be told which side it is.**
            // `blockTextureLayer` works out "is this the front" by comparing the
            // direction it is given against the block's own facing, so one layer
            // computed from `blockFacing` and handed to all four sides says
            // "front" four times - which put a sticky piston's plate right round
            // the block. The mesher and the inventory icon both already pass a
            // real direction per face; this was the third place a block is drawn
            // and the one that still did not.
            const auto wallLayer = [&](FaceDirection direction) {
                return model != nullptr && model->sideLayer >= 0.0f
                           ? model->sideLayer
                           : blockTextureLayer(block, BlockFace::Side, direction);
            };
            const float capLayer = model != nullptr && model->lidLayer >= 0.0f
                                       ? model->lidLayer
                                       : blockTextureLayer(block, BlockFace::Top);
            const glm::vec2 wall[4]{{model != nullptr ? model->uMin : 0.0f, model != nullptr ? model->vMax : 1.0f},
                                    {model != nullptr ? model->uMax : 1.0f, model != nullptr ? model->vMax : 1.0f},
                                    {model != nullptr ? model->uMax : 1.0f, model != nullptr ? model->vMin : 0.0f},
                                    {model != nullptr ? model->uMin : 0.0f, model != nullptr ? model->vMin : 0.0f}};
            const glm::vec2 cap[4]{
                {model != nullptr ? model->topUMin : 0.0f, model != nullptr ? model->topVMax : 1.0f},
                {model != nullptr ? model->topUMax : 1.0f, model != nullptr ? model->topVMax : 1.0f},
                {model != nullptr ? model->topUMax : 1.0f, model != nullptr ? model->topVMin : 0.0f},
                {model != nullptr ? model->topUMin : 0.0f, model != nullptr ? model->topVMin : 0.0f}};

            const auto face = [&](const glm::vec3& a, const glm::vec3& b2, const glm::vec3& d,
                                  const glm::vec3& e, const glm::vec2* rect, float layer, float shade) {
                // **A dropped pane of stained glass is the fourth place a block
                // is drawn**, and the one that would have been missed: its art
                // keeps a real alpha now, so left in the opaque mesh the cutout
                // test would throw away the 0.40 centre panel and drop a
                // wireframe frame on the floor.
                engine::MeshData& out =
                    (blended != nullptr && isBlendedGlass(block)) ? *blended : mesh;
                const auto base = static_cast<std::uint32_t>(out.vertices.size());
                const glm::vec3 corners[4]{a, b2, d, e};
                for (int i = 0; i < 4; ++i) {
                    out.vertices.push_back(
                        engine::Vertex{{corners[i].x, corners[i].y, corners[i].z},
                                       engine::packVertexColor(sky, blockLight, shade, 1.0f),
                                       {rect[i].x, rect[i].y},
                                       layer,
                                       engine::kVertexSurfaceDefault});
                }
                // Both windings: it spins, so either side can face the camera.
                out.indices.insert(out.indices.end(),
                                   {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                    base + 2, base + 1, base + 0, base + 3, base + 2, base + 0});
            };

            face(corner(b.minX, b.minY, b.minZ), corner(b.maxX, b.minY, b.minZ),
                 corner(b.maxX, b.maxY, b.minZ), corner(b.minX, b.maxY, b.minZ), wall,
                 wallLayer(FaceDirection::NegZ), 0.60f);
            face(corner(b.maxX, b.minY, b.maxZ), corner(b.minX, b.minY, b.maxZ),
                 corner(b.minX, b.maxY, b.maxZ), corner(b.maxX, b.maxY, b.maxZ), wall,
                 wallLayer(FaceDirection::PosZ), 0.86f);
            face(corner(b.maxX, b.minY, b.minZ), corner(b.maxX, b.minY, b.maxZ),
                 corner(b.maxX, b.maxY, b.maxZ), corner(b.maxX, b.maxY, b.minZ), wall,
                 wallLayer(FaceDirection::PosX), 0.72f);
            face(corner(b.minX, b.minY, b.maxZ), corner(b.minX, b.minY, b.minZ),
                 corner(b.minX, b.maxY, b.minZ), corner(b.minX, b.maxY, b.maxZ), wall,
                 wallLayer(FaceDirection::NegX), 0.72f);
            face(corner(b.minX, b.maxY, b.maxZ), corner(b.maxX, b.maxY, b.maxZ),
                 corner(b.maxX, b.maxY, b.minZ), corner(b.minX, b.maxY, b.minZ), cap, capLayer, 1.0f);
            face(corner(b.minX, b.minY, b.minZ), corner(b.maxX, b.minY, b.minZ),
                 corner(b.maxX, b.minY, b.maxZ), corner(b.minX, b.minY, b.maxZ), cap,
                 model != nullptr ? capLayer : blockTextureLayer(block, BlockFace::Bottom), 0.45f);
        };

        if (usesModelIcon(blockShape(block))) {
            const ModelBoxes model = postModel(block);
            for (int i = 0; i < model.count; ++i) {
                part(model.boxes[i].box, &model.boxes[i]);
            }
        } else {
            const BlockBoxes parts = iconBoxes(block);
            for (int i = 0; i < parts.count; ++i) {
                part(parts.boxes[i], nullptr);
            }
        }
    }

    return mesh;
}

} // namespace game
