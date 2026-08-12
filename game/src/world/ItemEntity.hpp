#pragma once

#include "item/Item.hpp"
#include "item/SpriteMask.hpp"
#include "world/DrawRange.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <vector>

namespace game {

class World;

/// Items lying in the world after being broken or thrown.
///
/// The first thing here that is neither a block nor the player, and deliberately
/// the smallest version of that: a position, a velocity and a stack. The general
/// entity system is M20, and building it now would be guessing at what creatures
/// need before any exist.
class ItemEntities {
public:
    /// Spawned with a small upward push so it visibly pops out of the block.
    ///
    /// `impulse` throws it somewhere: without one a dropped stack lands at your
    /// feet and is collected again immediately. `pickupDelay` has to cover the
    /// flight, or a thrown item is pulled straight back before it gets away.
    /// `damage` rides along untouched. It is a tool's wear for most items, and
    /// **which stored contents a stowbox is** for those - either way, dropping
    /// something and picking it up again must not change it.
    void spawn(const glm::vec3& position, ItemId item, int count, const glm::vec3& impulse = glm::vec3{0.0f},
               float pickupDelay = 0.35f, int damage = 0);

    /// Falls, settles on the ground, and drifts toward a nearby player.
    void update(const World& world, const glm::vec3& playerFeet, float deltaSeconds);

    /// Stacks close enough to collect, cleared out as they are taken. The caller
    /// decides whether there is room, so nothing vanishes into a full inventory.
    struct Collectable {
        std::size_t index;
        ItemId item;
        int count;
        int damage;
    };
    std::vector<Collectable> collectable(const glm::vec3& playerFeet) const;
    void remove(std::size_t index);
    void reduce(std::size_t index, int taken);

    /// Rebuilt every frame rather than transformed, because world meshes are
    /// drawn with an identity model matrix - the shader recovers its normals
    /// from screen-space derivatives of world position, which only holds while
    /// vertex positions *are* world positions.
    ///
    /// `sprites` is the silhouette of every item sprite, which is what turns a
    /// dropped tool from two flat faces into a solid shape.
    ///
    /// `range` drops the ones too far away to read as anything. They keep
    /// falling, keep drifting toward the player and keep expiring; only their
    /// geometry is skipped.
    ///
    /// `blended` takes the drops whose art is see-through everywhere - the
    /// stained and tinted glass - because the opaque pass throws away any texel
    /// under half alpha and theirs is a frame at 0.64 around a panel at 0.40.
    /// Null simply leaves them in the return value, which is what every caller
    /// that has no blended mesh to give wants.
    engine::MeshData buildMesh(const World& world, float timeSeconds, const SpriteMask& sprites,
                               const DrawRange& range = {},
                               engine::MeshData* blended = nullptr) const;

    std::size_t count() const { return m_drops.size(); }

private:
    struct Drop {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        ItemId item = ItemId::None;
        int count = 0;
        int damage = 0;
        float age = 0.0f;
        float pickupDelay = 0.0f;
        bool onGround = false;
    };

    std::vector<Drop> m_drops;
};

} // namespace game
