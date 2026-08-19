#pragma once

#include "item/Item.hpp"
#include "item/SpriteMask.hpp"
#include "world/DrawRange.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>
#include <cstddef>
#include <vector>

namespace game {

class World;

/// Long enough that breaking a block does not immediately collect what it
/// dropped, short enough that it does not read as a delay. Named because
/// `spawn` and `dropStack` both default to it and a second copy of the number
/// is a second thing to get wrong.
constexpr float kDropPickupDelay = 0.35f;

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
    ///
    /// **Prefer `dropStack` below wherever the thing being dropped is an
    /// `ItemStack`.** `damage` being last and defaulted is exactly why eight of
    /// this function's nine stack-carrying callers silently dropped it.
    void spawn(const glm::vec3& position, ItemId item, int count, const glm::vec3& impulse = glm::vec3{0.0f},
               float pickupDelay = kDropPickupDelay, int damage = 0);

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

    /// What persistence needs of a drop, and **the stack arrives assembled**.
    ///
    /// `Drop` keeps the stack loose - `item`, `count` and `damage` as three
    /// fields - because that is what `spawn` is handed and what `collectable`
    /// gives back. Persistence wants the opposite: one `ItemStack`, composed
    /// once, here. Handing the three parts out instead would put the assembly in
    /// the caller, and getting it wrong there is silent in the worst way - a
    /// record with the damage in the count compiles, writes, reads back, and
    /// hands the player a stack of 47 diamond pickaxes. **There is no argument
    /// left to forget**, which is the same reasoning `dropStack` below was
    /// written on, after eight of nine callers forgot `damage`.
    ///
    /// `onGround` stays a `bool` because that is what it is in memory. The save
    /// record spells it `std::int32_t` for an unrelated reason that belongs to
    /// that file - a `bool` in a record opens three bytes of padding that reach
    /// the disk uninitialised - and the one-line conversion is the caller's.
    struct Persisted {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        ItemStack stack;
        float age = 0.0f;
        float pickupDelay = 0.0f;
        bool onGround = false;
    };

    /// **ADDING A FIELD HERE BREAKS TWO THINGS IN FILES THAT CANNOT SEE YOU.**
    /// Written 2026-08-19. A constraint, not a status, so it stays true.
    ///
    /// This struct is field-parallel with `SavedItem` in `WorldStore.hpp`, and
    /// the conversion between them lives in `Main.cpp`'s save and load paths.
    /// Neither of those files is watching this one.
    ///
    /// **So a new member here needs all three of**: the matching member on
    /// `SavedItem`, appended at its tail for the same aggregate-init reason
    /// spelled out above `SavedPlayer`; a new value in that file's
    /// `static_assert(sizeof(SavedItem) == 48)`, which exists to catch exactly
    /// this and will fail loudly, so let it; and a bump of `kDropVersion` in
    /// `WorldStore.cpp`, because a `drops.dat` written by the old layout is
    /// otherwise read back into the new one field-by-field and silently
    /// misaligned. **The size assert is the only one of the three that tells
    /// you** - the other two are silent, which is why they are written here.
    ///
    /// **Do not "simplify" this by giving `Persisted` an `ItemStack` twin on
    /// the save side and copying member-for-member.** The stack is composed
    /// once, here, for the reason given above: a record with the damage in the
    /// count compiles, writes, reads back and hands the player 47 pickaxes.
    ///
    /// **Falsified by**: `SavedItem` no longer having one member per member of
    /// this struct, or its size assert no longer reading 48. Either means the
    /// two have already drifted and this note is describing a repair, not a
    /// rule.
    ///
    /// **And because a comment is the weakest of the three instruments, the
    /// silent half of that is now a build failure.** Added 2026-08-19. The
    /// assert below pins the offset of the last member, so ADDING a field here
    /// stops the build and sends you to this paragraph, rather than compiling
    /// cleanly and quietly not persisting. It cannot check `SavedItem` - that
    /// type is not visible from this header and deliberately so - but it can
    /// guarantee nobody reaches the other file unaware. Offset rather than
    /// `sizeof` because the trailing `bool` pads, and the right-hand side is
    /// derived from the member types rather than written as a number, so it
    /// survives any change in `glm::vec3`'s alignment.
    static_assert(offsetof(Persisted, onGround) ==
                      2 * sizeof(glm::vec3) + sizeof(ItemStack) + 2 * sizeof(float),
                  "ItemEntities::Persisted gained, lost or reordered a member. Its disk twin "
                  "WorldStore::SavedItem needs the same change, its size assert needs a new "
                  "value, and kDropVersion needs a bump - none of which the compiler can see");

    /// Every drop on the floor, for writing out. Ordinary and cheap: this runs
    /// on the autosave timer and at quit, never per frame.
    std::vector<Persisted> persisted() const;

    /// Put one back exactly as it was, **and deliberately not through `spawn`**.
    ///
    /// `spawn` is the *gameplay* entry: it applies an impulse so the drop pops
    /// out of the block, starts `age` at zero and takes the stack in three
    /// loose parts. Every one of those is wrong here. A restored item has
    /// already fallen, is already partway to despawning, and must come back
    /// where it was rather than somewhere near it - restoring through `spawn`
    /// would scatter the floor a little further from the truth on every load and
    /// reset the despawn clock, so nothing dropped would ever expire across a
    /// save.
    ///
    /// Loose parameters rather than a `Persisted`, matching
    /// `Creatures::restore`, which is this codebase's existing answer to exactly
    /// this question. **The stack stays whole** for the reason given above.
    ///
    /// Trusts what it is given. The store sanitises on the way out - stacks,
    /// finiteness, position plausibility, the sign of both timers - so a second
    /// set of rules here would be a second owner for a settled question.
    void restore(const glm::vec3& position, const glm::vec3& velocity, const ItemStack& stack,
                 float age, float pickupDelay, bool onGround);

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

/// Throws a whole stack on the floor - **damage and all**.
///
/// This exists because `spawn` takes `damage` last and defaults it to zero, and
/// eight of its nine stack-carrying callers forgot to pass it. `damage` is a
/// tool's wear *and* it is which stored contents a stowbox holds, so a forgotten
/// argument handed every tool back fully repaired and returned every stowbox
/// empty, with its contents stranded in `stowboxes.dat` behind a handle nothing
/// could ever reach again. Dying is routine, so that made durability optional
/// and storage lossy at the same time.
///
/// Taking the stack itself is the fix: there is no argument left to forget.
/// **Every path that parts a player from an `ItemStack` goes through here.**
void dropStack(ItemEntities& drops, const glm::vec3& position, const ItemStack& stack,
               const glm::vec3& impulse = glm::vec3{0.0f},
               float pickupDelay = kDropPickupDelay);

/// The same, for the partial case: throwing one of a held stack, or the part of
/// it that would not fit back into the inventory. `count` replaces the stack's
/// own and everything else - `damage` included - rides along untouched.
void dropStack(ItemEntities& drops, const glm::vec3& position, const ItemStack& stack, int count,
               const glm::vec3& impulse = glm::vec3{0.0f},
               float pickupDelay = kDropPickupDelay);

} // namespace game
