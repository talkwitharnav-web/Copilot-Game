#include <engine/core/FrameLimiter.hpp>
#include <engine/core/JobSystem.hpp>
#include <engine/core/Log.hpp>
#include <engine/core/Paths.hpp>
#include <engine/platform/Window.hpp>
#include <engine/render/Camera.hpp>
#include <engine/render/Renderer.hpp>
#include <engine/render/VulkanContext.hpp>

#include "core/Gamepad.hpp"
#include "core/Settings.hpp"
#include "hud/Crosshair.hpp"
#include "hud/DebugOverlay.hpp"
#include "hud/Hotbar.hpp"
#include "hud/StatusBars.hpp"
#include "core/Sounds.hpp"

#include <engine/audio/AudioEngine.hpp>
#include "hud/HudPrimitives.hpp"
#include "hud/InventoryScreen.hpp"
#include "hud/LoadingScreen.hpp"
#include "item/BlockDrops.hpp"
#include "item/Inventory.hpp"
#include "item/Mining.hpp"
#include "item/Recipe.hpp"
#include "item/SlotOps.hpp"
#include "item/Smelting.hpp"
#include "item/Tool.hpp"
#include "world/Furnace.hpp"
#include "world/ItemEntity.hpp"
#include "world/Projectile.hpp"
#include "world/Biome.hpp"
#include "world/BlockOutline.hpp"
#include "world/Campfire.hpp"
#include "world/Chunk.hpp"
#include "world/Collision.hpp"
#include "world/Copper.hpp"
#include "world/Creature.hpp"
#include "world/Explosion.hpp"
#include "world/Farming.hpp"

#include <map>
#include "world/FallingBlock.hpp"
#include "world/Loot.hpp"
#include "world/Material.hpp"
#include "world/Player.hpp"
#include "world/Raycast.hpp"
#include "world/Sky.hpp"
#include "world/TerrainGenerator.hpp"
#include "world/Tick.hpp"
#include "world/Particles.hpp"
#include "world/Village.hpp"
#include "world/Weather.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kWindowWidth = 1280;
constexpr std::uint32_t kWindowHeight = 720;
constexpr float kLookRadiansPerPixel = 0.0025f;

// How far the player can reach, in metres, measured from the eye.
//
// The reference's own keyboard-and-mouse numbers: five blocks to a *block* in
// every mode, and three to a *creature* - except creative, which gets five for
// both. Its touch controls use six and twelve, and twelve is what this was,
// which made the crosshair reach absurdly far for a mouse.
constexpr float kBlockReach = 5.0f;
constexpr float kEntityReach = 3.0f;
constexpr float kCreativeEntityReach = 5.0f;

// Holding the button keeps editing at this rate, so dragging across terrain
// does not need one click per block.
constexpr float kPlaceRepeatSeconds = 0.18f;

// A charge's blast strength. The same scale the creeper's uses, so the two go
// through one explosion path.
constexpr float kTntPower = 4.0f;

/// Blocks per second of shove at full impact.
///
/// The reference's knockback is one block per **tick** at point blank, which is
/// twenty a second - and that is what this was. It reads as a rocket rather
/// than a blast, because the reference's velocity decays by 9% every tick and
/// ours does not: a player thrown off the ground keeps whatever horizontal
/// speed the blast gave until they land. This is the sustained-speed equivalent
/// of that impulse, tuned to just under a jump's worth of lift at point blank.
///
/// **Read from `Explosion.hpp` rather than repeated here, and that is the whole
/// fix.** This was `7.0f` written out, and `Creature.cpp`'s `kBlastThrow` is
/// the same fact for creatures; the pair had already drifted once, to a 2.9x
/// difference between how far one blast threw the player and how far it threw
/// a pig standing beside them. `Creature.cpp` now reads the shared constant, so
/// leaving this one a literal would be bug shape #5 - a derivation applied to
/// one of a pair and not the other - and would let the identical drift come
/// back from the other side. Rung 1 of the ladder: derive, so the coupling
/// cannot break rather than being asserted or commented into holding. No new
/// include is needed; this file already calls `game::blast::explosionDropChance`.
constexpr float kBlastKnockback = game::blast::kKnockbackSpeed;

/// A pearl may be thrown once a second, the reference's own cooldown.
constexpr float kPearlCooldownSeconds = 1.0f;

/// How far below the eye anything the player launches actually leaves from.
///
/// The reference's own figure: a shot starts at `eyeY - 0.1`, and spawning at
/// the eye itself puts an arrow or a thrown potion through your own head at
/// point-blank range - `segmentEntersBox` reports a box *entered*, which saves
/// you on tick zero but not on the tick after it.
///
/// **One owner, because the throw site already claimed to share it.** The bow
/// and the egg/pearl/potion branch each carried their own
/// `- glm::vec3{0.0f, 0.1f, 0.0f}`, while the second one's comment said "Same
/// anchor as the bow" - a claim of sharing that the code did not implement, so
/// retuning one would have silently left the other behind. This is the
/// three-near-identical-rows question with the answer "they differ by nothing".
constexpr float kLaunchDropBelowEye = 0.1f;

/// How far above the impact the teleport will search for room to stand, in
/// whole blocks. Two is enough to clear a slab or a stair underfoot; more than
/// that and you are being moved somewhere you did not aim.
constexpr int kPearlLandingLift = 3;

// Rate a held button lands blows on a creature, distinct from digging: a swing
// connects once and then has to be wound up again.
constexpr float kSwingSeconds = 0.45f;
// The reference's own gap between one block coming apart and the next one
// starting: **six ticks**, and it is skipped entirely when the break took a
// single tick or less. Ours was 0.22 s and, worse, was applied to exactly the
// breaks the reference exempts.
constexpr float kBreakRepeatSeconds = 6.0f * game::tick::kSeconds;
// What an instant break still costs. The reference tests the dig **once per
// tick**, so even an exempt break can only happen twenty times a second; our
// loop runs per frame, and without a floor here a creative click strips the
// whole reach in a line - which is the shape this constant was added for.
constexpr float kInstantBreakSeconds = game::tick::kSeconds;

// Both gaps above are whole ticks, and this says so in the *other* spelling of
// the rate - which is the only thing that would notice one of them being
// written back out as a bare `1.0f / 20.0f`, as both were.
//
// > Fails if: either constant stops deriving from `game::tick`, or the tick
// > rate is changed - and the tick rate is never the fix for smoothness.
static_assert(kInstantBreakSeconds * game::tick::kPerSecond == 1.0f,
              "the instant-break floor is exactly one tick");

// Field of view in degrees, vertical. 70 matches the genre default; the range
// is wide enough to be useful without the edge distortion that makes very high
// values unplayable. Keyboard-adjustable until there is a settings screen.
constexpr float kDefaultFov = 70.0f;
constexpr float kFovStep = 5.0f;
constexpr float kMinFov = 50.0f;
constexpr float kMaxFov = 110.0f;

// Two Space presses closer together than this toggle flight. Long enough to be
// comfortable, short enough that ordinary repeated jumping does not trigger it.
constexpr float kDoubleTapSeconds = 0.3f;

// Frames shown in the diagnostics graph. At 120 fps this is about a second and a
// half of history, which is long enough to catch a stutter and short enough that
// the graph still reacts.
constexpr std::size_t kFrameHistoryLength = 160;

// The overlay changes every frame, but rebuilding its mesh that often would
// churn GPU buffers for no visible benefit.
constexpr auto kOverlayRefreshInterval = std::chrono::milliseconds(50);

// Time allowed per frame for generating and meshing chunks. Anything left over
// waits for the next frame, so a burst of new terrain slows the horizon down
// instead of freezing the game.
constexpr float kStreamingBudgetSeconds = 0.003f;

/// Streaming budget while the loading screen is up. Larger than the in-game one
/// because nothing else is competing for the frame, but still a budget: the
/// point is that the window keeps drawing instead of queuing every chunk at once.
constexpr float kLoadingBudgetSeconds = 0.016f;

/// How fast the bar catches up to the reported progress, per second. Chunk
/// counts arrive in coarse steps, and easing is what turns them into movement.
constexpr float kLoadingBarEase = 6.0f;

/// How long the loading screen tolerates every queue being empty while its
/// checkpoints still disagree. Only a fault can produce that, so this is a
/// bail-out rather than a timeout.
constexpr float kLoadingStallSeconds = 3.0f;

/// How hard a dropped item is thrown, and how long before it can be collected.
/// The delay has to outlast the flight, or the item is pulled straight back
/// before it clears the pickup radius.
constexpr float kThrowSpeed = 6.0f;
constexpr float kThrowPickupDelay = 1.2f;

/// The largest coordinate a saved position may name, in blocks.
///
/// **A float precision limit rather than a world limit**, and derived rather
/// than picked: a `float` carries 24 mantissa bits, so 2^21 is the last
/// magnitude at which it still resolves a quarter of a block. Past it,
/// movement snaps in visible steps; well past it, `static_cast<int>` of the
/// value no longer means what the collision and chunk code read it as. A NaN
/// is worse again - casting one to `int` is undefined behaviour and on x86
/// yields `INT_MIN` straight into chunk arithmetic.
constexpr float kMaxSavedCoord = 2097152.0f; // 2^21 blocks.

/// A full turn in radians, for wrapping a saved yaw back into one.
constexpr float kTwoPi = 6.2831853f;

/// **What a blow lands for, before Strength and Weakness have their say.**
///
/// Bedrock's table (`RESEARCH.md` section 2.2, https://minecraft.wiki/w/Damage),
/// written as its shape rather than as five rows of numbers: every row steps by
/// one per tier and a sword is the best weapon at every tier, so the kind
/// contributes a constant and the tier contributes itself. Wood is tier 1 and
/// emberite is tier 5, which is why a wooden sword comes out at 5 and an
/// emberite one at 9.
///
/// It replaces `kind == Sword ? (tier >= kStoneTier ? 5 : 4) : 1 + tier`, which
/// flattened every sword from stone upwards to a single 5 and **made an
/// emberite shovel the strongest weapon in the game**.
constexpr int weaponDamage(game::ToolKind kind, int tier) {
    switch (kind) {
    case game::ToolKind::Sword:
        return 4 + tier;
    case game::ToolKind::Axe:
        return 3 + tier;
    // Bedrock's hoe matches its pickaxe. `[JE]` a hoe always deals 1 and an axe
    // out-damages a sword; we follow Bedrock, so swords win and the tiering is
    // clean.
    case game::ToolKind::Pickaxe:
    case game::ToolKind::Hoe:
        return 2 + tier;
    case game::ToolKind::Shovel:
        return 1 + tier;
    // A bare fist, and anything that is not a tool at all. **Named rather than
    // left to a `default:`**, so the next kind added is a compiler error here
    // instead of silently dealing one.
    case game::ToolKind::None:
    case game::ToolKind::Shears:
        break;
    }
    return 1;
}

static_assert(weaponDamage(game::ToolKind::Sword, game::kWoodTier) == 5 &&
                  weaponDamage(game::ToolKind::Sword, game::kStoneTier) == 6 &&
                  weaponDamage(game::ToolKind::Sword, game::kIronTier) == 7 &&
                  weaponDamage(game::ToolKind::Sword, game::kDiamondTier) == 8 &&
                  weaponDamage(game::ToolKind::Sword, game::kEmberiteTier) == 9,
              "Bedrock's sword row, verbatim. Change 4 to anything else and this fails.");
static_assert(weaponDamage(game::ToolKind::Axe, game::kWoodTier) == 4 &&
                  weaponDamage(game::ToolKind::Pickaxe, game::kWoodTier) == 3 &&
                  weaponDamage(game::ToolKind::Hoe, game::kWoodTier) == 3 &&
                  weaponDamage(game::ToolKind::Shovel, game::kWoodTier) == 2 &&
                  weaponDamage(game::ToolKind::Axe, game::kEmberiteTier) == 8 &&
                  weaponDamage(game::ToolKind::Pickaxe, game::kEmberiteTier) == 7 &&
                  weaponDamage(game::ToolKind::Hoe, game::kEmberiteTier) == 7 &&
                  weaponDamage(game::ToolKind::Shovel, game::kEmberiteTier) == 6,
              "Bedrock's other four rows, both ends. RESEARCH.md 2.2 is the table; the single edit "
              "that breaks this is changing a kind's constant to make one tool feel better.");

/// Whether a sword still beats every other tool **at its own tier**, which is
/// the property the expression this replaced actually broke. Written as the
/// whole comparison a player makes rather than as one hand-picked pair, because
/// an assert that names two tiers proves nothing about the other four.
constexpr bool swordLeadsAtEveryTier() {
    for (int tier = game::kWoodTier; tier <= game::kEmberiteTier; ++tier) {
        const int sword = weaponDamage(game::ToolKind::Sword, tier);
        if (sword <= weaponDamage(game::ToolKind::Axe, tier) ||
            sword <= weaponDamage(game::ToolKind::Pickaxe, tier) ||
            sword <= weaponDamage(game::ToolKind::Hoe, tier) ||
            sword <= weaponDamage(game::ToolKind::Shovel, tier)) {
            return false;
        }
    }
    return true;
}

static_assert(swordLeadsAtEveryTier(),
              "a sword out-damages every other tool of its own tier, which is Bedrock's shape and "
              "the reason we follow it. The single edit that breaks this is flattening the sword "
              "row - `tier >= kStoneTier ? 5 : 4` is exactly what shipped, and it let an emberite "
              "shovel out-hit a diamond sword.");
static_assert(weaponDamage(game::ToolKind::None, game::kHandTier) == 1,
              "a bare fist deals 1, and so does anything that is not a tool");

/// The hardest a blow of ours can land without effects, which is what the
/// rumble strength is scaled against. Read off the table rather than restated,
/// or a new tier silently pins the controller at maximum.
constexpr int kStrongestBlow = weaponDamage(game::ToolKind::Sword, game::kEmberiteTier);

/// Slower than breaking or placing: emptying a stack by accident is more
/// annoying than having to hold the key a moment longer.
constexpr float kDropRepeatSeconds = 0.22f;

/// **Whether merging two slabs of `family` gives back what it swallowed.**
///
/// Two halves meeting in one cell become the parent block, because there is no
/// double-slab id to hold the pair. minecraft.wiki *Slab*: "Double slabs are
/// handled as a single block instead of two different slabs; as such, breaking
/// one destroys the whole block and drops two slabs." For fifty-four of the
/// fifty-five families the parent hands its own item back, so the merge is
/// reversible in a crafting grid and costs nothing. **Stone is the fifty-fifth
/// and it is a real loss**: `StoneSlab`'s parent is `Stone`, and `Stone` drops
/// `Cobblestone` even under Silk Touch, so two stone slabs merged and then
/// mined came back as one cobblestone with no way to get the slabs again.
///
/// So the merge asks first, and a family that would eat the pair simply does
/// not merge - the second slab stands in its own cell like any other block,
/// which is worse to build with and takes nothing off the player. **The right
/// fix is a double-slab id per family and it is not in this file**; this is the
/// guard that stops the loss until `Block.hpp` grows one.
///
/// `primaryDrop` rather than the whole table because a slab parent is a plain
/// one-row block - none of the fifty-five rolls, and none pays two of itself.
constexpr bool slabMergeReturnsItsMaterial(int family) {
    const game::BlockId parent =
        game::kSlabFamilies[static_cast<std::size_t>(family)].parent;
    return game::primaryDrop(parent) == game::itemForBlock(parent);
}

/// Counted rather than stated, so the assert below measures the table instead
/// of repeating a number somebody typed.
constexpr int mergeableSlabFamilies() {
    int total = 0;
    for (int family = 0; family < game::kSlabFamilyCount; ++family) {
        total += slabMergeReturnsItsMaterial(family) ? 1 : 0;
    }
    return total;
}

// **The single edit that makes this fail:** giving one more slab family a
// parent whose drop row is not itself - a deepslate or a blackstone variant
// would do it - or changing an existing parent's row. Either way the merge
// starts destroying slabs again, and the build says so rather than a
// playtester finding it four milestones later.
static_assert(mergeableSlabFamilies() == game::kSlabFamilyCount - 1,
              "exactly one slab family (Stone, whose parent drops Cobblestone) must be excluded "
              "from the merge - a second one means another pair of slabs is being eaten");
static_assert(!slabMergeReturnsItsMaterial(game::slabFamily(game::BlockId::StoneSlab)),
              "and it must be Stone that is excluded, not some other family that has drifted");

/// **How long a button stays down.** Bedrock holds a wooden button for 30 ticks
/// and a stone one for 20 (https://minecraft.wiki/w/Button). Which is which is
/// read off the family table's own parent rather than off the index, so a
/// thirteenth family cannot quietly inherit the wrong number by being added in
/// the wrong place.
///
/// **`isPlanksBlock` and not `isFlammable`**, which was the first thing written
/// here and was wrong: crimson and warped planks do not burn, so two perfectly
/// wooden buttons would have taken the stone timing. The assert below is what
/// said so, before anything was played.
constexpr float buttonHeldSeconds(int family) {
    const game::BlockId parent =
        game::kButtonFamilies[static_cast<std::size_t>(family)].parent;
    return static_cast<float>(game::isPlanksBlock(parent) ? 30 : 20) * game::tick::kSeconds;
}

constexpr int woodenButtonFamilies() {
    int total = 0;
    for (int family = 0; family < game::kButtonFamilyCount; ++family) {
        total +=
            game::isPlanksBlock(game::kButtonFamilies[static_cast<std::size_t>(family)].parent)
                ? 1
                : 0;
    }
    return total;
}

// **The single edit that makes this fail:** adding a button family cut from
// something that is not planks - a polished blackstone button, which the
// reference does have - which would take the stone timing without anyone
// deciding it should, or moving the stone family out of last place.
static_assert(woodenButtonFamilies() == game::kButtonFamilyCount - 1,
              "eleven wooden button families and one stone one - a second non-wooden family "
              "means the 20/30 tick split has stopped being derivable from the table");

/// A compass direction as one step, so the four-way walks below share one
/// spelling of it instead of each writing the ternary chain out again.
inline glm::ivec3 stepAlong(game::FaceDirection direction) {
    switch (direction) {
    case game::FaceDirection::PosX:
        return glm::ivec3{1, 0, 0};
    case game::FaceDirection::NegX:
        return glm::ivec3{-1, 0, 0};
    case game::FaceDirection::PosZ:
        return glm::ivec3{0, 0, 1};
    case game::FaceDirection::NegZ:
        return glm::ivec3{0, 0, -1};
    default:
        return glm::ivec3{0, 0, 0};
    }
}

/// **The wall a block is held up by, or `Unknown` when nothing holds it
/// sideways.**
///
/// A direction pointing *from* the block *at* the cell that supports it, so one
/// caller can ask "did the cell that just went hold this up" and another can
/// ask "is there anything here to hang this on" off the same answer.
///
/// `settleAround` had this rule for **ladders alone**, and it was right there.
/// Five other families hang off the side of a block and none of them was asked,
/// so mining a wall left every wall sign, wall banner, wall torch, lever,
/// button and cocoa pod floating in mid-air paying no drop
/// (https://minecraft.wiki/w/Sign, https://minecraft.wiki/w/Banner: a sign or
/// banner "breaks and drops itself as an item if the block it is attached to is
/// moved, removed or destroyed"). The other half is the same missing answer
/// read backwards and is worse: `needsSupportBelow` is true for every torch id
/// including the eight wall ones, the only support test was the floor, so a
/// redstone torch could not be put on a wall over open air at all and was
/// deleted the moment the ground beneath it was mined - against
/// https://minecraft.wiki/w/Redstone_Torch, where what a torch needs is its
/// **attachment** block.
///
/// **Every direction is read from the accessor that owns it**, and where the
/// stored value is the way the block *looks* rather than the way it leans - a
/// sign faces out of its wall - the flip is taken here, once, rather than at
/// each call site.
///
/// > Three families are deliberately absent and are reported rather than
/// > guessed: a hanging sign holds on above rather than sideways, a vine can
/// > cling to several sides at once and would need its remaining sides rewritten
/// > rather than simply dropping, and the tripwire hook and lightning rod store
/// > a direction whose meaning `Block.hpp`'s own comment and this file's
/// > placement branch disagree about.
constexpr game::FaceDirection wallBehind(game::BlockId id) {
    if (game::isLadder(id)) {
        return game::ladderFacing(id);
    }
    if (game::isTorchBlock(id)) {
        // `isWallTorch` is this test; the direction comes back from the same
        // accessor, so there is no second decode to drift.
        return game::redstoneTorchWall(id);
    }
    if (game::isLever(id)) {
        const int mount = game::leverMount(id);
        return mount >= game::LeverWallFirst
                   ? static_cast<game::FaceDirection>(mount - game::LeverWallFirst)
                   : game::FaceDirection::Unknown;
    }
    if (game::isButton(id)) {
        // Floor is 0 and ceiling is 1; the four walls follow in `FaceDirection`
        // order, which is what `buttonMount`'s own comment says.
        const int mount = game::buttonMount(id);
        return mount >= 2 ? static_cast<game::FaceDirection>(mount - 2)
                          : game::FaceDirection::Unknown;
    }
    if (game::isCocoa(id)) {
        return game::cocoaFacing(id);
    }
    if (game::isSignLike(id) && game::signOnWall(id)) {
        return game::oppositeDirection(game::signFacing(id));
    }
    return game::FaceDirection::Unknown;
}

// **The single edit that makes these fail:** changing what any one of those
// accessors stores without changing the placement branch that writes it - which
// is exactly how a facing gets inverted and stays inverted for four milestones.
// Each pair below is written the way placement writes it and read the way
// `settleAround` reads it, so the two ends are asserted against each other
// rather than against themselves.
static_assert(wallBehind(game::ladderFacing(game::FaceDirection::PosX)) ==
                  game::FaceDirection::PosX,
              "a ladder's stored facing is the wall it hangs on");
static_assert(wallBehind(game::redstoneTorchAt(game::FaceDirection::NegZ, true)) ==
                      game::FaceDirection::NegZ &&
                  wallBehind(game::redstoneTorchAt(game::FaceDirection::Unknown, true)) ==
                      game::FaceDirection::Unknown,
              "a wall torch leans on its wall and a floor torch leans on nothing");
static_assert(wallBehind(game::leverAt(
                  game::LeverWallFirst + static_cast<int>(game::FaceDirection::PosZ), false)) ==
                      game::FaceDirection::PosZ &&
                  wallBehind(game::leverAt(game::LeverFloorX, false)) ==
                      game::FaceDirection::Unknown,
              "a wall lever leans on its wall and a floor lever does not");
static_assert(wallBehind(game::buttonAt(0, 2 + static_cast<int>(game::FaceDirection::NegX),
                                        false)) == game::FaceDirection::NegX &&
                  wallBehind(game::buttonAt(0, 0, false)) == game::FaceDirection::Unknown,
              "and the same for a button, whose mount counts from floor and ceiling");
static_assert(wallBehind(game::signAt(0, 0, game::FaceDirection::PosX, true)) ==
                      game::FaceDirection::NegX &&
                  wallBehind(game::signAt(0, 0, game::FaceDirection::PosX, false)) ==
                      game::FaceDirection::Unknown,
              "a wall sign looks away from what holds it up, so the wall is the opposite of "
              "its facing - and a standing sign has no wall at all");

/// The four two-block flowers are stored bottom then top, so a plant's halves
/// are one id apart. Placement writes `lower + 1` and `clearPairedHalf` reads
/// `removed - 1`; **inserting any id between a pair, or reordering the eight so
/// a top comes first, makes both write the wrong cell's block** and there is no
/// type change to catch it.
static_assert(!game::isTallFlowerUpper(game::BlockId::SunflowerLower) &&
                  game::isTallFlowerUpper(static_cast<game::BlockId>(
                      static_cast<int>(game::BlockId::SunflowerLower) + 1)) &&
                  game::isTallFlower(game::BlockId::PeonyUpper) &&
                  game::isTallFlowerUpper(game::BlockId::PeonyUpper),
              "tall flowers pair as bottom-then-top, one id apart, and the run ends on a top "
              "half - placement and the paired break both derive the twin from that");

/// **A `FallingBlocks::Crushed` names one of two very different debts, and this
/// sweep is what makes telling them apart a classification rather than a guess.**
///
/// `applyFallingLanding` reports either the *occupant* a landing displaced, or
/// the *faller itself* when it broke on something it may not replace - the torch
/// trick. Those owe different items. The occupant owes the **mining table**: a
/// sand column falling on tall grass drops what breaking that grass drops, seed
/// roll and all. The faller owes **itself**, because nothing was swung at it -
/// and running it through `resolveBreak` instead paid flint for gravel 9,942
/// times in 100,000 breaks, measured. That is a conserved *count* carrying the
/// wrong *item*, which is worse than a missing one because nothing looks wrong.
///
/// The drain tells the two arms apart with `isFalling(hit.block)`, and that is
/// only sound because the occupant arm is gated on `isReplaceable(occupying)`.
/// This proves the two sets never meet across every id rather than the three a
/// reader would think of, so the day a replaceable block is given gravity the
/// **build** breaks instead of the item quietly changing. It also proves every
/// faller has an item to become: `itemForBlock` has a `None` answer, and paying
/// that would delete the block outright rather than mis-name it.
constexpr int kFallerSweepStride = 512;

/// One owner for the count, for the reason `Block.hpp`'s name sweep records:
/// the passes are generated from this and the coverage assert multiplied the
/// same constant, so raising it could not quieten the alarm without also
/// sweeping the ids it just admitted.
///
/// **Derived rather than written, which is the tenth of eleven sweep counts in
/// this tree to make the move.** It was `7`, with a `static_assert` below
/// checking `7 * 512 >= kBlockIdCount` - a ceiling of 3584 against an id count
/// that moved 3285 -> 3309 -> 3314 -> 3315 -> 3328 inside a single day. That
/// assert was loud, which is better than silent, but its message told you to
/// raise a number that should never have been a number: the stride *is* the
/// count, so a gap is not expressible and there is nothing left to maintain.
/// `game::` stays on the qualification because this file is outside that
/// namespace.
///
/// **RETIRED FIGURES: the `7`, the `7 * 512` and the 3584 above are the state
/// this constant replaced, not anything live in this file.** Nothing here
/// fires when the id count grows - the expression simply yields one more pass.
///
/// Spelled out because 3584 is a collision rather than a shared ceiling.
/// `Mining.hpp` carries a *live* limit at the same figure by different
/// arithmetic: `kMiningChunkCapacity` is 14 against a 256-wide chunk, where
/// this was 7 against 512, and 256 * 14 and 512 * 7 are both 3584 by
/// coincidence. So a `3584` search returns two sites, one retired and one
/// current, and the identical number invites reading them as one fact and
/// "fixing" whichever is met first. Finding 161 routes that one; it is
/// deliberate there, with spare capacity that self-activates as the count
/// crosses 3328, and it is not this constant's problem to solve.
constexpr int kFallerSweepPasses =
    (static_cast<int>(game::kBlockIdCount) + kFallerSweepStride - 1) / kFallerSweepStride;

constexpr bool fallersAreNeverOccupants(int stride) {
    const int first = stride * kFallerSweepStride;
    const int end = first + kFallerSweepStride;
    for (int id = first; id < end && id < static_cast<int>(game::kBlockIdCount); ++id) {
        const game::BlockId block = static_cast<game::BlockId>(id);
        if (!game::isFalling(block)) {
            continue;
        }
        if (game::isReplaceable(block) || game::itemForBlock(block) == game::ItemId::None) {
            return false;
        }
    }
    return true;
}

/// **The per-pass assert lives inside the template on purpose**, exactly as the
/// name sweep in `Block.hpp` explains: each instantiation is its own constant
/// evaluation with its own step budget, which is the entire point of striding.
/// Folding all seven into one expression spends a single allowance on the lot.
template <int Pass>
struct FallerSweep {
    static_assert(fallersAreNeverOccupants(Pass),
                  "a block that falls is also replaceable, or has no item of its own - either "
                  "one makes the falling-block drain pay the wrong thing");
    static constexpr bool swept = true;
};

template <int... Pass>
constexpr bool everyFallerPassSwept(std::integer_sequence<int, Pass...>) {
    return (FallerSweep<Pass>::swept && ...);
}

static_assert(everyFallerPassSwept(std::make_integer_sequence<int, kFallerSweepPasses>{}),
              "the failing FallerSweep instantiation above names which stride");
/// **The control, and without it the sweep above could pass while inspecting
/// nothing.** `fallersAreNeverOccupants` `continue`s past every id `isFalling`
/// rejects, so if that predicate ever collapsed - narrowed to nothing by a
/// widened family test, which is the shape that turned eight `blockName` cases
/// into dead code - all seven passes would go on returning true over an empty
/// population and the alarm would be silent. Measured today: the sweep inspects
/// 25 ids, and the identical rule with its gate opened to every id returns false
/// in 2 of the 7 passes, so it demonstrably *can* say no. These three keep that
/// true in the build rather than in a probe somebody deleted: **a zero from a
/// test that cannot produce a one is not evidence.** Named ids rather than a
/// count, because a count over 3309 ids is a constant-evaluation budget this
/// does not need to spend, and because sand and gravel will not stop falling.
static_assert(game::isFalling(game::BlockId::Sand) && game::isFalling(game::BlockId::Gravel) &&
                  game::isFalling(game::BlockId::Anvil),
              "isFalling answers no for sand, gravel or an anvil, so the sweep above is now "
              "proving nothing about an empty set");
/// **The coverage assert that stood here has been retired, not lost.** It read
/// `kFallerSweepPasses * kFallerSweepStride >= kBlockIdCount`, and once the
/// pass count is derived from exactly those two values that is one side of a
/// derivation compared against itself - bug shape #11, a `static_assert` that
/// cannot fail and therefore proves nothing, while reading like a guard. The
/// property it protected is now structural: the passes are *generated* from
/// the constant by `make_integer_sequence`, so ids appended past the last
/// stride raise the pass count instead of falling off the end of it.
///
/// **This is the "run" case and only the run case.** `CLAUDE.md` is explicit
/// that deriving a size does not retire a literal-row check - `std::array`
/// zero-fills a short initialiser and the compiler says nothing. There is no
/// array here and no initialiser list: nothing is written out per pass, so
/// there is no second property left for an assert to hold. The two asserts
/// above stay, because both check something a derivation cannot: that the
/// swept population is not empty, and that every pass actually inspected it.

// Change this and the entire world changes, reproducibly.
constexpr std::uint32_t kWorldSeed = 1337u;
// Selectable frame caps, lowest to highest. 0 means uncapped.
// Temporary keyboard-driven stand-in until there is a real settings screen.
//
// **Size deduced, for the reason spelled out over `kToneMapperNames` below** -
// and this one has no cross-file assert to fall back on, so the deduction is
// the whole of its protection. `kDefaultFpsCapIndex` indexes it at startup and
// F1/F2 walk it at runtime; a dropped row under an explicit `8` would have left
// a silent `0.0` in the tail, which reads as "uncapped" rather than as damage.
constexpr std::array kFpsCapOptions{30.0, 60.0, 90.0, 120.0, 144.0, 165.0, 240.0, 0.0};
constexpr std::size_t kDefaultFpsCapIndex = 3;
static_assert(kDefaultFpsCapIndex < kFpsCapOptions.size());

// In the order `tonemap.frag` tests for them, which is also the order
// `Settings::toneMapper` counts in. Short on purpose: these are shown on the F5
// overlay as well as logged, and the overlay has one column to fit them in.
//
// **No explicit size, on this and the five arrays around it, and that is load
// bearing rather than terseness.** Written `std::array<const char*, 4>` the
// literal `4` and the rows are two independent facts, and `std::array` checks
// only one of them: an initialiser that is too LONG is a hard error, one that
// is too SHORT is not - it silently zero-fills, so a dropped row leaves a null
// `const char*` in the tail. That is not a wrong string on the overlay, it is
// undefined behaviour the moment `settings.toneMapper` happens to index it, and
// the assert below would still pass because `.size()` reports the declared 4
// either way. Deducing the size from the rows makes the shortfall
// *inexpressible* rather than merely detected.
//
// **And it costs no new assert - it upgrades the one already here.** The
// existing `static_assert` pins this against a count in another file, which is
// the real coupling and is worth keeping; with the size deduced, that same
// assert now covers both properties at once, because `.size()` IS the row
// count. Rung 2 becomes rung 1 for the price of deleting a token.
//
// **The deduction is exact, not approximate**: `std::array`'s guide takes its
// parameters by value, so every string literal decays to `const char*` before
// deduction and all four agree, giving `std::array<const char*, 4>` - the type
// that was spelled out here, unchanged.
constexpr std::array kToneMapperNames{"PBR neutral", "Hable", "Reinhard", "ACES"};
static_assert(kToneMapperNames.size() == game::Settings::kToneMapperCount);

// What F12 cycles through, in the order `deferred.frag` tests for them. These
// are how a deferred renderer is debugged: when the picture is wrong, one of
// them says which input is wrong.
constexpr std::array kDebugViewNames{
    "off",      "albedo",   "normal",   "roughness",   "metallic",
    "occlusion", "emissive", "sky/block light", "distance", "cast shadow"};
static_assert(kDebugViewNames.size() == engine::Renderer::kDebugViewCount);

// What G cycles through. Each step raises the shadow map's resolution, how many
// slices of the view it is split across, and how far shadows are drawn.
constexpr std::array kShadowQualityNames{"off", "low", "medium", "high"};
static_assert(kShadowQualityNames.size() == game::Settings::kShadowQualityCount);
static_assert(game::Settings::kShadowQualityCount ==
              static_cast<unsigned>(engine::Renderer::kShadowQualityCount));

// What C cycles through: how many steps each ray takes through the cloud deck.
constexpr std::array kCloudQualityNames{"off", "fast", "fancy"};
static_assert(kCloudQualityNames.size() == game::Settings::kCloudQualityCount);
static_assert(game::Settings::kCloudQualityCount ==
              static_cast<unsigned>(engine::Renderer::kCloudQualityCount));

// Blocks a second the deck drifts west **in a calm**. The reference's own
// figure is not published anywhere; this is ours, chosen so a cloud crosses the
// view in about a minute rather than the several the reference's estimated 0.6
// would take.
//
// It is a gain on `Weather::windSpeed()` rather than the whole answer. That
// accessor rests at exactly 1.0 in a calm - `Weather.cpp` writes
// `1.0f + rain * 5 + thunder * 6` and seeds the member at 1.0 - so multiplying
// by it leaves a clear day at precisely this number and nothing else changes.
constexpr float kCloudDriftPerSecond = 1.1f;

// `Weather::windSpeed()` in a calm. Subtracted where a reader wants a strength
// that starts at nothing rather than a multiplier that starts at one: the gust
// model below is deliberately zero-floored so a clear day can stand completely
// still, and adding the weather's baseline to it would take that away.
constexpr float kCalmWindSpeed = 1.0f;

// The deck's own ceiling, in the same spirit as the rain slant's twenty degrees
// and the foliage bend's 0.22. A full storm takes the wind to 12, and 12 x 1.1
// would cross the view in five seconds - which stops reading as weather and
// starts reading as a time-lapse. Four keeps a gale dramatic at about fifteen.
constexpr float kMaxDeckWindFactor = 4.0f;

std::string describeCap(double fps) {
    return fps > 0.0 ? std::to_string(static_cast<int>(fps)) + " fps" : "uncapped";
}

/// Block positions are small and clustered, so the usual shift-and-xor collides
/// badly along axes. Multiplying each axis by its own large odd constant is the
/// standard spatial hash and spreads them properly.
struct BlockPositionHash {
    std::size_t operator()(const glm::ivec3& position) const noexcept {
        const auto x = static_cast<std::size_t>(static_cast<std::uint32_t>(position.x));
        const auto y = static_cast<std::size_t>(static_cast<std::uint32_t>(position.y));
        const auto z = static_cast<std::size_t>(static_cast<std::uint32_t>(position.z));
        return (x * 73856093u) ^ (y * 19349663u) ^ (z * 83492791u);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// The worldgen census, behind `worldgen_probe` in settings.cfg.
//
// Terrain is the one system here with a *distribution*, and a distribution can
// be printed. Everything this reports has been wrong at least once in a build
// that compiled clean and produced no validation errors.
// ---------------------------------------------------------------------------
namespace {

/// Writes what every block actually occupies, so it can be checked against the
/// reference's own `models/block/*.json` rather than against nobody.
///
/// The union of the drawn geometry is the useful number: a model's boxes for
/// anything that has them, and the collision box otherwise, which for a full
/// cube is the cell. `tools/check-models.ps1` does the comparing - this side
/// only has to tell the truth about what we draw.
void probeBlockShapes() {
    const auto shapeName = [](game::BlockShape shape) {
        switch (shape) {
        case game::BlockShape::Empty: return "Empty";
        case game::BlockShape::Full: return "Full";
        case game::BlockShape::Cross: return "Cross";
        case game::BlockShape::Slab: return "Slab";
        case game::BlockShape::Stairs: return "Stairs";
        case game::BlockShape::Fence: return "Fence";
        case game::BlockShape::Wall: return "Wall";
        case game::BlockShape::Pane: return "Pane";
        case game::BlockShape::Model: return "Model";
        case game::BlockShape::Ladder: return "Ladder";
        case game::BlockShape::Vine: return "Vine";
        case game::BlockShape::Cocoa: return "Cocoa";
        case game::BlockShape::Gate: return "Gate";
        case game::BlockShape::Hovering: return "Hovering";
        case game::BlockShape::Door: return "Door";
        case game::BlockShape::Trapdoor: return "Trapdoor";
        case game::BlockShape::Bed: return "Bed";
        case game::BlockShape::Tilled: return "Tilled";
        case game::BlockShape::Flat: return "Flat";
        // The three that were missing, and the dump said `?` for every button,
        // pressure plate, sign and banner in the game because of it - which is
        // a shape column `tools/check-models.ps1` groups its report by. Found
        // by building once under `/w44062`; see `DECISIONS.md`.
        case game::BlockShape::Button: return "Button";
        case game::BlockShape::Plate: return "Plate";
        case game::BlockShape::Sign: return "Sign";
        }
        return "?";
    };

    std::ofstream out(engine::executableDirectory() / "block-shapes.txt");
    out << "# id\tname\tshape\tboxes\tminX\tminY\tminZ\tmaxX\tmaxY\tmaxZ\n";
    for (int raw = 1; raw <= static_cast<int>(game::kLastBlock); ++raw) {
        const auto id = static_cast<game::BlockId>(raw);
        const game::BlockShape shape = game::blockShape(id);
        if (shape == game::BlockShape::Empty) {
            continue;
        }
        float lo[3]{2.0f, 2.0f, 2.0f};
        float hi[3]{-1.0f, -1.0f, -1.0f};
        int count = 0;
        const auto swallow = [&](const game::BlockBox& b) {
            lo[0] = std::min(lo[0], b.minX);
            lo[1] = std::min(lo[1], b.minY);
            lo[2] = std::min(lo[2], b.minZ);
            hi[0] = std::max(hi[0], b.maxX);
            hi[1] = std::max(hi[1], b.maxY);
            hi[2] = std::max(hi[2], b.maxZ);
            ++count;
        };
        if (shape == game::BlockShape::Model) {
            const game::ModelBoxes model = game::postModel(id);
            for (int i = 0; i < model.count; ++i) {
                swallow(model.boxes[i].box);
            }
        } else if (shape == game::BlockShape::Full || shape == game::BlockShape::Cross) {
            swallow({0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f});
        } else if (game::connectsToNeighbours(shape)) {
            // With no arms, which is the post the reference ships on its own.
            const game::BlockBoxes boxes = game::collisionBoxesWith(id, 0);
            for (int i = 0; i < boxes.count; ++i) {
                swallow(boxes.boxes[i]);
            }
        } else if (game::drawsWithoutColliding(id)) {
            // **A block that collides with nothing has no collision boxes**, so
            // falling through to `collisionBoxes` below counts zero and drops it
            // out of the dump entirely - which is how every button, pressure
            // plate, wire, rail and tripwire went unmeasured for a whole round.
            const game::BlockBoxes boxes = game::uncollidableDrawnBoxes(id);
            for (int i = 0; i < boxes.count; ++i) {
                swallow(boxes.boxes[i]);
            }
        } else {
            const game::BlockBoxes boxes = game::collisionBoxes(id);
            for (int i = 0; i < boxes.count; ++i) {
                swallow(boxes.boxes[i]);
            }
        }
        if (count == 0) {
            continue;
        }
        out << raw << '\t' << game::blockName(id) << '\t' << shapeName(shape) << '\t' << count;
        for (int a = 0; a < 3; ++a) {
            out << '\t' << lo[a];
        }
        for (int a = 0; a < 3; ++a) {
            out << '\t' << hi[a];
        }
        out << '\n';
    }
    engine::logInfo("block-shapes.txt written beside the exe");
}

void probeWorldgen() {
    constexpr int kSpan = 8192;
    constexpr int kStride = 48;

    std::array<int, static_cast<std::size_t>(game::BiomeId::Count)> biomeCounts{};
    int columns = 0;
    int surfaceMin = 999;
    int surfaceMax = -999;
    long long surfaceSum = 0;
    int belowSea = 0;

    // How heavy each climate field's tails are. The band edges were taken from
    // the reference, which draws them across a bell-shaped Perlin sum; if ours
    // is flatter then every biome at an end of an axis is over-represented, and
    // that one fact shows up as too many peaks *and* as deserts appearing where
    // a temperate region should be.
    struct FieldStat {
        const char* name;
        double absSum = 0.0;
        double signedSum = 0.0;
        long long positive = 0;
        long long tail = 0;
    };
    std::array<FieldStat, 5> fields{{{"temperature"}, {"humidity"}, {"continental"}, {"erosion"},
                                     {"weirdness"}}};

    for (int z = -kSpan; z <= kSpan; z += kStride) {
        for (int x = -kSpan; x <= kSpan; x += kStride) {
            const game::Climate climate = game::climateAt(kWorldSeed, x, z);
            ++biomeCounts[static_cast<std::size_t>(game::biomeFor(climate))];

            // **Both sizes stay written out, and this is the counter-example to
            // the rule stated over `kToneMapperNames`.** Deducing is right when
            // a short row zero-fills into a value that is *used*; it is wrong
            // here, because the loop below walks `f < fields.size()` and
            // subscripts `sample[f]`. The two arrays are pinned to each other by
            // that loop and to nothing else. Left explicit, a row dropped from
            // either gives a wrong statistic - `sample[4]` reads a zeroed float
            // - which is bad. Deduced, the same edit makes `sample` shorter than
            // the bound and `sample[f]` runs off the end, which is undefined
            // behaviour. So deduction would convert a wrong number into a
            // memory bug, and the honest fix is neither: it is that five climate
            // fields have no single owner to derive from. If one ever appears,
            // derive both from it; until then two literals that a reader can see
            // on adjacent lines beat one deduced and one inferred.
            const std::array<float, 5> sample{climate.temperature, climate.humidity,
                                              climate.continentalness, climate.erosion,
                                              climate.weirdness};
            for (std::size_t f = 0; f < fields.size(); ++f) {
                fields[f].absSum += std::abs(sample[f]);
                fields[f].signedSum += sample[f];
                if (sample[f] > 0.0f) {
                    ++fields[f].positive;
                }
                if (std::abs(sample[f]) > 0.55f) {
                    ++fields[f].tail;
                }
            }
            const int surface = game::surfaceHeightAt(kWorldSeed, x, z);
            surfaceMin = std::min(surfaceMin, surface);
            surfaceMax = std::max(surfaceMax, surface);
            surfaceSum += surface;
            if (surface < game::kSeaLevel) {
                ++belowSea;
            }
            ++columns;
        }
    }

    engine::logInfo("PROBE columns " + std::to_string(columns) + " surface min/mean/max " +
                    std::to_string(surfaceMin) + "/" +
                    std::to_string(static_cast<double>(surfaceSum) / columns) + "/" +
                    std::to_string(surfaceMax) + "  below sea " +
                    std::to_string(100.0 * belowSea / columns) + "%");

    for (const FieldStat& stat : fields) {
        engine::logInfo(std::string("PROBE field ") + stat.name + " mean|v| " +
                        std::to_string(stat.absSum / columns) + "  beyond +/-0.55 " +
                        std::to_string(100.0 * static_cast<double>(stat.tail) / columns) +
                        "%  mean " + std::to_string(stat.signedSum / columns) + "  above 0 " +
                        std::to_string(100.0 * static_cast<double>(stat.positive) / columns) + "%");
    }

    for (std::size_t i = 0; i < biomeCounts.size(); ++i) {
        if (biomeCounts[i] == 0) {
            engine::logWarn(std::string("PROBE biome MISSING ") +
                            game::biomeInfo(static_cast<game::BiomeId>(i)).name);
        } else {
            engine::logInfo(std::string("PROBE share ") +
                            game::biomeInfo(static_cast<game::BiomeId>(i)).name + " " +
                            std::to_string(100.0 * biomeCounts[i] / columns) + "%");
        }
    }

    // Per-biome top block, which is the whole of the "stone rings" and
    // "peaks that are only snow" question.
    //
    // **On the heap, not the stack.** One `long long` per block per biome is a
    // quarter of a megabyte now that there are eleven hundred blocks, and a
    // chunk stack sits beside it in the same frame - together they overran the
    // thread's stack and the game exited before it could log a single line.
    using TopBlockCounts = std::array<std::array<long long, game::kBlockIdCount>,
                                      static_cast<std::size_t>(game::BiomeId::Count)>;
    const auto owned = std::make_unique<TopBlockCounts>();
    TopBlockCounts& topBlocks = *owned;
    // Solid cells sitting above the terrain top. Terrain is single-valued, so
    // anything here that is not a tree is floating land.
    long long floatingTerrain = 0;
    long long treeCellsAbove = 0;
    long long pillars = 0;
    long long dryBed = 0;
    long long snowOnWarm = 0;
    long long caveMouths = 0;
    long long nearSurfaceAir = 0;
    long long floatingDecor = 0;
    long long deepAir = 0;
    long long deepSolid = 0;
    std::array<long long, game::kBlockIdCount> blockCounts{};
    int chunkCount = 0;

    // **Every chunk buffer here is on the heap.** A chunk is a hundred
    // kilobytes and MSVC reserves the whole frame up front, so three of them
    // beside the per-biome table overran the thread's stack outright - the
    // process died with 0xC00000FD before it could log a line, which reads as
    // "the probe does nothing" rather than as a crash.
    using ChunkStack = std::array<game::Chunk, 3>;
    const auto columnOwner = std::make_unique<ChunkStack>();
    ChunkStack& stack = *columnOwner;

    for (int cz = -5; cz <= 5; ++cz) {
        for (int cx = -5; cx <= 5; ++cx) {
            for (int cy = 0; cy < game::kWorldHeightChunks; ++cy) {
                stack[static_cast<std::size_t>(cy)] = game::generateChunk(kWorldSeed, {cx, cy, cz});
                ++chunkCount;
            }

            // Heights for this chunk plus a one-column border, so a pillar can
            // be told from a hillside without paying for it per neighbour.
            constexpr int kHSpan = game::Chunk::kSize + 2;
            std::array<int, static_cast<std::size_t>(kHSpan) * kHSpan> heights{};
            for (int hz = -1; hz <= game::Chunk::kSize; ++hz) {
                for (int hx = -1; hx <= game::Chunk::kSize; ++hx) {
                    heights[static_cast<std::size_t>(hz + 1) * kHSpan + static_cast<std::size_t>(hx + 1)] =
                        game::surfaceHeightAt(kWorldSeed, cx * game::Chunk::kSize + hx,
                                              cz * game::Chunk::kSize + hz);
                }
            }
            const auto heightAt = [&](int hx, int hz) {
                return heights[static_cast<std::size_t>(hz + 1) * kHSpan + static_cast<std::size_t>(hx + 1)];
            };

            for (int lz = 0; lz < game::Chunk::kSize; ++lz) {
                for (int lx = 0; lx < game::Chunk::kSize; ++lx) {
                    const int worldX = cx * game::Chunk::kSize + lx;
                    const int worldZ = cz * game::Chunk::kSize + lz;
                    const int surface = heightAt(lx, lz);
                    const auto biome = game::biomeFor(game::climateAt(kWorldSeed, worldX, worldZ));

                    // A column standing well clear of every neighbour is the
                    // sheer-sided pillar a would-be island turns into.
                    const int highest = std::max(std::max(heightAt(lx - 1, lz), heightAt(lx + 1, lz)),
                                                 std::max(heightAt(lx, lz - 1), heightAt(lx, lz + 1)));
                    if (surface - highest >= 5) {
                        ++pillars;
                    }

                    const game::BlockId topBlock =
                        stack[static_cast<std::size_t>(surface / game::Chunk::kSize)].at(
                            lx, surface % game::Chunk::kSize, lz);

                    // A bed material on dry ground: the ribbon of gravel and
                    // sand a river leaves when it never reaches the waterline.
                    if (surface >= game::kSeaLevel &&
                        (topBlock == game::BlockId::Gravel || topBlock == game::BlockId::Sand) &&
                        game::biomeHasAny(biome, game::BiomeTag::Ocean | game::BiomeTag::River)) {
                        ++dryBed;
                    }

                    // The terrain top carved away, which is a cave open to the
                    // sky rather than a tunnel that merely comes close to one.
                    if (topBlock == game::BlockId::Air) {
                        ++caveMouths;

                        // ...and something still sitting on top of that hole.
                        // FLOATING-TERRAIN cannot see this: it files logs,
                        // leaves and dirt as tree cells and looks no further,
                        // which is exactly the blind spot that let trees be
                        // built over cave mouths.
                        const int above = surface + 1;
                        if (above < game::kWorldHeightChunks * game::Chunk::kSize &&
                            stack[static_cast<std::size_t>(above / game::Chunk::kSize)].at(
                                lx, above % game::Chunk::kSize, lz) != game::BlockId::Air) {
                            ++floatingDecor;
                        }
                    }
                    for (int d = 0; d < 8; ++d) {
                        const int y = surface - d;
                        if (y >= 0 && stack[static_cast<std::size_t>(y / game::Chunk::kSize)].at(
                                          lx, y % game::Chunk::kSize, lz) == game::BlockId::Air) {
                            ++nearSurfaceAir;
                            break;
                        }
                    }

                    // Snow standing where the freeze rule says it cannot. Asks
                    // the rule itself rather than naming biomes, so it stays
                    // true whatever the table says tomorrow.
                    if (topBlock == game::BlockId::Snow &&
                        !game::freezesAt(kWorldSeed, game::biomeInfo(biome).warmth, worldX, surface,
                                         worldZ)) {
                        ++snowOnWarm;
                    }

                    for (int y = 0; y < game::kWorldHeightChunks * game::Chunk::kSize; ++y) {
                        const game::BlockId id =
                            stack[static_cast<std::size_t>(y / game::Chunk::kSize)].at(
                                lx, y % game::Chunk::kSize, lz);
                        ++blockCounts[static_cast<std::size_t>(id)];

                        if (y == surface) {
                            ++topBlocks[static_cast<std::size_t>(biome)][static_cast<std::size_t>(id)];
                        }
                        if (y > surface && id != game::BlockId::Air && !game::isWater(id) &&
                            !game::isIce(id)) {
                            // **A family test, not a list of ids.** This named
                            // oak's log and leaves and five plants outright, so
                            // every spruce, birch and new flower placed above
                            // the surface counted as floating land - 8031 of
                            // them, which is the counter being wrong rather than
                            // the world. The dirt is the block a trunk forces
                            // under itself, and the lily pad grows on a water
                            // surface, which is above the terrain top by
                            // definition - it was the whole of a later 1472.
                            if (game::isLogBlock(id) || game::isLeafBlock(id) ||
                                game::isCrossBlock(id) || id == game::BlockId::Dirt ||
                                id == game::BlockId::LilyPad) {
                                ++treeCellsAbove;
                            } else {
                                ++floatingTerrain;
                            }
                        }
                        if (y < 20) {
                            if (id == game::BlockId::Air) {
                                ++deepAir;
                            } else {
                                ++deepSolid;
                            }
                        }
                    }
                }
            }
        }
    }

    engine::logInfo("PROBE chunks " + std::to_string(chunkCount) + " FLOATING-TERRAIN " +
                    std::to_string(floatingTerrain) + "  (tree cells above surface " +
                    std::to_string(treeCellsAbove) + ", so the probe can see up there)");
    engine::logInfo("PROBE PILLARS " + std::to_string(pillars) + " (columns 5+ above every neighbour)  " +
                    "DRY-BED " + std::to_string(dryBed) + " (gravel or sand on dry river/ocean ground)  " +
                    "SNOW-ON-WARM " + std::to_string(snowOnWarm) + " (snow where the freeze rule says no)");
    engine::logInfo("PROBE CAVE-MOUTHS " + std::to_string(caveMouths) + " of " +
                    std::to_string(363 / game::kWorldHeightChunks * game::Chunk::kSize *
                                   game::Chunk::kSize) +
                    " columns open to the sky, air within 8 of surface " +
                    std::to_string(nearSurfaceAir) + "  FLOATING-DECOR " +
                    std::to_string(floatingDecor) + " (tree, dirt or water over a hole)");
    engine::logInfo("PROBE deep-rock hollow " +
                    std::to_string(100.0 * static_cast<double>(deepAir) /
                                   static_cast<double>(std::max<long long>(1, deepAir + deepSolid))) +
                    "%");

    // Villages. Every number here has to be *counted* rather than reasoned
    // about: the placement grid, the biome gate and the site test each throw
    // candidates away, and "the code looks right" says nothing about how many
    // survive. A run reporting zero villages is the failure this exists to make
    // impossible to miss.
    {
        constexpr int kVillageSpan = 6144;
        std::array<int, 6> byType{};
        int found = 0;
        long long buildings = 0;
        long long residents = 0;
        int biggest = 0;
        std::array<int, static_cast<std::size_t>(game::village::Design::Count)> byDesign{};
        bool checked = false;

        // Walk grid cells directly rather than chunks: a village belongs to one
        // cell, so this counts each exactly once.
        std::array<int, 5> rejects{};
        const int cells = kVillageSpan / game::village::kCellBlocks + 1;
        for (int cz = -cells; cz <= cells; ++cz) {
            for (int cx = -cells; cx <= cells; ++cx) {
                const game::village::Plan plan = game::village::solveCell(kWorldSeed, cx, cz);
                if (!plan.valid) {
                    ++rejects[static_cast<std::size_t>(plan.reject)];
                    continue;
                }
                ++found;
                ++byType[static_cast<std::size_t>(plan.type)];
                buildings += plan.buildingCount;
                residents += plan.residentCount;
                biggest = std::max(biggest, static_cast<int>(plan.buildingCount));
                for (int b = 0; b < plan.buildingCount; ++b) {
                    ++byDesign[static_cast<std::size_t>(plan.buildings[b].design)];
                }
            }
        }

        const int attempts = (2 * cells + 1) * (2 * cells + 1);
        engine::logInfo("PROBE VILLAGES " + std::to_string(found) + " of " +
                        std::to_string(attempts) + " grid cells, one per " +
                        std::to_string(found > 0 ? (attempts / found) * game::village::kCellBlocks *
                                                       game::village::kCellBlocks / 1000000
                                                 : 0) +
                        " million blocks; buildings/village " +
                        std::to_string(found > 0 ? static_cast<double>(buildings) / found : 0.0) +
                        " (largest " + std::to_string(biggest) + "), residents/village " +
                        std::to_string(found > 0 ? static_cast<double>(residents) / found : 0.0));
        static constexpr const char* kRejectNames[5] = {"-", "biome", "water", "cave", "rough"};
        for (std::size_t r = 1; r < rejects.size(); ++r) {
            engine::logInfo(std::string("PROBE village reject ") + kRejectNames[r] + " " +
                            std::to_string(rejects[r]));
        }
        static constexpr const char* kTypeNames[6] = {"none",  "plains", "desert",
                                                      "savanna", "taiga",  "snowy"};
        for (std::size_t t = 1; t < byType.size(); ++t) {
            engine::logInfo(std::string("PROBE village type ") + kTypeNames[t] + " " +
                            std::to_string(byType[t]));
        }
        static constexpr const char* kDesignNames[] = {
            "town centre", "small house A", "small house B", "small house C",
            "medium house", "large house",  "workshop",      "library",
            "temple",      "farm",          "animal pen"};
        for (std::size_t d = 0; d < byDesign.size(); ++d) {
            engine::logInfo(std::string("PROBE village piece ") + kDesignNames[d] + " " +
                            std::to_string(byDesign[d]));
        }

        // Generate one whole village and read what actually came out of it.
        //
        // Counting plans proves the *layout* solver runs. It says nothing about
        // whether a house has a floor, whether its door can be walked through,
        // or whether the thing is standing on a plinth over a hole - and every
        // one of those has to be a number, because none of them fails loudly.
        //
        // The village nearest the origin is the one inspected, because that is
        // also the one worth quoting to whoever is going to go and look at it.
        int bestCellX = 0;
        int bestCellZ = 0;
        long long bestDistance = -1;
        for (int cz = -cells; cz <= cells; ++cz) {
            for (int cx = -cells; cx <= cells; ++cx) {
                const game::village::Plan plan = game::village::solveCell(kWorldSeed, cx, cz);
                if (!plan.valid) {
                    continue;
                }
                const long long dx = plan.originX;
                const long long dz = plan.originZ;
                const long long distance = dx * dx + dz * dz;
                if (bestDistance < 0 || distance < bestDistance) {
                    bestDistance = distance;
                    bestCellX = cx;
                    bestCellZ = cz;
                }
            }
        }

        if (bestDistance >= 0) {
            const game::village::Plan plan =
                game::village::solveCell(kWorldSeed, bestCellX, bestCellZ);
            checked = true;

                const int firstChunkX = game::floorDivInt(plan.minX, game::Chunk::kSize);
                const int lastChunkX = game::floorDivInt(plan.maxX, game::Chunk::kSize);
                const int firstChunkZ = game::floorDivInt(plan.minZ, game::Chunk::kSize);
                const int lastChunkZ = game::floorDivInt(plan.maxZ, game::Chunk::kSize);
                const int spanX = lastChunkX - firstChunkX + 1;
                const int spanZ = lastChunkZ - firstChunkZ + 1;

                auto grid = std::make_unique<std::vector<game::Chunk>>();
                grid->resize(static_cast<std::size_t>(spanX) * spanZ * game::kWorldHeightChunks);
                for (int qz = 0; qz < spanZ; ++qz) {
                    for (int qx = 0; qx < spanX; ++qx) {
                        for (int qy = 0; qy < game::kWorldHeightChunks; ++qy) {
                            (*grid)[(static_cast<std::size_t>(qz) * spanX + qx) *
                                        game::kWorldHeightChunks +
                                    qy] =
                                game::generateChunk(kWorldSeed,
                                                    {firstChunkX + qx, qy, firstChunkZ + qz});
                        }
                    }
                }

                const auto blockAt = [&](int x, int y, int z) {
                    const int qx = game::floorDivInt(x, game::Chunk::kSize) - firstChunkX;
                    const int qz = game::floorDivInt(z, game::Chunk::kSize) - firstChunkZ;
                    const int qy = game::floorDivInt(y, game::Chunk::kSize);
                    if (qx < 0 || qz < 0 || qx >= spanX || qz >= spanZ || qy < 0 ||
                        qy >= game::kWorldHeightChunks) {
                        return game::BlockId::Air;
                    }
                    const game::Chunk& chunk =
                        (*grid)[(static_cast<std::size_t>(qz) * spanX + qx) *
                                    game::kWorldHeightChunks +
                                qy];
                    return chunk.at(x - (firstChunkX + qx) * game::Chunk::kSize,
                                    y - qy * game::Chunk::kSize,
                                    z - (firstChunkZ + qz) * game::Chunk::kSize);
                };

                int floorHoles = 0;
                int hollowUnder = 0;
                int blockedDoors = 0;
                int missingDoors = 0;
                int beds = 0;
                int jobSites = 0;
                int doors = 0;
                int lights = 0;

                for (int b = 0; b < plan.buildingCount; ++b) {
                    const game::village::Building& built = plan.buildings[b];
                    for (int lz = 0; lz < built.depth; ++lz) {
                        for (int lx = 0; lx < built.width; ++lx) {
                            const int x = built.minX + lx;
                            const int z = built.minZ + lz;
                            if (blockAt(x, built.floorY - 1, z) == game::BlockId::Air) {
                                ++floorHoles;
                            }
                            if (blockAt(x, built.floorY - 2, z) == game::BlockId::Air) {
                                ++hollowUnder;
                            }
                        }
                    }
                }

                for (int z = plan.minZ; z <= plan.maxZ; ++z) {
                    for (int x = plan.minX; x <= plan.maxX; ++x) {
                        for (int y = std::max(0, plan.minY);
                             y <= std::min(plan.maxY,
                                           game::kWorldHeightChunks * game::Chunk::kSize - 1);
                             ++y) {
                            const game::BlockId id = blockAt(x, y, z);
                            if (game::isBed(id)) {
                                ++beds;
                            } else if (game::isDoor(id)) {
                                ++doors;
                            } else if (id == game::BlockId::Torch ||
                                       id == game::BlockId::Lantern) {
                                ++lights;
                            } else if (game::isFurnace(id) || game::isComposter(id) ||
                                       id == game::BlockId::Barrel ||
                                       id == game::BlockId::FletchingTable ||
                                       id == game::BlockId::Loom ||
                                       id == game::BlockId::CartographyTable ||
                                       id == game::BlockId::Lectern ||
                                       id == game::BlockId::Stonecutter ||
                                       id == game::BlockId::SmithingTable ||
                                       id == game::BlockId::Grindstone ||
                                       game::isCauldron(id) ||
                                       id == game::BlockId::BrewingStand) {
                                ++jobSites;
                            }
                        }
                    }
                }

                // Every dwelling has to be enterable: a door in the wall, and
                // two clear cells on the step outside it.
                for (int b = 0; b < plan.buildingCount; ++b) {
                    const game::village::Building& built = plan.buildings[b];
                    if (built.design == game::village::Design::TownCentre ||
                        built.design == game::village::Design::Farm ||
                        built.design == game::village::Design::AnimalPen) {
                        continue;
                    }
                    const int x1 = built.minX + built.width - 1;
                    const int z1 = built.minZ + built.depth - 1;
                    int doorX = built.minX;
                    int doorZ = built.minZ;
                    switch (built.facing) {
                    case game::FaceDirection::NegZ:
                        doorZ = built.minZ;
                        doorX = built.minX + built.width / 2;
                        break;
                    case game::FaceDirection::PosZ:
                        doorZ = z1;
                        doorX = built.minX + built.width / 2;
                        break;
                    case game::FaceDirection::NegX:
                        doorX = built.minX;
                        doorZ = built.minZ + built.depth / 2;
                        break;
                    default:
                        doorX = x1;
                        doorZ = built.minZ + built.depth / 2;
                        break;
                    }
                    // The exact slot is a style roll, so scan the wall rather
                    // than re-deriving it - a second copy of that rule here is
                    // precisely the bug this codebase keeps paying for.
                    bool hasDoor = false;
                    for (int t = -3; t <= 3 && !hasDoor; ++t) {
                        const int sx = doorX + (built.facing == game::FaceDirection::NegZ ||
                                                        built.facing == game::FaceDirection::PosZ
                                                    ? t
                                                    : 0);
                        const int sz = doorZ + (built.facing == game::FaceDirection::NegX ||
                                                        built.facing == game::FaceDirection::PosX
                                                    ? t
                                                    : 0);
                        if (!game::isDoor(blockAt(sx, built.floorY, sz))) {
                            continue;
                        }
                        hasDoor = true;
                        const int stepX =
                            sx + (built.facing == game::FaceDirection::PosX    ? 1
                                  : built.facing == game::FaceDirection::NegX ? -1
                                                                              : 0);
                        const int stepZ =
                            sz + (built.facing == game::FaceDirection::PosZ    ? 1
                                  : built.facing == game::FaceDirection::NegZ ? -1
                                                                              : 0);
                        if (blockAt(stepX, built.floorY, stepZ) != game::BlockId::Air ||
                            blockAt(stepX, built.floorY + 1, stepZ) != game::BlockId::Air) {
                            ++blockedDoors;
                        }
                    }
                    if (!hasDoor) {
                        ++missingDoors;
                    }
                }

                engine::logInfo(
                    "PROBE village at " + std::to_string(plan.originX) + "," +
                    std::to_string(plan.centreY) + "," + std::to_string(plan.originZ) + " type " +
                    kTypeNames[static_cast<std::size_t>(plan.type)] + ": " +
                    std::to_string(static_cast<int>(plan.buildingCount)) + " buildings, " +
                    std::to_string(doors) + " doors, " + std::to_string(beds) + " beds, " +
                    std::to_string(jobSites) + " job sites, " + std::to_string(lights) +
                    " lights");
                engine::logInfo("PROBE village FLOOR-HOLES " + std::to_string(floorHoles) +
                                " HOLLOW-UNDER " + std::to_string(hollowUnder) +
                                " MISSING-DOORS " + std::to_string(missingDoors) +
                                " BLOCKED-DOORS " + std::to_string(blockedDoors) +
                                "  (all four must read zero)");
        }
        (void)checked;
    }

    // Largest connected run of one ore, which is the whole of the "why is there
    // a coal seam the size of a room" question. A thresholded noise field has no
    // cap on this; a placed vein does, and that difference is only visible as a
    // number.
    {
        constexpr int kStack = game::kWorldHeightChunks * game::Chunk::kSize;
        const auto veinOwner = std::make_unique<ChunkStack>();
        ChunkStack& column = *veinOwner;
        for (int cy = 0; cy < game::kWorldHeightChunks; ++cy) {
            column[static_cast<std::size_t>(cy)] = game::generateChunk(kWorldSeed, {0, cy, 0});
        }
        const auto at = [&](int x, int y, int z) {
            return column[static_cast<std::size_t>(y / game::Chunk::kSize)].at(
                x, y % game::Chunk::kSize, z);
        };

        std::vector<bool> seen(static_cast<std::size_t>(game::Chunk::kSize) * kStack *
                                   game::Chunk::kSize,
                               false);
        const auto index = [&](int x, int y, int z) {
            return (static_cast<std::size_t>(y) * game::Chunk::kSize + static_cast<std::size_t>(z)) *
                       game::Chunk::kSize +
                   static_cast<std::size_t>(x);
        };

        std::array<int, game::kBlockIdCount> largest{};
        std::vector<glm::ivec3> open;
        for (int y = 0; y < kStack; ++y) {
            for (int z = 0; z < game::Chunk::kSize; ++z) {
                for (int x = 0; x < game::Chunk::kSize; ++x) {
                    const game::BlockId id = at(x, y, z);
                    if (!game::isOre(id) || seen[index(x, y, z)]) {
                        continue;
                    }
                    int size = 0;
                    open.clear();
                    open.push_back({x, y, z});
                    seen[index(x, y, z)] = true;
                    while (!open.empty()) {
                        const glm::ivec3 p = open.back();
                        open.pop_back();
                        ++size;
                        // Size deduced: a dropped row under an explicit `6`
                        // would zero-fill to `glm::ivec3{0, 0, 0}`, and a
                        // flood-fill step of "stay where you are" is a silently
                        // wrong answer rather than a crash.
                        constexpr std::array steps{
                            glm::ivec3{1, 0, 0},  glm::ivec3{-1, 0, 0}, glm::ivec3{0, 1, 0},
                            glm::ivec3{0, -1, 0}, glm::ivec3{0, 0, 1},  glm::ivec3{0, 0, -1}};
                        for (const glm::ivec3& step : steps) {
                            const glm::ivec3 n = p + step;
                            if (n.x < 0 || n.x >= game::Chunk::kSize || n.z < 0 ||
                                n.z >= game::Chunk::kSize || n.y < 0 || n.y >= kStack) {
                                continue;
                            }
                            if (at(n.x, n.y, n.z) != id || seen[index(n.x, n.y, n.z)]) {
                                continue;
                            }
                            seen[index(n.x, n.y, n.z)] = true;
                            open.push_back(n);
                        }
                    }
                    int& best = largest[static_cast<std::size_t>(id)];
                    best = std::max(best, size);
                }
            }
        }

        std::string line = "PROBE largest vein (one column):";
        for (std::size_t i = 0; i < largest.size(); ++i) {
            if (largest[i] > 0) {
                line += std::string(" ") + game::blockName(static_cast<game::BlockId>(i)) + " " +
                        std::to_string(largest[i]);
            }
        }
        engine::logInfo(line);
    }

    for (std::size_t b = 0; b < topBlocks.size(); ++b) {
        long long total = 0;
        for (long long n : topBlocks[b]) {
            total += n;
        }
        if (total < 400) {
            continue;
        }
        std::string line = std::string("PROBE top ") + game::biomeInfo(static_cast<game::BiomeId>(b)).name +
                           " (" + std::to_string(total) + "):";
        for (std::size_t i = 0; i < topBlocks[b].size(); ++i) {
            if (topBlocks[b][i] * 100 < total * 3) {
                continue;
            }
            line += std::string(" ") + game::blockName(static_cast<game::BlockId>(i)) + " " +
                    std::to_string(100 * topBlocks[b][i] / total) + "%";
        }
        engine::logInfo(line);
    }

    const long long total = static_cast<long long>(chunkCount) * game::Chunk::kSize * game::Chunk::kSize *
                            game::Chunk::kSize;
    for (std::size_t i = 0; i < blockCounts.size(); ++i) {
        if (blockCounts[i] == 0) {
            continue;
        }
        engine::logInfo(std::string("PROBE block ") + game::blockName(static_cast<game::BlockId>(i)) + " " +
                        std::to_string(blockCounts[i]) + " (" +
                        std::to_string(100.0 * static_cast<double>(blockCounts[i]) /
                                       static_cast<double>(total)) +
                        "%)");
    }
}
// PROBE-END

/// Tilts a look direction upward by `degrees`, so a thrown potion arcs instead
/// of flying flat.
///
/// **Source: Mojang's own published Bedrock behaviour packs**
/// (`Mojang/bedrock-samples`, `behavior_pack/entities/`), which are a primary
/// source and beat minecraft.wiki - the wiki documents Java in several places
/// without saying so. `splash_potion.json` and `lingering_potion.json` both
/// carry `"angle_offset": -20.0` on their `minecraft:projectile` component;
/// `egg.json` and `ender_pearl.json` both publish `0.0`. **That split is why
/// the caller gates this on `isThrownPotion` rather than on "is it thrown"** -
/// two of the four things the player can throw arc, and two do not.
///
/// **What it does, and the alternative that was rejected.** The offset moves
/// the pitch used for the *vertical* component only; the horizontal keeps the
/// unmodified aim's cos(pitch), and the result is renormalised. Java spells the
/// identical rule as `shootFromRotation(shooter, xRot, yRot, -20.0F, ...)` and
/// passes the *same* -20.0 constant, which is the evidence that the two
/// editions share the semantics rather than merely the number.
///
/// A **pure rotation about the right vector** was tried and rejected: it tips
/// over the top. At 80 degrees up it hands back a direction pointing 10 degrees
/// past vertical, so the potion flies *behind* the thrower - measured in
/// `probe-fxconserve/potion.cpp`, which keeps that rule as a control. The
/// decomposed form has no degenerate case: at a straight-up or straight-down
/// aim the horizontal part is already zero, so it simply stays vertical.
///
/// **The honest cost:** decomposing means a level aim leaves at 18.9 degrees,
/// not exactly 20 - renormalising shortens the vertical relative to a
/// horizontal that was never scaled. The reference does the same, and it is the
/// length of the throw that moves, not the direction of it.
[[nodiscard]] glm::vec3 liftedThrowAim(const glm::vec3& aim, float degrees) {
    // Minecraft's pitch is positive looking *down*, so `aim.y` is -sin(pitch).
    const float cosPitch = std::sqrt(aim.x * aim.x + aim.z * aim.z);
    const float pitch = std::atan2(-aim.y, cosPitch);
    const float lifted = pitch + glm::radians(degrees);
    // Never the zero vector: that would need the horizontal to vanish *and* the
    // lifted pitch to come out level at the same time, and the horizontal only
    // vanishes at a straight-up or straight-down aim, where it does not.
    return glm::normalize(glm::vec3{aim.x, -std::sin(lifted), aim.z});
}

/// Where a photo lands: a `photos/` folder in the project root.
///
/// **The exe does not run from the project root** - it sits in
/// `build/<config>/bin`, and a photo written beside it would be inside the one
/// folder `.gitignore` throws away and a clean rebuild deletes. The player
/// asked for photos in the checkout, so the root is *found* rather than
/// assumed: walk up from the executable until a directory holds a
/// `CMakeLists.txt`. That is true of the checkout and of nothing between it and
/// the exe, and it keeps working if the build folder is renamed, moved, or
/// configured somewhere else entirely.
///
/// Falls back to a `photos/` beside the exe when there is no such directory,
/// which is what a packaged copy with no source tree around it looks like -
/// **`GAPS.md` section 0.6 owns that packaging question**, and this
/// deliberately does not try to settle it.
[[nodiscard]] std::filesystem::path photosDirectory() {
    const std::filesystem::path& exeDirectory = engine::executableDirectory();
    std::error_code error;
    for (std::filesystem::path probe = exeDirectory;;) {
        // The error_code overload, because the throwing one would end the
        // session from inside a keypress handler if a directory on the way up
        // happened to be unreadable.
        if (std::filesystem::exists(probe / "CMakeLists.txt", error)) {
            return probe / "photos";
        }
        // `parent_path()` of a root is that root again, so the walk is stopped
        // by reaching a fixed point rather than by counting levels.
        const std::filesystem::path parent = probe.parent_path();
        if (parent.empty() || parent == probe) {
            break;
        }
        probe = parent;
    }
    return exeDirectory / "photos";
}

/// Local wall-clock time, in fields, without a deprecation warning.
///
/// **`std::localtime` is the obvious call and cannot be used here**: it hands
/// back a pointer into a shared static buffer, so MSVC deprecates it and warns
/// at /W4, and this project builds warning-free. Its two replacements do the
/// same job with their arguments in opposite orders and different names on the
/// two platforms, which is the whole reason for the `#if`.
[[nodiscard]] std::tm localTimeNow() {
    const std::time_t now = std::time(nullptr);
    std::tm broken{};
#if defined(_WIN32)
    localtime_s(&broken, &now);
#else
    localtime_r(&now, &broken);
#endif
    return broken;
}

/// A timestamped photo name that sorts chronologically and never overwrites one
/// already there.
///
/// Local time rather than UTC: the player is looking for the picture they took
/// a minute ago, and a name three hours out is one they have to do arithmetic
/// on. The fields run largest-first so a plain alphabetical listing is also a
/// chronological one.
[[nodiscard]] std::filesystem::path nextPhotoPath(const std::filesystem::path& directory) {
    const std::tm local = localTimeNow();
    char stamp[32]{};
    std::snprintf(stamp, sizeof(stamp), "photo-%04d-%02d-%02d-%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec);

    // Two photos inside the same second collide, and one silently replacing the
    // other is worse than a failure: the player would be looking at the second
    // picture wondering where the first went.
    std::error_code error;
    std::filesystem::path candidate = directory / (std::string(stamp) + ".png");
    for (int copy = 2; std::filesystem::exists(candidate, error); ++copy) {
        candidate = directory / (std::string(stamp) + "-" + std::to_string(copy) + ".png");
    }
    return candidate;
}

} // namespace

int main() {
    try {
        const std::filesystem::path settingsPath = engine::executableDirectory() / "settings.cfg";
        game::Settings settings = game::loadSettings(settingsPath);

        if (settings.worldgenProbe) {
            probeWorldgen();
            return 0;
        }

        if (settings.blockProbe) {
            probeBlockShapes();
            return 0;
        }

        // Declared before the world, and therefore destroyed after it: the world
        // submits jobs to this pool and must not outlive it.
        engine::JobSystem jobs(settings.workerThreads);

        // Audio comes up before the window on purpose: a machine with no sound
        // card logs one line and carries on silently, and finding that out
        // before anything is on screen keeps the failure legible.
        engine::AudioEngine audio;
        game::Sounds sounds;
        sounds.load(audio, engine::executableDirectory() / "sounds-reference");
        // **What the bank can say against what the game can name.** Three
        // faults that never produce a warning, a validation error or a
        // wrong-looking line: a staged stem this file spells one way and the
        // script spells another, a creature voice with nothing behind it, and
        // the one that cost the most - a bank that loaded perfectly and that
        // nothing in `game/` ever plays. Thirteen of those were found at once.
        // A query, so the logging is here rather than buried in the loader.
        for (const std::string& line : sounds.sweep()) {
            engine::logWarn(line);
        }
        // **`setSoundVolume`, not `setMasterVolume`.** Music has its own
        // multiplier now, which is what `Settings.hpp` always claimed - the old
        // name silenced both, so `sound_volume=0` took the soundtrack with it.
        audio.setSoundVolume(settings.soundVolume);
        audio.setMusicVolume(settings.musicVolume);

        engine::Window window(kWindowWidth, kWindowHeight, "Voxel Game");
        engine::VulkanContext context(window);

        // Order defines the texture array layer indices, which must match
        // TextureLayer in Block.hpp.
        const std::filesystem::path textureDir = engine::executableDirectory() / "assets" / "textures" / "blocks";

        // Reference block and item art, staged beside the exe rather than under
        // assets/ so it can never ship. Preferred per texture, so anything the
        // reference has no counterpart for silently keeps ours.
        //
        // **That set is exactly one file, `white.png`, measured 2026-08-19**
        // against the staged copy this code actually opens rather than the
        // repository's - those differ by nine stale skins and the repository is
        // the wrong population to ask. Dated, because this sentence used to
        // name the sun as a second example and `sun.png` has since been staged,
        // so a reader chasing it would have gone hunting for hand-drawn art the
        // reference already wins.
        //
        // The white layer is not art and cannot be given a counterpart: it is a
        // 16x16 solid swatch, and every `TextureLayer::White` caller multiplies
        // it by a vertex colour to draw the crosshair, the block outline, HUD
        // panels, the inventory dim and particles.
        //
        // **Falsified by**: any second name present in
        // `assets/textures/blocks` and absent from `blocks-reference` that also
        // appears as a `blockTexture` argument - the three sets are what make
        // this claim checkable without running anything.
        const std::filesystem::path referenceBlocks = engine::executableDirectory() / "blocks-reference";
        std::vector<std::string> missingTextures;
        // **What reached the screen, not merely whether a file existed.** This
        // vector is the whole of the fix and the distinction is the user's own
        // red line: the reference art *is* the art, so a texture quietly served
        // out of tier 2 - our hand-authored placeholder - is a wrong picture on
        // screen, not a successful load. `missingTextures` cannot see that by
        // construction, because tier 2 *returns a path*, so "0 block textures
        // are missing" was true with all hundred-and-twenty of our placeholders
        // on screen. Presence and provenance are different questions and only
        // one of them was being asked.
        //
        // **The same concept is already right ninety lines below**, which is
        // what makes this a rule that did not travel rather than a missing
        // feature: `pushEgg` has only two tiers, staged or blank, so it is loud
        // by accident of having no our-art tier to hide in. Blocks were silent
        // for exactly the reason they have a richer fallback.
        std::vector<std::string> ownArtTextures;
        const auto blockTexture = [&](const char* name) {
            std::filesystem::path staged = referenceBlocks / name;
            if (std::filesystem::exists(staged)) {
                return staged;
            }
            std::filesystem::path own = textureDir / name;
            if (std::filesystem::exists(own)) {
                // Recorded rather than counted, for the reason the drain below
                // gives: a count says how bad it is and a name says what to
                // look at. `white.png` is the one legitimate resident of this
                // tier - it is a rendering primitive rather than art - so the
                // drain filters it instead of this returning early, because a
                // second exemption belongs in one place and not in two.
                ownArtTextures.emplace_back(name);
                return own;
            }
            // The list index *is* the layer index, so a missing file has to
            // become a blank layer rather than be skipped - dropping it would
            // slide every layer after it.
            missingTextures.emplace_back(name);
            return textureDir / "white.png";
        };

        const std::vector<std::filesystem::path> blockTextures{
            blockTexture("stone.png"),          blockTexture("dirt.png"),
            blockTexture("grass_top.png"),      blockTexture("grass_side.png"),
            blockTexture("sand.png"),           blockTexture("white.png"),
            blockTexture("cobblestone.png"),    blockTexture("gravel.png"),
            blockTexture("snow.png"),           blockTexture("planks.png"),
            blockTexture("bricks.png"),         blockTexture("glowstone.png"),
            blockTexture("water.png"),          blockTexture("log_side.png"),
            blockTexture("log_top.png"),        blockTexture("leaves.png"),
            blockTexture("sun.png"),            blockTexture("tall_grass.png"),
            blockTexture("stick.png"),          blockTexture("crafting_table_top.png"),
            blockTexture("crafting_table_front.png"), blockTexture("crafting_table_side.png"),
            blockTexture("furnace_top.png"),    blockTexture("furnace_side.png"),
            blockTexture("furnace_front.png"),  blockTexture("furnace_front_on.png"),
            blockTexture("charcoal.png"),       blockTexture("torch.png"),
            blockTexture("wooden_pickaxe.png"), blockTexture("wooden_axe.png"),
            blockTexture("wooden_shovel.png"),  blockTexture("wooden_sword.png"),
            blockTexture("wooden_hoe.png"),     blockTexture("stone_pickaxe.png"),
            blockTexture("stone_axe.png"),      blockTexture("stone_shovel.png"),
            blockTexture("stone_sword.png"),    blockTexture("stone_hoe.png"),
            blockTexture("andesite.png"),       blockTexture("diorite.png"),
            blockTexture("granite.png"),        blockTexture("smooth_stone.png"),
            blockTexture("stone_bricks.png"),   blockTexture("mossy_cobblestone.png"),
            blockTexture("obsidian.png"),       blockTexture("clay.png"),
            blockTexture("sandstone_top.png"),  blockTexture("sandstone_side.png"),
            blockTexture("sandstone_bottom.png"), blockTexture("bookshelf.png"),
            blockTexture("glass.png"),          blockTexture("dandelion.png"),
            blockTexture("poppy.png"),          blockTexture("dead_bush.png"),
            blockTexture("coal_ore.png"),       blockTexture("iron_ore.png"),
            blockTexture("copper_ore.png"),     blockTexture("gold_ore.png"),
            blockTexture("redstone_ore.png"),   blockTexture("lapis_ore.png"),
            blockTexture("diamond_ore.png"),    blockTexture("emerald_ore.png"),
            blockTexture("deepslate_side.png"), blockTexture("deepslate_top.png"),
            blockTexture("bedrock.png"),        blockTexture("terracotta.png"),
            blockTexture("packed_ice.png")};

        // The missing-texture report used to stand here, which is where
        // `blockTextures` ends and where about nine tenths of the list has not
        // been built yet: `blockTexture` keeps being called for every appended
        // sprite run for another seven hundred lines, and every one of those
        // names went unreported because the only drain had already run. It now
        // lives immediately before `engine::Renderer`, where the list is
        // complete and still ahead of first use.

        // Spawn egg sprites, staged beside the exe rather than under assets/ for
        // the same reason the reference skins and `blocks-reference/` are:
        // `assets/` is committed and copied by the build, while these are copied
        // by a tool out of a reference tree the repository does not carry, so a
        // fresh checkout has to start without them.
        //
        // **Not because they are provisional.** The 2026-08-17 ruling settled
        // that Mojang's files are this game's permanent art - private
        // repository, personal use, nothing ships - so the "placeholder art"
        // wording that stood here was scheduling work the user had explicitly
        // cancelled, and a reader acting on it would go hunting for an egg
        // nobody is drawing. Where staged art should finally live is `GAPS.md`
        // S0.6's packaging question, not this file's.
        //
        // A missing sprite falls back to blank rather than being skipped. The
        // list index *is* the layer index, so dropping one would silently shift
        // every layer after it - and there are no layers after these today,
        // which is exactly the sort of thing that stops being true later.
        std::vector<std::filesystem::path> spriteLayers = blockTextures;
        {
            // The eggs start where the block list ends, and `SpawnEggFirst` says
            // where that is as a constant. Adding a texture to the list above
            // without moving it would slide all thirty-six egg sprites by one
            // and mistexture every egg - silently, because nothing else knows.
            if (blockTextures.size() != static_cast<std::size_t>(game::TextureLayer::SpawnEggFirst)) {
                engine::logError("TextureLayer::SpawnEggFirst is " +
                                 std::to_string(static_cast<int>(game::TextureLayer::SpawnEggFirst)) +
                                 " but the block texture list has " +
                                 std::to_string(blockTextures.size()) +
                                 " entries - every spawn egg will be mistextured");
            }
            const std::filesystem::path eggDir = engine::executableDirectory() / "spawn-eggs";
            int missing = 0;
            const auto pushEgg = [&](int index) {
                char name[16]{};
                std::snprintf(name, sizeof(name), "egg%02d.png", index);
                std::filesystem::path egg = eggDir / name;
                if (!std::filesystem::exists(egg)) {
                    egg = textureDir / "white.png";
                    ++missing;
                }
                spriteLayers.push_back(egg);
            };
            for (int i = 0; i < game::kSpawnEggLayers; ++i) {
                pushEgg(i);
            }
            if (missing > 0) {
                engine::logError(std::to_string(missing) +
                                 " spawn egg sprites are missing and will draw blank -"
                                 " run tools\\make-spawn-egg-sprites.ps1");
            }

            // Resource sprites go after the whole egg run, which is what lets a
            // new one be added without shifting any egg's layer.
            if (spriteLayers.size() != static_cast<std::size_t>(game::kResourceSpritesFirst)) {
                engine::logError("kResourceSpritesFirst is " + std::to_string(game::kResourceSpritesFirst) +
                                 " but " + std::to_string(spriteLayers.size()) +
                                 " layers are loaded - every resource icon will be wrong");
            }
            for (const char* name : {"coal.png", "raw_iron.png", "iron_ingot.png", "raw_gold.png",
                                     "gold_ingot.png", "raw_copper.png", "copper_ingot.png",
                                     "diamond.png", "emerald.png", "lapis_lazuli.png", "redstone.png"}) {
                spriteLayers.push_back(blockTexture(name));
            }

            // And the buckets after those, same arrangement and same reason.
            if (spriteLayers.size() != static_cast<std::size_t>(game::kBucketSpritesFirst)) {
                engine::logError("kBucketSpritesFirst is " + std::to_string(game::kBucketSpritesFirst) +
                                 " but " + std::to_string(spriteLayers.size()) +
                                 " layers are loaded - both bucket icons will be wrong");
            }
            for (const char* name : {"bucket.png", "water_bucket.png"}) {
                spriteLayers.push_back(blockTexture(name));
            }

            // The water surface's frames, last of all. A missing one falls back
            // to the still texture rather than being skipped, because the list
            // index is the layer index.
            if (spriteLayers.size() != static_cast<std::size_t>(game::kWaterFrameFirst)) {
                engine::logError("kWaterFrameFirst is " + std::to_string(game::kWaterFrameFirst) +
                                 " but " + std::to_string(spriteLayers.size()) +
                                 " layers are loaded - the water animation will sample rubbish");
            }
            for (int i = 0; i < game::kWaterFrames; ++i) {
                char name[20]{};
                std::snprintf(name, sizeof(name), "water%02d.png", i);
                spriteLayers.push_back(blockTexture(name));
            }

            // Spawn eggs for the species added after the first thirty-six. A
            // second run right at the end rather than a longer first one: the
            // resource, bucket and water layers sit behind that one, and their
            // item ids are in the player's saved inventory.
            if (spriteLayers.size() != static_cast<std::size_t>(game::kExtraSpawnEggFirst)) {
                engine::logError("kExtraSpawnEggFirst is " + std::to_string(game::kExtraSpawnEggFirst) +
                                 " but " + std::to_string(spriteLayers.size()) +
                                 " layers are loaded - the newest spawn eggs will be wrong");
            }
            const int extraMissingBefore = missing;
            for (int i = 0; i < game::kExtraSpawnEggLayers; ++i) {
                pushEgg(game::kSpawnEggLayers + i);
            }
            if (missing > extraMissingBefore) {
                engine::logError(std::to_string(missing - extraMissingBefore) +
                                 " spawn egg sprites are missing and will draw blank -"
                                 " run tools\\make-spawn-egg-sprites.ps1");
            }

            // The three upper tool tiers and what they are made of, right at the
            // very end. Block textures included: `blockTextureLayer` returns a
            // layer index and nothing cares where it points, so putting the two
            // new blocks here rather than among the first sixty-seven means not
            // one existing layer moves.
            if (spriteLayers.size() != static_cast<std::size_t>(game::kUpgradeToolSpritesFirst)) {
                engine::logError("kUpgradeToolSpritesFirst is " +
                                 std::to_string(game::kUpgradeToolSpritesFirst) + " but " +
                                 std::to_string(spriteLayers.size()) +
                                 " layers are loaded - every new tool icon will be wrong");
            }
            for (const char* name :
                 {"iron_pickaxe.png", "iron_axe.png", "iron_shovel.png", "iron_sword.png", "iron_hoe.png",
                  "diamond_pickaxe.png", "diamond_axe.png", "diamond_shovel.png", "diamond_sword.png",
                  "diamond_hoe.png", "emberite_pickaxe.png", "emberite_axe.png", "emberite_shovel.png",
                  "emberite_sword.png", "emberite_hoe.png", "emberite_scrap.png", "emberite_ingot.png",
                  "ancient_debris_side.png", "ancient_debris_top.png", "emberite_block.png",
                  "smoker_front.png", "smoker_front_on.png", "smoker_side.png", "smoker_top.png",
                  "smithing_top.png", "smithing_front.png", "smithing_side.png",
                  "smithing_bottom.png", "chest_top.png", "chest_front.png", "chest_side.png",
                  // Food, in `ItemId` order, because `itemTextureLayer` maps
                  // the run across by arithmetic rather than by name.
                  "apple.png", "porkchop_raw.png", "porkchop_cooked.png", "beef_raw.png",
                  "beef_cooked.png", "chicken_raw.png", "chicken_cooked.png", "mutton_raw.png",
                  "mutton_cooked.png", "cod_raw.png", "cod_cooked.png",
                  // Three appended blocks, same image on every face.
                  "prismarine.png", "sea_lantern.png", "coarse_dirt.png",
                  // The table-driven run, in `kExtraBlocks` layer order. A name
                  // out of order here gives a block someone else's texture and
                  // nothing catches it but your eyes.
                  "cobbled_deepslate.png", "ice.png", "blue_ice.png", "coal_block.png",
                  "iron_block.png", "gold_block.png", "diamond_block.png", "emerald_block.png",
                  "lapis_block.png", "redstone_block.png", "copper_block.png",
                  "polished_andesite.png", "polished_diorite.png", "polished_granite.png",
                  "chiseled_stone_bricks.png", "mossy_stone_bricks.png", "cracked_stone_bricks.png",
                  "polished_deepslate.png", "deepslate_bricks.png", "deepslate_tiles.png",
                  "smooth_sandstone.png", "cut_sandstone.png", "chiseled_sandstone.png",
                  "tube_coral_block.png", "brain_coral_block.png", "bubble_coral_block.png",
                  "fire_coral_block.png", "horn_coral_block.png", "sponge.png", "wet_sponge.png",
                  "dark_prismarine.png", "prismarine_bricks.png", "spruce_log.png",
                  "spruce_log_top.png", "spruce_leaves.png", "spruce_planks.png", "birch_log.png",
                  "birch_log_top.png", "birch_leaves.png", "birch_planks.png", "cornflower.png",
                  "oxeye_daisy.png", "azure_bluet.png", "allium.png", "red_tulip.png",
                  "orange_tulip.png", "brown_mushroom.png", "red_mushroom.png", "kelp.png",
                  "seagrass.png",
                  // Appended 2026-08-07, still in `kExtraBlocks` layer order.
                  // Colour families run white through black, matching the dyes.
                  "white_wool.png", "orange_wool.png", "magenta_wool.png", "light_blue_wool.png",
                  "yellow_wool.png", "lime_wool.png", "pink_wool.png", "gray_wool.png",
                  "light_gray_wool.png", "cyan_wool.png", "purple_wool.png", "blue_wool.png",
                  "brown_wool.png", "green_wool.png", "red_wool.png", "black_wool.png",
                  "white_concrete.png", "orange_concrete.png", "magenta_concrete.png",
                  "light_blue_concrete.png", "yellow_concrete.png", "lime_concrete.png",
                  "pink_concrete.png", "gray_concrete.png", "light_gray_concrete.png",
                  "cyan_concrete.png", "purple_concrete.png", "blue_concrete.png",
                  "brown_concrete.png", "green_concrete.png", "red_concrete.png",
                  "black_concrete.png",
                  "white_terracotta.png", "orange_terracotta.png", "magenta_terracotta.png",
                  "light_blue_terracotta.png", "yellow_terracotta.png", "lime_terracotta.png",
                  "pink_terracotta.png", "gray_terracotta.png", "light_gray_terracotta.png",
                  "cyan_terracotta.png", "purple_terracotta.png", "blue_terracotta.png",
                  "brown_terracotta.png", "green_terracotta.png", "red_terracotta.png",
                  "black_terracotta.png",
                  "deepslate_coal_ore.png", "deepslate_iron_ore.png", "deepslate_copper_ore.png",
                  "deepslate_gold_ore.png", "deepslate_redstone_ore.png", "deepslate_lapis_ore.png",
                  "deepslate_diamond_ore.png", "deepslate_emerald_ore.png",
                  "jungle_log.png", "jungle_log_top.png", "jungle_leaves.png", "jungle_planks.png",
                  "acacia_log.png", "acacia_log_top.png", "acacia_leaves.png", "acacia_planks.png",
                  "dark_oak_log.png", "dark_oak_log_top.png", "dark_oak_leaves.png",
                  "dark_oak_planks.png", "cherry_log.png", "cherry_log_top.png",
                  "cherry_leaves.png", "cherry_planks.png",
                  "stripped_oak_log.png", "stripped_oak_log_top.png", "stripped_spruce_log.png",
                  "stripped_spruce_log_top.png", "stripped_birch_log.png",
                  "stripped_birch_log_top.png", "stripped_jungle_log.png",
                  "stripped_jungle_log_top.png", "stripped_acacia_log.png",
                  "stripped_acacia_log_top.png", "stripped_dark_oak_log.png",
                  "stripped_dark_oak_log_top.png",
                  "tuff.png", "calcite.png", "dripstone_block.png", "moss_block.png", "mud.png",
                  "packed_mud.png", "mud_bricks.png", "rooted_dirt.png", "amethyst_block.png",
                  "smooth_basalt.png", "basalt_side.png", "basalt_top.png", "magma.png",
                  "honeycomb_block.png", "honey_block_side.png", "honey_block_top.png",
                  "red_sandstone.png", "red_sandstone_top.png", "cut_red_sandstone.png",
                  "chiseled_red_sandstone.png",
                  "pumpkin_side.png", "pumpkin_top.png", "melon_side.png", "melon_top.png",
                  "hay_block_side.png", "hay_block_top.png", "note_block.png", "jukebox_side.png",
                  "jukebox_top.png",
                  "blue_orchid.png", "pink_tulip.png", "white_tulip.png", "lily_of_the_valley.png",
                  "oak_sapling.png", "spruce_sapling.png", "birch_sapling.png",
                  "jungle_sapling.png", "acacia_sapling.png", "dark_oak_sapling.png", "fern.png",
                  "sugar_cane.png", "cobweb.png",
                  // The second table-driven run, layers 176-248. Appended to
                  // the table's own sprites rather than given a run of their
                  // own, because `ExtraBlockInfo::layer` is an offset from
                  // `kTableSpritesFirst` and one origin is easier to keep true
                  // than two.
                  "netherrack.png", "soul_sand.png", "soul_soil.png", "blackstone.png",
                  "blackstone_top.png", "polished_blackstone.png",
                  "polished_blackstone_bricks.png", "chiseled_polished_blackstone.png",
                  "cracked_polished_blackstone_bricks.png", "gilded_blackstone.png",
                  "nether_bricks.png", "red_nether_bricks.png", "cracked_nether_bricks.png",
                  "chiseled_nether_bricks.png", "nether_gold_ore.png", "nether_quartz_ore.png",
                  "quartz_block_side.png", "quartz_block_top.png", "smooth_quartz.png",
                  "chiseled_quartz_block.png", "chiseled_quartz_block_top.png",
                  "quartz_bricks.png", "end_stone.png", "end_stone_bricks.png",
                  "purpur_block.png", "podzol_side.png", "podzol_top.png", "mycelium_side.png",
                  "mycelium_top.png", "dried_kelp_side.png", "dried_kelp_top.png",
                  "slime_block.png", "sculk.png", "budding_amethyst.png", "polished_tuff.png",
                  "tuff_bricks.png", "chiseled_tuff.png", "polished_basalt_side.png",
                  "polished_basalt_top.png", "raw_iron_block.png", "raw_gold_block.png",
                  "raw_copper_block.png", "exposed_copper.png", "weathered_copper.png",
                  "oxidized_copper.png", "cut_copper.png", "exposed_cut_copper.png",
                  "weathered_cut_copper.png", "oxidized_cut_copper.png", "chiseled_copper.png",
                  "reinforced_deepslate_side.png", "reinforced_deepslate_top.png",
                  "chiseled_deepslate.png", "cracked_deepslate_bricks.png",
                  "cracked_deepslate_tiles.png", "smooth_red_sandstone.png",
                  "nether_wart_block.png", "white_concrete_powder.png",
                  "orange_concrete_powder.png", "magenta_concrete_powder.png",
                  "light_blue_concrete_powder.png", "yellow_concrete_powder.png",
                  "lime_concrete_powder.png", "pink_concrete_powder.png",
                  "gray_concrete_powder.png", "light_gray_concrete_powder.png",
                  "cyan_concrete_powder.png", "purple_concrete_powder.png",
                  "blue_concrete_powder.png", "brown_concrete_powder.png",
                  "green_concrete_powder.png", "red_concrete_powder.png",
                  "black_concrete_powder.png",
                  // The third batch, layers 249-326.
                  "cactus_side.png", "cactus_top.png", "bamboo.png", "sweet_berry_bush.png",
                  "glow_lichen.png", "pointed_dripstone.png", "sea_pickle.png",
                  "nether_sprouts.png", "crimson_roots.png", "warped_roots.png",
                  "crimson_fungus.png", "warped_fungus.png", "twisting_vines.png",
                  "weeping_vines.png", "hanging_roots.png", "spore_blossom.png",
                  "amethyst_cluster.png", "large_fern.png", "lily_pad.png",
                  "white_glazed_terracotta.png", "orange_glazed_terracotta.png",
                  "magenta_glazed_terracotta.png", "light_blue_glazed_terracotta.png",
                  "yellow_glazed_terracotta.png", "lime_glazed_terracotta.png",
                  "pink_glazed_terracotta.png", "gray_glazed_terracotta.png",
                  "light_gray_glazed_terracotta.png", "cyan_glazed_terracotta.png",
                  "purple_glazed_terracotta.png", "blue_glazed_terracotta.png",
                  "brown_glazed_terracotta.png", "green_glazed_terracotta.png",
                  "red_glazed_terracotta.png", "black_glazed_terracotta.png",
                  "shroomlight.png", "ochre_froglight_side.png", "ochre_froglight_top.png",
                  "verdant_froglight_side.png", "verdant_froglight_top.png",
                  "pearlescent_froglight_side.png", "pearlescent_froglight_top.png",
                  "crimson_nylium_side.png", "crimson_nylium_top.png", "warped_nylium_side.png",
                  "warped_nylium_top.png", "crimson_stem_side.png", "crimson_stem_top.png",
                  "warped_stem_side.png", "warped_stem_top.png", "crimson_planks.png",
                  "warped_planks.png", "warped_wart_block.png", "mangrove_log_side.png",
                  "mangrove_log_top.png", "mangrove_planks.png", "mangrove_leaves.png",
                  "muddy_mangrove_roots_side.png", "muddy_mangrove_roots_top.png",
                  "bamboo_block_side.png", "bamboo_block_top.png", "bamboo_planks.png",
                  "bamboo_mosaic.png", "bone_block_side.png", "bone_block_top.png",
                  "quartz_pillar_side.png", "quartz_pillar_top.png", "purpur_pillar_side.png",
                  "purpur_pillar_top.png", "target_side.png", "target_top.png", "snow_block.png",
                  "sculk_catalyst_side.png", "sculk_catalyst_top.png", "azalea_side.png",
                  "azalea_top.png", "flowering_azalea_side.png", "flowering_azalea_top.png",
                  "stripped_cherry_log.png", "stripped_cherry_log_top.png",
                  "stripped_mangrove_log.png", "stripped_mangrove_log_top.png",
                  "stripped_crimson_stem.png", "stripped_crimson_stem_top.png",
                  "stripped_warped_stem.png", "stripped_warped_stem_top.png",
                  "stripped_bamboo_block.png", "stripped_bamboo_block_top.png",
                  // The third table run: coloured glass, bars and the lights.
                  "white_stained_glass.png", "orange_stained_glass.png",
                  "magenta_stained_glass.png", "light_blue_stained_glass.png",
                  "yellow_stained_glass.png", "lime_stained_glass.png",
                  "pink_stained_glass.png", "gray_stained_glass.png",
                  "light_gray_stained_glass.png", "cyan_stained_glass.png",
                  "purple_stained_glass.png", "blue_stained_glass.png",
                  "brown_stained_glass.png", "green_stained_glass.png",
                  "red_stained_glass.png", "black_stained_glass.png", "iron_bars.png",
                  "lantern.png", "soul_lantern.png", "soul_torch.png", "redstone_torch.png",
                  "end_rod.png",
                  // The fourth table run: the farm. `dirt.png`, `pumpkin_side`
                  // and `pumpkin_top` appear a second time on purpose - this is
                  // a list of files per layer, so naming one twice costs a
                  // layer and keeps the run's numbering contiguous, which is
                  // far cheaper than reaching back at an earlier layer index.
                  "dirt.png", "farmland.png", "farmland_moist.png", "dirt_path_side.png",
                  "dirt_path_top.png",
                  "wheat_stage0.png", "wheat_stage1.png", "wheat_stage2.png", "wheat_stage3.png",
                  "wheat_stage4.png", "wheat_stage5.png", "wheat_stage6.png", "wheat_stage7.png",
                  "carrots_stage0.png", "carrots_stage1.png", "carrots_stage2.png",
                  "carrots_stage3.png",
                  "potatoes_stage0.png", "potatoes_stage1.png", "potatoes_stage2.png",
                  "potatoes_stage3.png",
                  "beetroots_stage0.png", "beetroots_stage1.png", "beetroots_stage2.png",
                  "beetroots_stage3.png",
                  "melon_stem.png", "attached_melon_stem.png", "pumpkin_stem.png",
                  "attached_pumpkin_stem.png",
                  "nether_wart_stage0.png", "nether_wart_stage1.png", "nether_wart_stage2.png",
                  "pumpkin_side.png", "pumpkin_top.png", "carved_pumpkin.png",
                  "jack_o_lantern.png", "composter_top.png", "composter_side.png",
                  "composter_ready.png",
                  // The fifth table run. The twenty bark blocks name log sides
                  // this list has already staged - a duplicate here costs one
                  // layer and keeps the run contiguous, which is far safer than
                  // reaching back at an earlier index.
                  "log_side.png", "spruce_log.png", "birch_log.png", "jungle_log.png",
                  "acacia_log.png", "dark_oak_log.png", "cherry_log.png", "mangrove_log_side.png",
                  "crimson_stem_side.png", "warped_stem_side.png",
                  "stripped_oak_log.png", "stripped_spruce_log.png", "stripped_birch_log.png",
                  "stripped_jungle_log.png", "stripped_acacia_log.png",
                  "stripped_dark_oak_log.png", "stripped_cherry_log.png",
                  "stripped_mangrove_log.png", "stripped_crimson_stem.png",
                  "stripped_warped_stem.png",
                  "brown_mushroom_block.png", "red_mushroom_block.png", "mushroom_stem.png",
                  "dead_tube_coral_block.png", "dead_brain_coral_block.png",
                  "dead_bubble_coral_block.png", "dead_fire_coral_block.png",
                  "dead_horn_coral_block.png",
                  // Waxed copper is the unwaxed picture again: wax is a promise
                  // that the block will not change, not something you can see.
                  "copper_block.png", "exposed_copper.png", "weathered_copper.png",
                  "oxidized_copper.png", "cut_copper.png", "exposed_cut_copper.png",
                  "weathered_cut_copper.png", "oxidized_cut_copper.png", "chiseled_copper.png",
                  "copper_grate.png", "exposed_copper_grate.png", "weathered_copper_grate.png",
                  "oxidized_copper_grate.png",
                  "copper_bulb.png", "copper_bulb_lit.png", "exposed_copper_bulb.png",
                  "exposed_copper_bulb_lit.png", "weathered_copper_bulb.png",
                  "weathered_copper_bulb_lit.png", "oxidized_copper_bulb.png",
                  "oxidized_copper_bulb_lit.png",
                  "crying_obsidian.png", "powder_snow.png", "suspicious_sand.png",
                  "suspicious_gravel.png", "azalea_leaves.png", "flowering_azalea_leaves.png",
                  "redstone_lamp.png", "redstone_lamp_on.png",
                  "lodestone_side.png", "lodestone_top.png",
                  "enchanting_table_side.png", "enchanting_table_top.png",
                  "chiseled_bookshelf_side.png", "chiseled_bookshelf_top.png",
                  "cartography_table_side.png", "cartography_table_top.png",
                  "fletching_table_side.png", "fletching_table_top.png",
                  "barrel_side.png", "barrel_top.png",
                  "blast_furnace_side.png", "blast_furnace_top.png",
                  "loom_side.png", "loom_top.png",
                  "stonecutter_side.png", "stonecutter_top.png",
                  // `grindstone_pivot.png` is **deliberately absent**, and this
                  // is the only place a reader would look for it. The two
                  // pivots borrow `grindstone_side` (`Block.hpp` says so at the
                  // model), and staging the right picture needs a *new* layer,
                  // not a swap: 473 is also this block's `extraBlockInfo.layer`,
                  // so the icon and the dropped entity paint from it too.
                  //
                  // **When it is picked up, the row does not go here.** This
                  // list's order *is* the atlas order, so where the row sits is
                  // the layer number, and that position is the entire decision.
                  // Inserting it beside its sibling moves 183 literals inside
                  // `Block.hpp`'s table - of which only 3 are pinned by an
                  // assert, because the table *is* the thing a layer would be
                  // checked against - so a single miscount survives `/W4`,
                  // every assert and `check-models.ps1`, and lands as the wrong
                  // texture on several hundred blocks. **Appended to the end of
                  // the table run instead, it moves 3**, and all three are
                  // absolute atlas literals that fail to compile if they are
                  // wrong. The atlas is an unordered bag; nothing here requires
                  // the reference's file order, and `kComposterCompostLayer` is
                  // already a table texture that was appended rather than slotted
                  // in. So: end of the run, in this file and in
                  // `tools/make-reference-blocks.ps1`, matching whatever index
                  // `Block.hpp` takes - **agreed first, landed together.** If
                  // one file inserts and another appends, nothing fails and
                  // every layer after the seam is off by one.
                  "grindstone_side.png", "grindstone_round.png",
                  "lectern_sides.png", "lectern_top.png",
                  "bell_side.png", "bell_top.png",
                  "cauldron_side.png", "cauldron_top.png",
                  "brewing_stand_base.png", "brewing_stand.png",
                  "anvil_base.png", "anvil_top.png", "chipped_anvil_top.png",
                  "damaged_anvil_top.png",
                  "scaffolding_side.png", "scaffolding_top.png", "flower_pot.png",
                  "sculk_vein.png", "sculk_sensor_side.png", "sculk_sensor_top.png",
                  "sculk_shrieker_side.png", "sculk_shrieker_top.png",
                  "small_amethyst_bud.png", "medium_amethyst_bud.png", "large_amethyst_bud.png",
                  "big_dripleaf_top.png", "small_dripleaf_top.png",
                  "cave_vines.png", "cave_vines_lit.png", "moss_block.png",
                  "chorus_plant.png", "chorus_flower.png",
                  "tube_coral.png", "brain_coral.png", "bubble_coral.png", "fire_coral.png",
                  "horn_coral.png", "dead_tube_coral.png", "dead_brain_coral.png",
                  "dead_bubble_coral.png", "dead_fire_coral.png", "dead_horn_coral.png",
                  "tube_coral_fan.png", "brain_coral_fan.png", "bubble_coral_fan.png",
                  "fire_coral_fan.png", "horn_coral_fan.png", "dead_tube_coral_fan.png",
                  "dead_brain_coral_fan.png", "dead_bubble_coral_fan.png",
                  "dead_fire_coral_fan.png", "dead_horn_coral_fan.png",
                  "sunflower_bottom.png", "sunflower_top.png", "lilac_bottom.png", "lilac_top.png",
                  "rose_bush_bottom.png", "rose_bush_top.png", "peony_bottom.png", "peony_top.png",
                  "wither_rose.png",
                  // **The third name here is a replacement, not an addition**,
                  // and it has to stay one: `Block.hpp` pins this run by
                  // literal offset (`kCampfireLogLayer` 534, `kCampfireLitLog`
                  // 535, `kSoulCampfireLitLog` 536) and four `static_assert`s
                  // hold those against the two campfires' `topLayer` rows.
                  // Inserting a name would slide every layer after it and give
                  // several hundred blocks someone else's texture, silently.
                  //
                  // 536 held `soul_campfire_fire.png`, the animated *flame*
                  // sheet, and was read as the soul campfire's *ember log* -
                  // so the blue variant wore a frame of its own fire where its
                  // ordinary twin wears a lit plank. The flame sheet had no
                  // other reader anywhere (the campfire model is five solid
                  // boxes and no flame quad), so swapping the name in place
                  // fixes it and moves nothing.
                  "campfire_log.png", "campfire_log_lit.png", "soul_campfire_log_lit.png",
                  "respawn_anchor_side.png", "respawn_anchor_top.png",
                  // The sixth run: the candles, declared white-first after the
                  // plain one like every other colour family.
                  "candle.png", "white_candle.png", "orange_candle.png", "magenta_candle.png",
                  "light_blue_candle.png", "yellow_candle.png", "lime_candle.png",
                  "pink_candle.png", "gray_candle.png", "light_gray_candle.png",
                  "cyan_candle.png", "purple_candle.png", "blue_candle.png", "brown_candle.png",
                  "green_candle.png", "red_candle.png", "black_candle.png",
                  "tinted_glass.png", "beacon.png", "conduit.png", "dragon_egg.png",
                  "end_portal_frame_side.png", "end_portal_frame_top.png", "spawner.png",
                  "trapped_chest_side.png", "trapped_chest_top.png",
                  "trapped_chest_front.png",
                  "blast_furnace_front.png", "blast_furnace_front_on.png",
                  // The seventeen unlit candles, in the same colour order.
                  "candle_unlit.png", "white_candle_unlit.png", "orange_candle_unlit.png",
                  "magenta_candle_unlit.png", "light_blue_candle_unlit.png",
                  "yellow_candle_unlit.png", "lime_candle_unlit.png", "pink_candle_unlit.png",
                  "gray_candle_unlit.png", "light_gray_candle_unlit.png", "cyan_candle_unlit.png",
                  "purple_candle_unlit.png", "blue_candle_unlit.png", "brown_candle_unlit.png",
                  "green_candle_unlit.png", "red_candle_unlit.png", "black_candle_unlit.png",
                  // Doors then trapdoors, in `kDoorFamilies` order. A door is
                  // bottom picture then top; a trapdoor has only the one.
                  "oak_door_bottom.png", "oak_door_top.png", "spruce_door_bottom.png",
                  "spruce_door_top.png", "birch_door_bottom.png", "birch_door_top.png",
                  "jungle_door_bottom.png", "jungle_door_top.png", "acacia_door_bottom.png",
                  "acacia_door_top.png", "dark_oak_door_bottom.png", "dark_oak_door_top.png",
                  "cherry_door_bottom.png", "cherry_door_top.png", "mangrove_door_bottom.png",
                  "mangrove_door_top.png", "crimson_door_bottom.png", "crimson_door_top.png",
                  "warped_door_bottom.png", "warped_door_top.png", "bamboo_door_bottom.png",
                  "bamboo_door_top.png", "iron_door_bottom.png", "iron_door_top.png",
                  "oak_trapdoor.png", "spruce_trapdoor.png", "birch_trapdoor.png",
                  "jungle_trapdoor.png", "acacia_trapdoor.png", "dark_oak_trapdoor.png",
                  "cherry_trapdoor.png", "mangrove_trapdoor.png", "crimson_trapdoor.png",
                  "warped_trapdoor.png", "bamboo_trapdoor.png", "iron_trapdoor.png",
                  // The sixteen beds, four faces each in the order
                  // `bedFamilyAt` reads them: foot top, foot side, head top,
                  // head side.
                  "white_bed_foot_top.png", "white_bed_foot_side.png",
                  "white_bed_head_top.png", "white_bed_head_side.png",
                  "orange_bed_foot_top.png", "orange_bed_foot_side.png",
                  "orange_bed_head_top.png", "orange_bed_head_side.png",
                  "magenta_bed_foot_top.png", "magenta_bed_foot_side.png",
                  "magenta_bed_head_top.png", "magenta_bed_head_side.png",
                  "light_blue_bed_foot_top.png", "light_blue_bed_foot_side.png",
                  "light_blue_bed_head_top.png", "light_blue_bed_head_side.png",
                  "yellow_bed_foot_top.png", "yellow_bed_foot_side.png",
                  "yellow_bed_head_top.png", "yellow_bed_head_side.png",
                  "lime_bed_foot_top.png", "lime_bed_foot_side.png", "lime_bed_head_top.png",
                  "lime_bed_head_side.png", "pink_bed_foot_top.png", "pink_bed_foot_side.png",
                  "pink_bed_head_top.png", "pink_bed_head_side.png", "gray_bed_foot_top.png",
                  "gray_bed_foot_side.png", "gray_bed_head_top.png", "gray_bed_head_side.png",
                  "light_gray_bed_foot_top.png", "light_gray_bed_foot_side.png",
                  "light_gray_bed_head_top.png", "light_gray_bed_head_side.png",
                  "cyan_bed_foot_top.png", "cyan_bed_foot_side.png", "cyan_bed_head_top.png",
                  "cyan_bed_head_side.png", "purple_bed_foot_top.png",
                  "purple_bed_foot_side.png", "purple_bed_head_top.png",
                  "purple_bed_head_side.png", "blue_bed_foot_top.png",
                  "blue_bed_foot_side.png", "blue_bed_head_top.png", "blue_bed_head_side.png",
                  "brown_bed_foot_top.png", "brown_bed_foot_side.png",
                  "brown_bed_head_top.png", "brown_bed_head_side.png",
                  "green_bed_foot_top.png", "green_bed_foot_side.png",
                  "green_bed_head_top.png", "green_bed_head_side.png", "red_bed_foot_top.png",
                  "red_bed_foot_side.png", "red_bed_head_top.png", "red_bed_head_side.png",
                  "black_bed_foot_top.png", "black_bed_foot_side.png",
                  "black_bed_head_top.png", "black_bed_head_side.png",
                  "ender_chest_side.png", "ender_chest_top.png",
                  "hopper_outside.png", "hopper_top.png",
                  // The seventeen stowboxes, plain then the sixteen dyes.
                  "shulker_box.png", "white_shulker_box.png", "orange_shulker_box.png",
                  "magenta_shulker_box.png", "light_blue_shulker_box.png",
                  "yellow_shulker_box.png", "lime_shulker_box.png", "pink_shulker_box.png",
                  "gray_shulker_box.png", "light_gray_shulker_box.png", "cyan_shulker_box.png",
                  "purple_shulker_box.png", "blue_shulker_box.png", "brown_shulker_box.png",
                  "green_shulker_box.png", "red_shulker_box.png", "black_shulker_box.png",
                  // The double chest's two halves, named for the viewer's left
                  // and right - which is the opposite way round from Mojang's
                  // own file names. Right at the end, so nothing above moves.
                  "chest_left_top.png", "chest_left_front.png", "chest_left_back.png",
                  "chest_right_top.png", "chest_right_front.png", "chest_right_back.png",
                  // The forty-nine appended items, in `ItemId` order.
                  "bread.png", "cookie.png", "melon_slice.png", "carrot.png", "potato.png",
                  "baked_potato.png", "beetroot.png", "sweet_berries.png", "golden_apple.png",
                  "pumpkin_pie.png", "string.png", "feather.png", "leather.png", "bone.png",
                  "gunpowder.png", "slimeball.png", "ink_sac.png", "glow_ink_sac.png",
                  "clay_ball.png", "brick.png", "flint.png", "wheat.png", "wheat_seeds.png",
                  "sugar.png", "paper.png", "book.png", "glass_bottle.png", "bowl.png", "egg.png",
                  "rotten_flesh.png", "spider_eye.png", "honeycomb.png", "honey_bottle.png",
                  "white_dye.png", "orange_dye.png", "magenta_dye.png", "light_blue_dye.png",
                  "yellow_dye.png", "lime_dye.png", "pink_dye.png", "gray_dye.png",
                  "light_gray_dye.png", "cyan_dye.png", "purple_dye.png", "blue_dye.png",
                  "brown_dye.png", "green_dye.png", "red_dye.png", "black_dye.png",
                  // The nine appended items.
                  "lava_bucket.png", "milk_bucket.png", "flint_and_steel.png",
                  "amethyst_shard.png", "quartz.png", "nether_brick_item.png",
                  "glowstone_dust.png", "dried_kelp.png", "magma_cream.png",
                  // The twenty-seven appended items.
                  "glow_berries.png", "rabbit_raw.png", "rabbit_cooked.png", "salmon_raw.png",
                  "salmon_cooked.png", "tropical_fish.png", "pufferfish.png",
                  "beetroot_seeds.png", "melon_seeds.png", "pumpkin_seeds.png", "bone_meal.png",
                  "prismarine_shard.png", "prismarine_crystals.png", "nautilus_shell.png",
                  "heart_of_the_sea.png", "scute.png", "phantom_membrane.png", "cinder_rod.png",
                  "cinder_powder.png", "drifter_tear.png", "void_pearl.png", "void_eye.png",
                  "chorus_fruit.png", "popped_chorus_fruit.png", "rabbit_hide.png",
                  "rabbit_foot.png", "echo_shard.png", "water_bottle.png", "bow.png", "arrow.png",
                  "shears.png", "cocoa_beans.png",
                  // The sixty-one appended items, in `ItemId` order. Armour
                  // runs helmet-chest-legs-boots by rising material, which is
                  // the order the enum uses, so a piece's icon is one offset.
                  //
                  // The number in that first line is a courtesy and drifts - it
                  // said seventy-four while the run held seventy-six. The count
                  // that is actually load-bearing is `kExtraItemSprites`, and
                  // the `spriteLayers.size()` check below is what proves this
                  // list and that constant still agree.
                  "nether_wart.png",
                  "leather_helmet.png", "leather_chestplate.png", "leather_leggings.png",
                  "leather_boots.png", "chainmail_helmet.png", "chainmail_chestplate.png",
                  "chainmail_leggings.png", "chainmail_boots.png", "iron_helmet.png",
                  "iron_chestplate.png", "iron_leggings.png", "iron_boots.png",
                  "golden_helmet.png", "golden_chestplate.png", "golden_leggings.png",
                  "golden_boots.png", "diamond_helmet.png", "diamond_chestplate.png",
                  "diamond_leggings.png", "diamond_boots.png", "emberite_helmet.png",
                  "emberite_chestplate.png", "emberite_leggings.png", "emberite_boots.png",
                  "turtle_helmet.png", "shield.png",
                  // **The fifteen `music_disc_*` rows that stood here are gone.**
                  // They were a second, hand-written disc run whose ids
                  // duplicated the real `MusicDiscFirst..MusicDiscLast` one -
                  // which is the loop over `music_disc_%02d.png` in the
                  // collectibles section below, twenty-two of them, and is the
                  // run that stays. Different file names, so nothing above or
                  // below moved by accident; `kExtraItemSprites` went 166 to
                  // 151 in `Block.hpp` to match, and the `spriteLayers.size()`
                  // check is what catches it if these two ever disagree again.
                  "saddle.png", "name_tag.png", "lead.png", "elytra.png", "totem_of_undying.png",
                  "spyglass.png", "brush.png", "trident.png", "crossbow.png", "fishing_rod.png",
                  "compass.png", "clock.png", "empty_map.png", "filled_map.png",
                  "recovery_compass.png", "firework_rocket.png", "writable_book.png",
                  "written_book.png",
                  "mushroom_stew.png", "beetroot_soup.png", "rabbit_stew.png",
                  "suspicious_stew.png", "enchanted_golden_apple.png", "poisonous_potato.png",
                  "golden_carrot.png", "glistering_melon_slice.png",
                  "powder_snow_bucket.png", "cod_bucket.png", "salmon_bucket.png",
                  "tropical_fish_bucket.png", "pufferfish_bucket.png", "axolotl_bucket.png",
                  "iron_nugget.png", "gold_nugget.png",
                  // The beehive. Its front changes when it fills, which is the
                  // only visible difference between an empty hive and a full one.
                  "beehive_front.png", "beehive_front_honey.png", "beehive_side.png",
                  "beehive_end.png",
                  // Lava, fire and TNT.
                  "lava.png", "fire.png", "tnt_top.png", "tnt_bottom.png", "tnt_side.png",
                  "tnt_primed_top.png", "tnt_primed_bottom.png", "tnt_primed_side.png",
                  // Fire's 32 animation frames, swapped in at run time.
                  "fire00.png", "fire01.png", "fire02.png", "fire03.png", "fire04.png",
                  "fire05.png", "fire06.png", "fire07.png", "fire08.png", "fire09.png",
                  "fire10.png", "fire11.png", "fire12.png", "fire13.png", "fire14.png",
                  "fire15.png", "fire16.png", "fire17.png", "fire18.png", "fire19.png",
                  "fire20.png", "fire21.png", "fire22.png", "fire23.png", "fire24.png",
                  "fire25.png", "fire26.png", "fire27.png", "fire28.png", "fire29.png",
                  "fire30.png", "fire31.png",
                  // The bow being drawn, and the arrow's own sheet in flight.
                  "bow_pulling_0.png", "bow_pulling_1.png", "bow_pulling_2.png",
                  "arrow_entity.png", "ladder.png", "vine.png", "cocoa_stage0.png",
                  "cocoa_stage1.png", "cocoa_stage2.png",
                  // The moon, in the order it runs through its phases.
                  "moon_full.png", "moon_waning_gibbous.png", "moon_third_quarter.png",
                  "moon_waning_crescent.png", "moon_new.png", "moon_waxing_crescent.png",
                  "moon_first_quarter.png", "moon_waxing_gibbous.png",
                  // The stonecutter's saw blade, appended after every existing
                  // run so nothing above it moves.
                  "stonecutter_saw.png",
                  // The end face of a bed's head, shared by all sixteen colours.
                  "bed_head_north.png",
                  // The compost in a composter below its ready level.
                  "composter_compost.png",
                  // ---- Redstone, appended after every existing run. ----
                  // Sixteen copies of the dust, each tinted at staging time by
                  // the strength it stands for, because the reference tints it
                  // at draw time and nothing here can.
                  "redstone_dust_00.png", "redstone_dust_01.png", "redstone_dust_02.png",
                  "redstone_dust_03.png", "redstone_dust_04.png", "redstone_dust_05.png",
                  "redstone_dust_06.png", "redstone_dust_07.png", "redstone_dust_08.png",
                  "redstone_dust_09.png", "redstone_dust_10.png", "redstone_dust_11.png",
                  "redstone_dust_12.png", "redstone_dust_13.png", "redstone_dust_14.png",
                  "redstone_dust_15.png",
                  "redstone_torch_off.png", "lever.png",
                  "repeater.png", "repeater_on.png", "comparator.png", "comparator_on.png",
                  "redstone_slab.png",
                  "observer_front.png", "observer_back.png", "observer_back_on.png",
                  "observer_side.png", "observer_top.png",
                  "piston_top.png", "piston_top_sticky.png", "piston_side.png",
                  "piston_bottom.png", "piston_inner.png",
                  "dispenser_front.png", "dispenser_front_vertical.png",
                  "dropper_front.png", "dropper_front_vertical.png",
                  "machine_side.png", "machine_top.png",
                  "daylight_detector_side.png", "daylight_detector_top.png",
                  "daylight_detector_inverted_top.png",
                  "lightning_rod.png", "lightning_rod_on.png",
                  "tripwire_hook.png", "tripwire.png",
                  // The four rail families, quiet then live - except the plain
                  // one, whose second picture is the corner it alone can bend
                  // into.
                  "rail.png", "rail_corner.png", "powered_rail.png", "powered_rail_on.png",
                  "detector_rail.png", "detector_rail_on.png", "activator_rail.png",
                  "activator_rail_on.png",
                  "redstone_lamp_on.png"}) {
                spriteLayers.push_back(blockTexture(name));
            }
            if (spriteLayers.size() !=
                static_cast<std::size_t>(game::kRedstoneSpritesFirst + game::kRedstoneSprites)) {
                engine::logError("appended sprites end at " + std::to_string(spriteLayers.size()) +
                                 " but the layer constants say " +
                                 std::to_string(game::kRedstoneSpritesFirst +
                                                game::kRedstoneSprites));
            }

            // ---- Brewing. ----
            // **Loops rather than a hundred and twenty-four more names in that
            // list.** The potion pictures are generated in the staging script
            // from one bottle and forty-one tints, so their names are already
            // arithmetic; writing them out by hand in a fixed order is exactly
            // where an off-by-one hides, and the bed's sixty-four layers proved
            // it once already.
            // **Three rows shorter than it was.** `blaze_rod`, `blaze_powder`
            // and `ghast_tear` were Mojang's names for ids the project already
            // has under its own coined ones - `CinderRod`, `CinderPowder` and
            // `DrifterTear`, whose `cinder_rod.png`, `cinder_powder.png` and
            // `drifter_tear.png` sit in the appended-items list above. Two
            // pictures for one id is two chances to draw the wrong one, and
            // only the coined names may ship. `kBrewingSprites` is 2 to match.
            for (const char* name : {"fermented_spider_eye.png", "dragon_breath.png"}) {
                spriteLayers.push_back(blockTexture(name));
            }
            {
                char name[40];
                for (int i = 0; i < game::kPotionSpriteTypes; ++i) {
                    std::snprintf(name, sizeof(name), "potion_%02d.png", i);
                    spriteLayers.push_back(blockTexture(name));
                }
                for (int i = 0; i < game::kPotionSpriteTypes; ++i) {
                    std::snprintf(name, sizeof(name), "splash_potion_%02d.png", i);
                    spriteLayers.push_back(blockTexture(name));
                }
                for (int i = game::kFirstTippedPotion; i < game::kPotionSpriteTypes; ++i) {
                    std::snprintf(name, sizeof(name), "tipped_arrow_%02d.png", i);
                    spriteLayers.push_back(blockTexture(name));
                }
                for (int i = 0; i < game::kPotionSpriteTypes; ++i) {
                    std::snprintf(name, sizeof(name), "lingering_potion_%02d.png", i);
                    spriteLayers.push_back(blockTexture(name));
                }
            }
            if (spriteLayers.size() != static_cast<std::size_t>(game::kPotionSpritesEnd)) {
                engine::logError("brewing sprites end at " + std::to_string(spriteLayers.size()) +
                                 " but the layer constants say " +
                                 std::to_string(game::kPotionSpritesEnd));
            }

            // ---- Collectibles. ----
            // Sherds and discs are loops for the reason the potions are: the
            // staging script names them by index, so the order lives in one
            // place rather than in two lists that can drift apart.
            {
                char name[32];
                for (const char* sherd :
                     {"angler", "archer", "arms_up", "blade", "brewer", "burn", "danger",
                      "explorer", "flow", "friend", "guster", "heart", "heartbreak", "howl",
                      "miner", "mourner", "plenty", "prize", "scrape", "sheaf", "shelter", "skull",
                      "snort"}) {
                    std::snprintf(name, sizeof(name), "sherd_%s.png", sherd);
                    spriteLayers.push_back(blockTexture(name));
                }
                spriteLayers.push_back(blockTexture("goat_horn.png"));
                for (int i = 0; i < game::kMusicDiscSprites; ++i) {
                    std::snprintf(name, sizeof(name), "music_disc_%02d.png", i);
                    spriteLayers.push_back(blockTexture(name));
                }
                for (int i = 0; i < game::kFireworkStarSprites; ++i) {
                    std::snprintf(name, sizeof(name), "firework_star_%02d.png", i);
                    spriteLayers.push_back(blockTexture(name));
                }
                for (int i = 0; i < game::kDestroyStages; ++i) {
                    std::snprintf(name, sizeof(name), "destroy_stage_%d.png", i);
                    spriteLayers.push_back(blockTexture(name));
                }
            }
            if (spriteLayers.size() != static_cast<std::size_t>(game::kDestroyStagesEnd)) {
                engine::logError("sprite layers end at " + std::to_string(spriteLayers.size()) +
                                 " but the layer constants say " +
                                 std::to_string(game::kDestroyStagesEnd));
            }
        }

        // Slice 2 proof: the catalogue's two lists exist and are the right
        // shape. Both are derived rather than written out, so a wrong count
        // here means a block, a species or a recipe was added without the
        // derivation seeing it.
        {
            std::array<int, static_cast<std::size_t>(game::ItemCategory::Count)> perCategory{};
            for (const game::ItemId item : game::allItems()) {
                ++perCategory[static_cast<std::size_t>(game::categoryFor(item))];
            }
            std::string breakdown;
            for (std::size_t i = 0; i < perCategory.size(); ++i) {
                breakdown += std::string(i == 0 ? "" : ", ") +
                             game::categoryName(static_cast<game::ItemCategory>(i)) + " " +
                             std::to_string(perCategory[i]);
            }
            int twoByTwo = 0;
            for (const game::Recipe& recipe : game::recipes()) {
                twoByTwo += recipe.fitsInTwoByTwo ? 1 : 0;
            }
            engine::logInfo("Catalogue: " + std::to_string(game::allItems().size()) + " items (" + breakdown +
                            "), " + std::to_string(game::recipes().size()) + " recipes, " +
                            std::to_string(twoByTwo) + " of them craftable without a table");
        }

        // Reference skins are the art, not a stand-in for it. The 2026-08-17
        // ruling is explicit - private repository, personal use, nothing ships,
        // Mojang's files stay - so "placeholder art for every species whose own
        // skin has not been drawn yet", which stood here, described a queue that
        // does not exist. Nobody is drawing a replacement and no later build
        // swaps one in.
        //
        // They sit beside the exe rather than under assets/ because a tool
        // stages them out of a reference tree the repository does not carry: the
        // committed `creatures.png` is the default and the staged sheet
        // overrides it when it exists, which is what lets a fresh checkout run
        // at all.
        std::filesystem::path skinTexture = textureDir.parent_path() / "creatures.png";
        const std::filesystem::path referenceSkins = engine::executableDirectory() / "creatures-reference.png";
        if (std::filesystem::exists(referenceSkins)) {
            skinTexture = referenceSkins;
        }

        // PNG stores width and height as big-endian at bytes 16-23. A sheet of
        // the wrong size does not fail, it slides every UV and mistextures
        // everything reading from it - which is exactly what a stale atlas did
        // once, for a whole session, with nothing anywhere reporting it.
        const auto pngSize = [](const std::filesystem::path& path) {
            std::ifstream png(path, std::ios::binary);
            unsigned char header[24]{};
            if (!png.read(reinterpret_cast<char*>(header), sizeof(header))) {
                return std::pair<int, int>{0, 0};
            }
            const auto be32 = [](const unsigned char* p) {
                return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
            };
            return std::pair<int, int>{be32(header + 16), be32(header + 20)};
        };

        {
            const auto [width, height] = pngSize(skinTexture);
            if (width != 0 && (width != game::kCreatureSheetWidth || height != game::kCreatureSheetHeight)) {
                engine::logError("Creature sheet " + skinTexture.filename().string() + " is " +
                                 std::to_string(width) + "x" + std::to_string(height) + ", expected " +
                                 std::to_string(game::kCreatureSheetWidth) + "x" +
                                 std::to_string(game::kCreatureSheetHeight) +
                                 " - every creature will be mistextured. Re-run tools\\make-creature-skins.ps1"
                                 " and tools\\make-reference-creature-atlas.ps1");
            }
        }

        // The HUD sheet, and the proof atlas that stands in for the parts whose
        // art has not been authored yet. Same arrangement as the creature
        // skins: beside the exe, never under assets/, preferred when present.
        std::filesystem::path hudTexture = textureDir.parent_path() / "hud.png";
        const std::filesystem::path referenceHud = engine::executableDirectory() / "hud-reference.png";
        if (std::filesystem::exists(referenceHud)) {
            hudTexture = referenceHud;
        }
        {
            const auto [width, height] = pngSize(hudTexture);
            const auto expectedWidth = static_cast<int>(game::hud::kSheetSize.x);
            const auto expectedHeight = static_cast<int>(game::hud::kSheetSize.y);
            if (width != 0 && (width != expectedWidth || height != expectedHeight)) {
                engine::logError("HUD sheet " + hudTexture.filename().string() + " is " + std::to_string(width) +
                                 "x" + std::to_string(height) + ", expected " + std::to_string(expectedWidth) +
                                 "x" + std::to_string(expectedHeight) +
                                 " - every HUD sprite will be skewed. Re-run tools\\make-hud-sheet.ps1"
                                 " and tools\\make-reference-hud.ps1");
            }
        }

        // The font, on the same arrangement: the reference's own `ascii.png`
        // beside the exe, ours under assets/ as the fallback. Both are 128x128
        // grids of 8x8 cells indexed by codepoint, so either one drops into the
        // other's place.
        std::filesystem::path fontTexture = textureDir.parent_path() / "font.png";
        const std::filesystem::path referenceFont = engine::executableDirectory() / "font-reference.png";
        if (std::filesystem::exists(referenceFont)) {
            fontTexture = referenceFont;
        }
        {
            const auto [width, height] = pngSize(fontTexture);
            const auto expected = static_cast<int>(game::hud::kFontSheetSize.x);
            if (width != 0 && (width != expected || height != expected)) {
                engine::logError("Font atlas " + fontTexture.filename().string() + " is " +
                                 std::to_string(width) + "x" + std::to_string(height) + ", expected " +
                                 std::to_string(expected) + "x" + std::to_string(expected) +
                                 " - every glyph will be wrong. Re-run tools\\make-font.ps1");
            }
        }

        // **Every name `blockTexture` could not find, reported once, here.**
        //
        // It has to be the last thing before the array is handed to the
        // renderer, because that lambda is called from the first block name
        // down to the last redstone sprite roughly seven hundred lines below
        // where the first run ends - and it appends as it goes. Draining the
        // list where `blockTextures` was built reported the first sixty-seven
        // names and silently ignored the thousand after them, which is the
        // opposite of what this check is for: a name out of order there gives a
        // block someone else's texture, and this line is the only thing besides
        // a pair of eyes that catches it.
        //
        // **The single edit that breaks this:** move it back above any call to
        // `blockTexture`, or add a sprite run below it.
        if (!missingTextures.empty()) {
            std::string names;
            for (const std::string& name : missingTextures) {
                names += (names.empty() ? "" : ", ") + name;
            }
            engine::logError(std::to_string(missingTextures.size()) +
                             " block textures are missing and will draw blank: " + names);
        }

        // **And every name served out of our own art instead, drained at the
        // same site and for the same reason.** A texture found here loaded
        // fine, so nothing above this line has anything to report - which is
        // precisely the failure: the game comes up looking wrong and every log
        // reads clean, so the first detector is the user opening the window.
        //
        // A warning rather than an error, and that ranking is deliberate: a
        // blank layer is broken, whereas this is *drawn but not the reference*.
        // Both are worth a line and only one of them stops you playing. The
        // message names the exact staging tool because a diagnostic that does
        // not say what to type is half a diagnostic - `pushEgg` names
        // `make-spawn-egg-sprites.ps1` ninety lines below and the font check
        // names `make-font.ps1` just above, so this names the one that fills
        // `blocks-reference/`. **Checked against the directory rather than
        // recalled**: `tools\run.ps1` does not exist, only a root `run.ps1`,
        // and a diagnostic that tells you to run a missing script is worse than
        // one that tells you nothing.
        //
        // `white.png` is excluded and is the only exclusion. It is a 16x16
        // solid swatch multiplied by a vertex colour to draw the crosshair, the
        // block outline, HUD panels, the inventory dim and particles - a
        // rendering primitive that the reference has no counterpart for and
        // never will, so listing it would put a permanent false positive at the
        // top of every launch and teach the reader to skip the line.
        //
        // **Falsified by**: a second name appearing here on a fully staged
        // tree. That is the same falsifier the paragraph above `referenceBlocks`
        // carries, except this one fires in the log at runtime instead of
        // waiting for somebody to re-run the three-set comparison by hand.
        {
            std::string ownNames;
            std::size_t ownCount = 0;
            for (const std::string& name : ownArtTextures) {
                if (name == "white.png") {
                    continue;
                }
                ownNames += (ownNames.empty() ? "" : ", ") + name;
                ++ownCount;
            }
            if (ownCount > 0) {
                engine::logWarn(std::to_string(ownCount) +
                                " block textures fell back to our own art because the reference"
                                " has not been staged for them - re-run"
                                " tools\\make-reference-blocks.ps1: " +
                                ownNames);
            }
        }

        engine::Renderer renderer(context, window, spriteLayers, hudTexture, fontTexture, skinTexture);

        // What each texture layer is made of. Built by walking every block, face
        // and facing rather than being authored per layer, so a new block gets a
        // material the day it is added.
        {
            const game::MaterialTable materials = game::buildMaterialTable(renderer.textureLayerCount());
            renderer.setMaterialTable(materials.rows);
            if (materials.conflicts != 0) {
                engine::logWarn("Material table: " + std::to_string(materials.conflicts) +
                                " texture layers are claimed by two different material families; one of them "
                                "is being ignored. First is layer " +
                                std::to_string(materials.firstConflictLayer) + ", held by family " +
                                std::to_string(materials.firstConflictHeld) + " and wanted by " +
                                std::to_string(materials.firstConflictWanted) + ".");
            }
        }

        // How wide each glyph actually is, measured off the atlas that loaded
        // rather than written down: the rightmost opaque column of a cell, plus
        // one texel of spacing. That is the reference's own rule, and it
        // reproduces its published widths exactly - 'i' 2, 'l' 3, 'I' 4, 'a' 6,
        // '@' 7. A blank cell has no column to measure, so the space is the one
        // advance that has to be a number.
        {
            const std::uint32_t cell = static_cast<std::uint32_t>(game::hud::kFontCell);
            const std::uint32_t columns = static_cast<std::uint32_t>(game::hud::kFontColumns);
            std::array<std::uint8_t, 128> advances{};
            advances.fill(6);
            if (renderer.fontWidth() >= cell * columns) {
                for (std::size_t code = 0; code < advances.size(); ++code) {
                    const auto cellX = static_cast<std::uint32_t>(code % columns) * cell;
                    const auto cellY = static_cast<std::uint32_t>(code / columns) * cell;
                    int rightmost = -1;
                    for (std::uint32_t x = 0; x < cell; ++x) {
                        for (std::uint32_t y = 0; y < cell; ++y) {
                            if (renderer.fontAlphaAt(cellX + x, cellY + y) != 0) {
                                rightmost = static_cast<int>(x);
                                break;
                            }
                        }
                    }
                    advances[code] = static_cast<std::uint8_t>(rightmost < 0 ? 4 : rightmost + 2);
                }
            }
            advances[' '] = 4;
            game::hud::setFontAdvances(advances);
        }

        // The silhouette of every sprite, taken once from what the renderer just
        // loaded. A dropped tool is extruded from this rather than drawn flat,
        // so it needs the shape the art cuts out, not the art itself.
        const game::SpriteMask spriteMask = [&renderer] {
            const std::uint32_t width = renderer.textureWidth();
            const std::uint32_t height = renderer.textureHeight();
            const std::uint32_t layers = renderer.textureLayerCount();
            std::vector<std::uint8_t> alpha(static_cast<std::size_t>(width) * height * layers);
            std::size_t index = 0;
            for (std::uint32_t layer = 0; layer < layers; ++layer) {
                for (std::uint32_t y = 0; y < height; ++y) {
                    for (std::uint32_t x = 0; x < width; ++x) {
                        alpha[index++] = renderer.textureAlphaAt(layer, x, y);
                    }
                }
            }
            return game::SpriteMask{static_cast<int>(width), static_cast<int>(height), std::move(alpha)};
        }();

        std::size_t capIndex = kDefaultFpsCapIndex;
        for (std::size_t i = 0; i < kFpsCapOptions.size(); ++i) {
            if (kFpsCapOptions[i] == static_cast<int>(settings.frameCap)) {
                capIndex = i;
                break;
            }
        }
        engine::FrameLimiter frameLimiter(kFpsCapOptions[capIndex]);

        engine::Camera camera;
        camera.yaw = -1.57f;
        camera.pitch = -0.15f;
        window.setCursorCaptured(true);

        const auto buildStart = std::chrono::steady_clock::now();
        game::World world(kWorldSeed, engine::executableDirectory() / "saves", jobs,
                          static_cast<int>(settings.renderDistance));
        world.setDetailRadius(static_cast<int>(settings.detailDistance));

        // How far entity geometry is built. Zero means everything, which is
        // what the tier being off has to mean for creatures and drops as much
        // as for chunks - one number turns the whole feature off.
        const auto entityDrawDistance = [&] {
            return world.detailRadius() >= world.visibleRadius()
                       ? 0.0f
                       : static_cast<float>(world.detailRadius() * game::Chunk::kSize);
        };

        // Far enough to reach the diagonal corner of the furthest drawn chunk,
        // or the world visibly clips into a dome at high render distances.
        renderer.setFarPlane(static_cast<float>(world.loadRadius() * game::Chunk::kSize) * 1.8f);

        // Spawn is chosen before any chunk exists, so the surface height comes
        // straight from the generator rather than from loaded blocks.
        //
        // **The requested column may be seabed, and ours was.** The player
        // started with their eyes below the waterline, drowning before the world
        // had finished loading. The reference searches outward for somewhere to
        // stand rather than trusting the coordinate it was handed, and a cave
        // mouth is no good either - that is a hole, not ground.
        int spawnX = settings.spawnX;
        int spawnZ = settings.spawnZ;
        {
            constexpr int kStep = 8;
            constexpr int kMaxSearch = 512;
            const auto standable = [](int x, int z) {
                return game::surfaceHeightAt(kWorldSeed, x, z) > game::kSeaLevel + 1 &&
                       !game::surfaceCarvedAt(kWorldSeed, x, z);
            };
            for (int radius = 0; radius <= kMaxSearch && !standable(spawnX, spawnZ);
                 radius += kStep) {
                for (int dz = -radius; dz <= radius; dz += kStep) {
                    for (int dx = -radius; dx <= radius; dx += kStep) {
                        // Ring only; the inside was covered by a smaller radius.
                        if (radius > 0 && std::abs(dx) != radius && std::abs(dz) != radius) {
                            continue;
                        }
                        if (standable(settings.spawnX + dx, settings.spawnZ + dz)) {
                            spawnX = settings.spawnX + dx;
                            spawnZ = settings.spawnZ + dz;
                            break;
                        }
                    }
                }
            }
            if (spawnX != static_cast<int>(settings.spawnX) ||
                spawnZ != static_cast<int>(settings.spawnZ)) {
                engine::logInfo("Spawn moved to dry ground at " + std::to_string(spawnX) + ", " +
                                std::to_string(spawnZ));
            }
        }

        const glm::vec3 spawn{static_cast<float>(spawnX) + 0.5f,
                              static_cast<float>(game::surfaceHeightAt(kWorldSeed, spawnX, spawnZ) + 1),
                              static_cast<float>(spawnZ) + 0.5f};

        // A chunk owns two meshes: its opaque geometry and its water, which has
        // to be drawn in a separate pass.
        struct ChunkHandles {
            engine::MeshHandle opaque = engine::kInvalidMesh;
            engine::MeshHandle translucent = engine::kInvalidMesh;
        };
        std::unordered_map<game::ChunkCoord, ChunkHandles> chunkMeshes;

        // Triangles per chunk as currently installed. A snapshot rather than a
        // determinism check: the world now loads progressively, so the moment it
        // is read varies by a few hundredths of a percent.
        std::unordered_map<game::ChunkCoord, std::size_t> trianglesPerChunk;

        // Applies mesh changes and is the only place handles are created or
        // released. Anything that removes a chunk without going through here
        // would leak its GPU buffers for the rest of the session.
        const auto applyUpdates = [&](const std::vector<game::ChunkMeshUpdate>& updates) {
            for (const game::ChunkMeshUpdate& update : updates) {
                const auto existing = chunkMeshes.find(update.coord);

                if (update.removed) {
                    if (existing != chunkMeshes.end()) {
                        if (existing->second.opaque != engine::kInvalidMesh) {
                            renderer.removeMesh(existing->second.opaque);
                        }
                        if (existing->second.translucent != engine::kInvalidMesh) {
                            renderer.removeMesh(existing->second.translucent);
                        }
                        chunkMeshes.erase(existing);
                    }
                    continue;
                }

                ChunkHandles& handles =
                    existing != chunkMeshes.end() ? existing->second : chunkMeshes[update.coord];

                // `shapedTail` is only ever non-empty for the translucent buffer:
                // the shape pass emits a whole non-cube block's faces together, so
                // they belong to no face direction and are appended after the six
                // that do. The renderer draws that tail **last**, because a pane is
                // far more often in front of water than inside it. Defaulted on
                // both sides, which is why this pass-through could go missing and
                // still compile - stained panes simply drew in emission order.
                const auto apply = [&](engine::MeshHandle& handle, const engine::MeshData& mesh,
                                       bool translucent,
                                       const engine::MeshIndexRange& shapedTail = {}) {
                    if (handle != engine::kInvalidMesh) {
                        renderer.updateMesh(handle, mesh, shapedTail);
                    } else if (!mesh.empty()) {
                        handle = renderer.addMesh(mesh, translucent, shapedTail);
                    }
                };

                apply(handles.opaque, update.mesh, false);
                apply(handles.translucent, update.translucentMesh, true,
                      engine::MeshIndexRange{update.translucentShaped.first,
                                             update.translucentShaped.count});
                trianglesPerChunk[update.coord] = update.mesh.indices.size() / 3;
            }
        };

        // **Where the world has to be streamed around**, which a resumed save can
        // put thousands of blocks from `spawn_x`. Read before the loading screen
        // rather than after it: loading around the generator's spawn and then
        // dropping the player somewhere else means the bar was measuring a piece
        // of world nobody was about to stand in, and the real one streamed in
        // underneath you while you played.
        game::Player player;
        const std::optional<game::SavedPlayer> savedPlayer = world.store().loadPlayer();
        if (savedPlayer.has_value()) {
            // **Checked, not trusted - the same rule as the health below, which
            // states it out loud and then stopped one line short of where it
            // started.** These three were the only saved player floats nobody
            // looked at, and the round trip is closed: `saveEverything` writes
            // `player.position` verbatim, so one NaN produced anywhere in the
            // movement code is written to disk and read straight back on every
            // launch afterwards. `static_cast<int>` of a NaN is undefined
            // behaviour, so the world does not merely load wrong, it stays
            // wrong - see `kMaxSavedCoord`.
            const glm::vec3 where = savedPlayer->position;
            const bool placeable = std::isfinite(where.x) && std::isfinite(where.y) &&
                                   std::isfinite(where.z) && std::fabs(where.x) <= kMaxSavedCoord &&
                                   std::fabs(where.y) <= kMaxSavedCoord &&
                                   std::fabs(where.z) <= kMaxSavedCoord;
            if (!placeable) {
                engine::logError("The saved player position is not a place - putting you back at "
                                 "spawn rather than trusting it.");
            }
            player.position = placeable ? where : spawn;
            // **Applied through `addLook` rather than assigned**, because that
            // is the one owner of how far up and down a look may go: a second
            // copy of the limit here is a second thing to get wrong the day it
            // changes. Zeroed first so the delta *is* the saved angle.
            camera.yaw = 0.0f;
            camera.pitch = 0.0f;
            if (std::isfinite(savedPlayer->yaw) && std::isfinite(savedPlayer->pitch)) {
                camera.addLook(std::fmod(savedPlayer->yaw, kTwoPi), savedPlayer->pitch);
            }
            // Clamped on the way in rather than trusted: this is the one place
            // the game reads bytes it did not write this run, and a health of
            // zero out of a corrupt file would kill you on the first frame.
            player.health = std::clamp(savedPlayer->health, 1, game::survival::kMaxHealth);
            player.food = std::clamp(savedPlayer->food, 0, game::survival::kMaxFood);
            player.saturation =
                game::survival::clampSaturation(std::max(0.0f, savedPlayer->saturation), player.food);
            player.exhaustion = std::clamp(savedPlayer->exhaustion, 0.0f,
                                           game::survival::kExhaustionPerLevel);
            // **The other end of the effect round trip.** Checked on the way in
            // for the same reason as the health above - these are bytes this
            // run did not write.
            for (const game::SavedEffect& record : savedPlayer->effects) {
                if (record.id <= 0 ||
                    record.id >= static_cast<std::int32_t>(game::effects::Effect::Count)) {
                    continue;
                }
                const auto id = static_cast<game::effects::Effect>(record.id);
                // An id this build does not name - a file written by a later
                // build, or a corrupt one. `effectInfo` answers an empty name
                // for those rather than reading off the end of anything, which
                // is what makes this a test and not a hope. `Effects::apply`
                // refuses the instant ones on its own, so they need no second
                // test here.
                if (game::effects::effectInfo(id).name[0] == '\0') {
                    continue;
                }
                if (!std::isfinite(record.secondsLeft) || record.secondsLeft <= 0.0f) {
                    continue;
                }
                player.effects.apply(id, std::max(0, record.amplifier), record.secondsLeft);
            }
            // **The pool, clamped against the grant that is now running rather
            // than against a number written here.** `Player::update` only ever
            // raises it to `effects::absorptionPoints`, so the pool can never
            // honestly exceed that, and a corrupt file offering a thousand
            // hearts is refused by arithmetic rather than by a second constant.
            player.absorption =
                std::clamp(std::isfinite(savedPlayer->absorption) ? savedPlayer->absorption : 0.0f,
                           0.0f, game::effects::absorptionPoints(player.effects));
            // **And the second half of the pair, which is derived rather than
            // saved, and without it this fix would be an exploit.**
            // `Player::update` reconciles the pool by comparing the effect's
            // remaining seconds against `absorptionSeconds`, and treats a rise
            // as "it was granted again" - so leaving this at zero makes the
            // very first tick after a load look like a fresh golden apple and
            // refill the pool to full. Spend your absorption hearts, quit,
            // come back and they are all there again.
            //
            // It needs no field on disk: what the tick would have left here is
            // exactly the restored effect's own remaining seconds, so it comes
            // off the one table that owns it.
            player.absorptionSeconds =
                player.effects.secondsLeft(game::effects::Effect::Absorption);
        } else {
            player.position = spawn;
        }

        // The world streams in with the window already alive and drawing, rather
        // than blocking before the first frame. Queuing every chunk at once
        // pinned all cores, took the peak memory before anything was on screen,
        // and left the window unresponsive long enough for Windows to say so.
        {
            float shown = 0.0f;
            float lastDrawn = -1.0f;
            float stalled = 0.0f;
            const char* lastPhase = nullptr;
            auto lastTick = std::chrono::steady_clock::now();

            while (!window.shouldClose()) {
                window.pollEvents();

                const auto now = std::chrono::steady_clock::now();
                const float delta = std::chrono::duration<float>(now - lastTick).count();
                lastTick = now;

                const std::vector<game::ChunkMeshUpdate> batch =
                    world.update(player.position, kLoadingBudgetSeconds);
                applyUpdates(batch);

                // Eased toward each checkpoint rather than snapped to it: the
                // measurements arrive in coarse steps and an unsmoothed bar
                // jerks between them.
                const game::World::LoadStatus status = world.loadStatus();
                const float target = world.initialLoadProgress();
                shown += (target - shown) * std::min(1.0f, delta * kLoadingBarEase);
                // A checkpoint can dip when the streamer re-centres, and a bar
                // that walks backwards reads as a fault rather than as honesty.
                shown = std::max(shown, lastDrawn);

                if (status.complete && shown > 0.998f) {
                    break;
                }

                // `settled` means there is genuinely no work left anywhere, so
                // if the checkpoints still disagree, nothing is ever going to
                // move them. Waiting on that is a hang; proceeding with a named
                // fault is recoverable. It cannot fire during an ordinary slow
                // load, because a slow load always has work outstanding.
                if (status.settled && !status.complete) {
                    stalled += delta;
                    if (stalled > kLoadingStallSeconds) {
                        engine::logError("Load stalled with terrain at " +
                                         std::to_string(static_cast<int>(status.generated * 100.0f)) +
                                         "% and geometry at " +
                                         std::to_string(static_cast<int>(status.drawn * 100.0f)) +
                                         "% and nothing queued - entering the world anyway");
                        break;
                    }
                } else {
                    stalled = 0.0f;
                }

                // Rebuilt only when it would look different: this runs every
                // frame and each rebuild retires a GPU buffer.
                const char* phase = status.generated < 1.0f ? "Generating terrain"
                                    : status.drawn < 1.0f   ? "Building the world"
                                                            : "Finishing up";
                if (std::abs(shown - lastDrawn) > 0.001f || phase != lastPhase) {
                    lastDrawn = shown;
                    lastPhase = phase;
                    renderer.setScreenMesh(
                        game::hud::makeLoadingScreen(shown, phase, renderer.aspectRatio()));
                    // **All three, because the renderer RETAINS whatever mesh
                    // was last set** - `UI.md` S0.4 R6 states that as a rule.
                    // `rebuildHud` ends by setting all three unconditionally and
                    // this set only one, which is benign today for a reason that
                    // is a measurement rather than a property: `makeLoadingScreen`
                    // has exactly one call site, here, at world init, before
                    // `rebuildHud` has ever run, so the other two layers are
                    // still empty and `drawScreen` early-outs on an index count
                    // of zero.
                    //
                    // Falsified the moment there is a *second* loading screen -
                    // a world reload, a render-distance rebuild, or `UI.md`
                    // S0.6's multi-world store - at which point the previous
                    // session's cursor stack and F3 panel float over it, and
                    // whoever debugs that will open `LoadingScreen.cpp`, which
                    // is innocent. Two lines here cost nothing and remove the
                    // trap rather than dating it.
                    //
                    // The clip rectangle is the HUD's own, so an empty mesh
                    // cannot be clipped into visibility by a stale one.
                    const auto [loadClipMin, loadClipMax] =
                        game::inventoryScreen::catalogueListBounds();
                    renderer.setClippedScreenMesh(engine::MeshData{}, loadClipMin, loadClipMax);
                    renderer.setTopScreenMesh(engine::MeshData{});
                }
                renderer.drawFrame(engine::ClearColor{0.055f, 0.06f, 0.075f, 1.0f}, camera.viewMatrix());
            }
        }
        const auto worldReady = std::chrono::steady_clock::now();


        std::size_t initialTriangles = 0;
        for (const auto& [coord, count] : trianglesPerChunk) {
            initialTriangles += count;
        }

        renderer.setOverlayMesh(game::makeBlockOutline());
        glm::vec3 outlineSize{1.0f};
        renderer.setSkyMesh(game::sky::makeSunQuad(), 0);
        int moonPhase = 0;
        renderer.setSkyMesh(game::sky::makeMoonQuad(moonPhase), 1);

        renderer.setToneMapper(static_cast<engine::ToneMapper>(settings.toneMapper));
        renderer.setExposure(settings.exposure);
        renderer.setBloom(settings.bloom, settings.bloomStrength);
        renderer.setShadowQuality(static_cast<int>(settings.shadows));
        renderer.setShadowDarkness(settings.shadowDarkness);
        renderer.setHandheldLight(settings.handheldLight);
        renderer.setClouds(static_cast<int>(settings.clouds), settings.cloudCoverage, settings.cloudShadow);
        renderer.setWater(settings.waterWaves, settings.waterReflection);
        renderer.setWaterDetail(settings.waterFoam, settings.waterCaustics, settings.waterRefraction);
        renderer.setImageQuality(settings.antiAlias, settings.contactShadows);
        renderer.setRenderScale(settings.renderScale);
        float cloudDrift = 0.0f;
        // The sun is the one surface in the game that is genuinely a light
        // rather than a lit thing, so it is written brighter than white and
        // bloom picks it up. Everything else waits for the material table.
        renderer.setSkyEmission(3.0f);

        // Starts mid-morning rather than at sunrise, so the first thing seen is
        // a lit world with the sun clearly off to one side.
        float timeOfDay = 0.18f;
        float waterAnimationSeconds = 0.0f;
        // The ripples run on their own clock, because the sprite animation's
        // wraps every 3.2 seconds and a wave train that jumped that often would
        // be a visible tick rather than a swell.
        float waveSeconds = 0.0f;

        game::weather::Weather weather{kWorldSeed};
        weather.force(static_cast<int>(settings.startWeather));
        // The curtain is rebuilt only when the camera changes column, which is a
        // few times a second while walking rather than every frame.
        glm::ivec2 precipitationColumn{INT_MIN, INT_MIN};
        float precipitationLevel = -1.0f;
        float precipitationFallen = 0.0f;
        auto precipitationKind = game::weather::Precipitation::None;
        float precipitationWind = 0.0f;
        struct PendingThunder {
            float delay;
            float distance;
        };
        std::vector<PendingThunder> thunderQueue;
        float rainSoundTimer = 0.0f;

        game::Particles particles;
        float splashTimer = 0.0f;
        // How often a column near the player is picked for snow to settle on or
        // water to freeze in.
        float settleTimer = 0.0f;
        float windClock = 0.0f;
        // Turned slowly rather than held due west, so a cloud still roughly
        // tells you which way is west but the weather is not on rails.
        float windAngle = 0.0f;
        renderer.setVerticalFov(kDefaultFov);

        if (savedPlayer.has_value()) {
            // A saved position can end up inside rock if the terrain rules have
            // changed under it, and unlike a creature the player has no way to
            // climb out - every move it tries overlaps something. Lifting to the
            // surface costs one test on a resumed load and is the difference
            // between "the world changed" and "the game is broken".
            constexpr float halfWidth = game::player_constants::kWidth * 0.5f;
            const game::Aabb body{
                player.position - glm::vec3{halfWidth, 0.0f, halfWidth},
                player.position + glm::vec3{halfWidth, game::player_constants::kHeight, halfWidth}};
            if (game::overlapsSolid(world, body)) {
                player.position.y = static_cast<float>(
                    world.groundHeight(static_cast<int>(std::floor(player.position.x)),
                                       static_cast<int>(std::floor(player.position.z))));
                engine::logWarn("Saved position was inside terrain; lifted to the surface.");
            }
            // **The hour, wrapped by the same expression that advances it.**
            // `timeOfDay` is a phase in [0, 1) and the day cycle keeps it there
            // with `timeOfDay -= std::floor(timeOfDay)`; reusing that identical
            // form rather than a `clamp` or an `fmod` means the wrap has one
            // owner, and a saved 1.0f lands on sunrise instead of on a value
            // the first frame's advance would have to correct.
            //
            // Guarded by `isfinite` like `yaw`/`pitch` above and `absorption`
            // below. `WorldStore::loadPlayer` sanitises *stacks*, because that
            // is what it can know the shape of; it cannot know what a bare
            // float means, so a hand-edited `player.dat` carrying a NaN would
            // propagate straight into `sunDirection` and stop the sky being
            // drawn - which reads as a renderer bug, a long way from its cause.
            if (std::isfinite(savedPlayer->timeOfDay)) {
                timeOfDay = savedPlayer->timeOfDay - std::floor(savedPlayer->timeOfDay);
            }
            // **The weather, restored beside the hour.** `restore` takes the
            // two flags and the two remaining countdowns.
            //
            // **It SNAPS the five ramps, and this paragraph used to say the
            // opposite.** It read "leaves the five ramps alone, so a world
            // reloaded mid-storm fades up over about five seconds instead of
            // snapping - `Weather.hpp` argues that case and I am not
            // second-guessing it here", which was true when written and is now
            // false in the most expensive direction there is: it is a comment
            // arguing, in this codebase's own voice and citing another file for
            // support, for an edit that would put a fixed bug straight back.
            // `Weather.cpp` now argues the reverse over `restore` itself - a
            // load is a resumption rather than a transition, and the fade gave
            // **heavy rain over perfectly still trees for roughly half a
            // minute**, because wind ramps about six times slower than the
            // levels and the cloud deck below reads it. Do not restore the fade.
            //
            // **`settings.startWeather` still wins where it is set - but NOT
            // because of the call order, which is the other half this used to
            // get wrong.** It credited the `force` call beside the declaration
            // above running first, plus "`restore` does not touch `m_forced`".
            // Both clauses are individually true and the conclusion did not
            // follow: `force` ran first and then these two flags overwrote what
            // it had just set, while `m_forced` stayed non-zero and went on
            // freezing the cycle - so `start_weather=3` over a world saved in
            // clear skies locked in *clear skies*, unchangeable for the whole
            // session, which is worse than the override simply failing. What
            // makes it true today is the `if (m_forced == 0)` inside `restore`,
            // and the countdowns are deliberately assigned outside that guard so
            // the cycle can carry on from the save the moment V clears it.
            //
            // **No sanitising on this side, on purpose.** `restore` is
            // documented as defensive about negative, infinite and NaN timers,
            // and NaN is the one that matters - `m_rainSeconds <= 0.0f` is
            // *false* for NaN, so an unguarded one never expires and the
            // weather freezes for the rest of the session. One owner for that
            // check, and it is not this call site.
            weather.restore(savedPlayer->weatherRaining != 0,
                            savedPlayer->weatherThundering != 0, savedPlayer->weatherRainSeconds,
                            savedPlayer->weatherThunderSeconds);
            engine::logInfo("Resumed from the last saved position.");
        } else {
            // Only the fresh-world placement is left here: it reads blocks, so
            // it cannot run until the chunks exist. A resumed position was read
            // before the loading screen, because it is what the world had to be
            // streamed around.
            player.position.y = static_cast<float>(world.groundHeight(spawnX, spawnZ));

            if (settings.spawnUnderground) {
                // Searched over an area rather than one column, because whether
                // a particular column happens to contain a cave is luck, and
                // hunting for one by hand is exactly the friction this setting
                // exists to remove. The most open spot wins, so the result is a
                // chamber worth standing in rather than a one-block crevice.
                constexpr int radius = 20;
                int bestOpenness = 0;
                glm::ivec3 best{0};

                for (int z = spawnZ - radius; z <= spawnZ + radius; ++z) {
                    for (int x = spawnX - radius; x <= spawnX + radius; ++x) {
                        const int ceiling = world.highestSolid(x, z) - 5;
                        for (int y = 4; y < ceiling; ++y) {
                            if (!world.isSolid(x, y, z) || world.isSolid(x, y + 1, z) ||
                                world.isSolid(x, y + 2, z) || world.isSolid(x, y + 3, z)) {
                                continue;
                            }

                            int openness = 0;
                            for (int dz = -2; dz <= 2; ++dz) {
                                for (int dy = 1; dy <= 3; ++dy) {
                                    for (int dx = -2; dx <= 2; ++dx) {
                                        if (!world.isSolid(x + dx, y + dy, z + dz)) {
                                            ++openness;
                                        }
                                    }
                                }
                            }
                            if (openness > bestOpenness) {
                                bestOpenness = openness;
                                best = {x, y + 1, z};
                            }
                            break;
                        }
                    }
                }

                if (bestOpenness > 0) {
                    player.position = glm::vec3{static_cast<float>(best.x) + 0.5f, static_cast<float>(best.y),
                                                static_cast<float>(best.z) + 0.5f};
                    engine::logInfo("Spawned underground at " + std::to_string(best.x) + ", " +
                                    std::to_string(best.y) + ", " + std::to_string(best.z));
                } else {
                    engine::logWarn("spawn_underground: no cave found near the spawn column");
                }
            }
        }

        // Where the player actually ended up, which a resumed save can put a
        // long way from `spawn_x`. One line, but it is the difference between
        // "no animals here" being a bug report and being the answer.
        engine::logInfo(
            std::string{"Standing in "} +
            game::biomeInfo(game::sampleBiome(kWorldSeed, static_cast<int>(std::floor(player.position.x)),
                                              static_cast<int>(std::floor(player.position.z)))
                                .dominant)
                .name);

        const auto ms = [](auto from, auto to) {
            return std::to_string(std::chrono::duration<float, std::milli>(to - from).count());
        };
        engine::logInfo("World seed " + std::to_string(kWorldSeed) + ", render distance " +
                        std::to_string(world.visibleRadius()) + " chunks");
        engine::logInfo("Saves: " + (engine::executableDirectory() / "saves").string());
        engine::logInfo("Initial load: " + std::to_string(world.loadedChunkCount()) + " chunks, " +
                        std::to_string(initialTriangles) + " triangles in " + ms(buildStart, worldReady) + " ms on " +
                        std::to_string(jobs.threadCount()) + " workers");

        engine::logInfo("Frame cap: " + describeCap(kFpsCapOptions[capIndex]) + " (F1 lower, F2 raise)");
        engine::logInfo("Field of view: " + std::to_string(static_cast<int>(kDefaultFov)) +
                        " (F3 narrower, F4 wider)");
        engine::logInfo("Move: WASD. Space jump, Left Shift sneak, Left Ctrl sprint.");
        engine::logInfo("Left click breaks, right click places. 1-9 or scroll pick a block.");
        engine::logInfo("E opens the inventory; right-click a crafting table for its 3x3 grid.");
        engine::logInfo("Double-tap Space to fly. Descend onto the ground to land.");
        engine::logInfo("Escape releases the mouse; click to recapture.");
        engine::logInfo("F5 toggles the diagnostics overlay.");
        engine::logInfo("F6/F7 change render distance.");
        engine::logInfo(std::string("F10 cycles tone mapping (now: ") + kToneMapperNames[settings.toneMapper] +
                        "), F11 toggles bloom.");
        engine::logInfo(std::string("G cycles shadow quality (now: ") + kShadowQualityNames[settings.shadows] +
                        "). F12 cycles the surface debug views.");
        engine::logInfo(std::string("C cycles clouds (now: ") + kCloudQualityNames[settings.clouds] + ").");
        engine::logInfo("F8 spawns a Bramble ahead of you, F9 a charged one.");
        engine::logInfo("Enter takes a photo of the screen into " + photosDirectory().string() + ".");
        engine::logInfo("Right click a spawn egg to place that creature; the inventory's left card has them all.");
        engine::logInfo("Entering main loop. Close the window to exit.");

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        auto lastReportTime = previousTime;
        int framesSinceReport = 0;

        float placeTimer = 0.0f;
        /// **Buttons that are still down, and when they let go.**
        ///
        /// A button is the one hand toggle that undoes itself, so the press has
        /// to outlive the frame that made it. Deliberately **not saved**: it
        /// lasts a second and a half at most, nothing but the picture reads it
        /// while there is no signal engine, and a press that survives a quit is
        /// undone by the next one - pressing an already-down button re-arms this
        /// and the sweep releases it. A handful at a time, so a plain vector is
        /// the whole of the storage.
        std::vector<std::pair<glm::ivec3, float>> heldButtons;
        /// Which two-input bench is open, so the previewed result can be the
        /// smithing upgrade or the repair without a second screen kind.
        game::BlockId openBench = game::BlockId::SmithingTable;

        /// A cloud left by a lingering potion.
        ///
        /// **Kept here rather than in the projectile system**, which reads the
        /// world and never writes it and has no idea a player exists - the same
        /// hand-off every landing already uses. A handful at a time, so a plain
        /// vector is the whole of the storage.
        struct LingeringCloud {
            glm::vec3 position{0.0f};
            game::ItemId potion = game::ItemId::None;
            float secondsLeft = 0.0f;
            float applyTimer = 0.0f;
        };
        std::vector<LingeringCloud> lingeringClouds;
        /// The reference's own: a cloud lives thirty seconds, starts three
        /// blocks across and applies once a second at a quarter strength.
        constexpr float kCloudSeconds = 30.0f;
        constexpr float kCloudRadius = 3.0f;
        constexpr float kCloudInterval = 1.0f;
        constexpr float kLingeringScale = 0.25f;
        /// A splash reaches four blocks, and what it does falls off linearly to
        /// nothing at the edge.
        constexpr float kSplashRadius = 4.0f;

        /// Which jukebox is playing which disc.
        ///
        /// **Named divergence: this is not saved.** A disc left in a jukebox
        /// comes back to you when the world reloads rather than still being in
        /// there, because the block-entity file has no room for it yet.
        ///
        /// **That sentence was a claim rather than a description until
        /// 2026-08-18.** Loading a jukebox consumes the disc from the bag and
        /// nothing ever restored this vector, so what actually happened on
        /// reload was that the disc ceased to exist - a music disc is a
        /// dungeon-loot item you get one of, so "it comes back to you" and "it
        /// is destroyed" are a long way apart from the player's side. The fold
        /// in `saveEverything` is what makes the comment true; the divergence
        /// that remains is only that the jukebox is empty afterwards.
        std::vector<std::pair<glm::ivec3, game::ItemId>> jukeboxDiscs;
        /// **One inventory for every ender chest in the world.** It belongs to
        /// the player rather than to any block, which is the whole point of it,
        /// so it lives here and not in the chest map.
        game::Chest enderChest;
        /// Where a bed that has been slept in stands, if any.
        ///
        /// **Restored here rather than beside the rest of the saved player**,
        /// for the same reason the inventory is: this is where the variable
        /// first exists, and dragging the declaration up to the load would put
        /// it four hundred lines from everything that reads it.
        ///
        /// `SavedPlayer::respawnBed` spells "no bed" as a negative `y` and
        /// `loadPlayer` has already forgotten any cell that is not a place in
        /// this world, so the single test below is the whole of the validation
        /// - there is no second, staler copy of "is the bed still there" on
        /// disk, because that question is asked of the live world at the moment
        /// of death and can only be answered there.
        glm::ivec3 respawnPoint{0};
        bool hasRespawnPoint = false;
        if (savedPlayer.has_value() && savedPlayer->respawnBed.y >= 0) {
            respawnPoint = savedPlayer->respawnBed;
            hasRespawnPoint = true;
        }
        /// The composter's own roll. A plain linear generator rather than a
        /// shared one, so filling a tub cannot perturb worldgen or spawning.
        std::uint32_t composterRandom = 0x2545F491u;
        float dropTimer = 0.0f;
        float secondsSinceSpacePress = kDoubleTapSeconds;

        // Digging is now a progress bar rather than a repeat timer: how long a
        // block takes depends on what it is and what you are holding.
        constexpr glm::ivec3 kNoBlock{INT_MIN, INT_MIN, INT_MIN};
        glm::ivec3 breakingBlock = kNoBlock;
        float breakProgress = 0.0f;
        // A block that takes no time to break would otherwise break again the
        // very next frame, and since each one exposes the block behind it, a
        // single click would tunnel the whole reach in a straight line.
        float breakCooldown = 0.0f;
        // Varies the per-ray roll and the drop roll of a blast. Two Brambles
        // going off in the same spot should not carve the same hole.
        std::uint32_t blastRandom = kWorldSeed | 1u;
        // The HUD only rebuilds when something asks it to, so the frame digging
        // *stops* has to ask - otherwise the last drawn bar stays on screen.
        float lastBreakProgress = 0.0f;
        // What the crack overlay is currently showing, so it is rebuilt when
        // the picture would change and not once a frame. `-1` is "no cracks".
        int crackStage = -1;
        glm::ivec3 crackBlock = kNoBlock;
        // Where the last footstep was taken, and whether the last frame was
        // already showing a hurt flash - both exist so an event fires on the
        // edge rather than every frame the condition holds.
        glm::vec3 lastStepAt{0.0f};
        bool wasHurt = false;
        bool wasAlive = true;
        /// Health as of the last hurt check, so the next one can tell how much
        /// was actually lost. Nothing else reports the size of a hit.
        int lastHealth = 0;
        bool wasInWater = false;
        /// Whether a stroke was already under way, so `Swim` sounds once per
        /// stroke rather than every frame of one. `Player::treading` is latched
        /// across exactly one stroke by the fluid model, which is what makes its
        /// rising edge the right clock for this - and the reason nothing here
        /// invents a cadence of its own.
        bool wasTreading = false;
        float lastFallDistance = 0.0f;
        /// Whether the player was standing last frame, so a landing is an edge
        /// rather than a state - trampling must fire once per fall, not every
        /// frame you stand on the field afterwards.
        bool wasOnGround = true;
        float caveTimer = 0.0f;
        /// Where the scenery ambiences stand as of the last scan, and how long
        /// until the next one.
        ///
        /// **Cached because the answer costs a hundred and twenty-five
        /// `blockAt` calls and the question does not change in a tenth of a
        /// second.** `tickAmbient` has to be called every frame - that is its
        /// contract, a cue that lapses forgets its timer - but the *scan* that
        /// feeds it must not be, or four cues become five hundred lookups a
        /// frame and the cost lands as a frame-time regression nobody would
        /// think to blame on audio. One scan answers all three, because they
        /// are three readings of the same box.
        float ambientScanTimer = 0.0f;
        bool fireNearby = false;
        bool lavaNearby = false;
        glm::vec3 fireAt{0.0f};
        glm::vec3 lavaAt{0.0f};
        constexpr float kStepDistance = 2.1f;
        constexpr float kBigFallDistance = 7.0f;
        // Rolled rarely rather than every frame; the reference's own cave
        // ambience is sparse enough that a check a few times a minute is
        // indistinguishable from one every tick.
        constexpr float kCaveCheckSeconds = 22.0f;
        // What the status bars last drew. Compared rather than flagged, because
        // health, food, air and the absorption pool all change from inside the
        // physics and nothing there knows the HUD exists.
        int lastShownHealth = -1;
        int lastShownFood = -1;
        game::hud::AirRow lastShownAir;
        bool lastShownHurt = false;
        int lastShownAbsorbHearts = -1;

        // Creative starts with one of everything placeable; survival starts with
        // nothing and fills up from what you break.
        constexpr std::array<game::BlockId, game::kHotbarSlots> creativeKit{
            game::BlockId::Grass,     game::BlockId::Dirt,          game::BlockId::Stone,
            game::BlockId::StoneSlab, game::BlockId::CobbleStairs0, game::BlockId::TallGrass,
            game::BlockId::Planks,    game::BlockId::PlanksFence,   game::BlockId::Glowstone};

        // Raw materials, in the storage rows rather than the hotbar: they are
        // crafting inputs rather than things to place, and the hotbar is full.
        // **Size deduced, and this is the one where the zero-fill is not
        // abstract.** `BlockId` 0 is `Air`, so a row dropped from an explicit
        // `8` would not shorten the creative kit - it would hand the player a
        // hotbar slot holding air that looks empty, cannot be dropped and
        // cannot be explained. That is the same bug `CLAUDE.md` records against
        // a `kFlowers` declared 12 with 11 rows, which quietly planted Air as a
        // flower. Deducing makes the short row inexpressible.
        constexpr std::array creativeStock{
            game::BlockId::Log,     game::BlockId::Cobblestone,   game::BlockId::Sand,
            game::BlockId::Gravel,  game::BlockId::CraftingTable, game::BlockId::Furnace,
            game::BlockId::Torch,   game::BlockId::Bricks};

        game::Inventory inventory;
        bool creative = settings.creativeMode;

        /// **Every way the player can be hurt goes through here.** The gate is
        /// the whole point: four of the seven damage sources checked `creative`
        /// and three did not, so a creative player drowned in a thunderstorm,
        /// died to a witch's splash potion, and could kill themselves with a
        /// bottle of Harming. The rule was stated in a comment at one of the
        /// guarded sites - "creative takes the hit and not the damage, which is
        /// the rule every other source of harm here already follows" - and was
        /// simply not true of three of them.
        ///
        /// `damagePlayer` still owns the half-second invulnerability window;
        /// this owns only who is allowed to be hurt at all.
        ///
        /// **`armour` has no default, and that is the point.** It was a
        /// two-argument lambda, so every combat blow in the game reached
        /// `damagePlayer`'s own defaulted `kNoArmour` and a full diamond set
        /// reduced nothing - while the *same* set worked correctly against lava
        /// and fire, because `Player.cpp` passes a real one there. A playtester
        /// standing in lava would have concluded armour was wired.
        ///
        /// That is `CLAUDE.md` bug shape #14 in its purest form: a rule that is
        /// correct and commented in one of the two places that need it.
        /// Removing the default is what makes the compiler name every site, so
        /// the next source of harm cannot inherit the silence.
        ///
        /// **Which blows armour touches is `Survival.hpp`'s list, not a
        /// judgement made here** - reduced: explosions, projectiles, lightning,
        /// a falling anvil; not reduced: magic, and everything `Player.cpp`
        /// already decides for itself.
        const auto hurtPlayer = [&](int amount, game::survival::ArmourSet armour) {
            if (creative || amount <= 0) {
                return;
            }
            game::damagePlayer(player, amount, false, armour);
        };
        /// **One owner for "grant this effect to the player".**
        ///
        /// The split matters and is easy to get wrong: `Effects::apply` holds a
        /// timer, so it refuses the two instant effects outright and returns
        /// false - hand it an Instant Health and nothing at all happens, with no
        /// warning anywhere. The three-way test was written correctly at the
        /// drink site and copied to the splash-and-cloud site, and adding the
        /// food grants was about to make a third copy of it. A row in the food
        /// table asking for an instant effect would then work in two of three
        /// places, which is this project's most expensive bug shape.
        ///
        /// `scale` is how much of it reaches you - a splash falls off with
        /// distance and a cloud applies a quarter. **It multiplies the amount
        /// for an instant and the duration for a timed one**, because those are
        /// the two different things "a quarter of a potion" means.
        ///
        /// **Granting is not the same as being read**, and this is the one place
        /// that sees every grant, so the gap is recorded here. `Effects.hpp`
        /// keeps the list and says whose file each missing reader belongs in;
        /// two of its entries have moved since:
        ///
        ///   - **Night Vision is now read** (`setSunLighting`, below), so both
        ///     brewing rows finally do something.
        ///   - **Invisibility is not, and `Effects.hpp`'s note that it "skips
        ///     drawing the player and held item" cannot be acted on: there is
        ///     no player model and no first-person hand in the game at all**,
        ///     so there is nothing to skip. What is left of it is entirely
        ///     `Creature.cpp`'s - it already shortens its notice range for a
        ///     crouched player, and invisibility is the same multiplier.
        ///
        /// Nausea and Blindness want a post-process the renderer does not have;
        /// Health Boost and Saturation are `Survival.hpp` numbers. None of the
        /// four is grantable today, so none is a live hole - Night Vision and
        /// Invisibility were, because they brew.
        const auto grantEffect = [&](game::effects::Effect effect, int amplifier, float seconds,
                                     float scale = 1.0f) {
            if (effect == game::effects::Effect::None || scale <= 0.0f) {
                return;
            }
            if (effect == game::effects::Effect::InstantHealth) {
                game::healPlayer(player, static_cast<int>(
                                             game::effects::instantAmount(effect, amplifier) * scale));
            } else if (effect == game::effects::Effect::InstantDamage) {
                // **Magic, which armour does not touch** - `Survival.hpp`'s
                // "NOT reduced" list names it, and Bedrock's own cause enum
                // routes a Harming potion through `magic`. A splash of Harming
                // through full diamond costs exactly what it costs naked.
                hurtPlayer(static_cast<int>(game::effects::instantAmount(effect, amplifier) * scale),
                           game::survival::kNoArmour);
            } else {
                player.effects.apply(effect, amplifier, seconds * scale);
            }
        };
        const auto fillCreativeKit = [&] {
            for (std::size_t i = 0; i < game::kInventorySlots; ++i) {
                inventory.slot(i) = game::ItemStack{};
            }
            for (std::size_t i = 0; i < game::kHotbarSlots; ++i) {
                inventory.slot(i) = game::ItemStack{game::itemForBlock(creativeKit[i]), game::kMaxStack};
            }
            for (std::size_t i = 0; i < creativeStock.size(); ++i) {
                inventory.slot(game::kHotbarSlots + i) =
                    game::ItemStack{game::itemForBlock(creativeStock[i]), game::kMaxStack};
            }
        };

        if (creative) {
            fillCreativeKit();
        }

        game::ItemEntities drops;
        game::Projectiles projectiles;
        // How long the bow has been drawn, and whether it was drawn last frame
        // - releasing is what fires, so the shot needs the falling edge.
        float bowDraw = 0.0f;
        bool bowHeld = false;
        // A pearl may only be thrown once a second.
        float pearlCooldown = 0.0f;
        game::FallingBlocks fallingBlocks;
        engine::MeshHandle dropMesh = engine::kInvalidMesh;
        engine::MeshHandle dropGlassMesh = engine::kInvalidMesh;
        engine::MeshHandle fallingMesh = engine::kInvalidMesh;
        engine::MeshHandle projectileMesh = engine::kInvalidMesh;

        // Creatures live beside the drops: few of them, main thread, and they
        // only ever read the world.
        game::Creatures creatures(kWorldSeed ^ 0x9E3779B9u);
        creatures.setActiveRadius(static_cast<float>(world.visibleRadius() * game::Chunk::kSize));
        engine::MeshHandle creatureMesh = engine::kInvalidMesh;
        // A slime's gel shell is the only see-through creature geometry, and it
        // has to ride in the translucent pass or it blends against whatever was
        // already in the framebuffer rather than against the core inside it.
        engine::MeshHandle creatureShellMesh = engine::kInvalidMesh;

        if (settings.creatureShowcase > 0) {
            // The camera starts looking down -Z, so rows recede along -Z and
            // columns spread along X. A yaw of half pi is a profile; zero turns
            // the animal to face the camera and pi turns it away.
            constexpr float kShowcaseYaw = 1.5707963f;
            const int count = static_cast<int>(game::CreatureKind::Count);

            // Standing on the ground, but **never below the player's own
            // feet**. A seabed is twenty blocks under a swimming camera and a
            // cliff edge nearly as far, and a subject placed down there is a
            // model nobody can review. It is also the only thing that puts a
            // fish in water rather than on the bottom of the sea.
            const auto showcaseY = [&](float x, float z) {
                const int ground = world.highestSolid(static_cast<int>(std::floor(x)),
                                                      static_cast<int>(std::floor(z)));
                return std::max(static_cast<float>(ground + 1), player.position.y);
            };

            if (settings.creatureShowcase == 1) {
                // A grid rather than a row: the field of view cannot hold
                // thirteen animals side by side at a distance where any of them
                // is big enough to judge. Rows are staggered so a far one never
                // sits directly behind a near one.
                constexpr int kPerRow = 5;
                for (int i = 0; i < count; ++i) {
                    const int row = i / kPerRow;
                    const int column = i % kPerRow;
                    const float z = player.position.z - (8.0f + static_cast<float>(row) * 7.0f);
                    const float x = player.position.x + static_cast<float>(column - 2) * 3.0f +
                                    static_cast<float>(row) * 1.5f;
                    creatures.place(static_cast<game::CreatureKind>(i),
                                    glm::vec3{x, showcaseY(x, z), z}, kShowcaseYaw);
                }
            } else {
                // One species, three copies: profile, facing the camera, and
                // facing away. A face drawn on all six sides of a head is
                // invisible from the front alone.
                const int index = std::clamp(settings.creatureShowcase - 2, 0, count - 1);
                const auto kind = static_cast<game::CreatureKind>(index);
                const float yaws[3]{kShowcaseYaw, 0.0f, 3.1415927f};
                for (int i = 0; i < 3; ++i) {
                    const float x = player.position.x + static_cast<float>(i - 1) * 2.5f;
                    const float z = player.position.z - 5.0f;
                    // Something that inflates gets its three *stages* rather
                    // than three angles. The roster is frozen here, so a
                    // pufferfish left to itself would sit deflated forever and
                    // two thirds of its model would be unreviewable.
                    const float puff =
                        game::speciesInfo(kind).puffs ? static_cast<float>(i) : 0.0f;
                    creatures.place(kind, glm::vec3{x, showcaseY(x, z), z}, yaws[i], false, puff);
                }
                engine::logWarn(std::string{"creature_showcase: "} + game::speciesInfo(kind).name + " only");
            }
            engine::logWarn("creature_showcase: the roster is frozen and the spawner is off");
        }

        // One of every block whose dropped form is not simply a little cube,
        // laid out in a row so all of them can be judged in one look.
        //
        // **This used to be free because dropped items were never saved, and
        // since 2026-08-19 they are.** The sentence that stood here said so and
        // would now be the reason a review mode quietly minted an anvil, an
        // enchanting table and thirty more items into the world on every look.
        // Both ends of the drop save are gated on this same flag instead - the
        // restore beside `loadCreatures` and the write in `saveEverything` - so
        // the showcase floor is neither loaded into nor written out.
        //
        // **Falsified by**: any of the three gates below going missing. **The
        // search this used to give was its own name, and my own refactor broke
        // it the same day - 2026-08-19.** The old text said to grep
        // `dropShowcase` and expect three `if` gates; the persistence pair was
        // then hoisted onto a derived owner, so that grep now finds exactly one
        // and a reader following it would conclude two gates had been deleted
        // and go looking for a bug that is not there. A falsifier that names one
        // spelling of a question does not survive the question acquiring a
        // second spelling. Three searches, because there are three names and
        // the counts differ - and each is given as CODE sites, because this
        // paragraph quotes all three and so appears in its own results:
        //
        //   `if (settings.dropShowcase)` - **one** code site, the placement
        //   just below, correctly this mode alone, because filling the floor
        //   for review is the only thing the creature showcase does not want.
        //   `if (showcasing)` - **one** code site, the whole-body refusal at
        //   the top of `saveEverything`.
        //   `if (dropsPersist)` - **two** code sites, the restore beside
        //   `loadCreatures` and the write inside `saveEverything`.
        //
        // So a correct tree greps 2, 2 and 3, one of each being this comment.
        // That off-by-one is not pedantry: a `static_assert` count elsewhere in
        // this project swallowed one quoted in a doc comment, reported six where
        // there were five, and the shifted index produced a wrong verdict as
        // well as a wrong number.
        //
        // `showcasing` and `dropsPersist` are exact De Morgan twins declared on
        // adjacent lines, so the four cannot disagree about what a review mode
        // is; the split exists so the reader of each site sees the question in
        // the polarity that site asks it in.
        if (settings.dropShowcase) {
            const game::BlockId kAwkward[]{
                game::BlockId::Torch,          game::BlockId::Bell,
                game::BlockId::Cauldron,       game::BlockId::Anvil,
                game::BlockId::Hopper,         game::BlockId::Stonecutter,
                game::BlockId::Grindstone,     game::BlockId::BrewingStand,
                game::composterAt(4),          game::BlockId::Lantern,
                game::BlockId::EndRod,         game::BlockId::Campfire,
                game::BlockId::Scaffolding,    game::BlockId::EnchantingTable,
                game::BlockId::EndPortalFrame, game::BlockId::SculkShrieker,
                game::BlockId::PlanksFence,    game::BlockId::CobbleStairs0,
                game::BlockId::StoneSlab,      game::BlockId::Stone,
                // The redstone round. Every one of these is a model or a cut
                // shape whose dropped miniature is the only place its geometry
                // is drawn at that size, which is exactly where a bell spent
                // twenty milestones as a gold brick.
                game::BlockId::RedstoneTorch,
                game::leverAt(game::LeverFloorX, false),
                game::buttonAt(0, 0, false),
                game::pressurePlateAt(0, 0),
                game::repeaterAt(game::FaceDirection::NegZ, 1, false, false),
                game::comparatorAt(game::FaceDirection::NegZ, false, false),
                game::pistonAt(game::Facing6North, false, true),
                game::pistonHeadAt(game::Facing6North, false),
                game::observerAt(game::Facing6North, false),
                game::dispenserAt(game::Facing6North, false),
                game::daylightDetectorAt(0, false),
                game::lightningRodAt(game::Facing6Up, false),
                game::tripwireHookAt(game::FaceDirection::NegZ, false, false),
            };
            constexpr int kPerRow = 7;
            const auto count = static_cast<int>(std::size(kAwkward));
            for (int i = 0; i < count; ++i) {
                const float x = player.position.x + static_cast<float>(i % kPerRow - kPerRow / 2) * 0.9f;
                const float z = player.position.z - 3.0f - static_cast<float>(i / kPerRow) * 1.2f;
                drops.spawn(glm::vec3{x, player.position.y + 0.6f, z},
                            game::itemForBlock(kAwkward[i]), 1, glm::vec3{0.0f});
            }
            // The 2D sprite path, thrown down beside them: these must stay flat
            // pictures with thickness and must not have become little cubes.
            // Redstone dust and a rail belong here rather than above - both are
            // flat shapes, so both are drawn as the picture their texture is.
            constexpr game::ItemId kSprites[]{game::ItemId::StonePickaxe, game::ItemId::Coal,
                                              game::ItemId::Bucket, game::ItemId::Redstone};
            for (int i = 0; i < static_cast<int>(std::size(kSprites)); ++i) {
                drops.spawn(glm::vec3{player.position.x + static_cast<float>(i - 1) * 0.9f,
                                      player.position.y + 0.6f, player.position.z - 6.6f},
                            kSprites[i], 1, glm::vec3{0.0f});
            }
            for (const game::BlockId plant : {game::BlockId::Poppy, game::BlockId::LadderNorth,
                                              game::BlockId::Glass,
                                              game::railAt(0, 0, false)}) {
                drops.spawn(glm::vec3{player.position.x + 3.0f, player.position.y + 0.6f,
                                      player.position.z - 6.6f},
                            game::itemForBlock(plant), 1, glm::vec3{0.0f});
            }
            engine::logWarn("drop_showcase: " + std::to_string(count) +
                            " blocks and six sprite drops are on the floor a few paces north");
        }

        float swingTimer = 0.0f;
        // Which panel is up, if any. A crafting table reuses the inventory
        // screen with a wider grid rather than owning a screen of its own.
        std::optional<game::inventoryScreen::Kind> openScreen;
        // The catalogue card's own state. Kept out here rather than inside the
        // screen module, so `build` stays a pure function of what it is given.
        game::inventoryScreen::CatalogueState catalogue;
        // Free-running clock for anything on screen that pulses. The caret is
        // the only user so far, on the reference's own six-tick cadence: three
        // tenths of a second on, three off.
        float uiSeconds = 0.0f;
        constexpr float kCaretBlinkSeconds = 0.6f;
        // Leftover fraction of a wheel notch. A mouse reports whole detents and
        // a precision trackpad reports fractions, and truncating each frame's
        // delta on its own throws every one of the latter away - the list simply
        // never moves.
        float scrollCarry = 0.0f;
        game::ItemStack heldStack;
        // Sized for the largest grid any screen offers, so moving between the
        // inventory's 2x2 and a table's 3x3 is a change of extent, not of storage.
        // A furnace borrows the first two for its input and fuel.
        std::array<game::ItemStack, game::kMaxCraftSlots> craftSlots{};

        // What the stonecutter's nth option would make from whatever is in its
        // input slot. **The single owner** - the screen draws it, the tooltip
        // reads it and taking it spends from the same expression.
        const auto stonecutterCut = [&craftSlots](int option) {
            const game::ItemStack& input = craftSlots[0];
            if (input.empty() || !game::isBlockItem(input.item)) {
                return game::ItemStack{};
            }
            const game::BlockId cut =
                game::stonecutterOption(game::blockForItem(input.item), option);
            if (cut == game::BlockId::Air) {
                return game::ItemStack{};
            }
            return game::ItemStack{game::itemForBlock(cut), game::stonecutterYield(option)};
        };

        // Every furnace the player has interacted with, by block position.
        //
        // Kept here rather than in `World` on purpose: chunks are loaded and
        // saved on worker threads, and block-entity data does not need to go
        // anywhere near that. Entries outlive their chunk being unloaded, which
        // costs nothing for something a player places a handful of.
        //
        // **They also outlive their chunk being *discarded*, and one path out of
        // that is still real.** None of these tables is chunk-keyed and the
        // loads below are unconditional, so a `kChunkFormatVersion` bump - which
        // throws away every modified chunk and regenerates it - takes the
        // container block away and leaves the record behind.
        //
        // Two of the three ways that used to bite are now closed, and the third
        // is not. **Closed:** placing a block clears the cell's records first,
        // for every placement rather than only for containers, so building a
        // fresh chest on a stale cell no longer opens it full of someone else's
        // gear. **Closed:** the save path drops any entry whose block is no
        // longer a container once its column is resident, so a stale record is
        // not carried in the file for the life of the world.
        //
        // **Closed too, and it was the one nobody walked past:** a regenerated
        // `LootChest` standing over a stale entry. Worldgen places it, so no
        // placement ran to clear the cell, and `materialise` took `chests[at]` -
        // which *finds* the stale contents rather than starting empty - and
        // rolled the table into it on top. Measured on the real table before the
        // fix: 11 items became 22 after one format bump and 44 after three, and
        // a chest the player had already emptied handed back a full 11 every
        // time. That is the same duplication shape as the drag cluster, arriving
        // through a different door.
        //
        // The cure is `rolledLoot` below, and the point of it is that **"this
        // chest has been rolled" was recorded in exactly one place - the block
        // id - and that place does not survive a format bump.** So it is now
        // recorded in the store as well, and the two have different lifetimes on
        // purpose. `materialise` rolls only on a cell it has never seen, and an
        // emptied loot chest keeps an otherwise-pointless empty record so the
        // flag survives. **The guard alone is not enough and the probe says so
        // numerically** - without the empty record a fully looted chest, which
        // is the ordinary case, still refilled with 11.
        //
        /// **The rule for a stack out of a file lives in `WorldStore`**, and this
        /// was a lambda restating it letter for letter. Four restores need the
        /// answer, `WorldStore`'s own readers already apply it before handing
        /// anything back, and a second copy here is precisely how the two would
        /// drift - the copy would keep the old bound while the real one grew a
        /// new clause, and nothing would say so. `game::sanitiseStack`,
        /// `sanitiseChest`, `plausibleStowHandle` and `plausibleBlockPosition`
        /// are all exported for exactly this.
        std::unordered_map<glm::ivec3, game::Furnace, BlockPositionHash> furnaces;
        for (const game::PlacedFurnace& placed : world.store().loadFurnaces()) {
            // **A position out of a file is as suspect as a count out of one.**
            // It is a map key here and it becomes chunk arithmetic the moment
            // anything asks `blockAt` about it, so an implausible one is
            // dropped rather than stored. Same owner as the stack rule.
            if (!game::plausibleBlockPosition(placed.position)) {
                continue;
            }
            game::Furnace restored = placed.furnace;
            game::sanitiseStack(restored.input);
            game::sanitiseStack(restored.fuel);
            game::sanitiseStack(restored.output);
            furnaces.emplace(placed.position, restored);
        }
        if (!furnaces.empty()) {
            engine::logInfo("Restored " + std::to_string(furnaces.size()) + " furnaces.");
        }

        // Chests, on the same arrangement and for the same reasons.
        std::unordered_map<glm::ivec3, game::Chest, BlockPositionHash> chests;

        /// Every cell whose loot table has already been rolled.
        ///
        /// **The second home of a fact that used to have only one**, and the
        /// whole of the fix described above. The block id says "rolled" by no
        /// longer being a `LootChest`, which is the right place for it and is
        /// enough for everything except the one event that throws the block
        /// away and keeps the store - a `kChunkFormatVersion` bump. This set
        /// rides in the store instead, so the two survive different things.
        ///
        /// It is seeded from the empty records, because **an empty chest record
        /// is written for exactly one reason**: the save filter keeps one only
        /// when the cell is in here. A record that sanitised down to nothing is
        /// swept up by the same rule, which is the conservative way round - it
        /// costs a chest whose every stack was unreadable its re-roll, and the
        /// alternative is handing the player a free one.
        ///
        /// A stale entry is inert. The save loop walks `chests`, so a cell with
        /// no chest entry is never asked about, and the break and placement
        /// paths need no clearing pass of their own.
        std::unordered_set<glm::ivec3, BlockPositionHash> rolledLoot;
        for (const game::PlacedChest& placed : world.store().loadChests()) {
            if (!game::plausibleBlockPosition(placed.position)) {
                continue;
            }
            game::Chest restored = placed.chest;
            game::sanitiseChest(restored);
            if (restored.empty()) {
                rolledLoot.insert(placed.position);
            }
            chests.emplace(placed.position, restored);
        }
        if (!chests.empty()) {
            engine::logInfo("Restored " + std::to_string(chests.size()) + " chests.");
        }

        // Campfires, on the same arrangement again. **`sanitiseStack` and the
        // timer clamp both happen in `loadCampfires`**, not here, for the reason
        // the comment above the furnaces gives: the rule for a record out of a
        // file belongs to `WorldStore`, and a second copy at the call site is
        // how the two drift.
        std::unordered_map<glm::ivec3, game::Campfire, BlockPositionHash> campfires;
        for (const game::PlacedCampfire& placed : world.store().loadCampfires()) {
            if (!game::plausibleBlockPosition(placed.position)) {
                continue;
            }
            campfires.emplace(placed.position, placed.campfire);
        }
        if (!campfires.empty()) {
            engine::logInfo("Restored " + std::to_string(campfires.size()) + " campfires.");
        }

        // What is inside every stowbox that is currently an item rather than a
        // block. The key rides in the stack's `damage`, so it survives being
        // dropped, picked up, saved and reloaded with no new field anywhere.
        std::unordered_map<int, game::Chest> stowed;
        int nextStowHandle = 1;
        for (const game::StowedBox& box : world.store().loadStowboxes()) {
            // **A handle has to be positive or nothing can reach it**, and it
            // has to be small enough that `nextStowHandle` cannot be pushed to
            // overflow by one corrupt record. Handles ride in an
            // `ItemStack::damage`, which the inventory restore only floors at
            // zero - so a negative one names a map entry the box item in the
            // bag can no longer ask for, and zero is what an item carrying no
            // box at all looks like. `plausibleStowHandle` owns both bounds;
            // this used to be a hand-rolled `<= 0` that had only the lower one.
            if (!game::plausibleStowHandle(box.handle)) {
                continue;
            }
            game::Chest restored = box.contents;
            game::sanitiseChest(restored);
            stowed.emplace(box.handle, restored);
            nextStowHandle = std::max(nextStowHandle, box.handle + 1);
        }

        // Where the hopper pass is up to, and the scratch it gathers positions
        // into. Reused rather than allocated every four hundred milliseconds.
        float hopperTimer = 0.0f;
        std::vector<glm::ivec3> hopperCells;

        // How many of a `Chest`'s slots a block actually uses. A hopper stores
        // its five in the same twenty-seven-slot struct and simply never
        // touches the rest, so **this is the one place that difference lives**
        // - reading past it would let a hopper hold items no screen can reach.
        const auto containerSlots = [](game::BlockId id) -> std::size_t {
            return game::isHopper(id) ? game::kHopperSlots : game::kChestSlots;
        };

        // Moves a single item from one container into another, stacking onto a
        // match before taking an empty slot, which is the same preference the
        // player's own inventory has.
        //
        // **It takes a slot range, not a `Chest`.** A furnace's three slots are
        // three separate destinations with three separate rules - the hopper
        // above feeds the input, the one beside it feeds the fuel, the one below
        // pulls the output - so there is nothing for a whole-container mover to
        // point at. Widening this to a pointer and a length is what let the
        // furnace join in without a second copy of the stacking preference, the
        // `damage` rule and the `roomFor`/`moveOne` pairing below.
        //
        // `accepts` is the destination's own filter, and null means "anything":
        // a furnace's fuel slot is the only caller that has one today, and it
        // exists because a hopper beside a furnace feeding it raw iron would
        // otherwise wedge the fuel slot with something that cannot burn and can
        // only be recovered by breaking the block.
        //
        // **`damage` rides along, and the merge test asks `slots::roomFor`.**
        // Rebuilding the stack as `{source.item, 1}` let its third member
        // default to zero, which is a tool's wear and a stowbox's contents: a
        // hopper handed back a fully repaired tool - two of them in a loop made
        // durability optional - and emptied every box that passed through it,
        // stranding the real contents in `stowboxes.dat` behind a handle
        // nothing could ever name again. `roomFor` is the one owner of "will
        // this fit" and `moveOne` is the one owner of the move itself, and both
        // compare `damage`, so the merge pass can no longer stack two different
        // kinds together either.
        const auto moveOneItem = [](game::ItemStack* from, std::size_t fromSlots,
                                    game::ItemStack* to, std::size_t toSlots,
                                    bool (*accepts)(game::ItemId) = nullptr) {
            for (std::size_t i = 0; i < fromSlots; ++i) {
                game::ItemStack& source = from[i];
                if (source.empty()) {
                    continue;
                }
                if (accepts != nullptr && !accepts(source.item)) {
                    continue;
                }
                for (int pass = 0; pass < 2; ++pass) {
                    for (std::size_t j = 0; j < toSlots; ++j) {
                        game::ItemStack& into = to[j];
                        const bool usable =
                            pass == 0 ? (!into.empty() && game::slots::roomFor(into, source) > 0)
                                      : into.empty();
                        if (!usable) {
                            continue;
                        }
                        // **The move itself belongs to `slots`, not here.**
                        // This was the twelfth site writing a stack's fields by
                        // hand, and the primitive exists now precisely so it is
                        // the last: `moveOne` gives an empty destination the
                        // source's `damage` and empties the source when it hits
                        // zero. Tested rather than assumed - both passes above
                        // have already proved there is room, so a zero here
                        // would mean `roomFor` and `merge` disagree, and moving
                        // on is a better answer than reporting a transfer that
                        // did not happen.
                        if (game::slots::moveOne(into, source) > 0) {
                            return true;
                        }
                    }
                }
            }
            return false;
        };

        /// Which break this is, counted from the start of the session.
        ///
        /// **Gameplay state, not worldgen**, which is why it can exist at all:
        /// `dropHash` is otherwise a pure function of the cell, and a cell that
        /// grows its block back - a crop, a stem, a melon, a leaf - would
        /// otherwise pay the identical haul on every harvest until the end of
        /// the world. `resolveBreak` in `BlockDrops.hpp` is what decides which
        /// blocks this reaches; every one-shot feature ignores it and keeps the
        /// "same cell, same haul" rule gravel's flint was written with.
        ///
        /// It does not survive a restart, and does not need to: two breaks only
        /// have to disagree with each other, and the first break of a session
        /// repeating the first break of the last one is not worth a byte in the
        /// save file.
        std::uint32_t breakNonce = 0;

        /// Asks the frame loop to save at the end of this frame.
        ///
        /// A flag rather than a direct call, because closing a screen happens in
        /// the middle of the break path and the middle of the explosion loop -
        /// writing the whole world to disk from inside either would stutter the
        /// frame, and a blast that destroys four open containers would do it
        /// four times.
        bool savePending = false;

        /// True while the last save failed to write every table, so that keeping
        /// `savePending` set - which is what makes a failure retry rather than
        /// be forgotten - does not turn into a save attempt every frame. The
        /// retry waits for the autosave timer instead.
        bool saveFailing = false;

        // **The one place a destroyed block becomes items.** Mining it,
        // blasting it, washing it away, dropping a gravel column on it, pulling
        // its support out from under it - all of them come here, because a drop
        // is no longer "one item, this many": `dropsForBlock` can hand back
        // three entries and `resolveBreak` settles every count and every chance.
        //
        // **The roll is a pure function of the cell for anything you can only
        // harvest once**, so breaking the same gravel twice gives the same haul
        // twice and a reloaded save cannot re-roll it - while a block that grows
        // back takes a fresh roll each time, or the cell becomes a rate the
        // player owns rather than a thing they found. And **entries sharing a
        // salt are alternatives decided by one roll**, in table order - which is
        // why gravel gives flint *instead of* its gravel rather than beside it.
        // Two independent rolls at 10% and 90% would hand back both about one
        // time in eleven, which is precisely the bug the salt exists to close,
        // so nothing here may take a second roll per entry.
        //
        // `stowHandle` rides on the first stack: a stowbox's row is a single
        // identity entry, and that handle is the only thing standing between
        // its contents and being stranded in `stowboxes.dat` forever. The count
        // comes back so a caller that keeps a tally - the blast does - does not
        // have to resolve the drop a second time to get one.
        //
        // **The nonce is taken here rather than passed in**, so no call site can
        // forget it and no two breaks can share one. It is counted for every
        // break, whatever was broken, so what a renewable block rolls does not
        // depend on how much stone was mined in between.
        const auto spillBlockDrop = [&drops, &breakNonce](const glm::ivec3& at,
                                                          game::BlockId block,
                                                          const game::BreakContext& context,
                                                          int stowHandle = 0) -> int {
            const game::ResolvedDrop rolled =
                game::resolveBreak(block, context, at.x, at.y, at.z, ++breakNonce);
            const glm::vec3 centre = glm::vec3{at} + glm::vec3{0.5f};
            for (int i = 0; i < rolled.count; ++i) {
                game::ItemStack stack = rolled.stacks[static_cast<std::size_t>(i)];
                if (i == 0) {
                    stack.damage = stowHandle;
                }
                // **Through `dropStack`, never `spawn`** - it is the one helper
                // that carries `damage`, and `damage` is both a tool's wear and
                // which stowbox this is.
                game::dropStack(drops, centre, stack);
            }
            return rolled.count;
        };

        /// **What a landed cube owes, and it is not always the same debt.**
        ///
        /// One owner for both falling-block drains - the frame update and
        /// `settleAll` on the way out - because they were paying the identical
        /// channel two lines apart, so fixing one would have left the other.
        ///
        /// `FallingBlocks::Crushed` is a **union channel**: `applyFallingLanding`
        /// reports either the occupant a landing displaced *or* the faller itself
        /// when it broke, never both for one landing, so `hit.block` is always
        /// exactly the one thing that owes an item. That is what conserves the
        /// count. It does **not** say which arm sent it, and the two arms owe
        /// different items:
        ///
        /// - A displaced **occupant** owes the mining table. Sand landing on tall
        ///   grass really does drop what breaking that grass drops.
        /// - A **faller that broke** owes *itself*. Nothing was swung at it, so
        ///   gravel popped off a torch is gravel - and `resolveBreak` was paying
        ///   flint for it 10% of the time, measured at 9,942 in 100,000. The
        ///   count was right and the item was wrong, which is the one error no
        ///   conservation probe can see.
        ///
        /// `isFalling` separates them, and the strided sweep at the top of this
        /// file is what makes that a classification instead of a guess: the
        /// occupant arm is gated on `isReplaceable`, nothing that falls is
        /// replaceable, and every faller has an item to become. Give a
        /// replaceable block gravity and the build stops, rather than the item
        /// quietly changing.
        ///
        /// **Two of the twenty-five fallers are destroyed rather than dropped,
        /// and the drop table is what says which.** "It drops nothing if it
        /// breaks, and will break if it falls or is moved"
        /// (https://minecraft.wiki/w/Suspicious_Sand) - and on Bedrock that is
        /// the whole story, the "falls for more than 30 seconds and drops as an
        /// item" escape being Java only. Paying every faller its own item was
        /// therefore an overshoot in the opposite direction from the bug: it
        /// would have made suspicious sand obtainable, which on Bedrock it is
        /// not outside Creative. The guard is `primaryDrop == None`, read off
        /// the table that owns "what does destroying this give you" rather than
        /// written as a list of two ids - and a probe over the whole enum
        /// confirms it selects **exactly** Suspicious Sand and Suspicious
        /// Gravel out of the twenty-five, nothing else.
        ///
        /// **Bare hands, not the wash path's context.** The reference lists
        /// exactly three things that cut a cobweb - a sword, water and a piston -
        /// and a block falling on one is none of them.
        const auto payForCrushed = [&drops,
                                    &spillBlockDrop](const game::FallingBlocks::Crushed& hit) {
            if (game::isFalling(hit.block)) {
                if (game::primaryDrop(hit.block) == game::ItemId::None) {
                    return;
                }
                game::dropStack(drops, glm::vec3{hit.position} + glm::vec3{0.5f},
                                game::ItemStack{game::itemForBlock(hit.block), 1});
                return;
            }
            spillBlockDrop(hit.position, hit.block, game::BreakContext{});
        };

        // The chest at `at`, **rolled if it has never been touched**.
        //
        // A village places its chests as `LootChest` ids carrying nothing but
        // which table they owe; this is the one place that turns one into real
        // contents. It is **idempotent** - the second call finds a plain chest
        // and does nothing - which is what lets the open path ask twice for the
        // two halves of a double chest.
        //
        // **Every caller must reach here before it writes Air over the cell.**
        // The table lives in the block id and nowhere else, so a break or a
        // blast that clears the block first has nothing left to ask and spills
        // an empty chest.
        const auto materialise = [&](const glm::ivec3& at) -> game::Chest& {
            // **Asked before `chests[at]`, because that call is what creates
            // the entry it reads.** Both terms are here and neither is spare:
            // the flag catches a chest already emptied or broken, whose record
            // is empty or gone, and the map catches a save written before the
            // flag existed, which has a record and no flag.
            const bool firstTouch = chests.find(at) == chests.end() && rolledLoot.count(at) == 0;
            game::Chest& chest = chests[at];
            const game::BlockId id = world.blockAt(at.x, at.y, at.z);
            if (game::isLootChest(id)) {
                if (firstTouch) {
                    game::loot::rollInto(chest, game::loot::tableFor(id), world.seed(), at);
                }
                // **Two records of one fact, on purpose, because they survive
                // different things.** The block id says "rolled" by no longer
                // being a `LootChest`, and writing it flags the chunk modified,
                // so an emptied chest stays emptied. That is the right home for
                // it and it is enough for everything except the one event that
                // throws the chunk away and keeps the store - a
                // `kChunkFormatVersion` bump, after which worldgen puts the
                // marker back over a live record and this rolled a second time
                // on top of it. Measured before the guard: 11 items became 22
                // after one bump and 44 after three, and a chest the player had
                // already emptied handed back a full 11 every time.
                rolledLoot.insert(at);
                world.setBlock(at.x, at.y, at.z, game::plainChestFor(id));
                // **And the world is written at the end of this frame.** The
                // two halves of a rolled chest are saved by two different
                // things: the block goes out with its chunk, which happens on
                // unload as well as on a save, and the contents go out only
                // with `chests.dat`, which nothing but `saveEverything` writes.
                // Walk far enough away to unload the chunk and then lose the
                // process before the next autosave, and the world comes back
                // holding a plain empty chest - the marker id that made the
                // loot re-derivable from the seed has already been overwritten,
                // so it is gone for good. The flag collapses that window to one
                // frame.
                savePending = true;
            }
            return chest;
        };

        /// **The one place a campfire's contents become items.** Called from the
        /// break path and from the blast path, which is the whole reason it is a
        /// lambda rather than a loop written twice: the furnace above it *is*
        /// written twice, and that is exactly the shape - a rule that exists, is
        /// correct, and lives in only one of the two places that need it - which
        /// has cost this project more than any other.
        ///
        /// The reference always drops what is cooking, with no tool and no
        /// silk-touch exception, and it drops the raw item rather than the
        /// finished one however far along the timer was. Partial progress is not
        /// an item and there is nowhere to put it.
        const auto spillCampfire = [&](const glm::ivec3& at) {
            const auto found = campfires.find(at);
            if (found == campfires.end()) {
                return;
            }
            const glm::vec3 centre = glm::vec3{at} + glm::vec3{0.5f};
            for (const game::ItemStack& stack : found->second.items) {
                if (!stack.empty()) {
                    game::dropStack(drops, centre, stack);
                }
            }
            campfires.erase(found);
        };

        /// **The disc a jukebox is holding, handed back when the block goes.**
        ///
        /// One owner and three callers, for the reason the campfire above is
        /// one: `jukeboxDiscs` is keyed by cell and **nothing but a right-click
        /// ever emptied it**, so breaking or blowing up a loaded jukebox left
        /// the entry stranded at a cell that no longer held one. The disc was
        /// not merely dropped on the floor and missed - it stopped existing
        /// anywhere the player could reach, recoverable only by building a new
        /// jukebox on that exact block, and a music disc is a one-per-dungeon
        /// item. **The claim that used to stand here is quoted below so it is
        /// recognisable, and every line of it is marked, because an unmarked
        /// quotation of a false claim is indistinguishable from the claim to
        /// anything that searches by text - which has now cost this project
        /// three false reports in one day, one of them against this very
        /// paragraph.** RETIRED: "there is no periodic autosave, so
        /// RETIRED: 'it comes back on reload' meant 'quit the game and come
        /// RETIRED: back'." It is false, and it was load-bearing in the wrong
        /// direction. `kAutosaveInterval` is thirty seconds and `saveEverything`
        /// runs on it, so a reader reasoning about any of this file's
        /// empty-collection-deletes-the-file shapes would have taken that
        /// sentence as licence to assume the disk is only written at quit -
        /// which is exactly the assumption that makes `saveDrops({})` and
        /// `saveChests({})` look harmless. The stranding was real; the
        /// thirty-second window is the whole of how long you had.
        ///
        /// The reference's own rule, and the same one the right-click uses: a
        /// broken jukebox ejects its disc (https://minecraft.wiki/w/Jukebox).
        /// Through `dropStack`, like every other parting of a player and an
        /// `ItemStack`.
        const auto spillJukeboxDisc = [&](const glm::ivec3& at) {
            const auto found =
                std::find_if(jukeboxDiscs.begin(), jukeboxDiscs.end(),
                             [&](const std::pair<glm::ivec3, game::ItemId>& entry) {
                                 return entry.first == at;
                             });
            if (found == jukeboxDiscs.end()) {
                return;
            }
            game::dropStack(drops, glm::vec3{at} + glm::vec3{0.5f, 1.1f, 0.5f},
                            game::ItemStack{found->second, 1});
            *found = jukeboxDiscs.back();
            jukeboxDiscs.pop_back();
        };

        /// **"May something be put in this cell without deleting anything?"**
        ///
        /// Three sites ask it - lighting a fire, sowing a seed, planting nether
        /// wart - and all three used to spell it `inCell == Air ||
        /// isWashedAway(inCell)`, which was *nearly* right for as long as
        /// `isWashedAway` happened to mean "cross plants, vines and torches".
        /// The day it widened to the reference's real list, those three sites
        /// silently gained the power to write over a rail, a carpet, a redstone
        /// line, tripwire and any depth of snow, with no drop and no feedback.
        /// **They never asked "would a fluid sweep this away"; they asked "is
        /// this cell free", and the two only looked alike.** `isReplaceable` is
        /// the predicate for the second question and already excludes all five
        /// families - its own `static_assert` says so in as many words.
        ///
        /// The `!isFluid` half is the part `isReplaceable` alone does not give.
        /// Replacing water is correct for *placing a block* - we have no
        /// waterlogging, so the reference's own answer is that the water goes -
        /// but a torch of fire or a wheat seed must not delete a lake, and
        /// `fireCanSurvive` cannot catch it because it asks about the block
        /// below and the neighbours, never the cell itself.
        const auto cellIsFree = [](game::BlockId id) {
            return game::isReplaceable(id) && !game::isFluid(id);
        };
        // **Every one of its three callers spills first.** This says a cell may
        // be *built into*, not that it is empty: tall grass, a fern, a dead
        // bush, a large fern and a one-deep snow layer all pass and all owe an
        // item. Striking a fire or sowing a seed into one destroyed that item
        // until the `spillReplaced` calls beside each `setBlock` were added -
        // add a fourth caller and it needs the same line.

        /// **Somewhere to stand next to a bed, or nothing.** The other half of
        /// the reference's respawn rule, and the half the message at the death
        /// site claimed to check for twenty milestones without checking.
        ///
        /// Asked through `overlapsSolid`, which is the one owner of "does a
        /// body fit here" - a second answer written out of `isSolid` would be
        /// wrong for every slab, stair and fence the moment either changed, and
        /// wrong invisibly, since the failure is a respawn that looks fine and
        /// leaves you standing inside a wall.
        ///
        /// Order is the reference's: **beside the bed at its own level first**,
        /// then on top of it, then one down for a bed on a ledge. Nine columns
        /// in each ring, so a bed walled in on three sides still finds the open
        /// one.
        const auto standingRoomBeside =
            [&world](const glm::ivec3& bed) -> std::optional<glm::vec3> {
            constexpr int kRings[3] = {0, 1, -1};
            constexpr int kWorldTop = game::kWorldHeightChunks * game::Chunk::kSize;
            for (const int dy : kRings) {
                const int feet = bed.y + dy;
                if (feet < 1 || feet + 2 >= kWorldTop) {
                    continue;
                }
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const glm::vec3 at{static_cast<float>(bed.x + dx) + 0.5f,
                                           static_cast<float>(feet),
                                           static_cast<float>(bed.z + dz) + 0.5f};
                        constexpr float kHalf = game::player_constants::kWidth * 0.5f;
                        const game::Aabb box{
                            glm::vec3{at.x - kHalf, at.y, at.z - kHalf},
                            glm::vec3{at.x + kHalf, at.y + game::player_constants::kHeight,
                                      at.z + kHalf}};
                        if (game::overlapsSolid(world, box)) {
                            continue;
                        }
                        // And something under the feet, or this is a respawn
                        // into open air over a ravine. The same owner again -
                        // and the bed's own top counts, which is what stands
                        // you on the bed when all four sides are walled in.
                        if (game::worldCollisionBoxes(world, bed.x + dx, feet - 1, bed.z + dz)
                                .count == 0) {
                            continue;
                        }
                        return at;
                    }
                }
            }
            return std::nullopt;
        };

        /// **The other cell of a two-cell block, taken without paying for it.**
        ///
        /// A door, a bed and a tall flower are each two cells and one thing:
        /// whichever half goes, the other goes with it, and only one item is
        /// owed because both halves map back to the same item
        /// (`itemForBlock(SunflowerUpper)` is the sunflower). Paying twice
        /// duplicates the plant, which is why the paired *write* below and this
        /// paired *break* had to arrive in the same edit.
        ///
        /// It lives out here rather than inside `settleAround` because there are
        /// **three** callers, not one, and the third is the one that made this
        /// blocking. `settleAround` covers the mined and blasted cell;
        /// `spillReplaced` covers a cell written over, which the raycast can
        /// hand any occupant at all (finding 142's thin-box skim, and the bucket
        /// pour, which writes whatever cell it reached); and the **fluid wash
        /// drain** covers the one path that can take either half *on its own*,
        /// because a flow arrives one cell at a time. That last one was missing,
        /// and a bucket poured over a sunflower paid a flower for the top and
        /// then a second flower for the bottom, repeatably, out of one plant.
        ///
        /// Returns the cell it emptied, so a caller draining a batch can tell
        /// that a later entry in the same batch has already been paid for.
        ///
        /// > An earlier version of this comment claimed the tall-flower ids are
        /// > `isReplaceable`. **They are not** - `Block.hpp`'s list is air,
        /// > fluid, fire, the dry ground cover, the nether roots, vines and
        /// > one-deep snow, and `LargeFern` in it is a separate single-cell id.
        /// > Nothing that needs a twin carries the tag, which is why the wash
        /// > and not the placement was where the item was actually being
        /// > duplicated.
        const auto clearPairedHalf = [&world](const glm::ivec3& cell,
                                              game::BlockId removed) -> std::optional<glm::ivec3> {
            glm::ivec3 other = cell;
            bool paired = false;
            if (game::isDoor(removed)) {
                other.y += game::doorIsUpper(removed) ? -1 : 1;
                const game::BlockId twin = world.blockAt(other.x, other.y, other.z);
                paired = game::isDoor(twin) && game::doorFamily(twin) == game::doorFamily(removed);
            } else if (game::isBed(removed)) {
                // Lying down rather than standing up: one step along the facing,
                // forward from the foot or back from the head.
                other += stepAlong(game::bedIsHead(removed)
                                       ? game::oppositeDirection(game::bedFacing(removed))
                                       : game::bedFacing(removed));
                const game::BlockId twin = world.blockAt(other.x, other.y, other.z);
                paired = game::isBed(twin) && game::bedColour(twin) == game::bedColour(removed);
            } else if (game::isTallFlower(removed)) {
                // Bottom then top for each of the four, so the halves are one id
                // apart and that id *is* the family test - a lilac top is not a
                // sunflower bottom's other half.
                const int step = game::isTallFlowerUpper(removed) ? -1 : 1;
                other.y += step;
                paired = world.blockAt(other.x, other.y, other.z) ==
                         static_cast<game::BlockId>(static_cast<int>(removed) + step);
            }
            if (!paired) {
                return std::nullopt;
            }
            world.setBlock(other.x, other.y, other.z, game::BlockId::Air);
            return other;
        };

        /// **Writing a block into an occupied cell is a break, so it drops.**
        ///
        /// minecraft.wiki *Block* names exactly three ways a block is broken
        /// without its sound or its particles: washed away by a fluid,
        /// **replaced by another block**, and its supporting block removed. All
        /// three are breaks and all three still pay their drop - only the noise
        /// is suppressed. Two of them were honoured here and the third was not:
        /// the wash spills below, losing a support spills through
        /// `settleAround`, and a placement simply wrote over whatever was
        /// standing there. **A rule that did not travel**, and the proof it was
        /// never a decision is that nearly every replaceable id is *also* washed
        /// away - so the identical block dropped when water reached it and
        /// dropped nothing when a block did. A four-high candle stack was the
        /// sharpest case: one right-click, four candles gone.
        ///
        /// **The numbers, measured, because the ones that used to stand here
        /// were wrong by an order of magnitude and argued for a `static_assert`
        /// that would not hold.** Of 3309 ids, **27** are replaceable and
        /// neither Air nor a fluid - Air owes nothing and returns early below -
        /// and **7** of those pay something to a bare hand. **26 of the 27 are
        /// `isWashedAway`, and the exception is Glow Lichen**, which the
        /// reference waterlogs rather than breaks. So this is very nearly a
        /// subset and deliberately not asserted as one.
        ///
        /// **The occupant is not always something `isReplaceable` allowed.**
        /// `Raycast` walks `worldSelectionBoxes`, and a rail, carpet, redstone
        /// line, tripwire or thin snow box is a sixteenth of a block tall, so a
        /// near-horizontal ray skims over it, strikes the side face of the block
        /// beyond and hands back a normal pointing *back into* the cell that
        /// holds it. That is a further 91 ids - the `Flat` family, of which
        /// exactly one is replaceable, so all but one of them arrive here by
        /// this route and no other - and they reach every writer of a cell,
        /// which is why this asks what is there rather than trusting how the
        /// cell was chosen.
        ///
        /// Bare hands by default, for the same reason `settleAround` uses them:
        /// nothing was swung at it. Air and fluids return early rather than
        /// being rolled - neither owes anything, and a roll would spend a break
        /// nonce for nothing.
        ///
        /// > A container is out of scope here on purpose. Every container id is
        /// > non-replaceable, and the only occupant a *non*-replaceable cell can
        /// > hand this helper is the thin-box skim above, whose victim set is
        /// > the 89 `Flat` ids and holds no block entity. Spilling twenty-seven
        /// > slots would mean a second copy of the break path's container rules,
        /// > which is the one thing this project pays for hardest.
        const auto spillReplaced = [&](const glm::ivec3& cell,
                                       const game::BreakContext& context = game::BreakContext{}) {
            const game::BlockId standing = world.blockAt(cell.x, cell.y, cell.z);
            if (standing == game::BlockId::Air || game::isFluid(standing)) {
                return;
            }
            spillBlockDrop(cell, standing, context);
            // **The occupant may be half of something.** No two-cell block is
            // `isReplaceable`, so the cell was never *chosen* for holding one -
            // but this helper is handed cells the ray only skimmed and the cell
            // a bucket reached, and either can be a door half or a bed foot.
            // Leaving the other half standing on nothing is the same bug in a
            // different path, so it asks the same owner the break path does.
            clearPairedHalf(cell, standing);
        };

        // Two chests shoulder to shoulder open as one. **Which of a row pairs
        // with which is worked out from position alone, never remembered** - so
        // it survives a reload, costs no saved state, and cannot disagree with
        // itself depending on who asked. `World` owns that walk, because the
        // mesher needs the same answer to pick each half's texture and a second
        // copy of the rule is exactly how the two would drift apart.
        const auto chestPartnerAt = [&world](glm::ivec3 at) {
            return world.chestPartnerAt(at);
        };

        // The population survives a restart rather than being rebuilt from
        // scratch. Anything the player has walked away from since is retired by
        // the first `manage`, so a stale saved position corrects itself.
        //
        // **The showcase reads none of it.** It is a review mode, and loading
        // the world's animals into a frozen roster is exactly what stopped "one
        // species alone" being one species - they arrive after the subjects are
        // placed and, with the spawner off, nothing ever retires them.
        //
        // **`<= 0`, the negation of the one test that turns the showcase on**
        // (`if (settings.creatureShowcase > 0)`, where the subjects are placed).
        // It cited that test by line number and the number had rotted into an
        // unrelated `applyUpdates` call - measured tree-wide, 13 of 18
        // line-numbered citations were stale, so they are named rather than
        // numbered now. "On" is `> 0` there and "off" was `== 0` at the three
        // sites that turn the
        // world's own creatures back on, so a negative `creature_showcase` fell
        // into a dead zone where every branch took "no": no showcase, no
        // restore, no spawner - and, because the save path asks the same way,
        // **no `saveCreatures`, which silently deleted the population.**
        if (settings.creatureShowcase <= 0) {
            // **Counted off the roster rather than off the loop.** Two guards can
            // drop a record and they are not the same guard: this one checks the
            // raw `std::int32_t` as it came off the disk, and `Creatures::restore`
            // checks the `CreatureKind` it was handed - which is a `std::uint8_t`,
            // so a stored 300 arrives there as a perfectly valid 44 and only the
            // check here can refuse it. Both stay; a `++` beside the call would
            // count what was attempted, and the line says "Restored".
            const std::size_t before = creatures.count();
            for (const game::SavedCreature& saved : world.store().loadCreatures()) {
                if (saved.kind < 0 ||
                    saved.kind >= static_cast<std::int32_t>(game::CreatureKind::Count)) {
                    continue;
                }
                // **The three claim cells travel with the villager.** Without
                // them an armourer reloads employed but owning nothing: it can
                // never work again, it is refused a fresh claim because it
                // already has a profession, and the next villager to wake takes
                // the same anvil - so the village quietly ends up with two of
                // every trade. `kCreatureVersion` was bumped to 4 for this.
                creatures.restore(static_cast<game::CreatureKind>(saved.kind),
                                  glm::vec3{saved.x, saved.y, saved.z}, saved.yaw, saved.health,
                                  saved.scale, saved.charged != 0, saved.playerBuilt != 0,
                                  saved.profession, saved.bedCell, saved.jobCell, saved.meetCell);
            }
            const std::size_t restored = creatures.count() - before;
            if (restored > 0) {
                engine::logInfo("Restored " + std::to_string(restored) + " creatures.");
            }
            // **The marker set, and it is not derivable from the loop above.**
            // A column stays populated after everything in it has been eaten,
            // and quitting anywhere but on top of a village means its animals
            // are not in memory to imply it - so rebuilding this from the
            // restored creatures marks nothing, the one-off pass runs again on
            // load, and the population grows every session until the village is
            // solid villagers.
            //
            // **Unpacked at both ends on purpose.** `populatedKey` is private to
            // `Creature.cpp` and must stay the only thing that packs a column;
            // `WorldStore` stores two `std::int32_t` and this loop hands them
            // straight back, so neither file is a second owner of the packing.
            // If it were, changing the packing would silently stop matching
            // everything already on disk, with no error and no warning.
            //
            // Inside this gate deliberately, and symmetric with the save: the
            // showcase leaves the world's own creature state alone at both ends
            // rather than restoring half of it.
            for (const game::PopulatedColumn& column : world.store().loadPopulatedColumns()) {
                creatures.restorePopulatedColumn(column.x, column.z);
            }
        }

        // **The floor comes back too, and this is the call site the format had
        // been waiting for.** Landed 2026-08-19. `WorldStore::loadDrops`,
        // `saveDrops`, `SavedItem` and its `sizeof == 48` assert were written,
        // proved on bytes and left with no caller anywhere - `CLAUDE.md` bug
        // shape #15 - so everything lying on the ground at quit was deleted: a
        // death drop you had not walked back to yet, a chest's contents spilled
        // by a creeper, a mining trip's worth of stacks stacked outside the
        // entrance. It read as the game eating your things, because it was.
        //
        // **Through `ItemEntities::restore`, never `spawn`.** `spawn` is the
        // gameplay entry and every one of its three habits is wrong here: it
        // adds a pop-out impulse, so the floor would scatter a little further
        // from the truth on every load; it starts `age` at zero, so nothing
        // dropped would ever reach the five-minute despawn across a save and a
        // reload would be a way to keep a stack forever; and it takes the stack
        // in three loose parts, which is the shape that puts the damage in the
        // count. `restore` keeps both clocks exactly as they were, so an item
        // thrown in the last second before quitting comes back still inside its
        // `kThrowPickupDelay` window rather than being handed straight back.
        //
        // **Nothing is validated here.** `loadDrops` sanitises the stack, the
        // finiteness, the position and the sign of both clocks on the way out,
        // so a second set of rules on this side would be a second owner for a
        // settled question. `ItemEntity.hpp` says the same over `restore`.
        //
        // **Gated on BOTH review modes, symmetric with the save**, for exactly
        // the reason the creature showcase is gated above. `dropShowcase` is
        // the obvious one: it is a review mode that fills the floor with one of
        // everything, and loading the world's real drops into that frozen row -
        // then writing the row back out - would inject thirty-odd free items
        // into the world every time anyone looked at a model. **The creature
        // showcase needed the same clause and did not have it**, which is the
        // rule-that-did-not-travel shape: `Settings.cpp` states that mode's
        // contract as "the world's own animals are neither loaded nor saved
        // while it is on", the roster it spawns in front of you is killable, and
        // with the floor now persisted every showcased sheep killed for a look
        // at its death animation was banking wool into the real world's
        // `drops.dat` for the next ordinary session to collect.
        //
        // **`<= 0`, never `== 0`.** A negative `creature_showcase` used to fall
        // into a dead zone where every branch answered "no" - no showcase, no
        // restore, no spawner, and no `saveCreatures`, which silently deleted
        // the population. The comment above `loadCreatures` tells that story;
        // this asks the question the same way so it cannot open a second one.
        //
        // **One owner for "is this a review mode", and `dropsPersist` is
        // derived from it rather than repeating the test.** The two are exactly
        // De Morgan's of each other - `!(a || b > 0)` is `!a && b <= 0` - so
        // this is rung 1 of the ladder and the pair cannot drift. It matters
        // because a *third* reader has just arrived: `saveEverything` refuses
        // its whole body while `showcasing`, and gating the floor on one
        // spelling of the question while gating the disk on another is how a
        // review mode ends up half-sealed.
        const bool showcasing = settings.dropShowcase || settings.creatureShowcase > 0;
        const bool dropsPersist = !showcasing;
        if (dropsPersist) {
            // Counted off the roster rather than off the loop, matching the
            // creature restore: `loadDrops` already dropped whatever it refused,
            // so the loop count would report what was on disk and the line says
            // "Restored".
            const std::size_t dropsBefore = drops.count();
            for (const game::SavedItem& saved : world.store().loadDrops()) {
                drops.restore(saved.position, saved.velocity, saved.stack, saved.age,
                              saved.pickupDelay, saved.onGround != 0);
            }
            const std::size_t restoredDrops = drops.count() - dropsBefore;
            if (restoredDrops > 0) {
                engine::logInfo("Restored " + std::to_string(restoredDrops) + " dropped items.");
            }
        }

        // Which furnace the open screen is looking at, if any.
        glm::ivec3 openFurnacePosition{0};
        // And which workbench, stonecutter, anvil, grindstone, brewing stand or
        // smithing table. **Only the furnace and the containers had one**, so
        // breaking the table you were standing at left its screen open with a
        // live crafting grid - and the ingredients in that grid were still
        // yours to take from a block that no longer existed.
        glm::ivec3 openBenchPosition{0};
        glm::ivec3 openChestPosition{0};
        // The second half of an open double chest, and equal to the first when
        // there is only one, so nothing has to ask which case it is.
        glm::ivec3 openChestPartner{0};
        game::ItemStack furnaceOutput;

        // A sweep with the button held spreads the cursor's stack over every
        // slot it crosses.
        //
        // The distribution is recomputed from scratch every frame rather than
        // applied incrementally, because adding one more slot changes every
        // other slot's share - so each frame has to be able to undo the last.
        //
        /// **A sweep owns what the sweep put there, and nothing else.**
        ///
        /// This was a *snapshot*: each swept slot's contents from before the
        /// sweep, written back verbatim. That is sound only while nothing
        /// outside the screen may write those slots with the button down, and
        /// **three things may**. A furnace keeps smelting under an open screen,
        /// so holding the button on its input slot restored the input the tick
        /// had already eaten and the ingots were free - 65 raw iron and 8 coal
        /// in, 65 raw iron, 7 coal and **2 iron ingots** out, once per smelt,
        /// for as long as the button was held. A hopper keeps draining a chest
        /// the same way: 32 diamonds in the chest and 8 on the cursor came back
        /// out as 40 in the chest **and** 20 in the hopper. And a drop picked
        /// up off the floor tops up the very slot the sweep photographed, so
        /// the rewind erased it - 32 coal plus 16 held plus 5 collected gave
        /// back 48.
        ///
        /// **Three symptoms, one question**: what may mutate a slot while a
        /// drag holds a claim on it? Guarding the three known writers is three
        /// answers to remember separately and a fourth writer that arrives
        /// unguarded - and the third of them, the pickup, writes an ordinary
        /// inventory slot, so a fix that only re-reads the block-entity views
        /// closes two of the three and reads as complete.
        ///
        /// So the drag stops making a claim it cannot defend. It records **how
        /// many it deposited**; a rewind takes back at most that many, only
        /// while the slot still holds that item at that damage, and hands the
        /// cursor exactly what it recovered - through `slots::merge`, which is
        /// this project's one owner of item, damage and stack cap. Anything
        /// else that happened to the slot in between simply stays happened,
        /// which is what balances the arithmetic for every writer, including
        /// the ones not written yet.
        ///
        /// **The single edit that breaks this:** storing the slot's contents
        /// here instead of the deposit, or restoring the cursor from a
        /// remembered value rather than from what the rewind actually
        /// recovered. Either one is the snapshot again.
        enum class DragButton { None, Left, Right };
        struct DragDeposit {
            game::inventoryScreen::SlotHit at;
            // What was put there, so a slot whose contents were replaced
            // wholesale by something else is recognised and left alone.
            game::ItemId item = game::ItemId::None;
            int damage = 0;
            // Zero for a slot the distribution skipped - recorded anyway,
            // because "the sweep never left its first slot" is a question about
            // how many slots were crossed, not about how many took anything.
            int placed = 0;
        };
        DragButton dragButton = DragButton::None;
        std::vector<DragDeposit> draggedSlots;

        /// **Ends a sweep wherever it has got to, committing it rather than
        /// rewinding it.** One owner, because three things outside the drag
        /// block need it and none of them can call `rewindDrag`.
        ///
        /// Committing is the safe half of the choice: `applyDrag` is
        /// rewind-then-`distribute`, so at the end of every frame the slots
        /// hold their share and the cursor holds the remainder and the sum is
        /// already right. Rewinding here would need `stackAt`, which resolves a
        /// `SlotHit` against **whichever screen is open now** - so a deposit
        /// recorded in a chest would be taken back out of a hopper slot no
        /// screen can reach.
        ///
        /// **This is what closes the duplication.** The variables above outlive
        /// the screen block that reads them, and only that block ever cleared
        /// them. A sweep armed in a chest therefore survived `closeScreen`,
        /// which had already handed the cursor stack back to the inventory -
        /// and the next screen to open rewound first, putting the same items on
        /// the cursor a second time. Sixty-four cobble in, a hundred and
        /// twenty-seven out, with any item, in any container, as fast as a
        /// screen can be reopened.
        const auto cancelDrag = [&] {
            dragButton = DragButton::None;
            draggedSlots.clear();
        };

        // Two left clicks on one slot inside this window pull every matching
        // item onto the cursor.
        constexpr auto kDoubleClickWindow = std::chrono::milliseconds(350);
        auto lastSlotClick = Clock::now();
        game::inventoryScreen::Region lastClickRegion = game::inventoryScreen::Region::Grid;
        std::size_t lastClickIndex = ~std::size_t{0};

        std::size_t selectedSlot = 0;
        // What they were carrying, restored here rather than beside the rest of
        // the saved player because this is where the inventory first exists -
        // and **after** the creative starting kit, so reopening a world hands
        // back what was in your hands rather than a fresh set of blocks.
        //
        // **Checked rather than trusted, for the same reason the health is**:
        // an item id out of a corrupt file would index the name and sprite
        // tables straight off the end, so anything outside the run is dropped
        // and every count is clamped to what that item may actually stack to.
        if (savedPlayer.has_value()) {
            // A version 2 world has no inventory in it at all, and neither has
            // one saved with every slot empty. Either way there is nothing to
            // restore, and wiping on the strength of it would throw away the
            // creative starting kit for no reason.
            const bool carried =
                std::any_of(savedPlayer->inventory.begin(), savedPlayer->inventory.end(),
                            [](const game::ItemStack& stack) { return stack.count > 0; });
            for (std::size_t i = 0; carried && i < game::kInventorySlots; ++i) {
                game::ItemStack& into = inventory.slot(i);
                // Assigned rather than merged, so the saved inventory is the
                // whole answer: a slot deliberately emptied stays empty instead
                // of being restocked by the starting kit above.
                //
                // **Through `game::sanitiseStack`, which is the single owner of
                // this rule.** It was written here as a local lambda, correctly,
                // and then not carried to the chest, furnace and stowbox
                // restores - so the one door that was guarded had three
                // unguarded ones beside it. `WorldStore` now exports it and the
                // copy here is gone.
                into = savedPlayer->inventory[i];
                game::sanitiseStack(into);
            }
            selectedSlot = static_cast<std::size_t>(
                std::clamp<std::int32_t>(savedPlayer->selectedSlot, 0,
                                         static_cast<std::int32_t>(game::kHotbarSlots) - 1));
            // **The ender chest, which no save path ever saw.** It belongs to
            // the player rather than to a block, so it has no position, no entry
            // in `chests` and no place in `chests.dat` - and the one block the
            // reference promises is safe came back empty every launch. Through
            // the same rule as everything else, because a record read off disk
            // is a record read off disk - and `sanitiseChest` is that rule for
            // a whole container, so this is one call rather than a loop.
            enderChest = savedPlayer->enderChest;
            game::sanitiseChest(enderChest);
            // **The worn set, restored outside the `carried` gate on purpose.**
            // That flag asks "did the saved *bag* have anything in it", and a
            // player who quit wearing a full set with an empty inventory is an
            // ordinary thing to be - gating the armour on it would take the set
            // off exactly the player who had spent everything else.
            //
            // Assigned rather than merged, like the bag above, so a piece
            // deliberately taken off stays off.
            //
            // **No second sanitise.** `WorldStore::loadPlayer` runs
            // `sanitiseStack` over `player.armour` at the trust boundary, and
            // `armourSet` independently declines to count a piece sitting in
            // the wrong cell - so a corrupt file can put a sword in the head
            // slot and it grants nothing. A third check here would be a third
            // owner of one rule.
            for (std::size_t i = 0; i < game::kArmourSlots; ++i) {
                inventory.armourAt(i) = savedPlayer->armour[i];
            }
        }
        bool hudDirty = true;
        /// Whether the camera itself is inside a water cell, which is a
        /// different question from `Player::inWater` - that one asks about the
        /// whole body, and being waist deep does not change what you see.
        bool eyeUnderwater = false;
        bool eyeInLava = false;

        /// Every stack an open screen is holding that lives **nowhere else**:
        /// the cursor, then the crafting grid behind it. Both exist only while
        /// the screen is up, so whoever is asked here is who has to be given
        /// them back or write them down.
        ///
        /// **A furnace's two slots are not on the list.** They are a view onto
        /// the block, which owns them and is saved in its own file, so handing
        /// them over would hand out a free copy of whatever is smelting.
        ///
        /// One owner of that rule, because two things need the answer - closing
        /// a screen and saving the world - and the whole reason the autosave
        /// reloaded a world with the cursor stack missing is that only one of
        /// them knew about it.
        const auto forEachScreenStack = [&](auto&& visit) {
            visit(heldStack);
            if (openScreen != game::inventoryScreen::Kind::Furnace) {
                for (game::ItemStack& slot : craftSlots) {
                    visit(slot);
                }
            }
        };

        // **One owner of "the search field lost the keyboard".** Four places
        // let it go - the screen closing, a tab clicked, a tab cycled with the
        // stick, and a click landing anywhere but the field - and they are the
        // four that would each have to remember to reconcile the caret if the
        // query ever started being cleared on blur. Today none of them clears
        // it, which is exactly why this exists now rather than later: a fifth
        // caller is free, and adding `setQuery({})` here later is one edit
        // instead of four, three of which would be forgotten.
        //
        // `clampCaret` is the cheap half of the same guarantee - it is
        // idempotent, and it is what stops a caret outliving its string if
        // anything ever shortens `query` without going through the mutators.
        const auto blurSearch = [&] {
            catalogue.searchFocused = false;
            catalogue.clampCaret();
        };

        // Hands back everything the screen was holding: the cursor stack, then
        // the crafting grid. **Every** way of closing has to do this, which is
        // why it is one function - Escape used to close without it and stranded
        // whatever was in the grid.
        const auto closeScreen = [&] {
            // **First, and unconditionally.** A sweep still armed when the
            // screen goes away is the one way an item can be handed back here
            // *and* claimed by the next screen's first rewind - see
            // `cancelDrag` for the full journey. At the top rather than beside
            // `openScreen.reset()` so that every line below it, and every
            // screen kind added later, is already downstream of the one exit
            // path that reconciles the cursor.
            //
            // **The single edit that breaks this:** move this call below
            // `forEachScreenStack(giveBack)`. Under the snapshot rewind that
            // put the cursor's stack in the bag and then refilled the cursor
            // from the snapshot, for a free copy; now it leaves live deposits
            // whose `SlotHit`s the next screen resolves against *its* slots, so
            // the sweep takes its share back out of whatever container is open
            // next.
            cancelDrag();
            const auto giveBack = [&](game::ItemStack& stack) {
                if (stack.empty()) {
                    return;
                }
                // **`damage` on both halves.** It is a tool's wear and it is
                // which stored contents a stowbox holds, so the old pair - an
                // `add` and a `spawn` that both dropped it - handed back a
                // repaired tool and an empty box every time a screen closed.
                const int left = inventory.add(stack.item, stack.count, stack.damage);
                if (left > 0) {
                    game::dropStack(drops, camera.position + camera.forward() * 0.5f, stack, left,
                                    camera.forward() * kThrowSpeed, kThrowPickupDelay);
                }
                stack = game::ItemStack{};
            };

            forEachScreenStack(giveBack);
            // A furnace keeps what is inside it - the slots were only ever a
            // view onto the block, so they are DISCARDED here rather than
            // handed back. Leaving them populated would show the furnace's
            // contents in the next screen's crafting grid, and hand the player
            // a free copy when *that* screen closed. A crafting grid's own
            // contents do come back, because they exist only while it is up,
            // which is the split `forEachScreenStack` above owns.
            if (openScreen == game::inventoryScreen::Kind::Furnace) {
                craftSlots.fill(game::ItemStack{});
                furnaceOutput = game::ItemStack{};
            }
            blurSearch();
            // A lid closing is placed at the chest, not at the ear: you hear it
            // from wherever you walked off to.
            if (openScreen == game::inventoryScreen::Kind::Chest ||
                openScreen == game::inventoryScreen::Kind::DoubleChest) {
                sounds.play(audio, game::SoundEvent::ChestClose,
                            glm::vec3{openChestPosition} + glm::vec3{0.5f}, 0.6f);
            }
            openScreen.reset();
            window.setCursorCaptured(true);
            hudDirty = true;
            // A screen closing is when a chest, a furnace or the crafting grid
            // last changed hands, so it is the cheapest honest moment to write
            // the lot down.
            savePending = true;
        };

        /// **Whether the open screen is a window onto `cell`.** One owner for a
        /// question the break path and the blast path each answered with their
        /// own expression, and neither answer covered the benches: a crafting
        /// table, stonecutter, anvil, grindstone, brewing stand or smithing
        /// table could be destroyed while you stood in its screen, and the grid
        /// kept working over a block that was now Air. Closing it returns the
        /// grid's contents through `closeScreen`, which is the only path that
        /// reconciles them.
        const auto screenLooksAt = [&](const glm::ivec3& cell) {
            if (!openScreen.has_value()) {
                return false;
            }
            switch (*openScreen) {
            case game::inventoryScreen::Kind::Furnace:
                return openFurnacePosition == cell;
            case game::inventoryScreen::Kind::Chest:
            case game::inventoryScreen::Kind::DoubleChest:
            case game::inventoryScreen::Kind::Hopper:
                return openChestPosition == cell || openChestPartner == cell;
            case game::inventoryScreen::Kind::CraftingTable:
            case game::inventoryScreen::Kind::SmithingTable:
            case game::inventoryScreen::Kind::Stonecutter:
                return openBenchPosition == cell;
            // The player's own screen is not attached to a block at all, so no
            // block being destroyed can close it. **Named rather than left to a
            // `default:`**, so a new screen kind is a compile error here instead
            // of silently answering "no" and reopening this bug.
            case game::inventoryScreen::Kind::Inventory:
                break;
            }
            return false;
        };

        /// **Hands the player an item and puts on the floor whatever did not
        /// fit.** `Inventory::add` returns the count it could **not** take, and
        /// four paths through the interaction block below simply discarded that
        /// return: emptying a bucket, swapping one at a cauldron, taking bone
        /// meal out of a composter and filling a glass bottle each destroyed
        /// what they were handing over the moment the inventory was full. The
        /// bookshelf path a few hundred lines away got it right and said why,
        /// and the rule did not travel - so this is the one owner of it, and a
        /// fifth path cannot reintroduce the deletion.
        ///
        /// **`damage` goes in and comes out**, and the remainder leaves through
        /// `dropStack` rather than `spawn`, which is exactly why `dropStack`
        /// exists.
        const auto giveOrDrop = [&](const game::ItemStack& stack, const glm::vec3& where) {
            if (stack.empty()) {
                return;
            }
            const int left = inventory.add(stack.item, stack.count, stack.damage);
            if (left > 0) {
                game::dropStack(drops, where, stack, left);
            }
            hudDirty = true;
        };

        /// **Wears the held tool and destroys it once it is spent.** One owner
        /// for the three places that do it - breaking a block, working the
        /// world with a hoe, shovel, shears or axe, and landing a blow - which
        /// between them carried three copies of the same six lines, and only
        /// one of them made a sound when the tool finally snapped. `ItemBreak`
        /// was decoded at startup and never played.
        ///
        /// `points` is 1 everywhere except an attack with something that is not
        /// a sword or a hoe, which Bedrock charges 2 for
        /// (https://minecraft.wiki/w/Durability).
        const auto wearTool = [&](game::ItemId tool, int points) {
            if (creative || points <= 0) {
                return;
            }
            const game::ToolProperties properties = game::toolFor(tool);
            if (properties.durability <= 0) {
                return;
            }
            game::ItemStack& slot = inventory.slot(selectedSlot);
            slot.damage += points;
            if (slot.damage >= properties.durability) {
                slot = game::ItemStack{};
                sounds.playGlobal(audio, game::SoundEvent::ItemBreak, 0.9f);
            }
            hudDirty = true;
        };

        /// **Lands every cube still in the air, because a save writes the hole
        /// it came out of and not the cube.** Finding 873.
        ///
        /// `World::updateFalls` air-writes the source cell the moment a column
        /// starts falling and flags that chunk modified, so a cube in flight
        /// when the world is written is a block **deleted** from the saved world
        /// rather than merely misplaced: knock the base out of a twenty-high
        /// sand pillar, lose the process inside the two seconds it takes to
        /// fall, and twenty sand never existed. Measured over the real landing
        /// rule at twenty cubes above bedrock: before this ran, in 20, world 0,
        /// items 0 - all twenty annihilated; after it, in 20, world 20, items 0.
        ///
        /// **Chosen over persisting the flight state**, which would have cost a
        /// new `WorldStore` section and a format-version bump to keep something
        /// that lasts under two seconds and that no player can tell apart from
        /// "it had already landed". `FallingBlocks::settleAll` walks each cube
        /// down through the same `fallingCubeRest` the frame update uses and
        /// lands it through the same `applyFallingLanding`, so this is not a
        /// second, simpler rule that happens to agree today.
        ///
        /// **The two terminal saves only, never the autosave timer**, and that
        /// is the one judgement here. The landing goes through `setBlock`, which
        /// is what flags a chunk modified, so it rides out with the save that is
        /// about to happen either way - but on the timer it would also be
        /// *visible*, snapping a collapse to the floor in front of a player who
        /// is still playing. What it would buy there is the window between an
        /// autosave and the landing, which only matters if the process is killed
        /// outright inside it - and that is the one case no save path can reach.
        ///
        /// **The vector it returns has to be drained or settling loses items.**
        /// A cube that finishes its fall on a torch or a slab becomes an item
        /// rather than a block, so it goes through `payForCrushed` - the *same*
        /// owner the frame drain uses, and sharing it is the point rather than
        /// tidiness. This called `spillBlockDrop` directly until finding 888,
        /// which means a gravel column settled at shutdown was paid flint 10% of
        /// the time exactly as the frame drain was, and a fix to one of the two
        /// would have read as complete. **Those items used to be destroyed
        /// immediately afterwards by finding 592 - dropped items had no save
        /// file - and since 2026-08-19 they are written out with the rest**, so
        /// this ordering now matters rather than merely being tidy: this
        /// destructor runs `settleFallers` *before* `save`, so a cube that lands
        /// on a slab at shutdown becomes an item and that item reaches
        /// `drops.dat`. Reverse the two and it is lost again.
        const std::function<void()> settleFallersBeforeFinalSave = [&] {
            for (const game::FallingBlocks::Crushed& hit : fallingBlocks.settleAll(world)) {
                payForCrushed(hit);
            }
        };

        /// **Everything that "save the world" means, in one place.**
        ///
        /// It used to exist only after the frame loop, so the crash path threw
        /// it away wholesale: `vkCheck` throws on any Vulkan failure and a
        /// `VK_ERROR_DEVICE_LOST` is a thing laptop GPUs really do. Terrain that
        /// unloaded mid-session is written as it goes, so what survived a crash
        /// was a world where the chest is still standing and empty.
        ///
        /// Cheap enough to run on a timer: `saveAll` writes only chunks flagged
        /// modified and clears the flag, so a second call moments later writes
        /// nothing at all.
        ///
        /// **Returns whether every table wrote**, because the caller clears the
        /// dirty flag on the strength of it. Discarding these five results and
        /// then logging success was the `saveIfModified` shape one layer up: the
        /// writer announces its own failure, and then this function contradicts
        /// it and the caller clears the flag, so a world that failed to write is
        /// marked clean and never retried.
        const std::function<bool()> saveEverything = [&] {
            // **A review mode writes nothing at all, and the gate is here
            // rather than on each writer.** Both showcases are non-destructive
            // by contract - `Settings.cpp` states it for creatures, and
            // `dropShowcase` fills the floor with one of everything purely to
            // be looked at. Three writers were gated to honour that
            // (`loadDrops`, `saveDrops`, `saveCreatures`) and the rest were
            // not, so the floor had a **fourth exit and the bag was it**: the
            // showcase spawns forty-one items, `ItemEntities` homes anything
            // within two metres onto the player at seven metres a second and
            // collects it inside seven tenths of one, collection is
            // mode-independent by design, and walking up to a model is how you
            // review it. An enchanting table, an anvil, an end portal frame and
            // thirty-eight more were then minted into the real `player.dat`
            // every time anyone looked. Zero removed from the world, up to
            // forty-one added to a file that outlives the session.
            //
            // **Gating `savePlayer` alone would not have closed it.** The bag
            // is only the widest door; put a minted anvil in a chest and
            // `saveChests` carries it, a stowbox and `saveStowboxes` does, a
            // furnace slot and `saveFurnaces` does. Refusing the body is the
            // only version of this fix that closes the class rather than the
            // instance - and it is also the only one that stays closed when
            // somebody adds a sixth table here, which on this file's history is
            // a matter of days.
            //
            // **Returns true, and that is not the "contradict the writer" bug
            // this lambda's own doc block warns about.** Nothing failed; nothing
            // was attempted. Saying false would mark the world permanently
            // dirty, set `saveFailing`, and print "the shutdown save did not
            // write everything" on every exit from a mode that is working
            // exactly as intended.
            //
            // **Falsified by**: `showcasing` acquiring a reader that is not
            // this line, the drop restore or `dropsPersist`.
            //
            // **This gate is named `showcasing`, and a search for "showcase"
            // cannot find it - in any case-mode.** Not a style note: two
            // separate reviews have now filed this very block as an ungated
            // fourth exit for the player's bag, both because their probe swept
            // the lambda body for a `Showcase` token, matched `dropShowcase`
            // and `creatureShowcase` elsewhere, and concluded that no gate
            // stood above `savePlayer`. The spelling is the entire cause:
            // "showcasing" is "showcas" plus "ing", and "showcase" ends in an
            // `e`, so it is not a substring of it at all. Measured 2026-08-19:
            // case-sensitive `Showcase` and case-insensitive `showcase` both
            // return false against this line, while `showcasing` returns true.
            //
            // **The fleet's known trap is a token that is a PREFIX of a longer
            // name and so over-matches. This is the exact inverse**: an
            // inflected form sharing a stem without being a superstring, so it
            // under-matches to nothing - and a zero reads as a clean absence.
            // `showcas` finds every form; so does this predicate's own name.
            //
            // **Consequence for anyone auditing the writers below**: this
            // return dominates all eight of them, so none carries a gate of
            // its own and none needs one. A per-writer sweep asking whether
            // each call site is individually guarded answers "no" truthfully
            // for all eight and still yields the wrong verdict. Wrapping any
            // of them in `if (dropsPersist)` would compile and do nothing:
            // `dropsPersist` is declared `!showcasing`, so past this line it
            // is provably true and the condition is dead.
            if (showcasing) {
                return true;
            }
            // **Which tables did not write, by name.** Named rather than
            // counted, because they are not equally recoverable. Furnaces,
            // stowboxes and creatures mostly re-derive or are simply lost;
            // `chests.dat` is the one that cannot heal, and it is worth knowing
            // it was the one that failed. `materialise` overwrites the
            // `LootChest` marker in the *block* - which rides out with the chunk
            // and succeeds on its own path - while the rolled contents live only
            // here, so losing this write loses the marker that made the loot
            // re-derivable from the seed.
            //
            // **`saveAll` is not in this count and cannot be**: it returns
            // `void`, so a chunk that failed to write is invisible from here.
            // That half belongs to `World.hpp`'s owner.
            std::string unwritten;
            const auto wrote = [&unwritten](bool ok, const char* table) {
                if (!ok) {
                    unwritten += unwritten.empty() ? table : std::string{", "} + table;
                }
                return ok;
            };
            const std::size_t chunksBefore = world.savedChunkCount();
            world.saveAll();
            {
                // **Designators, not position, and that is load-bearing rather
                // than style.** The binding is by declaration order in
                // `WorldStore.hpp`, a file this one cannot see change. An
                // insertion before `health` shifts a float into an `int32` and
                // the compiler stops it - fine. An insertion anywhere *within*
                // the float run is silent: put a new float between `saturation`
                // and `exhaustion` and the positional form hands the new member
                // the exhaustion value, leaves `exhaustion` at its default and
                // writes both to disk, with no warning at `/W4`, no validation
                // error and no soak failure. The player just loads with their
                // exhaustion quietly reset. C++20 designators may be **skipped
                // but not reordered**, so this stays legal as fields are
                // appended and any future insertion either keeps working or
                // fails loudly *by name*.
                game::SavedPlayer saved{.position = player.position,
                                        .yaw = camera.yaw,
                                        .pitch = camera.pitch,
                                        .health = player.health,
                                        .food = player.food,
                                        .saturation = player.saturation,
                                        .exhaustion = player.exhaustion};
                // **The cursor and the crafting grid are part of the carried
                // inventory as far as the disk is concerned**, because they are
                // written nowhere else: only `closeScreen` hands them back, and
                // neither the autosave timer nor the emergency save closes
                // anything. Hold a stack, wait thirty seconds, lose the GPU, and
                // those items simply did not exist in the reloaded world - from
                // the save added to *stop* losing items.
                //
                // Folded into a **copy** rather than given back for real,
                // because this runs at the end of a frame in which a drag may
                // still be in progress: the sweep goes on writing `heldStack`
                // every frame the button is held - it takes its deposits back
                // onto the cursor and spreads them again - so emptying the real
                // one here would put those items in the inventory *and* leave
                // the sweep free to hand them out again. The copy dies with this
                // block, so it cannot double-add and the player never sees the
                // screen twitch.
                //
                // What will not fit is discarded rather than dropped, and that
                // stays true now that `drops.dat` exists - the reason changed,
                // not the behaviour. Dropping the overflow here would duplicate
                // it: this runs at the end of a frame in which a drag may still
                // be in progress, the sweep goes on writing `heldStack` every
                // frame the button is held, so an autosave that threw the
                // overflow on the floor would leave the player holding it *and*
                // standing over a copy. Reaching this at all needs a completely
                // full inventory *and* a cursor stack, which is why it is not
                // worth a second mechanism to get right.
                game::Inventory carried = inventory;
                forEachScreenStack([&carried](const game::ItemStack& stack) {
                    if (!stack.empty()) {
                        carried.add(stack.item, stack.count, stack.damage);
                    }
                });
                // **And every disc sitting in a jukebox**, for exactly the same
                // reason and into the same copy - and the only one of these
                // whose comment already promised the disc "comes back to you
                // when the world reloads". It did not, because loading a
                // jukebox consumes the disc and nothing restores this list.
                //
                // **The set is LISTED rather than counted, and this line used
                // to say "the third place".** Three members today, in the order
                // this block folds them: `heldStack`, the cursor; `craftSlots`,
                // the open screen's grid - both reached through
                // `forEachScreenStack`, which is why the fold looks like two
                // statements and covers three homes; and `jukeboxDiscs`. The
                // ordinal was correct when it was written and is still correct
                // now, and that is the problem with it: a reviewer reading this
                // after `drops.dat` landed could not recover which three were
                // meant, could not tell whether the drop table leaving the set
                // had shifted the number, and correctly declined to guess. An
                // ordinal cannot be checked against the code; a list can, and
                // the candidate set here is three.
                //
                // **Membership is "no save file can see it", and two things
                // have LEFT the set** - so do not read the list as a census of
                // everything an `ItemStack` can sit in. `drops` left when
                // `drops.dat` acquired its writer, and it was never a member of
                // *this* fold in any case, because the floor is the world's and
                // this copy is the player's - folding it in here would hand the
                // player every item lying in the world at every autosave.
                // `enderChest` left when `SavedPlayer` grew a tail for it, forty
                // lines below. Furnace input and output were never in it, which
                // is the whole reason `forEachScreenStack` skips `craftSlots`
                // for that one screen kind: `saveFurnaces` can already see them,
                // and folding them in would duplicate a smelt every thirty
                // seconds.
                //
                // **Falsified by**: a fourth home appearing in this fold, or
                // any of the three named above acquiring a store call.
                // Re-derived 2026-08-19 against `forEachScreenStack`.
                //
                // **Survival only, matching the consume.** Creative never took
                // the disc out of the bag in the first place, so folding one in
                // here would hand back a copy of an item the player still has.
                if (!creative) {
                    for (const std::pair<glm::ivec3, game::ItemId>& entry : jukeboxDiscs) {
                        carried.add(entry.second, 1, 0);
                    }
                }
                for (std::size_t i = 0; i < game::kInventorySlots; ++i) {
                    saved.inventory[i] = carried.slot(i);
                }
                saved.selectedSlot = static_cast<std::int32_t>(selectedSlot);
                // **The ender chest rides with the player, because it is the
                // player's.** `saveEverything` issued five store calls and not
                // one of them could see it: it is a plain local routed around
                // the `chests` map, keyed by nothing, so it had no file to be in
                // until `SavedPlayer` grew a tail for it.
                saved.enderChest = enderChest;
                // **The bed, which is the player's and not the world's.** It
                // was a plain local for as long as beds have existed: sleep,
                // quit, come back, die, and you woke at world spawn with
                // nothing on screen to say why - which from the outside is
                // indistinguishable from beds simply not working.
                //
                // Only *where* it stands. Whether it is still standing is asked
                // of the live world at the moment of death, so there is nothing
                // here that can go stale while the game is not running.
                saved.respawnBed = hasRespawnPoint ? respawnPoint : glm::ivec3{0, -1, 0};
                // **Every running effect, and the absorption pool that is not
                // one of them.** `SavedPlayer` grew room for both and nothing
                // ever filled it - the format was written, asserted and proved
                // on bytes, and then left one call site short of being
                // reachable, which is `CLAUDE.md` bug shape #15.
                //
                // **A straight copy, deliberately.** `SavedEffect::secondsLeft`
                // and `effects::ActiveEffect::secondsLeft` are both a
                // *remaining duration* in seconds, so nothing here converts a
                // clock. Spelling either as a `time_point` is what would make a
                // world left overnight come back with everything expired.
                static_assert(static_cast<std::size_t>(game::effects::kMaxActive) <=
                                  game::kSavedEffectSlots,
                              "a player can run more effects than the save record has room "
                              "for; widen kSavedEffectSlots and bump the player version");
                std::size_t effectsWritten = 0;
                for (const game::effects::ActiveEffect& active : player.effects.all()) {
                    // An empty slot and an expired one are the same thing on
                    // disk; `Effects::tick` clears them, so this is belt as well
                    // as braces against saving mid-frame.
                    if (active.id == game::effects::Effect::None || active.secondsLeft <= 0.0f) {
                        continue;
                    }
                    saved.effects[effectsWritten] = {static_cast<std::int32_t>(active.id),
                                                     active.amplifier, active.secondsLeft};
                    ++effectsWritten;
                }
                // **The pool, and it is not the grant.** Effect 22 is written
                // above and says what a golden apple *offered*; this is what is
                // left of it after the player has been hit. Writing only the
                // effect hands back a full pool, and writing only the pool
                // hands back hearts that nothing is keeping alive.
                saved.absorption = player.absorption;
                // **And the worn set, written by the loop below since
                // 2026-08-19.** Dated, because this paragraph used to call it
                // "the only one still empty" - true when written, false an hour
                // later, and that exact rot put four finished features within
                // one step of being re-queued as gaps tonight.
                // **Falsified by**: `saved.armour` matching nothing here.
                //
                // The format was written, asserted and proved on hand-built
                // bytes before any of it could be filled, and then left one
                // call site short, so a full diamond set vanished at the next
                // launch. `CLAUDE.md` bug shape #15, and it arrived the same
                // day as the hit test that finally let a set be worn at all.
                //
                // **On "version 6 plus the absorption clock and the worn
                // armour" - the clock is a FIELD, not a written value, and it
                // must stay that way.** `SavedPlayer::absorptionSeconds` goes
                // to disk as 0.0f every time, deliberately: the live value is
                // derived on load from `effects.secondsLeft(Absorption)` - grep
                // that call rather than trusting a line number, because the one
                // this sentence used to carry was already two lines stale - and
                // that is the one table that owns it, so there is nothing to
                // keep in step. A sweep therefore finds that name on neither
                // `saved.*` nor `savedPlayer->*`, which reads exactly like an
                // unwired field - it has now been filed as one twice, off the
                // strength of the sentence this paragraph used to end with.
                // **Do not wire it.** A second answer beside a settled one is
                // bug shape #1, and this particular one would also be an
                // exploit: a stored clock disagreeing with the restored effect
                // reads as a fresh grant and refills the pool on the first tick.
                // `WorldStore.hpp` says the same thing over the field itself.
                //
                // **From `inventory`, not from `carried`.** The two hold the
                // same four stacks - `carried` is a copy - but `carried` exists
                // to fold the cursor and the crafting grid into the *bag*, and
                // reading the worn set off it would suggest those stacks can
                // reach here. They cannot: armour is a separate array with a
                // separate restore.
                //
                // In `ArmourSlot`'s own order, which is the order
                // `Inventory::armourAt` indexes and the order `loadPlayer`
                // sanitises - one order, three readers.
                for (std::size_t i = 0; i < game::kArmourSlots; ++i) {
                    saved.armour[i] = inventory.armourAt(i);
                }
                // **The hour, and it is world state living in the player record
                // on purpose.** `WorldStore.hpp` argues that above the field:
                // there is exactly one such fact today, and a `level.dat`
                // holding a single float would be a second format to version
                // and migrate for no gain. It moves the day the *second* piece
                // of true world state arrives.
                //
                // Copied from the live local because there is nothing to
                // recompute it from - `timeOfDay` is a plain accumulator
                // advanced once per frame by `deltaSeconds / dayLengthSeconds`
                // and is the only copy of the fact. Without this line every
                // launch began at 0.18, mid-morning, however long the world had
                // run.
                saved.timeOfDay = timeOfDay;
                // **The weather - and `thundering()` deliberately, never
                // `storming()`.** `storming()` is the conjunction
                // `raining() && thundering()`, so a false answer does not say
                // which side was false, and thunder running while rain is off
                // is an ordinary reachable state because the two countdowns in
                // `update` never consult each other. Save the conjunction and
                // the thunder flag is lost; its timer then expires and flips it
                // *on*, so the world comes back in the opposite phase to the
                // one it was saved in. `Weather.hpp` states this above
                // `restore`, and I had reached for `storming()` before reading
                // it.
                //
                // The two timers are **seconds remaining** - not elapsed, not
                // ticks. All three spellings compile and validate, which is why
                // `Weather.hpp` says so and why it is worth repeating at the
                // one place that writes them to disk.
                //
                // The five ramps are not saved. They chase these two flags at
                // `kLevelRampPerSecond` and are back at full within about five
                // seconds, so storing one would freeze a presentation detail
                // into the format and give a single fact two owners.
                saved.weatherRaining = weather.raining() ? 1 : 0;
                saved.weatherThundering = weather.thundering() ? 1 : 0;
                saved.weatherRainSeconds = weather.rainSeconds();
                saved.weatherThunderSeconds = weather.thunderSeconds();
                wrote(world.store().savePlayer(saved), "player");
            }

            // **A block entity whose block is gone is not saved.** `chests.dat`
            // and `furnaces.dat` are keyed by position and carry their own
            // format version, so they outlive a `kChunkFormatVersion` bump that
            // regenerates every chunk under them - and a chunk file is only
            // written when the player modified it, so rejecting one discards the
            // container *block* while its contents stay in the map. That is dead
            // weight in the file forever and, until the placement path started
            // clearing the cell, free gear for whoever built there next.
            //
            // **Only asked of a chunk that is actually loaded**, because an
            // unloaded one reads as Air and would delete every container in the
            // world the moment you walked away from it.
            //
            // **The "what counts" half is the caller's, passed in.** It used to
            // be a `bool furnace` with the two answers written out here, and a
            // third kind of block entity is exactly what a two-valued flag
            // cannot grow to hold - the next reader adds `bool campfire` beside
            // it and the day after that the pair means four things, two of which
            // are nonsense. A predicate has no such ceiling, and each caller now
            // names the same family predicate the break path names.
            const auto stillHasItsBlock = [&](const glm::ivec3& at, auto&& isKind) {
                // `columnResident` rather than a chunk lookup, because it is the
                // public question and it already carries the warning this needs:
                // *an absent chunk reads as air*.
                if (!world.columnResident(at.x, at.z)) {
                    return true;
                }
                const game::BlockId here = world.blockAt(at.x, at.y, at.z);
                return isKind(here);
            };

            // Empty ones are dropped rather than written: a furnace nobody has
            // used is indistinguishable from one that has never been opened.
            std::vector<game::PlacedFurnace> savedFurnaces;
            savedFurnaces.reserve(furnaces.size());
            for (const auto& [position, furnace] : furnaces) {
                if (!furnace.idle() &&
                    stillHasItsBlock(position,
                                     [](game::BlockId here) { return game::isFurnace(here); })) {
                    savedFurnaces.push_back(game::PlacedFurnace{position, furnace});
                }
            }
            wrote(world.store().saveFurnaces(savedFurnaces), "furnaces");

            std::vector<game::PlacedChest> savedChests;
            savedChests.reserve(chests.size() + rolledLoot.size());
            for (const auto& [position, chest] : chests) {
                // The break path's own three, so this cannot drift from what
                // counts as a container there.
                //
                // **An empty HOPPER is written where an empty chest is not, and
                // the difference is not cosmetic.** The transfer pass finds
                // hoppers by walking this map, so a hopper with no record in it
                // does not tick - and dropping the empty ones here meant every
                // working hopper in the world went inert on the next load until
                // the player opened it by hand, which is the same defect the
                // placement path had. The chest reasoning does not carry across:
                // "a chest nobody has used is indistinguishable from one that
                // was never opened" is true of a chest, whose record is only
                // storage, and false of a hopper, whose record is also its
                // existence.
                //
                // **Residency is asked before the block**, so an entry in a
                // column that is not loaded behaves exactly as it did before -
                // `stillHasItsBlock` answers true for those without consulting
                // the predicate, and widening `keep` for them would start
                // writing records for chests the old rule dropped.
                const bool hopperCell =
                    world.columnResident(position.x, position.z) &&
                    game::isHopper(world.blockAt(position.x, position.y, position.z));
                const bool keep =
                    (!chest.empty() || hopperCell) &&
                    stillHasItsBlock(position, [](game::BlockId here) {
                        return game::isChest(here) || game::isHopper(here) ||
                               here == game::BlockId::Lectern;
                    });
                if (keep) {
                    savedChests.push_back(game::PlacedChest{position, chest});
                } else if (rolledLoot.count(position) != 0) {
                    // **Dropped for its contents, kept for its flag.** One
                    // predicate, asked once, and the two answers are different
                    // questions: whether there is anything worth carrying, and
                    // whether this cell has already been paid for.
                    savedChests.push_back(game::PlacedChest{position, game::Chest{}});
                }
            }
            // And the rolled cells with no entry left at all, which is what
            // breaking a loot chest leaves behind - it spills the contents and
            // erases the record, so the only thing left to save is the fact
            // that it happened.
            //
            // **Deliberately not asked `stillHasItsBlock`**, and that is the
            // point rather than an oversight: a cell whose block is gone is the
            // case this exists for, because a `kChunkFormatVersion` bump
            // regenerates the chunk and puts the `LootChest` marker back over
            // it. An empty record is not the "free gear for whoever builds there
            // next" that guard was written against, and the placement path
            // clears a cell's records before it builds anyway.
            //
            // Measured: with the `materialise` guard alone and no record kept,
            // a chest the player had fully emptied - the ordinary case - still
            // handed back a full 11 items after one bump, and a broken one did
            // too.
            //
            // **This knowingly departs from `Chest.hpp`, which says an empty
            // chest "can be forgotten rather than written to disk".** That
            // sentence rests on the roll being recorded in the block id, and a
            // format bump regenerates the block - so for a *rolled* cell the two
            // rules disagree and this one wins. The observable behaviour is the
            // same either way, "this chest gives you nothing"; only the reason
            // differs. Filed against `Chest.hpp` rather than edited, because the
            // contract is not this file's to rewrite.
            //
            // **And the marker is proved to survive the disk, not assumed to.**
            // Driving the real `WorldStore` on real bytes: two records in, two
            // out, the empty one keeping its position and seeding `rolledLoot`
            // on the next launch, so the chest pays 0. Three controls, all
            // firing - the same cell pays 10 without the marker, so it is
            // load-bearing; a table containing *only* markers reads back as one
            // record rather than being mistaken for the `saveChests({})` that
            // deletes the file; and a genuinely empty table still clears it, so
            // the older "stale chests come back" exploit stays closed.
            for (const glm::ivec3& cell : rolledLoot) {
                if (chests.find(cell) == chests.end()) {
                    savedChests.push_back(game::PlacedChest{cell, game::Chest{}});
                }
            }
            wrote(world.store().saveChests(savedChests), "chests");

            // A campfire with nothing on it is not written either - it is
            // indistinguishable from one nobody has used, and the block itself
            // is already in the chunk.
            std::vector<game::PlacedCampfire> savedCampfires;
            savedCampfires.reserve(campfires.size());
            for (const auto& [position, campfire] : campfires) {
                if (!campfire.idle() &&
                    stillHasItsBlock(position,
                                     [](game::BlockId here) { return game::isCampfire(here); })) {
                    savedCampfires.push_back(game::PlacedCampfire{position, campfire});
                }
            }
            wrote(world.store().saveCampfires(savedCampfires), "campfires");

            std::vector<game::StowedBox> savedStowboxes;
            savedStowboxes.reserve(stowed.size());
            for (const auto& [handle, contents] : stowed) {
                if (!contents.empty()) {
                    savedStowboxes.push_back(game::StowedBox{handle, contents});
                }
            }
            wrote(world.store().saveStowboxes(savedStowboxes), "stowboxes");

            // And it must not write one back either. The showcase population is
            // three copies of whatever was being looked at; saving that would
            // replace the world's animals with it.
            //
            // **`<= 0`, matching the other three sites**: `== 0` here meant a
            // negative `creature_showcase` skipped the save entirely, so every
            // creature in the world was deleted on quit by a setting that
            // showed nothing and logged nothing.
            //
            // **Unreachable since the whole-body `showcasing` refusal at the top
            // of this lambda, and kept for the same reason its drop-side
            // counterpart is kept - do not delete it.** `showcasing` is
            // `dropShowcase || creatureShowcase > 0`, so reaching this line
            // already proves `creatureShowcase <= 0`. It stays because it is the
            // visible twin of the restore gate beside `loadCreatures`, and the
            // symmetry between the load and save ends is only checkable while
            // both are written down. Stated here as well as at the drop gate
            // because a reason that lives at one of two identical sites is the
            // rule that did not travel.
            std::vector<game::SavedCreature> savedCreatures;
            if (settings.creatureShowcase <= 0) {
                savedCreatures.reserve(creatures.all().size());
                for (const game::Creature& creature : creatures.all()) {
                    // Something caught part-way through falling over is already
                    // gone; the body is only still there so the fall can finish.
                    // Writing it out would reload a corpse that dies again on
                    // sight.
                    if (creature.health <= 0) {
                        continue;
                    }
                    // Written field-by-field rather than as a positional
                    // aggregate: the record carries a named `reserved` byte and
                    // three claim cells, and a positional list silently puts the
                    // wrong value in the wrong slot the next time one is added.
                    game::SavedCreature record{};
                    record.kind = static_cast<std::int32_t>(creature.kind);
                    record.x = creature.position.x;
                    record.y = creature.position.y;
                    record.z = creature.position.z;
                    record.yaw = creature.yaw;
                    record.health = creature.health;
                    record.scale = creature.scale;
                    record.charged = static_cast<std::uint8_t>(creature.charged ? 1 : 0);
                    record.playerBuilt = static_cast<std::uint8_t>(creature.playerBuilt ? 1 : 0);
                    record.profession = creature.profession;
                    record.bedCell = creature.bedCell;
                    record.jobCell = creature.jobCell;
                    record.meetCell = creature.meetCell;
                    savedCreatures.push_back(record);
                }
                // **The second argument, which the deprecated overload existed
                // to point at.** Without it a village breeds a fresh population
                // on every load: the marker set is not derivable from the
                // creature list, so a one-argument save wrote a valid file whose
                // marker table was empty and the one-off pass ran again.
                //
                // Converted a pair at a time rather than handed over whole,
                // because the two types are deliberately different: `Creatures`
                // speaks `glm::ivec2` and `WorldStore` writes a
                // `PopulatedColumn` of two `std::int32_t`, and neither knows how
                // the other packs a column key. That is the point - `populatedKey`
                // is private to `Creature.cpp` and stays the only owner of the
                // packing, so a change to it cannot silently orphan every marker
                // already on disk.
                std::vector<game::PopulatedColumn> savedColumns;
                {
                    const std::vector<glm::ivec2> columns = creatures.populatedColumns();
                    savedColumns.reserve(columns.size());
                    for (const glm::ivec2& column : columns) {
                        savedColumns.push_back(game::PopulatedColumn{column.x, column.y});
                    }
                }
                wrote(world.store().saveCreatures(savedCreatures, savedColumns), "creatures");
            }

            // **Everything lying on the floor, which until 2026-08-19 was
            // deleted on every quit.** The storage half - `saveDrops`,
            // `loadDrops`, `SavedItem` and its `sizeof == 48` assert - had been
            // written, asserted and proved on hand-built bytes with no caller
            // anywhere in the tree, so a death drop, a creeper-spilled chest or
            // a mining trip's worth of stacks simply did not exist at the next
            // launch. This and the restore beside `loadCreatures` are the two
            // lines that were missing.
            //
            // **`persisted()` composes the stack, and that is deliberate.**
            // `ItemEntities::Drop` keeps `item`, `count` and `damage` loose;
            // handing the three parts across this boundary and assembling them
            // here is how a record ends up with the damage in the count, which
            // compiles, writes, reads back and returns a stack of 47 diamond
            // pickaxes. `ItemEntity.hpp` argues it over the struct.
            //
            // **The `bool` becomes an `std::int32_t` here and only here.**
            // `SavedItem::onGround` is spelled wide because a one-byte member in
            // a record opens three bytes of compiler-owned padding that reach
            // the disk uninitialised; the live flag stays a `bool` because that
            // is what it is in memory. That is the whole conversion, and it is
            // the caller's by the same reasoning `SavedCreature::charged` is.
            //
            // **Field by field, not a positional aggregate**, matching
            // `SavedCreature` twenty lines above and `SavedPlayer` above that:
            // six members of which four are floats or vectors, so the day a
            // seventh is appended a braced list silently puts the age in the
            // pickup delay and nothing warns. `ItemEntity.hpp` carries a
            // `static_assert` on `Persisted`'s last offset that stops the build
            // if that struct changes, which is what routes the next editor here.
            //
            // **Skipped entirely while EITHER review mode is on**, exactly as
            // the creature save is skipped for the creature showcase and for the
            // same reason. The drop showcase drops one of every awkward-shaped
            // block on the floor to be looked at, and writing those out would
            // hand the world thirty-odd free items - including a full anvil and
            // an enchanting table - every time anyone reviewed a model. The
            // creature showcase spawns a killable roster in front of you, so
            // without the same clause every showcased animal killed for a look
            // at its death animation banked its loot into the real world's
            // `drops.dat`. Skipping the write leaves whatever the real session
            // saved untouched on disk, which is the right answer for both.
            //
            // **One owner: `dropsPersist`, declared beside the restore**, so the
            // two ends cannot answer differently. A save gated on one mode and a
            // load gated on two is how a review session quietly eats the floor.
            //
            // **Unreachable since the whole-body `showcasing` refusal at the top
            // of this lambda, and kept anyway - do not delete it.** It is not
            // clutter: it is the *load* side's twin, and the symmetry the
            // paragraph above claims is only visible while both ends are
            // written down. Deleting it would leave `dropsPersist` with one
            // reader and make the next person to touch the outer gate believe
            // the floor was never gated at all.
            std::vector<game::SavedItem> savedDrops;
            if (dropsPersist) {
                const std::vector<game::ItemEntities::Persisted> floor = drops.persisted();
                savedDrops.reserve(floor.size());
                for (const game::ItemEntities::Persisted& record : floor) {
                    game::SavedItem saved;
                    saved.position = record.position;
                    saved.velocity = record.velocity;
                    saved.stack = record.stack;
                    saved.age = record.age;
                    saved.pickupDelay = record.pickupDelay;
                    saved.onGround = record.onGround ? 1 : 0;
                    savedDrops.push_back(saved);
                }
                wrote(world.store().saveDrops(savedDrops), "drops");
            }

            // The chunk count is a running total, so the difference is what this
            // save actually wrote - and an autosave line reading zero chunks is
            // how you tell "nothing changed" from "the timer stopped firing".
            engine::logInfo("Saved " + std::to_string(world.savedChunkCount() - chunksBefore) +
                            " modified chunks, " + std::to_string(savedFurnaces.size()) +
                            " furnaces, " + std::to_string(savedChests.size()) + " chests, " +
                            std::to_string(savedCampfires.size()) + " campfires, " +
                            std::to_string(savedCreatures.size()) + " creatures and " +
                            std::to_string(savedDrops.size()) + " dropped items.");
            // **Said again, above the counts, because the counts are what was
            // *intended*.** Each writer already logged its own failure, and a
            // success line printed underneath it is how a reader talks
            // themselves out of believing the first one.
            if (!unwritten.empty()) {
                engine::logError("Save incomplete - these did not write: " + unwritten +
                                 ". The world stays dirty so the next save retries.");
            }
            return unwritten.empty();
        };

        /// Set once the ordinary shutdown save has run, so the guard below knows
        /// there is nothing left to do.
        bool worldSaved = false;

        /// **The emergency save, and the only place it can safely live.**
        ///
        /// The obvious home is the `catch` at the bottom of `main`, and that
        /// would be a use-after-free: unwinding destroys this block's locals -
        /// `world` included - *before* the handler outside it runs, so a save
        /// written there would be calling `saveAll` on a dead object. A
        /// destructor declared here runs first, because destruction is the
        /// reverse of construction and everything `saveEverything` touches was
        /// constructed earlier. The alternative was wrapping six thousand lines
        /// of frame loop in a second `try`.
        struct SaveOnUnwind {
            const std::function<bool()>& save;
            /// **Run before `save`, because it writes blocks and `save` writes
            /// chunks.** Held here rather than called from the frame loop for
            /// the same reason the save is: this destructor is the only thing
            /// that runs on the crash path, and a cube in flight when the GPU
            /// is lost is deleted from the world exactly as it is at shutdown.
            const std::function<void()>& settleFallers;
            const bool& done;

            ~SaveOnUnwind() {
                if (done) {
                    return;
                }
                engine::logError("Unwinding without a save. Trying to write the world out anyway.");
                // A throw out of a destructor during unwinding calls
                // `std::terminate` outright, and a second failure here must not
                // replace the first as the reason the game died. So it swallows
                // and says so.
                try {
                    settleFallers();
                    // **"Finished" is not "worked".** This is the last thing that
                    // will ever run for this world, so the one line the player
                    // has to go on had better not claim more than it did.
                    if (save()) {
                        engine::logError("Emergency save finished.");
                    } else {
                        engine::logError("Emergency save ran but did not write everything.");
                    }
                } catch (const std::exception& error) {
                    engine::logError(std::string("Emergency save failed: ") + error.what());
                } catch (...) {
                    engine::logError("Emergency save failed for an unknown reason.");
                }
            }
        } saveOnUnwind{saveEverything, settleFallersBeforeFinalSave, worldSaved};

        /// Copies the open furnace into the shared slot storage and back.
        ///
        /// The furnace stays authoritative because it keeps smelting while the
        /// screen is up; the slots are refreshed from it every frame and written
        /// back the moment the player changes one.
        const auto furnaceToSlots = [&] {
            const auto found = furnaces.find(openFurnacePosition);
            if (found == furnaces.end()) {
                return;
            }
            craftSlots[0] = found->second.input;
            craftSlots[1] = found->second.fuel;
            furnaceOutput = found->second.output;
        };

        const auto slotsToFurnace = [&] {
            const auto found = furnaces.find(openFurnacePosition);
            if (found == furnaces.end()) {
                return;
            }
            found->second.input = craftSlots[0];
            found->second.fuel = craftSlots[1];
            found->second.output = furnaceOutput;
        };

        bool overlayVisible = false;
        std::vector<float> frameHistory;
        frameHistory.reserve(kFrameHistoryLength);
        auto lastHudRebuild = previousTime;

        // One frame of gamepad input, rewritten at the top of every frame.
        game::Gamepad pad;
        game::Rumble rumble;
        auto inputMode =
            static_cast<game::InputMode>(std::min(settings.inputMode, game::Settings::kInputModeCount - 1));
        game::InputDevice inputDevice = inputMode == game::InputMode::Gamepad ? game::InputDevice::Gamepad
                                                                             : game::InputDevice::KeyboardMouse;

        // Where the interface's pointer is, in screen space.
        //
        // **One variable for both devices.** The mouse writes its position here
        // and the left stick nudges it, so every hit test downstream takes a
        // point and none of them has to know which device produced it - which
        // is why a pad can drive the whole inventory without a second copy of
        // the screen's interaction written against a focus ring.
        float pointerX = 0.0f;
        float pointerY = 0.0f;
        bool pointerScreenWasOpen = false;
        /// Set when a click is spent taking the cursor back, and held until that
        /// button comes up again, so re-entering the window cannot also swing.
        bool swallowClickUntilRelease = false;
        double lastMouseX = 0.0;
        double lastMouseY = 0.0;
        bool cursorWasCaptured = false;
        /// Frames left to ignore mouse movement for. See where it is set.
        int mouseSettleFrames = 0;

        // Crosshair, hotbar and the diagnostics panel share one screen mesh.
        const auto rebuildHud = [&](const game::OverlayStats& stats) {
            // A panel carries its own hotbar row, and a crosshair over a
            // pointer-driven screen is just clutter.
            engine::MeshData hud = openScreen.has_value() ? engine::MeshData{} : game::makeCrosshair();
            // Catalogue entries, which are the one part of the HUD that has to
            // stop at an edge rather than simply being drawn or not.
            engine::MeshData clipped;
            // And what the cursor carries, which has to be drawn after them.
            engine::MeshData topLayer;

            const auto append = [&](const engine::MeshData& part) {
                const auto base = static_cast<std::uint32_t>(hud.vertices.size());
                hud.vertices.insert(hud.vertices.end(), part.vertices.begin(), part.vertices.end());
                for (const std::uint32_t index : part.indices) {
                    hud.indices.push_back(base + index);
                }
            };

            if (openScreen.has_value()) {
                // A furnace shows what it has actually made; every other screen
                // shows what its grid *would* make.
                game::ItemStack shownResult;
                std::array<game::ItemStack, game::kStonecutterOptions> shownCuts{};
                game::inventoryScreen::FurnaceProgress progress;
                if (*openScreen == game::inventoryScreen::Kind::Furnace) {
                    const auto found = furnaces.find(openFurnacePosition);
                    if (found != furnaces.end()) {
                        shownResult = found->second.output;
                        progress.burn = found->second.burnFraction();
                        progress.cook = found->second.cookFraction();
                    }
                } else if (*openScreen == game::inventoryScreen::Kind::SmithingTable) {
                    shownResult =
                        openBench == game::BlockId::SmithingTable
                            ? game::smithingResult(craftSlots[0], craftSlots[1])
                        : openBench == game::BlockId::BrewingStand
                            ? game::brewingResult(craftSlots[0], craftSlots[1])
                            : game::repairResult(craftSlots[0], craftSlots[1]);
                } else if (*openScreen == game::inventoryScreen::Kind::Stonecutter) {
                    for (int option = 0; option < game::kStonecutterOptions; ++option) {
                        shownCuts[static_cast<std::size_t>(option)] = stonecutterCut(option);
                    }
                } else {
                    shownResult = game::craftResult(craftSlots.data(),
                                                    game::inventoryScreen::craftSize(*openScreen));
                }

                // **An ender chest draws its own twenty-seven.** `stackAt` and
                // `quickMoveTargets` both resolve it that way, but the draw
                // path went to the block map instead - where opening one has
                // `try_emplace`d an empty chest at that very position - so the
                // screen showed twenty-seven empty slots while every click,
                // shift-click and drag moved real items behind it. One
                // question, one answer.
                const bool drawingEnderChest =
                    *openScreen == game::inventoryScreen::Kind::Chest &&
                    game::isEnderChest(world.blockAt(openChestPosition.x, openChestPosition.y,
                                                     openChestPosition.z));
                const game::Chest* const shownChest =
                    drawingEnderChest                      ? &enderChest
                    : chests.count(openChestPosition) != 0 ? &chests.at(openChestPosition)
                                                           : nullptr;
                const game::Chest* const shownPartner =
                    drawingEnderChest                     ? nullptr
                    : chests.count(openChestPartner) != 0 ? &chests.at(openChestPartner)
                                                          : nullptr;

                append(game::inventoryScreen::build(
                    *openScreen, inventory, craftSlots.data(),
                    *openScreen == game::inventoryScreen::Kind::Stonecutter ? shownCuts.data()
                                                                           : &shownResult,
                    heldStack, pointerX, pointerY, renderer.aspectRatio(),
                    catalogue, progress, creative, shownChest, shownPartner, clipped,
                    topLayer));

                // The pad has no pointer of its own, so it gets one drawn.
                // Nothing is shown for the mouse: the OS already draws that,
                // and two arrows in the same place is the bug this avoids.
                if (inputDevice == game::InputDevice::Gamepad) {
                    // In front of everything, held stack included, because it
                    // is the thing you are aiming with.
                    constexpr float kPointerHeight = 0.075f;
                    constexpr float kPointerDepth = 0.0004f;
                    game::hud::appendPointer(topLayer, pointerX, pointerY, kPointerHeight, kPointerDepth);
                }
            } else {
                append(game::makeHotbar(inventory, selectedSlot, bowHeld ? bowDraw : -1.0f));

                // The survival bars sit above it. **Creative shows nothing**,
                // the same way the reference hides them there: nothing can hurt
                // you and nothing can starve you, so a full row of hearts is a
                // permanent lie taking up screen.
                if (!creative) {
                    game::hud::StatusValues status;
                    status.health = player.health;
                    status.food = player.food;
                    status.airFraction = player.air / game::fluid::kAirSeconds;
                    // The pool, not what a grant is worth. `Player::absorption`
                    // is the balance that `damagePlayer` spends;
                    // `effects::absorptionPoints` would report full hearts for
                    // the whole two minutes after they had been eaten through.
                    status.absorption = player.absorption;
                    status.hurtFlash = player.hurtFlash;
                    append(game::hud::makeStatusBars(status));
                }
            }
            if (overlayVisible) {
                // The top layer, not the main one. Its own depths put its
                // backdrops behind the world dim quad, so with a screen open the
                // panel was covered and only the text survived; drawn last it
                // sits over everything, which is what a diagnostic wants.
                const auto base = static_cast<std::uint32_t>(topLayer.vertices.size());
                const engine::MeshData overlay =
                    game::makeDebugOverlay(stats, frameHistory, renderer.aspectRatio());
                topLayer.vertices.insert(topLayer.vertices.end(), overlay.vertices.begin(),
                                         overlay.vertices.end());
                for (const std::uint32_t index : overlay.indices) {
                    topLayer.indices.push_back(base + index);
                }
            }

            renderer.setScreenMesh(hud);
            const auto [clipMin, clipMax] = game::inventoryScreen::catalogueListBounds();
            renderer.setClippedScreenMesh(clipped, clipMin, clipMax);
            renderer.setTopScreenMesh(topLayer);
        };

        // A lily pad floats, so the water under it is what holds it up where
        // every other plant needs something solid - and a wall torch, a lever, a
        // button, a sign, a ladder or a cocoa pod is held up **sideways**, which
        // this asked nobody about. `needsSupportBelow` is `Cross || Flat ||
        // isTorchBlock`, so it says yes to all eight wall torch ids and the only
        // question here was the floor: a redstone torch could not be hung on a
        // wall over open air, and one that was standing came down when the block
        // *beneath* it was mined. `wallBehind` is the one owner of which way a
        // thing leans; this is the reader that turns it into a cell.
        /// **Track shape is derived, never chosen.** A rail works out its own
        /// shape from what is next to it and works it out again every time a
        /// neighbour appears or goes. Placement here used to emit two of the ten
        /// shapes and nothing ever revised them, so the plain rail's four slopes
        /// and four curves and the four slopes of each of the other three
        /// families - **twenty block ids** - decoded, drew and could never be
        /// built. That is this project's other standing bug shape: a complete
        /// feature one call site short of being reachable at all.
        ///
        /// This is **not** the redstone decision and needs no signal engine. A
        /// rail's shape is a function of which cells hold rails, and of nothing
        /// else.
        ///
        /// The reference's adjacency, quoted rather than remembered: a rail
        /// joins one in that direction at the same level, and "existing rail
        /// lines one block up and down are considered for adjacency in the same
        /// manner" (https://minecraft.wiki/w/Rail).
        const auto railJoins = [&world](const glm::ivec3& cell, game::FaceDirection dir) {
            const glm::ivec3 step = stepAlong(dir);
            const glm::ivec3 side = cell + step;
            return game::isRail(world.blockAt(side.x, side.y, side.z)) ||
                   game::isRail(world.blockAt(side.x, side.y + 1, side.z)) ||
                   game::isRail(world.blockAt(side.x, side.y - 1, side.z));
        };

        /// Whether the rail that way stands one cell **higher**, which is the
        /// only thing that makes a ramp. **The ramp is always the lower of the
        /// two** - it ascends *toward* the higher neighbour - so a rail with a
        /// neighbour one cell down stays flat and that neighbour becomes the
        /// ramp when its own turn comes.
        const auto railClimbs = [&world](const glm::ivec3& cell, game::FaceDirection dir) {
            const glm::ivec3 step = stepAlong(dir);
            return game::isRail(world.blockAt(cell.x + step.x, cell.y + 1, cell.z + step.z));
        };

        /// One rail, re-derived in place. Silent for every cell that is not a
        /// rail, which is what lets the caller sweep a neighbourhood blind.
        const auto solveRailShape = [&](const glm::ivec3& cell) {
            const game::BlockId self = world.blockAt(cell.x, cell.y, cell.z);
            if (!game::isRail(self)) {
                return;
            }
            const int family = game::railFamily(self);
            // **Only the plain rail bends**, which is why `railShape` is ten
            // wide for family 0 and six for the other three. Emitting a curve
            // for a powered rail would fold to a flat id inside `railAt` and
            // look like the solver had simply got it wrong.
            const bool mayCurve = family == 0;
            const bool east = railJoins(cell, game::FaceDirection::PosX);
            const bool west = railJoins(cell, game::FaceDirection::NegX);
            const bool south = railJoins(cell, game::FaceDirection::PosZ);
            const bool north = railJoins(cell, game::FaceDirection::NegZ);

            // Shapes 0 and 1 are the two flat runs, 2-5 the slopes and 6-9 the
            // curves. `railSlopeToward` owns the slope numbering and is asked
            // for it here rather than being written out backwards - shape
            // `2 + int(direction)` climbs toward that direction. **Block.hpp
            // does not name the four curves**, so this is where the convention
            // is stated and it is the reference's own: 6 joins south and east,
            // 7 south and west, 8 north and west, 9 north and east. Nothing else
            // in the tree reads a curve apart from the corner sprite, which does
            // not care which corner it is.
            constexpr int kFlatNorthSouth = 0;
            constexpr int kFlatEastWest = 1;
            constexpr int kCurveSouthEast = 6;
            constexpr int kCurveSouthWest = 7;
            constexpr int kCurveNorthWest = 8;
            constexpr int kCurveNorthEast = 9;
            int shape = -1;
            if ((north || south) && !east && !west) {
                shape = kFlatNorthSouth;
            }
            if ((east || west) && !north && !south) {
                shape = kFlatEastWest;
            }
            if (mayCurve) {
                if (south && east && !north && !west) {
                    shape = kCurveSouthEast;
                }
                if (south && west && !north && !east) {
                    shape = kCurveSouthWest;
                }
                if (north && west && !south && !east) {
                    shape = kCurveNorthWest;
                }
                if (north && east && !south && !west) {
                    shape = kCurveNorthEast;
                }
            }
            // Three or four ways to go. **The order these are written in is the
            // rule**, because each one overwrites the last: the reference's
            // T-junction resolves to a curve and its four-way junction "always
            // curves south-to-east" (https://minecraft.wiki/w/Rail), which is
            // exactly what falls out of testing south-east last.
            if (shape < 0) {
                if (north || south) {
                    shape = kFlatNorthSouth;
                }
                if (east || west) {
                    shape = kFlatEastWest;
                }
                if (mayCurve) {
                    if (west && north) {
                        shape = kCurveNorthWest;
                    }
                    if (east && north) {
                        shape = kCurveNorthEast;
                    }
                    if (west && south) {
                        shape = kCurveSouthWest;
                    }
                    if (south && east) {
                        shape = kCurveSouthEast;
                    }
                }
            }
            // And the same trick again for the ramp: the reference "prefers, in
            // order: west, east, south, north", so west is tested after east and
            // south after north, and the later test wins.
            if (shape == kFlatNorthSouth) {
                if (railClimbs(cell, game::FaceDirection::NegZ)) {
                    shape = 2 + static_cast<int>(game::FaceDirection::NegZ);
                }
                if (railClimbs(cell, game::FaceDirection::PosZ)) {
                    shape = 2 + static_cast<int>(game::FaceDirection::PosZ);
                }
            }
            if (shape == kFlatEastWest) {
                if (railClimbs(cell, game::FaceDirection::PosX)) {
                    shape = 2 + static_cast<int>(game::FaceDirection::PosX);
                }
                if (railClimbs(cell, game::FaceDirection::NegX)) {
                    shape = 2 + static_cast<int>(game::FaceDirection::NegX);
                }
            }
            // Nothing adjacent at all: keep what it already had, which on a
            // fresh placement is the reference's own default. **Bedrock lays an
            // isolated rail north-south; Java lays it the way the player is
            // facing** - a real edition difference, and Bedrock is this
            // project's reference, so the placement branch seeds shape 0.
            if (shape < 0) {
                shape = game::railShape(self);
            }
            const game::BlockId want = game::railAt(family, shape, game::railPowered(self));
            if (want != self) {
                world.setBlock(cell.x, cell.y, cell.z, want);
            }
        };

        /// Every rail that could have been reading the cell that just changed.
        ///
        /// **One pass reaches the fixpoint, and that is a property rather than a
        /// hope**: adjacency asks only *whether* a cell holds a rail, never what
        /// shape it is, so re-deriving one rail can never change another rail's
        /// answer. Only the twelve cells that can see this one - four sides at
        /// three heights - plus the cell itself have anything to re-derive, and
        /// each of the thirteen is a silent no-op unless it holds a rail.
        const auto refreshRailsAround = [&](const glm::ivec3& cell) {
            solveRailShape(cell);
            for (int side = 0; side < 4; ++side) {
                const glm::ivec3 step = stepAlong(static_cast<game::FaceDirection>(side));
                for (int rise = -1; rise <= 1; ++rise) {
                    solveRailShape(glm::ivec3{cell.x + step.x, cell.y + rise, cell.z + step.z});
                }
            }
        };

        const auto hasItsSupport = [&world](game::BlockId block, int x, int y, int z) {
            const game::FaceDirection wall = wallBehind(block);
            if (wall != game::FaceDirection::Unknown) {
                const glm::ivec3 step = stepAlong(wall);
                return world.isSolid(x + step.x, y + step.y, z + step.z);
            }
            return game::restsOnWater(block) ? game::isWaterSource(world.blockAt(x, y - 1, z))
                                             : world.isSolid(x, y - 1, z);
        };

        /// **What a cell owes its neighbours once it has become Air.** Four
        /// rules, one owner: whatever was resting on top of it comes down, any
        /// ladder that was hung on its side comes down, and the other half of a
        /// door, a bed or a tall flower goes with it.
        ///
        /// All four existed and were correct - **in the break path only**. A
        /// blast left doors as floating upper halves, beds as half beds, torches
        /// and ladders hanging in mid-air, and none of the four paid their drop.
        /// That is this project's most expensive bug shape (a rule that did not
        /// travel), so it now lives in one place both callers reach rather than
        /// in a copy that has to be remembered twice.
        ///
        /// `removed` is what *used* to be at `cell`; the caller has already
        /// written Air there, because the door and bed halves are found from the
        /// broken block's own facing and it must still be readable.
        const auto settleAround = [&](const glm::ivec3& cell, game::BlockId removed) {
            // A door, a bed and a tall flower are each two cells and one thing.
            // `clearPairedHalf` owns which cell the other half is in, because a
            // replacement has to ask the identical question.
            clearPairedHalf(cell, removed);

            // Whatever was resting on it comes down too, rather than being left
            // hanging in the air.
            const glm::ivec3 above{cell.x, cell.y + 1, cell.z};
            const game::BlockId resting = world.blockAt(above.x, above.y, above.z);
            if (game::needsSupportBelow(resting) &&
                !hasItsSupport(resting, above.x, above.y, above.z)) {
                world.setBlock(above.x, above.y, above.z, game::BlockId::Air);
                // **Through the table, so the count comes with it.** This
                // spawned exactly one of whatever fell, so a stack of four
                // candles losing its floor paid a single candle and the other
                // three were gone. Bare hands, because nothing was swung at it.
                spillBlockDrop(above, resting, game::BreakContext{});
                // **And whatever fell may itself be half of something.** The
                // rule three lines above did not travel the extra cell: mining
                // the *dirt* under a sunflower dropped the lower half and left
                // the upper half standing, `World::setBlock` scheduled a support
                // update on the cell just vacated, and the cascade found that
                // orphan unsupported and **paid it a second time** - one plant,
                // two flowers, from the very function whose own comment names
                // this bug shape. It cannot be caught downstream: the two
                // payments are `kFallDelay` apart, so the wash drain's
                // `alreadyPaid` list, which is per-batch, never sees them
                // together. Clearing the twin here is what stops the cascade
                // ever being handed one, so nothing double-pays and the
                // scheduled update finds Air and does nothing.
                clearPairedHalf(above, resting);
            }

            // A ladder is held up sideways rather than from below, so it is the
            // four neighbours that have to be checked instead of the one cell
            // above - **and a ladder was the only one of six families ever
            // asked**. A wall sign, a wall banner, a wall torch, a lever, a
            // button and a cocoa pod all hang the same way and all stayed
            // floating with no drop when their wall was mined. `wallBehind` is
            // the single owner of which cell holds what; the walk below is the
            // ladder rule with the family test taken out of it.
            for (const glm::ivec3& step : {glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0},
                                           glm::ivec3{0, 0, 1}, glm::ivec3{0, 0, -1}}) {
                const glm::ivec3 beside = cell + step;
                const game::BlockId hung = world.blockAt(beside.x, beside.y, beside.z);
                const game::FaceDirection wall = wallBehind(hung);
                // The wall it leans on is `beside + stepAlong(wall)`; the cell
                // that just went is `beside - step`. It comes down when those
                // are the same cell, which is exactly what the hand-written
                // ladder test spelled out one comparison at a time.
                if (wall == game::FaceDirection::Unknown || stepAlong(wall) != -step) {
                    continue;
                }
                world.setBlock(beside.x, beside.y, beside.z, game::BlockId::Air);
                // Same table, same reason as the cell above.
                spillBlockDrop(beside, hung, game::BreakContext{});
            }

            // **And the fifth thing a vacated cell owes its neighbours.** Track
            // shape is read off which cells hold rails, so a rail that goes
            // leaves every rail that could see it holding a stale answer - a
            // corner that no longer turns, a ramp climbing to nothing. Hung here
            // rather than at the three removal sites for the reason the other
            // four rules are: this is already the one owner all three reach.
            refreshRailsAround(cell);
            refreshRailsAround(glm::ivec3{cell.x, cell.y + 1, cell.z});
        };

        /// **Whether a block placed here would seal something living inside
        /// it.** The placement path asked `playerOverlapsBlock` and nothing
        /// else, so a block dropped on a sheep buried it in stone - Bedrock
        /// refuses the placement instead, for the player and for most mobs
        /// alike (https://minecraft.fandom.com/wiki/Solid_block). The rule
        /// existed, was correct, and covered exactly one of the things standing
        /// in the world.
        ///
        /// Scaled by the creature's own `scale`, because a baby's box is a baby's
        /// box; a half-open interval on every axis, so a creature standing
        /// exactly on a boundary belongs to one cell rather than to both.
        const auto cellIsOccupied = [&](const glm::ivec3& cell) {
            if (game::playerOverlapsBlock(player, cell)) {
                return true;
            }
            const glm::vec3 low{cell};
            const glm::vec3 high = low + glm::vec3{1.0f};
            for (const game::Creature& creature : creatures.all()) {
                const game::CreatureSpecies& species = game::speciesInfo(creature.kind);
                const float half = species.halfWidth * creature.scale;
                const float tall = species.height * creature.scale;
                if (creature.position.x + half > low.x && creature.position.x - half < high.x &&
                    creature.position.z + half > low.z && creature.position.z - half < high.z &&
                    creature.position.y + tall > low.y && creature.position.y < high.y) {
                    return true;
                }
            }
            return false;
        };

        // A T of iron blocks with a carved pumpkin on its head becomes an iron
        // golem. Checked from the pumpkin outward, in all three orientations.
        //
        // **The four corner cells must be exactly `Air`, not merely
        // non-occluding.** That reads like the mistake this codebase keeps
        // making - asking `== Air` where `occludesFace` is the general
        // question - but here it is what the reference actually tests: a snow
        // layer, a flower or a block of water in those cells prevents the
        // build, and any looser test would let a golem rise out of a snowfield.
        const auto tryRaiseGolem = [&world, &creatures](const glm::ivec3& head) {
            struct Frame {
                glm::ivec3 down;
                glm::ivec3 across;
            };
            // Upright, then lying along each horizontal axis. All three are
            // accepted, and the golem always faces south whichever was used.
            const std::array<Frame, 3> frames{{{{0, -1, 0}, {1, 0, 0}},
                                               {{0, -1, 0}, {0, 0, 1}},
                                               {{1, 0, 0}, {0, 0, 1}}}};

            for (const Frame& frame : frames) {
                const glm::ivec3 body = head + frame.down;
                const glm::ivec3 foot = body + frame.down;
                const glm::ivec3 left = body - frame.across;
                const glm::ivec3 right = body + frame.across;
                const auto isIron = [&](const glm::ivec3& at) {
                    return world.blockAt(at.x, at.y, at.z) == game::BlockId::IronBlock;
                };
                if (!isIron(body) || !isIron(foot) || !isIron(left) || !isIron(right)) {
                    continue;
                }
                const auto isClear = [&](const glm::ivec3& at) {
                    return world.blockAt(at.x, at.y, at.z) == game::BlockId::Air;
                };
                if (!isClear(head - frame.across) || !isClear(head + frame.across) ||
                    !isClear(foot - frame.across) || !isClear(foot + frame.across)) {
                    continue;
                }

                for (const glm::ivec3& cell : {head, body, foot, left, right}) {
                    world.setBlock(cell.x, cell.y, cell.z, game::BlockId::Air);
                }
                // Stood on the lowest of the five cells, so a golem built lying
                // down does not end up buried.
                const glm::ivec3 feet{std::min({head.x, foot.x, left.x, right.x}),
                                      std::min({head.y, foot.y, left.y, right.y}),
                                      std::min({head.z, foot.z, left.z, right.z})};
                creatures.place(game::CreatureKind::IronGolem,
                                glm::vec3{feet} + glm::vec3{0.5f, 0.0f, 0.5f}, 0.0f, false, 0.0f,
                                true);
                return true;
            }
            return false;
        };

        /// **Roughly every thirty seconds, and never before the loop starts.**
        ///
        /// Seeded from the clock here rather than at the top of `main`, so the
        /// initial chunk load - which takes seconds and calls `setBlock` for
        /// every water flow it settles - cannot make the very first frame try to
        /// save a world that is still arriving.
        constexpr auto kAutosaveInterval = std::chrono::seconds(30);
        auto lastSaveTime = Clock::now();

        /// Refilled by `FallingBlocks::update` every frame and drained on the
        /// next line, so it holds nothing across a frame boundary. Declared out
        /// here rather than inside the loop because allocation stays out of hot
        /// loops - `clear()` keeps whatever capacity a collapse already paid for.
        std::vector<game::FallingBlocks::Landed> landings;

        while (!window.shouldClose()) {
            window.pollEvents();

            const auto now = Clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - previousTime).count();
            previousTime = now;
            secondsSinceSpacePress += deltaSeconds;

            frameHistory.push_back(deltaSeconds * 1000.0f);
            if (frameHistory.size() > kFrameHistoryLength) {
                frameHistory.erase(frameHistory.begin());
            }

            // The pad is sampled once, here, and everything below asks `pad`
            // rather than the device - so every reader sees the same frame and
            // the trigger edges are found in exactly one place.
            game::updateGamepad(pad, window, settings, deltaSeconds);
            {
                // **Read rather than consumed.** The cursor delta belongs to
                // mouse-look further down, and draining it here to notice
                // movement would steal it.
                const double mouseX = window.cursorX();
                const double mouseY = window.cursorY();

                // **Capturing or releasing the cursor teleports the position
                // the OS reports**, and a teleport is indistinguishable from a
                // large mouse movement. Without this the two modes hand the
                // cursor back and forth forever: the pad captures, the jump
                // reads as the mouse being used, that releases, and so on. The
                // change is noticed a frame late because it is made further
                // down, so the count covers this frame and the next.
                if (window.isCursorCaptured() != cursorWasCaptured) {
                    cursorWasCaptured = window.isCursorCaptured();
                    mouseSettleFrames = 2;
                }
                const bool mouseMoved = mouseSettleFrames == 0 && (std::abs(mouseX - lastMouseX) > 0.5 ||
                                                                   std::abs(mouseY - lastMouseY) > 0.5);
                mouseSettleFrames = std::max(0, mouseSettleFrames - 1);
                lastMouseX = mouseX;
                lastMouseY = mouseY;

                const bool keyboardActive =
                    mouseMoved || window.isMouseButtonDown(engine::MouseButton::Left) ||
                    window.isMouseButtonDown(engine::MouseButton::Right) ||
                    window.isKeyDown(engine::Key::W) || window.isKeyDown(engine::Key::A) ||
                    window.isKeyDown(engine::Key::S) || window.isKeyDown(engine::Key::D) ||
                    window.isKeyDown(engine::Key::Space) || window.isKeyDown(engine::Key::LeftShift) ||
                    window.isKeyDown(engine::Key::LeftControl);

                const game::InputDevice wanted =
                    game::resolveInputDevice(inputMode, inputDevice, pad, keyboardActive);
                if (wanted != inputDevice) {
                    inputDevice = wanted;
                    engine::logInfo(inputDevice == game::InputDevice::Gamepad ? "Input: gamepad"
                                                                             : "Input: keyboard and mouse");
                }

                const engine::Extent2D extent = window.framebufferExtent();
                const float halfHeight = static_cast<float>(extent.height) * 0.5f;
                const float aspect = renderer.aspectRatio();
                const bool screenOpen = openScreen.has_value();

                // A screen opens with the pointer in the middle. Carrying the
                // last position over means it starts wherever the stick left it
                // last time, which on a pad is nowhere useful.
                if (screenOpen && !pointerScreenWasOpen) {
                    pointerX = 0.0f;
                    pointerY = 0.0f;
                }
                pointerScreenWasOpen = screenOpen;

                if (inputDevice == game::InputDevice::Gamepad) {
                    if (screenOpen) {
                        pointerX = std::clamp(pointerX + pad.pointerDelta.x, -aspect, aspect);
                        pointerY = std::clamp(pointerY + pad.pointerDelta.y, -1.0f, 1.0f);
                    }
                    // Two cursors on screen with only one of them working is
                    // worse than none, so the OS pointer stays hidden and put
                    // even while a panel is up.
                    if (screenOpen && !window.isCursorCaptured()) {
                        window.setCursorCaptured(true);
                    }
                } else {
                    if (halfHeight > 0.0f) {
                        pointerX =
                            (static_cast<float>(mouseX) - static_cast<float>(extent.width) * 0.5f) / halfHeight;
                        pointerY = (static_cast<float>(mouseY) - halfHeight) / halfHeight;
                    }
                    // Switching back mid-panel has to hand the real pointer
                    // over, or the screen becomes unusable by either device.
                    if (screenOpen && window.isCursorCaptured()) {
                        window.setCursorCaptured(false);
                    }
                }
            }

            // Drained every frame whether or not anything wants it, so a
            // keystroke can never be delivered late. Read before the key loop,
            // so the `E` that opens a screen is discarded with the frame it
            // belonged to rather than arriving as the first typed character.
            //
            // **The search field takes the keyboard only once it has been
            // clicked into.** `E` would otherwise close the screen and `Q`
            // throw the cursor's stack away, and neither can be typed into a
            // box that does not have focus - but a field that grabs the
            // keyboard merely because its tab is selected is worse, because
            // nothing on screen says it has. Escape still closes, which is the
            // one key a text field must not eat.
            const bool typingSearch = openScreen.has_value() &&
                                      game::inventoryScreen::showsCatalogue(*openScreen) &&
                                      catalogue.tab == game::inventoryScreen::CatalogueTab::Search &&
                                      catalogue.searchFocused;
            // **One owner of "the field just changed".** The blink phase is
            // part of it, not decoration: `uiSeconds` is the phase and the
            // caret is drawn for its first half, so an edit landing in the dark
            // half looks like a dropped keystroke - the player presses Left
            // again and jumps two. Zeroing the phase means every edit is seen.
            //
            // **The phase and not `caretVisible`**, which is derived from it a
            // few hundred lines below and must keep exactly one writer; setting
            // both here is the same value owned in two places, and the copy
            // would be the one that rots. The derivation runs after this, so
            // the caret is already lit on the frame the key landed.
            //
            // `contentChanged` is passed rather than assumed because the two
            // cases genuinely differ: new text is a new result set and the list
            // has to go back to the top, but moving the caret changes nothing
            // about what matches, and throwing the scroll away for it would be
            // a jump the player did not ask for.
            const auto searchChanged = [&](bool contentChanged) {
                if (contentChanged) {
                    catalogue.scrollRow = 0;
                }
                uiSeconds = 0.0f;
                hudDirty = true;
            };
            // **Every editing key, in one place, for both queues.** Before the
            // caret existed each loop knew about Backspace and nothing else,
            // which is precisely why holding Backspace worked and holding it
            // was the only thing that did. A key wired into one loop and not
            // the other is this file's most expensive shape, and with six keys
            // instead of one it is no longer a shape you would spot by eye.
            //
            // Returns whether the key was an editing key, so the press loop can
            // still swallow everything else while the field has the keyboard.
            //
            // **`Up`/`Down` are absent deliberately** - they scroll the
            // catalogue, and a caret is not what a player reaches for them
            // expecting. Selection, clipboard and word-wise motion are out of
            // scope by the same owner's decision.
            const auto editSearchKey = [&](const engine::Key key) {
                const std::size_t before = catalogue.query.size();
                switch (key) {
                case engine::Key::Backspace:
                    catalogue.backspaceAtCaret();
                    break;
                case engine::Key::Delete:
                    catalogue.deleteAtCaret();
                    break;
                case engine::Key::Left:
                    catalogue.moveCaret(-1);
                    break;
                case engine::Key::Right:
                    catalogue.moveCaret(1);
                    break;
                case engine::Key::Home:
                    catalogue.caretToStart();
                    break;
                case engine::Key::End:
                    catalogue.caretToEnd();
                    break;
                default:
                    return false;
                }
                searchChanged(catalogue.query.size() != before);
                return true;
            };
            {
                const std::string typed = window.consumeTypedText();
                if (typingSearch && !typed.empty()) {
                    // Capped at what the field can show; a longer query would
                    // scroll out of the box with no way to see it. The cap is
                    // enforced here rather than in `insertAtCaret`, which is
                    // deliberately a plain editing primitive - so a second text
                    // field would have to state its own width, which is right.
                    constexpr std::size_t kMaxQuery = 22;
                    bool inserted = false;
                    for (const char c : typed) {
                        if (catalogue.query.size() < kMaxQuery) {
                            catalogue.insertAtCaret(c);
                            inserted = true;
                        }
                    }
                    if (inserted) {
                        searchChanged(true);
                    }
                }
            }

            // **Repeats, drained every frame whether or not a field is open.**
            // `Window` keeps them in a queue of their own precisely so nothing
            // else sees them - holding `E` must not strobe the inventory - and
            // that queue is capped, so a leaned-on key nobody drains arrives
            // later as a burst. Holding Backspace in the search field cleared
            // exactly one character before this, because the press queue fires
            // once per press by design and nothing was reading the other one.
            for (const engine::Key key : window.consumeKeyRepeats()) {
                if (typingSearch) {
                    editSearchKey(key);
                }
            }

            // Double-tapping jump toggles flight, on either device. One lambda
            // rather than a copy per device: the timer behind it is shared, so
            // two copies would each half-work.
            const auto tapJump = [&] {
                if (secondsSinceSpacePress < kDoubleTapSeconds) {
                    player.flying = !player.flying;
                    player.velocity = glm::vec3{0.0f};
                    engine::logInfo(player.flying ? "Fly mode ON" : "Fly mode OFF");
                    // Reset, so a third tap starts a fresh pair rather than
                    // toggling again immediately.
                    secondsSinceSpacePress = kDoubleTapSeconds;
                } else {
                    secondsSinceSpacePress = 0.0f;
                }
            };

            for (const engine::Key key : window.consumeKeyPresses()) {
                if (key == engine::Key::Escape) {
                    // With a panel up, Escape closes it and hands the items
                    // back; otherwise it lets go of the mouse.
                    if (openScreen.has_value()) {
                        closeScreen();
                    } else {
                        window.setCursorCaptured(false);
                    }
                    continue;
                }
                if (typingSearch) {
                    // The field holds the keyboard, so everything that is not
                    // an editing key is swallowed rather than acted on - `E`
                    // would close the screen mid-word and `Q` would throw the
                    // cursor's stack away. Escape is already handled above,
                    // which is the one key a text field must not eat.
                    editSearchKey(key);
                    continue;
                }
                if (key == engine::Key::Enter) {
                    // **Enter was otherwise unbound.** Swept across the whole
                    // of `game/` on 2026-08-20: `Key::Enter` occurred only in
                    // `Window.hpp`'s enum and `Window.cpp`'s two translation
                    // tables, and nowhere in gameplay - so nothing confirms a
                    // menu with it and there is nothing to collide with.
                    // **Falsified by** any second `Key::Enter` under `game/`.
                    //
                    // Placed below the search-field branch on purpose, which
                    // swallows every key while the catalogue's text box holds
                    // the keyboard - and deliberately **not** gated on a screen
                    // being closed, because photographing an inventory that
                    // looks wrong is one of the things this was asked for.
                    //
                    // **No debounce of its own, and it does not need one.**
                    // `consumeKeyPresses` fires once per physical press, and
                    // `repeatsWhenHeld` refuses Enter a place in the repeat
                    // queue - so leaning on it takes exactly one photo. F10,
                    // F11, G, C and V are one-shots on the same two facts.
                    const std::filesystem::path directory = photosDirectory();
                    std::error_code error;
                    std::filesystem::create_directories(directory, error);
                    if (error) {
                        engine::logWarn("Photo not taken: could not create " + directory.string() + " (" +
                                        error.message() + ")");
                        continue;
                    }
                    // Absolute by construction - `executableDirectory()` is,
                    // and every step from it is a parent or a named child - so
                    // the line below is a path the player can paste.
                    const std::filesystem::path photo = nextPhotoPath(directory);
                    if (renderer.capturePhoto(photo)) {
                        engine::logInfo("Photo saved: " + photo.string());
                    } else {
                        // `capturePhoto` has already logged *why*. This adds the
                        // half it cannot know is interesting: which file was
                        // meant.
                        engine::logWarn("Photo not saved: " + photo.string());
                    }
                    continue;
                }
                if (key == engine::Key::E) {
                    if (openScreen.has_value()) {
                        closeScreen();
                    } else {
                        // The screen needs a pointer, so opening it hands the
                        // cursor back and closing it takes it again.
                        openScreen = game::inventoryScreen::Kind::Inventory;
                        window.setCursorCaptured(false);
                        hudDirty = true;
                    }
                    continue;
                }
                if (key == engine::Key::Q) {
                    // Only the cursor is emptied here. Dropping from the hotbar
                    // repeats while held, so it lives with the other held-key
                    // actions below.
                    if (openScreen.has_value() && !heldStack.empty()) {
                        // **The sweep ends here too.** This key loop runs
                        // *before* the screen block, so throwing the cursor with
                        // a drag still armed left a sweep spreading a stack that
                        // is now on the floor: under the snapshot rewind that
                        // was one keypress for one free copy, and it is now the
                        // sweep pulling its deposits back out of the slots and
                        // onto a cursor the player has already emptied.
                        cancelDrag();
                        game::dropStack(drops, camera.position + camera.forward() * 0.5f, heldStack,
                                        camera.forward() * kThrowSpeed, kThrowPickupDelay);
                        heldStack = game::ItemStack{};
                        hudDirty = true;
                    }
                    continue;
                }
                if (key == engine::Key::Space) {
                    tapJump();
                    continue;
                }
                if (key == engine::Key::F5) {
                    overlayVisible = !overlayVisible;
                    hudDirty = true;
                    continue;
                }
                if (key == engine::Key::F6 || key == engine::Key::F7) {
                    const int step = (key == engine::Key::F7) ? 1 : -1;
                    // **Clamped to what the SETTINGS FILE can carry, not to what
                    // the world can render, and the gap between those two was
                    // silent data loss.** `World::setVisibleRadius` clamps to
                    // [1, 64], so walking F7 up past 32 was accepted by the
                    // world and looked like it worked. The branch below then
                    // copies the live radius into `settings.renderDistance` and
                    // saves on the same keypress - and `Settings.cpp`'s
                    // `parseUnsigned` REJECTS a value above its limit rather
                    // than clamping it, so on the next launch the field keeps
                    // its struct default of 12. Raise the distance to 40, quit,
                    // come back: 12. Not 40, and not 32 either - which is what
                    // makes it read as "the game forgot" rather than as a
                    // setting being capped. The unit was never in question;
                    // both sides are chunks. The defect was range.
                    //
                    // **The bound belongs on this side of the boundary.**
                    // `world/` has never included `core/` - measured 0 hits
                    // across it, with `item/` at 20 across 13 files as the
                    // control proving the probe does see cross-layer game
                    // includes - so writing 32 into `World.cpp` would be a
                    // second independently authored copy of one number, which is
                    // bug shape #1 one layer down. This file already includes
                    // `core/Settings.hpp`, so it can name the owner directly.
                    //
                    // **Do not "fix" a future mismatch by raising
                    // `kMaxRenderDistance` to 64 instead.** That would make the
                    // loader accept a radius the renderer was never sized for.
                    // The cap is deliberate; the writer was the side that was
                    // wrong.
                    //
                    // **Falsified by**: `kMaxRenderDistance` and
                    // `setVisibleRadius`'s own clamp coming to agree, at which
                    // point this becomes redundant rather than wrong.
                    const int wanted = std::clamp(world.visibleRadius() + step, 1,
                                                  static_cast<int>(game::Settings::kMaxRenderDistance));
                    world.setVisibleRadius(wanted);
                    creatures.setActiveRadius(
                        static_cast<float>(world.visibleRadius() * game::Chunk::kSize));

                    if (world.visibleRadius() != static_cast<int>(settings.renderDistance)) {
                        settings.renderDistance = static_cast<unsigned>(world.visibleRadius());
                        renderer.setFarPlane(static_cast<float>(world.loadRadius() * game::Chunk::kSize) * 1.8f);
                        game::saveSettings(settingsPath, settings);
                        engine::logInfo("Render distance: " + std::to_string(world.visibleRadius()) + " chunks (" +
                                        std::to_string(world.visibleRadius() * game::Chunk::kSize) + " blocks)");
                        hudDirty = true;
                    }
                    continue;
                }
                if (key == engine::Key::F8 || key == engine::Key::F9) {
                    // A live Bramble, spawner and cap ignored. The showcase
                    // cannot serve here because it freezes the simulation, and
                    // a fuse that never lights tests nothing.
                    //
                    // Twelve metres, not five. A Bramble's sense range is 14, so
                    // it hunts the moment it lands, and from five it closed and
                    // detonated in about two seconds - long enough to be a blast
                    // and far too short to watch. The charged one gets its own
                    // key rather than a modifier because **Shift is sneak**:
                    // holding it left the player crouched at 1.3 m/s against a
                    // creeper doing 2.4, which is why backing away did nothing.
                    const bool charged = key == engine::Key::F9;
                    const glm::vec3 look = camera.forward();
                    const glm::vec3 ahead =
                        player.position + glm::vec3{look.x, 0.0f, look.z} * 12.0f;
                    const int ground = world.highestSolid(static_cast<int>(std::floor(ahead.x)),
                                                          static_cast<int>(std::floor(ahead.z)));
                    // Turned to face the player. Derived rather than taken from
                    // the camera, because a creature's yaw is (sin, cos) and the
                    // camera's is (cos, sin) - the two are ninety degrees apart.
                    creatures.place(game::CreatureKind::Bramble,
                                    glm::vec3{ahead.x, static_cast<float>(ground + 1), ahead.z},
                                    std::atan2(-look.x, -look.z), charged);
                    engine::logInfo(charged ? "Spawned a charged Bramble ahead."
                                            : "Spawned a Bramble ahead.");
                    continue;
                }
                if (key >= engine::Key::Num1 && key <= engine::Key::Num9) {
                    selectedSlot = static_cast<std::size_t>(key) - static_cast<std::size_t>(engine::Key::Num1);
                    hudDirty = true;
                    continue;
                }
                if (key == engine::Key::F3 || key == engine::Key::F4) {
                    const float step = key == engine::Key::F3 ? -kFovStep : kFovStep;
                    renderer.setVerticalFov(
                        std::clamp(renderer.verticalFov() + step, kMinFov, kMaxFov));
                    engine::logInfo("Field of view: " + std::to_string(static_cast<int>(renderer.verticalFov())));
                    continue;
                }
                if (key == engine::Key::F10) {
                    settings.toneMapper = (settings.toneMapper + 1) % game::Settings::kToneMapperCount;
                    renderer.setToneMapper(static_cast<engine::ToneMapper>(settings.toneMapper));
                    game::saveSettings(settingsPath, settings);
                    engine::logInfo(std::string("Tone mapping: ") + kToneMapperNames[settings.toneMapper]);
                    continue;
                }
                if (key == engine::Key::F11) {
                    settings.bloom = !settings.bloom;
                    renderer.setBloom(settings.bloom, settings.bloomStrength);
                    game::saveSettings(settingsPath, settings);
                    engine::logInfo(settings.bloom ? "Bloom ON" : "Bloom OFF");
                    continue;
                }
                if (key == engine::Key::G) {
                    settings.shadows = (settings.shadows + 1) % game::Settings::kShadowQualityCount;
                    renderer.setShadowQuality(static_cast<int>(settings.shadows));
                    game::saveSettings(settingsPath, settings);
                    engine::logInfo(std::string("Shadows: ") + kShadowQualityNames[settings.shadows]);
                    continue;
                }
                if (key == engine::Key::C) {
                    settings.clouds = (settings.clouds + 1) % game::Settings::kCloudQualityCount;
                    renderer.setClouds(static_cast<int>(settings.clouds), settings.cloudCoverage,
                                       settings.cloudShadow);
                    game::saveSettings(settingsPath, settings);
                    engine::logInfo(std::string("Clouds: ") + kCloudQualityNames[settings.clouds]);
                    continue;
                }
                if (key == engine::Key::F) {
                    // Auto, then the two pinned modes. Auto is right almost
                    // always; the pins are for a stick worn enough to drift and
                    // for a pad you would rather the game ignored.
                    settings.inputMode = (settings.inputMode + 1) % game::Settings::kInputModeCount;
                    inputMode = static_cast<game::InputMode>(settings.inputMode);
                    game::saveSettings(settingsPath, settings);
                    engine::logInfo(std::string("Input mode: ") + game::inputModeName(inputMode) +
                                    (pad.connected ? " (gamepad connected)" : " (no gamepad)"));
                    continue;
                }
                if (key == engine::Key::V) {
                    // Cycles the override rather than the weather itself: a
                    // storm you asked for has to stay until you ask for
                    // something else, or the countdown ends it mid-look.
                    weather.force(weather.forced() + 1);
                    // Size deduced rather than written, for the reason given
                    // over `kToneMapperNames`: `weather.forced()` indexes this
                    // at runtime, so a dropped row under an explicit `4` would
                    // be a null `const char*` concatenated into a log line.
                    static constexpr std::array kForcedNames{"cycling", "rain",
                                                             "storm", "clear"};
                    engine::logInfo(std::string("Weather: ") + kForcedNames[weather.forced()]);
                    continue;
                }
                if (key == engine::Key::F12) {
                    renderer.setDebugView((renderer.debugView() + 1) % engine::Renderer::kDebugViewCount);
                    engine::logInfo(std::string("Surface view: ") + kDebugViewNames[renderer.debugView()]);
                    continue;
                }
                if (key == engine::Key::F1 && capIndex > 0) {
                    --capIndex;
                } else if (key == engine::Key::F2 && capIndex + 1 < kFpsCapOptions.size()) {
                    ++capIndex;
                } else {
                    continue;
                }
                frameLimiter.setTargetFps(kFpsCapOptions[capIndex]);
                engine::logInfo("Frame cap: " + describeCap(kFpsCapOptions[capIndex]));
            }

            // Gamepad buttons that are not clicks. The layout is the
            // reference's: A jumps, B sneaks, Y opens the item screen, the
            // bumpers page sideways and the triggers mine and place.
            //
            // The pad's *pointer* actions are further down, where they are
            // turned into clicks instead.
            //
            // **Both blocks test this one copy of the screen state**, taken
            // before either can change it. Reading it fresh in the second block
            // meant the Y that opened the inventory arrived again as a click
            // inside it, quick-moving whatever the pointer had been left on.
            const bool padScreenOpen = openScreen.has_value();
            if (inputDevice == game::InputDevice::Gamepad) {
                if (padScreenOpen) {
                    // B backs out, which is what B does in every menu the
                    // reference has. **Start does the same rather than opening
                    // a pause menu**, because there is not one yet.
                    if (pad.pressed(game::PadButton::B) || pad.pressed(game::PadButton::Start)) {
                        closeScreen();
                    } else if (game::inventoryScreen::showsCatalogue(*openScreen)) {
                        const int step = (pad.repeated(game::PadButton::RightBumper) ? 1 : 0) -
                                         (pad.repeated(game::PadButton::LeftBumper) ? 1 : 0);
                        if (step != 0) {
                            const auto tabs =
                                static_cast<int>(game::inventoryScreen::CatalogueTab::Count);
                            int next = (static_cast<int>(catalogue.tab) + step) % tabs;
                            if (next < 0) {
                                next += tabs;
                            }
                            catalogue.tab = static_cast<game::inventoryScreen::CatalogueTab>(next);
                            // Same reset a click on a tab does: a new tab starts
                            // at the top and does not hold the keyboard.
                            catalogue.scrollRow = 0;
                            blurSearch();
                            hudDirty = true;
                        }
                    }
                } else {
                    // The reference puts crafting on X and the inventory on Y.
                    // Here they are one screen - the 2x2 grid lives in it - so
                    // both buttons land on the same place rather than one of
                    // them doing nothing.
                    if (pad.pressed(game::PadButton::Y) || pad.pressed(game::PadButton::X)) {
                        openScreen = game::inventoryScreen::Kind::Inventory;
                        hudDirty = true;
                    }
                    if (pad.pressed(game::PadButton::A)) {
                        tapJump();
                    }
                    const int cycle = (pad.repeated(game::PadButton::RightBumper) ? 1 : 0) -
                                      (pad.repeated(game::PadButton::LeftBumper) ? 1 : 0);
                    if (cycle != 0) {
                        const auto slots = static_cast<int>(game::kHotbarSlots);
                        int next = (static_cast<int>(selectedSlot) + cycle) % slots;
                        if (next < 0) {
                            next += slots;
                        }
                        selectedSlot = static_cast<std::size_t>(next);
                        hudDirty = true;
                    }
                }
            }

            // Drained every frame even when unused, so the queue cannot grow
            // without bound. It only exists to notice a click while the cursor
            // is free.
            std::vector<engine::MouseButton> presses = window.consumeMouseButtonPresses();

            // **The pad drives the existing pointer interface rather than a
            // second copy of it**: a button becomes the click it stands for and
            // the whole of the screen handling below runs unchanged. That is
            // also the reference's arrangement - its controller inventory is a
            // cursor, not a focus ring - and it is why none of the slot code,
            // the sweep or the double-click gather needed touching.
            //
            // At most one press a frame, in a fixed order, so `padQuickMove`
            // can never be read against a different button's click.
            bool padQuickMove = false;
            if (padScreenOpen && inputDevice == game::InputDevice::Gamepad) {
                if (pad.pressed(game::PadButton::A)) {
                    presses.push_back(engine::MouseButton::Left);
                } else if (pad.pressed(game::PadButton::X)) {
                    // Take half, which is what the right button does here and
                    // what X does in the reference's own crafting screen.
                    presses.push_back(engine::MouseButton::Right);
                } else if (pad.pressed(game::PadButton::Y)) {
                    presses.push_back(engine::MouseButton::Left);
                    padQuickMove = true;
                }
            }

            const bool clicked = !presses.empty();
            const bool hadCursor = window.isCursorCaptured();

            // Consumed here with the rest of the presses but acted on after the
            // raycast, so it tests the block under the crosshair *this* frame
            // rather than where the camera was last frame.
            bool wantInteract = false;

            if (openScreen.has_value()) {
                // Already in the space the screen is laid out in: relative to
                // window height, origin at the centre, Y down. Which device put
                // it there was settled at the top of the frame.
                const float cursorX = pointerX;
                const float cursorY = pointerY;
                // **Every press on a screen makes the interface's own noise.**
                // `Click` was staged, decoded and never named, so the entire
                // inventory - slots, tabs, the recipe book, the search field -
                // was operated in silence while the world outside had a sound
                // for everything. One line at the top of the block rather than
                // one per handler, because "a press landed on a screen" is a
                // single fact and the fifteen branches below are what it did.
                //
                // `swallowClickUntilRelease` cannot be set here: it belongs to
                // the click that recaptures a released cursor, which is the
                // `else if` to this branch and so is never reached with a
                // screen up.
                if (!presses.empty()) {
                    sounds.playGlobal(audio, game::SoundEvent::Click, 0.35f);
                }
                const int craftExtent = game::inventoryScreen::craftSize(*openScreen);
                const bool furnaceOpen = *openScreen == game::inventoryScreen::Kind::Furnace;
                const bool smithingOpen = *openScreen == game::inventoryScreen::Kind::SmithingTable;
                const bool stonecutterOpen =
                    *openScreen == game::inventoryScreen::Kind::Stonecutter;
                const bool enderChestOpen =
                    *openScreen == game::inventoryScreen::Kind::Chest &&
                    game::isEnderChest(world.blockAt(openChestPosition.x, openChestPosition.y,
                                                     openChestPosition.z));
                // What the result slot is currently offering, and what taking it
                // costs. A smithing table upgrades rather than crafts, so it can
                // never be a grid pattern - but from here it behaves the same
                // way, which is what keeps the take path as one piece of code.
                const auto pendingResult = [&](std::size_t option) {
                    if (stonecutterOpen) {
                        return stonecutterCut(static_cast<int>(option));
                    }
                    return smithingOpen ? (openBench == game::BlockId::SmithingTable
                                               ? game::smithingResult(craftSlots[0], craftSlots[1])
                                           : openBench == game::BlockId::BrewingStand
                                               ? game::brewingResult(craftSlots[0], craftSlots[1])
                                               : game::repairResult(craftSlots[0], craftSlots[1]))
                                        : game::craftResult(craftSlots.data(), craftExtent);
                };
                const auto spendIngredients = [&] {
                    if (stonecutterOpen) {
                        if (--craftSlots[0].count <= 0) {
                            craftSlots[0] = game::ItemStack{};
                        }
                        return;
                    }
                    if (!smithingOpen) {
                        game::consumeIngredients(craftSlots.data(), craftExtent);
                        return;
                    }
                    for (int i = 0; i < 2; ++i) {
                        if (--craftSlots[i].count <= 0) {
                            craftSlots[i] = game::ItemStack{};
                        }
                    }
                };
                if (furnaceOpen) {
                    furnaceToSlots();
                }
                const auto hit = game::inventoryScreen::slotAt(*openScreen, cursorX, cursorY);

                using Region = game::inventoryScreen::Region;

                // Resolves a hit to the stack behind it. The crafting result is
                // deliberately absent: it is produced, not stored, so it cannot
                // be written to.
                const auto stackAt = [&](const game::inventoryScreen::SlotHit& at) -> game::ItemStack* {
                    if (at.region == Region::Grid) {
                        return &inventory.slot(at.index);
                    }
                    if (at.region == Region::Chest) {
                        // An ender chest is a window onto the player's own
                        // twenty-seven, so it resolves before the block map is
                        // consulted at all.
                        if (enderChestOpen) {
                            return &enderChest.slots[at.index % game::kChestSlots];
                        }
                        const glm::ivec3 where =
                            at.index < game::kChestSlots ? openChestPosition : openChestPartner;
                        const auto found = chests.find(where);
                        return found == chests.end() ? nullptr
                                                     : &found->second.slots[at.index % game::kChestSlots];
                    }
                    if (at.region == Region::Craft) {
                        return &craftSlots[at.index];
                    }
                    // **A worn piece is a real stack in a real slot**, so the
                    // ordinary pick-up, swap and shift-click all work through
                    // it. What this deliberately does *not* carry is the rule
                    // about which piece may go where - that is `armourSlot`'s.
                    // A resolver that also policed the type would be a second
                    // owner of the same question.
                    //
                    // **This used to say the rule is enforced "at the one place
                    // a piece can enter, further down", and that undercount
                    // cost two reviewers a wrong verdict on 2026-08-19.** One
                    // read the bare pointer, went looking for the single door,
                    // could not find one that covered a plain drag, and filed a
                    // CRITICAL saying four stacks of anything could be parked in
                    // the armour column; the recommended fix was a guard at the
                    // deposit site, which is exactly the second owner the
                    // paragraph above forbids. The code was right the whole
                    // time. So the doors are LISTED rather than counted, because
                    // an ordinal cannot be checked and a list can - and because
                    // "further down" is not a search a reader can run.
                    //
                    // **Five callers, and Armour is unreachable at three of them
                    // structurally rather than by a guard that could rot.**
                    // `rewindDrag`, `applyDrag`'s target loop and the
                    // drag-collapses-to-a-click branch all resolve entries out
                    // of `draggedSlots`, and `draggedSlots` has exactly two fill
                    // sites: the sweep's, which tests
                    // `region != Region::Armour` explicitly beside the
                    // `CraftResult` test, and the press-with-a-full-cursor one,
                    // which sits *below* the plain-click armour branch and is
                    // therefore dominated by that branch's unconditional
                    // `continue`. An armour cell cannot join the list, so
                    // nothing that reads the list can write one.
                    //
                    // **The other two each apply the one rule.** The plain click
                    // asks `armourSlot(heldStack.item) == hit->index` before it
                    // swaps, and shift-click hands the piece to
                    // `Inventory::equipArmour`, which derives the destination
                    // from the same `armourSlot`. Two enforcement sites, one
                    // owner between them.
                    //
                    // **Falsified by**: a third `draggedSlots` fill site, a
                    // sixth `stackAt` caller, or the press-with-a-full-cursor
                    // branch moving above the armour branch. Grep
                    // `draggedSlots.push_back` and `stackAt(...)` - **2 and 5
                    // code sites, but a raw grep returns 3 and 6**, because this
                    // paragraph names each token once and so matches itself.
                    // Subtract this comment before concluding the falsifier has
                    // fired. Measured 2026-08-19: 2 fill sites at the sweep and
                    // the full-cursor press; 5 callers, being `rewindDrag`,
                    // `applyDrag`'s target loop, the collapse-to-a-click branch,
                    // and the two that apply the rule.
                    //
                    // **Reported as a live hole twice, on 2026-08-19, and it is
                    // not one - both reports failed the same way.** Each
                    // enumerated the sites that WRITE an armour cell, found no
                    // type test at the drag path, and concluded it was
                    // unpoliced. The refusal is not a guard at the write: it is
                    // the plain-click armour branch's `continue`, which sits
                    // outside that branch's inner type test and so fires for
                    // EVERY armour-region click, including one holding a full
                    // cursor of cobblestone. Control therefore never reaches the
                    // full-cursor push below it with an armour hit. A sweep for
                    // a guard cannot see a `continue` roughly fifty lines above
                    // the site it protects, and this is the same instrument
                    // failure that produced the report of an ungated
                    // `savePlayer`, where an early return dominated all eight
                    // writers. **When a per-site audit finds no guard, ask what
                    // dominates the site before concluding it is open.**
                    if (at.region == Region::Armour &&
                        at.index < game::inventoryScreen::armourSlotCount(*openScreen)) {
                        return &inventory.armourAt(at.index);
                    }
                    // A furnace's output is a real slot holding real items; a
                    // crafting result is computed and cannot be written to.
                    if (at.region == Region::CraftResult && furnaceOpen) {
                        return &furnaceOutput;
                    }
                    return nullptr;
                };

                // Where a shift-click sends a stack. The hotbar and storage feed
                // each other; a crafting grid empties into the whole inventory.
                const auto quickMoveTargets = [&](const game::inventoryScreen::SlotHit& at) {
                    std::vector<game::ItemStack*> targets;
                    targets.reserve(game::kInventorySlots);

                    const auto append = [&](std::size_t begin, std::size_t end) {
                        for (std::size_t i = begin; i < end; ++i) {
                            targets.push_back(&inventory.slot(i));
                        }
                    };

                    if (game::inventoryScreen::isContainer(*openScreen)) {
                        // A container is a second store, not a workbench: a
                        // shift-click crosses between the two halves of the
                        // screen rather than shuffling within one.
                        if (at.region == Region::Chest) {
                            append(0, game::kInventorySlots);
                            return targets;
                        }
                        if (enderChestOpen) {
                            for (game::ItemStack& slot : enderChest.slots) {
                                targets.push_back(&slot);
                            }
                            return targets;
                        }
                        const bool doubled = *openScreen == game::inventoryScreen::Kind::DoubleChest;
                        for (int half = 0; half < (doubled ? 2 : 1); ++half) {
                            const auto found =
                                chests.find(half == 0 ? openChestPosition : openChestPartner);
                            if (found == chests.end()) {
                                continue;
                            }
                            // **Only the slots the screen shows.** A hopper
                            // stores its five in the same struct a chest uses,
                            // and shift-clicking into the other twenty-two
                            // would put items where nothing can reach them.
                            const std::size_t shown =
                                game::inventoryScreen::chestSlotCount(*openScreen);
                            for (std::size_t i = 0; i < found->second.slots.size() && i < shown;
                                 ++i) {
                                targets.push_back(&found->second.slots[i]);
                            }
                        }
                        if (!targets.empty()) {
                            return targets;
                        }
                    }

                    if (at.region != Region::Grid) {
                        append(0, game::kInventorySlots);
                    } else if (at.index < game::kHotbarSlots) {
                        append(game::kHotbarSlots, game::kInventorySlots);
                    } else {
                        append(0, game::kHotbarSlots);
                    }
                    return targets;
                };

                const bool leftDown = window.isMouseButtonDown(engine::MouseButton::Left);
                const bool rightDown = window.isMouseButtonDown(engine::MouseButton::Right);

                // **Takes back what the sweep deposited, and only that.** Not a
                // snapshot: a furnace tick, a hopper pass or a drop landing in
                // the bag may all have written these slots since, and each of
                // them was a live duplication or a live loss while this
                // restored a remembered stack over the top of their work. See
                // `DragDeposit` for the three, and for why one subtraction
                // beats three guards.
                //
                // `slots::merge` is the mover, so item, damage and the stack cap
                // have their usual single owner and no field is written by hand;
                // it reports what actually crossed, which is what is struck off
                // the claim. Anything it could not move stays in the slot and
                // stays claimed - items conserved either way.
                const auto rewindDrag = [&] {
                    for (DragDeposit& deposit : draggedSlots) {
                        if (deposit.placed <= 0) {
                            continue;
                        }
                        game::ItemStack* stack = stackAt(deposit.at);
                        // A slot that now holds something else entirely - or
                        // nothing - was emptied by whoever wrote it, and they
                        // accounted for what they took. The claim simply lapses.
                        if (stack == nullptr || stack->empty() || stack->item != deposit.item ||
                            stack->damage != deposit.damage) {
                            deposit.placed = 0;
                            continue;
                        }
                        deposit.placed -= game::slots::merge(
                            heldStack, *stack, std::min(deposit.placed, stack->count));
                    }
                };

                const auto applyDrag = [&] {
                    rewindDrag();
                    std::vector<game::ItemStack*> targets;
                    // Which deposit each target belongs to: a slot whose screen
                    // has gone resolves to nothing, so the two lists are not
                    // index-for-index.
                    std::vector<std::size_t> owners;
                    std::vector<int> before;
                    targets.reserve(draggedSlots.size());
                    owners.reserve(draggedSlots.size());
                    before.reserve(draggedSlots.size());
                    for (std::size_t i = 0; i < draggedSlots.size(); ++i) {
                        if (game::ItemStack* stack = stackAt(draggedSlots[i].at); stack != nullptr) {
                            targets.push_back(stack);
                            owners.push_back(i);
                            before.push_back(stack->count);
                        }
                    }
                    // Read before the call, because `distribute` empties what it
                    // is spreading - the mutating-helper trap this whole cluster
                    // came out of.
                    const game::ItemStack spreading = heldStack;
                    game::slots::distribute(targets, heldStack, dragButton == DragButton::Right);
                    // What actually landed, measured rather than assumed:
                    // `distribute` skips a slot with no room and shares only
                    // between the ones that have some.
                    for (std::size_t t = 0; t < targets.size(); ++t) {
                        DragDeposit& deposit = draggedSlots[owners[t]];
                        deposit.placed = std::max(0, targets[t]->count - before[t]);
                        if (deposit.placed > 0) {
                            deposit.item = spreading.item;
                            deposit.damage = spreading.damage;
                        }
                    }
                };

                if (dragButton != DragButton::None) {
                    const bool stillHeld = dragButton == DragButton::Left ? leftDown : rightDown;

                    if (stillHeld) {
                        // **The armour cells are excluded alongside the crafting
                        // result, and for a different reason worth stating.** A
                        // sweep spreads one stack across every slot it crosses;
                        // an armour cell takes exactly one item and only if it
                        // is the right piece, so a sweep that passed over the
                        // column would either drop a helmet into the boots slot
                        // or silently swallow part of what is on the cursor.
                        // Refused here rather than filtered inside `applyDrag`,
                        // so the cell never joins `draggedSlots` and the rewind
                        // has nothing to undo.
                        if (hit.has_value() && hit->region != Region::CraftResult &&
                            hit->region != Region::Armour) {
                            const bool already =
                                std::any_of(draggedSlots.begin(), draggedSlots.end(), [&](const DragDeposit& seen) {
                                    return seen.at.region == hit->region && seen.at.index == hit->index;
                                });
                            if (!already) {
                                // The slot is not read here: what goes on the
                                // list is a claim of nothing, which `applyDrag`
                                // fills in from what the distribution actually
                                // put there. Recorded even when the slot cannot
                                // be resolved, so the crossed-slot count below
                                // stays a count of slots crossed.
                                rewindDrag();
                                draggedSlots.push_back(DragDeposit{*hit});
                                applyDrag();
                                hudDirty = true;
                            }
                        }
                    } else {
                        // A sweep that never left its first slot is an ordinary
                        // click, so undo the split and treat it as one.
                        if (draggedSlots.size() < 2) {
                            rewindDrag();
                            if (!draggedSlots.empty()) {
                                if (game::ItemStack* stack = stackAt(draggedSlots.front().at); stack != nullptr) {
                                    if (dragButton == DragButton::Right) {
                                        game::slots::rightClick(*stack, heldStack);
                                    } else {
                                        game::slots::leftClick(*stack, heldStack);
                                    }
                                }
                            }
                        }
                        dragButton = DragButton::None;
                        draggedSlots.clear();
                        hudDirty = true;
                    }
                }

                for (const engine::MouseButton button : presses) {
                    // **Nothing else may touch the cursor while a sweep owns
                    // it.** Arming happens further down this same loop, so this
                    // only ever refuses a *second* press - which is the right
                    // answer anyway. Throwing the cursor out of the panel,
                    // binning it in the catalogue and taking a fresh stack from
                    // the catalogue all assign `heldStack` behind the drag's
                    // back, and the next `rewindDrag` pushes the sweep's
                    // deposits onto it on top of whatever is there: the items
                    // are on the floor **and** back on the cursor.
                    if (dragButton != DragButton::None) {
                        break;
                    }
                    const bool right = button == engine::MouseButton::Right;

                    // Focus follows the click, and every click that is not on
                    // the field lets it go - which is what a text box does
                    // everywhere, and the only thing that makes "E closes the
                    // screen" true again the moment you are not typing.
                    {
                        const bool onField =
                            catalogue.tab == game::inventoryScreen::CatalogueTab::Search &&
                            game::inventoryScreen::insideSearchField(*openScreen, cursorX, cursorY);
                        if (onField != catalogue.searchFocused) {
                            if (onField) {
                                catalogue.searchFocused = true;
                            } else {
                                blurSearch();
                            }
                            hudDirty = true;
                        }
                        if (onField) {
                            continue; // The field swallows its own click.
                        }
                    }

                    if (!hit.has_value()) {
                        if (const auto tab = game::inventoryScreen::tabAt(*openScreen, cursorX, cursorY);
                            tab.has_value()) {
                            catalogue.tab = *tab;
                            // A new tab starts at the top. Keeping the offset
                            // would open a short list scrolled past its end.
                            catalogue.scrollRow = 0;
                            blurSearch();
                            hudDirty = true;
                            continue;
                        }

                        // The catalogue is a *source*, not a container: an entry
                        // never depletes and nothing can be placed into it. Left
                        // click takes a full stack, right click takes one, and
                        // shift sends a stack straight to the inventory.
                        //
                        // Survival gets nothing from it yet - there it is a
                        // recipe book, and clicking a recipe fills the grid
                        // rather than conjuring the item.
                        if (creative && game::inventoryScreen::showsCatalogue(*openScreen)) {
                            // A cell past the end of the list is not an entry,
                            // it is part of the list's empty space - so it falls
                            // through to the bin below rather than swallowing
                            // the click.
                            bool tookEntry = false;
                            if (const auto cell = game::inventoryScreen::catalogueCellAt(
                                    *openScreen, cursorX, cursorY, catalogue.scrollRow);
                                cell.has_value()) {
                                const std::vector<game::ItemId> listed =
                                    game::inventoryScreen::catalogueItems(catalogue);
                                if (*cell < listed.size()) {
                                    const game::ItemId picked = listed[*cell];
                                    if ((window.isKeyDown(engine::Key::LeftShift) || padQuickMove) && !right) {
                                        // **The one `add` whose return is
                                        // rightly ignored.** The catalogue is a
                                        // source, not a store: nothing is being
                                        // moved out of anywhere, so a remainder
                                        // that will not fit is simply not
                                        // conjured. Every other discarded return
                                        // in this file destroyed real items.
                                        inventory.add(picked, game::maxStackFor(picked));
                                    } else {
                                        // Whatever the cursor held is replaced
                                        // rather than swapped - a source list has
                                        // nowhere to put it.
                                        heldStack = game::ItemStack{picked,
                                                                    right ? 1 : game::maxStackFor(picked)};
                                    }
                                    hudDirty = true;
                                    tookEntry = true;
                                }
                            }
                            if (tookEntry) {
                                continue;
                            }
                            if (creative &&
                                game::inventoryScreen::insideCatalogueList(*openScreen, cursorX, cursorY)) {
                                // Every part of the list that is not a filled
                                // entry is the bin: the empty cells, the gaps and
                                // the clipped row at the bottom. Left click
                                // destroys what the cursor carries; the reference
                                // puts it back where it came from on right click,
                                // which needs an origin slot we do not track, so
                                // right click leaves it alone.
                                if (!right && !heldStack.empty()) {
                                    heldStack = game::ItemStack{};
                                    hudDirty = true;
                                }
                                continue;
                            }
                        }

                        if (!game::inventoryScreen::insidePanel(*openScreen, cursorX, cursorY) &&
                            !heldStack.empty()) {
                            // Clicking into the world throws items away, the way
                            // letting go of something over open ground would.
                            // The right button parts with one; the left, all of
                            // it.
                            const int thrown = right ? 1 : heldStack.count;
                            game::dropStack(drops, camera.position + camera.forward() * 0.5f,
                                            heldStack, thrown, camera.forward() * kThrowSpeed,
                                            kThrowPickupDelay);
                            heldStack.count -= thrown;
                            if (heldStack.count <= 0) {
                                heldStack = game::ItemStack{};
                            }
                            hudDirty = true;
                        }
                        continue;
                    }

                    const bool shiftHeld = window.isKeyDown(engine::Key::LeftShift) || padQuickMove;

                    if (shiftHeld && !right) {
                        if (hit->region == Region::CraftResult && !furnaceOpen) {
                            // Crafts as many as will fit rather than one. Every
                            // pass spends at least one ingredient, so this always
                            // terminates.
                            while (true) {
                                const game::ItemStack batch = pendingResult(hit->index);
                                // **`damage` on the test and on the add.** A
                                // crafted stowbox or a repaired tool carries
                                // one, and asking about the item alone counted a
                                // box holding other contents as room that does
                                // not exist - then laundered the result into a
                                // fresh one when it landed.
                                if (batch.empty() ||
                                    !inventory.hasRoomFor(batch.item, batch.count, batch.damage)) {
                                    break;
                                }
                                inventory.add(batch.item, batch.count, batch.damage);
                                spendIngredients();
                            }
                        } else if (game::ItemStack* moving = stackAt(*hit); moving != nullptr) {
                            // **Shift-clicking a piece in the bag puts it on**,
                            // which is the reference's behaviour and the route
                            // most players reach for before they find the four
                            // cells at all. `equipArmour` derives the
                            // destination from `armourSlot` and swaps, so a
                            // second helmet trades with the one being worn
                            // rather than being refused or destroyed.
                            //
                            // **And only when the open screen actually HAS
                            // armour cells**, which with a chest, a double
                            // chest, a hopper, a furnace, a crafting table, a
                            // smithing table or a stonecutter open it does not.
                            // `armourSlotCount` answers `kArmourSlots` for the
                            // plain inventory and **0** for every other screen,
                            // deliberately - the panel art for the others has
                            // the armour column painted out - and all four
                            // readers gate on it: `build`'s draw loop, `slotAt`,
                            // the tooltip switch and this file's own `stackAt`.
                            // So without this clause the piece left the grid,
                            // was drawn nowhere, could not be clicked and could
                            // not be reached by any means a player would find.
                            // Nothing was destroyed - `equipArmour` is a swap -
                            // but the observed behaviour was "the chest ate my
                            // diamond helmet", and if something was already worn
                            // the old piece appeared in the slot the new one had
                            // just left, which reads as the chest trading a good
                            // helmet for a bad one. The plain third consequence:
                            // **armour could not be shift-clicked into a chest
                            // at all**, which is how everybody stores it.
                            //
                            // **The screen test goes first so it short-circuits
                            // ahead of `equipArmour`**, which mutates.
                            //
                            // Falsifier, so this does not rot: it becomes wrong
                            // the day a container panel grows an armour column,
                            // at which point `armourSlotCount` stops answering 0
                            // for that `Kind` and this clause silently starts
                            // permitting the equip again - which is the right
                            // answer then. It read
                            // `kind == Kind::Inventory ? kArmourSlots : 0` in
                            // `hud/InventoryScreen.hpp` on 2026-08-19.
                            //
                            // **Only from the grid.** Asked of a worn piece this
                            // would put it straight back on, and taking armour
                            // off with shift-click is exactly what the fall
                            // through to `quickMove` below already does.
                            const bool equipped =
                                game::inventoryScreen::armourSlotCount(*openScreen) > 0 &&
                                hit->region == Region::Grid && game::isArmour(moving->item) &&
                                inventory.equipArmour(hit->index);
                            if (!equipped) {
                                game::slots::quickMove(*moving, quickMoveTargets(*hit));
                            }
                        }
                        hudDirty = true;
                        continue;
                    }

                    if (hit->region == Region::CraftResult && !furnaceOpen) {
                        // The result is a preview until it is taken, which is why
                        // the ingredients are only spent here.
                        const game::ItemStack made = pendingResult(hit->index);
                        if (made.empty()) {
                            continue;
                        }
                        const bool intoCursor = heldStack.empty();
                        // **`roomFor`, not `space()`.** The shift-click path
                        // twenty lines above already compares `damage`; this
                        // one asked about the item alone, so a crafted stowbox
                        // holding one set of contents merged onto a cursor
                        // holding another and one of them stopped existing.
                        const bool ontoSame =
                            !intoCursor && game::slots::roomFor(heldStack, made) >= made.count;
                        if (intoCursor || ontoSame) {
                            if (intoCursor) {
                                heldStack = made;
                            } else {
                                heldStack.count += made.count;
                            }
                            spendIngredients();
                            hudDirty = true;
                        }
                        continue;
                    }

                    game::ItemStack* stack = stackAt(*hit);
                    if (stack == nullptr) {
                        continue;
                    }

                    // Smelted output comes out and never goes back in. Putting
                    // something into it would let a furnace be used as a chest,
                    // and worse, be consumed by the next thing it finished.
                    if (furnaceOpen && hit->region == Region::CraftResult) {
                        if (!stack->empty()) {
                            // **One `merge`, not a hand-rolled pair.** This was
                            // two branches - a whole-stack assignment when the
                            // cursor was empty and a clamped top-up when it was
                            // not - and only the second one asked whether the
                            // items were the same kind. `merge` is the owner of
                            // both halves: it gives an empty cursor the output's
                            // `damage`, it clamps to what the item actually
                            // stacks to, and it empties the output slot when the
                            // last one leaves.
                            //
                            // **The single edit that breaks this:** writing
                            // `heldStack = *stack` back in for the empty case
                            // skips the cap, so a furnace holding more than a
                            // stack hands the surplus to the cursor.
                            game::slots::merge(heldStack, *stack, stack->count);
                            hudDirty = true;
                        }
                        continue;
                    }

                    // **The one slot family whose contents are decided by the
                    // item and not by the click.** Handled here, before the
                    // double-click gather and before a full cursor arms a
                    // sweep, because both of those are wrong for a cell that
                    // holds one piece of a named kind.
                    //
                    // `armourSlot` is the only thing consulted, and it is the
                    // same owner `Inventory::equipArmour` and
                    // `Inventory::armourSet` use - so a sword cannot enter the
                    // helmet cell, and boots cannot enter the head cell either.
                    // `ArmourSlot::None` is 4 and no cell index is 4, which is
                    // what makes the non-armour case fall out of the same test
                    // rather than needing an `isArmour` of its own.
                    //
                    // Taking a piece *out* is unconditional: the cursor is
                    // empty, so there is nothing to refuse.
                    if (hit->region == Region::Armour) {
                        if (heldStack.empty() ||
                            static_cast<std::size_t>(game::armourSlot(heldStack.item)) ==
                                hit->index) {
                            if (right) {
                                game::slots::rightClick(*stack, heldStack);
                            } else {
                                game::slots::leftClick(*stack, heldStack);
                            }
                            hudDirty = true;
                        }
                        continue;
                    }

                    const bool sameSlot = hit->region == lastClickRegion && hit->index == lastClickIndex;
                    const bool soonAfter = now - lastSlotClick <= kDoubleClickWindow;
                    lastSlotClick = now;
                    lastClickRegion = hit->region;
                    lastClickIndex = hit->index;

                    // Checked before the drag is armed, because the second click
                    // of a double-click always arrives with a full cursor and
                    // would otherwise be read as the start of a sweep.
                    if (!right && !heldStack.empty() && sameSlot && soonAfter) {
                        std::vector<game::ItemStack*> sources;
                        sources.reserve(game::kInventorySlots + 2 * game::kChestSlots);
                        for (std::size_t i = 0; i < game::kInventorySlots; ++i) {
                            sources.push_back(&inventory.slot(i));
                        }
                        // **And the open container's slots.** This gathered
                        // only from the player's own thirty-six, so
                        // double-clicking inside a chest picked up whatever was
                        // in your bag and left the twenty-six other stacks of
                        // the same thing sitting in the chest in front of you -
                        // which reads as the feature simply not working.
                        //
                        // Sourced from `quickMoveTargets`, which already owns
                        // "which of a container's slots does this screen
                        // actually show": asked from a `Grid` slot it returns
                        // the container half, ender chest and hopper included,
                        // so this cannot drift from shift-click's answer.
                        if (game::inventoryScreen::isContainer(*openScreen)) {
                            for (game::ItemStack* slot :
                                 quickMoveTargets(game::inventoryScreen::SlotHit{Region::Grid, 0})) {
                                sources.push_back(slot);
                            }
                        }
                        game::slots::gather(heldStack, sources);
                        hudDirty = true;
                        continue;
                    }

                    // Pressing with a full cursor begins a sweep rather than
                    // acting immediately: which it turns out to be is only known
                    // once the button comes back up. Picking a stack *up* never
                    // starts one, or moving away from the slot you just took
                    // from would scatter it again.
                    if (!heldStack.empty()) {
                        dragButton = right ? DragButton::Right : DragButton::Left;
                        draggedSlots.clear();
                        draggedSlots.push_back(DragDeposit{*hit});
                        applyDrag();
                        hudDirty = true;
                        continue;
                    }

                    if (right) {
                        game::slots::rightClick(*stack, heldStack);
                    } else {
                        game::slots::leftClick(*stack, heldStack);
                    }
                    hudDirty = true;
                }

                // Written back once, after every press this frame has been
                // handled, so the block is authoritative again before it ticks.
                if (furnaceOpen) {
                    slotsToFurnace();
                }
            } else if (!hadCursor && clicked) {
                window.setCursorCaptured(true);
                swallowClickUntilRelease = true;
            } else if (hadCursor && player.alive()) {
                // **`player.alive()` is half of the same rule `playing` carries
                // further down**, and it has to be restated here because this
                // runs six hundred lines earlier - a corpse could open a chest
                // and a jukebox for the whole 1.6 s of `kRespawnSeconds`, and
                // then respawn with the screen still up. `wantInteract` is the
                // only door into the screen-opening block, so this is the one
                // place it needs saying.
                //
                // Sneaking suppresses this, which is what lets you place a block
                // on top of a table rather than opening it.
                wantInteract = !window.isKeyDown(engine::Key::LeftShift) &&
                               std::any_of(presses.begin(), presses.end(), [](engine::MouseButton button) {
                                   return button == engine::MouseButton::Right;
                               });
                // Same rule on the pad: the left trigger uses, B is sneak.
                if (inputDevice == game::InputDevice::Gamepad &&
                    pad.pressed(game::PadButton::LeftTrigger) && !pad.down(game::PadButton::B)) {
                    wantInteract = true;
                }
            }

            // Scrolling away from the user moves right along the bar, and the
            // selection wraps at both ends.
            //
            // **While a screen is open the wheel belongs to the catalogue**, the
            // same way the movement keys do. Without that guard it kept driving
            // the hotbar underneath - silently, because the bar is hidden behind
            // the panel while you are looking at it.
            const float scroll = window.consumeScrollDelta();
            // The right stick scrolls the catalogue, the way the wheel does.
            // In the world that same stick is the camera, so this only counts
            // while a panel is up.
            scrollCarry += scroll + ((inputDevice == game::InputDevice::Gamepad && openScreen.has_value())
                                         ? pad.scrollNotches
                                         : 0.0f);
            const int notches = static_cast<int>(scrollCarry);
            scrollCarry -= static_cast<float>(notches);
            if (notches != 0) {
                if (openScreen.has_value() && game::inventoryScreen::showsCatalogue(*openScreen)) {
                    const int limit = game::inventoryScreen::catalogueMaxScroll(
                        game::inventoryScreen::catalogueItems(catalogue).size());
                    const int next = std::clamp(catalogue.scrollRow - notches, 0, limit);
                    if (next != catalogue.scrollRow) {
                        catalogue.scrollRow = next;
                        hudDirty = true;
                    }
                } else if (!openScreen.has_value()) {
                    const auto slots = static_cast<int>(game::kHotbarSlots);
                    int next = (static_cast<int>(selectedSlot) - notches) % slots;
                    if (next < 0) {
                        next += slots;
                    }
                    selectedSlot = static_cast<std::size_t>(next);
                    hudDirty = true;
                }
            }

            // The overlay wants refreshing continuously, and so does the
            // inventory because the held stack follows the cursor. Everything
            // else only when the selection changes.
            //
            // **A drawing bow is the third continuous thing.** `hudDirty` is
            // only set when the draw starts and when it ends, so without this
            // the hotbar was built once at the start of the pull and not again
            // until the arrow left - the picture and the pullback were both
            // computed correctly every frame and never uploaded. Exactly the
            // dig bar's bug: a transient HUD element has to ask for the frames
            // it changes on.
            const bool overlayDue = overlayVisible && now - lastHudRebuild >= kOverlayRefreshInterval;
            uiSeconds = std::fmod(uiSeconds + deltaSeconds, kCaretBlinkSeconds);
            catalogue.caretVisible = uiSeconds < kCaretBlinkSeconds * 0.5f;
            if (breakProgress <= 0.0f && lastBreakProgress > 0.0f) {
                hudDirty = true;
            }
            lastBreakProgress = breakProgress;

            // **The survival bars are the fifth continuous case**, and they get
            // a comparison rather than a flag because nothing sets one: health,
            // food and air all change from inside the physics. Comparing what
            // was last *drawn* means a bar cannot stick on screen after the
            // number behind it moved, which is the dig bar's bug in its fourth
            // outfit. The flash is included so the hearts stop shaking.
            //
            // **The air row is compared through the builder's own answer**,
            // never through a bubble count worked out here. A count agrees with
            // the row in the middle and disagrees at both ends: submerging left
            // `ceil(0.999 * 10)` at ten, so the row did not appear until a
            // bubble and a half had gone, and surfacing reached ten while the
            // row was still drawn, so it hung there until something unrelated
            // dirtied the HUD. A value derived somewhere other than the one
            // place that owns it, which is the oldest shape of bug here.
            const game::hud::AirRow air =
                game::hud::airRow(player.air / game::fluid::kAirSeconds);
            // **And the gold hearts through theirs**, for the same reason and
            // with the extra one that the pool is a `float`: comparing it
            // directly would rebuild the HUD for every fractional point that
            // changed no icon, and rounding it here rather than in
            // `absorptionHearts` would be the bubble count again.
            const int absorbHearts = game::hud::absorptionHearts(player.absorption);
            const bool statusMoved = player.health != lastShownHealth ||
                                     player.food != lastShownFood || air != lastShownAir ||
                                     absorbHearts != lastShownAbsorbHearts ||
                                     (player.hurtFlash > 0.0f) != lastShownHurt;
            if (statusMoved) {
                hudDirty = true;
                lastShownHealth = player.health;
                lastShownFood = player.food;
                lastShownAir = air;
                lastShownAbsorbHearts = absorbHearts;
                lastShownHurt = player.hurtFlash > 0.0f;
            }

            // **The recipe book only ever grows.** Recomputed when something in
            // the inventory moved rather than every frame, because scanning
            // every recipe is not free and nothing else can change the answer.
            //
            // Creative keeps the whole catalogue: there it is a source to take
            // from, not a book of what you have learned.
            catalogue.restrictToKnown = !creative;
            if (hudDirty && !creative) {
                // The full grid, so a recipe you could only make at a table is
                // still learned by holding its ingredients.
                for (const game::ItemId made : game::craftableItems(inventory, game::kMaxCraftSize)) {
                    catalogue.known.insert(made);
                }
            }

            if (hudDirty || overlayDue || openScreen.has_value() || breakProgress > 0.0f || bowHeld) {
                game::OverlayStats stats;
                stats.frameMilliseconds = deltaSeconds * 1000.0f;
                stats.gpuMilliseconds = renderer.stats().gpuMilliseconds;
                stats.fps = deltaSeconds > 0.0f ? static_cast<int>(1.0f / deltaSeconds) : 0;
                stats.loadedChunks = world.loadedChunkCount();
                stats.meshes = renderer.meshCount();
                stats.pending = world.pendingChunkCount();
                stats.retired = renderer.retiredMeshCount();
                stats.drawCalls = renderer.stats().drawCalls;
                stats.triangles = renderer.stats().triangles;
                stats.workerThreads = jobs.threadCount();
                stats.renderDistance = world.visibleRadius();
                stats.detailDistance = world.detailRadius();
                stats.detailedChunks = world.detailedChunkCount();
                const game::World::DetailLag lag = world.detailLag();
                stats.detailLagChunks = lag.chunks;
                stats.detailLagNearest = lag.nearest;
                stats.deviceAllocations = renderer.stats().deviceAllocations;
                stats.deviceAllocationLimit = renderer.stats().deviceAllocationLimit;
                stats.gpuMegabytes = renderer.stats().pooledMegabytesHeld;
                stats.biome = game::biomeInfo(game::sampleBiome(kWorldSeed,
                                                                static_cast<int>(std::floor(player.position.x)),
                                                                static_cast<int>(std::floor(player.position.z)))
                                                  .dominant)
                                  .name;
                stats.air = player.air;
                stats.toneMapper = kToneMapperNames[settings.toneMapper];
                stats.shadows = kShadowQualityNames[settings.shadows];
                stats.clouds = kCloudQualityNames[settings.clouds];

                rebuildHud(stats);
                lastHudRebuild = now;

                if (hudDirty) {
                    const game::ItemStack& held = inventory.slot(selectedSlot);
                    engine::logInfo(std::string("Holding: ") +
                                    (held.empty() ? "nothing" : game::displayNameOf(held.item)) +
                                    (held.empty() ? "" : " x" + std::to_string(held.count)));
                    hudDirty = false;
                }
            }

            const engine::CursorDelta look = window.consumeCursorDelta();
            // The panel owns the pointer while it is up. In gamepad mode the
            // OS cursor stays captured behind it, so without the screen test a
            // knocked mouse would turn the camera while you shopped.
            if (window.isCursorCaptured() && !openScreen.has_value()) {
                // Screen Y grows downward, so moving the mouse down must pitch down.
                camera.addLook(look.x * kLookRadiansPerPixel, -look.y * kLookRadiansPerPixel);
            }
            if (!openScreen.has_value()) {
                // **Added, not scaled.** This is already radians for this
                // frame; the mouse constant above is radians per *pixel*, and
                // putting a stick through it would make turning depend on the
                // frame rate.
                camera.addLook(pad.lookRadians.x, pad.lookRadians.y);
            }

            // Movement is relative to where the camera is looking, but flattened
            // so that looking down does not drive you into the ground.
            glm::vec3 forward = camera.forward();
            forward.y = 0.0f;
            glm::vec3 right = camera.right();
            right.y = 0.0f;

            game::PlayerInput move;
            // Keys belong to the screen while it is open, not to the player.
            if (!openScreen.has_value()) {
                const bool keyMoving =
                    window.isKeyDown(engine::Key::W) || window.isKeyDown(engine::Key::S) ||
                    window.isKeyDown(engine::Key::A) || window.isKeyDown(engine::Key::D);
                if (window.isKeyDown(engine::Key::W)) {
                    move.moveDirection += forward;
                }
                if (window.isKeyDown(engine::Key::S)) {
                    move.moveDirection -= forward;
                }
                if (window.isKeyDown(engine::Key::D)) {
                    move.moveDirection += right;
                }
                if (window.isKeyDown(engine::Key::A)) {
                    move.moveDirection -= right;
                }
                move.moveDirection += forward * pad.move.y + right * pad.move.x;

                // How far the stick is pushed is the walking speed, and a key
                // is always all the way. `moveDirection` is normalised inside
                // the physics, so the magnitude has to travel separately.
                const float push = std::min(std::sqrt(pad.move.x * pad.move.x + pad.move.y * pad.move.y), 1.0f);
                move.moveScale = std::max(keyMoving ? 1.0f : 0.0f, push);

                const bool jumping = window.isKeyDown(engine::Key::Space) || pad.down(game::PadButton::A);
                const bool sneaking = window.isKeyDown(engine::Key::LeftShift) || pad.down(game::PadButton::B);
                move.jump = jumping;
                move.sprint = window.isKeyDown(engine::Key::LeftControl) || pad.sprint;
                move.sneak = sneaking;
                move.lookY = glm::normalize(camera.forward()).y;
                move.verticalWish = (jumping ? 1.0f : 0.0f) - (sneaking ? 1.0f : 0.0f);
            }

            // **What the simulation must know whoever owns the keyboard.**
            // Everything above is gated on no screen being open, because keys
            // belong to the panel while it is up - but neither of these is
            // input. They are state the damage rule reads, and a blow does not
            // stop landing because you opened your inventory. `invulnerable`
            // was inside that guard and so lapsed for exactly as long as a
            // creative player looked at their own inventory; `armour` would have
            // inherited the identical bug on its first day.
            //
            // **`armour` is the first of the two missing call sites
            // `Survival.hpp` names by hand**, and until now
            // `PlayerInput::armour` had exactly one reference in the whole tree:
            // `updateSurvival` *reading* it. So every blow was struck against
            // `kNoArmour` and a full set of diamond reduced nothing.
            // `CLAUDE.md` bug shape #15 - a complete feature one call site short
            // of being reachable, and there is no warning for a field nobody
            // writes.
            //
            // **No longer inert, as of 2026-08-19.** The paragraph that used to
            // sit here said the armour slots had no hit test and that
            // `Region::Armour` appeared only as a `case` label, so `armourSet()`
            // could only answer `{0, 0.0f}`. `hud/InventoryScreen.cpp` now
            // produces `SlotHit{Region::Armour, i}` from four cells measured out
            // of the panel art, `Main.cpp` resolves it to `Inventory::armourAt`,
            // and shift-click and right-click both equip - so this line reads a
            // real set and the curve is no longer an identity.
            //
            // **If you arrived here from a sweep reporting `armourDefence` = 0
            // in this file, that zero is right and is not the bug.** `Main.cpp`
            // never names it: the chain is `armourSet()` here ->
            // `Inventory.hpp`'s loop -> `armourDefence`/`armourToughness` in
            // `Item.hpp`. **Sweep for `armourSet` instead**, which read 6 on
            // 2026-08-19 - this line plus five `hurtPlayer` sites. Three agents
            // have now measured the leaf name at the boundary and concluded the
            // feature was inert; `Inventory::armourSet`'s own comment carries
            // the long version and what would falsify it.
            move.armour = inventory.armourSet();
            // **The two leather flags, filled here because `Player.hpp` asks
            // for them "the same frame, the same place and the same reasoning
            // as `armour` directly above" - finding 9670.** Left false they
            // mean "not wearing leather", which is why powder snow had no
            // counterplay at all: the freezing clock, the sink physics and the
            // boots-climb branch were all live and correct, and every one of
            // them read a flag nothing ever wrote. The game showed the player
            // a leather boot that did nothing.
            //
            // **Two flags and not one, because powder snow states two rules
            // against two different sets** - any leather piece stops freezing,
            // but only boots keep you on the surface. A leather cap with iron
            // boots is immune and still falls in. `Player.cpp` ORs them for
            // the freezing test, so the narrower one alone still buys immunity;
            // filling both honestly is cheaper than relying on that.
            //
            // **Material 0 is spelled `armourMaterial(LeatherHelmet)`, not
            // `0`.** Both sides come off the one table that owns the material
            // rows, so a row inserted above leather cannot silently make iron
            // count as leather - the unlinked-literal shape reconciled out of
            // this file an hour ago in `kFallingLevel`.
            //
            // **`isArmour` guards the material test and is not redundant.**
            // `armourMaterial` is `(item - LeatherHelmet) / 4`, and C++
            // truncates a negative quotient toward zero, so any id in the three
            // slots BELOW `LeatherHelmet` also yields 0 and would read as
            // leather. The live UI cannot put one there - `equipArmour` derives
            // the destination through `armourSlot` - but `loadPlayer` trusts
            // `player.dat`, which is the same reason the death drop above is
            // content-agnostic.
            bool leatherWorn = false;
            for (std::size_t i = 0; i < game::kArmourSlots; ++i) {
                const game::ItemStack& worn = inventory.armourAt(i);
                if (!worn.empty() && game::isArmour(worn.item) &&
                    game::armourMaterial(worn.item) ==
                        game::armourMaterial(game::ItemId::LeatherHelmet)) {
                    leatherWorn = true;
                    break;
                }
            }
            move.leatherArmour = leatherWorn;
            // `empty()` before `.item`, the way every other reader of a worn
            // cell in this file and in `Inventory.hpp` does it - a cleared
            // stack is not required to forget which item it held.
            const game::ItemStack& wornBoots = inventory.armour(game::ArmourSlot::Feet);
            move.leatherBoots = !wornBoots.empty() && wornBoots.item == game::ItemId::LeatherBoots;
            move.invulnerable = creative;

            game::updatePlayer(player, move, world, deltaSeconds);
            // **The second of the two, and they had to land together.**
            // `updateSurvival` banks what each worn piece owes into
            // `player.armourWear` rather than applying it, because `world/` has
            // no business reaching into `item/` storage - the project's own
            // "compute the result, then apply it" rule made concrete. Nothing
            // drained it, so it climbed forever and a set never wore out.
            // Taking the damage reduction without the durability cost is half a
            // rule, which is `CLAUDE.md` shape #5, so the pair is one edit.
            //
            // **A point is per piece rather than shared between them**, the
            // reference's rule and the one `wearArmour` implements - this passes
            // the banked total as the per-piece cost on purpose.
            if (player.armourWear > 0) {
                if (inventory.wearArmour(player.armourWear) > 0) {
                    hudDirty = true;
                }
                player.armourWear = 0;
            }
            camera.position = player.renderEyePosition();
            /// **Where reach and targeting start, which is not where the camera
            /// is.** `Player.hpp` says of `stepSmooth` that only the camera may
            /// read it - reach, targeting and knockback all want the true eye -
            /// and every raycast here read `camera.position` anyway, so for the
            /// fifth of a second after each step up the crosshair aimed from up
            /// to 0.6 m below the eye and picked the wrong block. Computed once
            /// per frame so the seven call sites cannot disagree.
            ///
            /// **The trade-off, stated so it does not read as an oversight:**
            /// the world is *drawn* from `camera.position`, so during that same
            /// fifth of a second the ray and the picture disagree and the
            /// crosshair can sit a little off what it selects. That is the
            /// deliberate choice and it is the reference's: reach is a fixed
            /// simulation quantity, and sourcing it from an interpolated
            /// position makes what you can click on breathe sub-frame - the
            /// same class of error as raising the tick rate to smooth motion.
            /// A stable rule that is briefly a pixel out beats a rule that
            /// changes between two frames of standing still.
            ///
            /// `camera.position` remains correct for what the *eye* sees - the
            /// underwater and lava overlays below - and for everything drawn,
            /// heard or ranged against the view.
            const glm::vec3 reachFrom = player.eyePosition();

            // The ears follow the camera. Done here rather than at the top of
            // the frame so a sound started later in the same frame is placed
            // against where the player actually ended up.
            {
                const glm::vec3 facing = camera.forward();
                audio.setListener(camera.position.x, camera.position.y, camera.position.z,
                                  facing.x, facing.z);
            }
            sounds.tickMusic(audio, deltaSeconds);

            // **Footsteps are paced by distance, not by time**, which is what
            // makes a sprint sound like a sprint without a second animation or
            // a second interval. The reference does the same.
            if (player.onGround && !player.flying) {
                const float moved = glm::distance(glm::vec2{player.position.x, player.position.z},
                                                  glm::vec2{lastStepAt.x, lastStepAt.z});
                if (moved >= kStepDistance) {
                    lastStepAt = player.position;
                    const game::BlockId under = world.blockAt(
                        static_cast<int>(std::floor(player.position.x)),
                        static_cast<int>(std::floor(player.position.y - 0.2f)),
                        static_cast<int>(std::floor(player.position.z)));
                    const game::SoundEvent step =
                        game::stepSoundFor(game::soundMaterialFor(under));
                    if (step != game::SoundEvent::Count) {
                        sounds.play(audio, step, player.position, 0.32f);
                    }
                }
            } else if (player.inWater) {
                // **A stroke is a footstep in water**, and it is paced the same
                // way for the same reason - so swimming hard sounds like it,
                // with no second timer and no second constant that can drift
                // away from `kStepDistance`.
                //
                // **Ahead of the `!player.onGround` reset below on purpose.**
                // That branch exists so a landing does not fire one step for the
                // whole distance fallen, and it resets `lastStepAt` every frame
                // you are off the ground - which is every frame you are
                // swimming. Behind it, this could never accumulate a stroke's
                // worth of distance and `Swim` stays silent, which is exactly
                // how it stayed unplayed. The single edit that reintroduces
                // that: move this branch below the reset.
                //
                // The fraction is our judgement, not a number of the
                // reference's: swimming covers ground more slowly than walking,
                // so a shorter interval keeps the *rhythm* about the same.
                constexpr float kStrokeFraction = 0.75f;
                const float moved = glm::distance(glm::vec2{player.position.x, player.position.z},
                                                  glm::vec2{lastStepAt.x, lastStepAt.z});
                // Bobbing on the spot covers no distance at all, so the stroke
                // latch is the other half of the same question. Both reset
                // `lastStepAt`, so the two can never double up.
                if (moved >= kStepDistance * kStrokeFraction || (player.treading && !wasTreading)) {
                    lastStepAt = player.position;
                    sounds.play(audio, game::SoundEvent::Swim, player.position, 0.28f);
                }
            } else if (!player.onGround) {
                // Landing should not fire a step for the whole distance fallen.
                lastStepAt = player.position;
            }

            // Taking damage is something that happens to you, so it is not
            // placed in the world. A long drop gets its own heavier noise,
            // which is the reference's own split and is most of how you know
            // how badly you landed.
            if (player.hurtFlash > 0.0f && !wasHurt) {
                if (lastFallDistance > kBigFallDistance) {
                    sounds.playGlobal(audio, game::SoundEvent::FallBig, 0.8f);
                } else if (lastFallDistance > game::survival::kSafeFallDistance) {
                    sounds.playGlobal(audio, game::SoundEvent::FallSmall, 0.7f);
                } else {
                    sounds.playGlobal(audio, game::SoundEvent::Hurt, 0.7f);
                }
                // Every damage source that is *not* a blow or a landing: lava,
                // fire, drowning, poison, cactus, starvation. The two that are
                // have their own cues on their own edges, because **a blow is
                // an event and damage is only its consequence** - creative
                // takes none, and hanging these here left a golem knocking the
                // player across a field in silence.
                //
                // Sized by the health actually lost, which is the only thing
                // here that knows whether that was a singe or a lava bath.
                game::playRumble(rumble, game::RumbleEvent::Hurt,
                                 game::rumbleStrength(static_cast<float>(lastHealth - player.health),
                                                      1.0f, 10.0f));
                if (settings.particles && lastFallDistance > game::survival::kSafeFallDistance) {
                    const auto feet = glm::ivec3{glm::floor(player.position)};
                    particles.spawnFootstep(player.position,
                                            world.blockAt(feet.x, feet.y - 1, feet.z), 14);
                }
            }
            wasHurt = player.hurtFlash > 0.0f;
            lastHealth = player.health;
            // Trampling. **A probability, not a threshold** - the reference's
            // rule is `fallDistance - 0.5`, so a one-block step ruins tilled
            // ground about half the time and a long drop always does. Sampled
            // on the same edge the fall sound uses, and *before* the landing
            // clears the distance.
            if (player.onGround && !wasOnGround && lastFallDistance > 0.5f) {
                // The landing itself, so it still lands in creative. Scaled
                // across the drop rather than switched on at one height, or the
                // block either side of the threshold feel nothing alike.
                if (lastFallDistance > game::survival::kSafeFallDistance) {
                    game::playRumble(rumble, game::RumbleEvent::HeavyLanding,
                                     game::rumbleStrength(lastFallDistance,
                                                          game::survival::kSafeFallDistance,
                                                          kBigFallDistance));
                }
                const auto feet = glm::ivec3{glm::floor(player.position)};
                const glm::ivec3 under{feet.x, feet.y - 1, feet.z};
                if (game::isFarmland(world.blockAt(under.x, under.y, under.z))) {
                    composterRandom = composterRandom * 1664525u + 1013904223u;
                    const int roll = static_cast<int>((composterRandom >> 16) % 100u);
                    if (roll < game::farming::trampleChancePercent(lastFallDistance)) {
                        // Whatever was growing on it is harvested rather than
                        // deleted, which is the reference's own behaviour.
                        const glm::ivec3 plant{under.x, under.y + 1, under.z};
                        const game::BlockId crop = world.blockAt(plant.x, plant.y, plant.z);
                        if (game::isCropBlock(crop) || game::isStemBlock(crop)) {
                            // Through the drop table, so a trampled crop pays
                            // what its age is worth - a ripe wheat's seeds are
                            // a range and an unripe one is a single seed back.
                            spillBlockDrop(plant, crop, game::BreakContext{});
                            world.setBlock(plant.x, plant.y, plant.z, game::BlockId::Air);
                        }
                        world.setBlock(under.x, under.y, under.z, game::BlockId::Dirt);
                        sounds.play(audio, game::SoundEvent::DigGravel,
                                    glm::vec3{under} + glm::vec3{0.5f}, 0.6f);
                    }
                }
            }
            wasOnGround = player.onGround;
            // Sampled *before* the landing clears it, or every fall reads as
            // zero blocks by the time the damage arrives.
            if (!player.onGround) {
                lastFallDistance = player.fallDistance;
            }

            // Breaking the surface, either way. The reference plays this on
            // entry and on exit and so do we, off the same edge the underwater
            // view already watches.
            //
            // **A belly-flop is louder than a step off a kerb.** `SplashBig` was
            // decoded at startup and never played; a fall that would have hurt
            // on land is the line between the two, which is our judgement rather
            // than a number of the reference's - it is `kSafeFallDistance`, so
            // it moves with the rule it borrows instead of being a second
            // constant that can drift away from it.
            if (player.inWater != wasInWater) {
                const bool hard = !wasInWater &&
                                  lastFallDistance >= game::survival::kSafeFallDistance;
                sounds.play(audio,
                            hard ? game::SoundEvent::SplashBig : game::SoundEvent::Splash,
                            player.position, hard ? 0.8f : 0.5f);
                // **Water puts you out**, and says so. A burning player diving
                // in was silent, which is the one moment you most want to hear.
                if (!wasInWater && player.burningSeconds > 0.0f) {
                    sounds.play(audio, game::SoundEvent::Fizz, player.position, 0.7f);
                }
            }
            wasInWater = player.inWater;

            // One stroke, one sound. Treading is latched across a single stroke
            // by the fluid model, so its rising edge is the stroke itself -
            // **read up in the footstep block**, which is the one owner of the
            // stroke cadence, rather than sounded a second time here. Two
            // triggers for one event is how a swimmer ended up clicking twice
            // per bob; the latch is only *cleared* here.
            wasTreading = player.treading;

            // Two continuing conditions, both of which `Sounds` already owns the
            // cadence and the volume for - so each is one line here and neither
            // needs a timer of its own. Called every frame whether or not the
            // condition holds, which is what `tickAmbient` asks for: a cue that
            // lapses forgets its timer and sounds immediately next time.
            //
            // **`Water` is `global` in the cue table**, so it asks about you
            // rather than about the scenery: it is the muffling of being under,
            // not a river heard from the bank. That is why it reads
            // `player.underwater` and takes no part in the scan below.
            sounds.tickAmbient(audio, game::AmbientCue::Water, player.underwater, player.position,
                               deltaSeconds);
            sounds.tickAmbient(audio, game::AmbientCue::Breath,
                               player.underwater && player.air <= 0.0f, player.position,
                               deltaSeconds);

            // The three scenery ambiences. **One scan feeds all of them**, on
            // its own timer, and `tickAmbient` still sees a fresh answer every
            // frame from the cache - the cue's retrigger interval and this
            // scan's interval are separate clocks on purpose, because the first
            // belongs to the recording's length and the second to how fast a
            // player can walk out of earshot.
            //
            // Nearest rather than first found, so a wall of lava sounds from
            // the side you are standing on. Five cells across is two either
            // way, which is close enough that the positional mix still places
            // it; anything wider and the sound arrives before the light does.
            ambientScanTimer -= deltaSeconds;
            if (ambientScanTimer <= 0.0f) {
                constexpr float kAmbientScanSeconds = 0.25f;
                constexpr int kAmbientReach = 2;
                ambientScanTimer = kAmbientScanSeconds;
                const glm::ivec3 centre{glm::floor(player.eyePosition())};
                // Larger than any squared offset the box can produce, derived
                // from the reach rather than written as a literal, so widening
                // the box cannot quietly leave the sentinel inside it.
                constexpr int kOutside = kAmbientReach * kAmbientReach * 3 + 1;
                int bestFire = kOutside;
                int bestLava = kOutside;
                fireNearby = false;
                lavaNearby = false;
                for (int dy = -kAmbientReach; dy <= kAmbientReach; ++dy) {
                    for (int dz = -kAmbientReach; dz <= kAmbientReach; ++dz) {
                        for (int dx = -kAmbientReach; dx <= kAmbientReach; ++dx) {
                            const game::BlockId id =
                                world.blockAt(centre.x + dx, centre.y + dy, centre.z + dz);
                            const bool fire = id == game::BlockId::Fire;
                            const bool lava = game::isLava(id);
                            if (!fire && !lava) {
                                continue;
                            }
                            const int away = dx * dx + dy * dy + dz * dz;
                            const glm::vec3 at = glm::vec3{centre + glm::ivec3{dx, dy, dz}} +
                                                 glm::vec3{0.5f};
                            // Two independent tests rather than an `else if`.
                            // A cell cannot be both, so the chain would behave
                            // identically - but only after the reader has
                            // proved that, and the proof is the sort of thing
                            // a new block id quietly invalidates.
                            if (fire && away < bestFire) {
                                bestFire = away;
                                fireNearby = true;
                                fireAt = at;
                            }
                            if (lava && away < bestLava) {
                                bestLava = away;
                                lavaNearby = true;
                                lavaAt = at;
                            }
                        }
                    }
                }
            }
            sounds.tickAmbient(audio, game::AmbientCue::Fire, fireNearby, fireAt, deltaSeconds);
            // **Both lava cues read the same fact**, because they are the same
            // pool heard two ways - a steady seethe and the occasional burst.
            // The cue table is what makes them different, and its intervals are
            // the only place that difference is allowed to live.
            sounds.tickAmbient(audio, game::AmbientCue::Lava, lavaNearby, lavaAt, deltaSeconds);
            sounds.tickAmbient(audio, game::AmbientCue::LavaPop, lavaNearby, lavaAt, deltaSeconds);

            // Cave ambience: rare, unplaced, and only when the sky cannot see
            // you. It is the reference's own most effective piece of sound
            // design and costs one light lookup.
            caveTimer -= deltaSeconds;
            if (caveTimer <= 0.0f) {
                caveTimer = kCaveCheckSeconds;
                const int sky = world.skyLightAt(static_cast<int>(std::floor(player.position.x)),
                                                 static_cast<int>(std::floor(player.position.y + 1.0f)),
                                                 static_cast<int>(std::floor(player.position.z)));
                if (sky <= 2 && player.position.y < 40.0f) {
                    sounds.playGlobal(audio, game::SoundEvent::Cave, 0.55f);
                }
            }

            // **Dying takes a moment.** The world keeps running underneath, so
            // the body settles and whatever killed you is still visible; only
            // once `kRespawnSeconds` is up does the world put you back. Anything
            // faster reads as a teleport rather than as a death.
            // Death is a transition and `alive()` is a state. Without the
            // mirror this would fire again on every frame of the death
            // animation, which is the same shape as the trampling bug above.
            if (!player.alive() && wasAlive) {
                game::playRumble(rumble, game::RumbleEvent::Death);
                // **On the edge, not 1.6 s later at the respawn.** The screen
                // was closed at the *end* of the death wait, so dying with a
                // chest open left it up and operable for the whole of
                // `kRespawnSeconds` - a corpse rearranging its inventory while
                // the death rumble played. Closing here also means the drop
                // loop below always finds an empty cursor and a settled bag,
                // which is what its own comment already claimed.
                //
                // Safe to call twice: `closeScreen` is a no-op on a closed
                // screen except for two flags and a `savePending`, and the one
                // below is what guarantees the reconciliation even if a screen
                // is somehow opened during the wait.
                if (openScreen.has_value()) {
                    closeScreen();
                }
            }
            wasAlive = player.alive();

            if (!player.alive()) {
                hudDirty = true;
                if (player.deathSeconds >= game::survival::kRespawnSeconds) {
                    // Everything carried is thrown down where it fell, which is
                    // the reference's rule and the only reason death costs
                    // anything at all.
                    //
                    // **The screen is closed first**, so the cursor stack and
                    // the crafting grid are back in the inventory before the
                    // loop below empties it. The other order kept them: dying
                    // with a screen open and a stack on the cursor dropped
                    // everything else and handed those items straight into the
                    // freshly emptied bag. `forEachScreenStack` claims those
                    // stacks are part of what is carried, and this is the one
                    // place that has to agree with it.
                    closeScreen();
                    if (!creative) {
                        for (std::size_t slot = 0; slot < inventory.size(); ++slot) {
                            game::ItemStack& stack = inventory.slot(slot);
                            if (!stack.empty()) {
                                // **Whole stacks, wear and all.** Dropping
                                // `damage` here made durability optional -
                                // dying handed every tool back fully repaired -
                                // and returned every stowbox empty, with its
                                // contents stranded in `stowboxes.dat` behind a
                                // handle nothing could ever reach again.
                                game::dropStack(drops,
                                                player.position + glm::vec3{0.0f, 0.5f, 0.0f},
                                                stack);
                                stack = game::ItemStack{};
                            }
                        }
                        // **And what was being worn** - finding 9635. The loop
                        // above walks `inventory.size()`, which is
                        // `kInventorySlots` and therefore `m_slots` alone, so a
                        // full diamond set was the one thing dying did not cost.
                        //
                        // **This is a seam, not a missed line, and the seam is
                        // worth naming.** `m_armour` is deliberately a second
                        // array outside `m_slots`, and that decision is right:
                        // it keeps `add`, `hasRoomFor`, `count`, `consume` and
                        // the craft and drop paths from treating a worn helmet
                        // as loose storage. It is right for every loop that
                        // walks the bag except this one, which is the single
                        // loop that MUST see worn pieces - and the exception
                        // never got written. The tell that it was an oversight:
                        // `closeScreen()` above exists precisely so the cursor
                        // and the crafting grid are reconciled before the bag is
                        // emptied, and armour got no equivalent.
                        //
                        // **Cleared as well as dropped, in the same statement
                        // that drops it.** A drop that does not clear is the
                        // duplication shape this file has already paid for; the
                        // slot loop above is written the same way for the same
                        // reason, and the clear is not a second guard somewhere
                        // else that could rot apart from this one.
                        //
                        // **Deliberately content-agnostic.** It drops whatever
                        // sits in the cell rather than asking `isArmour` first.
                        // Nothing in the live tree can put a non-armour stack
                        // there - see the audit in finding 9635 - but
                        // `loadPlayer` trusts `player.dat`, and a corrupt or
                        // hand-edited file can. Asking `isArmour` here would
                        // make the death drop the one place that silently keeps
                        // a junk stack forever.
                        for (std::size_t wornSlot = 0; wornSlot < game::kArmourSlots; ++wornSlot) {
                            game::ItemStack& worn = inventory.armourAt(wornSlot);
                            if (!worn.empty()) {
                                game::dropStack(drops,
                                                player.position + glm::vec3{0.0f, 0.5f, 0.0f},
                                                worn);
                                worn = game::ItemStack{};
                            }
                        }
                    }
                    // A bed that has been slept in wins over the world spawn,
                    // and the reference's rule for losing it has three answers
                    // rather than two.
                    //
                    // **Bedrock: the bed has to be there *and* there has to be
                    // somewhere beside it to stand.** Either way it fails, the
                    // point is cleared and you go to world spawn - it is not
                    // remembered for later and there is no outward search the
                    // way Java does one (minecraft.wiki/w/Bed; MCPE-42892).
                    // This site printed "missing or blocked" while checking
                    // only the first half of that sentence, which is the shape
                    // where a rule exists in one of the two places that need
                    // it.
                    //
                    // **And the third answer, which is the one that costs a
                    // base: an unloaded chunk reads as air.** Dying further
                    // from home than the render distance is ordinary - a cave
                    // trip is - and `blockAt` there answers "no bed", so the
                    // point was cleared and the player sent to spawn by the
                    // streamer rather than by anything they did. That was
                    // survivable only while nothing was written down; now that
                    // the point persists, clearing it is permanent. So an
                    // absent column is **"cannot tell", not "gone"**, guarded
                    // by the same `columnResident` the save path already uses.
                    glm::vec3 respawnAt{static_cast<float>(spawnX) + 0.5f,
                                        static_cast<float>(world.groundHeight(spawnX, spawnZ)),
                                        static_cast<float>(spawnZ) + 0.5f};
                    if (hasRespawnPoint) {
                        const glm::ivec3 bed = respawnPoint;
                        if (!world.columnResident(bed.x, bed.z)) {
                            // Nothing can be checked, so nothing is concluded.
                            // One above the bed is where getting out of one
                            // leaves you, and the streamer brings the ground in
                            // underneath within a frame or two.
                            respawnAt = glm::vec3{static_cast<float>(bed.x) + 0.5f,
                                                  static_cast<float>(bed.y + 1),
                                                  static_cast<float>(bed.z) + 0.5f};
                        } else if (const std::optional<glm::vec3> beside =
                                       game::isBed(world.blockAt(bed.x, bed.y, bed.z))
                                           ? standingRoomBeside(bed)
                                           : std::nullopt;
                                   beside.has_value()) {
                            respawnAt = *beside;
                        } else {
                            hasRespawnPoint = false;
                            engine::logInfo("Your bed was missing or blocked.");
                        }
                    }
                    game::respawnPlayer(player, respawnAt);
                    engine::logInfo("Respawned.");
                }
            }

            // Being underwater has to be visible, not merely felt. Without this
            // the only evidence is the physics changing, which reads as gravity
            // being broken rather than as swimming.
            //
            // **`camera.position` on purpose, and one of the few places it is
            // right.** This asks what the eye can *see*, not where reach starts,
            // so it must follow the drawn camera or the overlay would appear a
            // frame before the water does.
            {
                const game::BlockId eyeIn =
                    world.blockAt(static_cast<int>(std::floor(camera.position.x)),
                                  static_cast<int>(std::floor(camera.position.y)),
                                  static_cast<int>(std::floor(camera.position.z)));
                eyeUnderwater = game::isWater(eyeIn);
                eyeInLava = game::isLava(eyeIn);
            }

            const game::RaycastHit target = game::raycast(world, reachFrom, camera.forward(), kBlockReach);

            // A jukebox, which is neither a screen nor a placement. Asked
            // before the screens below because it takes the interact for
            // itself: right-clicking one with a disc must not open anything.
            if (wantInteract && target.hit &&
                world.blockAt(target.block.x, target.block.y, target.block.z) ==
                    game::BlockId::Jukebox) {
                const auto loaded =
                    std::find_if(jukeboxDiscs.begin(), jukeboxDiscs.end(),
                                 [&](const std::pair<glm::ivec3, game::ItemId>& entry) {
                                     return entry.first == target.block;
                                 });
                if (loaded != jukeboxDiscs.end()) {
                    // It gives the disc back rather than swallowing it, which is
                    // the same rule the drinking path uses for the glass.
                    //
                    // **Through `dropStack`, like every other drop.** A disc
                    // carries no wear and no contents, so `damage` was not being
                    // lost here and this is not a bug fix - it is the claim in
                    // `ItemEntity.hpp` that "every path that parts a player from
                    // an `ItemStack` goes through here" being made true, so the
                    // next person copying a nearby line copies the safe one.
                    game::dropStack(drops, glm::vec3{target.block} + glm::vec3{0.5f, 1.1f, 0.5f},
                                    game::ItemStack{loaded->second, 1});
                    *loaded = jukeboxDiscs.back();
                    jukeboxDiscs.pop_back();
                    sounds.play(audio, game::SoundEvent::Pop,
                                glm::vec3{target.block} + glm::vec3{0.5f}, 0.6f);
                } else if (const game::ItemStack& disc = inventory.slot(selectedSlot);
                           !disc.empty() && game::isMusicDisc(disc.item)) {
                    // **Named divergence: there is no music.** A disc sounds one
                    // note of its own rather than a track, because the music is
                    // the one part of this that has to be written rather than
                    // reimplemented.
                    jukeboxDiscs.emplace_back(target.block, disc.item);
                    sounds.play(audio, game::SoundEvent::Orb,
                                glm::vec3{target.block} + glm::vec3{0.5f}, 1.0f,
                                0.5f + 0.05f * static_cast<float>(game::musicDiscIndex(disc.item)));
                    if (!creative) {
                        inventory.consumeOne(selectedSlot);
                    }
                    hudDirty = true;
                }
                wantInteract = false;
            }

            if (wantInteract && target.hit &&
                game::isInteractive(world.blockAt(target.block.x, target.block.y, target.block.z))) {
                const game::BlockId opened = world.blockAt(target.block.x, target.block.y, target.block.z);
                if (game::isFurnace(opened)) {
                    openFurnacePosition = target.block;
                    // Created on first use rather than when placed, so a furnace
                    // nobody has touched costs nothing. **It can also find a
                    // record older than the block** - see the map's declaration
                    // for the one path a chunk format bump can still strand one
                    // down, and for the two that are now closed.
                    furnaces.try_emplace(openFurnacePosition);
                    openScreen = game::inventoryScreen::Kind::Furnace;
                } else if (game::isChest(opened)) {
                    const std::optional<glm::ivec3> partner = chestPartnerAt(target.block);
                    // The half further back along the join axis always fills the
                    // top three rows, so both halves open the same view.
                    const bool firstIsThis =
                        !partner.has_value() ||
                        target.block.x + target.block.z < partner->x + partner->z;
                    openChestPosition = firstIsThis ? target.block : *partner;
                    openChestPartner = partner.has_value() ? (firstIsThis ? *partner : target.block) : openChestPosition;
                    // **Both halves, and `materialise` rather than
                    // `try_emplace`.** A village chest is a `LootChest` id with
                    // no entry in the map at all until it is opened; this rolls
                    // it, turns it into a plain chest and hands back the same
                    // reference `try_emplace` would have. Asking twice for a
                    // single chest - both names point at the same cell - is
                    // safe, because the second call finds a plain chest.
                    materialise(openChestPosition);
                    materialise(openChestPartner);
                    openScreen = partner.has_value() ? game::inventoryScreen::Kind::DoubleChest
                                                     : game::inventoryScreen::Kind::Chest;
                    sounds.play(audio, game::SoundEvent::ChestOpen,
                                glm::vec3{openChestPosition} + glm::vec3{0.5f}, 0.6f);
                } else if (game::isHopper(opened)) {
                    // Five slots, no partner. It stores them in the very same
                    // block-entity map a chest uses, so the whole of the
                    // container path - clicking, shift-clicking, spilling on
                    // break, saving - already covers it.
                    openChestPosition = target.block;
                    openChestPartner = target.block;
                    chests.try_emplace(openChestPosition);
                    openScreen = game::inventoryScreen::Kind::Hopper;
                } else if (opened == game::BlockId::Stonecutter) {
                    openBenchPosition = target.block;
                    openScreen = game::inventoryScreen::Kind::Stonecutter;
                } else if (opened == game::BlockId::SmithingTable ||
                           opened == game::BlockId::Grindstone || opened == game::BlockId::Anvil ||
                           opened == game::BlockId::ChippedAnvil ||
                           opened == game::BlockId::DamagedAnvil ||
                           opened == game::BlockId::BrewingStand) {
                    // All of them are two inputs and a previewed result, so they
                    // share one screen and differ only in what that result is.
                    openBench = opened;
                    openBenchPosition = target.block;
                    openScreen = game::inventoryScreen::Kind::SmithingTable;
                } else {
                    openBenchPosition = target.block;
                    openScreen = game::inventoryScreen::Kind::CraftingTable;
                }
                window.setCursorCaptured(false);
                hudDirty = true;
            }

            // A gate swings. It shares `wantInteract` with the screens above
            // rather than the placement path below, because opening one is not
            // a use of whatever you happen to be holding - the reference lets
            // you open a gate with a full stack of blocks in hand.
            if (wantInteract && target.hit) {
                const game::BlockId aimed =
                    world.blockAt(target.block.x, target.block.y, target.block.z);
                if (game::isFenceGate(aimed)) {
                    const bool opening = !game::gateIsOpen(aimed);
                    world.setBlock(target.block.x, target.block.y, target.block.z,
                                   game::gateAt(game::gateFamily(aimed), game::gateFacing(aimed),
                                                opening));
                    sounds.play(audio,
                                opening ? game::SoundEvent::DoorOpen : game::SoundEvent::DoorClose,
                                glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                    // **The swing has to consume the click.** `wantInteract` is
                    // edge-triggered - a just-pressed scan - and `wantPlace` is
                    // level, so on the press frame both are true and the
                    // placement branch below ran on the same press: opening your
                    // own gate with a stack in hand built a block beside it.
                    // Doors, trapdoors and beds are protected at the head of that
                    // branch by exactly this line, and screen-opening blocks by
                    // `openScreen`; the gate sits in a third path and reached
                    // neither. minecraft.wiki *Block* - a block that responds to
                    // use takes the interaction unless the player is sneaking,
                    // and sneaking is already handled, because `wantInteract`
                    // tests `!LeftShift`.
                    placeTimer = kPlaceRepeatSeconds;
                } else if (aimed == game::BlockId::Bell) {
                    // A bell has one state and one behaviour: it rings. The
                    // reference's swing is an animated model rather than a
                    // block state, and we have no animated block models, so the
                    // sound is the whole of it.
                    sounds.play(audio, game::SoundEvent::Bell,
                                glm::vec3{target.block} + glm::vec3{0.5f}, 1.0f);
                    // Same defect, same line: ringing a bell with a block in
                    // hand also built one against it.
                    placeTimer = kPlaceRepeatSeconds;
                } else if (game::isCampfire(aimed)) {
                    // **Food goes on a campfire on the same press that swings a
                    // gate, and it is a block interaction rather than a
                    // container.** The reference has no screen here at all: one
                    // item per use, four at a time, thirty seconds each, and no
                    // fuel slot to fill. Sitting in this block gets the two
                    // properties that matter for free - it is edge-triggered, so
                    // holding the button cannot dump a stack onto the fire a
                    // frame at a time, and it is already `!LeftShift`, so
                    // sneaking still places a block against the side.
                    //
                    // **Compute, then apply.** `addToCampfire` is asked of a
                    // copy and the map entry and the player's stack are written
                    // only when it says yes - which is the only thing standing
                    // between a fire with four things already on it and an item
                    // consumed into nowhere. The `find` rather than `operator[]`
                    // is the same care: a refused offer must not leave an empty
                    // entry behind at that cell.
                    const game::ItemStack& hand = inventory.slot(selectedSlot);
                    if (!hand.empty() && game::campfireCooks(hand.item)) {
                        const auto standing = campfires.find(target.block);
                        game::Campfire trial = standing == campfires.end() ? game::Campfire{}
                                                                          : standing->second;
                        if (game::addToCampfire(trial, hand.item)) {
                            campfires[target.block] = trial;
                            if (!creative) {
                                inventory.consumeOne(selectedSlot);
                                hudDirty = true;
                            }
                            sounds.play(audio, game::SoundEvent::WoodClick,
                                        glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        }
                        // Claimed whether or not it fitted, for the same reason
                        // the gate above claims it: the placement path runs on
                        // this very press otherwise.
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }
            }

            // Gated on the cursor state from the start of the frame, so the
            // click that recaptures the cursor does not also swing at a block,
            // and on the screen state *now*, so the click that just opened a
            // table does not also place against it.
            //
            // **The latch is the other half of that**, and it is what the
            // frame-start test alone missed: the button is still down on the
            // *next* frame, by which time the cursor is held and the click that
            // only meant "give this window the mouse back" swings for real.
            if (swallowClickUntilRelease && !window.isMouseButtonDown(engine::MouseButton::Left) &&
                !window.isMouseButtonDown(engine::MouseButton::Right)) {
                swallowClickUntilRelease = false;
            }
            // **And death, which nothing here asked about.** `player.alive()`
            // was read in exactly three places, none of them input: for the
            // 1.6 s of `kRespawnSeconds` a corpse could mine, place, attack,
            // shoot and throw its own inventory on the floor, and any of it
            // that produced an item was then dropped again by the death loop
            // above. The rule that "you cannot act while dead" existed in the
            // death handler's *comment* and nowhere in code - the shape where a
            // rule is correct in one of the two places that need it.
            //
            // Folded into `playing` rather than tested at each of the four
            // consumers, so a fifth cannot be added without it.
            const bool playing = player.alive() && hadCursor && !openScreen.has_value() &&
                                 !swallowClickUntilRelease;
            const bool wantBreak = playing && (window.isMouseButtonDown(engine::MouseButton::Left) ||
                                               pad.down(game::PadButton::RightTrigger));
            const bool wantPlace = playing && (window.isMouseButtonDown(engine::MouseButton::Right) ||
                                               pad.down(game::PadButton::LeftTrigger));
            const bool wantDrop = player.alive() && !openScreen.has_value() &&
                                  (window.isKeyDown(engine::Key::Q) ||
                                   pad.down(game::PadButton::DpadDown));

            if (!wantDrop) {
                dropTimer = 0.0f;
            } else {
                dropTimer -= deltaSeconds;
                if (dropTimer <= 0.0f) {
                    game::ItemStack& held = inventory.slot(selectedSlot);
                    if (!held.empty()) {
                        game::dropStack(drops, camera.position + camera.forward() * 0.5f, held, 1,
                                        camera.forward() * kThrowSpeed, kThrowPickupDelay);
                        inventory.consumeOne(selectedSlot);
                        hudDirty = true;
                    }
                    dropTimer = kDropRepeatSeconds;
                }
            }

            // Timers only run down while the button is held, so releasing and
            // pressing again always acts immediately.
            swingTimer = std::max(0.0f, swingTimer - deltaSeconds);
            breakCooldown = std::max(0.0f, breakCooldown - deltaSeconds);
            pearlCooldown = std::max(0.0f, pearlCooldown - deltaSeconds);

            // Drawing and loosing the bow. **Firing happens on the falling
            // edge**, so this needs the button's previous state rather than the
            // one-shot press list, which reports the wrong end of the gesture.
            {
                const bool drawing = wantPlace && inventory.slot(selectedSlot).item == game::ItemId::Bow &&
                                     (creative || inventory.count(game::ItemId::Arrow) > 0);
                if (drawing) {
                    bowDraw = std::min(bowDraw + deltaSeconds, game::kBowDrawSeconds);
                } else if (bowHeld) {
                    const float charge = game::bowCharge(bowDraw);
                    if (charge >= game::kMinBowCharge) {
                        // Eye height less a tenth, the reference's own anchor
                        // and offset. Spawning at the eye itself puts the shaft
                        // through your own head at point-blank range.
                        //
                        // **`reachFrom`, not `camera.position`.** Where a shot
                        // starts is simulation, and the camera carries
                        // `stepSmooth` - so an arrow loosed in the fifth of a
                        // second after a step up left from as much as 0.6 m
                        // below the eye it was aimed from.
                        const glm::vec3 from = reachFrom - glm::vec3{0.0f, kLaunchDropBelowEye, 0.0f};
                        // Blocks per tick, which is the unit the whole
                        // projectile system is written in.
                        glm::vec3 launch =
                            camera.forward() * (game::projectileInfo(game::ProjectileKind::Arrow).power * charge);
                        // The shooter's own momentum carries into the shot, but
                        // only the vertical part, and only while airborne -
                        // otherwise running along the ground would lift your aim.
                        //
                        // `tick::kSeconds` is the metres-per-second to
                        // blocks-per-tick conversion, and it is the shared one:
                        // this was a bare `1.0f / 20.0f` in both throw sites.
                        launch += player.velocity * game::tick::kSeconds *
                                  glm::vec3{1.0f, player.onGround ? 0.0f : 1.0f, 1.0f};

                        const bool spendsArrows = !creative;
                        // **Named as the player's, and it has to be.** An
                        // unattributed shot exempts the *whole* roster for the
                        // launch window - there is no id to skip, so nothing may
                        // be hit - and at three blocks a tick that is the first
                        // fifteen blocks of flight. Saying who fired it opens the
                        // window on creatures immediately and closes it on the
                        // one entity it is meant for, the thrower.
                        projectiles.spawn(game::ProjectileKind::Arrow, from, launch,
                                          charge >= 1.0f, spendsArrows, game::ItemId::None,
                                          game::Projectiles::kPlayerOwner);
                        // Pitched by the charge, so a snap shot sounds thinner
                        // than a full draw - one number doing two jobs.
                        sounds.playGlobal(audio, game::SoundEvent::Bow, 0.7f, 0.85f + charge * 0.35f);
                        game::playRumble(rumble, game::RumbleEvent::BowLoosed,
                                         game::rumbleStrength(charge, game::kMinBowCharge, 1.0f));
                        if (spendsArrows) {
                            inventory.consume(game::ItemId::Arrow, 1);
                            // **A fourth copy of the wear rule, found by the
                            // sound sweep.** It read `kBowDurability` directly
                            // and zeroed the slot itself, so a bow snapped in
                            // silence while every other tool now says so - the
                            // rule existed and was correct in three places and
                            // did not travel to the fourth. `Mining.hpp` puts
                            // the bow in the tool table on purpose, with
                            // `ToolKind::None` so it gains no mining speed and
                            // no harvest tier, which is exactly what makes
                            // `wearTool` the right owner for it.
                            wearTool(game::ItemId::Bow, 1);
                        }
                        // Releasing must not also place a block against
                        // whatever the shot was aimed at.
                        placeTimer = kPlaceRepeatSeconds;
                    }
                    bowDraw = 0.0f;
                } else {
                    bowDraw = 0.0f;
                }
                if (bowHeld != drawing) {
                    hudDirty = true;
                }
                bowHeld = drawing;
            }

            // A creature in the way takes the swing instead of the block behind
            // it. Asked every frame rather than only when a swing is ready,
            // because the swing's cooldown would otherwise let the block behind
            // a creature be mined straight through it.
            const float armsLength = creative ? kCreativeEntityReach : kEntityReach;
            // **A swing stops at the first thing it cannot pass through.**
            // `findAimed` is told about creatures and nothing else, so without
            // this a villager behind a shut door was exactly as reachable as
            // one standing in the open.
            //
            // Collision geometry rather than opacity, which is the distinction
            // `Raycast.hpp` already draws: you cannot punch through glass even
            // though you can see through it, and you can punch through tall
            // grass even though a creeper cannot see through it.
            const game::SweepHit swingBlocked = game::sweepBlocks(
                world, reachFrom, reachFrom + camera.forward() * armsLength);
            const float entityReach = swingBlocked.hit ? swingBlocked.distance : armsLength;
            const bool creatureInWay =
                wantBreak && creatures.aimedAt(reachFrom, camera.forward(), entityReach);
            if (creatureInWay && swingTimer <= 0.0f) {
                const game::ToolProperties swung = game::toolFor(inventory.slot(selectedSlot).item);
                // **Strength and Weakness get their say here, and this is the
                // only place they could.** `effects::meleeDamage` had zero call
                // sites, so both potions did nothing at all while its twin
                // `miningSpeedScale` was wired twelve lines below - built,
                // clean and never called. Rounded rather than truncated, and
                // floored at 1: Weakness IV would otherwise reduce a fist to a
                // blow that lands for nothing.
                const float base = static_cast<float>(weaponDamage(swung.kind, swung.tier));
                const int damage = std::max(
                    1, static_cast<int>(std::lround(game::effects::meleeDamage(player.effects, base))));
                if (creatures.strike(reachFrom, camera.forward(), entityReach, damage)) {
                    swingTimer = kSwingSeconds;
                    player.exhaustion += game::survival::kExhaustAttack;
                    // **A swing costs durability**, which it did not until now:
                    // only breaking a block ever wore anything out, so a sword
                    // was a permanent item and the whole tool tier ladder was
                    // free for anyone who only fought with it. Bedrock charges
                    // a sword and a hoe one point per hit and every other tool
                    // two (https://minecraft.wiki/w/Durability). A bare fist
                    // has no durability and `wearTool` says so with a zero.
                    wearTool(inventory.slot(selectedSlot).item,
                             (swung.kind == game::ToolKind::Sword ||
                              swung.kind == game::ToolKind::Hoe)
                                 ? 1
                                 : 2);
                    // A bare fist deals 1 and the best weapon in the game deals
                    // `kStrongestBlow`, which is read off the damage table
                    // rather than restated here.
                    game::playRumble(rumble, game::RumbleEvent::HitCreature,
                                     game::rumbleStrength(static_cast<float>(damage), 1.0f,
                                                          static_cast<float>(kStrongestBlow)));
                }
            }

            // Mid-swing at an animal is not mining. Testing only whether one is
            // under the crosshair is not enough: the blow knocks it out of the
            // ray, so the very next frame the block behind it would be fair
            // game. `swingTimer` is only ever set by a landed strike.
            if (!wantBreak || !target.hit || creatureInWay || swingTimer > 0.0f) {
                breakProgress = 0.0f;
                breakingBlock = kNoBlock;
            } else if (breakCooldown <= 0.0f) {
                // Aiming somewhere new abandons the old dig rather than
                // carrying its progress across, which would let you chip at one
                // block and finish a different one.
                if (target.block != breakingBlock) {
                    breakingBlock = target.block;
                    breakProgress = 0.0f;
                }

                const game::BlockId aimed = world.blockAt(target.block.x, target.block.y, target.block.z);
                const game::ItemStack& tool = inventory.slot(selectedSlot);
                // Creative pays no cost for anything, breaking included.
                //
                // **Haste and Mining Fatigue are passed *into* the formula, not
                // applied to its result.** `effects::miningSpeedScale` carries
                // Bedrock's own curves - `(1 + 0.2n) * 1.2^n` for haste and
                // `0.21^n` for fatigue, neither of which is Java's - and returns
                // exactly 1 when neither effect is running, so this is a no-op
                // for a player with no potions.
                //
                // Handing it to `breakSeconds` rather than dividing afterwards
                // matters for two reasons the reference cares about: the scale
                // has to land **before** the round-up to whole ticks, or a
                // hasted break can come out a tick off, and `breaksInstantly`
                // has to see it too, or a block that haste should shatter in
                // one tick still pays the six-tick cooldown.
                //
                // Guarded rather than trusted: fatigue is `0.21^n`, which
                // underflows to zero somewhere past level 30, and a zero scale
                // would turn a slow block into an infinite one.
                const float miningScale =
                    std::max(0.0001f, game::effects::miningSpeedScale(player.effects));
                // **The middle two arguments are facts about the player, not the
                // block.** The reference divides mining speed by five for a
                // submerged head without Aqua Affinity and by five again for
                // feet off the ground, so treading water above a hole is
                // twenty-five times slower than standing in one. `underwater`
                // is the eye alone, which is the same thing the reference asks.
                const float seconds =
                    creative ? 0.0f
                             : game::breakSeconds(aimed, tool.item, player.underwater,
                                                  player.onGround, miningScale);

                breakProgress = seconds <= 0.0f ? 1.0f : breakProgress + deltaSeconds / seconds;

                if (breakProgress >= 1.0f) {
                    breakProgress = 0.0f;
                    breakingBlock = kNoBlock;
                    player.exhaustion += game::survival::kExhaustBreakBlock;
                    {
                        const game::SoundEvent dug = game::digSoundFor(
                            game::soundMaterialFor(world.blockAt(target.block.x, target.block.y,
                                                                 target.block.z)));
                        if (dug != game::SoundEvent::Count) {
                            sounds.play(audio, dug, glm::vec3{target.block} + glm::vec3{0.5f}, 0.8f);
                        }
                        // A tick rather than a thump. This fires several times a
                        // second while mining, and anything heavier here turns
                        // the whole core loop into one long buzz.
                        //
                        // Sized by the block's **own** hardness rather than by
                        // how long the dig took: the tool in your hand changes
                        // the second and not the first, and obsidian should not
                        // feel like dirt because you brought the right pick.
                        game::playRumble(
                            rumble, game::RumbleEvent::BlockBroken,
                            game::rumbleStrength(game::blockHardness(world.blockAt(
                                                     target.block.x, target.block.y, target.block.z)),
                                                 0.2f, 3.0f));
                        if (settings.particles) {
                            particles.spawnBlockBreak(target.block,
                                                      world.blockAt(target.block.x, target.block.y,
                                                                    target.block.z));
                        }
                    }
                    // **The reference's six ticks between blocks, and its own
                    // exemption.** A break that took a single tick or less -
                    // every creative break, and every plant and torch in
                    // survival - skips the delay and is held back only by the
                    // one-tick floor our per-frame loop needs; everything else
                    // pays the full 0.30 s. This was the other way round, so
                    // mining a wall was six ticks faster per block than the
                    // reference and stripping a hedge was four times slower.
                    const bool instantBreak =
                        creative || game::breaksInstantly(aimed, tool.item, player.underwater,
                                                          player.onGround, miningScale);
                    breakCooldown = instantBreak ? kInstantBreakSeconds : kBreakRepeatSeconds;
                    const game::BlockId broken = aimed;
                    // What the player brought to it, in the one struct the drop
                    // table asks about: shears on a cobweb, a shovel on snow, a
                    // pickaxe on an amethyst cluster. **The tier gate is not in
                    // here** - `yieldsDrop` still owns that, below.
                    const game::BreakContext brokenBy = game::breakContextFor(tool.item);
                    // **Before the block goes**, because the pairing is derived
                    // from what is standing there and asking afterwards returns
                    // nothing.
                    const std::optional<glm::ivec3> brokenPartner =
                        game::isChest(broken) ? chestPartnerAt(target.block) : std::nullopt;
                    // Which stored contents the dropped item will carry, or 0
                    // for everything that is not a stowbox with something in it.
                    int brokenStowHandle = 0;
                    // **Rolled before the cell is cleared**, because the table a
                    // village chest owes lives in its block id and nowhere else:
                    // write Air first and the marker is gone, so the chest
                    // spills nothing and the loot is lost with it. The partner
                    // gets the same treatment below, before *its* setBlock.
                    if (game::isLootChest(broken)) {
                        materialise(target.block);
                    }
                    world.setBlock(target.block.x, target.block.y, target.block.z, game::BlockId::Air);

                    // A broken furnace spills what was inside it. Erasing the
                    // entry without this would destroy the contents silently.
                    if (game::isFurnace(broken)) {
                        const auto found = furnaces.find(target.block);
                        if (found != furnaces.end()) {
                            const glm::vec3 centre = glm::vec3{target.block} + glm::vec3{0.5f};
                            for (const game::ItemStack* stack :
                                 {&found->second.input, &found->second.fuel, &found->second.output}) {
                                if (!stack->empty()) {
                                    game::dropStack(drops, centre, *stack);
                                }
                            }
                            furnaces.erase(found);
                        }
                        if (openScreen == game::inventoryScreen::Kind::Furnace &&
                            openFurnacePosition == target.block) {
                            closeScreen();
                        }
                    }

                    // A broken campfire drops what was cooking on it, which is
                    // the reference's rule and needs no tool. It has no screen,
                    // so there is nothing to close.
                    if (game::isCampfire(broken)) {
                        spillCampfire(target.block);
                    }

                    // And a broken jukebox ejects its disc, on the same rule
                    // and through the same kind of single owner.
                    if (broken == game::BlockId::Jukebox) {
                        spillJukeboxDisc(target.block);
                    }

                    // **Every other screen this cell could be showing.** The two
                    // branches below and above spill first and close second,
                    // because a container's contents have to be on the floor
                    // before the screen showing them goes away. Everything else
                    // - the crafting table, the stonecutter, the anvil,
                    // grindstone, brewing stand and smithing table - had no
                    // check at all, so its grid stayed live over a block that
                    // was now Air. `screenLooksAt` is the one owner.
                    if (!game::isChest(broken) && !game::isHopper(broken) &&
                        broken != game::BlockId::Lectern && screenLooksAt(target.block)) {
                        closeScreen();
                    }

                    // Twenty-seven slots, same reasoning.
                    if (game::isChest(broken) || game::isHopper(broken) ||
                        broken == game::BlockId::Lectern) {
                        const auto found = chests.find(target.block);
                        if (found != chests.end()) {
                            const glm::vec3 centre = glm::vec3{target.block} + glm::vec3{0.5f};
                            // A stowbox is the one container whose contents do
                            // not fall out: they move onto the item, which is
                            // the whole point of it.
                            if (game::isStowbox(broken)) {
                                if (!found->second.empty()) {
                                    brokenStowHandle = nextStowHandle++;
                                    stowed.emplace(brokenStowHandle, found->second);
                                }
                            } else {
                                for (const game::ItemStack& stack : found->second.slots) {
                                    if (!stack.empty()) {
                                        game::dropStack(drops, centre, stack);
                                    }
                                }
                            }
                            chests.erase(found);
                        }
                        if (screenLooksAt(target.block)) {
                            closeScreen();
                        }

                        // A joined chest is one container to the player, so
                        // breaking either half takes the whole thing - otherwise
                        // half the storage walks into your inventory and the
                        // other half stays standing with its contents inside.
                        if (brokenPartner) {
                            const glm::ivec3 other = *brokenPartner;
                            const game::BlockId otherBlock =
                                world.blockAt(other.x, other.y, other.z);
                            // Same ordering rule as the cell above: roll it
                            // while the marker is still standing there.
                            if (game::isLootChest(otherBlock)) {
                                materialise(other);
                            }
                            world.setBlock(other.x, other.y, other.z, game::BlockId::Air);
                            const auto pair = chests.find(other);
                            if (pair != chests.end()) {
                                const glm::vec3 centre = glm::vec3{other} + glm::vec3{0.5f};
                                for (const game::ItemStack& stack : pair->second.slots) {
                                    if (!stack.empty()) {
                                        game::dropStack(drops, centre, stack);
                                    }
                                }
                                chests.erase(pair);
                            }
                            if (creative || game::yieldsDrop(otherBlock, tool.item)) {
                                spillBlockDrop(other, otherBlock, brokenBy);
                            }
                            // **The partner cell owes its neighbours exactly
                            // what the mined cell does.** The cell the player
                            // struck is settled below and the blast path settles
                            // both, but this branch wrote Air over the partner
                            // and stopped - so a torch, a plant or a rail
                            // standing on the far half of a double chest was
                            // left hanging over nothing, and a ladder on its
                            // side stayed on air. A rule that did not travel.
                            settleAround(other, otherBlock);
                        }
                    }

                    // Breaking yields its drop in every mode, but only if what
                    // you are holding is good enough for it. Stone mined by hand
                    // gives nothing, which is what makes a pickaxe worth making.
                    //
                    // **The gravel roll that used to sit here has moved into the
                    // table**, where it is two entries sharing one salt rather
                    // than a second independent coin flip - so flint arrives
                    // *instead of* the gravel, which is what the reference does
                    // and what a separate 1-in-10 spawn beside a certain gravel
                    // never did.
                    if (creative || brokenStowHandle != 0 ||
                        game::yieldsDrop(broken, tool.item)) {
                        spillBlockDrop(target.block, broken, brokenBy, brokenStowHandle);
                    }

                    // A door twin, a bed twin, whatever was resting on top and
                    // any ladder hung on its side - all four now live in
                    // `settleAround`, which the blast path calls too.
                    settleAround(target.block, broken);

                    // Tools wear on every break the reference charges for, which
                    // is **every break that took more than a single tick** -
                    // sharper than "the block had some hardness", because a
                    // fast enough tool finishes a hard block inside one tick and
                    // the reference charges nothing for that either.
                    if (!instantBreak) {
                        wearTool(tool.item, 1);
                    }
                }
            }

            if (!wantPlace) {
                placeTimer = 0.0f;
                player.eatingSeconds = 0.0f;
            } else {
                placeTimer -= deltaSeconds;
                const game::ItemStack& held = inventory.slot(selectedSlot);

                // **Eating is held, not clicked**, which is the whole of why it
                // has a timer of its own rather than sharing `placeTimer`: the
                // reference takes 1.6 s and shows the food going down, and a
                // meal you can tap through is not a cost.
                //
                // A full bar refuses, exactly as the reference does - otherwise
                // the first thing anyone does is eat their entire stack.
                //
                // **A potion is drunk on the same gesture and the same timer.**
                // It is not gated on the hunger bar, because a potion of
                // healing is exactly the thing you reach for when nothing else
                // about you is full.
                const bool drinking = !held.empty() && game::isDrinkablePotion(held.item);
                // **Offering food to a campfire is not eating it, and the two
                // are the same button.** The interaction above is edge-triggered
                // and this is level, so without this clause the press puts one
                // steak on the fire and the *hold* that follows quietly eats the
                // rest of the stack - the player never let go, and never meant
                // to eat anything at all.
                //
                // Suppressed whether or not the fire has room, deliberately.
                // Nothing draws what is on a campfire yet, so "full" is
                // invisible from where the player is standing, and eating the
                // one porkchop you meant to cook is a far worse failure than
                // having to look away to eat. Turn your head to eat.
                const game::BlockId aimedAtNow =
                    target.hit ? world.blockAt(target.block.x, target.block.y, target.block.z)
                               : game::BlockId::Air;
                const bool offeringToFire = !held.empty() && game::isCampfire(aimedAtNow) &&
                                            game::campfireCooks(held.item);
                if (drinking || (!held.empty() && !offeringToFire &&
                                 game::survival::isEdible(held.item) &&
                                 player.food < game::survival::kMaxFood)) {
                    player.eatingSeconds += deltaSeconds;
                    // Crumbs, on a spacing rather than every frame - the same
                    // handful whatever the frame rate.
                    constexpr float kCrumbSpacing = 0.11f;
                    if (settings.particles &&
                        std::floor(player.eatingSeconds / kCrumbSpacing) !=
                            std::floor((player.eatingSeconds - deltaSeconds) / kCrumbSpacing)) {
                        const glm::vec3 gaze = camera.forward();
                        particles.spawnEat(camera.position + gaze * 0.35f -
                                               glm::vec3{0.0f, 0.18f, 0.0f},
                                           gaze,
                                           static_cast<float>(game::itemTextureLayer(held.item)), 3);
                    }
                    if (player.eatingSeconds >= game::survival::kEatSeconds) {
                        player.eatingSeconds = 0.0f;
                        if (drinking) {
                            const game::PotionKind brew = game::potionKind(held.item);
                            // The two instant ones land once and are gone, which
                            // is why they cannot go through `apply` - it holds a
                            // timer, and theirs would be zero. `grantEffect`
                            // owns that split for all three sites that need it.
                            grantEffect(brew.effect, brew.amplifier, brew.seconds);
                            // Only the turtle master carries a second effect,
                            // and it is the reason `PotionKind` has room for one.
                            grantEffect(brew.second, brew.secondAmplifier, brew.seconds);
                            if (!creative) {
                                inventory.consumeOne(selectedSlot);
                                // The glass survives. If there is nowhere to put
                                // it, it goes on the floor rather than nowhere.
                                giveOrDrop(game::ItemStack{game::ItemId::GlassBottle, 1},
                                           camera.position + camera.forward() * 0.6f);
                            }
                        } else {
                            // **The food's own effects, from the food's own
                            // table.** `feedPlayer` carries hunger and
                            // saturation and has no channel for anything else,
                            // so this is where a golden apple's Absorption
                            // comes from - `Absorption` was granted by nothing
                            // in the entire game, so `absorptionPoints` was
                            // built, correct and unreachable.
                            //
                            // The pool it fills lives on `Player` and is spent
                            // in `damagePlayer`; the gold hearts that show it
                            // are `hud::AbsorbFull`/`AbsorbHalf` on the row
                            // above the health row, drawn from
                            // `Player::absorption`. An earlier version of this
                            // comment claimed that HUD row already existed. It
                            // did not, for the whole day between the pool
                            // landing and this line being true, and a comment
                            // that says a thing was built is exactly what stops
                            // the next reader looking.
                            //
                            // **Read off the row rather than named here.** The
                            // two apples are not written out at this call site
                            // on purpose: which food grants what belongs to
                            // `foodValue`, and a `case ItemId::GoldenApple`
                            // here would be the food table's knowledge kept
                            // somewhere other than the food table.
                            //
                            // **Stops at the first `None`**, which is the
                            // packing `Survival.hpp` documents and
                            // `foodGrantsAreUsable()` proves - the array is
                            // filled from the front, so a gap would mean a
                            // silently ignored tail.
                            const game::survival::FoodValue meal =
                                game::survival::foodValue(held.item);
                            game::feedPlayer(player, meal);
                            for (const game::survival::EffectGrant& grant : meal.grants) {
                                if (grant.effect == game::effects::Effect::None) {
                                    break;
                                }
                                grantEffect(grant.effect, grant.amplifier, grant.seconds);
                            }
                            if (!creative) {
                                inventory.consumeOne(selectedSlot);
                            }
                        }
                        // **You burp after a meal, not after a potion.** This
                        // played `Burp` for both, and `Drink` - staged, decoded,
                        // never named - is what the other half wanted.
                        sounds.playGlobal(audio,
                                          drinking ? game::SoundEvent::Drink
                                                   : game::SoundEvent::Burp,
                                          0.5f);
                        hudDirty = true;
                    } else if (player.eatingSeconds - deltaSeconds <= 0.0f) {
                        // One bite at the start rather than a chew loop, which
                        // is a whole timer for something nobody would miss.
                        //
                        // **The same split the finish makes.** This played `Eat`
                        // for a potion too, so a swig of anything opened with a
                        // mouthful of bread and closed with a gulp - the pair
                        // has to agree or the drink is half a meal.
                        sounds.playGlobal(audio,
                                          drinking ? game::SoundEvent::Drink
                                                   : game::SoundEvent::Eat,
                                          0.5f);
                    }
                } else {
                    player.eatingSeconds = 0.0f;
                }

                // **Right-clicking a piece of armour puts it on**, which is the
                // route most players reach for before they ever open a screen.
                // Shares `placeTimer` for the same reason the spawn egg below
                // does: without a cooldown one click at 120 fps swaps the piece
                // on and off thirty times and lands wherever the frame ends.
                //
                // **`equipArmour` decides everything.** It derives the
                // destination from `armourSlot`, refuses anything that is not
                // armour, and swaps - so the piece it displaces comes back into
                // the same hotbar slot and a second right-click takes it off
                // again. Nothing here spells a slot number, which is what stops
                // this from becoming a second owner of "which piece goes
                // where".
                //
                // **No `target.hit` and no reach test**, unlike everything else
                // on this button: putting a helmet on is not an interaction
                // with the world, and requiring you to be looking at a block to
                // get dressed would be a rule the reference does not have.
                if (placeTimer <= 0.0f && !held.empty() && game::isArmour(held.item) &&
                    inventory.equipArmour(selectedSlot)) {
                    hudDirty = true;
                    placeTimer = kPlaceRepeatSeconds;
                }

                // A spawn egg is used rather than placed. It shares `placeTimer`
                // deliberately: without a cooldown one click at 120 fps drops
                // seven creatures on the same square, which is the same shape as
                // the instant-dig bug that levelled a row of blocks per click.
                if (placeTimer <= 0.0f && !held.empty() && game::isSpawnEgg(held.item) && target.hit) {
                    const game::CreatureKind kind = game::creatureForSpawnEgg(held.item);
                    const glm::vec3 feet = glm::vec3{target.adjacent} + glm::vec3{0.5f, 0.0f, 0.5f};
                    // Facing whoever released it, and derived rather than taken
                    // from the camera: a creature's yaw is (sin, cos) where the
                    // camera's is (cos, sin) - ninety degrees apart.
                    const glm::vec3 aim = camera.forward();
                    creatures.place(kind, feet, std::atan2(-aim.x, -aim.z));
                    engine::logInfo(std::string{"Spawned a "} + game::speciesInfo(kind).name + ".");
                    if (!creative) {
                        inventory.consumeOne(selectedSlot);
                        hudDirty = true;
                    }
                    placeTimer = kPlaceRepeatSeconds;
                }

                // A glass bottle fills from water the same way a bucket does,
                // and needs the same ray for the same reason. **This is the only
                // way into the whole brewing tree** - every potion in the game
                // starts as a bottle of water, and without it they are creative
                // only.
                if (placeTimer <= 0.0f && !held.empty() &&
                    held.item == game::ItemId::GlassBottle) {
                    const game::RaycastHit reached =
                        game::raycast(world, reachFrom, camera.forward(), kBlockReach, true);
                    // A cauldron with water in it fills one too, and does not
                    // empty itself doing so - the reference takes a level, but
                    // ours has no bottle-sized level to take.
                    const bool fromWater =
                        reached.hit && game::isWaterSource(world.blockAt(
                                           reached.block.x, reached.block.y, reached.block.z));
                    if (fromWater || player.underwater) {
                        if (!creative) {
                            inventory.consumeOne(selectedSlot);
                            giveOrDrop(game::ItemStack{game::ItemId::WaterBottle, 1},
                                       camera.position + camera.forward() * 0.6f);
                        }
                        sounds.play(audio, game::SoundEvent::BucketFill,
                                    glm::vec3{reached.hit ? reached.block
                                                          : glm::ivec3{camera.position}} +
                                        glm::vec3{0.5f},
                                    0.6f);
                        placeTimer = kPlaceRepeatSeconds;
                        hudDirty = true;
                    }
                }

                // A bucket is used rather than placed, and needs its own ray:
                // water has no selection geometry, so the ordinary aim passes
                // straight through it to the riverbed.
                if (placeTimer <= 0.0f && !held.empty() &&
                    (held.item == game::ItemId::Bucket ||
                     held.item == game::ItemId::WaterBucket ||
                     held.item == game::ItemId::LavaBucket ||
                     held.item == game::ItemId::MilkBucket)) {
                    const bool filling = held.item == game::ItemId::Bucket;
                    const game::RaycastHit reached =
                        game::raycast(world, reachFrom, camera.forward(), kBlockReach, filling);

                    bool used = false;
                    game::ItemId became = game::ItemId::Bucket;

                    if (held.item == game::ItemId::MilkBucket) {
                        // Drinking, and **milk clears every effect there is** -
                        // which is the one thing it is for, and the reason it is
                        // worth carrying beside a potion of harming.
                        player.effects.clear();
                        sounds.playGlobal(audio, game::SoundEvent::Drink, 0.5f);
                        used = true;
                    } else if (filling) {
                        // A cow first: an empty bucket aimed at one milks it,
                        // and only falls through to the world if it misses.
                        const std::size_t milked =
                            creatures.findMilkable(reachFrom, camera.forward(), kBlockReach);
                        if (milked != game::Creatures::kNoCreature) {
                            became = game::ItemId::MilkBucket;
                            used = true;
                        } else if (reached.hit) {
                            const game::BlockId source =
                                world.blockAt(reached.block.x, reached.block.y, reached.block.z);
                            // **A source only.** Scooping a flowing cell leaves a
                            // gap its own source refills a moment later, which
                            // reads as the bucket having done nothing.
                            if (game::isWaterSource(source) || game::isLavaSource(source)) {
                                const bool lava = game::isLavaSource(source);
                                became = lava ? game::ItemId::LavaBucket
                                              : game::ItemId::WaterBucket;
                                world.setBlock(reached.block.x, reached.block.y, reached.block.z,
                                               game::BlockId::Air);
                                sounds.play(audio,
                                            lava ? game::SoundEvent::BucketFillLava
                                                 : game::SoundEvent::BucketFill,
                                            glm::vec3{reached.block} + glm::vec3{0.5f}, 0.8f);
                                used = true;
                            }
                        }
                    } else if (reached.hit && !game::playerOverlapsBlock(player, reached.adjacent)) {
                        // Level 0 is a *source*, not merely a full cell. Placing
                        // a flowing level instead would drain itself the moment
                        // the fluid update ran.
                        const bool lava = held.item == game::ItemId::LavaBucket;
                        const game::BlockId doused =
                            world.blockAt(reached.adjacent.x, reached.adjacent.y,
                                          reached.adjacent.z);
                        // **Poured is not different from flowed.** A spreading
                        // flow spills whatever it sweeps aside (see
                        // `takeWashedBlocks` below); a bucket emptied into the
                        // same cell wrote straight over it, and `reached.adjacent`
                        // is `cell + normal`, which for a ray that skimmed over a
                        // rail, a carpet, a redstone line, tripwire or a snow
                        // layer and struck the block beyond points **back into**
                        // the cell holding it. So the one gesture that names a
                        // cell by hand was the one that deleted its occupant.
                        //
                        // The context is the wash path's, for the wash path's
                        // reason: minecraft.wiki *Cobweb* - "drops one piece of
                        // string if broken with a non-Silk Touch sword, **if
                        // water touches or flows over it** ... It drops nothing
                        // when broken using anything else, or if lava flows over
                        // it". Water meets that condition and lava does not, so
                        // water pours a sword-shaped break and lava pours a
                        // bare-handed one - which is the *whole* difference,
                        // because the cobweb rows are the only ones that read it.
                        spillReplaced(reached.adjacent,
                                      lava ? game::BreakContext{}
                                           : game::BreakContext{.tool = game::ToolKind::Sword});
                        world.setBlock(reached.adjacent.x, reached.adjacent.y, reached.adjacent.z,
                                       lava ? game::BlockId::Lava0 : game::BlockId::Water0);
                        sounds.play(audio,
                                    lava ? game::SoundEvent::BucketEmptyLava
                                         : game::SoundEvent::BucketEmpty,
                                    glm::vec3{reached.adjacent} + glm::vec3{0.5f}, 0.8f);
                        // **One of the two met the other**, which is the whole
                        // of what `Fizz` is for and why it was staged. Asked of
                        // what *was* there, because the cell has already been
                        // written by the time anything else could look.
                        if ((lava && game::isWater(doused)) ||
                            (!lava && game::isLava(doused))) {
                            sounds.play(audio, game::SoundEvent::Fizz,
                                        glm::vec3{reached.adjacent} + glm::vec3{0.5f}, 0.9f);
                        }
                        used = true;
                    }

                    if (used) {
                        // Emptying and filling is the *cost* of using a bucket,
                        // which is the one thing creative is allowed to skip.
                        if (!creative) {
                            game::ItemStack& slot = inventory.slot(selectedSlot);
                            if (slot.count > 1) {
                                // A stack of empties gives back one full bucket,
                                // so the swap has to go somewhere else - and if
                                // there is nowhere, on the floor rather than
                                // nowhere. Discarding `add`'s return here poured
                                // a lake and destroyed the bucket that did it.
                                --slot.count;
                                giveOrDrop(game::ItemStack{became, 1},
                                           camera.position + camera.forward() * 0.5f);
                            } else {
                                slot = game::ItemStack{became, 1};
                            }
                            hudDirty = true;
                        }
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }

                // Flint and steel. It sits before block placement for the same
                // reason the bucket does: it is a *use*, and the held item is
                // not a block, so the placement branch would decline it anyway.
                if (placeTimer <= 0.0f && !held.empty() &&
                    held.item == game::ItemId::FlintAndSteel && target.hit) {
                    const game::BlockId aimed =
                        world.blockAt(target.block.x, target.block.y, target.block.z);
                    if (game::isCandle(aimed) && !game::isCandleLit(aimed)) {
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::candleAt(game::candleColour(aimed),
                                                      game::candleCount(aimed), true));
                        sounds.play(audio, game::SoundEvent::Ignite,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.5f);
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (aimed == game::BlockId::Tnt) {                        // Lighting a charge directly rather than setting a fire
                        // beside it, which is the reference's own behaviour.
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::BlockId::TntPrimed);
                        world.primeTnt(target.block);
                        sounds.play(audio, game::SoundEvent::Fuse,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.9f);
                        placeTimer = kPlaceRepeatSeconds;
                    } else {
                        // A plant is replaced rather than built on, so the fire
                        // lands in its cell; anything solid is lit against.
                        const glm::ivec3 lightCell =
                            game::isReplaceable(aimed) ? target.block : target.adjacent;
                        const game::BlockId inCell =
                            world.blockAt(lightCell.x, lightCell.y, lightCell.z);
                        if (!game::playerOverlapsBlock(player, lightCell) &&
                            cellIsFree(inCell) &&
                            world.fireCanSurvive(lightCell.x, lightCell.y, lightCell.z)) {
                            // **`cellIsFree` says the cell may be built into, not
                            // that it is empty.** Tall grass, a fern, a dead
                            // bush and a one-deep snow layer all pass it and all
                            // owe something - seeds, a stick, a snowball - so
                            // striking a fire in a snow layer used to delete the
                            // snowball. Same rule as a placement: a write into
                            // an occupied cell is a break.
                            spillReplaced(lightCell);
                            world.setBlock(lightCell.x, lightCell.y, lightCell.z,
                                           game::BlockId::Fire);
                            sounds.play(audio, game::SoundEvent::Ignite,
                                        glm::vec3{lightCell} + glm::vec3{0.5f}, 0.7f);
                            placeTimer = kPlaceRepeatSeconds;
                        }
                    }
                }

                // Stripping, bottling and throwing. All three are *uses* of a
                // held item rather than placements, so they share the repeat
                // timer and sit ahead of the block branch, which would decline
                // them anyway.
                //
                // **Eating is not among them any more.** It used to be, back
                // when there was no hunger bar and a click simply destroyed the
                // food; M21 gave it a held 1.6 s timer above and this branch was
                // left standing, so every meal was swallowed whole on the first
                // frame and the timer above could never finish. Nothing caught
                // it, because both halves compiled and the survival tests
                // called `feedPlayer` directly rather than through a click.
                // The cauldron. A bucket fills or empties it outright; a bottle
                // takes two of its six levels, which is the reference's own
                // arithmetic and the reason a cauldron holds three bottles.
                if (placeTimer <= 0.0f && target.hit &&
                    game::isCauldron(
                        world.blockAt(target.block.x, target.block.y, target.block.z))) {
                    const game::BlockId tub =
                        world.blockAt(target.block.x, target.block.y, target.block.z);
                    const int level = game::cauldronLevel(tub);
                    const auto swapHeld = [&](game::ItemId became) {
                        if (creative) {
                            return;
                        }
                        game::ItemStack& slot = inventory.slot(selectedSlot);
                        if (slot.count > 1) {
                            --slot.count;
                            // The remainder goes on the floor rather than
                            // nowhere: this is the same discarded `add` return
                            // the bucket path above carried.
                            giveOrDrop(game::ItemStack{became, 1},
                                       glm::vec3{target.block} + glm::vec3{0.5f, 1.1f, 0.5f});
                        } else {
                            slot = game::ItemStack{became, 1};
                        }
                        hudDirty = true;
                    };
                    if (!held.empty() && held.item == game::ItemId::WaterBucket && level < 6) {
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::cauldronAt(6));
                        swapHeld(game::ItemId::Bucket);
                        sounds.play(audio, game::SoundEvent::BucketEmpty,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (!held.empty() && held.item == game::ItemId::Bucket && level == 6) {
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::cauldronAt(0));
                        swapHeld(game::ItemId::WaterBucket);
                        sounds.play(audio, game::SoundEvent::BucketFill,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (!held.empty() && held.item == game::ItemId::GlassBottle &&
                               level >= 2) {
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::cauldronAt(level - 2));
                        swapHeld(game::ItemId::WaterBottle);
                        sounds.play(audio, game::SoundEvent::BucketFill,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.6f);
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (!held.empty() && held.item == game::ItemId::WaterBottle &&
                               level <= 4) {
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::cauldronAt(level + 2));
                        swapHeld(game::ItemId::GlassBottle);
                        sounds.play(audio, game::SoundEvent::BucketEmpty,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.6f);
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }

                // A lectern holds one book. It borrows the chest map's first
                // slot rather than owning a map of its own, so it saves, spills
                // on break and survives a reload with no new machinery.
                if (placeTimer <= 0.0f && target.hit &&
                    world.blockAt(target.block.x, target.block.y, target.block.z) ==
                        game::BlockId::Lectern) {
                    const auto shelved = chests.find(target.block);
                    const bool hasBook =
                        shelved != chests.end() && !shelved->second.slots[0].empty();
                    if (!hasBook && !held.empty() && held.item == game::ItemId::Book) {
                        chests[target.block].slots[0] = game::ItemStack{game::ItemId::Book, 1};
                        if (!creative) {
                            inventory.consumeOne(selectedSlot);
                        }
                        sounds.play(audio, game::SoundEvent::WoodClick,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        hudDirty = true;
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (hasBook) {
                        game::ItemStack& shelf = shelved->second.slots[0];
                        // Whole stack, `damage` and all - the same rule the
                        // screen give-back keeps. **And the remainder goes on
                        // the floor rather than nowhere**: `add` returns what
                        // did not fit, and discarding that return deleted the
                        // book outright when the inventory was full. This was
                        // the last path that parted a player from an
                        // `ItemStack` without going through `dropStack`.
                        const int left = inventory.add(shelf.item, shelf.count, shelf.damage);
                        if (left > 0) {
                            game::dropStack(drops,
                                            glm::vec3{target.block} + glm::vec3{0.5f, 1.1f, 0.5f},
                                            shelf, left);
                        }
                        shelf = game::ItemStack{};
                        sounds.play(audio, game::SoundEvent::WoodClick,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        hudDirty = true;
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }

                // The composter. It sits ahead of the whole use chain because
                // aiming at one has to win over whatever the held item would
                // otherwise do - several compostable things are also food.
                if (placeTimer <= 0.0f && target.hit &&
                    game::isComposter(
                        world.blockAt(target.block.x, target.block.y, target.block.z))) {
                    const game::BlockId tub =
                        world.blockAt(target.block.x, target.block.y, target.block.z);
                    const int level = game::composterLevel(tub);
                    if (level >= game::farming::kComposterReady) {
                        // Level eight is *ready*, not merely full: taking from
                        // it yields exactly one bone meal and empties it. On
                        // the floor rather than nowhere when the bag is full -
                        // this used to discard `add`'s return and destroy it.
                        giveOrDrop(game::ItemStack{game::ItemId::BoneMeal, 1},
                                   glm::vec3{target.block} + glm::vec3{0.5f, 1.1f, 0.5f});
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::composterAt(0));
                        sounds.play(audio, game::SoundEvent::DigGrass,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        hudDirty = true;
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (!held.empty() && game::farming::isCompostable(held.item)) {
                        // Bedrock gives the **first item into an empty tub** a
                        // guaranteed rise; everything after it rolls the
                        // material's own chance.
                        composterRandom = composterRandom * 1664525u + 1013904223u;
                        const int roll = static_cast<int>((composterRandom >> 16) % 100u);
                        const bool rose =
                            level == 0 || roll < game::farming::compostChance(held.item);
                        if (rose) {
                            // **Seven successful additions fill a composter, not
                            // eight.** Layer seven is the last one an *item*
                            // buys: the reference ripens a full tub to the ready
                            // state on its own, twenty ticks later, for free -
                            // "when the composter reaches the 7th layer of
                            // compost and once 20 game ticks (1 second) have
                            // passed, the compost changes appearance indicating
                            // that bone meal can be collected". Raising by one
                            // all the way to eight charged an extra compostable
                            // for every single bone meal, about fourteen percent
                            // over the reference for the whole life of the save.
                            // The wiki's own average-items formula bottoms out
                            // at seven for a hundred-percent item, which is the
                            // arithmetic falsifier: if this ever needs eight
                            // cakes for one bone meal again, it has regressed.
                            //
                            // **The one-second wait is dropped on purpose, and
                            // that is the part to argue with rather than the
                            // seven.** It is not a quantity a player can act on
                            // - the tub is full for its duration so nothing may
                            // be added, and *taking* is the only thing the ready
                            // state unlocks. Honouring it would mean a timer
                            // list that no save file carries, and a quit during
                            // that second would strand a tub at seven, which is
                            // the overcharge back again, rarer and stranger.
                            // **The one reader that could ever tell 7 from 8 is
                            // a comparator** - the reference gives them
                            // different signal strengths - so revisit this if
                            // comparators ever learn to read a composter, and
                            // not before.
                            const int raised = level + 1;
                            world.setBlock(
                                target.block.x, target.block.y, target.block.z,
                                game::composterAt(raised >= game::farming::kComposterReady - 1
                                                      ? game::farming::kComposterReady
                                                      : raised));
                        }
                        sounds.play(audio, game::SoundEvent::DigGrass,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.6f);
                        if (!creative) {
                            inventory.consumeOne(selectedSlot);
                        }
                        hudDirty = true;
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }

                if (placeTimer <= 0.0f && !held.empty()) {
                    const game::ItemId used = held.item;
                    bool consumed = false;

                    // Working soil wears a tool exactly as breaking a block
                    // does. This *said* it was the break path's own rule and
                    // was in fact a second copy of it, six lines long, missing
                    // the snap sound - so it now forwards to the one owner and
                    // stays only as the shorthand its four callers already use.
                    const auto wearHeldTool = [&] { wearTool(used, 1); };

                    // **A second glass-bottle fill used to stand here** and it
                    // disagreed with the one above in three ways: it took any
                    // water rather than a source, it made no sound, and it
                    // discarded `add`'s return so a full bag destroyed the
                    // bottle. It was reachable, too - the branch above declines
                    // flowing water, which is exactly what left this one to
                    // answer. The reference fills a bottle from a **water
                    // source block or a water-filled cauldron only**
                    // (https://minecraft.wiki/w/Water_bottle), so the branch
                    // above is both the correct rule and the only one now.
                    if ((used == game::ItemId::VoidPearl && pearlCooldown <= 0.0f) ||
                        used == game::ItemId::Egg || game::isThrownPotion(used)) {
                        // A thrown entity that arcs, rather than a look-ray
                        // that dropped you where you were already aiming. Same
                        // anchor as the bow - and since 2026-08-19 that is
                        // literally true rather than a claim: both read
                        // `kLaunchDropBelowEye`, where each used to carry its
                        // own copy of the number.
                        const game::ProjectileKind kind =
                            used == game::ItemId::Egg          ? game::ProjectileKind::Egg
                            : game::isSplashPotion(used)       ? game::ProjectileKind::SplashPotion
                            : game::isLingeringPotion(used)    ? game::ProjectileKind::LingeringPotion
                                                               : game::ProjectileKind::Pearl;
                        const glm::vec3 from = reachFrom - glm::vec3{0.0f, kLaunchDropBelowEye, 0.0f};
                        // A potion is lobbed, not thrown flat: `splash_potion.json`
                        // and `lingering_potion.json` publish `angle_offset: -20.0`
                        // while `egg.json` and `ender_pearl.json` publish `0.0`, so
                        // the lift is exactly the `isThrownPotion` pair. Measured
                        // from an eye at 1.52 with a level aim, a throw carried 4.04
                        // blocks before and 5.54 after. See `liftedThrowAim` for the
                        // source and for the rotation rule that was rejected.
                        //
                        // **Before the momentum term below, deliberately** - the lift
                        // belongs to the aim. The bow's launch is built the same way:
                        // aim times power first, the shooter's momentum after it.
                        const glm::vec3 aim = game::isThrownPotion(used)
                                                  ? liftedThrowAim(camera.forward(), -20.0f)
                                                  : camera.forward();
                        glm::vec3 launch = aim * game::projectileInfo(kind).power;
                        // Blocks per tick, and only the vertical part while
                        // airborne - the same rule the bow uses, for the same
                        // reason: running along the ground must not lift a throw.
                        launch += player.velocity * game::tick::kSeconds *
                                  glm::vec3{1.0f, player.onGround ? 0.0f : 1.0f, 1.0f};
                        // The brew rides on the shot, because forty-one of them
                        // share two projectile kinds. So does the owner: none of
                        // these four carries impact damage today, so none of
                        // them reaches the creature test at all - but they leave
                        // the player's eye exactly as the arrow does, and an
                        // unnamed owner is what exempts the *whole* roster for
                        // the launch window rather than only the thrower. Naming
                        // it costs nothing now and is already right the day one
                        // of them is given a damage figure.
                        projectiles.spawn(kind, from, launch, false, false,
                                          game::isThrownPotion(used) ? used : game::ItemId::None,
                                          game::Projectiles::kPlayerOwner);
                        if (kind == game::ProjectileKind::Pearl) {
                            pearlCooldown = kPearlCooldownSeconds;
                        }
                        consumed = true;
                    } else if (game::isGoatHorn(used)) {
                        // The whole of what a horn does. Eight of them, and the
                        // note is the only thing that differs - so one recording
                        // at eight pitches is not a shortcut, it is the design.
                        sounds.play(audio, game::SoundEvent::Orb, camera.position, 1.0f,
                                    game::goatHornPitch(used));
                        consumed = true;
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (game::toolFor(used).kind == game::ToolKind::Axe && target.hit) {
                        const game::BlockId bark =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        /// **An axe works a block three ways and only one of
                        /// them was here.** Stripping was wired up; the other
                        /// two are what makes copper weathering reversible, and
                        /// without them `Copper.hpp` shipped a `scraped()` and
                        /// an `unwaxedForm()` that were written, asserted and
                        /// called by nothing at all - so oxidation was one-way
                        /// and a waxed block could never be unwaxed. Wiring a
                        /// table up is not a detail: a feature one call site
                        /// short of reachable builds clean, validates clean and
                        /// does nothing.
                        ///
                        /// **Wax first, then a layer**, which is the reference's
                        /// own wording: an axe "removes the wax if it has any,
                        /// or otherwise removes a level of oxidization"
                        /// (https://minecraft.wiki/w/Copper_Axe). Removing wax
                        /// leaves the stage alone, so a waxed weathered block
                        /// becomes a bare weathered block rather than jumping a
                        /// stage. The two are exclusive anyway - `scraped`
                        /// answers `Air` for anything waxed - so the order is
                        /// the reference's rule stated rather than relied on.
                        ///
                        /// One durability per action, exactly as stripping
                        /// costs one, and the same for every axe tier.
                        const auto workWithAxe = [&](game::BlockId into) {
                            world.setBlock(target.block.x, target.block.y, target.block.z, into);
                            // **The one world-working branch in this chain that
                            // made no sound at all**, which reads as an input
                            // that half-registered - the hoe, the shovel and the
                            // planting branches each play one right beside their
                            // own `setBlock`. Bedrock's strip action is the wood
                            // use sound
                            // (https://minecraft.wiki/w/Template:Sound_table/Block/Wood/BE),
                            // and this asks the material table for it rather
                            // than naming a recording, so a stripped log that
                            // ever changes material takes its sound with it -
                            // and copper, which has no scrape recording here,
                            // gets its own material's answer for free.
                            const game::SoundEvent worked =
                                game::digSoundFor(game::soundMaterialFor(into));
                            if (worked != game::SoundEvent::Count) {
                                sounds.play(audio, worked,
                                            glm::vec3{target.block} + glm::vec3{0.5f}, 0.6f);
                            }
                            // **Working a block wears the axe.** Stripping was
                            // the one world-working branch in this chain that
                            // did not - the hoe, the shovel and the shears all
                            // do - so an axe used only for stripping never wore
                            // out at all.
                            wearHeldTool();
                            placeTimer = kPlaceRepeatSeconds;
                        };
                        if (const game::BlockId stripped = game::strippedFor(bark);
                            stripped != bark) {
                            workWithAxe(stripped);
                        } else if (const game::BlockId unwaxed = game::copper::unwaxedForm(bark);
                                   unwaxed != game::BlockId::Air) {
                            workWithAxe(unwaxed);
                        } else if (const game::BlockId scrapedBack = game::copper::scraped(bark);
                                   scrapedBack != game::BlockId::Air) {
                            workWithAxe(scrapedBack);
                        }
                    } else if (used == game::ItemId::Honeycomb && target.hit) {
                        // The other half of the same feature, and the only thing
                        // honeycomb does to a block. `waxedForm` answers `Air`
                        // for anything already waxed and for every copper shape
                        // whose waxed id this project does not have yet - the
                        // bulbs, the cut stairs and slabs - so a player waxing
                        // one of those gets nothing rather than a wrong block,
                        // and keeps the honeycomb.
                        const game::BlockId bare =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        if (const game::BlockId waxed = game::copper::waxedForm(bare);
                            waxed != game::BlockId::Air) {
                            world.setBlock(target.block.x, target.block.y, target.block.z, waxed);
                            const game::SoundEvent waxOn =
                                game::digSoundFor(game::soundMaterialFor(waxed));
                            if (waxOn != game::SoundEvent::Count) {
                                sounds.play(audio, waxOn,
                                            glm::vec3{target.block} + glm::vec3{0.5f}, 0.6f);
                            }
                            // The honeycomb goes, and no tool wears: it is not
                            // one. `consumed` is the chain's own owner of both
                            // halves of that, creative included.
                            consumed = true;
                        }
                    } else if (game::toolFor(used).kind == game::ToolKind::Hoe && target.hit) {
                        // Tilling. Only the **top** face may be worked and only
                        // with air above it, which is the reference's rule and
                        // the reason you cannot hoe the underside of an
                        // overhang into a field.
                        //
                        // **Air, and nothing else, which is what the line above
                        // has always claimed.** This used to also accept
                        // anything `isWashedAway` - and it is the one of these
                        // sites where neither that predicate nor
                        // `isReplaceable` is the right answer, because nothing
                        // is being *placed* here. The cell above is not
                        // consumed, so whatever stands in it is left hovering
                        // over the new farmland: before today that was a torch
                        // or a tuft of grass, after the widening it was a rail
                        // line. Refusing is both the reference's behaviour and
                        // the only one with no wrong-looking result - break the
                        // grass first, exactly as you would there.
                        const game::BlockId soil =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        const game::BlockId tilled = game::farming::tilledFrom(soil);
                        const game::BlockId above = world.blockAt(
                            target.block.x, target.block.y + 1, target.block.z);
                        if (tilled != soil && above == game::BlockId::Air) {
                            world.setBlock(target.block.x, target.block.y, target.block.z, tilled);
                            sounds.play(audio, game::SoundEvent::DigGravel,
                                        glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                            wearHeldTool();
                            placeTimer = kPlaceRepeatSeconds;
                        }
                    } else if (game::toolFor(used).kind == game::ToolKind::Shovel && target.hit) {
                        // The same rule as the hoe above, and the same reason:
                        // the path replaces the *soil*, not the cell over it, so
                        // anything standing there would be left hovering.
                        const game::BlockId soil =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        const game::BlockId path = game::farming::pathFrom(soil);
                        const game::BlockId above = world.blockAt(
                            target.block.x, target.block.y + 1, target.block.z);
                        if (path != soil && above == game::BlockId::Air) {
                            world.setBlock(target.block.x, target.block.y, target.block.z, path);
                            sounds.play(audio, game::SoundEvent::DigGravel,
                                        glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                            wearHeldTool();
                            placeTimer = kPlaceRepeatSeconds;
                        }
                    } else if (target.hit && game::farming::cropForSeed(used) != game::BlockId::Air) {
                        // Sowing. A seed goes **on top of** what you aimed at,
                        // never into it, so the ground test and the cell test
                        // are two different blocks.
                        const game::BlockId ground =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        const glm::ivec3 cell = target.block + glm::ivec3{0, 1, 0};
                        const game::BlockId inCell = world.blockAt(cell.x, cell.y, cell.z);
                        if (game::farming::canSowOn(used, ground) && cellIsFree(inCell)) {
                            // Sowing over ground cover is a break too - the same
                            // three lines the fire branch above needed, and for
                            // the same reason: `cellIsFree` admits tall grass, a
                            // fern, a dead bush and one-deep snow, every one of
                            // which owes an item.
                            spillReplaced(cell);
                            world.setBlock(cell.x, cell.y, cell.z,
                                           game::farming::cropForSeed(used));
                            sounds.play(audio, game::SoundEvent::DigGrass,
                                        glm::vec3{cell} + glm::vec3{0.5f}, 0.6f);
                            consumed = true;
                        }
                    } else if (target.hit && used == game::ItemId::NetherWart) {
                        const game::BlockId ground =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        const glm::ivec3 cell = target.block + glm::ivec3{0, 1, 0};
                        const game::BlockId inCell = world.blockAt(cell.x, cell.y, cell.z);
                        if (game::farming::canSowOn(used, ground) && cellIsFree(inCell)) {
                            spillReplaced(cell);
                            world.setBlock(cell.x, cell.y, cell.z, game::BlockId::NetherWart0);
                            sounds.play(audio, game::SoundEvent::DigGrass,
                                        glm::vec3{cell} + glm::vec3{0.5f}, 0.6f);
                            consumed = true;
                        }
                    } else if (target.hit && used == game::ItemId::BoneMeal) {
                        // Bone meal is a random tick you asked for, so it goes
                        // through the same growth rule the sampler uses rather
                        // than a second one that could disagree with it.
                        if (world.applyBoneMeal(target.block)) {
                            sounds.play(audio, game::SoundEvent::DigGrass,
                                        glm::vec3{target.block} + glm::vec3{0.5f}, 0.6f);
                            consumed = true;
                        }
                    } else if (target.hit && used == game::ItemId::Shears &&
                               world.blockAt(target.block.x, target.block.y, target.block.z) ==
                                   game::BlockId::Pumpkin) {
                        // Carving. The face turns toward whoever cut it, which
                        // is the reference's own rule for a side cut.
                        const game::FaceDirection facing =
                            game::facingToward(camera.forward().x, camera.forward().z);
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       static_cast<game::BlockId>(
                                           static_cast<int>(game::BlockId::CarvedPumpkinFirst) +
                                           static_cast<int>(facing)));
                        // **One seed, not four.** Bedrock yields a single
                        // pumpkin seed from a carve; four is Java's number
                        // (https://minecraft.wiki/w/Pumpkin_Seeds), and the
                        // reference for this project is Bedrock. Do not
                        // "correct" this to 4.
                        //
                        // Through `dropStack` rather than `drops.spawn`, which
                        // is what it was: `spawn` takes `damage` last and
                        // defaulted, and `ItemEntity.hpp` says in as many words
                        // to prefer `dropStack` for anything that is a stack.
                        // A seed carries no wear so nothing was lost here - but
                        // this is the branch the honeycomb one below was copied
                        // from, and a template that quietly drops `damage` is
                        // how the other eleven sites happened.
                        game::dropStack(drops, glm::vec3{target.block} + glm::vec3{0.5f, 1.0f, 0.5f},
                                        game::ItemStack{game::ItemId::PumpkinSeeds, 1});
                        sounds.play(audio, game::SoundEvent::DigGrass,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        wearHeldTool();
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (target.hit && used == game::ItemId::Shears &&
                               game::beehiveHasHoney(world.blockAt(target.block.x, target.block.y,
                                                                   target.block.z))) {
                        // **The only source of honeycomb in the game**, and
                        // without it twenty-eight recipes downstream of it were
                        // unreachable - the item existed, the recipes existed,
                        // and nothing could ever put one in your hand.
                        //
                        // **Three, every time**, not a roll: Bedrock's shear of
                        // a full hive or nest yields exactly 3
                        // (https://minecraft.wiki/w/Honeycomb).
                        const game::BlockId hive =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        // The hive empties and **keeps two things about
                        // itself, not one**: which way it faces, and whether it
                        // was found or made.
                        //
                        // The facing half was always here - assigning a bare
                        // `BlockId::Beehive` would spin every sheared hive
                        // round to face north. **The kind half was missing
                        // until 2026-08-19 and is finding 9618**: this read
                        // `beehiveAt(beehiveFacing(hive), false)`, and a bare
                        // `FaceDirection` carries no record of which half of
                        // the family it came from, so shearing a natural nest
                        // rebuilt it out of the crafted run. Same facing, same
                        // look at a glance, but `isBeeNest` was now false, so
                        // it dropped a Beehive item when mined instead of
                        // nothing and the world had quietly lost the fact that
                        // it was found rather than built.
                        //
                        // `beeHomeAtLevel` is the one owner of that choice, and
                        // `Block.hpp` keeps the old call beside it as
                        // `beeHomeAtLevelForgettingKind` under a `static_assert`
                        // that **rejects** it - so the codebase already knew
                        // this was wrong before anyone sheared a nest.
                        //
                        // **Crafted hives are bit-identical across this change**
                        // - `beeHomeAtLevel(hive, 0)` on a non-nest is exactly
                        // `beehiveAtLevel(beehiveFacing(hive), 0)`, which is
                        // what `beehiveAt(..., false)` expanded to. Only the
                        // nest arm moves, which is what makes it safe.
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::beeHomeAtLevel(hive, 0));
                        game::dropStack(drops, glm::vec3{target.block} + glm::vec3{0.5f, 1.0f, 0.5f},
                                        game::ItemStack{game::ItemId::Honeycomb, 3});
                        // **And now the bees answer for it**, which is the whole
                        // reason a honey farm is built around a campfire rather
                        // than being a hole in the ground with a hive in it.
                        //
                        // Bedrock's rule, quoted rather than remembered:
                        // harvesting angers them "unless there is a fire
                        // directly beneath it or a lit campfire within five
                        // blocks below, and the smoke is not obstructed"
                        // (https://minecraft.wiki/w/Beehive). **Bedrock is the
                        // strict reading of that last clause** - Java lets smoke
                        // through one solid block and through a carpet, Bedrock
                        // counts a carpet as an obstruction - so the column has
                        // to be genuinely empty, and a hive sitting flat on the
                        // ground never qualifies. That is not us being harsh: it
                        // is why the reference's own advice is to sink the
                        // campfire into a hole beneath the hive.
                        //
                        // **Every campfire in this game is lit**, so the "lit"
                        // half is free rather than skipped - there is one
                        // `BlockId::Campfire`, it returns `kMaxLight`, and
                        // nothing can put one out. If an unlit state is ever
                        // added, this is a caller that has to hear about it.
                        constexpr int kCampfireSmokeReach = 5;
                        const auto smokeReachesHive = [&] {
                            for (int drop = 1; drop <= kCampfireSmokeReach; ++drop) {
                                const game::BlockId under = world.blockAt(
                                    target.block.x, target.block.y - drop, target.block.z);
                                if (under == game::BlockId::Campfire ||
                                    under == game::BlockId::SoulCampfire) {
                                    return true;
                                }
                                // Fire counts **only directly beneath**, which is
                                // the reference's wording and not a shortcut; it
                                // is tested before the obstruction rule because
                                // fire is not air and would otherwise stop the
                                // scan one block short of saying yes.
                                if (drop == 1 && under == game::BlockId::Fire) {
                                    return true;
                                }
                                if (under != game::BlockId::Air) {
                                    return false;
                                }
                            }
                            return false;
                        };
                        if (!smokeReachesHive()) {
                            // **The radius is the bee's own `alertRange`, read
                            // off the species row rather than typed here.** That
                            // field's comment is "how far striking one rouses its
                            // own kind", which is this exact question, and the
                            // bee's 20 is generous on purpose. Naming a distance
                            // at this call site would make it a second owner of a
                            // number the table already holds - and the two would
                            // then be free to disagree, which is how a rule ends
                            // up correct in only one of the places that need it.
                            //
                            // The reference angers the bees *inside* the hive,
                            // and a hive here stores a facing and a honey flag
                            // and no occupants at all, so "the bees around it" is
                            // the nearest thing that can be asked. `threatId`
                            // defaults to the player, which is who sheared it.
                            creatures.provokeNear(
                                glm::vec3{target.block} + glm::vec3{0.5f},
                                game::speciesInfo(game::CreatureKind::Bee).alertRange,
                                game::CreatureKind::Bee);
                        }
                        // **Breaking a hive still angers nobody**, and that one
                        // stays a gap rather than becoming a second copy of the
                        // rule above: the reference spares you only with Silk
                        // Touch, there are no enchantments in this tree, so the
                        // honest choice is either "every hive you mine stings
                        // you" or nothing. It belongs with enchantments.

                        // No shear recording exists, and inventing a
                        // `SoundEvent` belongs to `Sounds.hpp`'s owner. The
                        // carve above uses `DigGrass` for the same tool on the
                        // same kind of material, so this matches it rather than
                        // being silent.
                        sounds.play(audio, game::SoundEvent::DigGrass,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        wearHeldTool();
                        placeTimer = kPlaceRepeatSeconds;
                    }

                    if (consumed) {
                        if (!creative) {
                            inventory.consumeOne(selectedSlot);
                            hudDirty = true;
                        }
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }

                // A door or trapdoor already standing there swings rather than
                // being built on. **Before the placement branch**, which would
                // otherwise try to put a second one against it.
                //
                // **`wantInteract`, not the held button** - landed 2026-08-19
                // against finding 154, and it is two bugs in one token.
                //
                // Everything in here is a *toggle*, and a toggle on a level
                // signal repeats. `wantPlace` is `isMouseButtonDown` and the
                // only brake was `placeTimer`, so holding right-click on a door
                // flapped it open and shut 5.5 times a second for as long as the
                // button was down, and holding it on a bed at night re-ran the
                // sleep every 0.18 s - resetting `timeOfDay` to dawn and
                // printing "Slept. Good morning." over and over. A lever, a
                // button, a repeater, a comparator, a daylight detector and a
                // note block all did the same. `wantInteract` is the just-
                // pressed scan the gate, the bell, the jukebox and every screen
                // already use, which is the point: this is the same gesture on
                // the same kind of block and it was on the other input signal.
                //
                // **And it carries the sneak rule with it**, which is the second
                // half. `wantInteract` tests `!LeftShift` (and `!B` on the pad),
                // so sneaking now falls through to the placement branch below -
                // which is how the reference lets you build against a door, a
                // lever or a bed instead of using it (minecraft.wiki *Block*: a
                // block that responds to use takes the interaction unless the
                // player is sneaking). Before this, sneaking flapped the door
                // and the block never went down.
                //
                // **`placeTimer <= 0.0f` stays and is not redundant.** It is
                // what an earlier branch in the same frame sets to consume the
                // click - the cauldron, the lectern, the composter and the whole
                // use chain above all do it - so dropping it would let one press
                // both fill a cauldron and swing whatever is behind it.
                if (placeTimer <= 0.0f && wantInteract && target.hit) {
                    const game::BlockId hit =
                        world.blockAt(target.block.x, target.block.y, target.block.z);
                    if (game::isDoor(hit)) {
                        // Both halves swing together, so the other one is found
                        // from this one's own half rather than searched for.
                        const int step = game::doorIsUpper(hit) ? -1 : 1;
                        const glm::ivec3 other{target.block.x, target.block.y + step,
                                               target.block.z};
                        const game::BlockId twin = world.blockAt(other.x, other.y, other.z);
                        const bool open = !game::doorOpen(hit);
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::doorAt(game::doorFamily(hit), game::doorFacing(hit),
                                                    game::doorHingeRight(hit), open,
                                                    game::doorIsUpper(hit)));
                        if (game::isDoor(twin) && game::doorFamily(twin) == game::doorFamily(hit)) {
                            world.setBlock(other.x, other.y, other.z,
                                           game::doorAt(game::doorFamily(twin),
                                                        game::doorFacing(twin),
                                                        game::doorHingeRight(twin), open,
                                                        game::doorIsUpper(twin)));
                        }
                        sounds.play(audio, game::SoundEvent::DigWood,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (game::isTrapdoor(hit)) {
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::trapdoorAt(game::trapdoorFamily(hit),
                                                        game::trapdoorFacing(hit),
                                                        !game::trapdoorOpen(hit),
                                                        game::trapdoorIsTop(hit)));
                        sounds.play(audio, game::SoundEvent::DigWood,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (game::isBed(hit)) {
                        // Sleeping. **The respawn point is set whatever the
                        // hour** - the reference sets it on use, day or night -
                        // and only the skip to dawn needs it to be dark.
                        respawnPoint = target.block;
                        hasRespawnPoint = true;
                        const bool dark = game::sky::sunDirection(timeOfDay).y < -0.05f;
                        if (dark) {
                            // Dawn, in the same units the day cycle runs in.
                            timeOfDay = 0.0f;
                            engine::logInfo("Slept. Good morning.");
                        } else {
                            engine::logInfo("Respawn point set.");
                        }
                        sounds.play(audio, game::SoundEvent::DigCloth,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (game::isLever(hit) || game::isButton(hit) ||
                               game::isRepeater(hit) || game::isComparator(hit) ||
                               game::isDaylightDetector(hit) || game::isNoteBlock(hit)) {
                        // ---- The hand toggles. ----
                        //
                        // `DECISIONS.md`, *No redstone signal engine*: the
                        // forty-one components place, break, craft, drop, get
                        // measured against their own reference models **and
                        // toggle by hand**. That last clause was the one thing
                        // never built - every construction site in the placement
                        // branch above passes the off state and nothing ever
                        // wrote the other one, so a lever could be put down and
                        // never thrown. Worse than inert: with a block in hand
                        // the click fell through to the placement branch and
                        // built a cobblestone against your own lever.
                        //
                        // **None of these carries a signal anywhere**, and that
                        // is the decision rather than a gap. What a component
                        // owes by hand is its own state and its own sound, and
                        // that is the whole of what is here.
                        //
                        // Beside the door, the trapdoor and the bed because they
                        // are the same gesture on the same timer - the comment
                        // at the head of this block is the reason, and it did
                        // not travel any further than those three.
                        const glm::vec3 centre = glm::vec3{target.block} + glm::vec3{0.5f};
                        if (game::isLever(hit)) {
                            // https://minecraft.wiki/w/Lever - a lever flips and
                            // stays where it was put.
                            world.setBlock(target.block.x, target.block.y, target.block.z,
                                           game::leverAt(game::leverMount(hit),
                                                         !game::leverOn(hit)));
                            sounds.play(audio, game::SoundEvent::Click, centre, 0.6f);
                        } else if (game::isButton(hit)) {
                            // A button springs back on its own, which is why it
                            // is the one toggle that has to be remembered.
                            world.setBlock(target.block.x, target.block.y, target.block.z,
                                           game::buttonAt(game::buttonFamily(hit),
                                                          game::buttonMount(hit), true));
                            // **Refresh the entry rather than adding a second
                            // one.** Two entries for one cell means the older
                            // one expires first and pops a button that was just
                            // pressed, so re-pressing shortened the hold instead
                            // of renewing it.
                            const float holdSeconds = buttonHeldSeconds(game::buttonFamily(hit));
                            const auto pressed =
                                std::find_if(heldButtons.begin(), heldButtons.end(),
                                             [&](const std::pair<glm::ivec3, float>& entry) {
                                                 return entry.first == target.block;
                                             });
                            if (pressed != heldButtons.end()) {
                                pressed->second = holdSeconds;
                            } else {
                                heldButtons.emplace_back(target.block, holdSeconds);
                            }
                            sounds.play(audio, game::SoundEvent::WoodClick, centre, 0.6f);
                        } else if (game::isRepeater(hit)) {
                            // Four steps, wrapping - one to four ticks of delay
                            // (https://minecraft.wiki/w/Redstone_Repeater).
                            // `repeaterDelay` hands back what the block shows,
                            // so the wrap is on that and not on the stored value.
                            world.setBlock(target.block.x, target.block.y, target.block.z,
                                           game::repeaterAt(game::repeaterFacing(hit),
                                                            game::repeaterDelay(hit) % 4 + 1,
                                                            game::repeaterPowered(hit),
                                                            game::repeaterLocked(hit)));
                            sounds.play(audio, game::SoundEvent::WoodClick, centre, 0.5f);
                        } else if (game::isComparator(hit)) {
                            // Compare against subtract
                            // (https://minecraft.wiki/w/Redstone_Comparator).
                            world.setBlock(target.block.x, target.block.y, target.block.z,
                                           game::comparatorAt(game::comparatorFacing(hit),
                                                              game::comparatorPowered(hit),
                                                              !game::comparatorSubtracts(hit)));
                            sounds.play(audio, game::SoundEvent::WoodClick, centre, 0.5f);
                        } else if (game::isDaylightDetector(hit)) {
                            // Day against night
                            // (https://minecraft.wiki/w/Daylight_Detector). The
                            // signal it is showing is left alone - inverting is
                            // a change of question, and whatever reads the sky
                            // answers it again on its own.
                            world.setBlock(
                                target.block.x, target.block.y, target.block.z,
                                game::daylightDetectorAt(game::daylightDetectorSignal(hit),
                                                         !game::daylightDetectorInverted(hit)));
                            sounds.play(audio, game::SoundEvent::WoodClick, centre, 0.5f);
                        } else {
                            // One semitone up and it sounds, wrapping after
                            // twenty-five (https://minecraft.wiki/w/Note_Block).
                            // The pitch is the reference's own formula - two to
                            // the semitone over twelve, centred on the middle of
                            // the range - rather than a ramp picked to sound
                            // about right, so the two octaves land on real
                            // intervals.
                            const int tuned = game::noteBlockPitch(hit) + 1;
                            world.setBlock(target.block.x, target.block.y, target.block.z,
                                           game::noteBlockAt(tuned));
                            const int semitone = game::noteBlockPitch(game::noteBlockAt(tuned));
                            sounds.play(audio, game::SoundEvent::Orb, centre, 0.8f,
                                        std::pow(2.0f, static_cast<float>(semitone - 12) / 12.0f));
                        }
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }

                const bool canPlace = !held.empty() && game::isBlockItem(held.item);
                // Adding a candle to a cell that already holds one of the same
                // colour raises the stack rather than refusing the placement,
                // which is the whole of how you get to four.
                if (placeTimer <= 0.0f && canPlace && target.hit) {
                    const game::BlockId holding = game::blockForItem(held.item);
                    const game::BlockId standing =
                        world.blockAt(target.block.x, target.block.y, target.block.z);
                    if (game::isCandle(holding) && game::isCandle(standing) &&
                        game::candleColour(holding) == game::candleColour(standing) &&
                        game::candleCount(standing) < 4) {
                        world.setBlock(target.block.x, target.block.y, target.block.z,
                                       game::candleAt(game::candleColour(standing),
                                                      game::candleCount(standing) + 1,
                                                      game::isCandleLit(standing)));
                        sounds.play(audio, game::SoundEvent::DigWood,
                                    glm::vec3{target.block} + glm::vec3{0.5f}, 0.5f);
                        if (!creative) {
                            inventory.consumeOne(selectedSlot);
                            hudDirty = true;
                        }
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }
                // A lily pad is set down **on** the water, so its aim stops at
                // the surface every other block's ray goes straight through,
                // and it lands in the cell above rather than in it.
                const bool floating = canPlace && game::restsOnWater(game::blockForItem(held.item));
                const game::RaycastHit placeTarget =
                    floating ? game::raycast(world, reachFrom, camera.forward(), kBlockReach, true)
                             : target;
                const game::BlockId aimedAt =
                    placeTarget.hit ? world.blockAt(placeTarget.block.x, placeTarget.block.y,
                                                    placeTarget.block.z)
                                    : game::BlockId::Air;
                // Aiming at a plant puts the block **in** its cell rather than a
                // step above it, which is what left a flower standing under
                // whatever had just been placed on it.
                const glm::ivec3 placeCell =
                    floating && game::isWater(aimedAt)
                        ? placeTarget.block + glm::ivec3{0, 1, 0}
                        : (game::isReplaceable(aimedAt) ? placeTarget.block : placeTarget.adjacent);
                const bool clickedAbove = target.adjacent.y > target.block.y;
                const bool clickedBelow = target.adjacent.y < target.block.y;

                // **Which half of the clicked block the ray landed in, and
                // it is not the same question as which face was crossed.**
                // Landed 2026-08-19 against finding 140.
                //
                // https://minecraft.wiki/w/Stairs *Placement*: "Pointing at
                // a block top or the bottom half of a block side places the
                // stairs right side up. Pointing at a block bottom or the
                // top half of a block side places the stairs upside-down."
                // https://minecraft.wiki/w/Slab states the identical rule
                // for top against bottom slabs, and
                // https://minecraft.fandom.com/wiki/Trapdoor/BS the identical
                // rule again for a trapdoor's `upside_down_bit`.
                //
                // Only `clickedBelow` existed, and it is false for **every**
                // side click, so 416 stair ids and 110 slab ids could be
                // built one way only. The single route to an upside-down
                // stair was to click a *ceiling*, which in an ordinary build
                // means digging a hole above yourself first - so from the
                // player's side the game simply did not have them.
                //
                // **Measured against the clicked block rather than
                // `std::floor`**, because it is that cell's half the rule
                // names, and a point that lands exactly on an integer
                // boundary makes `floor` ambiguous while a subtraction is
                // not.
                //
                // **The slab merge below deliberately keeps the strict face
                // tests and is not widened to this**, which looks like the
                // same omission and is not: the raycast meets block
                // *geometry*, so the side of a bottom slab is only ever hit
                // between 0.0 and 0.5 and the side of a top slab only
                // between 0.5 and 1.0. A side click on a slab therefore
                // already resolves to the half that slab does not occupy,
                // in the *neighbouring* cell, which is the reference's
                // behaviour - a double slab is made by clicking the slab's
                // own top or bottom face, and that is what `completesSlab`
                // tests. Widening it would merge across a wall.
                //
                // `point` is `RaycastHit`'s, filled by `raycast` at no extra
                // cost and until now read by nothing in the game;
                // `Raycast.hpp` says over the member that this is what it is
                // for. `target` rather than `placeTarget` deliberately, to
                // match the two lines above: `placeTarget` differs only for
                // a block that rests on water, and no slab or stair does.
                const bool clickedSide = !clickedAbove && !clickedBelow;
                const bool upperHalfClicked =
                    clickedBelow ||
                    (clickedSide && target.point.y - static_cast<float>(target.block.y) > 0.5f);

                // Two halves meeting in one cell become a whole block. Left
                // as separate halves they stack as slab, gap, slab, which is
                // never what anyone is trying to build. **Both halves have
                // to be the same material**, or a spruce slab dropped on a
                // stone one silently produced a block of stone - **and the
                // material has to survive the round trip**, which is what
                // `slabMergeReturnsItsMaterial` is for: a stone pair merged
                // to `Stone` and mined back as one cobblestone, so those two
                // ids of the hundred and ten decline the merge rather than
                // eating the slabs.
                //
                // **Hoisted out of the placement body on 2026-08-19, and the
                // hoist is the fix rather than tidying** - finding 155. This
                // is the one branch that redirects `where` away from
                // `placeCell`, and the occupancy guard below was vetting
                // `placeCell` while the write landed in `target.block`. Two
                // bugs, opposite directions, one cause:
                //
                //   * A cell that **is** occupied was written. Nothing can
                //     stand wholly inside a cell holding a half slab, but a
                //     baby animal is 0.35 m tall and fits under a top slab
                //     perfectly well - and merging seals it inside a full
                //     block, which is exactly what `cellIsOccupied` was
                //     written to prevent.
                //   * A cell that is **not** occupied was refused. Crouched
                //     under a top slab with your head in the cell below it,
                //     the merge is refused because your head is in
                //     `placeCell` - a cell nothing was going to write.
                //
                // `held` is only safe to read through `canPlace`, which is
                // why that leads the expression: `blockForItem` of an empty
                // slot is not a block and `slabFamily` of it is not a family.
                const game::BlockId wouldPlace =
                    canPlace ? game::blockForItem(held.item) : game::BlockId::Air;
                const bool completesSlab =
                    game::isSlab(wouldPlace) && game::isSlab(aimedAt) &&
                    game::slabFamily(wouldPlace) == game::slabFamily(aimedAt) &&
                    slabMergeReturnsItsMaterial(game::slabFamily(wouldPlace)) &&
                    (game::isUpperHalf(aimedAt) ? clickedBelow : clickedAbove);
                // The cell the write actually lands in, which is the only one
                // worth asking about. Every other branch either leaves `where`
                // at `placeCell` or writes its second cell itself and asks
                // `cellIsOccupied` of that cell on its own - the door's
                // `upper`, the bed's `headCell`, the tall flower's `upper`.
                // **Those three are why this guard cannot simply be moved
                // below the branch chain**, which is the other repair this
                // finding suggested: all three call `setBlock` on their second
                // cell *inside* the chain, so a placement refused afterwards
                // would already have left half a door standing.
                const glm::ivec3 vetCell = completesSlab ? target.block : placeCell;
                if (placeTimer <= 0.0f && canPlace && placeTarget.hit && !cellIsOccupied(vetCell)) {
                    game::BlockId placing = game::blockForItem(held.item);
                    glm::ivec3 where = placeCell;

                    if (completesSlab) {
                        placing = game::kSlabFamilies[static_cast<std::size_t>(
                                                          game::slabFamily(placing))]
                                      .parent;
                        where = target.block;
                    } else if (game::isSlab(placing)) {
                        // Clicking an underside - or the top half of a side -
                        // puts the half up against it.
                        placing = game::slabAt(game::slabFamily(placing), upperHalfClicked);
                    } else if (game::isStairs(placing)) {
                        // Oriented blocks take their facing from the camera and
                        // their half from which end of the block was clicked,
                        // which is what lets you build a staircase that turns.
                        //
                        // **The facing is correct and its VALUE is not to be
                        // touched** - the axis-dominant opposite of camera aim
                        // puts the low step toward the player, which is the
                        // wiki's own definition of the `facing` property. Only
                        // the half was wrong.
                        //
                        // **The value is unchanged; only the spelling is.** This
                        // was a sixth hand-rolled copy of the toward-the-placer
                        // compass, and it wrote the `FaceDirection`-to-`Facing`
                        // half of it out as well, which is the rung above a
                        // comment on `CLAUDE.md`'s derive-or-assert ladder:
                        // `facingToward` already inverts - it answers `NegX`
                        // when `aimX > 0` - and `toFacing` already maps `NegX`
                        // to `West`. Term for term against what stood here:
                        // `ax > az` is `std::abs(aim.x) > std::abs(aim.z)` and
                        // **both take the z branch on a tie**; `aimX > 0.0f`
                        // splits West from East on exactly the same boundary,
                        // and `aimZ > 0.0f` splits North from South. All four
                        // arms agree, so this is a rename of an expression and
                        // not a change of behaviour.
                        //
                        // **Not `facingToward(-aim.x, -aim.z)`**, which is the
                        // redstone branch below and is the *other* rule - a
                        // machine points where the player is looking, a stair's
                        // low step points back at them. The sign is the whole
                        // difference between the two and it is easy to copy the
                        // wrong neighbour.
                        const glm::vec3 aim = camera.forward();
                        const game::Facing facing =
                            game::toFacing(game::facingToward(aim.x, aim.z));
                        placing =
                            game::stairsAt(game::stairFamily(placing), facing, upperHalfClicked);
                    } else if (game::isRedstoneComponent(placing) ||
                               game::isRedstoneTorch(placing)) {
                        // ---- Redstone. ----
                        // `into` points from the new cell at the block that was
                        // clicked, so it names the face this is stuck to: down
                        // for a floor, up for a ceiling, or one of the four
                        // walls.
                        const glm::ivec3 into = target.block - placeCell;
                        const glm::vec3 aim = camera.forward();
                        // Which way the player is looking, as one of six. A
                        // machine placed while looking down points down.
                        const int aimedFacing =
                            std::abs(aim.y) > std::abs(aim.x) && std::abs(aim.y) > std::abs(aim.z)
                                ? (aim.y > 0.0f ? game::Facing6Up : game::Facing6Down)
                                : game::directionAsFacing6(
                                      game::facingToward(-aim.x, -aim.z));
                        const game::FaceDirection wall =
                            into.x > 0   ? game::FaceDirection::PosX
                            : into.x < 0 ? game::FaceDirection::NegX
                            : into.z > 0 ? game::FaceDirection::PosZ
                            : into.z < 0 ? game::FaceDirection::NegZ
                                         : game::FaceDirection::Unknown;
                        if (game::isRedstoneTorch(placing)) {
                            // A torch on a wall leans off it; on a floor it
                            // stands up. A ceiling gives it nothing to hold on
                            // to, which is the reference's rule too.
                            placing = into.y > 0 ? game::BlockId::Air
                                                 : game::redstoneTorchAt(wall, true);
                        } else if (game::isLever(placing)) {
                            const bool alongX = std::abs(aim.x) > std::abs(aim.z);
                            const int mount =
                                wall != game::FaceDirection::Unknown
                                    ? game::LeverWallFirst + static_cast<int>(wall)
                                : into.y > 0 ? (alongX ? game::LeverCeilingX : game::LeverCeilingZ)
                                             : (alongX ? game::LeverFloorX : game::LeverFloorZ);
                            placing = game::leverAt(mount, false);
                        } else if (game::isButton(placing)) {
                            const int mount = wall != game::FaceDirection::Unknown
                                                  ? 2 + static_cast<int>(wall)
                                              : into.y > 0 ? 1
                                                           : 0;
                            placing = game::buttonAt(game::buttonFamily(placing), mount, false);
                        } else if (game::isRepeater(placing) || game::isComparator(placing)) {
                            // The arrow points **away** from whoever set it
                            // down, so the signal runs off into the build
                            // rather than back at the player.
                            const game::FaceDirection out = game::oppositeDirection(
                                game::facingToward(aim.x, aim.z));
                            placing = game::isRepeater(placing)
                                          ? game::repeaterAt(out, 1, false, false)
                                          : game::comparatorAt(out, false, false);
                        } else if (game::isPiston(placing)) {
                            placing = game::pistonAt(aimedFacing, false, game::pistonSticky(placing));
                        } else if (game::isObserver(placing)) {
                            // The **watching** face points away from you; the
                            // pulse comes out of the side facing you.
                            placing = game::observerAt(aimedFacing, false);
                        } else if (game::isDispenserLike(placing)) {
                            placing = game::dispenserAt(aimedFacing, game::isDropper(placing));
                        } else if (game::isLightningRod(placing)) {
                            // **A rod points AWAY from whatever holds it** -
                            // finding 775. These two arms were inverted, so a
                            // rod set on the ground pointed down into the floor
                            // and one under a ceiling pointed up into it.
                            //
                            // The sign is not guessable and is worth stating
                            // once: `into.y > 0` means the clicked block is
                            // ABOVE the new cell, i.e. this is stuck to a
                            // CEILING. Three things in this same block agree -
                            // `into`'s own comment above ("down for a floor, up
                            // for a ceiling"), the redstone torch, which refuses
                            // that case because "a ceiling gives it nothing to
                            // hold on to", and the lever, whose enumerators on
                            // that arm are literally named `LeverCeiling*`.
                            // So a ceiling hangs the rod DOWN and a floor stands
                            // it UP.
                            //
                            // The horizontal arm was always right and is
                            // untouched: `wall` is read off `into` and therefore
                            // points AT the support, so `oppositeDirection`
                            // already points away from it.
                            placing = game::lightningRodAt(
                                into.y > 0 ? game::Facing6Down
                                : into.y < 0
                                    ? game::Facing6Up
                                    : game::directionAsFacing6(game::oppositeDirection(wall)),
                                false);
                        } else if (game::isTripwireHook(placing)) {
                            placing = wall == game::FaceDirection::Unknown
                                          ? game::BlockId::Air
                                          : game::tripwireHookAt(
                                                game::oppositeDirection(wall), false, false);
                        } else if (game::isRail(placing)) {
                            // **Seeded flat north-south and then solved**, which
                            // is the reference's rule in both halves.
                            //
                            // The seed is the edition difference and it was
                            // backwards: an isolated rail is laid north-south in
                            // Bedrock and *the way the player is facing* in Java
                            // (https://minecraft.wiki/w/Rail), and Bedrock is
                            // this project's reference. Facing decided the whole
                            // shape here, which is why the divergence comment
                            // this replaces was true - the shape was never
                            // derived at all, so twenty of the thirty rail ids
                            // could not be built.
                            //
                            // `solveRailShape` keeps this seed only when nothing
                            // is adjacent; every other case is derived from the
                            // neighbours after the write, by `refreshRailsAround`
                            // below.
                            placing = game::railAt(game::railFamily(placing), 0, false);
                        }
                    } else if (game::isSignLike(placing)) {
                        // A sign clicked onto a wall hangs off it and shows its
                        // face outward; one set on the ground turns to whoever
                        // put it there. A hanging sign clicked onto a ceiling
                        // stays the hanging form and is the one that does not
                        // want a wall at all.
                        const glm::ivec3 into = target.block - placeCell;
                        const game::FaceDirection wall =
                            into.x > 0   ? game::FaceDirection::NegX
                            : into.x < 0 ? game::FaceDirection::PosX
                            : into.z > 0 ? game::FaceDirection::NegZ
                            : into.z < 0 ? game::FaceDirection::PosZ
                                         : game::FaceDirection::Unknown;
                        const bool onWall = wall != game::FaceDirection::Unknown &&
                                            !game::isHangingSign(placing);
                        placing = game::signAt(
                            game::signKind(placing), game::signFamily(placing),
                            onWall ? wall
                                   : game::facingToward(camera.forward().x, camera.forward().z),
                            onWall);
                    } else if (game::isFenceGate(placing)) {
                        // A gate takes the facing of whoever set it down, and is
                        // always shut to begin with.
                        placing = game::gateAt(game::gateFamily(placing),
                                               game::facingToward(camera.forward().x, camera.forward().z), false);
                    } else if (game::isFurnace(placing)) {
                        // The mouth turns to face whoever placed it, which is
                        // the reference's rule and the only way three plain
                        // sides and one front can be told apart.
                        placing = game::cookerAt(placing,
                                                 game::facingToward(camera.forward().x,
                                                                    camera.forward().z),
                                                 false);
                    } else if (game::isChest(placing)) {
                        // Same rule, same reason: one face has the latch on it.
                        // **Each family keeps its own ids** - handing every
                        // chest to `chestFacing` would turn a trapped chest
                        // into a plain one the moment it was placed.
                        const game::FaceDirection front =
                            game::facingToward(camera.forward().x, camera.forward().z);
                        if (game::isTrappedChest(placing)) {
                            placing = game::trappedChestFacing(front);
                        } else if (game::isEnderChest(placing)) {
                            placing = game::enderChestFacing(front);
                        } else if (placing != game::BlockId::Barrel &&
                                   !game::isStowbox(placing)) {
                            placing = game::chestFacing(front);
                        }
                    } else if (game::isDoor(placing)) {
                        // A door needs the cell above as well, and takes the
                        // facing of whoever hung it. **The hinge goes to the
                        // side with something solid beside it**, which is the
                        // reference's own rule and why it has to be stored.
                        const game::FaceDirection front =
                            game::facingToward(camera.forward().x, camera.forward().z);
                        const glm::ivec3 upper = placeCell + glm::ivec3{0, 1, 0};
                        const game::BlockId above = world.blockAt(upper.x, upper.y, upper.z);
                        // **The second cell is asked the same two questions the
                        // first one was.** It was only ever asked whether it was
                        // replaceable, so a door hung while you stood in the cell
                        // above put its upper half through your head - and the
                        // guard on `placeCell` that would have caught it is one
                        // line up, which is exactly the shape that keeps costing
                        // this project time.
                        if (!game::isReplaceable(above) || cellIsOccupied(upper)) {
                            placing = game::BlockId::Air;
                        } else {
                            const game::FaceDirection hingeSide = game::quarterTurn(front);
                            const glm::ivec3 step =
                                hingeSide == game::FaceDirection::PosX   ? glm::ivec3{1, 0, 0}
                                : hingeSide == game::FaceDirection::NegX ? glm::ivec3{-1, 0, 0}
                                : hingeSide == game::FaceDirection::PosZ ? glm::ivec3{0, 0, 1}
                                                                         : glm::ivec3{0, 0, -1};
                            const game::BlockId beside = world.blockAt(
                                placeCell.x + step.x, placeCell.y, placeCell.z + step.z);
                            const bool hingeRight = !game::isReplaceable(beside);
                            const int family = game::doorFamily(placing);
                            placing = game::doorAt(family, front, hingeRight, false, false);
                            // The second cell is replaced too, and a replacement
                            // is a break wherever it happens - a door hung over
                            // a candle stack or a wheat crop deleted it exactly
                            // as the cell below did.
                            spillReplaced(upper);
                            world.setBlock(upper.x, upper.y, upper.z,
                                           game::doorAt(family, front, hingeRight, false, true));
                        }
                    } else if (game::isHopper(placing)) {
                        // The spout points at whatever you clicked. Clicking a
                        // floor or a ceiling gives no compass direction, and
                        // `hopperWithSideSpout` folds both to the plain
                        // downward one - which is the reference's behaviour and
                        // the reason there is no upward state to fold *to*.
                        const glm::ivec3 into = target.block - placeCell;
                        placing = game::hopperWithSideSpout(
                            into.x > 0   ? game::FaceDirection::PosX
                            : into.x < 0 ? game::FaceDirection::NegX
                            : into.z > 0 ? game::FaceDirection::PosZ
                            : into.z < 0 ? game::FaceDirection::NegZ
                                         : game::FaceDirection::Unknown);
                    } else if (game::isTrapdoor(placing)) {
                        // **Top or bottom half by where on the block you
                        // clicked**, which is the same rule as a slab and a
                        // stair and the same `upperHalfClicked` that derives it.
                        // https://minecraft.fandom.com/wiki/Trapdoor/BS - the
                        // `upside_down_bit` follows the clicked half exactly as
                        // a slab's does.
                        //
                        // This read `camera.forward().y > 0.0f`, and its own
                        // comment named the reason: "the only way to get one
                        // under a ceiling without a per-face click position".
                        // `RaycastHit::point` is that position and it landed on
                        // 2026-08-19, so the workaround is retired rather than
                        // merely improved. It was wrong in the ordinary case as
                        // well as the awkward one - hanging a trapdoor on the
                        // top half of a wall while looking level or slightly
                        // down gave a bottom one every time, and the only way to
                        // get a top trapdoor was to aim upward, which usually
                        // means aiming at a different block entirely.
                        placing = game::trapdoorAt(
                            game::trapdoorFamily(placing),
                            game::facingToward(camera.forward().x, camera.forward().z), false,
                            upperHalfClicked);
                    } else if (game::isBed(placing)) {
                        // A bed needs the cell beyond it as well. The facing is
                        // the direction the **head** lies from the foot, so one
                        // value orients both halves.
                        const game::FaceDirection away = game::oppositeDirection(
                            game::facingToward(camera.forward().x, camera.forward().z));
                        const glm::ivec3 step =
                            away == game::FaceDirection::PosX   ? glm::ivec3{1, 0, 0}
                            : away == game::FaceDirection::NegX ? glm::ivec3{-1, 0, 0}
                            : away == game::FaceDirection::PosZ ? glm::ivec3{0, 0, 1}
                                                                : glm::ivec3{0, 0, -1};
                        const glm::ivec3 headCell = placeCell + step;
                        const game::BlockId atHead =
                            world.blockAt(headCell.x, headCell.y, headCell.z);
                        // **Both halves need a floor, and only the head was
                        // ever asked.** The foot is the cell you actually
                        // clicked, so it is the one most likely to be put over
                        // nothing: standing at a cliff edge facing inland placed
                        // a bed with its foot hanging in the air, and nothing
                        // afterwards could ever notice, because
                        // `needsSupportBelow` answers false for a bed and so the
                        // generic support gate below never runs on one. The
                        // reference refuses that placement - a bed wants a full
                        // solid block under each half - and this is a rule that
                        // was written once, correctly, and did not travel to the
                        // second cell that needs it.
                        //
                        // Support is asked *here* for the same reason the tall
                        // flower below asks it here: this branch writes the
                        // second cell itself, so a bed refused later would
                        // already have left half of itself in the world.
                        const game::BlockId underHead =
                            world.blockAt(headCell.x, headCell.y - 1, headCell.z);
                        const game::BlockId underFoot =
                            world.blockAt(placeCell.x, placeCell.y - 1, placeCell.z);
                        if (!game::isReplaceable(atHead) || !game::isSolid(underHead) ||
                            !game::isSolid(underFoot) || cellIsOccupied(headCell)) {
                            placing = game::BlockId::Air;
                        } else {
                            const int colour = game::bedColour(placing);
                            placing = game::bedAt(colour, away, false);
                            // Same rule as the door's upper half: the head cell
                            // passed `isReplaceable` and nothing asked what that
                            // let it write over.
                            spillReplaced(headCell);
                            world.setBlock(headCell.x, headCell.y, headCell.z,
                                           game::bedAt(colour, away, true));
                        }
                    } else if (game::isTallFlower(placing)) {
                        // **A tall flower is two cells and only one was ever
                        // written**, so all four rendered as headless stumps and
                        // the eight upper ids were unreachable in a live world.
                        // https://minecraft.wiki/w/Lilac - breaking either half
                        // destroys the whole plant and drops exactly one item,
                        // which is why the write here and the twin-clear in
                        // `clearPairedHalf` had to land together: writing the
                        // top without the paired break would have paid a whole
                        // flower per half, because both ids drop one.
                        //
                        // The support test is asked *here* rather than being
                        // left to the gate below, because that gate runs after
                        // this branch has already written the second cell - a
                        // flower refused for want of a floor would have left its
                        // own top half floating. A door and a bed never reach
                        // that gate (`needsSupportBelow` is Cross, Flat or a
                        // torch and neither of them is any of those), so this is
                        // the only two-cell branch that has to ask.
                        const game::BlockId lower =
                            game::isTallFlowerUpper(placing)
                                ? static_cast<game::BlockId>(static_cast<int>(placing) - 1)
                                : placing;
                        const glm::ivec3 upper = placeCell + glm::ivec3{0, 1, 0};
                        const game::BlockId above = world.blockAt(upper.x, upper.y, upper.z);
                        if (!game::isReplaceable(above) || cellIsOccupied(upper) ||
                            !hasItsSupport(lower, placeCell.x, placeCell.y, placeCell.z)) {
                            placing = game::BlockId::Air;
                        } else {
                            placing = lower;
                            // Same rule as the door's upper half: the cell above
                            // passed `isReplaceable` and nothing asked what that
                            // let it write over.
                            spillReplaced(upper);
                            world.setBlock(
                                upper.x, upper.y, upper.z,
                                static_cast<game::BlockId>(static_cast<int>(lower) + 1));
                        }
                    } else if (game::isBeehive(placing)) {
                        // Same rule again: a hive has one entrance, and the bees
                        // that will use it need to know which side it is on.
                        //
                        // **Through `facingToward` rather than written out
                        // here** - finding 156. `Block.hpp` says in as many
                        // words that this expression "was written out at four
                        // separate placement sites... which is exactly the
                        // shape of bug this project keeps paying for. One
                        // owner", and then this site went on being a fifth
                        // copy of it. Substituted term for term: the helper's
                        // `ax > az` with its hand-rolled absolute values is
                        // this line's `std::abs(aim.x) > std::abs(aim.z)`, and
                        // both take the z branch on a tie.
                        const glm::vec3 aim = camera.forward();
                        const game::FaceDirection front = game::facingToward(aim.x, aim.z);
                        // **`beeHomeAtLevel`'s THREE-argument overload, and the
                        // two-argument one still must not be used here.** They
                        // differ in exactly the field this branch exists to
                        // set: the short form reads the facing off the block
                        // handed to it, and a *placed* home has to face the
                        // camera rather than wherever the item's canonical id
                        // happened to point. Finding 9618 proposed swapping in
                        // the short form and taking it would have broken the
                        // facing of every hive a player places, to fix a nest
                        // path that cannot be reached at all - `Item.hpp`
                        // returns `ItemId::None` for a nest, so one can never
                        // be held. That is the "fixed one arm and broke the
                        // working one" shape, which is why this site asked
                        // `Block.hpp` for a facing-taking overload instead of
                        // taking the swap. It arrived, so the kind rule stops
                        // being written out here: `beeHomeAtLevel(placing,
                        // front, carriedHoney)` is the ternary below it stood,
                        // term for term - kind from `placing`, facing from the
                        // camera, level carried - and `Block.hpp` guards the
                        // substitution with two compiled negative controls, one
                        // that turns and forgets the kind and one that keeps the
                        // kind and ignores the new facing, so neither half can
                        // rot into the other.
                        //
                        // The level is carried rather than
                        // `beehiveHasHoney`'d: that predicate collapses six
                        // honey levels to a bool, so a partially filled home
                        // rounded down to empty on the way through.
                        const int carriedHoney = game::beehiveHoneyLevel(placing);
                        placing = game::beeHomeAtLevel(placing, front, carriedHoney);
                    } else if (game::isCarvedPumpkin(placing) || game::isJackOLantern(placing)) {
                        // **The carved face looks back at whoever put it down**
                        // - finding 248. There was no branch here at all, so
                        // `blockForItem` handed back facing index 0 every time
                        // and three of each family's four ids were unreachable
                        // by placement: every pumpkin in a build stared the same
                        // way whatever the player did.
                        //
                        // The reference states it twice over - "when placed, a
                        // carved pumpkin automatically faces the player", and
                        // the facing state is "the opposite from the direction
                        // the player faces while placing"
                        // (https://minecraft.wiki/w/Carved_Pumpkin). Those are
                        // the same sentence: the face points back down the
                        // player's line of sight, which is exactly what
                        // `facingToward` returns and why it is NOT wrapped in
                        // `oppositeDirection` the way the repeater above is.
                        //
                        // The carving path elsewhere in this file already did
                        // this correctly; it was only placement that was
                        // missing, so this is a new branch rather than a
                        // corrected one.
                        const game::FaceDirection pumpkinFace =
                            game::facingToward(camera.forward().x, camera.forward().z);
                        placing = game::isJackOLantern(placing)
                                      ? game::jackOLanternAt(pumpkinFace)
                                      : game::carvedPumpkinAt(pumpkinFace);
                    } else if (game::isLadder(placing) || game::isVine(placing) ||
                               game::isCocoa(placing)) {
                        // **These three take their facing from the wall they
                        // were put against, not from the camera.** They are the
                        // only placed blocks whose orientation is a fact about
                        // where they landed rather than about who put them
                        // there, and one facing into open air would be useless.
                        const glm::ivec3 back = placeTarget.block - where;
                        game::FaceDirection wall = game::FaceDirection::Unknown;
                        if (back.x > 0) {
                            wall = game::FaceDirection::PosX;
                        } else if (back.x < 0) {
                            wall = game::FaceDirection::NegX;
                        } else if (back.z > 0) {
                            wall = game::FaceDirection::PosZ;
                        } else if (back.z < 0) {
                            wall = game::FaceDirection::NegZ;
                        }
                        const bool solidBehind =
                            world.isSolid(placeTarget.block.x, placeTarget.block.y,
                                          placeTarget.block.z);
                        if (wall == game::FaceDirection::Unknown || !solidBehind) {
                            placing = game::BlockId::Air;
                        } else if (game::isLadder(placing)) {
                            placing = game::ladderFacing(wall);
                        } else if (game::isCocoa(placing)) {
                            // **Jungle wood only, and it is the block behind
                            // that decides rather than merely something being
                            // there.** `solidBehind` above is the shared test
                            // for all three of these families, and for a pod it
                            // is far too generous: the reference allows "jungle
                            // logs, jungle wood, stripped jungle logs and
                            // stripped jungle wood" and nothing else, so a pod
                            // could be stuck on cobblestone as decoration.
                            // Four ids, listed rather than reached through a
                            // family predicate because none exists - the log
                            // families are ranges keyed on species, not on
                            // wood-versus-stripped, and inventing a fifth
                            // spelling of "jungle" here would be the second
                            // owner rather than the first.
                            //
                            // The other half of finding 247 - a pod left
                            // floating when its log is mined - is **already
                            // closed** and needs nothing here: `wallBehind`
                            // answers `cocoaFacing` for a pod, and
                            // `settleAround`'s sideways walk drops it with the
                            // wall. Said so it is not fixed twice.
                            const game::BlockId host =
                                world.blockAt(placeTarget.block.x, placeTarget.block.y,
                                              placeTarget.block.z);
                            const bool jungleWood = host == game::BlockId::JungleLog ||
                                                    host == game::BlockId::StrippedJungleLog ||
                                                    host == game::BlockId::JungleWood ||
                                                    host == game::BlockId::StrippedJungleWood;
                            placing = jungleWood ? game::cocoaAt(wall, 0) : game::BlockId::Air;
                        } else {
                            // A vine clings to the side of its own cell facing
                            // the wall, which is the opposite of the wall's own
                            // compass direction.
                            const std::uint8_t side =
                                wall == game::FaceDirection::PosX   ? game::ConnectEast
                                : wall == game::FaceDirection::NegX ? game::ConnectWest
                                : wall == game::FaceDirection::PosZ ? game::ConnectSouth
                                                                    : game::ConnectNorth;
                            placing = game::vineWith(side);
                        }
                    }

                    // A ladder with no wall behind it clears `placing` above;
                    // nothing else that needs a support may go down without one.
                    if (placing == game::BlockId::Air) {
                        placeTimer = kPlaceRepeatSeconds;
                    } else if (game::needsSupportBelow(placing) &&
                               !hasItsSupport(placing, where.x, where.y, where.z)) {
                        placeTimer = kPlaceRepeatSeconds;
                    } else {
                        // **Whatever was standing here is being broken, so it
                        // drops.** Last thing before the write, so every branch
                        // above that redirected `where` - the slab, the lily
                        // pad, the plant a placement lands *in* - is already
                        // settled and there is one place to get this right.
                        //
                        // `completesSlab` is the one exemption and it is a merge
                        // rather than a break: the half already in the cell
                        // becomes part of the block that replaces it, so
                        // spilling it would hand back a slab that was never
                        // destroyed.
                        if (!completesSlab) {
                            spillReplaced(where);
                        }
                        world.setBlock(where.x, where.y, where.z, placing);
                        // **The other half of deriving track shape.** The rail
                        // just laid takes its shape from its neighbours and they
                        // take theirs from it - an existing straight run turns
                        // into a corner, or into a ramp, the moment one is put
                        // beside it, which is the reference's behaviour and the
                        // reason a corner can be built at all. Silent for the
                        // three thousand ids that are not rails.
                        refreshRailsAround(where);
                        // **Nothing may inherit a stale block entity.**
                        // `chests` and `furnaces` are keyed by position and
                        // versioned separately from the chunk format, so an
                        // entry can outlive the block it belonged to - a format
                        // bump regenerates the chunk, the container block is
                        // gone, and the twenty-seven slots are still sitting in
                        // the map at that exact cell. Building a new chest there
                        // handed them to you for free, which is a duplication
                        // exploit reached by ordinary play.
                        //
                        // Cleared for **every** placement rather than only for
                        // containers, because the cell can only hold one thing
                        // and whatever was recorded for it is now wrong however
                        // it got there. The stowbox restore below runs after
                        // this on purpose: it is putting the right contents in.
                        chests.erase(where);
                        furnaces.erase(where);
                        campfires.erase(where);
                        // **The fourth map keyed by cell, and the one that owes
                        // an item rather than merely being stale.** A disc in
                        // this list was taken out of the player's bag, so
                        // deleting the entry the way the three above are deleted
                        // would destroy it. Reachable only when a jukebox has
                        // gone without the break or blast path - a chunk-format
                        // bump regenerating the column - so there is nothing
                        // still in the world for the ejected disc to duplicate.
                        spillJukeboxDisc(where);
                        // The one construction in the game: a T of iron blocks
                        // with a carved pumpkin on top becomes an iron golem.
                        // **Hung off the placement of the pumpkin**, because
                        // that is the reference's rule - the head must go on
                        // last - and because this is already the single point
                        // where the main thread writes a block.
                        if (game::isCarvedPumpkin(placing) || game::isJackOLantern(placing)) {
                            tryRaiseGolem(where);
                        }
                        // **A hopper needs its block entity the moment it is
                        // placed, and this is why it looked broken.** The
                        // transfer pass discovers hoppers by walking the `chests`
                        // map - it has to, because scanning every loaded chunk
                        // for hoppers eight times a second is not affordable -
                        // so a hopper with no record in that map is not merely
                        // empty, it **does not tick at all**. The erase four
                        // lines above is what left it that way, and the only
                        // thing that ever created the record was opening the
                        // hopper by hand (`chests.try_emplace` in the use path).
                        // So: build a hopper under a furnace, watch it do
                        // nothing, right-click it once for no reason, and it
                        // starts working. Nobody would ever guess that.
                        //
                        // `try_emplace` rather than `materialise`, deliberately:
                        // `materialise` rolls loot on a cell it has not seen, and
                        // a hopper is not a loot container. Same call the use
                        // path makes.
                        if (game::isHopper(placing)) {
                            chests.try_emplace(where);
                        }
                        // A stowbox brings its contents back out of the side
                        // table. The handle is freed here rather than left
                        // behind, so nothing accumulates across a session.
                        if (game::isStowbox(placing) && held.damage != 0) {
                            const auto carried = stowed.find(held.damage);
                            if (carried != stowed.end()) {
                                chests[where] = carried->second;
                                stowed.erase(carried);
                            }
                        }
                        // The reference uses a block's *dig* sound for placing
                        // it too, quieter. One recording, two events.
                        const game::SoundEvent placed =
                            game::digSoundFor(game::soundMaterialFor(placing));
                        if (placed != game::SoundEvent::Count) {
                            sounds.play(audio, placed, glm::vec3{where} + glm::vec3{0.5f}, 0.6f);
                        }
                        if (!creative) {
                            inventory.consumeOne(selectedSlot);
                            hudDirty = true;
                        }
                        placeTimer = kPlaceRepeatSeconds;
                    }
                }
            }

            // Buttons let go on their own, which is the whole of what makes a
            // button different from a lever. Swept here rather than inside the
            // input block because a press outlives the click that made it, and
            // beside the streaming update because that is where the frame's
            // other world writes already are.
            //
            // **Whatever is standing there now is what gets released**, not what
            // was pressed: mine a pressed button and the entry finds Air, drops
            // its claim and writes nothing, so a button can never be resurrected
            // over the top of something else.
            for (std::pair<glm::ivec3, float>& pressed : heldButtons) {
                pressed.second -= deltaSeconds;
            }
            heldButtons.erase(
                std::remove_if(heldButtons.begin(), heldButtons.end(),
                               [&](const std::pair<glm::ivec3, float>& pressed) {
                                   const glm::ivec3 at = pressed.first;
                                   const game::BlockId now = world.blockAt(at.x, at.y, at.z);
                                   const bool stillDown =
                                       game::isButton(now) && game::buttonPressed(now);
                                   if (pressed.second > 0.0f && stillDown) {
                                       return false;
                                   }
                                   if (stillDown) {
                                       world.setBlock(at.x, at.y, at.z,
                                                      game::buttonAt(game::buttonFamily(now),
                                                                     game::buttonMount(now),
                                                                     false));
                                       sounds.play(audio, game::SoundEvent::WoodClick,
                                                   glm::vec3{at} + glm::vec3{0.5f}, 0.5f);
                                   }
                                   return true;
                               }),
                heldButtons.end());

            // Streaming runs after edits so a broken block is re-meshed in the
            // same frame it changed, and shares the same budget.
            applyUpdates(world.update(player.position, kStreamingBudgetSeconds));

            // Furnaces run whether or not anyone is watching, and swap between
            // the lit and unlit block as they light and go out. Only a change is
            // written, or every frame would re-mesh the chunk they sit in.
            for (auto& [position, furnace] : furnaces) {
                const game::BlockId present = world.blockAt(position.x, position.y, position.z);
                if (!game::isFurnace(present)) {
                    continue;
                }
                // A smoker runs the **whole** tick at double rate, which is the
                // reference's behaviour in one number: it cooks in five seconds
                // instead of ten and burns its fuel twice as fast, so the items
                // per lump of charcoal are unchanged.
                // **`present`, not the default.** `cooker` decides what the
                // thing will accept - a smoker takes food, a blast furnace ore
                // and metal - and it is defaulted to a plain `Furnace`, so
                // omitting it compiles perfectly and quietly turns the whole
                // restriction off, leaving all three cookers identical. The
                // world id is the right answer because `cookerAccepts` asks
                // `isSmoker`/`isBlastFurnace`, which are family predicates and
                // so already cover the lit and directional variants - the same
                // reason `cookSpeed(present)` on this very line works.
                const bool lit =
                    game::tickFurnace(furnace, deltaSeconds * game::cookSpeed(present), present);
                // Lighting one must not turn it round or change what kind it is:
                // all three live in the id, so they are recombined rather than
                // one being overwritten with a default.
                const game::BlockId wanted =
                    game::cookerAt(present, game::furnaceFacing(present), lit);
                if (present != wanted) {
                    world.setBlock(position.x, position.y, position.z, wanted);
                }
            }

            // Campfires cook too, and on their own arrangement entirely: four
            // items at once, thirty seconds each, no fuel, and the finished food
            // pops off onto the floor rather than into a slot nobody can open.
            //
            // **Gated on the column being resident, which the furnace loop above
            // is not.** A furnace that finishes in an unloaded column merely
            // moves an item between two slots it already owns; a campfire spawns
            // an entity, and an entity spawned into a column that is not there
            // falls through the world exactly as the player used to. The
            // asymmetry is deliberate and is filed rather than smoothed over -
            // the furnace's tick has other reasons to be watched.
            for (auto& [position, campfire] : campfires) {
                if (campfire.idle()) {
                    continue;
                }
                if (!game::isCampfire(world.blockAt(position.x, position.y, position.z))) {
                    continue;
                }
                if (!world.columnResident(position.x, position.z)) {
                    continue;
                }
                // **No `cookSpeed` here, and that is not an oversight.** There is
                // exactly one speed multiplier in this game and it lives on the
                // furnace call above; a campfire's thirty seconds is a published
                // number in its own right, `kCampfireCookSeconds` asserts it
                // against the furnace's ten, and multiplying it by anything
                // would make the assert a lie about what the player experiences.
                const game::CampfireDone finished = game::tickCampfire(campfire, deltaSeconds);
                if (finished.count == 0) {
                    continue;
                }
                const glm::vec3 above = glm::vec3{position} + glm::vec3{0.5f, 1.05f, 0.5f};
                for (int i = 0; i < finished.count; ++i) {
                    game::dropStack(drops, above, finished.stacks[static_cast<std::size_t>(i)],
                                    glm::vec3{0.0f, 1.6f, 0.0f});
                }
                sounds.play(audio, game::SoundEvent::Pop, above, 0.5f);
            }

            // Hoppers move one item every eight game ticks, which is the
            // reference's own rate. Positions are gathered before anything is
            // moved: a push into a container nobody has opened yet creates its
            // block entity, and creating one while walking the map is exactly
            // the rehash that invalidates the walk.
            hopperTimer += deltaSeconds;
            while (hopperTimer >= game::kHopperTransferSeconds) {
                hopperTimer -= game::kHopperTransferSeconds;

                hopperCells.clear();
                for (const auto& [position, contents] : chests) {
                    (void)contents;
                    if (game::isHopper(world.blockAt(position.x, position.y, position.z))) {
                        hopperCells.push_back(position);
                    }
                }

                for (const glm::ivec3& cell : hopperCells) {
                    const game::BlockId self = world.blockAt(cell.x, cell.y, cell.z);
                    const game::FaceDirection spout = game::hopperSideSpout(self);
                    const glm::ivec3 pours =
                        cell + (spout == game::FaceDirection::PosX   ? glm::ivec3{1, 0, 0}
                                : spout == game::FaceDirection::NegX ? glm::ivec3{-1, 0, 0}
                                : spout == game::FaceDirection::PosZ ? glm::ivec3{0, 0, 1}
                                : spout == game::FaceDirection::NegZ ? glm::ivec3{0, 0, -1}
                                                                     : glm::ivec3{0, -1, 0});

                    // Push first, then pull, so a chain of hoppers carries an
                    // item one step per hopper per tick rather than all the way
                    // along in whichever order the map happened to be in.
                    const game::BlockId ahead = world.blockAt(pours.x, pours.y, pours.z);
                    if (game::isChest(ahead) || game::isHopper(ahead)) {
                        // **`materialise`, not `chests[...]`.** Reaching into
                        // the map alone hands back an empty chest for an
                        // unrolled village one, so a hopper under it would sit
                        // there pulling nothing out of a chest that has never
                        // rolled - and the marker would stay standing forever.
                        moveOneItem(materialise(cell).slots.data(), containerSlots(self),
                                    materialise(pours).slots.data(), containerSlots(ahead));
                    } else if (game::isFurnace(ahead)) {
                        // **Which slot depends on which way this hopper points,
                        // and the two are not interchangeable.** Bedrock feeds
                        // the input from above and the fuel from the side, so a
                        // downward hopper - the one whose spout reads `Unknown`
                        // - is the "from above" case and every side spout is the
                        // fuel case. Getting this backwards is not a cosmetic
                        // difference: a fuel slot fed raw iron cannot be emptied
                        // without breaking the furnace.
                        game::Furnace& furnace = furnaces[pours];
                        if (spout == game::FaceDirection::Unknown) {
                            // The input slot takes anything, exactly as the
                            // reference's does - a furnace holding something it
                            // cannot smelt is the player's problem to notice,
                            // and filtering here would silently strand items in
                            // the hopper instead.
                            moveOneItem(materialise(cell).slots.data(), containerSlots(self),
                                        &furnace.input, 1);
                        } else {
                            moveOneItem(materialise(cell).slots.data(), containerSlots(self),
                                        &furnace.fuel, 1, +[](game::ItemId id) {
                                            return game::fuelBurnSeconds(id) > 0.0f;
                                        });
                        }
                    }

                    const glm::ivec3 over = cell + glm::ivec3{0, 1, 0};
                    const game::BlockId above = world.blockAt(over.x, over.y, over.z);
                    if (game::isChest(above) || game::isHopper(above)) {
                        moveOneItem(materialise(over).slots.data(), containerSlots(above),
                                    materialise(cell).slots.data(), containerSlots(self));
                    } else if (game::isFurnace(above)) {
                        // A hopper under a furnace pulls the output - and then
                        // **the empty bucket a lava bucket leaves sitting in the
                        // fuel slot**, which is the reference's rule and is the
                        // only way that bucket comes back at all without a
                        // player standing there to take it. `fuelBurnSeconds` is
                        // the same one owner the side push above asks, so "not
                        // fuel" here and "is fuel" there can never drift into a
                        // pair that pushes a bucket in and pulls it straight
                        // back out again.
                        game::Furnace& furnace = furnaces[over];
                        game::Chest& into = materialise(cell);
                        if (!moveOneItem(&furnace.output, 1, into.slots.data(),
                                         containerSlots(self))) {
                            moveOneItem(&furnace.fuel, 1, into.slots.data(), containerSlots(self),
                                        +[](game::ItemId id) {
                                            return game::fuelBurnSeconds(id) <= 0.0f;
                                        });
                        }
                    }
                }
            }

            // Anything a spreading flow swept aside - or a falling block landed
            // on - drops, the same way a plant left hanging by mining below it
            // does.
            //
            // **This channel carries five different events, not one - and the
            // producer now says which.** `World::Removal` names them: a genuine
            // `Flow` sweep, a `Support` loss, leaf `Decay`, sugar cane
            // `Uprooted` by losing its water, and `Copy`, which is bone meal on
            // a two-block flower and **removes nothing at all**.
            //
            // This comment named three of the five and cited each by a line
            // number into a file this one does not own; all three numbers had
            // rotted, and two whole producers had arrived since. It also carried
            // a note reporting to the `World.cpp` owner that the context
            // belonged on `WashedBlock` - **which has since landed**, as the
            // `cause` field read below, so the note outlived the request. Both
            // are the reason this file's cross-references are symbolic now.
            //
            // `alreadyPaid` is the other half of a two-cell plant that a twin
            // clear has already taken. Two ticks can drain in one frame, so a
            // flow can record *both* halves of a sunflower in the same batch -
            // and `washed.block` is the id as it stood when the water arrived,
            // so re-reading the world cannot tell the second entry it is already
            // paid for. Empty in every frame that washes nothing paired, which
            // is almost all of them.
            std::vector<glm::ivec3> alreadyPaid;
            for (const game::World::WashedBlock& washed : world.takeWashedBlocks()) {
                if (std::find(alreadyPaid.begin(), alreadyPaid.end(), washed.position) !=
                    alreadyPaid.end()) {
                    continue;
                }
                // The table rather than a single item: a washed-away stack of
                // candles is four candles, and a washed-away clay bank is four
                // clay.
                //
                // **A sword-shaped context, and only here.** Water is one of the
                // reference's qualifying conditions in its own right
                // (minecraft.wiki, *Cobweb*: "A cobweb drops one piece of string
                // if broken with a non-Silk Touch sword, **if water touches or
                // flows over it**, or a piston pushes it ... It drops nothing
                // when broken using anything else, or if lava flows over it"),
                // and a swept web was handing back nothing at all. `Sword` is
                // how this table is told that condition is met: the cobweb rows
                // are the only ones that read it, so every other plant a flow
                // sweeps aside answers exactly as it did - seeds rather than the
                // plant, which is what bare hands would have got.
                //
                // The lava half of that sentence needs no code: `World` never
                // records a lava-swept block as washed, it simply destroys it.
                //
                // **`Flow` alone earns it, and the event says so.** The sword
                // was handed to all five producers because when it was written
                // there was only one, and the qualifying condition the reference
                // states is *water touching the web* - not "arrived on this
                // queue". `World::Removal::Flow` is the only one water is
                // involved in, which is the producer's own wording. Nothing
                // reachable today changes answer, because the cobweb is the only
                // row in the table that reads `ToolKind::Sword` and the other
                // four producers are leaves, sugar cane, bone-mealed flowers and
                // blocks that lost their footing - but a cobweb that ever loses
                // its support would have paid string for it, and that is the
                // trap rather than the bug.
                spillBlockDrop(washed.position, washed.block,
                               washed.cause == game::World::Removal::Flow
                                   ? game::BreakContext{.tool = game::ToolKind::Sword}
                                   : game::BreakContext{});
                // **A two-cell plant is one plant however it is broken, and this
                // was the one path never told.** A flow arrives one cell at a
                // time, so it is the only path that can reach either half on its
                // own - wash the top of a sunflower for one flower, the water
                // falls into the bottom, and there is a second flower out of a
                // plant that cost one. `settleAround` and `spillReplaced` both
                // clear the twin; this did not.
                //
                // **Every cause but `Copy`**, because bone meal reports on this
                // same channel without removing anything: a copied flower is
                // still standing and clearing its twin would delete half the
                // plant the player just fed.
                //
                // This asked the *world* instead - "is the cell empty or a
                // fluid now?" - which is the same answer for every case that can
                // happen today and is not the same question. `World.hpp` says so
                // in as many words beside `Removal::Copy`: re-reading the cell
                // is "true today and stops being true the moment anything else
                // writes there first". The producer knows what it did; asking it
                // costs one comparison and cannot be raced. That is the whole
                // reason `Removal` was added, and this was the reader it was
                // added for.
                if (washed.cause != game::World::Removal::Copy) {
                    if (const std::optional<glm::ivec3> twin =
                            clearPairedHalf(washed.position, washed.block)) {
                        alreadyPaid.push_back(*twin);
                    }
                }
            }

            // Blocks the world just took out of the grid become entities, and
            // the entities put themselves back when they land.
            for (const game::World::WashedBlock& detached : world.takeDetachedBlocks()) {
                fallingBlocks.spawn(detached.position, detached.block);
            }

            // Where water put something out: lava setting, or a fire dying.
            //
            // **Drained unconditionally, beside its three siblings, and that
            // matters more here than it does for them.** This is the one channel
            // that is *capped* - past `kMaxPendingFizzes` the producer drops
            // events rather than queueing them - so an undrained frame is not
            // merely late, it is lossy, and a drain that never runs turns
            // `recordFizz` into a permanent no-op for the rest of the session
            // once the buffer stays full. A condition in front of this loop would
            // do exactly that.
            //
            // `became` is deliberately not read: there is one `Fizz` recording,
            // so there is nothing yet to choose between. It is the field to reach
            // for the day obsidian setting is meant to sound unlike a fire going
            // out - which is why the event carries it rather than making a reader
            // re-query a world that has moved on.
            for (const game::World::FizzEvent& fizz : world.takeFizzes()) {
                sounds.play(audio, game::SoundEvent::Fizz,
                            glm::vec3{fizz.position} + glm::vec3{0.5f}, 0.9f);
            }

            // What the population had to say for itself this tick.
            for (const game::CreatureVoiceEvent& voice : creatures.takeVoices()) {
                const game::VoiceState state =
                    voice.sound == game::CreatureSound::Hurt    ? game::VoiceState::Hurt
                    : voice.sound == game::CreatureSound::Death ? game::VoiceState::Death
                                                                : game::VoiceState::Idle;
                sounds.playVoice(audio, voice.kind, state, voice.at, voice.scale);
            }

            // Meat from anything that was killed, on the same path as every
            // other drop - **`dropStack`, not `spawn`**. It is the one helper
            // that carries a stack's `damage`, so the day a creature drops the
            // sword it was holding, that sword arrives worn rather than repaired
            // and a stowbox arrives with its contents rather than empty.
            for (const game::Creatures::Loot& loot : creatures.takeLoot()) {
                game::dropStack(drops, loot.position + glm::vec3{0.0f, 0.4f, 0.0f},
                                game::ItemStack{loot.item, loot.count});
            }

            // What a grazing animal just ate. The reference's
            // `eat_and_replace_block_pairs`: a mouthful of tall grass takes the
            // plant, and a mouthful of turf takes the grass off the block and
            // leaves bare dirt. Handed over rather than done inside `Creatures`
            // because only the main thread may write to the world.
            for (const glm::ivec3& cell : creatures.takeGrazed()) {
                if (world.blockAt(cell.x, cell.y, cell.z) == game::BlockId::TallGrass) {
                    world.setBlock(cell.x, cell.y, cell.z, game::BlockId::Air);
                } else if (world.blockAt(cell.x, cell.y - 1, cell.z) == game::BlockId::Grass) {
                    world.setBlock(cell.x, cell.y - 1, cell.z, game::BlockId::Dirt);
                }
            }

            // **What a bee brought home**, and the last rung of finding 216:
            // the producer has existed and had zero callers, so pollination ran
            // every tick and no hive ever filled. Honeycomb was unobtainable,
            // and with it the four waxed-copper stages, the honeycomb block and
            // the candle recipe.
            //
            // Handed over on the same arrangement as grazing directly above,
            // and for the same reason: `Creatures` is given a `const World&` in
            // every entry point by design, so only the main thread writes.
            //
            // **Four things here are load-bearing and none of them look it.**
            //
            // 1. *The list is never de-duplicated.* A repeated cell is the
            //    reference's 1% double-honey roll, rolled bee-side because that
            //    is where the random stream lives - a `std::unique` or a set
            //    would silently delete that rule, and would also merge two bees
            //    arriving in the same tick into one delivery.
            // 2. *The id is read back from the world every pass*, so two
            //    deliveries to one hive in one frame see each other's write.
            // 3. *`beeHomeAtLevel`, never `beehiveAtLevel(beehiveFacing(...))`.*
            //    The second reads perfectly and rebuilds every natural nest as a
            //    crafted hive; `Block.hpp` keeps it as a named control under a
            //    `static_assert` that rejects it. Same trap as the shear site.
            // 4. *Full hives are skipped rather than clamped.* `beeHomeAtLevel`
            //    clamps anyway, but writing an unchanged id would still dirty
            //    the chunk and pay for a remesh every time a bee came home to a
            //    hive nobody had sheared.
            //
            // And the drain is unconditional, exactly as `takeGrazed` is: the
            // queue is `std::exchange`d, so a frame that skipped it would drop
            // deliveries rather than defer them.
            for (const glm::ivec3& cell : creatures.takePollinated()) {
                const game::BlockId home = world.blockAt(cell.x, cell.y, cell.z);
                if (!game::isBeehive(home)) {
                    continue;  // mined, burnt or blown up while the bee was out
                }
                const int level = game::beehiveHoneyLevel(home);
                if (level >= game::kBeehiveFullHoney) {
                    continue;  // already full; the reference drops the delivery too
                }
                world.setBlock(cell.x, cell.y, cell.z, game::beeHomeAtLevel(home, level + 1));
            }

            // An archer's arrows and a witch's bottles, on the same handover:
            // `Creatures` works out where and how fast, and the loop that owns
            // projectiles fires them. **Never collectable** - the reference
            // refuses a mob's arrow even in creative, or a skeleton is an arrow
            // farm, and the same goes for a thrown potion.
            //
            // The kind is mapped here rather than carried, because
            // `Creature.hpp` deliberately does not know `ProjectileKind`
            // exists. This `switch` is the whole cost of that.
            for (const game::Creatures::Launch& shot : creatures.takeLaunches()) {
                const game::ProjectileKind kind =
                    shot.kind == game::Creatures::LaunchKind::SplashPotion
                        ? game::ProjectileKind::SplashPotion
                        : game::ProjectileKind::Arrow;
                projectiles.spawn(kind, shot.origin, shot.velocity, false, false, shot.payload,
                                  shot.fromId);
            }
            landings.clear();
            for (const game::FallingBlocks::Crushed& hit :
                 fallingBlocks.update(world, deltaSeconds, &landings)) {
                // **One channel, two questions, and `hit.block` says which.**
                // `applyFallingLanding` reports either the *occupant* a landing
                // displaced or the *faller itself* when it broke, and never both
                // for one landing - so whichever arm sent it, `hit.block` is
                // exactly the one thing that owes an item. That is what makes
                // the count conserved rather than a coincidence.
                //
                // **What it does not say is which arm, and the two owe different
                // items.** That is `payForCrushed`'s question, shared with the
                // settle-on-shutdown drain because both of them were getting it
                // wrong in the same way: a comment right here used to claim "the
                // gravel that pops this way drops gravel, never flint, because
                // nothing was swung at it", and a probe measured 9,942 flint in
                // 100,000. The claim was the intent and the code was the mining
                // table. Finding 888.
                //
                // **The torch trick is live, and it is the second arm.** On
                // Bedrock a faller coming to rest in a cell it may not replace
                // breaks and drops itself while the occupant stands
                // (https://minecraft.wiki/w/Sand) - put a torch under a sand
                // column and the whole column pops into items. Coming to rest on
                // a slab, soul sand, farmland or a dirt path is the same arm
                // reached from the support side.
                //
                // Findings 618 and 286, and they are closed in
                // `FallingBlock.hpp` - `landsAsABlock` is the predicate and the
                // `static_assert`s under it are the proof. The note that stood
                // here calling that work outstanding, and warning that this loop
                // paid the occupant unconditionally, was true when it was
                // written and stopped being true without anything touching this
                // line.
                payForCrushed(hit);
            }

            // **An anvil hurts whatever it lands on, and nothing was passing
            // `landings`, so it hurt nothing at all.** `update` fills that
            // vector only when it is given somewhere to put it, which made the
            // entire landing-damage path unreachable - twenty cells of anvil
            // onto a player's head did exactly nothing. This is the missing
            // call site, not a new feature: the ladder, the cap and the free
            // first cell all already existed in `anvilLandingDamage`, checked
            // against https://minecraft.wiki/w/Anvil#Falling_anvils at eight
            // heights.
            //
            // **No second "is this an anvil" test here on purpose.**
            // `anvilLandingDamage` answers 0 for every other block, which is how
            // falling gravel stays harmless without a caller having to remember
            // that it should - and how the two damaged anvil states stay
            // dangerous without anyone having to list them.
            //
            // **The helmet reduction is deliberately absent, and that is now a
            // decision rather than a deferral.** The reference is widely said to
            // take a quarter off an anvil's damage for a worn helmet and charge
            // that helmet double durability for it, and the two are one rule -
            // taking the quarter without the wear is the half-a-rule shape
            // `CLAUDE.md` records as #5. Armour's owner took the question on
            // 2026-08-19 and answered **neither half**, which `Survival.hpp`
            // sources at length: minecraft.wiki's *Anvil* and *Damage* pages
            // contradict each other on it, both cite one Java-only talk-page
            // archive (MC- tickets, decompiled Java, the `damages_helmet` tag
            // that does not exist in Bedrock), the Java code that archive quotes
            // applies the 0.75 *after* the damage has been dealt, and there is
            // no MCPE ticket, no Bedrock changelog entry and nothing in
            // `Mojang/bedrock-samples`. A falling anvil is armour-reducible by
            // the ordinary rule and that is all it gets.
            //
            // **A correction to what this comment used to say, because it was
            // the dangerous kind of wrong - confident, in the house voice, and
            // arguing for an edit.** It claimed `armourDefence` "has no callers
            // anywhere in the tree", which a reader could reasonably have acted
            // on by deleting it. As of 2026-08-19 that is false: it is called
            // from `Inventory.hpp`'s `armourSet()` and from `survival::`'s own
            // set arithmetic. The note was written with a `name(` search, and
            // this codebase puts plenty of live names where that search cannot
            // see them - a bare-name second pass is what found the callers.
            //
            // **What was actually true, and is no longer:** ordinary damage
            // ignored armour, because nothing assigned `PlayerInput::armour` and
            // `Main.cpp`'s `hurtPlayer` reached `damagePlayer`'s defaulted
            // `kNoArmour` on every combat path. Both are wired now, and
            // `hurtPlayer` has lost its default so the compiler names any site
            // that forgets.
            //
            // **The player and every creature in the same cube.**
            // `Creatures::hurtInBox` landed, and the seam this comment used to
            // describe was never real - damaging a creature touches the roster
            // alone, so the `const World&` it is handed is beside the point.
            if (!landings.empty()) {
                constexpr float kHalfWidth = game::player_constants::kWidth * 0.5f;
                const glm::vec3 low = player.position - glm::vec3{kHalfWidth, 0.0f, kHalfWidth};
                const glm::vec3 high =
                    player.position + glm::vec3{kHalfWidth, player.height(), kHalfWidth};
                for (const game::FallingBlocks::Landed& landing : landings) {
                    const int damage = game::anvilLandingDamage(landing.block, landing.fellCells);
                    if (damage <= 0) {
                        continue;
                    }
                    const glm::vec3 cellLow{landing.cell};
                    const glm::vec3 cellHigh = cellLow + glm::vec3{1.0f};
                    if (low.x < cellHigh.x && high.x > cellLow.x && low.y < cellHigh.y &&
                        high.y > cellLow.y && low.z < cellHigh.z && high.z > cellLow.z) {
                        // `hurtPlayer` owns creative immunity and `damagePlayer`
                        // under it owns the half-second window, so a collapsing
                        // stack of anvils lands as one hit rather than as ten.
                        //
                        // **Reduced by the ordinary rule and by nothing else.**
                        // The reference is widely said to give a helmet an
                        // extra 25% off a falling anvil at double durability;
                        // `Survival.hpp` went looking for that and found
                        // minecraft.wiki contradicting itself, both sides
                        // citing one Java-only talk-page archive, the Java code
                        // that archive quotes applying the multiplier *after*
                        // the damage is dealt, and nothing at all in
                        // `Mojang/bedrock-samples`. **Neither half is taken** -
                        // splitting them would be bug shape #5, and taking both
                        // on that evidence would be inventing a Bedrock number.
                        hurtPlayer(damage, inventory.armourSet());
                    }
                    // **The same box, on purpose.** A blow that flattens you and
                    // spares the pig standing beside you is worse than one that
                    // misses both, which is the rule `applyLightning` already
                    // states in its own comment. The two overlap tests were
                    // transcribed and compared over four million random boxes
                    // with no disagreement, including 593,948 exact
                    // face-touching cases - the only boundary where they could
                    // differ.
                    //
                    // **Still the caller's damage ladder.** `anvilLandingDamage`
                    // stays the sole owner and answers 0 for everything else, so
                    // gravel is harmless here without `hurtInBox` having to know
                    // that gravel exists.
                    //
                    // **A recorded divergence rather than a silent one:** the
                    // reference hurts what a falling block passes *through*, not
                    // only what is standing where it lands. Our player path does
                    // not do that either, and doing it for creatures alone would
                    // manufacture exactly the asymmetry the first paragraph
                    // rejects. Faithful swept damage wants a per-tick overlap
                    // inside `FallingBlocks::update`, which is not this file.
                    creatures.hurtInBox(cellLow, cellHigh, damage,
                                        game::DeathCause::CrushedByBlock);
                }
            }

            // Dropped items live entirely on the main thread: there are a
            // handful of them and they touch the world only to read it.
            drops.update(world, player.position, deltaSeconds);
            // **The projectile system cannot see the player, so it asks.** The
            // player is not in the creature roster, which is exactly why four
            // archer species have been shooting straight through you since they
            // arrived: `Projectiles::update` tested the roster and nothing else.
            // This lambda answers one geometric question - how far along the
            // remaining segment the player's own box is entered - and applies
            // nothing, so damage, the half-second immunity window and creative
            // immunity all stay where they already live.
            const auto playerReach = [&player](const glm::vec3& from, const glm::vec3& direction,
                                               float reach) {
                const float halfWidth = game::player_constants::kWidth * 0.5f;
                const float height = player.sneaking ? game::player_constants::kSneakHeight
                                                     : game::player_constants::kHeight;
                // `position` is the feet, which is why the box starts there
                // rather than being centred on it.
                //
                // **The box is EXACT, and the arrow has no width here - which
                // is an open question rather than a settled one, measured
                // 2026-08-19.** A creature's box is inflated by `kAimPadding`
                // (0.15) inside `Creatures::findAimed`, the player's by
                // nothing, and `ProjectileSpecies::halfWidth` - the field that
                // looks like it owns the answer - is 0.125 with five writers
                // and no readers at all. So the same arrow is fat against a
                // sheep and a point against you: one that visually clips your
                // shoulder passes through, while yours that clips a sheep
                // lands.
                //
                // **Deliberately not fixed here, because every fix available
                // from this file is the wrong one.** The callback is handed
                // `(from, direction, reach)` and nothing else, so reaching the
                // species means changing a signature in `Projectile.hpp`; and
                // writing 0.125 into this lambda instead would give one value
                // two owners, which is the exact shape that produced the
                // problem. There is also a live counter-hypothesis worth more
                // than a guess: `kAimPadding`'s own doc says it compensates for
                // a creature's `halfWidth` being narrower than its model, which
                // would make it a player-AIM allowance rather than a projectile
                // extent - and `player_constants::kWidth` is the reference's
                // full 0.6, needing no such compensation. Under that reading
                // this box is right and only the dead field is wrong.
                //
                // **Falsified by**: this callback growing a species or extent
                // parameter, or `halfWidth` acquiring its first reader.
                const glm::vec3 boxMin{player.position.x - halfWidth, player.position.y,
                                       player.position.z - halfWidth};
                const glm::vec3 boxMax{player.position.x + halfWidth, player.position.y + height,
                                       player.position.z + halfWidth};
                return game::segmentEntersBox(from, direction, reach, boxMin, boxMax);
            };
            projectiles.update(world, creatures, deltaSeconds, playerReach);
            for (const game::Projectiles::PlayerHit& hit : projectiles.takePlayerHits()) {
                // Creative takes the hit and not the damage, which is the rule
                // every other source of harm here already follows - and now
                // actually does, because `hurtPlayer` owns the gate rather than
                // four of the seven call sites restating it.
                //
                // **No knockback**, deliberately: `PlayerHit::direction` carries
                // the heading for whoever wants it, but this project has no
                // sourced number for an arrow's push and inventing one is
                // forbidden. Damage and the immunity window are the whole of it
                // until a real figure exists.
                //
                // **Reduced by armour** - `Survival.hpp`'s "reduced" list names
                // projectiles, and this was one of the five combat paths that
                // reached `damagePlayer`'s defaulted `kNoArmour`.
                hurtPlayer(hit.damage, inventory.armourSet());
                // **The thud of being hit by a shot**, which is a different
                // event from the damage that follows it - `Hurt` fires off the
                // health edge above and says nothing about *what* struck you.
                // `BowHit` was staged, decoded and never named, so the one
                // ranged attack in the game landed in silence. Unplaced,
                // because it happened to you rather than near you.
                sounds.playGlobal(audio, game::SoundEvent::BowHit, 0.8f);
                game::playRumble(rumble, game::RumbleEvent::Hurt,
                                 game::rumbleStrength(static_cast<float>(hit.damage), 1.0f, 10.0f));
            }

            // **Drained unconditionally**, like its two siblings: nothing in
            // this queue expires on its own, so a frame that skipped it would
            // leave the vector growing for the life of the session.
            //
            // The damage is already done - `Projectiles` called
            // `Creatures::strike` and this is only the notification - so the
            // whole of what happens here is the noise, which is the half of
            // ranged combat that was missing. A shot that connected sounded
            // exactly like a shot that sailed past.
            //
            // **The sound is placed rather than global**, because unlike a hit
            // on the player this happened over there. `position` is the near
            // end of the segment the strike was found in rather than the
            // impact point, and `reach` is exactly the error bound on that -
            // the midpoint halves it, and at one tick of arrow flight the worst
            // case is a metre or so, well inside what a placed sound resolves.
            //
            // **`BowHit` unconditionally is right only while the arrow is the
            // one kind that can strike**, which it is: `Projectile.cpp` gates
            // the whole entity test on `damagePerSpeed > 0`, and the arrow is
            // the only species with a non-zero one. Give a second kind damage
            // and this needs to pick its sound off `strike.kind`, the way the
            // landings loop below already does.
            for (const game::Projectiles::CreatureStrike& strike :
                 projectiles.takeCreatureStrikes()) {
                sounds.play(audio, game::SoundEvent::BowHit,
                            strike.position + strike.direction * (strike.reach * 0.5f), 0.8f);
            }

            // A shot that reports where it stopped. **What to do about it lives
            // here**, not in the projectile system, which reads the world and
            // never writes it.
            // What a thrown potion does where it lands, and what a cloud left
            // by one keeps doing. **Both are applied here** rather than in the
            // projectile system, which reads the world and never writes it.
            //
            // `scale` is how much of the brew reaches you: a splash falls off
            // linearly to nothing four blocks out, and a cloud applies a
            // quarter of the duration once a second.
            const auto applyBrew = [&](game::ItemId potion, float scale) {
                const game::PotionKind brew = game::potionKind(potion);
                grantEffect(brew.effect, brew.amplifier, brew.seconds, scale);
                grantEffect(brew.second, brew.secondAmplifier, brew.seconds, scale);
            };

            // Clouds, ticked before the landings that make them so a cloud born
            // this frame gets its first application next frame rather than
            // twice over.
            for (std::size_t i = 0; i < lingeringClouds.size();) {
                LingeringCloud& cloud = lingeringClouds[i];
                cloud.secondsLeft -= deltaSeconds;
                if (cloud.secondsLeft <= 0.0f) {
                    cloud = lingeringClouds.back();
                    lingeringClouds.pop_back();
                    continue;
                }
                // It shrinks as it goes, which is what makes standing at the
                // edge of an old one safe.
                const float radius =
                    kCloudRadius * (0.4f + 0.6f * cloud.secondsLeft / kCloudSeconds);
                cloud.applyTimer += deltaSeconds;
                if (cloud.applyTimer >= kCloudInterval) {
                    cloud.applyTimer -= kCloudInterval;
                    if (glm::distance(cloud.position, player.eyePosition()) <= radius) {
                        applyBrew(cloud.potion, kLingeringScale);
                    }
                    if (settings.particles) {
                        particles.spawnEat(cloud.position, glm::vec3{0.0f, 0.2f, 0.0f},
                                           static_cast<float>(
                                               game::itemTextureLayer(cloud.potion)),
                                           4);
                    }
                }
                ++i;
            }

            for (const game::Projectiles::Landing& landing : projectiles.takeLandings()) {
                const glm::ivec3 cell{static_cast<int>(std::floor(landing.position.x)),
                                      static_cast<int>(std::floor(landing.position.y)),
                                      static_cast<int>(std::floor(landing.position.z))};
                const glm::vec2 centre{static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.z) + 0.5f};

                // An arrow biting into wood, a bottle shattering, and everything
                // else bursting. The reference splits these the same way and it
                // is most of how you know whether a shot stuck or a snowball
                // burst. A bottle wants `random.glass`, which we already stage
                // as the glass digging sound - the same three recordings the
                // reference plays, so a potion needs no event of its own.
                const bool bottle = landing.kind == game::ProjectileKind::SplashPotion ||
                                    landing.kind == game::ProjectileKind::LingeringPotion;
                sounds.play(audio,
                            landing.kind == game::ProjectileKind::Arrow ? game::SoundEvent::HitLand
                            : bottle                                   ? game::SoundEvent::DigGlass
                                                                       : game::SoundEvent::Pop,
                            landing.position, 0.7f);

                if (landing.kind == game::ProjectileKind::SplashPotion) {
                    // Straight onto whoever is inside four blocks, weaker the
                    // further out they are - the reference's own falloff.
                    const float away = glm::distance(landing.position, player.eyePosition());
                    applyBrew(landing.payload, 1.0f - away / kSplashRadius);
                    if (settings.particles) {
                        particles.spawnEat(landing.position, glm::vec3{0.0f, 1.0f, 0.0f},
                                           static_cast<float>(
                                               game::itemTextureLayer(landing.payload)),
                                           12);
                    }
                    continue;
                }
                if (landing.kind == game::ProjectileKind::LingeringPotion) {
                    // A cloud rather than a splash. It is the whole difference
                    // between the two forms, and the reason a lingering potion
                    // is worth the dragon's breath it costs.
                    lingeringClouds.push_back(
                        {landing.position, landing.payload, kCloudSeconds, 0.0f});
                    continue;
                }

                if (landing.kind == game::ProjectileKind::Egg) {
                    // `egg.json` publishes these as absolute odds rather than
                    // nested ones: `first_spawn_chance: 8` with
                    // `first_spawn_count: 1`, `second_spawn_chance: 32` with
                    // `second_spawn_count: 4`. Both tests read the same roll, so
                    // the second is one throw in thirty-two overall rather than
                    // one in four of the survivors - phrasing it as a quarter
                    // invites `% 4u`, which is true of every multiple of eight
                    // and would hatch four birds every single time.
                    // Source: Mojang's own published Bedrock behaviour packs
                    // (`Mojang/bedrock-samples`, `behavior_pack/entities/`),
                    // fetched and checked field by field rather than recalled.
                    blastRandom ^= blastRandom << 13;
                    blastRandom ^= blastRandom >> 17;
                    blastRandom ^= blastRandom << 5;
                    if (blastRandom % 8u != 0u) {
                        continue;
                    }
                    const int hatched = blastRandom % 32u == 0u ? 4 : 1;
                    for (int i = 0; i < hatched; ++i) {
                        creatures.place(game::CreatureKind::Chicken,
                                        glm::vec3{centre.x, static_cast<float>(cell.y), centre.y},
                                        static_cast<float>(i) * 1.57f);
                    }
                    engine::logInfo("An egg hatched " + std::to_string(hatched) + " chicken(s)");
                    continue;
                }

                // A pearl that has landed moves whoever threw it. **The body is
                // wider and taller than the pearl**, so the cell it stopped in
                // is a starting guess rather than the answer - anything that
                // still overlaps is walked upward until it fits, and a throw
                // with nowhere to stand is spent without moving you rather than
                // melding you into the wall it hit.
                constexpr float kHalf = game::player_constants::kWidth * 0.5f;
                const auto fits = [&](const glm::vec3& feet) {
                    const game::Aabb body{feet - glm::vec3{kHalf, 0.0f, kHalf},
                                          feet + glm::vec3{kHalf, game::player_constants::kHeight, kHalf}};
                    return !game::overlapsSolid(world, body);
                };

                bool moved = false;
                for (int lift = 0; lift < kPearlLandingLift && !moved; ++lift) {
                    const float y = static_cast<float>(cell.y + lift);
                    // Where it actually stopped first, then the middle of that
                    // cell - which is what saves a throw that clipped a corner.
                    const glm::vec3 tries[2]{{landing.position.x, y, landing.position.z},
                                             {centre.x, y, centre.y}};
                    for (const glm::vec3& feet : tries) {
                        if (fits(feet)) {
                            player.position = feet;
                            player.velocity = glm::vec3{0.0f};
                            player.onGround = false;
                            moved = true;
                            break;
                        }
                    }
                }
            }
            for (const game::Projectiles::Collectable& ready : projectiles.collectable(player.position)) {
                // **Asked before taking, not after.** `Projectiles` has only
                // `remove`, never a partial `reduce`, so `add`ing first and
                // testing the return put whatever fitted into the bag and left
                // the whole arrow standing in the world - `add` takes what it
                // can and returns the rest. `collectable`'s own contract says
                // the caller decides whether there is room; this is the caller.
                if (!inventory.hasRoomFor(ready.item, ready.count)) {
                    continue; // No room for this one; the next may still fit.
                }
                inventory.add(ready.item, ready.count);
                projectiles.remove(ready.index);
                sounds.playGlobal(audio, game::SoundEvent::Pop, 0.25f);
                hudDirty = true;
                break; // Indices shift as shots are removed.
            }
            for (const game::ItemEntities::Collectable& ready : drops.collectable(player.position)) {
                // Collection is mode-independent, like dropping. Skipping it in
                // creative left anything dropped orbiting the player forever.
                //
                // **Take what fits rather than all or nothing.** `add` already
                // returns whatever would not go in, and the drop keeps exactly
                // that, so testing for room for the whole stack first was the
                // only thing preventing a partial pickup. And because this loop
                // takes one drop per frame and stopped at the first stack it
                // could not swallow whole, that one refusal left every other
                // drop in the world orbiting the player too - which is what
                // being nearly full looked like from the outside.
                const int left = inventory.add(ready.item, ready.count, ready.damage);
                const int taken = ready.count - left;
                if (taken <= 0) {
                    continue; // Genuinely no room for this item; try the next.
                }
                drops.reduce(ready.index, taken);
                sounds.playGlobal(audio, game::SoundEvent::Pop, 0.25f);
                hudDirty = true;
                break; // Indices shift as drops are removed.
            }

            // Rebuilt rather than transformed: world meshes are drawn with an
            // identity model matrix, which is what lets the shader recover
            // normals from world position. The handle is reused rather than
            // recreated, or every frame would retire a GPU buffer.
            //
            // Everything here is culled to the detail distance. It is a drawing
            // limit only - the drops still fall and are still collectable out
            // there, the creatures still think, and the arrows still fly.
            const game::DrawRange drawRange{camera.position, entityDrawDistance()};
            {
                // Two meshes, because a dropped pane of stained glass has to be
                // sorted behind the opaque world for the same reason the placed
                // block is - its art is see-through everywhere, and the opaque
                // pass would discard the centre of it.
                engine::MeshData dropBlended;
                engine::MeshData dropGeometry =
                    drops.buildMesh(world, timeOfDay * 1000.0f, spriteMask, drawRange, &dropBlended);
                if (dropGeometry.empty()) {
                    if (dropMesh != engine::kInvalidMesh) {
                        renderer.removeMesh(dropMesh);
                        dropMesh = engine::kInvalidMesh;
                    }
                } else if (dropMesh == engine::kInvalidMesh) {
                    dropMesh = renderer.addMesh(dropGeometry);
                } else {
                    renderer.updateMesh(dropMesh, dropGeometry);
                }

                if (dropBlended.empty()) {
                    if (dropGlassMesh != engine::kInvalidMesh) {
                        renderer.removeMesh(dropGlassMesh);
                        dropGlassMesh = engine::kInvalidMesh;
                    }
                } else if (dropGlassMesh == engine::kInvalidMesh) {
                    dropGlassMesh = renderer.addMesh(dropBlended, true);
                } else {
                    renderer.updateMesh(dropGlassMesh, dropBlended);
                }
            }

            {
                engine::MeshData fallingGeometry = fallingBlocks.buildMesh(world, drawRange);
                if (fallingGeometry.empty()) {
                    if (fallingMesh != engine::kInvalidMesh) {
                        renderer.removeMesh(fallingMesh);
                        fallingMesh = engine::kInvalidMesh;
                    }
                } else if (fallingMesh == engine::kInvalidMesh) {
                    fallingMesh = renderer.addMesh(fallingGeometry);
                } else {
                    renderer.updateMesh(fallingMesh, fallingGeometry);
                }
            }

            {
                engine::MeshData shotGeometry =
                    projectiles.buildMesh(world, spriteMask, camera.position, drawRange);
                if (shotGeometry.empty()) {
                    if (projectileMesh != engine::kInvalidMesh) {
                        renderer.removeMesh(projectileMesh);
                        projectileMesh = engine::kInvalidMesh;
                    }
                } else if (projectileMesh == engine::kInvalidMesh) {
                    projectileMesh = renderer.addMesh(shotGeometry);
                } else {
                    renderer.updateMesh(projectileMesh, shotGeometry);
                }
            }

            // Creatures move and decide, then a fresh mesh is built for them the
            // same way, and for the same reason. The showcase holds them still,
            // because a model being reviewed should not walk out of frame.
            //
            // **`<= 0`, matching the other three sites** - see the restore.
            const bool night = game::sky::sunDirection(timeOfDay).y < -0.05f;
            if (settings.creatureShowcase <= 0) {
                std::vector<game::CreatureExplosion> blasts;
                const game::CreatureAttack blow =
                    creatures.update(world, player.position, deltaSeconds, night, player.sneaking,
                                     timeOfDay, blasts);
                if (blow.landed) {
                    // The damage was computed and discarded for four milestones
                    // because there was nothing to apply it to. `damagePlayer`
                    // owns the half-second invulnerability window, so a pack
                    // standing on you still only lands twice a second.
                    //
                    // **The blow armour exists for.** A creature's melee is
                    // `entity_attack` in Bedrock's own cause enum and is
                    // reduced; this is the site a playtester will judge the
                    // whole feature by.
                    hurtPlayer(blow.damage, inventory.armourSet());
                    player.velocity += blow.push;
                    player.onGround = false;
                    // **Outside the creative gate on purpose.** Being hit and
                    // being hurt are different things: creative still takes the
                    // blow and still gets shoved, and it is the default mode.
                    //
                    // Sized against ten, not against the golem's worst of 21.
                    // Seven is the hardest an ordinary mob hits, so a range that
                    // reached 21 would squeeze every normal fight into the
                    // bottom third and leave one creature owning the whole top.
                    // The golem instead saturates, which is what it should do.
                    game::playRumble(rumble, game::RumbleEvent::Hurt,
                                     game::rumbleStrength(static_cast<float>(blow.damage), 1.0f, 10.0f));
                }

                // Only the strongest blast of a frame throws the player.
                glm::vec3 blastPush{0.0f};
                float strongestBlast = 0.0f;
                int blastDamage = 0;
                // **Two flags rather than reading `strongestBlast > 0`, because
                // zero impact is a real answer and not an absence.** A player
                // fully behind cover, inside twice the power, gets an impact of
                // exactly zero and the reference still charges them one point -
                // `explosionDamage`'s trailing `+ 1` *is* that floor, and it
                // says so over itself. This block had three separate gates that
                // each threw the floor away: `impact > strongestBlast` against a
                // `strongestBlast` starting at 0, so a zero-impact blast never
                // became the strongest of the frame; the `reach > 0.001f` guard,
                // which is about the *push* and had the damage assignment inside
                // it; and the `strongestBlast > 0.0f` test at the foot. Opening
                // only the first, which is what finding 1039 proposed, is a
                // silent no-op. `blastCaught` answers "did any blast reach you",
                // `blastThrows` answers "and could it move you" - separately,
                // because a sheltered hit hurts without shoving.
                bool blastCaught = false;
                bool blastThrows = false;

                // Charges that finished their fuse join the creature blasts, so
                // the whole destroy-spill-drop path below is shared rather than
                // written twice. Power 4 is the reference's TNT.
                //
                // **Where the two kinds part company, recorded before they are
                // mixed.** A charge drops everything it breaks and a creeper
                // drops one block in `power`, so the roll below has to know
                // which it is - and `CreatureExplosion` is `{ centre, power }`
                // with no room to say. `Explosion.hpp` states the rule and
                // spells out this exact blocker; the answer is that it is not a
                // blocker from here, because this file owns the vector and owns
                // the order. Everything the creature system produced above is a
                // creeper, and the only `push_back` in the function is the one
                // immediately below, so the boundary index is the whole answer.
                //
                // **What would make this false**, since an index is a fragile
                // thing to lean on: a second producer appending to `blasts`
                // after this line, or `creatures.update` being moved below it.
                // Both are visible in one grep for `blasts` - there are five
                // mentions in the file and this comment names all of them.
                const std::size_t creeperBlasts = blasts.size();
                for (const glm::ivec3& cell : world.takeDetonations()) {
                    blasts.push_back({glm::vec3{cell} + glm::vec3{0.5f}, kTntPower});
                }

                for (std::size_t blastIndex = 0; blastIndex < blasts.size(); ++blastIndex) {
                    const game::CreatureExplosion& blast = blasts[blastIndex];
                    const bool fromTnt = blastIndex >= creeperBlasts;
                    // Applied here rather than inside the creature system, which
                    // reads the world and never writes it.
                    blastRandom ^= blastRandom << 13;
                    blastRandom ^= blastRandom >> 17;
                    blastRandom ^= blastRandom << 5;
                    const std::vector<glm::ivec3> destroyed =
                        game::explosionBlocks(world, blast.centre, blast.power, blastRandom);
                    int dropped = 0;
                    for (const glm::ivec3& cell : destroyed) {
                        const game::BlockId removed = world.blockAt(cell.x, cell.y, cell.z);
                        // **A charge in a blast is lit, not destroyed** - the
                        // chain reaction, which is most of what TNT is for. It
                        // was being cleared to Air and spilled as an item, so a
                        // stack of charges was disarmed by the first one going
                        // off and handed back to you
                        // (https://minecraft.wiki/w/TNT).
                        //
                        // **`isTntBlock`, not `== Tnt`**, because a lit charge
                        // spends half its fuse showing the plain `Tnt` face:
                        // `World::updateTnt` blinks the id back and forth, so
                        // the two states are not "unlit" and "lit" and naming
                        // one of them is the trap that predicate exists for.
                        //
                        // **The short fuse, drawn per charge.** The reference
                        // gives a charge set off by another blast a random
                        // 10-30 ticks against the 80 a hand-lit one gets
                        // (https://minecraft.wiki/w/TNT), which is what makes a
                        // stack go up as one cascade rather than a slow ripple.
                        // The range and the tick conversion both live at
                        // `World::blastFuse`, which is why it is called rather
                        // than a number being written here - and it is called
                        // **inside the loop**, because it advances the world's
                        // PRNG and one draw shared across the pile would give
                        // every charge the same fuse, which is the simultaneity
                        // the randomisation exists to break.
                        //
                        // **Every charge in the blast is primed, lit or not**,
                        // and that is the fix rather than an oversight. There is
                        // no way to ask a cell whether it is already counting
                        // down - `m_tntFuses` is private and the block id is the
                        // blink, not the state - so gating this on the id meant
                        // an already-lit charge kept its four-second fuse or was
                        // shortened to one *depending on which face it happened
                        // to be showing that instant*. A coin flip on a
                        // rendering phase, deciding a gameplay number. Calling
                        // unconditionally is safe because `primeTnt` shortens or
                        // does nothing, never restarts and never queues a second
                        // countdown - so the re-prime rule has exactly one owner,
                        // and it is not this call site.
                        //
                        // The cost is that a charge already lit can play a second
                        // `Fuse`. That is the right side of the trade: the only
                        // way to suppress it is the same unreliable id read, and
                        // a duplicate cue under an explosion is cheaper than a
                        // fuse length decided by a blink.
                        if (game::isTntBlock(removed)) {
                            // Drawn into a local first: two non-`const` calls on
                            // `world` in one expression is well defined but reads
                            // as if it might not be, and this line is already
                            // carrying enough.
                            const auto fuse = world.blastFuse();
                            world.setBlock(cell.x, cell.y, cell.z, game::BlockId::TntPrimed);
                            world.primeTnt(cell, fuse);
                            sounds.play(audio, game::SoundEvent::Fuse,
                                        glm::vec3{cell} + glm::vec3{0.5f}, 0.9f);
                            continue;
                        }
                        // **Rolled before the cell is cleared.** A village chest
                        // caught in a blast owes its table to its block id, and
                        // Air remembers nothing - so this has to happen above
                        // the `setBlock`, not with the spill below it, or the
                        // chest bursts empty.
                        if (game::isLootChest(removed)) {
                            materialise(cell);
                        }
                        world.setBlock(cell.x, cell.y, cell.z, game::BlockId::Air);
                        // Which stored contents the item this cell drops will
                        // carry, or 0 for everything that is not a stowbox with
                        // something in it. The break path's own `brokenStowHandle`,
                        // because a blast destroys a container exactly as
                        // thoroughly as a pickaxe does.
                        int blastStowHandle = 0;

                        // A furnace caught in the blast still spills what was
                        // inside it, exactly as breaking one does.
                        if (game::isFurnace(removed)) {
                            const auto found = furnaces.find(cell);
                            if (found != furnaces.end()) {
                                const glm::vec3 centre = glm::vec3{cell} + glm::vec3{0.5f};
                                for (const game::ItemStack* stack : {&found->second.input,
                                                                     &found->second.fuel,
                                                                     &found->second.output}) {
                                    if (!stack->empty()) {
                                        game::dropStack(drops, centre, *stack);
                                    }
                                }
                                furnaces.erase(found);
                            }
                        }
                        // And a campfire caught in one, on the same rule and
                        // through the same single owner the break path calls.
                        if (game::isCampfire(removed)) {
                            spillCampfire(cell);
                        }
                        // And a jukebox, for the same reason and out of the
                        // same owner - a blast is the other half of every rule
                        // the break path has.
                        if (removed == game::BlockId::Jukebox) {
                            spillJukeboxDisc(cell);
                        }
                        // **All three, matching the break path.** This asked
                        // only `isChest`, so a blown-up hopper or lectern lost
                        // its contents outright *and* left its entry standing in
                        // `chests` keyed at that cell - so the next container
                        // built on that exact block inherited them.
                        if (game::isChest(removed) || game::isHopper(removed) ||
                            removed == game::BlockId::Lectern) {
                            const auto found = chests.find(cell);
                            if (found != chests.end()) {
                                const glm::vec3 centre = glm::vec3{cell} + glm::vec3{0.5f};
                                // A stowbox keeps its contents through a blast,
                                // as the reference's does - they move onto the
                                // item rather than falling out.
                                if (game::isStowbox(removed)) {
                                    if (!found->second.empty()) {
                                        blastStowHandle = nextStowHandle++;
                                        stowed.emplace(blastStowHandle, found->second);
                                    }
                                } else {
                                    for (const game::ItemStack& stack : found->second.slots) {
                                        if (!stack.empty()) {
                                            game::dropStack(drops, centre, stack);
                                        }
                                    }
                                }
                                chests.erase(found);
                            }
                        }

                        // Whatever was blown up has already spilled its contents
                        // as drops, so a screen still open on it is showing a
                        // copy the player could take a second time. **Every
                        // screen kind, through the one owner** - this asked only
                        // about a furnace and a container, so a blast that took
                        // the crafting table out from under you left its grid up
                        // and working.
                        if (screenLooksAt(cell)) {
                            closeScreen();
                        }

                        // **How much of what a blast breaks comes back, and it
                        // is not one rule but three.** A creeper leaves one
                        // block in `power`, which is what makes it a net loss
                        // rather than a mining technique; a charge drops
                        // everything it breaks; and a dragon egg, a beacon and
                        // a conduit always survive whatever set them off.
                        //
                        // All three live in `blast::explosionDropChance`, in
                        // `Explosion.hpp`, next to the four `static_assert`s
                        // that pin them. This site used to multiply the roll by
                        // the blast power and test that against one, which is
                        // the creeper's rule applied to all three cases - so a
                        // charge returned a quarter of what it broke and a
                        // dragon egg was destroyed outright two times in three.
                        // Measured over two million rolls against the real
                        // header: a 26-block charge yielded 6.5 and now yields
                        // 26, while the creeper at 0.3340 and the charged
                        // creeper at 0.1665 are bit-identical either side of
                        // the change, which is the half of this that had to not
                        // move.
                        //
                        // **The old expression is described above and not
                        // quoted, deliberately.** `Explosion.hpp` tells a later
                        // reader they can confirm this adoption in one grep, by
                        // that expression having left this file. Spelling it
                        // out here - which the first draft of this comment did
                        // - keeps the string alive and turns their check into a
                        // false negative. Do not helpfully paste it back in.
                        blastRandom ^= blastRandom << 13;
                        blastRandom ^= blastRandom >> 17;
                        blastRandom ^= blastRandom << 5;
                        const float roll = static_cast<float>(blastRandom & 0xFFFFFFu) /
                                           static_cast<float>(0x1000000u);
                        // **A loaded stowbox is exempt from the roll**, and the
                        // roll is still drawn so the blast's random stream does
                        // not depend on what it hit. Its contents live behind a
                        // handle that exists only on the item, so losing the item
                        // to the roll would strand them in `stowboxes.dat` with
                        // nothing left that could ever open them - the same leak
                        // the death drop had. **The break path guards the same
                        // way now**; it allocated the handle unconditionally and
                        // then let `yieldsDrop` decide whether anything would ever
                        // carry it.
                        if (blastStowHandle != 0 ||
                            roll < game::blast::explosionDropChance(removed, blast.power,
                                                                    fromTnt)) {
                            // **A blast drops what the block's own loot table
                            // gives with NO tool in hand, and skips the harvest
                            // gate a player would face.** `Pickaxe` is how that
                            // is spelled here, and the previous wording of this
                            // comment - "as an unenchanted diamond tool would" -
                            // was wrong in a way that argues for a breaking
                            // edit, which is the most expensive kind
                            // (CLAUDE.md bug shape #16). Finding 2029 measured
                            // the consequence: 10 of 3314 ids cannot be
                            // harvested by a diamond pickaxe, and 9 of them are
                            // snow, which no part of the old reasoning
                            // described.
                            //
                            // **THREE REFERENCE FACTS PIN THIS, AND ONLY ONE
                            // MODEL SATISFIES ALL THREE:**
                            //   exploded stone   -> cobblestone
                            //   exploded cobweb  -> nothing
                            //   exploded snow    -> nothing
                            // "Explosions harvest with the correct tool" gives
                            // cobweb its string and fails fact 2. "Explosions
                            // harvest with an empty hand, gate and all" gives
                            // stone nothing and fails fact 1. Only "the loot
                            // table with no tool, harvest gate skipped" gives
                            // all three, because stone's table names no tool
                            // while cobweb's requires shears and snow's a
                            // shovel.
                            //
                            // **So the code below is right and it is the
                            // comment that was broken.** Hardcoding `Pickaxe`
                            // reproduces all three: stone passes, cobweb fails,
                            // snow fails.
                            //
                            // **DO NOT "fix" this to `miningRow(removed).tool`.**
                            // Finding 2029 proposes exactly that, and it is the
                            // trap: asking each block for its own required kind
                            // hands a cobweb its shears and starts dropping
                            // string from every exploded web, breaking fact 2 to
                            // repair a snow case that was never broken. It would
                            // also pass any "does exploded snow drop now?" check
                            // written to test it. The 9 snow ids are correct as
                            // they stand.
                            //
                            // Unsettled and honestly so: `Mojang/bedrock-samples`
                            // publishes no `blocks/` and no `loot_tables/blocks/`
                            // (checked against a 404 control - entity loot
                            // tables are present, so the absence is real), so
                            // block behaviour is engine-side and unpublished.
                            // The three facts above are the evidence; if one of
                            // them is ever shown wrong on Bedrock, re-derive
                            // from the set rather than from this line.
                            const game::BreakContext blasted{game::ToolKind::Pickaxe,
                                                             game::kDiamondTier, false, 0, true};
                            dropped += spillBlockDrop(cell, removed, blasted, blastStowHandle) > 0
                                           ? 1
                                           : 0;
                        }

                        // **The same four cleanups a pickaxe pays.** A blast
                        // left floating door tops, half beds and ladders hanging
                        // on nothing, because every one of those rules lived in
                        // the break path and nowhere else. Called after the
                        // spill so a neighbour that comes down is not competing
                        // with this cell's own roll.
                        settleAround(cell, removed);
                    }

                    constexpr float kHalfWidth = game::player_constants::kWidth * 0.5f;
                    const game::Aabb body{
                        player.position - glm::vec3{kHalfWidth, 0.0f, kHalfWidth},
                        player.position + glm::vec3{kHalfWidth, game::player_constants::kHeight,
                                                    kHalfWidth}};
                    // **The cheap question first, and from the one place that
                    // owns it.** `explosionExposure` is about thirty-six DDA
                    // ray walks and it was being paid for every blast at any
                    // distance, only for `explosionImpact` to return zero.
                    // `withinBlast` is the `2 x power` reach `explosionImpact`
                    // already applies internally, exported precisely so the
                    // pre-filter and the real test cannot disagree - the open
                    // -coded radius test that used to be the alternative here
                    // is the second copy that eventually drifts.
                    //
                    // Both stay declared out here because the log line at the
                    // foot of this loop reads them, and zero is the honest
                    // answer for a blast that could not reach: it is what
                    // `explosionImpact` returns past that radius anyway, so the
                    // guard skips the rays rather than changing the result.
                    float exposure = 0.0f;
                    float impact = 0.0f;
                    if (game::withinBlast(blast.centre, blast.power, player.position)) {
                        exposure = game::explosionExposure(world, blast.centre, body);
                        impact = game::explosionImpact(blast.centre, blast.power, player.position,
                                                       exposure);
                        if (!blastCaught || impact > strongestBlast) {
                            // Blocks per tick in the reference; ours is per
                            // second, so twenty times over. Only the strongest
                            // blast of the frame throws you - several each
                            // adding their own is the accumulator bug that once
                            // launched the player clear off the map.
                            //
                            // **`!blastCaught ||` is the floor's first gate.**
                            // Without it a lone blast of exactly zero impact
                            // loses to a `strongestBlast` that starts at zero,
                            // and the sheltered hit registers as no hit at all.
                            blastCaught = true;
                            strongestBlast = impact;
                            blastDamage = game::explosionDamage(blast.power, impact);
                            // Aimed at the eyes rather than the feet, which is
                            // what gives a close blast its upward throw for
                            // free.
                            //
                            // **The push is worked out here and no longer gates
                            // the damage.** `reach > 0.001f` only ever guarded
                            // the division; having the damage assignment inside
                            // it meant a blast centred exactly on your eyes did
                            // nothing at all. The ternary is what keeps that
                            // division unevaluated, so no NaN can escape.
                            const glm::vec3 away = player.eyePosition() - blast.centre;
                            const float reach = glm::length(away);
                            blastThrows = reach > 0.001f && impact > 0.0f;
                            blastPush = blastThrows
                                            ? away / reach * impact * kBlastKnockback
                                            : glm::vec3{0.0f};
                        }
                    }

                    // Everything else in range takes it too. The blocks are the
                    // caller's business and the population is the creature
                    // system's, which is why this is a call rather than a loop.
                    const int caught = creatures.applyExplosion(world, blast.centre, blast.power);
                    sounds.play(audio, game::SoundEvent::Explode, blast.centre, 1.0f, 1.0f);
                    // Falls off with distance the way the noise does, so a blast
                    // across the valley is a tremor and one at your feet is a
                    // shove. Four blocks of reach per unit of power, matching how
                    // far the blast itself is felt.
                    {
                        const float span = std::max(blast.power * 4.0f, 1.0f);
                        const float away = glm::distance(camera.position, blast.centre);
                        const float nearness = std::clamp(1.0f - away / span, 0.0f, 1.0f);
                        // **How big it was and how close you were**, which is
                        // what a blast actually is. Sized against TNT, so
                        // anything at least that big simply reads as maximum -
                        // a charged Bramble's power lives in the creature table
                        // and does not need a second copy over here.
                        //
                        // No floor on the product: a blast far enough away
                        // genuinely should arrive as nothing at all.
                        game::playRumble(rumble, game::RumbleEvent::Explosion,
                                         game::rumbleStrength(blast.power, 1.0f, kTntPower) * nearness);
                    }
                    if (settings.particles) {
                        particles.spawnExplosion(blast.centre, blast.power);
                    }

                    engine::logInfo("Blast at " + std::to_string(static_cast<int>(blast.centre.x)) + ", " +
                                    std::to_string(static_cast<int>(blast.centre.y)) + ", " +
                                    std::to_string(static_cast<int>(blast.centre.z)) + ": " +
                                    std::to_string(destroyed.size()) + " blocks, " +
                                    std::to_string(dropped) + " dropped, " +
                                    std::to_string(caught) + " creatures hit, " +
                                    std::to_string(game::explosionDamage(blast.power, impact)) +
                                    " damage at exposure " + std::to_string(exposure));
                }

                if (blastCaught) {
                    // Damage follows the same "strongest of the frame" rule the
                    // throw does, rather than summing: several charges going off
                    // together should hit as hard as the worst of them, not as
                    // hard as all of them added up.
                    //
                    // **Reduced by armour**, which is `Survival.hpp`'s list
                    // again - an explosion is `entity_explosion`/`block_explosion`
                    // in Bedrock's cause enum and both are ordinary
                    // armour-reducible damage.
                    hurtPlayer(blastDamage, inventory.armourSet());
                    if (blastThrows) {
                        // **Separate from the damage, and the third gate the
                        // floor had to get past.** A blast you were fully
                        // sheltered from hurts you for one and does not move
                        // you, so lifting `onGround` for it would be wrong as
                        // well as pointless - it is what tells the physics you
                        // are airborne.
                        player.velocity += blastPush;
                        player.onGround = false;
                    }
                }

                creatures.manage(world, player.position, deltaSeconds, night);
            }
            {
                engine::MeshData creatureShells;
                // The mask is what turns a held item's picture into a shape, the
                // same way a dropped one and a thrown one already do.
                engine::MeshData creatureGeometry =
                    creatures.buildMesh(world, creatureShells, drawRange, &spriteMask);
                if (creatureGeometry.empty()) {
                    if (creatureMesh != engine::kInvalidMesh) {
                        renderer.removeMesh(creatureMesh);
                        creatureMesh = engine::kInvalidMesh;
                    }
                } else if (creatureMesh == engine::kInvalidMesh) {
                    creatureMesh = renderer.addMesh(creatureGeometry);
                } else {
                    renderer.updateMesh(creatureMesh, creatureGeometry);
                }

                if (creatureShells.empty()) {
                    if (creatureShellMesh != engine::kInvalidMesh) {
                        renderer.removeMesh(creatureShellMesh);
                        creatureShellMesh = engine::kInvalidMesh;
                    }
                } else if (creatureShellMesh == engine::kInvalidMesh) {
                    creatureShellMesh = renderer.addMesh(creatureShells, true);
                } else {
                    renderer.updateMesh(creatureShellMesh, creatureShells);
                }
            }

            std::optional<glm::mat4> highlight;
            if (target.hit) {
                // Read from the world rather than from `target`, which was
                // resolved before this frame's edits: breaking a slab otherwise
                // flashes a full-size cage around the cell just emptied.
                const game::BlockId aimedBlock =
                    world.blockAt(target.block.x, target.block.y, target.block.z);
                const game::BlockBoxes aimed =
                    game::worldSelectionBoxes(world, target.block.x, target.block.y, target.block.z);
                if (aimed.count > 0) {
                    // **The union of the shape's boxes on all three axes.**
                    // This used to measure only the height and assume a full
                    // cell across, so a ladder, a pane, a fence or a torch was
                    // caged as if it were a block - which is what made them
                    // read as blocks with an invisible shell round them.
                    glm::vec3 low{1.0f};
                    glm::vec3 high{0.0f};
                    for (int i = 0; i < aimed.count; ++i) {
                        const game::BlockBox& b = aimed.boxes[i];
                        low = glm::min(low, glm::vec3{b.minX, b.minY, b.minZ});
                        high = glm::max(high, glm::vec3{b.maxX, b.maxY, b.maxZ});
                    }

                    // A joined chest is one container, so it gets one cage. The
                    // origin moves to whichever half is lower on the join axis,
                    // and the box grows along it.
                    glm::ivec3 origin = target.block;
                    glm::vec3 size = high - low;
                    if (game::isChest(aimedBlock)) {
                        if (const std::optional<glm::ivec3> partner = chestPartnerAt(target.block)) {
                            origin = glm::min(target.block, *partner);
                            const glm::ivec3 span = glm::abs(target.block - *partner);
                            size.x += static_cast<float>(span.x);
                            size.z += static_cast<float>(span.z);
                        }
                    }
                    // Same rule for a door and a bed: **breaking either half
                    // takes both**, so caging one of them says the wrong thing
                    // about what you are aiming at. The door grows upward and
                    // the bed along the direction its head lies.
                    if (game::isDoor(aimedBlock)) {
                        if (game::doorIsUpper(aimedBlock)) {
                            origin.y -= 1;
                        }
                        size.y += 1.0f;
                    } else if (game::isBed(aimedBlock)) {
                        const game::FaceDirection lie = game::bedFacing(aimedBlock);
                        const glm::ivec3 step =
                            lie == game::FaceDirection::PosX   ? glm::ivec3{1, 0, 0}
                            : lie == game::FaceDirection::NegX ? glm::ivec3{-1, 0, 0}
                            : lie == game::FaceDirection::PosZ ? glm::ivec3{0, 0, 1}
                                                               : glm::ivec3{0, 0, -1};
                        const glm::ivec3 other =
                            game::bedIsHead(aimedBlock) ? target.block - step : target.block + step;
                        origin = glm::min(target.block, other);
                        size.x += static_cast<float>(std::abs(step.x));
                        size.z += static_cast<float>(std::abs(step.z));
                    }

                    // Rebuilt only when the targeted size changes, which is a
                    // handful of times a session rather than every frame.
                    if (size != outlineSize) {
                        outlineSize = size;
                        renderer.setOverlayMesh(game::makeBlockOutline(size));
                    }
                    highlight = glm::translate(glm::mat4{1.0f}, glm::vec3{origin} + low);
                }
            }

            // The breaking cracks. **Rebuilt only when the picture would
            // actually change** - the stage or the block - because each rebuild
            // retires a buffer, and a mesh per frame for a block that takes a
            // second to dig is a hundred of them for ten pictures.
            //
            // Torn down the moment digging stops, which is the same shape of
            // bug the dig bar already paid for: leave it and the last stage
            // stays painted on a block nobody is touching.
            {
                const int stage = breakProgress > 0.0f && breakingBlock != kNoBlock
                                      ? game::destroyStageLayer(breakProgress)
                                      : -1;
                if (stage != crackStage || breakingBlock != crackBlock) {
                    crackStage = stage;
                    crackBlock = breakingBlock;
                    renderer.setCrackMesh(stage < 0
                                              ? engine::MeshData{}
                                              : game::makeBlockCracks(world, breakingBlock,
                                                                      breakProgress));
                }
            }

            timeOfDay += deltaSeconds / static_cast<float>(settings.dayLengthSeconds);
            if (timeOfDay >= 1.0f) {
                // A new day, so a new phase. Rebuilt here rather than every
                // frame: it is four vertices, and each rebuild retires a buffer.
                moonPhase = (moonPhase + 1) % game::kMoonPhases;
                renderer.setSkyMesh(game::sky::makeMoonQuad(moonPhase), 1);
            }
            timeOfDay -= std::floor(timeOfDay);

            waveSeconds = std::fmod(waveSeconds + deltaSeconds, 100000.0f);
            renderer.setWaterTime(waveSeconds);

            const glm::vec3 sunDirection = game::sky::sunDirection(timeOfDay);
            const game::sky::Sunlight light = game::sky::lighting(timeOfDay);

            // --- Weather -------------------------------------------------
            weather.update(deltaSeconds, settings.weather);
            weather.strike(world, camera.position, deltaSeconds);

            const float rainAmount = weather.rainLevel();
            const float thunderAmount = weather.thunderLevel();
            const float flash = weather.flash();

            // --- Wind ------------------------------------------------------
            // Computed once, before anything reads it, because the rain's
            // slant, the deck's drift and the grass must all use the same
            // vector this frame or they visibly disagree.
            windClock = std::fmod(windClock + deltaSeconds, 10000.0f);
            // Turned slowly rather than pinned due west, so a cloud still
            // roughly tells you which way west is.
            windAngle = std::fmod(windAngle + deltaSeconds * 0.02f, 6.2831853f);
            const glm::vec2 windDirection{std::cos(windAngle), std::sin(windAngle)};
            // **Gusts, not a constant breeze.** Two slow waves multiplied, so
            // the product spends real time at nothing: on a clear day the world
            // stands still and then a gust crosses it, which is what wind
            // actually looks like. A steady sway reads as an animation someone
            // left running.
            const float gust = std::max(0.0f, std::sin(windClock * 0.19f)) *
                               std::max(0.0f, std::sin(windClock * 0.11f + 1.7f));
            // **`Weather` owns the wind; this file owns what each visual does
            // with it.** That split is the fix for a real defect rather than a
            // tidy-up: this line used to read `rain * 4 + thunder * 5`, a second
            // wind model living next to the one in `Weather.cpp`, and
            // `Weather.hpp` says so above `windSpeed()` - *"the defect is two
            // independent wind models, not an absent one, and the visible cost
            // is that they can disagree."*
            //
            // The cost was not hypothetical. Measured over a forced storm and a
            // forced calm, 14,398 frames against the real `Weather.cpp`: the
            // owner's wind swings 1.0 to 12.0 while the cloud deck's rate had
            // **exactly zero variance**, so the correlation between the deck and
            // the slant was not merely poor, it was *undefined* - the deck could
            // not agree or disagree with the wind because it never read it. Both
            // now derive from the one number: exactly 1.000000 below the deck's
            // cap, and 0.916 taken across the whole run including the frames
            // where the cap is saturating, which is the honest figure.
            //
            // Subtracting the calm baseline rather than reading the multiplier
            // raw is what preserves the gust model's whole point: `windSpeed()`
            // rests at exactly 1.0 in a calm, so this rests at exactly 0.0 and a
            // clear day still stands completely still until a gust crosses it.
            // Every clear-weather number here is bit-identical to what it was.
            const float weatherWind = std::max(0.0f, weather.windSpeed() - kCalmWindSpeed);
            const float windStrength = weatherWind + gust * (2.0f + weatherWind * 0.5f);

            // The deck reads the same wind, with its own gain and its own cap -
            // which is exactly what the slant, the bend and the particle push
            // below already do. It sits here rather than up beside the sun so
            // that it is downstream of `weather.update()`; run before it, it
            // would drift on last frame's wind.
            //
            // Wrapped against the field's own period so a long session cannot
            // lose the fraction the noise is sampled at.
            cloudDrift = std::fmod(cloudDrift +
                                       deltaSeconds * kCloudDriftPerSecond *
                                           std::min(weather.windSpeed(), kMaxDeckWindFactor),
                                   100000.0f);
            renderer.setCloudDrift(cloudDrift);

            {
                // A bolt only exists for a few tenths of a second, so this is
                // rebuilt on the frames one appears and cleared once on the
                // frame the last one goes.
                static bool boltShown = false;
                const bool anyBolt = !weather.strikes().empty();
                if (anyBolt) {
                    renderer.setBoltMesh(game::weather::buildBoltMesh(weather.strikes()));
                    for (game::weather::Strike& bolt : weather.strikes()) {
                        if (bolt.resolved) {
                            continue;
                        }
                        bolt.resolved = true;
                        // Delayed by the distance sound actually travels, which
                        // the reference does not do - it plays thunder to the
                        // whole world at once. Ours is the better answer and is
                        // marked as a divergence.
                        const float away = glm::distance(camera.position, bolt.position);
                        thunderQueue.push_back({away / 343.0f, away});
                        // The reference's five points over a 6x12x6 box centred
                        // on the strike. **Through `hurtPlayer`**, which owns
                        // the creative gate - this was the one damage source in
                        // the file that let a creative player be killed by the
                        // weather.
                        //
                        // **Measured from the simulation eye, not the drawn
                        // one.** Whether a bolt hurts you is simulation, and
                        // `camera.position` carries `stepSmooth`: on the exact
                        // frame you step up a block at the edge of the box, the
                        // interpolation was deciding five points of damage.
                        const glm::vec3 struck = player.eyePosition();
                        if (std::abs(struck.x - bolt.position.x) <= 3.0f &&
                            std::abs(struck.z - bolt.position.z) <= 3.0f &&
                            struck.y > bolt.position.y - 3.0f &&
                            struck.y < bolt.position.y + 9.0f) {
                            // **Reduced by armour.** `Survival.hpp`'s list names
                            // lightning among the reduced sources; Bedrock's
                            // cause enum spells it `lightning`, an ordinary
                            // cause with no bypass.
                            hurtPlayer(5, inventory.armourSet());
                        }

                        // What it does to everything that is not the player. A
                        // bolt used to be scenery with a sound.
                        const glm::ivec3 hit{static_cast<int>(std::floor(bolt.position.x)),
                                             static_cast<int>(std::floor(bolt.position.y)),
                                             static_cast<int>(std::floor(bolt.position.z))};
                        if (world.blockAt(hit.x, hit.y, hit.z) == game::BlockId::Air &&
                            world.isSolid(hit.x, hit.y - 1, hit.z)) {
                            // Safe to light because the storm that threw the
                            // bolt is already putting fires out - see
                            // `World::setPrecipitating`.
                            world.setBlock(hit.x, hit.y, hit.z, game::BlockId::Fire);
                        }
                        creatures.applyLightning(bolt.position);
                        if (settings.particles) {
                            particles.spawnSmoke(bolt.position + glm::vec3{0.5f, 0.4f, 0.5f}, 0.09f,
                                                 0.8f, 1.6f);
                        }
                    }
                } else if (boltShown) {
                    renderer.setBoltMesh(engine::MeshData{});
                }
                boltShown = anyBolt;
            }

            // Thunder arrives after its flash. Volume falls off with distance
            // because a strike on the horizon should be a rumble, not a crack.
            for (std::size_t i = 0; i < thunderQueue.size();) {
                thunderQueue[i].delay -= deltaSeconds;
                if (thunderQueue[i].delay <= 0.0f) {
                    const float near = std::clamp(1.0f - thunderQueue[i].distance / 220.0f, 0.15f, 1.0f);
                    sounds.playGlobal(audio, game::SoundEvent::Thunder, near,
                                      0.7f + 0.3f * near);
                    thunderQueue.erase(thunderQueue.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }
                ++i;
            }

            {
                const auto column = glm::ivec2{static_cast<int>(std::floor(camera.position.x)),
                                               static_cast<int>(std::floor(camera.position.z))};
                const auto biome =
                    game::sampleBiome(kWorldSeed, column.x, column.y).dominant;
                const int surface = std::max(world.highestSolid(column.x, column.y), 0);
                const auto kind = game::weather::precipitationFor(biome, surface);
                const bool falls = kind != game::weather::Precipitation::None && rainAmount > 0.01f;
                const float level = falls ? rainAmount : 0.0f;
                // **The wet bit is rain only - and `falls` above deliberately
                // stays wider than it.** This drives two rules in `World.cpp`,
                // both of which mean water: farmland hydration at `isHydrated`
                // and the open-sky fire quench in `updateFire`. `Weather.hpp`
                // carries the cross-read above the `Precipitation` enum -
                // [[Rain]] and [[Farmland]] give the wet effects to rain, and
                // [[Snowfall]] draws the contrast outright, *"Unlike with rain,
                // any entities that are on fire are not extinguished on contact
                // with snow."* Asking `!= None` here opted every snowy biome
                // into the wet rules, so a snowstorm put out open fires and
                // watered fields.
                //
                // **Wider than "the snowy biomes", measured against the real
                // `precipitationFor` and the real biome table:** 23 of the 30
                // biomes snow at some altitude. Nine never rain at all, so they
                // were wrongly wet always; the other fourteen are temperate with
                // a snow line - the first of them turns over at y=108, which is
                // ordinary hill country rather than a corner case. Of 6,030
                // sampled biome-altitude columns, 3,122 were wet before and are
                // dry now, 2,305 rain columns are untouched, and **none became
                // wet that was not** - the gate only ever removes wetness.
                //
                // **The tempting one-token version of this fix is wrong:**
                // narrowing `falls` instead would take `level` and the mesh
                // rebuild below with it and stop snow rendering at all. `falls`
                // is "is anything coming down", which is a question about
                // particles; this is "is it wet", which is a question about
                // gameplay. They are different questions and only the second
                // one is rain-only.
                //
                // **`kFallingLevel`, not a literal 0.2 - finding 9686.**
                // `Weather.hpp` asked for this reconciliation in the comment
                // above the constant, and named this expression specifically:
                // "`Main.cpp` holds an unlinked copy of this same 0.2 ... when
                // it is reconciled, this is the one that should survive,
                // because it sits beside the ramp rate that gives it its
                // meaning." That is the right call and this is that edit.
                //
                // **It named one copy and there were three**, which is the more
                // useful half of the finding: this site, the freeze/settle gate
                // and the rain-splash emitter all asked the same question with
                // the same literal, and one of the three was added tonight by
                // the ice work. Reconciling only the site the finding named
                // would have left two literals behind while making the fact
                // *look* like it had one owner - strictly worse than leaving
                // all three, because the next reader would trust the name.
                //
                // The nearby `rainAmount > 0.01f` on `falls` is deliberately
                // NOT this constant. It asks "is anything coming down at all",
                // which drives particles and the mesh; this asks "is it coming
                // down hard enough to count", which drives gameplay. Same
                // variable, different questions, and only the second is
                // `kFallingLevel`.
                world.setPrecipitating(falls && rainAmount > game::weather::kFallingLevel &&
                                       kind == game::weather::Precipitation::Rain);

                // Rebuilt on a column change or a material change in strength.
                // Every rebuild retires a buffer, so "every frame" is not free.
                // The wind is in there too: its component along each quad is
                // baked per vertex, so a slow turn eventually goes stale.
                if (level > 0.0f &&
                    (column != precipitationColumn || std::abs(level - precipitationLevel) > 0.05f ||
                     kind != precipitationKind ||
                     std::abs(windAngle - precipitationWind) > 0.08f)) {
                    precipitationColumn = column;
                    precipitationLevel = level;
                    precipitationKind = kind;
                    precipitationWind = windAngle;
                    renderer.setPrecipitationMesh(game::weather::buildPrecipitationMesh(
                        world, camera.position, static_cast<int>(settings.rainDistance), level,
                        windDirection));
                } else if (level <= 0.0f) {
                    precipitationLevel = 0.0f;
                }

                const bool snow = kind == game::weather::Precipitation::Snow;

                // **Snow settles and water freezes**, scattered over columns
                // near the player rather than swept, which is the reference's
                // random tick in everything but name. One column per fire,
                // because each one that lands calls `setBlock` and that costs a
                // remesh.
                settleTimer -= deltaSeconds;
                if (settleTimer <= 0.0f) {
                    settleTimer = 0.08f;
                    // **Only snow ACCUMULATION needs falling weather. Freezing
                    // and melting do not** - and gating all three on
                    // `rainAmount` is why a clear cold night never froze a
                    // single lake. The reference freezes water whenever the
                    // column is cold, sky-lit and dark enough; precipitation is
                    // not one of its conditions. So the pass runs on its own
                    // timer now and each of the three rules states its own
                    // weather requirement, rather than one gate quietly
                    // speaking for all of them.
                    //
                    // **`rainAmount` itself is untouched**, deliberately: it is
                    // `weather.rainLevel()`, the intensity of whatever is
                    // falling, and whether that falls as snow or as rain is
                    // `precipitationFor`'s answer below rather than this
                    // value's. Nothing here re-widens it.
                    const bool precipitating = rainAmount > game::weather::kFallingLevel;
                    const float angle = static_cast<float>(std::rand()) / RAND_MAX * 6.2831853f;
                    const float away =
                        std::sqrt(static_cast<float>(std::rand()) / RAND_MAX) * 26.0f;
                    const int sx =
                        static_cast<int>(std::floor(camera.position.x + std::cos(angle) * away));
                    const int sz =
                        static_cast<int>(std::floor(camera.position.z + std::sin(angle) * away));
                    const int top = world.highestSolid(sx, sz);
                    // Its own biome, not the player's: a snow line runs through
                    // the middle of a view and settling by the column you happen
                    // to stand in would put snow on the warm side of it.
                    const auto here = game::sampleBiome(kWorldSeed, sx, sz).dominant;
                    // **The five-argument form, because this one writes blocks.**
                    // The two-argument form runs a clean contour with no jitter;
                    // `freezesAt` - which is what the generator and the lightning
                    // both ask - carries a +/-2.24-block wobble, so inside that
                    // band the jitter-free answer says snow on columns the
                    // generator deliberately left bare and says nothing on the
                    // ones it snow-capped. These `setBlock`s persist, so the
                    // disagreement is permanent.
                    const auto falling = game::weather::precipitationFor(
                        here, std::max(top, 0), kWorldSeed, sx, sz);
                    if (top >= 0 && falling == game::weather::Precipitation::Snow) {
                        const game::BlockId standing = world.blockAt(sx, top, sz);
                        const game::BlockId above = world.blockAt(sx, top + 1, sz);
                        // Only under an open sky. Sky light is "can this cell
                        // see up", not brightness, so this holds at night too.
                        const bool open =
                            world.skyLightAt(sx, top + 1, sz) >= game::kMaxLight;
                        // A torch keeps its own patch clear, which is the
                        // reference's rule and the only thing that stops a
                        // sheltered doorway filling in.
                        // **Nine or lower, so the block is at 10 - ours said
                        // 12.** The reference accumulates snow only where the
                        // block light level is 9 or less, so two extra levels
                        // of torchlight were letting snow creep closer to a
                        // flame than it ever should.
                        const bool warmed = world.blockLightAt(sx, top + 1, sz) >= 10;
                        if (precipitating && open && !warmed) {
                            if (game::isSnowLayer(standing)) {
                                const int deeper = game::snowLayerDepth(standing) + 1;
                                world.setBlock(sx, top, sz, game::snowLayerAt(deeper));
                            } else if (above == game::BlockId::Air &&
                                       game::blockShape(standing) == game::BlockShape::Full &&
                                       !game::isFluid(standing) && standing != game::BlockId::Air) {
                                world.setBlock(sx, top + 1, sz, game::snowLayerAt(1));
                            }
                        }

                        // Ice, on the same pass. Water is not solid, so the
                        // surface sits above whatever `highestSolid` found.
                        for (int y = top + 1; y < top + 40; ++y) {
                            const game::BlockId cell = world.blockAt(sx, y, sz);
                            if (!game::isWater(cell)) {
                                break;
                            }
                            // **Below 10, and at least one horizontal neighbour
                            // that is not water.** That last clause is the one
                            // that was missing entirely, and it is what makes a
                            // lake freeze inward from its banks instead of
                            // skinning over all at once - the middle of an
                            // ocean has water on all four sides and never
                            // freezes at all. Kept last in the conjunction so
                            // the four extra `blockAt` reads are only paid by a
                            // cell that has already passed the cheap tests.
                            if (world.blockAt(sx, y + 1, sz) == game::BlockId::Air &&
                                game::isWaterSource(cell) &&
                                world.skyLightAt(sx, y + 1, sz) >= game::kMaxLight &&
                                world.blockLightAt(sx, y, sz) < 10 &&
                                (!game::isWater(world.blockAt(sx + 1, y, sz)) ||
                                 !game::isWater(world.blockAt(sx - 1, y, sz)) ||
                                 !game::isWater(world.blockAt(sx, y, sz + 1)) ||
                                 !game::isWater(world.blockAt(sx, y, sz - 1)))) {
                                // A source only. Freezing a flowing cell is
                                // undone by the next fluid tick, which would
                                // leave the shoreline flickering.
                                world.setBlock(sx, y, sz, game::BlockId::Ice);
                            }
                        }
                    }

                    // **Ice melts by BLOCK light alone, and sky light is
                    // ignored on purpose** - that is the reference's rule
                    // rather than a simplification. So ice under an open sky
                    // survives noon indefinitely, while one torch set beside a
                    // frozen lake opens a hole in it. That asymmetry is the
                    // whole character of the mechanic, and it is why melting
                    // cannot be folded into the freeze test above, which
                    // requires *full* sky light.
                    //
                    // **Outside the `falling == Snow` test, deliberately.**
                    // That test asks "is this column cold enough to freeze",
                    // which is the wrong question here twice over: a block of
                    // ice carried to a desert and placed must melt, and a cold
                    // biome does not protect ice from a torch either.
                    //
                    // Scanned over a short window rather than at `top` alone,
                    // because ice is solid once it forms - `highestSolid` then
                    // reports the ice itself, and a snow layer settling on top
                    // of it moves the answer again.
                    if (top >= 0) {
                        for (int y = std::max(top - 3, 0); y <= top + 3; ++y) {
                            if (world.blockAt(sx, y, sz) != game::BlockId::Ice) {
                                continue;
                            }
                            // "Higher than 11 immediately next to it on any
                            // side", so the six neighbours - the ice cell's own
                            // level is not what the rule asks for.
                            int lit = static_cast<int>(world.blockLightAt(sx + 1, y, sz));
                            lit = std::max(
                                lit, static_cast<int>(world.blockLightAt(sx - 1, y, sz)));
                            lit = std::max(
                                lit, static_cast<int>(world.blockLightAt(sx, y, sz + 1)));
                            lit = std::max(
                                lit, static_cast<int>(world.blockLightAt(sx, y, sz - 1)));
                            lit = std::max(
                                lit, static_cast<int>(world.blockLightAt(sx, y + 1, sz)));
                            lit = std::max(
                                lit, static_cast<int>(world.blockLightAt(sx, y - 1, sz)));
                            if (lit > 11) {
                                // Back to a *source*, which is what froze in the
                                // first place. Writing a flowing level would be
                                // corrected by the next fluid tick anyway and
                                // would flicker the shoreline for a frame on the
                                // way.
                                world.setBlock(sx, y, sz, game::BlockId::Water0);
                            }
                        }
                    }
                }

                const float fallSpeed = snow ? 1.2f : 10.0f;
                precipitationFallen =
                    std::fmod(precipitationFallen + deltaSeconds * fallSpeed, 4096.0f);
                // Sheared, not tilted: a thirty-two block quad leaned over for a
                // gale would swing eight blocks sideways and part company with
                // the column it belongs to. Capped near twenty degrees, past
                // which it stops reading as rain and starts reading as a broken
                // particle system.
                const float slant = std::clamp(windStrength * 0.45f / fallSpeed, 0.0f, 0.35f);
                renderer.setPrecipitation(level, snow, precipitationFallen, slant);

                // **Retriggered rather than looped**, because the mixer has no
                // loop point. Eight recordings, and the restart has to land
                // *before* the shortest of them ends or the bed stops being a
                // bed.
                //
                // **This interval read 3.4 s against a bank of 1.973-2.192 s**,
                // so rain played for two seconds, fell silent for one and a
                // half, and started again - heard as a noise switching on and
                // off rather than as weather, and reported by the player as
                // exactly that. The old comment claimed "a slightly early
                // restart"; it was 1.4 s LATE, which is `CLAUDE.md`'s "number
                // ported into a field measured against something else" - 3.4 s
                // matches no rain clip and was almost certainly carried from a
                // different recording set (`cave` runs 3.5-9.2 s).
                //
                // 1.8 s sits under the shortest clip (rain6, 1.973 s), so
                // consecutive clips always overlap and the seam never opens.
                // Measured 2026-08-20 from each file's final Ogg granule
                // position divided by its sample rate, by two independent
                // parsers that agreed. *Falsified by* any `rain*.ogg` shorter
                // than 1.8 s joining the bank - re-measure, never assume.
                // Silent under a roof, which the sky light answers for free.
                rainSoundTimer -= deltaSeconds;
                if (level > 0.05f && !snow && rainSoundTimer <= 0.0f) {
                    const int sky = world.skyLightAt(static_cast<int>(std::floor(camera.position.x)),
                                                     static_cast<int>(std::floor(camera.position.y)),
                                                     static_cast<int>(std::floor(camera.position.z)));
                    const float sheltered = static_cast<float>(sky) / static_cast<float>(game::kMaxLight);
                    // 0.30 rather than 0.60 on the player's report that rain was
                    // too loud. Broadband hiss reads louder than its amplitude
                    // suggests, and the overlap above now sums two clips at the
                    // seam, so the old gain would have got louder, not quieter.
                    sounds.playGlobal(audio, game::SoundEvent::Rain, level * sheltered * 0.30f);
                    rainSoundTimer = 1.8f;
                }
            }

            // The deck gathers before the first drop and clears long after the
            // last, which is what makes weather read as having a cause.
            renderer.setClouds(static_cast<int>(settings.clouds),
                               std::min(1.0f, settings.cloudCoverage + weather.cloudLevel() * 0.5f),
                               settings.cloudShadow);

            // --- Particles and foliage ------------------------------------
            const float bend = settings.foliageSway * std::min(0.22f, 0.016f * windStrength);
            renderer.setWind(windDirection, bend, windClock);

            if (settings.particles) {
                // Smoke leans on the same wind that bends the grass and slants
                // the rain, so the three cannot disagree about which way it is
                // blowing.
                particles.update(world, deltaSeconds,
                                 glm::vec3{windDirection.x, 0.0f, windDirection.y} * windStrength *
                                     0.35f);
                particles.emitAmbient(world, camera.position, deltaSeconds);

                // Rain landing. Scattered around the player rather than
                // everywhere, because a splash is only legible within a few
                // metres and the rest would be spent on sub-pixel specks.
                splashTimer -= deltaSeconds;
                if (rainAmount > game::weather::kFallingLevel &&
                    precipitationKind == game::weather::Precipitation::Rain && splashTimer <= 0.0f) {
                    splashTimer = 0.06f;
                    for (int i = 0; i < 4; ++i) {
                        const float angle = static_cast<float>(std::rand()) / RAND_MAX * 6.2831853f;
                        const float away = std::sqrt(static_cast<float>(std::rand()) / RAND_MAX) * 9.0f;
                        const int sx = static_cast<int>(std::floor(camera.position.x + std::cos(angle) * away));
                        const int sz = static_cast<int>(std::floor(camera.position.z + std::sin(angle) * away));
                        const int top = world.highestSolid(sx, sz);
                        if (top < 0) {
                            continue;
                        }
                        // Water is not solid, so `highestSolid` finds the bed
                        // rather than the surface. A drop lands on whichever is
                        // higher - and rain on water is the splash worth having,
                        // since the ripple rings under it are already drawn.
                        int surface = top;
                        for (int y = top + 1; y < top + 40; ++y) {
                            if (!game::isWater(world.blockAt(sx, y, sz))) {
                                break;
                            }
                            surface = y;
                        }
                        // Only where the sky can actually reach it, or rain
                        // splashes on the floor of a cave.
                        if (world.skyLightAt(sx, surface + 1, sz) < 12) {
                            continue;
                        }
                        particles.spawnSplash(glm::vec3{static_cast<float>(sx) + 0.5f,
                                                        static_cast<float>(surface) + 1.02f,
                                                        static_cast<float>(sz) + 0.5f});
                    }
                }

                const glm::vec3 toScreen = camera.forward();
                const glm::vec3 sideways =
                    glm::normalize(glm::cross(toScreen, glm::vec3{0.0f, 1.0f, 0.0f}));
                renderer.setParticleMesh(particles.buildMesh(
                    world, sideways, glm::normalize(glm::cross(sideways, toScreen))));            }

            renderer.setSunDirection(light.direction);
            // The same distance the far plane is set from, held just inside it.
            // A body drawn nearer than the world is drawn reads as an object in
            // the landscape rather than as the sky - it passes behind hills a
            // few hundred metres off and appears to sink into the ground.
            const float skyDistance =
                static_cast<float>(world.loadRadius() * game::Chunk::kSize) * 1.8f * 0.92f;
            renderer.setSkyDistance(skyDistance);
            // Warm for the sun, a cool white at a fraction of the strength for
            // the moon. The shader cannot tell them apart - there is one
            // directional light and `sunDirection` is whichever is up.
            const bool moonUp = sunDirection.y <= 0.0f;
            renderer.setSkyGlow(moonUp ? glm::vec3{0.74f, 0.82f, 1.00f}
                                       : glm::vec3{1.00f, 0.84f, 0.62f},
                                light.strength * (moonUp ? 0.55f : 1.0f));
            // Rain takes the sun down about a fifth and a storm a third, which
            // is the reference's 15 -> 12 -> 10 on its own light scale. The
            // ambient is lifted a little as it goes, because an overcast sky is
            // a diffuser rather than simply a darker sun.
            const float overcast = 1.0f - 0.24f * rainAmount - 0.26f * thunderAmount;
            const float ambient = light.ambient * (1.0f - 0.18f * rainAmount) + flash * 0.55f;
            // **Night Vision, which had no reader anywhere.** Two
            // brewing rows make it, its icon and timer are displayed,
            // and drinking it changed nothing whatever about what you
            // could see - a finished-looking feature from every angle
            // except the one that matters.
            //
            // It lifts the **floor**, not the sun: the reference's
            // night vision makes unlit ground read as though it were
            // lit, and `ambientFloor` is exactly "what a surface no
            // light reaches receives".
            //
            // **The bright value is read off the day's own curve rather
            // than typed here.** A literal at this call site would be a
            // second owner of how bright daylight is, and would sit
            // where it was set while `sky::lighting` moved underneath
            // it - the derived-somewhere-other-than-the-owning-table
            // shape. `0.25f` is noon: `sunDirection` turns one full
            // circle across `timeOfDay`, so a quarter round is overhead.
            const float ambientFloor =
                player.effects.level(game::effects::Effect::NightVision) > 0
                    ? game::sky::lighting(0.25f).ambient
                    : game::sky::kAmbientFloor;
            renderer.setSunLighting(ambient, light.strength * overcast, ambientFloor);
            renderer.setSkyTransform(game::sky::skyTransform(camera.position, sunDirection,
                                                             game::sky::kSunSize, skyDistance),
                                     0);
            renderer.setSkyTransform(game::sky::skyTransform(camera.position,
                                                             game::sky::moonDirection(timeOfDay),
                                                             game::sky::kMoonSize, skyDistance),
                                     1);

            // The water surface plays its own strip of frames. Redirecting the
            // layer at draw time is what makes it free: not one chunk is rebuilt
            // for it, and a still lake and a waterfall stay one mesh.
            waterAnimationSeconds += deltaSeconds;
            // Wrapped rather than left to grow, or a long session eventually
            // loses the fraction that picks the frame.
            constexpr float kWaterLoop = game::kWaterFrames * game::kWaterFrameSeconds;
            waterAnimationSeconds -= std::floor(waterAnimationSeconds / kWaterLoop) * kWaterLoop;
            const int waterFrame =
                static_cast<int>(waterAnimationSeconds / game::kWaterFrameSeconds) % game::kWaterFrames;
            renderer.setAnimatedLayer(static_cast<float>(game::TextureLayer::Water),
                                      static_cast<float>(game::kWaterFrameFirst + waterFrame));

            // Fire runs on its own clock, faster than water, on the second slot.
            const int fireFrame =
                static_cast<int>(waterAnimationSeconds / game::kFireFrameSeconds) % game::kFireFrames;
            renderer.setSecondAnimatedLayer(static_cast<float>(game::kFireSprite),
                                            static_cast<float>(game::kFireFrameFirst + fireFrame));

            // Underwater everything fades to one colour over a fixed distance,
            // and your eyes open out over the first half-minute. Dimmed by the
            // daylight the surface is getting, or a night dive glows.
            const glm::vec3 sky = game::sky::skyColor(sunDirection);
            // **Flattened toward grey, not darkened per biome.** The reference
            // replaces the sky colour outright during rain rather than tinting
            // whatever was there, which is why an overcast day looks the same
            // everywhere. Thunder blends three quarters of the way again.
            constexpr glm::vec3 kRainSky{0.160f, 0.168f, 0.185f};
            constexpr glm::vec3 kStormSky{0.042f, 0.045f, 0.052f};
            glm::vec3 weatherSky = glm::mix(sky, kRainSky, rainAmount);
            weatherSky = glm::mix(weatherSky, kStormSky, thunderAmount * 0.75f);
            // A strike lights the whole sky from inside the deck. Added rather
            // than mixed, so it clears 1.0 and bloom turns it into a real flash
            // instead of a grey wash.
            weatherSky += glm::vec3{0.55f, 0.55f, 0.70f} * flash;
            glm::vec3 background = weatherSky;
            // Where the surface is, for the shafts of light coming down through
            // it. Walked from the eye once a frame rather than published by the
            // water system, because it is the only thing that ever asks and it
            // is only asked while submerged.
            if (eyeUnderwater) {
                const auto ex = static_cast<int>(std::floor(camera.position.x));
                const auto ez = static_cast<int>(std::floor(camera.position.z));
                int top = static_cast<int>(std::floor(camera.position.y));
                for (int i = 0; i < 64 && game::isWater(world.blockAt(ex, top + 1, ez)); ++i) {
                    ++top;
                }
                renderer.setUnderwaterShafts(static_cast<float>(top + 1),
                                             settings.waterCaustics * 0.9f);
            } else {
                renderer.setUnderwaterShafts(0.0f, 0.0f);
            }
            if (eyeInLava) {
                // Nearly zero distance, so nothing but lava reaches the screen.
                renderer.setFog(game::fluid::kLavaFogColour, game::fluid::kLavaFogDistance);
                background = game::fluid::kLavaFogColour;
            } else if (eyeUnderwater) {
                // A flat colour and a fixed distance, both constant. Anything
                // that varies with the time of day or with how long you have
                // been under makes the water look like a different colour every
                // time you dip into it.
                renderer.setFog(game::fluid::kFogColour, game::fluid::kFogDistance);
                // Anything with no geometry behind it is water too, so the sky
                // must not show through from sixty metres down.
                background = game::fluid::kFogColour;
            } else {
                // The last stretch before the far edge fades into the sky, so
                // the world reads as continuing rather than stopping at a wall.
                // The clear colour *is* that sky, which is what makes the join
                // invisible; ending the ramp exactly at the visible radius
                // spends the outermost ring of chunks hiding the boundary.
                //
                // **Measured in chunks, not as a fraction of the view.** Fog is
                // here to hide the edge and nothing else, so it should cover the
                // same short distance whatever the render distance is - a fixed
                // fraction hazed a third of the world at distance 12 and barely
                // anything at distance 4.
                //
                // Measured radially from the eye, so the wall sits the same
                // distance away in every direction rather than only straight
                // ahead - see `PushConstants::eye`.
                constexpr float kFogChunks = 1.25f;
                const float visible = std::max(static_cast<float>(world.visibleRadius()), 1.0f);
                const float fogStart = std::clamp(1.0f - kFogChunks / visible, 0.35f, 0.95f);
                // Weather closes the world in. The reference cuts the view by
                // about a third in a storm, and it is most of what makes one
                // feel enclosing rather than merely grey.
                const float reach = static_cast<float>(world.visibleRadius() * game::Chunk::kSize) *
                                    (1.0f - 0.18f * rainAmount - 0.15f * thunderAmount);
                renderer.setFog(weatherSky, reach, fogStart);
            }

            // Cues are asked for all over the frame and mixed here, once,
            // after everything that could add one.
            //
            // **Silent unless the pad is the device in use**, so one left
            // plugged in does not buzz at somebody playing on the keyboard. A
            // scale of zero also drains whatever was in flight when the mode
            // changed, rather than freezing a motor mid-cue.
            //
            // `isGamepadLive` rather than `connected`: losing focus stops the
            // motors, and without this a cue still fading would start them
            // again on the very next frame, behind whatever was alt-tabbed to.
            const bool rumbleWanted =
                inputDevice == game::InputDevice::Gamepad && window.isGamepadLive();
            game::updateRumble(rumble, deltaSeconds,
                               rumbleWanted ? settings.controllerRumble : 0.0f);
            window.setGamepadRumble(rumble.heavy, rumble.light);

            renderer.drawFrame(engine::ClearColor{background.r, background.g, background.b, 1.0f},
                               camera.viewMatrix(), highlight);

            ++framesSinceReport;
            if (now - lastReportTime >= std::chrono::seconds(1)) {
                const auto census = creatures.census();
                std::string censusText;
                for (std::size_t i = 0; i < census.size(); ++i) {
                    if (census[i] == 0) {
                        continue;
                    }
                    if (!censusText.empty()) {
                        censusText += ", ";
                    }
                    censusText += game::speciesInfo(static_cast<game::CreatureKind>(i)).name;
                    censusText += " ";
                    censusText += std::to_string(census[i]);
                }
                if (censusText.empty()) {
                    censusText = "none";
                }

                // Chunk, mesh and retired counts are reported together because a
                // streaming leak shows up as one of them climbing without bound
                // while the others hold steady.
                engine::logInfo(std::to_string(framesSinceReport) + " fps | chunks " +
                                std::to_string(world.loadedChunkCount()) + " | meshes " +
                                std::to_string(renderer.meshCount()) + " | pending " +
                                std::to_string(world.pendingChunkCount()) + " | retired " +
                                std::to_string(renderer.retiredMeshCount()) + " | gpu " +
                                std::to_string(renderer.stats().gpuMilliseconds) + " ms | draws " +
                                std::to_string(renderer.stats().drawCalls) + " | tris " +
                                std::to_string(renderer.stats().triangles) + " | vram " +
                                std::to_string(renderer.stats().pooledMegabytesUsed) + "/" +
                                std::to_string(renderer.stats().pooledMegabytesHeld) + " MB " +
                                std::to_string(renderer.stats().deviceAllocations) + " allocs | creatures " +
                                std::to_string(creatures.count()) + " (" + censusText + ") hunting " +
                                std::to_string(creatures.hunting()) + " | drops " +
                                std::to_string(drops.count()) + " | particles " +
                                std::to_string(particles.count()) + " | weather " +
                                std::to_string(weather.rainLevel()).substr(0, 4) + "/" +
                                std::to_string(weather.thunderLevel()).substr(0, 4));
                framesSinceReport = 0;
                lastReportTime = now;
            }

            // The whole session, written down. On a timer because a crash, a
            // lost device or a pulled power cable is not a polite shutdown, and
            // on the flag as well because a screen just closed on a container
            // whose contents changed. Last thing in the frame, after everything
            // that could have moved an item has already run.
            if ((savePending && !saveFailing) || now - lastSaveTime >= kAutosaveInterval) {
                // **The dirty flag now survives a failed write.** Clearing it
                // regardless was the same shape as the `saveIfModified` bug: the
                // world is marked clean, the next autosave sees nothing to do,
                // and the only copy of a rolled loot chest is the one that just
                // failed to write.
                //
                // `saveFailing` exists so that keeping it dirty does not become a
                // retry every single frame on a disk that is full or a directory
                // that has gone away - which would bury the writer's own error
                // in thousands of copies of itself. A failure hands the retry to
                // the timer instead, so it comes round once per autosave
                // interval, which is the right cadence for something a player
                // may be fixing in another window.
                const bool complete = saveEverything();
                savePending = !complete;
                saveFailing = !complete;
                lastSaveTime = now;
            }

            frameLimiter.waitForNextFrame();
        }

        engine::logInfo("Window closed. Saving world.");
        // **Before the save, not after**, and this is the ordinary shutdown's
        // half of what `SaveOnUnwind` does on the crash path: the landing is a
        // `setBlock`, and `saveEverything`'s first write is `world.saveAll()`.
        settleFallersBeforeFinalSave();
        // **`worldSaved` is set either way, and that is deliberate.** It only
        // tells `SaveOnUnwind` that the ordinary path already ran; leaving it
        // false would send the emergency save at the same tables that just
        // failed, and its "unwinding without a save" line would be untrue.
        if (!saveEverything()) {
            engine::logError("The shutdown save did not write everything - see the line above.");
        }
        worldSaved = true;
        engine::logInfo("Shutting down.");
    } catch (const std::exception& error) {
        // **No save here on purpose.** Unwinding has already destroyed `world`,
        // the player and every container by the time this runs, so a save
        // written at this point would be reading freed memory. `SaveOnUnwind`,
        // declared beside them, has already done it while they were alive.
        engine::logError(std::string("Fatal: ") + error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
