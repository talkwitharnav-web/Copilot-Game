#pragma once

#include "world/Block.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace game {

/// What a surface is made of, for lighting purposes.
///
/// Written on `soundMaterialFor`'s pattern and for the same reason: a dozen
/// family questions cover eleven hundred blocks, and a new block inherits the
/// right answer the day it is added rather than needing a row.
///
/// **There is no PBR art and none can be staged.** The reference dump contains
/// zero normal, roughness, metallic or emissive maps - checked across all 25,981
/// files - and Java has never shipped any. So these values are authored here,
/// which is the path Mojang's own `texture_set.json` offers: it accepts a single
/// value in place of a map, described in their docs as "the equivalent to
/// referencing a texture image filled uniformly with that value".
enum class MaterialFamily : std::uint8_t {
    Stone,
    /// Anything named Polished, Smooth, Cut or Bricks: worked rock, which takes
    /// a sheen that a broken face does not.
    PolishedStone,
    Dirt,
    Sand,
    Gravel,
    Wood,
    Leaves,
    Plant,
    Wool,
    Metal,
    Glass,
    Ice,
    Fluid,
    /// Water is nearly a mirror; lava is not, and sharing a family with it made
    /// a lava lake reflect like a swimming pool.
    Lava,
    /// Terracotta, concrete and the glazed set: fired, so smoother than rock.
    Ceramic,
    Snow,
    /// The fallback. Nothing looks wrong being slightly rough and non-metallic.
    Organic,
    Count,
};

/// **Roughness here is perceptual, not the `alpha` a GGX distribution wants.**
/// The shader squares it. A value copied from a LabPBR source is already alpha
/// and must not be squared twice.
struct MaterialProperties {
    float roughness = 0.85f;
    float metallic = 0.0f;
};

/// Hand-tuned, sixteen rows. Reflectance is deliberately not a column: 0.04 is
/// physically right for every dielectric here, and the two that are not - glass
/// and ice - differ by less than the eye can find without a reference beside it.
inline constexpr std::array<MaterialProperties, static_cast<std::size_t>(MaterialFamily::Count)> kMaterials{{
    {0.88f, 0.0f}, // Stone
    {0.45f, 0.0f}, // PolishedStone
    {0.95f, 0.0f}, // Dirt
    {0.90f, 0.0f}, // Sand
    {0.92f, 0.0f}, // Gravel
    {0.72f, 0.0f}, // Wood
    {0.82f, 0.0f}, // Leaves
    {0.86f, 0.0f}, // Plant
    {0.98f, 0.0f}, // Wool
    {0.30f, 1.0f}, // Metal
    {0.06f, 0.0f}, // Glass
    {0.12f, 0.0f}, // Ice
    {0.04f, 0.0f}, // Fluid
    {0.55f, 0.0f}, // Lava
    {0.38f, 0.0f}, // Ceramic
    {0.85f, 0.0f}, // Snow
    {0.90f, 0.0f}, // Organic
}};

/// Set on a layer that is vegetation, so a later slice can move it in the wind
/// without asking what block it came from. Reserved now because retrofitting a
/// bit into a packed word is expensive and this one is free.
inline constexpr std::uint8_t kMaterialFlagFoliage = 1u << 0;
inline constexpr std::uint8_t kMaterialFlagTranslucent = 1u << 1;

/// True for the handful of blocks that are worked metal rather than rock with
/// metal in it. **An ore is not metallic** - it is stone with specks, and a
/// binary metallic flag on the whole face would turn the stone into chrome.
constexpr bool isMetalBlock(BlockId id) {
    if (id == BlockId::IronBars) {
        return true;
    }
    if (!isExtraBlock(id)) {
        return false;
    }
    const std::string_view name = extraBlockInfo(id).name;
    // The rule above, enforced. A bare `find("Copper")` matched "Deepslate
    // Copper Ore" and "Block of Raw Copper" - so the ore this comment exists to
    // exclude was being rendered as chrome, and raw ore with it.
    if (name.find("Ore") != std::string_view::npos || name.find("Raw ") != std::string_view::npos) {
        return false;
    }
    return name.find("Block of Iron") != std::string_view::npos ||
           name.find("Block of Gold") != std::string_view::npos ||
           name.find("Block of Copper") != std::string_view::npos ||
           name.find("Block of Emberite") != std::string_view::npos ||
           name.find("Copper") != std::string_view::npos || name.find("Anvil") != std::string_view::npos ||
           name.find("Rail") != std::string_view::npos || name.find("Chain") != std::string_view::npos;
}

/// True for a name the reference gives to rock that has been worked smooth.
constexpr bool isWorkedStoneName(std::string_view name) {
    return name.find("Polished") != std::string_view::npos || name.find("Smooth") != std::string_view::npos ||
           name.find("Cut ") != std::string_view::npos || name.find("Chiseled") != std::string_view::npos ||
           name.find("Bricks") != std::string_view::npos || name.find("Quartz") != std::string_view::npos ||
           name.find("Tiles") != std::string_view::npos;
}

inline MaterialFamily materialFamilyFor(BlockId id) {
    // A cut shape is made of whatever it was cut from. Six hundred and forty
    // blocks are covered by this one line.
    const BlockId parent = shapedParent(id);
    if (parent != id) {
        return materialFamilyFor(parent);
    }

    if (isLava(id)) {
        return MaterialFamily::Lava;
    }
    if (isFluid(id)) {
        return MaterialFamily::Fluid;
    }
    if (isLeafBlock(id)) {
        return MaterialFamily::Leaves;
    }
    // Before the plant test, and that is the point: a torch is drawn as a
    // crossed pair like a flower, so it would otherwise be tagged as foliage and
    // sway in the wind the moment anything reads that flag.
    //
    // **`isTorchBlock`, not the three ids by name.** The redstone torch grew to
    // ten states, and the nine new ones share the lit one's texture layer - so
    // naming only `RedstoneTorch` left that layer claimed by two different
    // families, which the startup check reports and one of which is then
    // silently ignored.
    if (isTorchBlock(id)) {
        return MaterialFamily::Wood;
    }
    // The rest of the redstone family. A rail already answers Metal through its
    // name, and a button and a plate are cut shapes and answered above.
    if (isRedstoneWire(id) || isRepeater(id) || isComparator(id) || isObserver(id) ||
        isDispenserLike(id) || isPiston(id) || isPistonHead(id) || isLever(id) ||
        isTripwireHook(id) || isTripwire(id) || isTarget(id) || isNoteBlock(id) ||
        isDaylightDetector(id) || isRedstoneLamp(id)) {
        return isNoteBlock(id) || isDaylightDetector(id) ? MaterialFamily::Wood
                                                         : MaterialFamily::Stone;
    }
    if (isLightningRod(id)) {
        return MaterialFamily::Metal;
    }
    if (isCrossBlock(id) || isVine(id) || isCocoa(id) || id == BlockId::Kelp || id == BlockId::Seagrass ||
        id == BlockId::LilyPad) {
        return MaterialFamily::Plant;
    }
    if (isLogBlock(id) || isPlanksBlock(id) || id == BlockId::Bookshelf || id == BlockId::CraftingTable ||
        id == BlockId::SmithingTable || isChest(id) || isBeehive(id) || isLadder(id)) {
        return MaterialFamily::Wood;
    }
    if (isWoolBlock(id) || isCarpet(id)) {
        return MaterialFamily::Wool;
    }
    if (id == BlockId::Glass || isPane(id)) {
        return MaterialFamily::Glass;
    }
    if (isIce(id)) {
        return MaterialFamily::Ice;
    }
    if (isMetalBlock(id)) {
        return MaterialFamily::Metal;
    }
    if (id == BlockId::Sand || id == BlockId::Sandstone) {
        return MaterialFamily::Sand;
    }
    if (id == BlockId::Gravel || id == BlockId::Clay) {
        return MaterialFamily::Gravel;
    }
    if (id == BlockId::Snow || isSnowLayer(id)) {
        return MaterialFamily::Snow;
    }
    if (id == BlockId::Grass || id == BlockId::Dirt || id == BlockId::CoarseDirt || id == BlockId::Podzol ||
        id == BlockId::Mycelium) {
        return MaterialFamily::Dirt;
    }
    if (id == BlockId::Terracotta) {
        return MaterialFamily::Ceramic;
    }

    if (isExtraBlock(id)) {
        const std::string_view name = extraBlockInfo(id).name;
        if (name.find("Terracotta") != std::string_view::npos ||
            name.find("Concrete") != std::string_view::npos || name.find("Glazed") != std::string_view::npos) {
            return MaterialFamily::Ceramic;
        }
        if (isWorkedStoneName(name)) {
            return MaterialFamily::PolishedStone;
        }
    }

    // Flammable is the sharpest single question left: it separates the woods and
    // the soft organics from everything that is some kind of rock, and rock is
    // also the safest thing for an unknown to be.
    if (isFlammable(id)) {
        return MaterialFamily::Wood;
    }
    return MaterialFamily::Stone;
}

/// One row of the table the shader reads, packed exactly as it unpacks it.
///
/// R roughness, G metallic, B emissive, A flags - four bytes, one word, and
/// deliberately **no ambient occlusion column**: AO already has an owner in
/// `ChunkMesher`'s `kOcclusionSteps`, and a second copy of a value is this
/// project's most repeated bug.
constexpr std::uint32_t packMaterial(float roughness, float metallic, float emissive, std::uint8_t flags) {
    const auto quantise = [](float value) {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
    };
    return quantise(roughness) | (quantise(metallic) << 8) | (quantise(emissive) << 16) |
           (static_cast<std::uint32_t>(flags) << 24);
}

struct MaterialTable {
    std::vector<std::uint32_t> rows;
    /// **Distinct layers** two different families both claim. Should be zero;
    /// anything else means one of them is being silently ignored, and which one
    /// wins is decided by whichever block id happens to come first.
    ///
    /// Counted per layer, not per claim: the walk asks every face, facing and
    /// chest half, so one disagreeing layer used to be reported forty-five
    /// times over and the number looked like a catastrophe.
    int conflicts = 0;
    /// The first layer two families both claimed, and which two. **A count on
    /// its own is not a diagnostic** - it says something is wrong and gives you
    /// nothing to look at, which cost a round trip the first time a redstone
    /// block shared a picture with the block it was named after.
    int firstConflictLayer = -1;
    int firstConflictHeld = -1;
    int firstConflictWanted = -1;
    /// Layers no block ever asks for - item sprites, the white utility layer,
    /// animation frames. They keep the default row.
    int unclaimed = 0;
};

/// Walks every block, face and facing, and writes what each texture layer is
/// made of.
///
/// **Keyed on the texture layer, not on the block.** A block does not have one
/// material: grass is soil underneath and living surface on top, and a lit
/// furnace glows on exactly one of its six faces. The layer is already in the
/// vertex, already a `flat` varying, and already part of the greedy merge key -
/// so this costs no vertex change, no re-mesh, and cannot fragment merging.
inline MaterialTable buildMaterialTable(std::size_t layerCount) {
    MaterialTable table;
    const MaterialProperties fallback = kMaterials[static_cast<std::size_t>(MaterialFamily::Organic)];
    table.rows.assign(layerCount, packMaterial(fallback.roughness, fallback.metallic, 0.0f, 0));

    std::vector<int> claimedBy(layerCount, -1);
    std::vector<bool> conflicted(layerCount, false);
    std::vector<float> emission(layerCount, -1.0f);

    const auto faces = std::array{BlockFace::Top, BlockFace::Bottom, BlockFace::Side};
    const auto directions = std::array{FaceDirection::Unknown, FaceDirection::PosX, FaceDirection::NegX,
                                       FaceDirection::PosZ, FaceDirection::NegZ};
    const auto halves = std::array{ChestHalf::Single, ChestHalf::Left, ChestHalf::Right};

    for (int raw = 0; raw <= static_cast<int>(kLastBlock); ++raw) {
        const auto id = static_cast<BlockId>(raw);
        if (id == BlockId::Air) {
            continue;
        }
        const MaterialFamily family = materialFamilyFor(id);
        const MaterialProperties& properties = kMaterials[static_cast<std::size_t>(family)];
        const float emissive = static_cast<float>(blockLightEmission(id)) / static_cast<float>(kMaxLight);

        std::uint8_t flags = 0;
        if (family == MaterialFamily::Plant || family == MaterialFamily::Leaves) {
            flags |= kMaterialFlagFoliage;
        }
        if (isTranslucent(id)) {
            flags |= kMaterialFlagTranslucent;
        }

        for (BlockFace face : faces) {
            for (FaceDirection direction : directions) {
                for (ChestHalf half : halves) {
                    const auto layer = static_cast<int>(blockTextureLayer(id, face, direction, half));
                    if (layer < 0 || static_cast<std::size_t>(layer) >= layerCount) {
                        continue;
                    }

                    if (claimedBy[static_cast<std::size_t>(layer)] < 0) {
                        claimedBy[static_cast<std::size_t>(layer)] = static_cast<int>(family);
                        table.rows[static_cast<std::size_t>(layer)] =
                            packMaterial(properties.roughness, properties.metallic, 0.0f, flags);
                    } else if (claimedBy[static_cast<std::size_t>(layer)] != static_cast<int>(family) &&
                               !conflicted[static_cast<std::size_t>(layer)]) {
                        conflicted[static_cast<std::size_t>(layer)] = true;
                        if (table.firstConflictLayer < 0) {
                            table.firstConflictLayer = layer;
                            table.firstConflictHeld = claimedBy[static_cast<std::size_t>(layer)];
                            table.firstConflictWanted = static_cast<int>(family);
                        }
                        ++table.conflicts;
                    }

                    // **The lowest claim wins, and that is what gets the furnace
                    // right.** `FurnaceTop` is drawn by the unlit furnace as
                    // well as the lit one, so it scores zero and stays dark;
                    // `FurnaceFrontLit` is only ever drawn by the lit one, so it
                    // keeps its 13 and only the mouth glows. A block-keyed
                    // emissive would light the whole box.
                    float& stored = emission[static_cast<std::size_t>(layer)];
                    stored = stored < 0.0f ? emissive : std::min(stored, emissive);
                }
            }
        }
    }

    for (std::size_t layer = 0; layer < layerCount; ++layer) {
        if (claimedBy[layer] < 0) {
            ++table.unclaimed;
            continue;
        }
        const auto& properties = kMaterials[static_cast<std::size_t>(claimedBy[layer])];
        const std::uint8_t flags = static_cast<std::uint8_t>((table.rows[layer] >> 24) & 0xffu);
        table.rows[layer] =
            packMaterial(properties.roughness, properties.metallic, std::max(emission[layer], 0.0f), flags);
    }

    return table;
}

} // namespace game
