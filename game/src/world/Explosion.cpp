#include "world/Explosion.hpp"

#include "world/Raycast.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace game {
namespace {

/// The march's two fixed costs. `0.22500001` rather than `0.225` is the
/// reference's own literal, nudged off the boundary to dodge a float edge case;
/// divided by the 0.3 step it means a ray loses 0.75 intensity per block of
/// open air travelled.
constexpr float kStep = 0.3f;
constexpr float kDecayPerStep = 0.22500001f;

/// A ray samples the same cell about three times over, because the step is 0.3
/// and a cell is 1. Paying the resistance every sample is deliberate and is why
/// the reference notes that blasts inside a non-full block are heavily damped.
constexpr float kResistanceOffset = 0.3f;
constexpr float kResistanceScale = 0.3f;

/// Sample points across an entity box are spaced `1/(2·size + 1)` apart.
constexpr float kExposureSpacing = 2.0f;

/// The published damage curve is Java's. Bedrock hits measurably softer — 27.5
/// point-blank against Java's 43 on Normal — and does not publish a formula, so
/// the curve is scaled to land on the edition we follow.
constexpr float kBedrockDamageScale = 27.5f / 43.0f;

std::uint32_t nextRandom(std::uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float randomUnit(std::uint32_t& state) {
    return static_cast<float>(nextRandom(state) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

/// The 1352 ray directions, built once.
///
/// Points on the *surface* of a 16³ index grid, normalised. Uniform on a cube
/// rather than on a sphere, so rays crowd toward the six face centres — that
/// unevenness is the reference's and is part of why craters look the way they
/// do.
const std::vector<glm::vec3>& rayDirections() {
    static const std::vector<glm::vec3> directions = [] {
        std::vector<glm::vec3> built;
        built.reserve(1352);
        for (int j = 0; j < 16; ++j) {
            for (int k = 0; k < 16; ++k) {
                for (int l = 0; l < 16; ++l) {
                    if (j != 0 && j != 15 && k != 0 && k != 15 && l != 0 && l != 15) {
                        continue;
                    }
                    glm::vec3 direction{static_cast<float>(j) / 15.0f * 2.0f - 1.0f,
                                        static_cast<float>(k) / 15.0f * 2.0f - 1.0f,
                                        static_cast<float>(l) / 15.0f * 2.0f - 1.0f};
                    built.push_back(glm::normalize(direction));
                }
            }
        }
        return built;
    }();
    return directions;
}

std::uint64_t packCell(const glm::ivec3& cell) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.x)) << 42) ^
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.y)) << 21) ^
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(cell.z));
}

/// True if anything solid stands between the two points. Water is not solid, so
/// it shelters nothing — the reference's rule, arrived at for free.
bool shielded(const World& world, const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 offset = to - from;
    const float distance = glm::length(offset);
    if (distance < 0.0001f) {
        return false;
    }
    return raycast(world, from, offset / distance, distance).hit;
}

} // namespace

float blastResistance(BlockId block) {
    if (isFluid(block)) {
        // 100, and it is the whole reason a blast underwater breaks nothing:
        // a single 0.3 step costs 30, which no creeper ray can pay. Lava is the
        // same figure in the reference.
        return 100.0f;
    }
    // ---- Redstone, asked before the cut-shape forwarding. ----
    // A button and a plate would otherwise inherit a plank's three, and every
    // one of these is fragile in the reference. Without the branch they all
    // reach the stone default at the bottom, which is the exact shape of bug
    // that once left two whole table runs proof against any explosion.
    if (isRedstoneWire(block) || isRedstoneTorch(block) || isRepeater(block) ||
        isComparator(block) || isTripwireHook(block) || isTripwire(block)) {
        return 0.0f;
    }
    if (isDaylightDetector(block)) {
        return 0.2f;
    }
    if (isButton(block) || isPressurePlate(block) || isLever(block) || isTarget(block) ||
        isPiston(block) || isPistonHead(block)) {
        return 0.5f;
    }
    if (isRail(block)) {
        return 0.7f;
    }
    if (isNoteBlock(block)) {
        return 0.8f;
    }
    if (isObserver(block) || isLightningRod(block)) {
        return 3.0f;
    }
    if (isDispenserLike(block)) {
        return 3.5f;
    }
    // A cut shape resists exactly as the block it came from does, so an obsidian
    // stair is as blast-proof as obsidian and an oak fence is not.
    {
        const BlockId material = shapedParent(block);
        if (material != block) {
            return blastResistance(material);
        }
    }
    // Every facing and both lit states share one resistance, so the family is
    // answered once rather than as eight cases.
    if (isFurnace(block)) {
        return 3.5f;
    }
    // Wooden, so the stone default at the bottom would be badly wrong.
    if (isChest(block)) {
        return 2.5f;
    }
    // Snow, which the stone default at the bottom would make blast-proof.
    if (isSnowLayer(block)) {
        return 0.1f;
    }
    if (isDoor(block) || isTrapdoor(block)) {
        const bool metal = isDoor(block)
                               ? kDoorFamilies[static_cast<std::size_t>(doorFamily(block))].metal
                               : kTrapdoorFamilies[static_cast<std::size_t>(
                                     trapdoorFamily(block))].metal;
        return metal ? 5.0f : 3.0f;
    }
    if (isBed(block)) {
        return 0.2f;
    }
    // **Above the run tests below, not after them.** A plant, a torch or a
    // carpet offers a blast nothing at all, and each table run has its own
    // catch-all that would otherwise answer 6.0 for the ones nobody named -
    // which is what made bamboo, sweet berries, glow lichen, sea pickles and
    // the four nether plants blast-proof.
    if (blockShape(block) == BlockShape::Cross || blockShape(block) == BlockShape::Flat) {
        return 0.0f;
    }
    // The farm, above the run tests for the same reason: the fourth run has no
    // catch-all of its own, and soil is not rock.
    if (isFarmland(block)) {
        return 0.6f;
    }
    if (block == BlockId::DirtPath) {
        return 0.65f;
    }
    if (isComposter(block)) {
        return 0.6f;
    }
    if (isCarvedPumpkin(block) || isJackOLantern(block)) {
        return 1.0f;
    }
    // The fifth run. **A branch of its own rather than the rock default at the
    // bottom**, which is what made two whole table runs blast-proof last time.
    if (block >= kFirstExtraBlock5 && block <= kLastExtraBlock5) {
        if (block >= BlockId::OakWood && block <= BlockId::StrippedWarpedHyphae) {
            return 2.0f;
        }
        if (isCoralBlock(block) ||
            (block >= BlockId::WaxedCopperBlock && block <= BlockId::WaxedOxidizedCopperGrate) ||
            isCopperBulb(block)) {
            return 6.0f;
        }
        switch (block) {
        case BlockId::CryingObsidian:
        case BlockId::RespawnAnchor:
        case BlockId::EnchantingTable:
        case BlockId::Anvil:
        case BlockId::ChippedAnvil:
        case BlockId::DamagedAnvil:
            return 1200.0f;
        case BlockId::Grindstone:
        case BlockId::Bell:
            return 5.0f;
        case BlockId::Lodestone:
        case BlockId::BlastFurnace:
        case BlockId::Stonecutter:
            return 3.5f;
        case BlockId::CartographyTable:
        case BlockId::FletchingTable:
        case BlockId::Loom:
        case BlockId::Barrel:
        case BlockId::Lectern:
        case BlockId::Cauldron:
        case BlockId::Campfire:
        case BlockId::SoulCampfire:
            return 2.5f;
        case BlockId::ChiseledBookshelf:
            return 1.5f;
        case BlockId::SculkShrieker:
        case BlockId::SculkSensor:
            return 3.0f;
        case BlockId::BrownMushroomBlock:
        case BlockId::RedMushroomBlock:
        case BlockId::MushroomStem:
        case BlockId::SculkVein:
        case BlockId::MossCarpet:
            return 0.2f;
        case BlockId::PowderSnow:
        case BlockId::SuspiciousSand:
        case BlockId::SuspiciousGravel:
        case BlockId::RedstoneLamp:
        case BlockId::RedstoneLampLit:
            return 0.3f;
        default:
            return 1.0f;
        }
    }
    // The sixth run. Named rather than left to the rock default below.
    if (block >= kFirstExtraBlock6 && block <= kLastExtraBlock6) {
        switch (block) {
        case BlockId::EndPortalFrame:
            return 3600.0f;
        case BlockId::DragonEgg:
            return 9.0f;
        case BlockId::Beacon:
        case BlockId::Conduit:
            return 3.0f;
        case BlockId::MonsterSpawner:
            return 5.0f;
        case BlockId::TintedGlass:
            return 0.3f;
        default:
            return 1.0f;
        }
    }
    // The sixth run. Named rather than left to the rock default at the bottom.
    if (block >= kFirstExtraBlock6 && block <= kLastExtraBlock6) {
        switch (block) {
        case BlockId::EndPortalFrame:
            return 3600.0f;
        case BlockId::DragonEgg:
            return 9.0f;
        case BlockId::Beacon:
        case BlockId::Conduit:
            return 3.0f;
        case BlockId::MonsterSpawner:
            return 5.0f;
        case BlockId::TintedGlass:
            return 0.3f;
        default:
            return 1.0f;
        }
    }
    // The second table run. Almost all of it is rock at 6; the exceptions are
    // named and the rest falls through, which is what stops this needing an
    // edit every time the run grows.
    if (block >= kFirstExtraBlock2 && block <= kLastExtraBlock2) {
        switch (block) {
        case BlockId::Netherrack:
        case BlockId::Sculk:
            return 0.4f;
        case BlockId::SoulSand:
        case BlockId::SoulSoil:
        case BlockId::Podzol:
        case BlockId::Mycelium:
            return 0.5f;
        case BlockId::SlimeBlock:
        case BlockId::DriedKelpBlock:
        case BlockId::SnowBlock:
        case BlockId::Azalea:
        case BlockId::FloweringAzalea:
            return 0.1f;
        case BlockId::NetherWartBlock:
        case BlockId::WarpedWartBlock:
        case BlockId::Shroomlight:
            return 1.0f;
        case BlockId::Cactus:
        case BlockId::Target:
        case BlockId::OchreFroglight:
        case BlockId::VerdantFroglight:
        case BlockId::PearlescentFroglight:
        case BlockId::CrimsonNylium:
        case BlockId::WarpedNylium:
            return 0.4f;
        case BlockId::QuartzBlock:
        case BlockId::SmoothQuartz:
        case BlockId::ChiseledQuartz:
        case BlockId::QuartzBricks:
        case BlockId::QuartzPillar:
            return 0.8f;
        case BlockId::EndStone:
        case BlockId::EndStoneBricks:
            return 9.0f;
        case BlockId::CrimsonStem:
        case BlockId::WarpedStem:
        case BlockId::MangroveLog:
        case BlockId::BambooBlock:
        case BlockId::CrimsonPlanks:
        case BlockId::WarpedPlanks:
        case BlockId::MangrovePlanks:
        case BlockId::BambooPlanks:
        case BlockId::BambooMosaic:
        case BlockId::MuddyMangroveRoots:
        case BlockId::BoneBlock:
        case BlockId::PurpurPillar:
            return 2.0f;
        case BlockId::SculkCatalyst:
            return 3.0f;
        // The reference makes it blast-proof so a wither cannot open a vault.
        case BlockId::ReinforcedDeepslate:
            return 1200.0f;
        default:
            break;
        }
        if (isConcretePowder(block)) {
            return 0.5f;
        }
        // **Falls through to the family rules at the bottom rather than
        // answering 6.0 here.** A second copy of the rock default is a second
        // place for a leaf block or a stripped log to be quietly declared
        // blast-proof, which is exactly what happened to six of them.
    }
    switch (block) {
    case BlockId::Air:
        return 0.0f;
    // A charge offers no resistance at all, which is what lets one blast set
    // off a whole stack.
    case BlockId::Tnt:
    case BlockId::TntPrimed:
    case BlockId::Fire:
        return 0.0f;
    case BlockId::Torch:
    case BlockId::TallGrass:
        return 0.0f;
    case BlockId::Snow:
        return 0.1f;
    case BlockId::Leaves:
        return 0.2f;
    case BlockId::Dirt:
    case BlockId::Sand:
        return 0.5f;
    case BlockId::Grass:
    case BlockId::Gravel:
        return 0.6f;
    case BlockId::Log:
        return 2.0f;
    case BlockId::CraftingTable:
        return 2.5f;
    case BlockId::SmithingTable:
        return 2.5f;
    case BlockId::Planks:
        return 3.0f;
    case BlockId::Glowstone:
        return 0.3f;
    case BlockId::Bricks:
        return 6.0f;
    case BlockId::Glass:
        return 0.3f;
    case BlockId::Clay:
        return 0.6f;
    case BlockId::Sandstone:
        return 0.8f;
    case BlockId::Bookshelf:
        return 1.5f;
    case BlockId::Dandelion:
    case BlockId::Poppy:
    case BlockId::DeadBush:
        return 0.0f;
    case BlockId::Obsidian:
        // 1200, and it is why obsidian is what you build a blast shelter from.
        return 1200.0f;
    case BlockId::AncientDebris:
    case BlockId::EmberiteBlock:
        // The reference's 1200 as well. Blasting for it is a real technique
        // there precisely because the blast cannot destroy what it uncovers.
        return 1200.0f;
    case BlockId::Bedrock:
        return 3600.0f;
    case BlockId::PackedIce:
    case BlockId::Ice:
    case BlockId::BlueIce:
        return 0.5f;
    case BlockId::Terracotta:
        return 4.2f;
    case BlockId::CoalOre:
    case BlockId::IronOre:
    case BlockId::CopperOre:
    case BlockId::GoldOre:
    case BlockId::RedstoneOre:
    case BlockId::LapisOre:
    case BlockId::DiamondOre:
    case BlockId::EmeraldOre:
        return 3.0f;
    default:
        break;
    }
    // **Everything above is a named answer; everything below this line used to
    // be rock.** Only the second table run was range-handled, so all of run one
    // and all of run three fell through to 6.0 - sixteen stained glass blocks
    // blast-proof against the reference's 0.3, every flower in run one at 6.0
    // against 0, an end rod and two torches at 6.0 against 0. That is the same
    // shape as `harvestTier`'s wood-tier default, which left two hundred blocks
    // dropping nothing: **a `default:` that returns a real value hides every
    // entry nobody wrote.**
    //
    // So the fall-through now asks the block what it is rather than assuming.
    // Each rule is the reference's own figure for that family. The cross and
    // flat shapes are answered further up, before the per-run catch-alls.
    if (isPane(block) || isGlassBlock(block)) {
        return 0.3f;
    }
    if (isLeafBlock(block)) {
        return 0.2f;
    }
    if (isWoolBlock(block)) {
        return 0.8f;
    }
    if (isFlammable(block)) {
        // The woods, which is what is left once the leaves and wool are gone.
        return 2.0f;
    }
    // Stone, cobblestone and everything cut from them - slabs and stairs
    // inherit their material's resistance, and the march ignores shape anyway.
    return 6.0f;
}

std::vector<glm::ivec3> explosionBlocks(const World& world, const glm::vec3& centre, float power,
                                        std::uint32_t seed) {
    std::vector<glm::ivec3> destroyed;
    std::unordered_set<std::uint64_t> seen;
    std::uint32_t random = seed | 1u;

    for (const glm::vec3& direction : rayDirections()) {
        // Rolled fresh per ray, not once per blast. That is what stops the
        // crater being a clean sphere.
        float intensity = power * (0.7f + randomUnit(random) * 0.6f);
        glm::vec3 position = centre;

        while (intensity > 0.0f) {
            const glm::ivec3 cell{static_cast<int>(std::floor(position.x)),
                                  static_cast<int>(std::floor(position.y)),
                                  static_cast<int>(std::floor(position.z))};
            const BlockId block = world.blockAt(cell.x, cell.y, cell.z);
            if (block != BlockId::Air) {
                intensity -= (blastResistance(block) + kResistanceOffset) * kResistanceScale;
                // Taken only if the ray survived paying for it, so whatever
                // finally stops a ray is left standing.
                if (intensity > 0.0f) {
                    if (seen.insert(packCell(cell)).second) {
                        destroyed.push_back(cell);
                    }
                }
            }
            position += direction * kStep;
            intensity -= kDecayPerStep;
        }
    }
    return destroyed;
}

float explosionExposure(const World& world, const glm::vec3& centre, const Aabb& box) {
    const glm::vec3 size = box.max - box.min;
    const glm::vec3 spacing{1.0f / (kExposureSpacing * size.x + 1.0f),
                            1.0f / (kExposureSpacing * size.y + 1.0f),
                            1.0f / (kExposureSpacing * size.z + 1.0f)};

    int total = 0;
    int clear = 0;
    for (float u = 0.0f; u <= 1.0f; u += spacing.x) {
        for (float v = 0.0f; v <= 1.0f; v += spacing.y) {
            for (float w = 0.0f; w <= 1.0f; w += spacing.z) {
                const glm::vec3 sample{box.min.x + size.x * u, box.min.y + size.y * v,
                                       box.min.z + size.z * w};
                ++total;
                if (!shielded(world, centre, sample)) {
                    ++clear;
                }
            }
        }
    }
    return total == 0 ? 0.0f : static_cast<float>(clear) / static_cast<float>(total);
}

float explosionImpact(const glm::vec3& centre, float power, const glm::vec3& feet, float exposure) {
    const float distance = glm::length(feet - centre);
    const float reach = 2.0f * power;
    if (distance >= reach) {
        return 0.0f;
    }
    return (1.0f - distance / reach) * exposure;
}

int explosionDamage(float power, float impact) {
    if (impact <= 0.0f) {
        return 0;
    }
    const float raw = 7.0f * power * (impact * impact + impact) + 1.0f;
    return static_cast<int>(std::round(raw * kBedrockDamageScale));
}

} // namespace game
