#pragma once

/// Everything the farm knows, in one place: what a hoe makes, what a seed
/// grows into, how fast it grows and what a composter will take.
///
/// Header-only and `constexpr` throughout, for the reason `Survival.hpp` is:
/// it needs no edit to any `CMakeLists`, and every number is visible to a
/// `static_assert` so the tables can be proved against each other rather than
/// merely eyeballed.
///
/// **Bedrock is the reference.** Where the two editions differ the divergence
/// is named at the value, because the published "minutes to grow" figures are
/// almost all Java's and are three times too fast for us.

#include "Block.hpp"
#include "../item/Item.hpp"

namespace game::farming {

/// What a hoe turns a block into, or the block itself if a hoe does nothing.
///
/// **Podzol and mycelium are deliberately absent**: the reference refuses to
/// till either, and a path has to be cut with a shovel first. Coarse and rooted
/// dirt take two goes - hoe once to plain dirt, hoe again to farmland - which
/// falls straight out of returning `Dirt` for them rather than `Farmland`.
constexpr BlockId tilledFrom(BlockId block) {
    switch (block) {
    case BlockId::Dirt:
    case BlockId::Grass:
    case BlockId::DirtPath:
        return BlockId::Farmland;
    case BlockId::CoarseDirt:
    case BlockId::RootedDirt:
        return BlockId::Dirt;
    default:
        return block;
    }
}

/// What a shovel flattens into a path. Farmland is not in the list on purpose:
/// the reference lets you shovel grass, dirt, podzol and mycelium, and tilling
/// is the one-way trip.
constexpr BlockId pathFrom(BlockId block) {
    switch (block) {
    case BlockId::Grass:
    case BlockId::Dirt:
    case BlockId::CoarseDirt:
    case BlockId::RootedDirt:
    case BlockId::Podzol:
    case BlockId::Mycelium:
        return BlockId::DirtPath;
    default:
        return block;
    }
}

/// Which crop a seed sows, as that family's age 0. `BlockId::Air` for anything
/// that is not a seed, so one lookup answers "can this be planted" too.
constexpr BlockId cropForSeed(ItemId seed) {
    switch (seed) {
    case ItemId::WheatSeeds:
        return BlockId::WheatCrop0;
    case ItemId::Carrot:
        return BlockId::CarrotCrop0;
    case ItemId::Potato:
        return BlockId::PotatoCrop0;
    case ItemId::BeetrootSeeds:
        return BlockId::BeetrootCrop0;
    case ItemId::MelonSeeds:
        return BlockId::MelonStem0;
    case ItemId::PumpkinSeeds:
        return BlockId::PumpkinStem0;
    default:
        return BlockId::Air;
    }
}

/// A seed needs tilled ground; nether wart needs soul sand and nothing else.
/// Two questions rather than one, because the two plants share no soil.
constexpr bool seedWantsFarmland(ItemId seed) { return cropForSeed(seed) != BlockId::Air; }

constexpr bool canSowOn(ItemId seed, BlockId ground) {
    if (seed == ItemId::NetherWart) {
        return ground == BlockId::SoulSand;
    }
    return seedWantsFarmland(seed) && isFarmland(ground);
}

/// How far a water block may sit and still keep ground wet. The reference's own
/// rule is a **9x9 horizontal box centred on the farmland, at its own level or
/// one above** - not a radius, not a line of sight, and blocks in between make
/// no difference at all.
constexpr int kHydrationReach = 4;

/// The reference's growth arithmetic, and it is a function of the **3x3 patch
/// of farmland under and around the plant**, not of the plant.
///
/// Points: 2 for dry ground beneath, 4 for wet; a quarter for each of the eight
/// neighbours that is dry farmland and three quarters for each wet one. The
/// chance of advancing on a random tick is then `1 / (floor(25 / points) + 1)`.
///
/// The `floor` is why this cannot be written as a smooth curve: several
/// neighbour counts collapse onto the same probability.
constexpr float growthChance(int quarterPoints) {
    const float points = static_cast<float>(quarterPoints) * 0.25f;
    if (points <= 0.0f) {
        return 0.0f;
    }
    const int steps = static_cast<int>(25.0f / points);
    return 1.0f / static_cast<float>(steps + 1);
}

/// Points are accumulated in quarters so the whole sum stays in integers -
/// a quarter is the smallest the reference ever awards.
constexpr int kPointsDryUnder = 8;   // 2.0
constexpr int kPointsWetUnder = 16;  // 4.0
constexpr int kPointsDryNear = 1;    // 0.25
constexpr int kPointsWetNear = 3;    // 0.75

/// Bedrock's random tick is **three times slower than Java's** - a mean of
/// 204.8 s per block against 68.27 s - and every published growth time is
/// Java's. This is the one number that decides whether a farm takes minutes or
/// an afternoon, so it is stated once here rather than folded into a rate.
constexpr int kRandomTicksPerChunkPerTick = 8;

/// What bone meal does. Wheat, carrots and potatoes jump 2-5 stages; beetroot
/// gets a single stage and only three times in four, which is why it is the
/// one crop bone meal is bad value on.
constexpr bool boneMealIsSingleStage(BlockId cropFamilyId) {
    return cropFamilyId == BlockId::BeetrootCrop0;
}

/// Whether a composter will take this, and how often it succeeds - the
/// reference's own five bands, as percentages.
///
/// **Bamboo is deliberately absent**, and so is anything from an animal: the
/// reference composts plants only.
constexpr int compostChance(ItemId item) {
    switch (item) {
    case ItemId::WheatSeeds:
    case ItemId::BeetrootSeeds:
    case ItemId::MelonSeeds:
    case ItemId::PumpkinSeeds:
    case ItemId::SweetBerries:
    case ItemId::GlowBerries:
    case ItemId::DriedKelp:
        return 30;
    case ItemId::MelonSlice:
    case ItemId::CocoaBeans:
        return 50;
    case ItemId::Apple:
    case ItemId::Beetroot:
    case ItemId::Carrot:
    case ItemId::Potato:
    case ItemId::Wheat:
    case ItemId::NetherWart:
        return 65;
    case ItemId::BakedPotato:
    case ItemId::Bread:
    case ItemId::Cookie:
        return 85;
    case ItemId::PumpkinPie:
        return 100;
    default:
        return 0;
    }
}

constexpr bool isCompostable(ItemId item) { return compostChance(item) > 0; }

/// The level a composter is *ready* at. Eight is a distinct state rather than
/// merely full - seven is a full tub that has not finished, eight is bone meal
/// waiting - and taking from it resets to nothing.
constexpr int kComposterReady = 8;

/// What a mature stem may put its fruit on. The reference checks the block
/// **beneath** the candidate cell, not the cell itself.
constexpr bool supportsFruit(BlockId below) {
    return below == BlockId::Dirt || below == BlockId::Grass || below == BlockId::CoarseDirt ||
           below == BlockId::RootedDirt || below == BlockId::Podzol ||
           below == BlockId::Mycelium || below == BlockId::MossBlock || below == BlockId::Mud ||
           isFarmland(below);
}

/// Chance in a hundred that walking off a fall of `fallBlocks` wrecks tilled
/// ground. The reference's rule is `fallDistance - 0.5`, as a probability -
/// so a one-block step is half likely to ruin it and a long drop is certain.
///
/// **Bedrock has no small-mob exemption**, unlike Java: a chicken really can
/// wreck a field here.
constexpr int trampleChancePercent(float fallBlocks) {
    const float chance = fallBlocks - 0.5f;
    if (chance <= 0.0f) {
        return 0;
    }
    return chance >= 1.0f ? 100 : static_cast<int>(chance * 100.0f);
}

// The points table has to agree with the probabilities the reference publishes,
// and both are cheap enough to prove here rather than trust.
static_assert(growthChance(kPointsWetUnder + 8 * kPointsWetNear) > 0.33f &&
                  growthChance(kPointsWetUnder + 8 * kPointsWetNear) < 0.34f,
              "a plant in the middle of a watered field advances one tick in three");
static_assert(growthChance(kPointsDryUnder) > 0.076f && growthChance(kPointsDryUnder) < 0.077f,
              "dry ground with nothing around it is the reference's 1-in-13");
static_assert(growthChance(0) == 0.0f, "a plant with no ground under it never advances");

static_assert(cropForSeed(ItemId::WheatSeeds) == BlockId::WheatCrop0 &&
                  cropForSeed(ItemId::Bread) == BlockId::Air,
              "only a seed sows");
static_assert(tilledFrom(BlockId::Grass) == BlockId::Farmland &&
                  tilledFrom(BlockId::CoarseDirt) == BlockId::Dirt &&
                  tilledFrom(BlockId::Stone) == BlockId::Stone,
              "a hoe works soil, takes two goes on coarse dirt, and does nothing to rock");

} // namespace game::farming
