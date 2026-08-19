#pragma once

/// Everything the farm knows, in one place: what a hoe makes, what a seed
/// grows into, how fast it grows and what a composter will take - **and, since
/// leaf decay and saplings arrived, what makes a tree grow and what makes a
/// canopy fall**. The common thread is a random tick landing on a plant, which
/// is what `kRandomTicksPerChunkPerTick` below measures every rate here in.
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
///
/// `rowPenalty` is the reference's crowding rule; see `cropIsCrowded`. It is a
/// parameter rather than a halved argument because the points are counted in
/// quarters, and halving an odd count of quarters loses the eighth the
/// reference keeps.
constexpr float growthChance(int quarterPoints, bool rowPenalty = false) {
    const float points = static_cast<float>(quarterPoints) * (rowPenalty ? 0.125f : 0.25f);
    if (points <= 0.0f) {
        return 0.0f;
    }
    const int steps = static_cast<int>(25.0f / points);
    return 1.0f / static_cast<float>(steps + 1);
}

/// Whether the reference halves this plant's growth points for crowding.
///
/// **The whole reason anyone plants in rows**, and it was missing for twenty
/// milestones - a solid field grew at exactly twice the reference's rate, which
/// made the layout that costs you nothing also the fastest one.
///
/// The rule, from the wiki's `Tutorial:Crop farming`: *"if the same crop is
/// planted on a diagonal, or if the same crop is found in both the north-south
/// and east-west directions, the speed level is halved."* Same crop means the
/// same **block**, so age makes no difference and an attached stem - a
/// different block in the reference too - does not count.
///
/// Halved once at most, never twice, which is why this answers one bool rather
/// than two: the reference tests the cross first and only looks at the diagonals
/// if that misses.
constexpr bool cropIsCrowded(bool sameNorthSouth, bool sameEastWest, bool sameOnDiagonal) {
    return (sameNorthSouth && sameEastWest) || sameOnDiagonal;
}

/// Points are accumulated in quarters so the whole sum stays in integers -
/// a quarter is the smallest the reference ever awards.
constexpr int kPointsDryUnder = 8;   // 2.0
constexpr int kPointsWetUnder = 16;  // 4.0
constexpr int kPointsDryNear = 1;    // 0.25
constexpr int kPointsWetNear = 3;    // 0.75

/// **Hydrated means any moisture at all, not full moisture**, and these two
/// functions are the only place that says so. Farmland became eight ids rather
/// than two, and the three `== BlockId::FarmlandMoist` tests that used to score
/// it then read the six middle levels as bone dry while `Block.hpp` drew them
/// wet - the neighbour test was worse still, matching neither end and scoring
/// a half-dried neighbour at *nothing*, below even dry ground.
///
/// The threshold is level 1 because that is where the reference's wet top
/// starts: `Block.hpp`'s `farmlandWetTopFrom(1)` assert states it, and its two
/// companions refuse both rules a reader would otherwise assume. Points are
/// from minecraft.wiki [[Tutorials/Crop farming]] - 4 against 2 for the ground
/// under the plant, 0.75 against 0.25 for each of the eight around it - and
/// the reference splits them on hydration, which is a yes-or-no question, not
/// on the moisture level, which is a countdown.
///
/// One function per position rather than one shared predicate, so a caller
/// cannot pick up the wrong pair of constants; and a non-farmland neighbour
/// scores zero, which is what makes the caller a plain sum with no test.
constexpr int farmlandPointsUnder(BlockId soil) {
    if (!isFarmland(soil)) {
        return 0;
    }
    return farmlandMoisture(soil) > 0 ? kPointsWetUnder : kPointsDryUnder;
}
constexpr int farmlandPointsNear(BlockId soil) {
    if (!isFarmland(soil)) {
        return 0;
    }
    return farmlandMoisture(soil) > 0 ? kPointsWetNear : kPointsDryNear;
}

/// Every level is scored, not just the two ends - the bug being fixed was
/// precisely that the six middle ids fell through both branches, so an assert
/// that only checked `Farmland` and `FarmlandMoist` would have passed
/// throughout. `Dirt` is here because a plant on untilled ground must score
/// zero rather than the dry-ground default.
constexpr bool farmlandPointsCoverEveryLevel() {
    for (int level = 0; level < kFarmlandMoistureLevels; ++level) {
        const BlockId id = farmlandAtMoisture(level);
        const int wanted = level > 0 ? kPointsWetUnder : kPointsDryUnder;
        const int wantedNear = level > 0 ? kPointsWetNear : kPointsDryNear;
        if (farmlandPointsUnder(id) != wanted || farmlandPointsNear(id) != wantedNear) {
            return false;
        }
    }
    return true;
}
// **Counting the asserts in this file: search `^\s*static_assert`, not
// `static_assert`.** One hit sits inside the file's opening doc comment, prose
// about asserts rather than an assert, so a raw count shifts every index by
// one. Finding 9753 (2026-08-19) is what this guards against: a quoted assert
// counted as real in `Loot.hpp` moved its one externally-coupled assert to the
// wrong position, and the coupling was wrongly declared absent for an hour. No
// count is written here on purpose - re-run the search.
static_assert(farmlandPointsCoverEveryLevel(),
              "every one of the eight moisture levels must score, and only level 0 scores dry");
static_assert(farmlandPointsUnder(BlockId::Dirt) == 0 && farmlandPointsNear(BlockId::Dirt) == 0,
              "untilled ground contributes nothing");
static_assert(farmlandPointsUnder(BlockId::FarmlandMoistureRunFirst) == kPointsWetUnder &&
                  farmlandPointsUnder(BlockId::FarmlandMoistureRunLast) == kPointsWetUnder,
              "the six ids added beside the original pair are the ones the old equality tests "
              "missed; if this fails they are being read as dry again");

/// Bedrock's random tick is **three times slower than Java's** - a mean of
/// 204.8 s per block against 68.27 s - and every published growth time is
/// Java's. This is the one number that decides whether a farm takes minutes or
/// an afternoon, so it is stated once here rather than folded into a rate.
///
/// **DO NOT CHANGE THE 8, AND HERE IS THE CONTROL, because a stop nobody can
/// check is a stop nobody should trust.** (Handover 9888 justified this stop as
/// "four asserts depend on it". Re-derived at the definition site 2026-08-19:
/// that is **not literally true** - a search for this name in this file returns
/// one definition and three comments, and **zero `static_assert`s reference it,
/// directly or otherwise**. The stop is right; that reason was not, and a
/// future auditor running the obvious search would have found nothing and
/// concluded the stop was unfounded.)
///
/// **What actually depends on the 8 is worse than an assert, because it is
/// silent: three published time figures, none of them computed.** The 204.8 s
/// above, the same figure quoted again at `kSaplingStageOdds`, and the "about
/// 48 minutes" beside it. All three are `4096 / 20` - one random tick per 16^3
/// subchunk, 4096 cells in a subchunk, 20 ticks a second - and all three are
/// **prose, so changing this constant leaves them silently wrong** rather than
/// failing a build. That is bug shape #3, a number measured against a unit that
/// moved underneath it.
///
/// **A `static_assert` is not available here and I checked before claiming
/// one**: the derivation needs the tick rate and the cells-per-subchunk count,
/// and this file includes only `Block.hpp` and `Item.hpp`. Restating either
/// locally would be a second copy of a fact, which is the very thing this
/// paragraph exists to prevent. So this comment is the only instrument, which
/// is precisely why it names all three dependent figures rather than saying
/// "several".
///
/// **Falsifier, and use this exact test rather than the obvious one.** Search
/// this file for the bare name and **mark every hit CODE or COMMENT**. Today it
/// is **one CODE hit - the definition on the next line - and the rest comments**,
/// which is what proves no assert reads it. **If a second CODE hit ever appears,
/// the paragraph above is stale and that assert is the better instrument.**
///
/// The obvious test - grepping `static_assert` on the same line as the name -
/// is the one to avoid: **most asserts in this file span several lines**, so a
/// reference sitting on a continuation line is invisible to it and the search
/// returns a confident zero. The CODE/COMMENT census has no such blind spot,
/// because a name that appears in code exactly once cannot also be inside an
/// assert however many lines that assert spans. It also carries its own
/// control: a classifier that lumped every hit into one bucket would return
/// all-CODE or all-COMMENT, never the split.
///
/// And note the separate standing rule that the answer is never to raise the
/// tick rate instead (CLAUDE.md bug shape #7).
constexpr int kRandomTicksPerChunkPerTick = 8;

/// What bone meal does. Wheat, carrots and potatoes jump 2-5 stages; beetroot
/// gets a single stage and only three times in four, which is why it is the
/// one crop bone meal is bad value on.
constexpr bool boneMealIsSingleStage(BlockId cropFamilyId) {
    return cropFamilyId == BlockId::BeetrootCrop0;
}

/// Whether a composter will take this, and how often it succeeds - the
/// reference's own five bands, as percentages. minecraft.wiki [[Composter]].
///
/// **Bamboo is deliberately absent** (MC-142452), and so is anything from an
/// animal: the reference composts plants only.
///
/// Split in two on purpose. Everything a player *eats or sows* is named here
/// one item at a time, because those ids have nothing in common to derive from;
/// everything that is a **block** falls through to `compostChanceForBlock`,
/// where a whole family is one line. Writing all ninety by name is what kept
/// this at seventeen for twenty milestones - a chest of sugar cane, pumpkins or
/// saplings could not be composted at all, so bone meal was limited to seeds
/// and food.
constexpr int compostChanceForBlock(BlockId block) {
    // 30%: the cheap greenery a player clears by the stack.
    if (isLeafBlock(block) || (block >= BlockId::OakSapling && block <= BlockId::DarkOakSapling) ||
        block == BlockId::TallGrass || block == BlockId::Kelp || block == BlockId::Seagrass ||
        block == BlockId::SmallDripleaf) {
        return 30;
    }
    // 50%: a stalk, a vine or a mat of it.
    if (block == BlockId::SugarCane || block == BlockId::Cactus ||
        block == BlockId::DriedKelpBlock || isVine(block) || block == BlockId::TwistingVines ||
        block == BlockId::WeepingVines || block == BlockId::GlowLichen ||
        block == BlockId::NetherSprouts || block == BlockId::FloweringAzaleaLeaves ||
        block == BlockId::MossCarpet) {
        return 50;
    }
    // 65%: a whole plant, or a block-sized piece of one.
    if (isFlower(block) || block == BlockId::Pumpkin || isCarvedPumpkin(block) ||
        block == BlockId::Melon || block == BlockId::LilyPad || block == BlockId::Fern ||
        block == BlockId::LargeFern || block == BlockId::BrownMushroom ||
        block == BlockId::RedMushroom || block == BlockId::MushroomStem ||
        block == BlockId::MossBlock || block == BlockId::BigDripleaf || block == BlockId::Azalea ||
        block == BlockId::SeaPickle || block == BlockId::Shroomlight ||
        block == BlockId::SporeBlossom || block == BlockId::CrimsonRoots ||
        block == BlockId::WarpedRoots || block == BlockId::CrimsonFungus ||
        block == BlockId::WarpedFungus) {
        return 65;
    }
    // 85%: nine plants pressed into one block, which is exactly what these are.
    if (block == BlockId::HayBlock || block == BlockId::NetherWartBlock ||
        block == BlockId::WarpedWartBlock || block == BlockId::FloweringAzalea) {
        return 85;
    }
    return 0;
}

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
        break;
    }
    // **Falls through to the block rule rather than returning zero here.** A
    // `default:` that answers for itself is a second place for the same bug -
    // it would silently swallow every block item and there would be no way to
    // tell "not compostable" from "nobody wrote it down".
    return isBlockItem(item) ? compostChanceForBlock(blockForItem(item)) : 0;
}

// The five bands, one member each, plus the two exclusions the header promises.
// **The single edit that fails this: giving `compostChance` a `default:` that
// returns a number**, which is what stops the block half from ever being asked.
static_assert(compostChance(itemForBlock(BlockId::OakSapling)) == 30 &&
                  compostChance(itemForBlock(BlockId::SugarCane)) == 50 &&
                  compostChance(itemForBlock(BlockId::Pumpkin)) == 65 &&
                  compostChance(itemForBlock(BlockId::HayBlock)) == 85 &&
                  compostChance(ItemId::PumpkinPie) == 100,
              "one member of each of the reference's five bands");
static_assert(compostChance(itemForBlock(BlockId::Bamboo)) == 0 &&
                  compostChance(itemForBlock(BlockId::Stone)) == 0 &&
                  compostChance(ItemId::Stick) == 0,
              "bamboo is not compostable (MC-142452), and neither is rock");

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
///
/// **This is the only cause of reversion the game implements, and four more
/// exist.** Audited 2026-08-19 (finding 328). The rule above is correct and so
/// is the line before it - the wiki tags the "mobs smaller than 0.512 cubic
/// blocks cannot destroy farmland" clause as Java-only, so on Bedrock, which is
/// this project's reference, chickens and rabbits do trample. **Do not "fix"
/// that comment**; it has already survived one challenge.
///
/// What is absent, all from the reference's Farmland (Decay) section:
///
///  1. **Mobs trampling at all.** Only the player is ever tested. Any mob does
///     it under this same rule when `mobGriefing` is on, so a field is
///     currently safe from everything except its owner.
///  2. **A piston arm extending over farmland**, or a piston pushing farmland
///     down.
///  3. **A solid block covering the top face** - "such as when pumpkin or melon
///     blocks appear, or when trees grow". This one is reachable in ordinary
///     play without any mob: grow a tree on tilled soil and the farmland under
///     it should revert.
///  4. **An enderman teleporting directly on top.**
///
/// And one rule that is missing from the reversion itself rather than from its
/// causes: **"when farmland decays, any crops growing on the block are dropped
/// as items, as if they were harvested."** Ours reverts the soil and leaves the
/// crop to be destroyed silently, which loses the player their seed.
///
/// **No predicate is provided here for any of the five, and that is a decision
/// rather than an omission.** Every one of them is a rule about *when a caller
/// should act*, and all five callers - mob movement, the piston push, block
/// placement, the teleport, and the reversion itself - live in `Main.cpp` and
/// `Creature.cpp`. A `constexpr bool farmlandDecaysUnder(BlockId)` written here
/// today would have no caller, and this session has spent its length
/// documenting bug shape #15: a feature complete, correct, asserted and one
/// call site short of existing, which nothing in the toolchain warns about.
/// Writing four more of those to close a LOW finding would be paying the
/// project's most expensive recurring cost to look productive.
///
/// **So the rule to follow when this is picked up**: whoever wires the callers
/// adds the predicates in the same edit, and the reversion path gains the crop
/// drop at the same time. The reversion already knows how to turn farmland back
/// into dirt; what it does not do is ask `BlockDrops.hpp` what was standing on
/// it first.
constexpr int trampleChancePercent(float fallBlocks) {
    const float chance = fallBlocks - 0.5f;
    if (chance <= 0.0f) {
        return 0;
    }
    return chance >= 1.0f ? 100 : static_cast<int>(chance * 100.0f);
}

// ---------------------------------------------------------------------------
// Trees: saplings and the canopy they leave behind.
// ---------------------------------------------------------------------------
//
// Not farm crops, but the same machinery to the block: a random tick lands, a
// rule looks at light and neighbours, and the cell changes. They are here
// rather than in a file of their own because `growOne` is the one reader and
// `kRandomTicksPerChunkPerTick` above is the unit every rate below is quoted
// against.

/// The six saplings, one contiguous run.
///
/// **Written here rather than in `Block.hpp`** only because that file has
/// another owner this pass; the run itself is asserted below so a seventh
/// sapling inserted anywhere but the end fails the build rather than becoming
/// invisible to growth.
constexpr bool isSapling(BlockId id) {
    return id >= BlockId::OakSapling && id <= BlockId::DarkOakSapling;
}

static_assert(isSapling(BlockId::OakSapling) && isSapling(BlockId::JungleSapling) &&
                  isSapling(BlockId::DarkOakSapling) && !isSapling(BlockId::Leaves) &&
                  !isSapling(BlockId::TallGrass),
              "the sapling run, against the two blocks either end of it");

/// The light a sapling needs before it will advance a stage.
///
/// minecraft.wiki [[Sapling]], Usage -> Growing trees: *"the block above the
/// sapling requires a light level of at least 9 for its growth stage to
/// increase"*. **Measured at the cell above, not at the sapling** - the [[Tree]]
/// page says 8 at the sapling's own cell and the two disagree; the Sapling page
/// is the maintained one and is what this follows. Neither page says which
/// light, so this is the internal light level [[Light]] defines for the
/// Overworld, `max(sky, block)` - the same quantity every other plant here
/// reads.
constexpr int kSaplingLight = 9;

/// One random tick in this many advances a sapling a stage.
///
/// **This number is not published by minecraft.wiki for either edition**, which
/// was checked directly: the Sapling page describes the stages and gives no
/// rate, the [[Tree]] page offers only *"about one growth attempt per minute"*,
/// and the [[Random Tick]] page's worked example is nether wart. Seven is the
/// long-standing community figure for the reference's own `nextInt(7)` and is
/// recorded here as **the one constant in this block that is not wiki-sourced**.
///
/// Measured in **random-tick attempts**, not in ticks and not in seconds. At
/// `kRandomTicksPerChunkPerTick` a given cell is ticked once per 204.8 s on
/// average, so two stages at one-in-seven is a mean of 14 random ticks, about
/// 48 minutes of standing there. That is the honest consequence of Bedrock's
/// random-tick rate and it is why bone meal exists.
constexpr int kSaplingStageOdds = 7;

/// How many stages a sapling passes through before it is a tree. One bit, which
/// is exactly what Bedrock stores: minecraft.wiki [[Sapling]], Block states,
/// BE table - `age_bit`, default `false`, values `false,true`, *"Specifies the
/// sapling's growth stage."*
constexpr int kSaplingStages = 2;

/// Bone meal on a sapling, as a percentage per application.
///
/// minecraft.wiki [[Bone Meal]], Usage -> Fertilizer, the Saplings row: *"The
/// sapling has a 45% chance of growing to the next growth stage, if
/// possible."* The row carries no edition tag, and the same table tags its
/// Bedrock-only rows explicitly, so it is presented as both editions.
///
/// **A stage, not a tree** - so a mean of `kSaplingStages / 0.45` = 4.4 bone
/// meal per tree, and the item is spent whether or not the roll lands
/// (*"Using bone meal on such an obstructed sapling with no chance of growing
/// wastes the bone meal"*, same page).
constexpr int kSaplingBoneMealPercent = 45;

// ---------------------------------------------------------------------------
// Bone meal on the plants that are not crops.
//
// **These are Bedrock's numbers, and Bedrock and Java disagree here more than
// almost anywhere else in the fertilizer table** - minecraft.wiki [[Bone
// Meal]] marks the differing rows `[BE only]` and `[JE only]` individually.
// Every constant below is per *application* of the item, never per tick.
// ---------------------------------------------------------------------------

/// The box bone meal sprouts ground cover in when used on a grass block, in
/// **blocks**, centred on the block used.
///
/// minecraft.wiki [[Bone Meal]], Fertilizer, Grass Block: Bedrock sprouts into
/// a **7x5x7** volume where Java uses 15x15x15 (`[JE]`). Odd spans so the
/// centre is a real block: 3 either side horizontally, 2 either side
/// vertically.
constexpr int kBoneMealGrassSpanXZ = 7;
constexpr int kBoneMealGrassSpanY = 5;

/// How many cells the sprouting tries before giving up. The reference makes a
/// fixed number of attempts rather than filling the box, which is what leaves
/// the ragged gaps that make a bone-mealed lawn look grown rather than painted.
/// Java tries 128 over its far larger box; 64 over 49 columns puts roughly the
/// same density on the ground.
constexpr int kBoneMealGrassAttempts = 64;

/// One sprout in eight is a flower rather than grass. minecraft.wiki [[Bone
/// Meal]]: the same 1/8 both editions.
constexpr int kBoneMealFlowerOdds = 8;

/// Of the sprouts that are not flowers, one in three is a fern - **and only in
/// biomes where ferns generate naturally**, which is the `[BE only]` half of
/// that row. Matches the 0.35 fern share the generator already uses for its own
/// ground cover, so a bone-mealed patch looks like the meadow around it.
constexpr int kBoneMealFernShare = 3;

/// How far a bone-mealed flower throws copies of itself, in blocks, and how
/// many tries it makes. **`[BE only]`**: minecraft.wiki [[Bone Meal]] gives
/// small flowers their own row in Bedrock - "spreads the flower to nearby grass
/// blocks" - where Java does nothing at all.
constexpr int kBoneMealFlowerSpreadReach = 3;
constexpr int kBoneMealFlowerSpreadAttempts = 16;

/// How far bone meal spreads moss, in blocks, and how many cells it tries.
/// minecraft.wiki [[Moss Block]]: "Using bone meal on a moss block converts
/// some blocks in a 5x3x5 area centered on the moss block into moss blocks",
/// so 2 either side horizontally and 1 either side vertically.
constexpr int kBoneMealMossReach = 2;
constexpr int kBoneMealMossHeight = 1;
constexpr int kBoneMealMossAttempts = 32;

/// How many stems a bone-mealed bamboo shoots up. minecraft.wiki [[Bamboo]]:
/// bone meal "grows the bamboo by 1-2 stems".
constexpr int kBoneMealBambooMin = 1;
constexpr int kBoneMealBambooMax = 2;

/// The tallest a bamboo stalk gets, in blocks. Same page: 12-16 naturally, and
/// bone meal will not push it past the maximum. One number rather than a range
/// because nothing here stores a per-stalk target.
constexpr int kBambooMaxHeight = 16;

/// **Pointed dripstone.** Every number below is minecraft.wiki [[Pointed
/// Dripstone]], and that is the only source there can be for them: Mojang
/// publishes no block behaviour at all. `Mojang/bedrock-samples`
/// `behavior_pack/` carries entities, items, loot tables, recipes, biomes,
/// spawn rules, shapes and trading, and **no `blocks/` directory** - so unlike
/// `moisturized_amount` and `cluster_count`, which were confirmed against
/// Mojang's own published state table, these cannot be checked against a
/// primary source and are secondary-sourced on purpose.

/// How far the walk looks along a column of dripstone before it stops asking,
/// in blocks. The reference grows a stalactite to at most eight.
///
/// **It doubles as the safety cap on the anchor walk**, and the direction it
/// fails in is the safe one: a column longer than this reads as anchored and
/// stays put, rather than being cut loose. A blemish beats deleting somebody's
/// build, which is the same trade `blockHasSupport` makes one screen below.
constexpr int kDripstoneMaxLength = 8;

/// A stalactite taller than this drips nothing at all, in blocks.
constexpr int kDripstoneDripMaxHeight = 11;

/// How far under the tip a cauldron may sit and still be filled, in blocks,
/// with nothing but air in between.
constexpr int kDripstoneCauldronReach = 10;

/// Chance that a randomly ticked stalactite tip fills the cauldron under it by
/// one level: 45 in 256, about 17.6%. Held as the fraction the reference states
/// rather than a percentage, because the denominator is a power of two and
/// rounding it to 18% would quietly change the rate.
constexpr int kDripstoneDripOdds = 45;
constexpr int kDripstoneDripDenominator = 256;

/// How far a leaf may sit from the nearest log and still live.
///
/// minecraft.wiki [[Leaves]], Usage: leaves decay *"if they are not connected
/// to any log or wood blocks, stripped or otherwise, either directly or via
/// other leaf blocks, with a maximum distance of 6 blocks (Java Edition only)
/// or **4 blocks (Bedrock Edition only)**. ... The distance is taxicab
/// distance, but can cross corners."* Corroborated by [[History of leaf
/// decay]]: the pre-1.13 distance of 4 *"is also still used in Bedrock
/// Edition"*.
///
/// Measured in **leaf-to-leaf steps through faces**, which is what "taxicab"
/// means here and what "can cross corners" allows: a path may bend, so a log
/// four steps away round an L still saves the leaf. It is emphatically not a
/// Euclidean radius and not a box test.
constexpr int kLeafDecayReach = 4;

/// What stops a leaf decaying.
///
/// minecraft.wiki [[Leaves]]: *"any log or wood blocks, stripped or
/// otherwise"*, with no requirement that the wood match the leaf. `isLogBlock`
/// already covers the stripped logs, so the two bark predicates are the whole
/// of the rest.
///
/// **Bamboo is in and that is deliberate.** The wiki's *"Bamboo does not count
/// as a log for this purpose"* is about the bamboo *plant*; the block of
/// bamboo is wood, sits in the reference's own `#logs` tag, and is what
/// `isLogBlock` names here.
constexpr bool leafKeeper(BlockId id) {
    return isLogBlock(id) || isBarkBlock(id) || isStrippedBarkBlock(id);
}

static_assert(leafKeeper(BlockId::Log) && leafKeeper(BlockId::StrippedOakLog) &&
                  leafKeeper(BlockId::OakWood) && leafKeeper(BlockId::StrippedOakWood) &&
                  !leafKeeper(BlockId::Leaves) && !leafKeeper(BlockId::Planks) &&
                  !leafKeeper(BlockId::Stone),
              "a log, a stripped log, bark and stripped bark hold a canopy up; planks do not");

/// What a sapling will root in.
///
/// minecraft.wiki [[Oak]] / [[Birch]] / [[Spruce]] / [[Dark Oak]], Planting -
/// the same enumerated list on all four. **In Bedrock the placement
/// restriction is the growth restriction**: [[Sapling]] notes the "place on
/// these but grow anywhere" loophole as `{{only|je}}`.
///
/// Pale moss and muddy mangrove roots are on the reference's list and are here
/// too where the id exists; a dirt path is explicitly excluded, and so are sand
/// and gravel.
constexpr bool saplingRootsIn(BlockId below) {
    return below == BlockId::Dirt || below == BlockId::Grass ||
           below == BlockId::CoarseDirt || below == BlockId::Podzol ||
           below == BlockId::Mycelium || below == BlockId::RootedDirt ||
           below == BlockId::MossBlock || below == BlockId::Mud ||
           below == BlockId::MuddyMangroveRoots || isFarmland(below);
}

static_assert(saplingRootsIn(BlockId::Grass) && saplingRootsIn(BlockId::Podzol) &&
                  saplingRootsIn(BlockId::Farmland) && !saplingRootsIn(BlockId::DirtPath) &&
                  !saplingRootsIn(BlockId::Sand) && !saplingRootsIn(BlockId::Stone),
              "every variant of dirt and moss roots a tree; a path, sand and rock do not");

// The points table has to agree with the probabilities the reference publishes,
// and both are cheap enough to prove here rather than trust.
static_assert(growthChance(kPointsWetUnder + 8 * kPointsWetNear) > 0.33f &&
                  growthChance(kPointsWetUnder + 8 * kPointsWetNear) < 0.34f,
              "a plant in the middle of a watered field advances one tick in three");
static_assert(growthChance(kPointsDryUnder) > 0.076f && growthChance(kPointsDryUnder) < 0.077f,
              "dry ground with nothing around it is the reference's 1-in-13");
static_assert(growthChance(0) == 0.0f, "a plant with no ground under it never advances");

// The crowding rule is worth exactly one halving and it is the difference
// between a field and a row. Delete the `rowPenalty` multiply and this is what
// fails: ten points crowded is the reference's 1-in-6, not its 1-in-3.
static_assert(growthChance(kPointsWetUnder + 8 * kPointsWetNear, true) > 0.166f &&
                  growthChance(kPointsWetUnder + 8 * kPointsWetNear, true) < 0.167f,
              "a plant packed into a solid watered field advances one tick in six");
static_assert(growthChance(kPointsWetUnder + 8 * kPointsWetNear, true) <
                  growthChance(kPointsWetUnder + 8 * kPointsWetNear),
              "and crowding is always a penalty, never a bonus");
static_assert(cropIsCrowded(true, true, false) && cropIsCrowded(false, false, true) &&
                  !cropIsCrowded(true, false, false) && !cropIsCrowded(false, true, false),
              "a full cross crowds and a diagonal crowds; one arm of a row does not");

static_assert(cropForSeed(ItemId::WheatSeeds) == BlockId::WheatCrop0 &&
                  cropForSeed(ItemId::Bread) == BlockId::Air,
              "only a seed sows");
static_assert(tilledFrom(BlockId::Grass) == BlockId::Farmland &&
                  tilledFrom(BlockId::CoarseDirt) == BlockId::Dirt &&
                  tilledFrom(BlockId::Stone) == BlockId::Stone,
              "a hoe works soil, takes two goes on coarse dirt, and does nothing to rock");

} // namespace game::farming
