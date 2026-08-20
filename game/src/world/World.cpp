#include "world/World.hpp"

#include "world/ChunkMesher.hpp"
#include "world/Biome.hpp"
#include "world/Copper.hpp"
#include "world/Farming.hpp"
#include "world/Fluid.hpp"
#include "world/Structures.hpp"
#include "world/Tick.hpp"
#include "world/Weather.hpp"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

namespace game {
namespace {

using Clock = std::chrono::steady_clock;

/// Floor division, correct for negative coordinates. Plain integer division
/// truncates toward zero, which puts blocks at -1 and 0 in the same chunk.
int floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

int floorMod(int value, int divisor) {
    const int remainder = value % divisor;
    return remainder < 0 ? remainder + divisor : remainder;
}

int chebyshevDistance(const ChunkCoord& a, const ChunkCoord& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

/// The six directions light travels.
constexpr std::array<glm::ivec3, 6> kLightSteps{glm::ivec3{1, 0, 0},  glm::ivec3{-1, 0, 0}, glm::ivec3{0, 1, 0},
                                                glm::ivec3{0, -1, 0}, glm::ivec3{0, 0, 1},  glm::ivec3{0, 0, -1}};

/// Whether light of **either** channel may enter a cell at all.
///
/// **This is `isLightTransparent`, and saying so needs a word about why it is
/// not obviously so.** The two transparency predicates disagreed for twenty
/// milestones: 368 ids - every fence, wall, gate, rail, redstone line,
/// tripwire, snow layer, moss carpet and the 168 `Model` blocks - were
/// sky-transparent and light-opaque, so a walk gated on `isLightTransparent`
/// alone threw all of them away. The `isSkyTransparent` test further down each
/// walk was dead code for them, a placed fence darkened its own cell and the
/// column under it while the *same* fence reloaded from disk was fully bright
/// (`seedColumnLight` asks `isSkyTransparent`), and a torch would not light
/// through a paddock rail. Lighting depended on whether a block had been placed
/// or generated, which is why nobody caught it.
///
/// `Block.hpp` now derives `isLightTransparent` *from* `isSkyTransparent`, so
/// the containment is the definition rather than something proved over the ids
/// - a sweep that walks the derivation can only restate it - and the single
/// wider predicate is the whole gate, where a union here would be duplication
/// rather than safety. **The assert below is where the derivation is actually
/// held**: an oak fence is sky-transparent and is not a cutout, so dropping the
/// `isSkyTransparent` term stops this file compiling.
///
/// This is only the "can light enter" question. **How far it gets is still two
/// different answers** - the sky's free fall asks `isSkyTransparent` on its own,
/// which is what makes a canopy shade the ground.
constexpr bool admitsLight(BlockId id) { return isLightTransparent(id); }

// One member of each direction, because the containment this file depends on
// lives in another one. **The single edit that fails this: dropping the
// `isSkyTransparent(id) ||` term from `isLightTransparent` in `Block.hpp`**,
// which is exactly the state that produced the 368-id divergence. An oak fence
// is sky-transparent, so it must be light-transparent too; oak leaves are the
// one direction that stays a gap, and it is the gap that makes shade.
static_assert(isLightTransparent(BlockId::PlanksFence) && isSkyTransparent(BlockId::PlanksFence) &&
                  isLightTransparent(BlockId::Leaves) && !isSkyTransparent(BlockId::Leaves),
              "sky transparency has to imply light transparency, or a placed fence is darker "
              "than a generated one");
// And the counterweight, because widening a gate is the obvious next mistake:
// rock still stops light dead. Fails if either predicate ever admits a full
// opaque cube.
static_assert(!admitsLight(BlockId::Stone) && !admitsLight(BlockId::Dirt),
              "a solid cube stops both channels");

/// The four directions water spreads sideways.
constexpr std::array<glm::ivec3, 4> kFlowSteps{glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0}, glm::ivec3{0, 0, 1},
                                               glm::ivec3{0, 0, -1}};

/// Whether a block sitting directly on top of another covers its top surface -
/// the reference's own words for what turns tilled ground and a path back into
/// dirt. minecraft.wiki [[Farmland]]: "A solid block covers the top surface of
/// the farmland block".
///
/// **`occludesFace` alone is not quite it**, because it asks the *renderer's*
/// question - "is the face behind you invisible" - and answers no for glass and
/// for anything else see-through. A pane of glass laid over a field is still a
/// floor, so the shape test carries the cases the opacity test drops. A fence,
/// a torch, a rail and a carpet fail both, which is the point: they leave the
/// ground beneath them workable exactly as the reference does.
constexpr bool coversTopOf(BlockId placed) {
    return occludesFace(placed, 1) ||
           (blockShape(placed) == BlockShape::Full && game::isSolid(placed));
}

// The single edit that fails this: widening it to plain `isSolid`, which would
// have every fence post and every rail spoil the field under it.
static_assert(coversTopOf(BlockId::Melon) && coversTopOf(BlockId::Glass) &&
                  coversTopOf(BlockId::Dirt) && !coversTopOf(BlockId::PlanksFence) &&
                  !coversTopOf(BlockId::Torch) && !coversTopOf(BlockId::Air),
              "a floor covers the ground; a post standing on it does not");

/// The four cells a ripe stem can fruit into, in the reference's own order:
/// east, west, north, south.
///
/// **The attached-stem ids run in this same order**, so a stem's facing is an
/// index into this table - which is what lets a fruit being taken find the stem
/// that grew it with four reads and no search.
constexpr std::array<glm::ivec3, 4> kFruitSides{glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0},
                                                glm::ivec3{0, 0, -1}, glm::ivec3{0, 0, 1}};

/// How many consecutive ids the small-flower run spans, derived from its own
/// endpoints so that bone meal's picker cannot drift away from it.
///
/// `Block.hpp` owns the run and nothing there points back here, so writing the
/// length as a literal would be a second answer to "how many small flowers are
/// there?" sitting in a file that does not own the question. Deriving it means
/// an id inserted inside the run is still spanned.
///
/// **The assert is doing what the derivation cannot.** The condition this code
/// actually needs is not "span the run" but "every id in the run is a small
/// flower", and no expression can check that - only a reader can. The margin is
/// one: `BrownMushroom` sits immediately after `OrangeTulip`, so a run one id
/// too long plants mushrooms out of bone meal, and one too short never picks a
/// tulip. Failing the build is the correct response to that boundary moving.
constexpr int kSmallFlowerRun =
    static_cast<int>(BlockId::OrangeTulip) - static_cast<int>(BlockId::Cornflower) + 1;
static_assert(kSmallFlowerRun == 6,
              "the Cornflower..OrangeTulip run changed length - re-read it in Block.hpp and "
              "confirm every id in it is still a small flower before changing this number; "
              "BrownMushroom sits immediately after OrangeTulip");

/// Whether a block has anything to do when a random tick lands on it.
///
/// **One list, sitting between the sampler and `growOne`.** Every entry here is
/// a branch there and every branch there needs an entry here, and the two were
/// three lines apart in the same file for twenty milestones while sugar cane,
/// cactus and cocoa grew in neither: adding the branch is the obvious half and
/// the filter is the half nothing reminds you about. Naming the set once is
/// what makes the omission visible.
constexpr bool randomTicks(BlockId id) {
    return isFarmland(id) || isCropBlock(id) || isGrowingStem(id) || isNetherWart(id) ||
           isCocoa(id) || id == BlockId::SugarCane || id == BlockId::Cactus ||
           id == BlockId::Grass || id == BlockId::Mycelium || id == BlockId::Ice ||
           isSnowLayer(id) || id == BlockId::BuddingAmethyst || farming::isSapling(id) ||
           isLeafBlock(id) || id == BlockId::PointedDripstone ||
           // **Not `copper::isCopper`.** Waxed copper and oxidized copper have
           // nothing left to do, and a wall of them is a great many cells for
           // the sampler to keep waking up for nothing; asking the weathering
           // table directly is the same question as "has this a next stage".
           copper::weathered(id) != BlockId::Air;
}

// **The single edit that fails this: deleting a branch of `growOne` without
// deleting its line above**, or adding one without adding its line. Stone is
// the counterweight - the sampler spends most of its draws on it, so a
// predicate that ever answered yes for it would cost a hash lookup per draw.
static_assert(randomTicks(BlockId::SugarCane) && randomTicks(BlockId::Cactus) &&
                  randomTicks(BlockId::Ice) && !randomTicks(BlockId::Stone) &&
                  !randomTicks(BlockId::Air) && !randomTicks(BlockId::PackedIce),
              "the random-tick set is the set of branches growOne has");
// The dripping stalactite, written against the block it hangs from: only the
// pointed one has anything to do, and a dripstone block that ticked would wake
// the sampler for every cell of a cave wall. **This pair is why the branch in
// `growOne` is reachable at all** - the filter above is the gate, and a branch
// added without its entry here is dead code no warning would have mentioned.
static_assert(randomTicks(BlockId::PointedDripstone) && !randomTicks(BlockId::DripstoneBlock),
              "a stalactite has a random tick to spend; the block it hangs from has not");
// The three branches added with saplings, leaf decay and copper. Each is
// written against the thing next to it that must *not* tick, because a
// predicate that widened to cover both would pass a bare "does it tick".
static_assert(randomTicks(BlockId::OakSapling) && randomTicks(BlockId::Leaves) &&
                  randomTicks(BlockId::CopperBlock) && randomTicks(BlockId::WeatheredCutCopper) &&
                  !randomTicks(BlockId::WaxedCopperBlock) &&
                  !randomTicks(BlockId::OxidizedCopper) && !randomTicks(BlockId::Log),
              "a sapling, a leaf and unwaxed copper below the last stage tick; waxed copper, "
              "oxidized copper and a log do not");

/// A delay written in ticks, which is the unit every number this project ports
/// is published in, and converted once here.
///
/// **`Tick.hpp` is the single owner of the rate** and a millisecond literal is
/// the one spelling nothing can check against it: 250 and 1500 were correct and
/// said so nowhere, so a tick-rate change would have left them behind without
/// failing anything. The asserts below are the linkage.
constexpr std::chrono::milliseconds fromTicks(int ticks) {
    return std::chrono::milliseconds{
        static_cast<std::int64_t>(static_cast<double>(ticks) * static_cast<double>(tick::kSeconds) * 1000.0)};
}

/// One block every five ticks, which is the reference's water flow speed -
/// minecraft.wiki [[Fluid]], "Water moves at 1 block every 5 ticks".
constexpr std::chrono::milliseconds kFluidSpreadDelay = fromTicks(5);

/// Thirty ticks, the reference's Overworld lava - minecraft.wiki [[Fluid]],
/// "lava in the Overworld and the End ... moves at only 1 block every 30 game
/// ticks". Six times slower than water, and combined with a level step of two
/// it is what makes lava creep three blocks where water runs seven.
constexpr std::chrono::milliseconds kLavaSpreadDelay = fromTicks(30);

// The two delays the reference publishes, in the milliseconds they have always
// been. **The single edit that fails these: changing `tick::kSeconds`**, which
// is exactly the change that used to leave both of these numbers stale.
static_assert(kFluidSpreadDelay == std::chrono::milliseconds{250} &&
                  kLavaSpreadDelay == std::chrono::milliseconds{1500},
              "five ticks and thirty ticks at the reference's 20 Hz");

/// Eighty ticks. The reference's fuse for anything lit by hand, by fire or by
/// redstone; only a charge set off by another blast gets a shorter one.
constexpr std::chrono::milliseconds kTntFuse{4000};

/// How fast a lit charge blinks. **An animation rate, and nothing else.**
///
/// This was 250ms beside a `kTntBlinks = 16` whose product was the fuse, which
/// made one number mean two things: the only way to shorten a fuse was to hand
/// out fewer blinks, so a chain fuse would have changed how fast the charge
/// *flashes* rather than how long it lives. The count is now derived from the
/// fuse in `primeTnt`, which leaves this free to be the reference's own figure
/// - minecraft.wiki, *TNT*: "Primed TNT's texture blinks, alternating every 0.5
/// seconds". Eight half-second states still spend `kTntFuse` exactly.
constexpr std::chrono::milliseconds kTntBlink{500};

/// A charge set off by another blast rather than by hand, fire or redstone.
///
/// **Primary source, and it disagrees with the wiki about the upper end.**
/// `Mojang/bedrock-samples`, `behavior_pack/entities/tnt.json`, read
/// 2026-08-19: the `from_explosion` component group carries
/// `minecraft:explode` with `"fuse_length": {"range_min": 0.5, "range_max":
/// 2.0}`. **The unit there is seconds** - the same file gives the base
/// component `"fuse_length": 4`, which is our hand-lit `kTntFuse` of 4000ms -
/// so the range is 500ms to **2000ms**, not the 1500 that stood here. The wiki
/// says "between 10 and 30 game ticks (0.5 to 1.5 seconds)" and does not mark
/// that per edition; Mojang's own Bedrock pack does, and Bedrock is this
/// project's reference. The minimum agrees exactly, which is what makes the
/// one field that does not worth trusting rather than suspecting a unit slip.
///
/// The **structure** is confirmed by the same file rather than merely allowed
/// by it: the short randomised fuse lives in a component group added by a
/// `from_explosion` event, so the reference also gives it *only* to a charge
/// set off by another blast and leaves fire, hand and redstone ignition on the
/// flat four seconds. That is exactly the split between `primeTnt(at)` and
/// `primeTnt(at, blastFuse())`; do not collapse them.
///
/// Without this a chain ripples on the full four seconds instead of a stack
/// going up as one blast.
constexpr std::chrono::milliseconds kTntChainFuseMin{500};
constexpr std::chrono::milliseconds kTntChainFuseMax{2000};

// `primeTnt` divides the fuse by the blink period; fails if anyone sets the
// cadence to zero to stop the blinking.
static_assert(kTntBlink.count() > 0, "The blink period is a divisor.");
// Fails the moment the chain range is edited past a hand-lit fuse, at which
// point chaining would slow a stack down rather than setting it off together.
static_assert(kTntChainFuseMin <= kTntChainFuseMax && kTntChainFuseMax < kTntFuse,
              "A blast-lit charge must go sooner than a hand-lit one.");

/// A fuse split into what `updateTnt` actually walks: how long until the first
/// blink, and how many blinks follow it. The remainder rides in the *first*
/// interval because that is the least visible place to put it - the alternative
/// is a last interval of odd length, which is the one the player is watching.
struct TntSchedule {
    std::chrono::milliseconds first;
    int blinks;
};

constexpr TntSchedule tntSchedule(std::chrono::milliseconds fuse) {
    const std::int64_t intervals = std::max<std::int64_t>(1, fuse / kTntBlink);
    return {fuse - kTntBlink * (intervals - 1), static_cast<int>(intervals - 1)};
}

/// What `updateTnt` spends: the first wait, then one `kTntBlink` per blink.
constexpr std::chrono::milliseconds tntFuseSpent(std::chrono::milliseconds fuse) {
    return tntSchedule(fuse).first + kTntBlink * tntSchedule(fuse).blinks;
}

// **The whole expression the real reader evaluates**, not one side of it: the
// schedule must spend the fuse it was given, exactly, at both ends of the chain
// range and for a value that divides evenly into neither. Any of these fails
// on the single edit of rounding `intervals` up instead of down, or of moving
// the remainder to the last interval without telling `updateTnt` about it.
static_assert(tntFuseSpent(kTntFuse) == kTntFuse, "A hand-lit fuse must last exactly 80 ticks.");
static_assert(tntFuseSpent(kTntChainFuseMin) == kTntChainFuseMin, "Shortest chain fuse drifted.");
static_assert(tntFuseSpent(kTntChainFuseMax) == kTntChainFuseMax, "Longest chain fuse drifted.");
static_assert(tntFuseSpent(std::chrono::milliseconds{730}) == std::chrono::milliseconds{730},
              "A fuse that is not a whole number of blinks must still last exactly as long.");
// A fuse shorter than one blink never flashes, but must still go off on time -
// fails if the blink count is ever derived without the floor at one.
static_assert(tntSchedule(std::chrono::milliseconds{200}).blinks == 0 &&
                  tntFuseSpent(std::chrono::milliseconds{200}) == std::chrono::milliseconds{200},
              "A sub-blink fuse must not go negative.");

/// The reference re-evaluates a fire every 30-40 ticks. One figure in the
/// middle of that band is enough here, because nothing else keys off the exact
/// number.
constexpr std::chrono::milliseconds kFireTickDelay{1750};

/// How readily each fuel catches from a fire already burning beside it - the
/// reference's **burn odds**, out of a denominator that depends on direction.
///
/// **These are the reference's numbers. The two they replace were ours.**
/// `kFireSpreadPercent = 25` and `kFireBurnoutPercent = 40` were invented and
/// said so, which is honest but still wrong in a way a player meets: every fuel
/// in the game caught at the same rate, so a wool rug and an oak log burned
/// identically and a leaf canopy - which the reference makes the fastest thing
/// in the world to lose - was no quicker than the trunk under it.
///
/// minecraft.wiki [[Fire]], "Spread": the chance a neighbouring block catches
/// on a given fire tick is `burnOdds / 300` to the sides and `burnOdds / 250`
/// up and down. Unitless odds, not a percentage; the denominator is what turns
/// them into one. The table below is the wiki's flammability table, read
/// through the same family predicates `isFlammable` uses so the two cannot name
/// different sets - anything `isFlammable` accepts gets an answer here.
constexpr unsigned kBurnDenominatorSide = 300;
constexpr unsigned kBurnDenominatorVertical = 250;

constexpr unsigned burnOdds(BlockId id) {
    // Wood, in every form the reference calls wood, and the two blocks that are
    // compressed fuel. 5 is the lowest in the table and is why a log wall is a
    // real firebreak.
    if (isLogBlock(id) || isBarkBlock(id) || isStrippedBarkBlock(id) || id == BlockId::CoalBlock) {
        return 5;
    }
    // 100: everything the reference expects to be gone the instant fire reaches
    // it. Ground cover, flowers, vines, lichen and a live charge.
    if (id == BlockId::TallGrass || id == BlockId::Fern || id == BlockId::LargeFern ||
        id == BlockId::DeadBush || isFlower(id) || isTallFlower(id) ||
        id == BlockId::SweetBerryBush || id == BlockId::GlowLichen || isVine(id) ||
        id == BlockId::SporeBlossom || id == BlockId::BigDripleaf ||
        id == BlockId::SmallDripleaf || isTntBlock(id)) {
        return 100;
    }
    // 60: the middle of the table. Leaves and wool are the two a player notices,
    // because a canopy goes up roughly three times faster than the trunk.
    if (isLeafBlock(id) || isWoolBlock(id) || isCarpet(id) || id == BlockId::Bamboo ||
        id == BlockId::Scaffolding || id == BlockId::DriedKelpBlock || id == BlockId::Azalea ||
        id == BlockId::FloweringAzalea || id == BlockId::HangingRoots ||
        id == BlockId::CaveVines || id == BlockId::CaveVinesBerries) {
        return 60;
    }
    // 20: worked wood and the wooden workstations. A plank burns four times as
    // readily as the log it was cut from, which is the reference's rule and not
    // a mistake in the reading.
    //
    // **Asked through `shapedParent` for the same reason `isFlammable` does**:
    // a wooden stair, slab, fence or gate is its planks, and writing the six
    // hundred ids out here would be a second list to keep agreeing with that
    // one. A shaped block whose parent is not wood never reaches this line -
    // `isFlammable` has already refused it.
    const BlockId material = shapedParent(id);
    if (material != id) {
        return burnOdds(material);
    }
    return 20;
}

// Every fuel has to have an answer, and the only way to be sure is to ask the
// two functions the same question. The single edit that fails this: adding a
// family to `isFlammable` without adding it here, which would silently give it
// planks' 20 - the exact shape of `CLAUDE.md`'s "a `default:` that returns a
// real value hides every missing entry".
static_assert(burnOdds(BlockId::Log) == 5 && burnOdds(BlockId::Planks) == 20 &&
                  burnOdds(BlockId::Leaves) == 60 && burnOdds(BlockId::WhiteWool) == 60 &&
                  burnOdds(BlockId::TallGrass) == 100,
              "the five odds a player can tell apart by watching a forest burn");

/// The chance a fire with no fuel under it goes out on a given tick.
///
/// minecraft.wiki [[Fire]], "Ticking": at age 15, with no flammable block
/// below, a block tick has a **1/4** chance to extinguish the fire. Ours was
/// 40% and invented.
///
/// **The age is not modelled and that changes what this means**, so it is said
/// here rather than discovered later: `BlockId::Fire` is one id with no age, so
/// this fires from the first tick instead of after fifteen. A fire on stone
/// with nothing to eat therefore dies sooner than the reference's would. The
/// alternative is sixteen fire ids in `Block.hpp`, which is that file's call.
constexpr unsigned kFireBurnoutOdds = 1;
constexpr unsigned kFireBurnoutDenominator = 4;

/// How many un-drained hisses are worth keeping.
///
/// **An engineering bound, not a reference number** - there is no such limit in
/// the reference, because its world and its mixer are not separated by a queue.
/// A lava lake meeting an ocean converts a whole front at once and no ear can
/// tell forty simultaneous hisses from eight, so the only thing an unbounded
/// buffer buys is the memory to hold sounds nobody will hear. It also puts a
/// ceiling on the one ordering that can go wrong: a loop that runs `update`
/// without draining - the loading screen does exactly this - would otherwise
/// accumulate every conversion of the entire load and fire them in one wall of
/// noise on the first frame of play.
constexpr std::size_t kMaxPendingFizzes = 64;

/// One block of fall per tick, which is what a falling block does in the
/// reference: it is an entity under gravity, and over the first tick it covers
/// a little under one block. **Unit: milliseconds of wall clock; source: this
/// project's own 50 ms tick** (`kGrowthTick` is the same number and says so),
/// so 50 is exactly one tick and the comment above is now true of the number
/// below it. It was 60 - 1.2 ticks, from nowhere written down - which read as
/// 16.7 blocks per second against the 20 the comment describes.
constexpr std::chrono::milliseconds kFallDelay{50};

/// How long a leaf gets between something changing beside it and being asked
/// whether it still has wood in reach.
///
/// **Measured in wall-clock milliseconds, like the fall delay beside it and
/// unlike every growth constant in `farming`, which are per random tick.** It
/// is a spread, not a rule: the reference's canopy comes apart over a couple of
/// seconds rather than in one frame, and a delay here also collapses the burst
/// of six checks per removed log into work the frame budget can meter. Four
/// game ticks at this project's 20 Hz.
constexpr std::chrono::milliseconds kLeafCheckDelay{200};

/// One game tick. Every growth rate is quoted per tick, so this is the number
/// that must not drift - raising it would silently speed every farm up.
constexpr std::chrono::milliseconds kGrowthTick{50};

/// How many missed growth ticks are made up in one frame before the rest are
/// written off. Twenty is a second of them, which is longer than any frame the
/// budget allows and shorter than any stall worth simulating through.
constexpr int kMaxGrowthCatchUp = 20;

/// How many columns are offered to the weather per loaded chunk per tick.
///
/// **Derived exactly as `farming::kRandomTicksPerChunkPerTick` is**, from the
/// footprint rather than chosen for feel: the reference samples one column per
/// chunk per tick for freezing and settling snow, its chunk is 16 by 16, ours is
/// `Chunk::kSize` by `Chunk::kSize`, and 32 by 32 covers four of them. Sampling
/// a fixed count per chunk rather than a global budget is what makes the rate
/// independent of the render distance, which is the same reason the random-tick
/// sampler is shaped this way - a global budget would freeze ponds more slowly
/// the further you could see.
///
/// **Unit: columns per loaded chunk per 50 ms tick.** What it buys, in the only
/// terms worth checking: a chunk holds `32 * 32 = 1024` columns, so a given
/// column comes up every `1024 / (4 * 20) = 12.8` seconds, and a drift reaching
/// the eight layers Snowy Plains allows takes about a hundred seconds of
/// continuous snowfall. Under two minutes for a full blanket, which is the
/// right order: a storm is minutes long, and snow that arrived in one tick
/// would read as a texture swap rather than as weather.
constexpr int kWeatherColumnsPerChunkPerTick = 4;

// **The derivation, asserted rather than trusted.** The count above is
// `(kSize / 16)^2` and nothing else; if the chunk is ever resized this fails
// instead of quietly running at the wrong rate, which is exactly what happened
// to the random-tick constant's sibling before it was written down.
static_assert(kWeatherColumnsPerChunkPerTick ==
                  (Chunk::kSize / 16) * (Chunk::kSize / 16),
              "one column per 16-by-16 of footprint per tick; the number is the chunk size, not a "
              "taste");

/// The block-light level at which ice and settled snow give way.
///
/// minecraft.wiki [[Ice]]: ice melts "if the light level from blocks adjacent to
/// it is higher than 11", so 11 is the highest level ice survives. **Sky light
/// is deliberately not counted** - that is why a torch melts a frozen lake and
/// noon does not.
///
/// **One constant because freezing and melting must share it exactly.** They
/// are the two halves of one threshold, and two copies that drift apart give
/// either a dead band, where a cell is too bright to freeze and too dim to melt
/// and simply stays as it is, or an overlap, where the same cell freezes and
/// melts on alternate ticks and flickers. This was a bare `11` sitting in
/// `growOne` when the freezing half did not exist yet; naming it is what stops
/// the second half being written with a different number.
constexpr int kIceLightThreshold = 11;

/// A stable per-column number, for anything that must look random and stay put.
///
/// **Stable is the requirement here, not random.** A drift's target depth is
/// drawn once and then re-read on every tick for as long as it snows; taken
/// from the world's own PRNG instead, the target would change underneath the
/// drift and it would climb and shrink for ever. Seeded from the world seed, so
/// two worlds differ and one world reloads to the same snowfields it had.
///
/// Nothing about the quality of the mix matters beyond one property:
/// neighbouring columns must get unrelated answers, which is what makes the
/// edge of a snowfield ragged rather than terraced.
std::uint32_t columnNoise(std::uint32_t seed, int worldX, int worldZ) {
    std::uint32_t h = seed ^ 0x9E3779B9u;
    h ^= static_cast<std::uint32_t>(worldX) * 0x85EBCA6Bu;
    h ^= static_cast<std::uint32_t>(worldZ) * 0xC2B2AE35u;
    h ^= h >> 15;
    h *= 0x2545F491u;
    h ^= h >> 13;
    return h;
}

/// How far a run of chests is followed before giving up. A row this long is not
/// something anyone builds by accident, and the cap keeps the walk bounded.
///
/// **Read by two things now** - the pairing walk and the invalidation that has
/// to reach as far as the pairing walk can. A second copy of this number is a
/// run of chests whose far end never re-textures.
constexpr int kMaxChestRun = 64;

/// Everything a mesh job reads, allocated once and shared with the job.
///
/// Captured by `shared_ptr` rather than by value: a lambda holding a whole chunk
/// gets copied into the `std::function` that carries it, so the volume would be
/// copied twice per job. A pointer-sized capture also fits inside
/// `std::function` without a second heap allocation.
struct MeshJobInput {
    ChunkCoord coord;
    std::uint64_t revision = 0;
    glm::vec3 origin{0.0f};
    /// Decided on the main thread and carried with the volume, so the job never
    /// has to ask where the player is.
    MeshDetail detail = MeshDetail::Full;
    ChunkVolume volume;
};

/// How far past `m_detailRadius` a chunk keeps decoration it already has.
///
/// One chunk. The boundary is measured between chunk coordinates, so without
/// this a player pacing across a single chunk edge would flip a whole ring in
/// and out on alternate steps, re-meshing it every time.
constexpr int kDetailHysteresisChunks = 1;

/// Every chunk whose *geometry* a change at one cell can alter, this one
/// included. Between one and eight of them.
///
/// **Twenty-six-connected, not six.** The mesher's ambient occlusion and smooth
/// light read the diagonal cell at every face corner - `diagonal = front +
/// stepA + stepB`, one step along each of the three axes - so a cell in a chunk
/// *corner* is a sample for the chunk diagonally across from it. Stepping one
/// axis at a time can never name that chunk: `lx == 0` gives the -X neighbour
/// and `lz == 0` the -Z one, and neither is `{-1, 0, -1}`. What that left was a
/// corner of baked shading on the diagonal neighbour that no edit ever
/// corrected.
///
/// A template rather than a `std::function` because light propagation calls this
/// for tens of thousands of cells a frame.
template <typename Fn>
void forEachChunkTouchedBy(int x, int y, int z, Fn&& fn) {
    constexpr int last = Chunk::kSize - 1;

    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};

    // 0 for a cell that is not against this axis's edge, otherwise the one step
    // that names the chunk sharing it.
    const auto edge = [](int local) { return local == 0 ? -1 : (local == last ? 1 : 0); };
    const int ex = edge(floorMod(x, Chunk::kSize));
    const int ey = edge(floorMod(y, Chunk::kSize));
    const int ez = edge(floorMod(z, Chunk::kSize));

    const std::array<int, 2> xs{0, ex};
    const std::array<int, 2> ys{0, ey};
    const std::array<int, 2> zs{0, ez};
    const int xCount = ex != 0 ? 2 : 1;
    const int yCount = ey != 0 ? 2 : 1;
    const int zCount = ez != 0 ? 2 : 1;

    for (int i = 0; i < xCount; ++i) {
        for (int j = 0; j < yCount; ++j) {
            for (int k = 0; k < zCount; ++k) {
                fn(ChunkCoord{coord.x + xs[static_cast<std::size_t>(i)],
                              coord.y + ys[static_cast<std::size_t>(j)],
                              coord.z + zs[static_cast<std::size_t>(k)]});
            }
        }
    }
}

} // namespace

World::World(std::uint32_t seed, std::filesystem::path saveRoot, engine::JobSystem& jobs, int visibleRadiusChunks)
    : m_seed(seed), m_visibleRadius(std::max(1, visibleRadiusChunks)), m_loadRadius(m_visibleRadius + 1),
      m_unloadRadius(m_loadRadius + 2), m_detailRadius(m_visibleRadius),
      m_store(std::make_shared<WorldStore>(std::move(saveRoot), seed)), m_jobs(jobs),
      m_results(std::make_shared<JobResults>()) {}

/// Jobs hold `shared_ptr`s to the results buffer and the store, so anything
/// still running here keeps what it touches alive and simply writes into a
/// buffer nobody will read. Nothing needs to be waited for.
World::~World() = default;

int World::skyLightAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    // Unloaded reads as full sky, so the frontier does not darken as it streams.
    if (chunk == nullptr) {
        return kMaxLight;
    }
    return chunk->skyLightAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

int World::blockLightAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return 0;
    }
    return chunk->blockLightAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

std::optional<int> World::skyLightIfLoaded(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return std::nullopt;
    }
    return chunk->skyLightAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

std::optional<int> World::blockLightIfLoaded(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return std::nullopt;
    }
    return chunk->blockLightAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

void World::setSkyLightAt(int x, int y, int z, int level) {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    it->second.blocks.setSkyLight(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize),
                                  level);
    lightChangedAt(x, y, z);
}

void World::setBlockLightAt(int x, int y, int z, int level) {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    it->second.blocks.setBlockLight(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize),
                                    level);
    lightChangedAt(x, y, z);
}

/// Records that a chunk's geometry needs rebuilding because its light moved.
///
/// Only collects coordinates; the actual invalidation happens once per frame in
/// `flushLightDirty`. Propagation touches tens of thousands of cells, and
/// invalidating per cell would mean a linear scan of the pending queue each time.
void World::lightChangedAt(int x, int y, int z) {
    // A lit cell on a chunk face shades the neighbour's geometry too - and on a
    // chunk *corner* it shades the diagonal neighbour's, which is what
    // `forEachChunkTouchedBy` is for and what six separate edge tests missed.
    forEachChunkTouchedBy(x, y, z, [this](const ChunkCoord& coord) { m_lightDirty.insert(coord); });
}

void World::flushLightDirty() {
    for (const ChunkCoord& coord : m_lightDirty) {
        invalidateMesh(coord);
    }
    m_lightDirty.clear();
}

bool World::columnLoaded(int chunkX, int chunkZ) const {
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        if (!hasChunk({chunkX, cy, chunkZ})) {
            return false;
        }
    }
    return true;
}

bool World::columnsResidentAround(int x, int z, int reach) const {
    // Walked in chunk coordinates rather than block ones, so the cost is the
    // number of *columns* the box touches - one, two or four at any reach below
    // `Chunk::kSize` - and not the eighty-one cells a per-block sweep would ask
    // about at reach four.
    const int firstX = floorDiv(x - reach, Chunk::kSize);
    const int lastX = floorDiv(x + reach, Chunk::kSize);
    const int firstZ = floorDiv(z - reach, Chunk::kSize);
    const int lastZ = floorDiv(z + reach, Chunk::kSize);
    for (int cz = firstZ; cz <= lastZ; ++cz) {
        for (int cx = firstX; cx <= lastX; ++cx) {
            if (!columnLoaded(cx, cz)) {
                return false;
            }
        }
    }
    return true;
}

bool World::columnResident(int x, int z) const {
    // Reach zero **is** this question, so it delegates rather than restating the
    // floor division. This one is public and has callers outside this file; the
    // wider one is private and is the walk both go through.
    return columnsResidentAround(x, z, 0);
}

bool World::columnReadyOrDeferred(const glm::ivec3& p, std::deque<PendingFluid>& queue,
                                  std::chrono::milliseconds retryDelay, int reach) {
    if (columnsResidentAround(p.x, p.z, reach)) {
        return true;
    }

    // Still inside the radius the world loads to, so the missing chunks are
    // already queued and this is worth another look rather than a silent drop:
    // a column half-streamed in is a state that lasts a frame or two, and the
    // update that was scheduled for it is the only thing that will ever look at
    // that cell again. Once the column is out of load range nothing is going to
    // bring it back, and the entry dies here - which is what stops the frontier
    // accumulating retries as the player travels.
    //
    // **Measured on the cell's own column even when `reach` is wider**, and
    // that is the right asymmetry rather than an oversight: the question here is
    // "is anything ever coming back for this entry", and the answer belongs to
    // the column the entry is *in*. A neighbour that is out of load range while
    // this column is inside it is a frontier that will move again; giving up on
    // the entry for that would strand a leaf on the very edge of the loaded
    // world for the rest of the session.
    const ChunkCoord column{floorDiv(p.x, Chunk::kSize), 0, floorDiv(p.z, Chunk::kSize)};
    if (!m_hasCentre || chebyshevDistance(column, m_centre) <= m_loadRadius) {
        queue.push_back({p, std::chrono::steady_clock::now() + retryDelay, 0});
    }
    return false;
}

void World::seedColumnLight(int chunkX, int chunkZ) {
    const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(chunkX)) << 32) |
                              static_cast<std::uint32_t>(chunkZ);
    if (m_litColumns.count(key) != 0) {
        return;
    }

    constexpr int worldTop = kWorldHeightChunks * Chunk::kSize - 1;
    constexpr int size = Chunk::kSize;

    // Writes go straight into the chunks rather than through `setSkyLightAt`:
    // this touches ~98,000 cells per column, and marking dirty per cell would
    // dwarf the actual work. The whole column is marked once at the end.
    std::array<ChunkSlot*, kWorldHeightChunks> column{};
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        const auto it = m_chunks.find({chunkX, cy, chunkZ});
        if (it == m_chunks.end()) {
            return;
        }
        column[static_cast<std::size_t>(cy)] = &it->second;
    }

    // **Marked only once the work is certain to happen, and that ordering is
    // the whole point.** The mark used to be taken on the way in, so the
    // `return` above left a column recorded as lit that had never been seeded -
    // and every later call then stopped at the first line, so it stayed pitch
    // black for as long as it was resident. Today the only caller checks
    // `columnLoaded` first and the `return` cannot fire, which is exactly what
    // makes it worth writing down: the guard is in the caller and the mark is
    // in the callee, so a second caller - or that guard moving - turns a column
    // black with nothing to point at. Same shape as a save that clears the
    // dirty flag before knowing the write landed.
    m_litColumns.insert(key);

    // Lowest y still reached by open sky, per column. One cell of margin so the
    // seeding step below can compare against neighbouring chunk-columns.
    constexpr int span = size + 2;
    std::array<int, span * span> skyFloor{};

    for (int mz = 0; mz < span; ++mz) {
        for (int mx = 0; mx < span; ++mx) {
            const int lx = mx - 1;
            const int lz = mz - 1;
            const bool inside = lx >= 0 && lx < size && lz >= 0 && lz < size;
            const int worldX = chunkX * size + lx;
            const int worldZ = chunkZ * size + lz;

            int floorY = 0;
            for (int y = worldTop; y >= 0; --y) {
                const BlockId block =
                    inside ? column[static_cast<std::size_t>(y / size)]->blocks.at(lx, y % size, lz)
                           : blockAt(worldX, y, worldZ);
                if (!isSkyTransparent(block)) {
                    floorY = y + 1;
                    break;
                }
            }
            skyFloor[static_cast<std::size_t>(mz) * span + mx] = floorY;
        }
    }

    for (int lz = 0; lz < size; ++lz) {
        for (int lx = 0; lx < size; ++lx) {
            const int worldX = chunkX * size + lx;
            const int worldZ = chunkZ * size + lz;
            const int floorY = skyFloor[static_cast<std::size_t>(lz + 1) * span + (lx + 1)];

            for (int y = worldTop; y >= 0; --y) {
                ChunkSlot& slot = *column[static_cast<std::size_t>(y / size)];
                const int ly = y % size;
                const BlockId block = slot.blocks.at(lx, ly, lz);

                // Sky falls straight down at full strength until something stops
                // it. Vertical travel costs nothing, which is what makes open
                // ground uniformly bright; only sideways spread dims.
                slot.blocks.setSkyLight(lx, ly, lz, y >= floorY ? kMaxLight : 0);

                if (const int emission = blockLightEmission(block); emission > 0) {
                    slot.blocks.setBlockLight(lx, ly, lz, emission);
                    m_blockAdditions.push_back({worldX, y, worldZ});
                }
            }

            // Only cells that can actually spread anywhere are worth queueing.
            // A lit cell whose neighbours are all lit has nothing to give, and
            // seeding every one of them buried the queue under tens of millions
            // of entries that did nothing.
            const int highestDarkNeighbour =
                std::max({skyFloor[static_cast<std::size_t>(lz + 1) * span + lx],
                          skyFloor[static_cast<std::size_t>(lz + 1) * span + lx + 2],
                          skyFloor[static_cast<std::size_t>(lz) * span + lx + 1],
                          skyFloor[static_cast<std::size_t>(lz + 2) * span + lx + 1]});

            // The lowest full-sky cell always gets queued: whatever stopped the
            // free fall may still be something light seeps into, such as a
            // canopy, and nothing else would ever push light down into it.
            if (floorY <= worldTop) {
                m_skyAdditions.push_back({worldX, floorY, worldZ});
            }

            for (int y = floorY + 1; y < highestDarkNeighbour && y <= worldTop; ++y) {
                m_skyAdditions.push_back({worldX, y, worldZ});
            }
        }
    }

    // **And now the same rule applied to the ring, for the columns that were
    // seeded before this one existed.**
    //
    // The margin above is read through `blockAt`, and `blockAt` answers `Air`
    // for a chunk that is not resident - so a *neighbour* seeded while this
    // column was still streaming in scored this side as open sky all the way
    // down, took its `highestDarkNeighbour` from three real values and one
    // zero, and queued none of its border cells. Zero is the minimum, so an
    // absent neighbour can only ever make that maximum too small; it can never
    // make it too large. And nothing asks again - `m_litColumns` makes seeding
    // once-only, and a chunk arriving marks its neighbours for a *remesh*, not
    // a relight.
    //
    // What that left on screen is a strip against a cliff face at a column
    // boundary lit only by what crept up from the lowest full-sky cell - one
    // level dimmer per block, black about fifteen up - beside a column that is
    // at full 15 the whole way. It needs a height difference across the
    // boundary and it needs the taller column to arrive second, so it is
    // roughly one boundary in two at the streaming frontier, which is every
    // boundary in the world once the player has walked far enough.
    //
    // The rule is the interior one unchanged: a cell is worth queueing where it
    // has full sky and the cell across from it does not. Idempotent where the
    // neighbour got it right, because `propagateLight` drops an addition whose
    // target is already at least as bright, and free where the two columns are
    // level, because then `from == to` and nothing is pushed.
    const auto floorAt = [&](int lx, int lz) {
        return skyFloor[static_cast<std::size_t>(lz + 1) * span + (lx + 1)];
    };
    const auto queueRingRun = [&](int lx, int lz, int interiorLx, int interiorLz) {
        const int worldX = chunkX * size + lx;
        const int worldZ = chunkZ * size + lz;
        const int top = std::min(floorAt(interiorLx, interiorLz) - 1, worldTop);
        for (int y = floorAt(lx, lz); y <= top; ++y) {
            m_skyAdditions.push_back({worldX, y, worldZ});
        }
    };

    // Only the four sides, because light does not travel diagonally - a corner
    // is reached through one of these. Residency is asked per column rather
    // than per cell: an absent one reads as `floorY == 0` and would queue the
    // whole height of every border cell, thousands of entries the propagator
    // would then drop one at a time. A column that is loaded but not yet seeded
    // is all zeroes and drops just as cheaply, and its own seeding will get the
    // answer right now that this one is here.
    const bool westResident = columnLoaded(chunkX - 1, chunkZ);
    const bool eastResident = columnLoaded(chunkX + 1, chunkZ);
    const bool northResident = columnLoaded(chunkX, chunkZ - 1);
    const bool southResident = columnLoaded(chunkX, chunkZ + 1);
    for (int i = 0; i < size; ++i) {
        if (westResident) {
            queueRingRun(-1, i, 0, i);
        }
        if (eastResident) {
            queueRingRun(size, i, size - 1, i);
        }
        if (northResident) {
            queueRingRun(i, -1, i, 0);
        }
        if (southResident) {
            queueRingRun(i, size, i, size - 1);
        }
    }

    // The column and everything touching it, since border light shades
    // neighbouring geometry - **the four diagonal columns included**. The
    // mesher's corner shading samples one step along all three axes at once, so
    // a cell in this column's corner is a sample for the column diagonally
    // across from it, which no single-axis step names. Same rule as
    // `forEachChunkTouchedBy`, applied to a whole column at once.
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                m_lightDirty.insert({chunkX + dx, cy, chunkZ + dz});
            }
        }
    }
}

void World::unpropagate(std::deque<LightRemoval>& removals, std::deque<glm::ivec3>& additions, bool sky,
                        const BudgetCheck& budgetSpent) {
    while (!removals.empty() && !budgetSpent()) {
        const LightRemoval entry = removals.front();
        removals.pop_front();

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = entry.position + step;
            if (n.y < 0 || n.y >= kWorldHeightChunks * Chunk::kSize) {
                continue;
            }

            // **Not `skyLightAt`.** It answers full sky for a chunk that is not
            // here, which for a teardown is the worst possible lie: 15 is never
            // less than `previousLevel`, so an absent neighbour was pushed into
            // `additions` as a phantom source - and worse, an absent chunk
            // *below* satisfied `litFromAbove` exactly, so the walk stepped into
            // nothing, pushed `{n, kMaxLight}` back into `removals`, and from
            // there blanked whichever of that cell's neighbours were real. A
            // cell no chunk holds has no light to take away.
            const std::optional<int> reading =
                sky ? skyLightIfLoaded(n.x, n.y, n.z) : blockLightIfLoaded(n.x, n.y, n.z);
            if (!reading.has_value()) {
                continue;
            }
            const int level = *reading;
            if (level == 0) {
                continue;
            }

            // Sky light falls straight down at full strength, so the cell under
            // a removed full-strength one was lit by it at the *same* level -
            // which "dimmer than its source" can never detect. Without this a
            // roof left the whole column under it lit by a sky it can no longer
            // see.
            const bool litFromAbove = sky && step.y == -1 &&
                                      entry.previousLevel == kMaxLight && level == kMaxLight;

            if (level < entry.previousLevel || litFromAbove) {
                // This neighbour was lit by what we just removed.
                if (sky) {
                    setSkyLightAt(n.x, n.y, n.z, 0);
                } else {
                    setBlockLightAt(n.x, n.y, n.z, 0);
                }
                removals.push_back({n, level});
            } else {
                // Brighter than the source, so it has its own supply and will
                // fill the hole back in.
                additions.push_back(n);
            }
        }
    }
}

void World::propagateLight(const BudgetCheck& budgetSpent) {
    unpropagate(m_skyRemovals, m_skyAdditions, true, budgetSpent);
    unpropagate(m_blockRemovals, m_blockAdditions, false, budgetSpent);

    // **Nothing is filled back in while there is still light to tear down.**
    // The two passes are an order, not a preference: an addition run against a
    // half-torn-down region refills cells the removal is about to blank again,
    // so the work is done twice and the region visibly flickers. Both queues are
    // in `isSettled`, so a loading screen cannot finish on a half-done teardown
    // either.
    if (!m_skyRemovals.empty() || !m_blockRemovals.empty()) {
        return;
    }

    const int worldHeight = kWorldHeightChunks * Chunk::kSize;

    // Block light goes first even though sky light is what makes newly streamed
    // terrain look right. Block light is rare and its queue is short, so it
    // costs sky light almost nothing - but behind sky light it can be starved
    // outright, because streaming refills the sky queue every frame and the two
    // share one budget. That is a placed torch that never lights anything.
    while (!m_blockAdditions.empty() && !budgetSpent()) {
        const glm::ivec3 position = m_blockAdditions.front();
        m_blockAdditions.pop_front();

        // A cell no chunk holds is not a source. `setBlock` pushes all six
        // neighbours of an edit into these queues without asking whether they
        // are resident, and at the streaming frontier some of them are not.
        const std::optional<int> here = blockLightIfLoaded(position.x, position.y, position.z);
        if (!here.has_value() || *here <= 0) {
            continue;
        }
        const int level = *here;

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = position + step;
            if (n.y < 0 || n.y >= worldHeight) {
                continue;
            }
            // Answers "is it here" and "what is it" in the one lookup `blockAt`
            // was already costing, so everything below can read the light
            // directly knowing the chunk exists.
            const std::optional<BlockId> ahead = blockIfLoaded(n.x, n.y, n.z);
            if (!ahead.has_value() || !admitsLight(*ahead)) {
                continue;
            }
            if (blockLightAt(n.x, n.y, n.z) >= level - 1) {
                continue;
            }

            setBlockLightAt(n.x, n.y, n.z, level - 1);
            m_blockAdditions.push_back(n);
        }
    }

    while (!m_skyAdditions.empty() && !budgetSpent()) {
        const glm::ivec3 position = m_skyAdditions.front();
        m_skyAdditions.pop_front();

        // **The one that mattered.** `skyLightAt` answers `kMaxLight` for an
        // absent chunk, so a queued cell out at the frontier read as a
        // full-strength sky source and seeded 14 into whichever of its
        // neighbours *were* resident - a bright patch, in shadow, put there by
        // a chunk that does not exist.
        const std::optional<int> here = skyLightIfLoaded(position.x, position.y, position.z);
        if (!here.has_value() || *here <= 0) {
            continue;
        }
        const int level = *here;

        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = position + step;
            if (n.y < 0 || n.y >= worldHeight) {
                continue;
            }
            const std::optional<BlockId> ahead = blockIfLoaded(n.x, n.y, n.z);
            if (!ahead.has_value() || !admitsLight(*ahead)) {
                continue;
            }

            // Straight down keeps full strength, but only through cells that
            // are sky-transparent. A canopy breaks the free fall, and from there
            // light dims one level per block downward like any other direction.
            //
            // **This line was unreachable for the 368 ids that are
            // sky-transparent and light-opaque**, because the gate above asked
            // `isLightTransparent` alone and dropped every one of them before
            // the question was put; see `admitsLight`. A fence line cast the
            // hard shadow along itself that the comment there says it exists to
            // stop, and only until the chunk was reloaded.
            const int reaching =
                (step.y == -1 && level == kMaxLight && isSkyTransparent(*ahead)) ? kMaxLight : level - 1;
            if (reaching <= 0 || skyLightAt(n.x, n.y, n.z) >= reaching) {
                continue;
            }

            setSkyLightAt(n.x, n.y, n.z, reaching);
            m_skyAdditions.push_back(n);
        }
    }
}

void World::scheduleFluidUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_fluidUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kFluidSpreadDelay});
}

void World::scheduleLavaUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_lavaUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kLavaSpreadDelay});
}

void World::primeTnt(const glm::ivec3& at) {
    primeTnt(at, kTntFuse);
}

std::chrono::milliseconds World::blastFuse() {
    // **Runtime simulation on the main thread, not generation.** A PRNG here
    // breaks nothing: rule 1 binds `(seed, chunkCoord)` in a worker, and this
    // runs in the same single-owner update that already rolls for fire spread
    // and for lava's obsidian. It shares `m_fireRandom` on purpose - lighting a
    // charge is ignition, the same family - and that stream is seeded from a
    // fixed constant rather than the clock, so a replay of the same inputs
    // still gives the same world.
    m_fireRandom ^= m_fireRandom << 13;
    m_fireRandom ^= m_fireRandom >> 17;
    m_fireRandom ^= m_fireRandom << 5;
    const auto span = static_cast<std::uint32_t>((kTntChainFuseMax - kTntChainFuseMin).count());
    return kTntChainFuseMin + std::chrono::milliseconds{static_cast<std::int64_t>(m_fireRandom % (span + 1u))};
}

void World::primeTnt(const glm::ivec3& at, std::chrono::milliseconds fuse) {
    const auto now = std::chrono::steady_clock::now();
    // The fuse is spent as a run of blinks rather than one long wait, so the
    // charge visibly counts down; the entry that finds no blinks left is the
    // one that detonates. **The cadence is fixed and the count is derived**, so
    // a half-second chain fuse still flashes at the reference's rate instead of
    // flashing four times faster, and a fuse shorter than one blink simply
    // never flashes. `tntSchedule` owns the split and is asserted exact.
    const TntSchedule schedule = tntSchedule(fuse);

    for (PendingFluid& lit : m_tntFuses) {
        if (lit.position != at) {
            continue;
        }
        // **Shorten, never restart, and never add a second entry.** A charge in
        // a dense stack is re-primed by every blast that reaches it, so a fuse
        // that restarted would let a big enough pile hold itself lit forever;
        // and a duplicate entry would blink it twice as fast and then arrive at
        // an already-detonated cell. Compared on the moment it goes off, not on
        // the next blink - those are different numbers once fuses differ.
        if (now + fuse < lit.due + kTntBlink * lit.blinks) {
            lit.due = now + schedule.first;
            lit.blinks = schedule.blinks;
        }
        return;
    }
    m_tntFuses.push_back({at, now + schedule.first, schedule.blinks});
}

void World::scheduleFireUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_fireUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kFireTickDelay});
}

bool World::fireCanSurvive(int x, int y, int z) const {
    // **This is the *placement* rule, not the extinguishing rule**, and the
    // difference is why it reads as a disjunction where minecraft.wiki [[Fire]]
    // "Extinguishing" reads as a conjunction. They are the same statement seen
    // from opposite sides: fire *lives* where the floor is solid **or** eternal
    // **or** something beside it can burn, which is exactly "fire goes out when
    // there is nothing flammable adjacent **and** the floor is not solid".
    // De Morgan, not a contradiction - and worth writing down, because a
    // careful reader comparing this line against that wiki section will
    // otherwise "fix" it and break flint and steel.
    //
    // **`Main.cpp` calls this to decide whether a lighter may light a cell**,
    // which is the reference's `canSurvive`, so the semantics here are a
    // placement contract with another file and not free to move. The one clause
    // the wiki's tick rule adds - that a fire on a non-solid floor survives
    // while its age is 3 or less - is not expressible: `BlockId::Fire` has no
    // age. Filed as a `Block.hpp` finding.
    const BlockId under = blockAt(x, y - 1, z);
    if (feedsEternalFire(under) || game::isSolid(under)) {
        return true;
    }
    for (const glm::ivec3& step : kLightSteps) {
        if (isFlammable(blockAt(x + step.x, y + step.y, z + step.z))) {
            return true;
        }
    }
    return false;
}

bool World::rainFallsOn(int x, int y, int z) const {
    // **The storm bit is the gate and stays the gate**, so this can only ever
    // narrow what it used to admit and never widen it - the intensity threshold
    // `Main.cpp` folds into that bit is left where its owner put it. It is also
    // the cheap test, which keeps the biome sample off every fire tick and every
    // farmland tick in clear weather.
    if (!m_precipitating) {
        return false;
    }
    // Open to the sky, asked exactly as both callers asked it before this
    // function existed.
    if (skyLightAt(x, y, z) < kMaxLight) {
        return false;
    }
    // **And then the column's own answer, which is the half that was missing.**
    // `m_precipitating` is sampled in the camera's column, so a fire in a desert
    // was being put out by a shower two hundred blocks away over a plains, and a
    // desert field hydrated itself from it. `weatherTickColumn` has been asking
    // this same function for the settling half all along; one call site is
    // `CLAUDE.md` bug shape #1 and two is the fix.
    //
    // **Rain only.** minecraft.wiki [[Fire]] extinguishes on rain and
    // [[Farmland]] hydrates on rain; a snowy biome gets snow, and neither rule
    // applies there. `precipitationFor` answers `None` for a dry biome, which is
    // what keeps a desert dry however hard it is raining where the player is.
    const BiomeSample biome = sampleBiome(m_seed, x, z);
    return weather::precipitationFor(biome.dominant, y, m_seed, x, z) ==
           weather::Precipitation::Rain;
}

bool World::farmlandIsHydrated(int x, int y, int z) const {
    // **Rain hydrates too, and ours ignored it.** minecraft.wiki [[Farmland]]:
    // "Farmland becomes hydrated if ... rain falls on the block". So a field
    // out in the open never dries during a shower, which is the behaviour a
    // player notices - they stop hauling buckets when it rains.
    //
    // Sky exposure and the biome are both `rainFallsOn`'s business, and it is
    // the same call `updateFire` makes, so the two cannot disagree about which
    // cells the weather reaches.
    if (rainFallsOn(x, y + 1, z)) {
        return true;
    }
    // The reference's rule is a **nine-by-nine box at this level or one above**
    // - not a radius and not a line of sight, so nothing in between matters and
    // flowing water counts the same as a source.
    constexpr int reach = farming::kHydrationReach;
    for (int dy = 0; dy <= 1; ++dy) {
        for (int dz = -reach; dz <= reach; ++dz) {
            for (int dx = -reach; dx <= reach; ++dx) {
                if (isWater(blockAt(x + dx, y + dy, z + dz))) {
                    return true;
                }
            }
        }
    }
    return false;
}

void World::growOne(const glm::ivec3& at) {
    // The sampler's own xorshift, advanced in place. Kept apart from the fire's
    // so a field growing cannot perturb what a burning forest does next.
    const auto nextRandom = [this] {
        m_growthRandom ^= m_growthRandom << 13;
        m_growthRandom ^= m_growthRandom >> 17;
        m_growthRandom ^= m_growthRandom << 5;
        return m_growthRandom;
    };
    // Whether two plants are the reference's "same crop": the same **block**,
    // so age never enters into it - and a stem that has already fruited is a
    // different block here exactly as it is there, so it does not count.
    const auto samePlantAs = [](BlockId plant, BlockId other) {
        if (isCropBlock(plant)) {
            return isCropBlock(other) && cropFamily(other) == cropFamily(plant);
        }
        return isGrowingStem(plant) && isGrowingStem(other) &&
               stemGrowsMelon(other) == stemGrowsMelon(plant);
    };
    // The reference's crowding rule; `farming::cropIsCrowded` owns the decision
    // and this only reads the eight cells around the plant that feed it.
    const auto cropIsCrowdedAt = [&](const glm::ivec3& cell, BlockId plant) {
        const auto same = [&](int dx, int dz) {
            return samePlantAs(plant, blockAt(cell.x + dx, cell.y, cell.z + dz));
        };
        return farming::cropIsCrowded(same(0, -1) || same(0, 1), same(-1, 0) || same(1, 0),
                                      same(-1, -1) || same(1, -1) || same(-1, 1) || same(1, 1));
    };
    // Whether a stalk of cane still has under it what the reference demands.
    // minecraft.wiki [[Sugar Cane]]: it stands "on grass blocks, dirt, coarse
    // dirt, rooted dirt, podzol, mycelium, sand, red sand, suspicious sand,
    // moss blocks, pale moss blocks, mud, or muddy mangrove roots that are
    // directly adjacent to water, a waterlogged block, or frosted ice", or on
    // another sugar cane block. **The water is beside the soil, not beside the
    // cane** - that one word is the difference between cane that survives on a
    // riverbank and cane that survives only with its own feet wet, and it is
    // the same cell the generator tests when it plants a stand.
    const auto caneIsRooted = [this](const glm::ivec3& cell) {
        const glm::ivec3 soil{cell.x, cell.y - 1, cell.z};
        const BlockId under = blockAt(soil.x, soil.y, soil.z);
        if (under == BlockId::SugarCane) {
            return true;
        }
        switch (under) {
        case BlockId::Grass:
        case BlockId::Dirt:
        case BlockId::CoarseDirt:
        case BlockId::RootedDirt:
        case BlockId::Podzol:
        case BlockId::Mycelium:
        case BlockId::Sand:
        case BlockId::SuspiciousSand:
        case BlockId::MossBlock:
        case BlockId::Mud:
        case BlockId::MuddyMangroveRoots:
            break;
        default:
            return false;
        }
        for (const glm::ivec3& step : kFlowSteps) {
            const glm::ivec3 n = soil + step;
            if (isWater(blockAt(n.x, n.y, n.z)) || waterloggedAt(n.x, n.y, n.z)) {
                return true;
            }
        }
        return false;
    };
    const BlockId here = blockAt(at.x, at.y, at.z);

    // ---- Tilled ground. ----
    if (isFarmland(here)) {
        const bool wet = farmlandIsHydrated(at.x, at.y, at.z);
        const BlockId above = blockAt(at.x, at.y + 1, at.z);
        const bool planted = isCropBlock(above) || isStemBlock(above);
        const int moisture = farmlandMoisture(here);
        if (wet) {
            // minecraft.wiki [[Farmland]]: "If ... there is water within range,
            // the moisture level is set to 7", from whatever it was. Wetting is
            // instant in the reference too - it is only *drying* that is
            // gradual, and that asymmetry is the whole rule.
            if (moisture != kFarmlandFullMoisture) {
                setBlock(at.x, at.y, at.z, farmlandAtMoisture(kFarmlandFullMoisture));
            }
            return;
        }
        // **"Not wet" and "could not tell" are different answers, and only one
        // of them may dry a field.** `farmlandIsHydrated` sweeps a nine-by-nine
        // box two deep through `blockAt`, and `blockAt` answers `Air` for a
        // chunk that is not resident - so a field whose pond is in the next
        // column along reads bone dry at the streaming frontier. Nothing above
        // is at risk: an absent chunk can only ever read as *less* water, so a
        // false `wet` is not reachable and setting full moisture stays safe.
        // It is everything below, which walks a field down seven levels and
        // then turns unsown ground back into dirt.
        //
        // Slower than the leaf case rather than different from it - one level
        // per random tick, not one tick - and the same `CLAUDE.md` bug shape #1
        // answer: the same walk `decayLeafIfOrphaned` makes, at this rule's own
        // reach. Refusing costs one random tick; the field is sampled again
        // long before a player notices.
        if (!columnsResidentAround(at.x, at.z, farming::kHydrationReach)) {
            return;
        }
        // **Drying takes seven random ticks, and ours did it in one.**
        // minecraft.wiki [[Farmland]]: with no water in range "the moisture
        // level is reduced by 1" per random tick, and the block reverts to dirt
        // only "if the moisture level is 0 and there are no crops on top".
        //
        // **This used to be unreachable, and the comment here said so at
        // length.** `Block.hpp` carried two ids, so the countdown was written at
        // the resolution two ids allow - moist, dry, gone - and the note
        // explained why that was the best available. The other six landed on
        // 2026-08-19 (`moisturized_amount` `[0-7]`, confirmed against Mojang's
        // own published state table), and that text is deleted rather than
        // amended: a comment arguing that the correct edit is impossible is the
        // most expensive kind to leave lying about, because a reader takes it
        // as a reason not to make it.
        //
        // The level is asked of `farmlandMoisture`, never of the id. Bedrock
        // draws the wet top on levels 1-7 and the dry one only on 0 - which
        // `Block.hpp` proves with `farmlandWetTopFrom(1)` - so an equality test
        // against `FarmlandMoist` reads six of the eight levels as bone dry
        // while the world draws them wet. That is exactly what the three tests
        // replaced here were doing.
        if (moisture > 0) {
            setBlock(at.x, at.y, at.z, farmlandAtMoisture(moisture - 1));
            return;
        }
        if (!planted) {
            // Dry, out of moisture and unsown goes back to dirt. **Sown ground
            // never does**, which is what makes dry farming legitimate rather
            // than doomed.
            setBlock(at.x, at.y, at.z, BlockId::Dirt);
        }
        return;
    }

    // ---- Pointed dripstone: filling a cauldron, and drying mud to clay. ----
    // minecraft.wiki [[Pointed Dripstone]]: "When the uppermost block of a
    // stalactite less than 11 blocks tall gets randomly ticked, it checks for a
    // waterlogged block [JE only] or water source two blocks above it and a
    // cauldron within 10 blocks under the tip with no non-air blocks in
    // between... there is a 45/256 (~17.6%) chance for it to drip water and
    // fill the cauldron by one level."
    //
    // **The uppermost block, not the tip, is what is ticked** - the tip is
    // where the water lands. Getting those two the wrong way round is the
    // whole rule inverted, so both ends are named separately below.
    //
    // **The lava half is deliberately not built, and the same refusal now
    // covers powder snow.** The reference also drips lava at 15/256 into an
    // empty cauldron, which is what makes lava renewable - but `Block.hpp`'s
    // cauldron is seven fill levels and no liquid, where the reference carries
    // `cauldron_liquid` beside `fill_level`. Filling one with lava here would
    // produce a cauldron that looks and behaves like water, which is worse than
    // not filling it. Filed.
    //
    // **Generalised on 2026-08-19, because the rule had not travelled.** The
    // reference also fills a cauldron with powder snow when it snows on one
    // under open sky, and that was proposed as the way to make powder snow
    // obtainable. It is refused here for exactly the reason above, and the
    // reason is worth stating in its general form so the next person finds it:
    // **until a cauldron can say *what* it holds, every non-water fill is a
    // cauldron that lies.** Powder snow is the worst case rather than another
    // instance - the block's entire purpose is being a hazard you fall into, so
    // a vessel that renders as water and contains a trap is not a rough edge,
    // it is the mechanic inverted.
    //
    // **Detecting the snowfall is not what blocks this.** `setWeatherFalling`
    // plus `precipitationFor` would answer "is it snowing on this column"
    // cheaply, and that was the expensive part when the idea was first raised.
    // The blocker is representational and lives in `Block.hpp`: the state needs
    // a liquid dimension first. **Powder snow instead generates naturally** -
    // `Biome.cpp` scatters it in Grove and Snowy Slopes, which is the
    // reference's own primary source and needs nothing from this file.
    if (here == BlockId::PointedDripstone) {
        // Uppermost means nothing of its own kind above it, and hanging means
        // what is above it is solid. A stalagmite fails the second test, which
        // is the same derivation `dripstoneAnchored` makes and the reason the
        // single id costs nothing here.
        const BlockId ceiling = blockAt(at.x, at.y + 1, at.z);
        if (ceiling == BlockId::PointedDripstone || !game::isSolid(ceiling)) {
            return;
        }
        // **What sits two above is the whole dispatch, and there are two
        // answers, not one.** minecraft.wiki [[Mud]]: "Placing above a block
        // with a pointed dripstone hanging underneath the mud gives it a
        // 45/256 (about 17.6%) chance per random tick to convert into clay."
        // That is the same cell the water source occupies and the same odds -
        // mud is a second source for the identical rule, not a separate
        // mechanic bolted beside it.
        //
        // **Worth stating because the obvious reading is the wrong one.** The
        // [[Pointed Dripstone]] page phrases it as "if mud is placed above a
        // block with a stalactite underneath", which invites putting the mud
        // *below* the tip where the drip lands - the shape every clay farm
        // tutorial has. The [[Mud]] page settles it in as many words, and the
        // two pages were read against each other rather than one trusted.
        const BlockId source = blockAt(at.x, at.y + 2, at.z);
        const bool dripsWater = isWaterSource(source);
        const bool dripsMud = (source == BlockId::Mud);
        if (!dripsWater && !dripsMud) {
            return;
        }

        int tip = at.y;
        int length = 1;
        while (length < farming::kDripstoneDripMaxHeight &&
               blockAt(at.x, tip - 1, at.z) == BlockId::PointedDripstone) {
            --tip;
            ++length;
        }
        // "less than 11 blocks tall" - the loop stops counting at the limit, so
        // reaching it is the disqualifying case rather than the passing one.
        if (length >= farming::kDripstoneDripMaxHeight) {
            return;
        }
        if ((nextRandom() % static_cast<std::uint32_t>(farming::kDripstoneDripDenominator)) >=
            static_cast<std::uint32_t>(farming::kDripstoneDripOdds)) {
            return;
        }

        // **The mud consumer, and it deliberately shares every guard above.**
        // CLAUDE.md bug shape #14 is a rule that exists in one of the two
        // places that need it - which is what this branch would have become if
        // it were written as its own `if (here == PointedDripstone)` further
        // down with its own copy of the ceiling test, the length test and the
        // odds. Same trigger, same 45/256, one place.
        //
        // The mud is consumed where it stands, two above; there is no drip to
        // follow down and no cauldron involved, which is why this returns
        // before the reach walk rather than after it.
        if (dripsMud) {
            setBlock(at.x, at.y + 2, at.z, BlockId::Clay);
            return;
        }

        for (int drop = 1; drop <= farming::kDripstoneCauldronReach; ++drop) {
            const BlockId under = blockAt(at.x, tip - drop, at.z);
            if (isCauldron(under)) {
                const int level = cauldronLevel(under);
                if (level < cauldronLevel(BlockId::CauldronExtraLast)) {
                    setBlock(at.x, tip - drop, at.z, cauldronAt(level + 1));
                }
                return;
            }
            // "with no non-air blocks in between" - the first thing that is
            // neither air nor the cauldron ends the search.
            if (under != BlockId::Air) {
                return;
            }
        }
        return;
    }

    // ---- Nether wart. ----
    // A flat one-in-ten, and the one plant the reference gates on nothing else
    // at all: no light, no biome, no dimension, no bone meal.
    if (isNetherWart(here)) {
        if (netherWartAge(here) >= 3 || (nextRandom() % 10) != 0) {
            return;
        }
        setBlock(at.x, at.y, at.z, static_cast<BlockId>(static_cast<int>(here) + 1));
        return;
    }

    // ---- Cocoa. ----
    // minecraft.wiki [[Cocoa Beans]]: "The cocoa block has a 20% chance to grow
    // a stage when receiving a random tick", through three stages, and it is
    // the only plant here that grows on the side of a log rather than off the
    // ground.
    if (isCocoa(here)) {
        if (cocoaAge(here) >= 2 || (nextRandom() % 5) != 0) {
            return;
        }
        setBlock(at.x, at.y, at.z, cocoaAt(cocoaFacing(here), cocoaAge(here) + 1));
        return;
    }

    // ---- Sugar cane and cactus. ----
    //
    // The same rule for both, and the reference states it as a count rather
    // than a chance: minecraft.wiki [[Sugar Cane]], "grow only to a height of
    // three blocks, adding a block of height when the top sugar cane block has
    // received 16 random ticks", and [[Cactus]] carries the identical wording.
    // Cane grows "regardless of light level, even in complete darkness".
    //
    // **We roll one in sixteen instead of counting to sixteen**, because
    // `BlockId::SugarCane` and `BlockId::Cactus` are one id each with no age to
    // count in - see `Block.hpp`. The mean is identical, 16 random ticks; only
    // the spread around it differs, and a farm is measured by its mean. Giving
    // either plant an age would be a 16-id run in the block table, which is a
    // decision for that file rather than a fix here.
    if (here == BlockId::SugarCane || here == BlockId::Cactus) {
        const bool cane = here == BlockId::SugarCane;
        if (cane && !caneIsRooted(at)) {
            // "When all adjacent water is removed, sugar cane uproots on the
            // next block update or random tick" ([[Sugar Cane]]). Uprooting
            // drops the plant, so it goes out on the same channel a flow uses
            // for a plant it sweeps aside.
            m_washedBlocks.push_back({at, here, Removal::Uprooted});
            setBlock(at.x, at.y, at.z, BlockId::Air);
            return;
        }
        // Only the top of a column grows, and only to three.
        if (blockAt(at.x, at.y + 1, at.z) != BlockId::Air) {
            return;
        }
        int height = 1;
        while (height < 3 && blockAt(at.x, at.y - height, at.z) == here) {
            ++height;
        }
        if (height >= 3 || (nextRandom() % 16) != 0) {
            return;
        }
        setBlock(at.x, at.y + 1, at.z, here);
        return;
    }

    // ---- Budding amethyst. ----
    //
    // minecraft.wiki [[Amethyst Cluster]], Obtaining: "Every time a budding
    // amethyst block receives a random tick, there is a 20% chance for a small
    // amethyst bud to generate on any of its sides, as long as the block being
    // replaced with the small amethyst bud is air or a water source block." One
    // face is chosen per successful tick and an obstructed face simply fails.
    //
    // **The budding block drives its buds' advance as well as their birth, and
    // that is a deliberate simplification.** The reference lets a bud take
    // random ticks of its own, but 20% is the only rate the wiki publishes for
    // this family and inventing a second one is not on. It also gets the
    // reference's other rule for free: a bud advances only while it is attached
    // to budding amethyst, and here nothing else ever asks. Budding amethyst is
    // never consumed, so a geode keeps paying.
    if (here == BlockId::BuddingAmethyst) {
        if ((nextRandom() % 5) != 0) {
            return;
        }
        const glm::ivec3 face = at + kLightSteps[static_cast<std::size_t>(nextRandom() %
                                                                        kLightSteps.size())];
        const BlockId occupant = blockAt(face.x, face.y, face.z);
        BlockId next = BlockId::Air;
        switch (occupant) {
        case BlockId::SmallAmethystBud:
            next = BlockId::MediumAmethystBud;
            break;
        case BlockId::MediumAmethystBud:
            next = BlockId::LargeAmethystBud;
            break;
        case BlockId::LargeAmethystBud:
            next = BlockId::AmethystCluster;
            break;
        default:
            if (occupant == BlockId::Air || isWaterSource(occupant)) {
                next = BlockId::SmallAmethystBud;
            }
            break;
        }
        if (next != BlockId::Air) {
            setBlock(face.x, face.y, face.z, next);
        }
        return;
    }

    // ---- Grass and mycelium. ----
    //
    // minecraft.wiki [[Grass Block]] and [[Mycelium]], both "Spreading". A
    // random tick on the *source* block does the work, which is why this sits
    // here rather than anywhere near the dirt it converts.
    if (here == BlockId::Grass || here == BlockId::Mycelium) {
        const auto litAt = [this](const glm::ivec3& c) {
            return std::max(skyLightAt(c.x, c.y, c.z), blockLightAt(c.x, c.y, c.z));
        };
        // "Nothing light-impeding or fluid on top", expressed in the one term
        // this engine actually has. Our light model is a yes/no per block
        // rather than the reference's 0-15 opacity, so `admitsLight` is the
        // nearest true statement of "opacity below 2" - and it is the same
        // predicate the light propagator itself uses, so a block can never be
        // dark enough to kill grass yet bright enough to pass light.
        const glm::ivec3 lid{at.x, at.y + 1, at.z};
        const BlockId cover = blockAt(lid.x, lid.y, lid.z);
        const bool drowned = isWater(cover) || isLava(cover) || waterloggedAt(lid.x, lid.y, lid.z);
        if (drowned || (!admitsLight(cover) && litAt(lid) <= 4)) {
            setBlock(at.x, at.y, at.z, BlockId::Dirt);
            return;
        }
        if (litAt(lid) < 9) {
            return;
        }
        // Four independent attempts per random tick, each at a cell drawn from
        // the 3x5x3 box centred one above this one - dy from -3 to +1 - and a
        // wasted draw is simply a wasted attempt, which is what makes a lone
        // grass block heal a scar slowly and a field heal it fast.
        for (int attempt = 0; attempt < 4; ++attempt) {
            const std::uint32_t roll = nextRandom();
            const glm::ivec3 target{at.x + static_cast<int>((roll >> 2) % 3) - 1,
                                    at.y + static_cast<int>((roll >> 9) % 5) - 3,
                                    at.z + static_cast<int>((roll >> 16) % 3) - 1};
            // **Plain dirt only.** Coarse dirt, rooted dirt, podzol and
            // mycelium all read as "dirt" to a player and none of them is a
            // legal target; that is exactly what makes coarse dirt worth
            // having.
            if (blockAt(target.x, target.y, target.z) != BlockId::Dirt) {
                continue;
            }
            const glm::ivec3 lidThere{target.x, target.y + 1, target.z};
            const BlockId coverThere = blockAt(lidThere.x, lidThere.y, lidThere.z);
            if (isWater(coverThere) || isLava(coverThere) ||
                waterloggedAt(lidThere.x, lidThere.y, lidThere.z) || !admitsLight(coverThere) ||
                litAt(lidThere) < 4) {
                continue;
            }
            setBlock(target.x, target.y, target.z, here);
        }
        return;
    }

    // ---- Ice and snow. ----
    //
    // minecraft.wiki [[Ice]]: ice melts "if the light level from blocks
    // adjacent to it is higher than 11". **Sky light is deliberately not
    // counted** - that is the whole reason a torch melts a frozen lake and
    // noon does not - so this reads `blockLightAt` and never `skyLightAt`.
    // Packed and blue ice never melt, so the test is the one id and not
    // `isIce`. [[Snow]]: a snow layer goes at the same threshold, one layer at
    // a time. Bedrock also melts snow in any non-snowy biome regardless of
    // light; that half needs a biome the world can be asked for at a point and
    // is left out rather than guessed.
    if (here == BlockId::Ice || isSnowLayer(here)) {
        int brightest = blockLightAt(at.x, at.y, at.z);
        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = at + step;
            brightest = std::max(brightest, blockLightAt(n.x, n.y, n.z));
        }
        if (brightest <= kIceLightThreshold) {
            return;
        }
        if (here == BlockId::Ice) {
            setBlock(at.x, at.y, at.z, BlockId::Water0);
            return;
        }
        const int depth = snowLayerDepth(here);
        setBlock(at.x, at.y, at.z, depth > 1 ? snowLayerAt(depth - 1) : BlockId::Air);
        return;
    }

    // ---- Saplings. ----
    //
    // minecraft.wiki [[Sapling]], Usage -> Growing trees. Two stages then a
    // tree, gated on light at the cell **above** the sapling and on the soil
    // beneath it - see `farming::kSaplingLight` and `farming::saplingRootsIn`
    // for the wording and for which of the two contradicting wiki pages this
    // follows.
    if (farming::isSapling(here)) {
        if (!farming::saplingRootsIn(blockAt(at.x, at.y - 1, at.z))) {
            return;
        }
        const glm::ivec3 above{at.x, at.y + 1, at.z};
        if (std::max(skyLightAt(above.x, above.y, above.z),
                     blockLightAt(above.x, above.y, above.z)) < farming::kSaplingLight) {
            return;
        }
        if ((nextRandom() % static_cast<std::uint32_t>(farming::kSaplingStageOdds)) != 0u) {
            return;
        }
        // The stage lives in the cell's own state bit, which is the reference's
        // Bedrock `age_bit` - one bit, exactly as it stores it.
        if (!stateBitAt(at.x, at.y, at.z)) {
            setStateBit(at.x, at.y, at.z, true);
            return;
        }
        growSaplingAt(at, here);
        return;
    }

    // ---- Leaf decay. ----
    //
    // The rule itself is `decayLeafIfOrphaned`, because two things ask for it:
    // this random tick and the queue a felled log fills. One function, so the
    // slow backstop and the prompt path can never come to different answers
    // about which leaf lives.
    if (isLeafBlock(here)) {
        decayLeafIfOrphaned(at);
        return;
    }

    // ---- Copper weathering. ----
    //
    // minecraft.wiki [[Oxidation]] SS Mechanics. The first gate is a flat
    // `copper::kPreOxidationNumerator / kPreOxidationDenominator` of random
    // ticks; everything after it costs a 129-cell scan, which is why it is
    // behind this roll and not in front of it.
    if (copper::weathered(here) != BlockId::Air) {
        if ((nextRandom() % static_cast<std::uint32_t>(copper::kPreOxidationDenominator)) >=
            static_cast<std::uint32_t>(copper::kPreOxidationNumerator)) {
            return;
        }
        weatherCopperAt(at, here);
        return;
    }

    const bool crop = isCropBlock(here);
    const bool stem = isGrowingStem(here);
    if (!crop && !stem) {
        return;
    }

    // A plant needs tilled ground under it and light at its own cell.
    const glm::ivec3 soil{at.x, at.y - 1, at.z};
    const BlockId under = blockAt(soil.x, soil.y, soil.z);
    if (!isFarmland(under)) {
        return;
    }
    if (std::max(skyLightAt(at.x, at.y, at.z), blockLightAt(at.x, at.y, at.z)) < 9) {
        return;
    }

    // The reference's points, counted in quarters so the sum stays integral.
    // They come from the **three-by-three patch of farmland**, not from the
    // plant - which is why a lone crop in a field grows faster than one on its
    // own however well watered it is.
    //
    // The plants themselves enter only through the crowding rule below, which
    // halves the lot: the same crop on a diagonal, or on both axes at once,
    // costs half the growth. **That is the whole reason to plant in rows**, and
    // without it a solid nine-by-nine field grew at exactly twice the
    // reference's rate and the cheapest layout was also the fastest.
    int quarters = farming::farmlandPointsUnder(under);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            quarters += farming::farmlandPointsNear(blockAt(soil.x + dx, soil.y, soil.z + dz));
        }
    }

    const float roll = static_cast<float>(nextRandom() & 0xFFFFFF) /
                       static_cast<float>(0x1000000);
    if (roll >= farming::growthChance(quarters, cropIsCrowdedAt(at, here))) {
        return;
    }

    if (crop) {
        const int age = cropAge(here);
        if (age < 7) {
            setBlock(at.x, at.y, at.z, cropAt(cropFamily(here), age + 1));
        }
        return;
    }

    // ---- A stem. ----
    const int age = stemAge(here);
    if (age < 7) {
        setBlock(at.x, at.y, at.z, static_cast<BlockId>(static_cast<int>(here) + 1));
        return;
    }

    // Ripe: try to put a fruit down. **One cell is chosen at random and a bad
    // choice wastes the attempt** - minecraft.wiki [[Melon]]: "it attempts to
    // generate a melon block in one of the four immediately adjacent blocks;
    // however, this attempt may fail if the chosen adjacent block is not empty
    // or the block beneath is not an appropriate block". Walking the four in
    // order and taking the first legal one made a stem with a single free side
    // fruit as fast as one with four, which is four times the published rate
    // for the one-stem-one-slot layout every farm is built as. The support
    // test looks at the block beneath the candidate cell, not at the cell.
    const bool melon = stemGrowsMelon(here);
    const int side = static_cast<int>(nextRandom() % kFruitSides.size());
    const glm::ivec3 cell = at + kFruitSides[side];
    if (blockAt(cell.x, cell.y, cell.z) != BlockId::Air) {
        return;
    }
    if (!farming::supportsFruit(blockAt(cell.x, cell.y - 1, cell.z))) {
        return;
    }
    setBlock(cell.x, cell.y, cell.z, melon ? BlockId::Melon : BlockId::Pumpkin);
    // The stem now points at what it grew and produces nothing more until that
    // fruit is taken. Attached order matches `kFruitSides`, so the facing is the
    // index rather than a second table.
    const BlockId attached =
        melon ? BlockId::MelonStemAttachedFirst : BlockId::PumpkinStemAttachedFirst;
    setBlock(at.x, at.y, at.z, static_cast<BlockId>(static_cast<int>(attached) + side));
}

bool World::stateBitAt(int x, int y, int z) const {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return false;
    }
    const auto it = m_chunks.find(
        ChunkCoord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)});
    if (it == m_chunks.end()) {
        return false;
    }
    return it->second.blocks.stateBitAt(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize),
                                        floorMod(z, Chunk::kSize));
}

void World::setStateBit(int x, int y, int z, bool value) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    const auto it = m_chunks.find(
        ChunkCoord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)});
    if (it == m_chunks.end()) {
        return;
    }
    it->second.blocks.setStateBit(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize),
                                  floorMod(z, Chunk::kSize), value);
    it->second.modified = true;
}

bool World::leafHasWoodNearby(const glm::ivec3& at) const {
    constexpr int kReach = farming::kLeafDecayReach;
    constexpr int kSide = 2 * kReach + 1;
    constexpr std::size_t kCells = static_cast<std::size_t>(kSide) * kSide * kSide;

    // **A flood fill through leaves, six faces at a time, not a box test.**
    // minecraft.wiki [[Leaves]] calls the metric taxicab distance "but can
    // cross corners", which is what a bending path through leaves is; the
    // pre-1.13 algorithm the same wiki says Bedrock still uses is a
    // nine-cubed array with four rounds of face propagation, and this is that
    // walk done from the leaf outward instead of from the logs inward. The two
    // are symmetric.
    //
    // Everything is on the stack and sized by `kReach`: 729 flags and at most
    // 129 queued cells at reach 4, so the search allocates nothing and a
    // canopy being ticked cannot touch the heap.
    std::array<bool, kCells> seen{};
    std::array<glm::ivec3, kCells> queue{};
    std::array<std::uint8_t, kCells> depth{};

    const auto index = [&at](const glm::ivec3& p) {
        return static_cast<std::size_t>((p.x - at.x + kReach) + (p.y - at.y + kReach) * kSide +
                                        (p.z - at.z + kReach) * kSide * kSide);
    };

    std::size_t head = 0;
    std::size_t tail = 0;
    seen[index(at)] = true;
    queue[tail] = at;
    depth[tail] = 0;
    ++tail;

    while (head < tail) {
        const glm::ivec3 cell = queue[head];
        const int d = depth[head];
        ++head;
        if (d >= kReach) {
            continue;
        }
        for (const glm::ivec3& step : kLightSteps) {
            const glm::ivec3 n = cell + step;
            const BlockId there = blockAt(n.x, n.y, n.z);
            // A log found at depth `d + 1` is `d + 1` steps away, and the leaf
            // lives if that is within reach - which it is, because `d < kReach`.
            if (farming::leafKeeper(there)) {
                return true;
            }
            if (!isLeafBlock(there)) {
                continue;
            }
            const std::size_t at_n = index(n);
            if (seen[at_n]) {
                continue;
            }
            seen[at_n] = true;
            queue[tail] = n;
            depth[tail] = static_cast<std::uint8_t>(d + 1);
            ++tail;
        }
    }
    return false;
}

bool World::growSaplingAt(const glm::ivec3& at, BlockId sapling) {
    // **The northwest corner is the anchor**, which is the reference's own
    // convention: minecraft.wiki [[Sapling]] says a two-by-two grows from the
    // square a sapling belongs to, and any of the four may be the one that is
    // ticked or bone-mealed. So four candidate corners are tried, not one.
    glm::ivec3 corner = at;
    bool twoByTwo = false;
    if (structures::saplingHasGiantForm(sapling)) {
        constexpr std::array<glm::ivec3, 4> kCorners{glm::ivec3{0, 0, 0}, glm::ivec3{-1, 0, 0},
                                                     glm::ivec3{0, 0, -1}, glm::ivec3{-1, 0, -1}};
        for (const glm::ivec3& offset : kCorners) {
            const glm::ivec3 nw = at + offset;
            bool square = true;
            for (int dz = 0; dz <= 1 && square; ++dz) {
                for (int dx = 0; dx <= 1 && square; ++dx) {
                    square = blockAt(nw.x + dx, nw.y, nw.z + dz) == sapling;
                }
            }
            if (square) {
                corner = nw;
                twoByTwo = true;
                break;
            }
        }
    }
    if (structures::saplingNeedsGiantForm(sapling) && !twoByTwo) {
        // A lone dark oak sapling never grows. Bedrock's rule, and the reason a
        // dark oak forest has to be planted deliberately.
        return false;
    }

    // The plan comes back before anything is checked, because the reference
    // draws a height first and then asks whether *that* tree fits.
    m_growthRandom ^= m_growthRandom << 13;
    m_growthRandom ^= m_growthRandom >> 17;
    m_growthRandom ^= m_growthRandom << 5;
    const structures::SaplingPlan plan =
        structures::planSaplingTree(sapling, corner.x, corner.y, corner.z, m_growthRandom, twoByTwo);
    if (!plan.valid) {
        return false;
    }

    // **And the box has to be readable before it can be believed.** Everything
    // below reads through `blockAt`, which answers `Air` for a chunk that is
    // not resident - so a sapling four blocks from a column boundary at the
    // streaming frontier finds imaginary room, and then `setBlock` drops every
    // log and leaf that lands outside the resident chunks on the floor. The
    // result is a permanently half-built tree, sliced flat down a chunk line.
    //
    // Same walk `decayLeafIfOrphaned` and the farmland rule make, at this
    // plan's own reach rather than a constant: the clearance box below runs dx
    // and dz from `-radius` to `radius + trunkSpan - 1`, so the furthest cell
    // it can name is that upper bound, taken over every level it checks.
    // Refusing is what the plan already does when a wall is in the way, and it
    // costs one random tick - or one bone meal, which "no room" already costs.
    int planReach = 0;
    for (int dy = 1; dy <= plan.clearHeight; ++dy) {
        planReach = std::max(planReach, plan.clearedRadiusAt(dy) + plan.trunkSpan - 1);
    }
    if (!columnsResidentAround(corner.x, corner.z, planReach)) {
        return false;
    }

    // The reference's taper, and it comes off the plan rather than being
    // written out again here - see `SaplingPlan::clearedRadiusAt` for why that
    // matters. A box for the whole height would refuse to grow a tree beside a
    // wall, which the reference does not.
    for (int dy = 1; dy <= plan.clearHeight; ++dy) {
        const int radius = plan.clearedRadiusAt(dy);
        for (int dz = -radius; dz <= radius + plan.trunkSpan - 1; ++dz) {
            for (int dx = -radius; dx <= radius + plan.trunkSpan - 1; ++dx) {
                const BlockId there =
                    blockAt(corner.x + dx, corner.y + dy, corner.z + dz);
                // Air and leaves only, which is the reference's own test -
                // **and in Bedrock a log or a wood block above a sapling does
                // block it**, where Java lets a tree grow through its parent's
                // trunk. The saplings of a two-by-two are the other exception.
                if (there == BlockId::Air || isLeafBlock(there) || farming::isSapling(there)) {
                    continue;
                }
                return false;
            }
        }
    }

    // Nothing has been written until here, so a refusal above costs the sapling
    // nothing and leaves no half-built tree - which is the whole reason the
    // plan is computed before it is applied.
    const int span = plan.trunkSpan;
    for (int dz = 0; dz < span; ++dz) {
        for (int dx = 0; dx < span; ++dx) {
            setBlock(corner.x + dx, corner.y, corner.z + dz, BlockId::Air, Placement::Natural);
        }
    }
    for (const structures::PlannedBlock& block : plan.blocks) {
        if (block.onlyIntoAir) {
            // **Air or a leaf that grew there**, which is the reference's
            // `isAirOrLeaves` and is what lets two canopies merge instead of
            // one tree coming out with a bite missing. A leaf someone placed
            // by hand is exempt: it carries the persistent bit, and a tree
            // eating a hedge is not a mechanic anybody asked for.
            const BlockId there = blockAt(block.x, block.y, block.z);
            const bool free =
                there == BlockId::Air ||
                (isLeafBlock(there) && !stateBitAt(block.x, block.y, block.z));
            if (!free) {
                continue;
            }
        }
        // **`Natural`, so every leaf this tree grows can decay.** A leaf placed
        // by hand keeps its `persistent_bit` and never does; this is the only
        // caller in the game that clears it deliberately.
        setBlock(block.x, block.y, block.z, block.block, Placement::Natural);
    }
    return true;
}

void World::weatherCopperAt(const glm::ivec3& at, BlockId here) {
    const int stage = copper::stageOf(here);
    // The reference's `a` and `b`: every non-waxed copper block within taxicab
    // `copper::kScanReach`, and how many of those are further gone than this
    // one. **This block counts itself**, which is why an isolated block's `c`
    // is one half rather than one.
    int nearby = 0;
    int higher = 0;
    for (int dx = -copper::kScanReach; dx <= copper::kScanReach; ++dx) {
        for (int dy = -copper::kScanReach; dy <= copper::kScanReach; ++dy) {
            for (int dz = -copper::kScanReach; dz <= copper::kScanReach; ++dz) {
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) > copper::kScanReach) {
                    continue;
                }
                const BlockId other = blockAt(at.x + dx, at.y + dy, at.z + dz);
                if (!copper::isCopper(other) || copper::isWaxed(other)) {
                    continue;
                }
                const int otherStage = copper::stageOf(other);
                // **Any less-oxidised copper nearby stops this outright.** Not
                // "more than one stage below" - one block of fresh copper holds
                // a whole wall back, and that is the mechanic players build
                // with.
                if (otherStage < stage) {
                    return;
                }
                ++nearby;
                if (otherStage > stage) {
                    ++higher;
                }
            }
        }
    }

    m_growthRandom ^= m_growthRandom << 13;
    m_growthRandom ^= m_growthRandom >> 17;
    m_growthRandom ^= m_growthRandom << 5;
    const int chance = copper::oxidationChancePpm(nearby, higher, stage);
    if (static_cast<int>(m_growthRandom % 1000000u) >= chance) {
        return;
    }
    setBlock(at.x, at.y, at.z, copper::weathered(here));
}

bool World::applyBoneMeal(const glm::ivec3& at) {
    const BlockId here = blockAt(at.x, at.y, at.z);
    // The growth sampler, advanced in place. Shared with the branches below so
    // one application draws one stream rather than each branch reaching for
    // `m_growthRandom` in its own way.
    const auto roll = [this] {
        m_growthRandom ^= m_growthRandom << 13;
        m_growthRandom ^= m_growthRandom >> 17;
        m_growthRandom ^= m_growthRandom << 5;
        return m_growthRandom;
    };
    // **The three plants the reference accepts bone meal on that are not crops.**
    // minecraft.wiki [[Bone Meal]], the fertilizer table. Answered before the
    // crop test because none of them is a crop and each has its own rule.
    //
    // The refusals matter as much and are what the early return below keeps:
    // cactus (MCPE-73214, works as intended), nether wart ("Bone meal cannot be
    // used on the Nether wart") and chorus all take it and do nothing.
    if (isCocoa(here)) {
        // "Matures 1 growth stage."
        if (cocoaAge(here) >= 2) {
            return false;
        }
        setBlock(at.x, at.y, at.z, cocoaAt(cocoaFacing(here), cocoaAge(here) + 1));
        return true;
    }
    if (here == BlockId::SugarCane) {
        // **Bedrock only, and the reverse of Java** (MC-73963, where it does
        // nothing): "grows to maximum height (three blocks tall)". Bedrock is
        // our reference, so it grows - and it grows from the top of the stalk,
        // which may be several blocks above the one that was clicked.
        glm::ivec3 top = at;
        while (blockAt(top.x, top.y + 1, top.z) == BlockId::SugarCane) {
            ++top.y;
        }
        int height = 1;
        while (height < 3 && blockAt(top.x, top.y - height, top.z) == BlockId::SugarCane) {
            ++height;
        }
        bool grew = false;
        for (; height < 3 && blockAt(top.x, top.y + 1, top.z) == BlockId::Air; ++height) {
            ++top.y;
            setBlock(top.x, top.y, top.z, BlockId::SugarCane);
            grew = true;
        }
        return grew;
    }
    if (isTallFlower(here)) {
        // "Drops a copy of itself without breaking", which the wiki calls the
        // only way to reproduce a sunflower, lilac, rose bush or peony - so
        // without this those four are unrenewable. The *lower* half is the id
        // that carries the item, and clicking either half pays the same flower.
        const BlockId lower =
            isTallFlowerUpper(here) ? static_cast<BlockId>(static_cast<int>(here) - 1) : here;
        // **`Copy`, and it is the whole reason `Removal` exists.** Nothing is
        // removed here: the plant is still standing when this returns, so a
        // reader that treats the event as a break clears the twin, plays a
        // break sound and spawns break particles for a flower that never went
        // anywhere. It used to be told apart by re-reading the cell afterwards
        // and finding it still occupied, which is true today and stops being
        // true the moment anything else writes there first.
        m_washedBlocks.push_back({at, lower, Removal::Copy});
        return true;
    }
    if (farming::isSapling(here)) {
        // minecraft.wiki [[Bone Meal]], Fertilizer, Saplings: *"The sapling has
        // a 45% chance of growing to the next growth stage, if possible."*
        // **A stage, not a tree**, and the item is spent either way - the same
        // page notes that bone-mealing an obstructed sapling simply wastes it.
        // So this returns `true` whatever happens, which is what makes the
        // caller consume the meal.
        m_growthRandom ^= m_growthRandom << 13;
        m_growthRandom ^= m_growthRandom >> 17;
        m_growthRandom ^= m_growthRandom << 5;
        if (static_cast<int>(m_growthRandom % 100u) >= farming::kSaplingBoneMealPercent) {
            return true;
        }
        if (!stateBitAt(at.x, at.y, at.z)) {
            setStateBit(at.x, at.y, at.z, true);
            return true;
        }
        // **Bone-mealing any one of a two-by-two grows the whole thing**, which
        // `growSaplingAt` owns because the random tick needs the identical
        // rule - two copies of "find the square" is the one thing this must not
        // become.
        growSaplingAt(at, here);
        return true;
    }

    // ---- The rest of the reference's fertilizer table. ----
    //
    // **Everything below did nothing at all until now.** `applyBoneMeal`
    // returned false for anything that was not a crop, a stem, cocoa, cane or a
    // sapling, so bone meal - which a player gets by the stack from a skeleton
    // and from a composter - was inert on nine of the plants the reference
    // accepts it on, silently, with the item not even consumed.
    //
    // minecraft.wiki [[Bone Meal]], Usage -> Fertilizer is the whole list, and
    // **the Bedrock and Java columns differ enough that following the wrong one
    // is a real bug**: small flowers spreading and sugar cane growing are
    // Bedrock-only, ferns out of grass are Bedrock-only, and Java's grass box
    // is 15x15x15 against Bedrock's 7x5x7. Bedrock wins throughout, per
    // `CLAUDE.md`.
    //
    // The refusals stay refusals and are as much a part of the table: cactus
    // (MCPE-73214, "works as intended"), nether wart and chorus all take the
    // click and do nothing.

    // **Grass block: the one row that generates rather than advances.**
    // Everything else in the table bumps a state; this one places new blocks in
    // a box around itself, which is why it is the only one that needs a biome.
    if (here == BlockId::Grass) {
        // Pure in the sense architecture rule 1 needs: `sampleBiome` is a
        // function of `(seed, x, z)` alone, the same call the generator makes,
        // so a bone-mealed patch matches the ground cover around it.
        const BiomeSample biome = sampleBiome(m_seed, at.x, at.z);
        // Ferns are `[BE only]` here, and only where they grow anyway - the
        // taiga and jungle families, which is what these two tags name.
        const bool ferny = biomeHasTag(biome.dominant, BiomeTag::Cold) ||
                           biomeHasTag(biome.dominant, BiomeTag::Jungle);
        const bool swampy = biomeHasTag(biome.dominant, BiomeTag::Swamp);
        constexpr int halfXZ = farming::kBoneMealGrassSpanXZ / 2;
        constexpr int halfY = farming::kBoneMealGrassSpanY / 2;
        bool grew = false;
        for (int attempt = 0; attempt < farming::kBoneMealGrassAttempts; ++attempt) {
            const std::uint32_t r = roll();
            const glm::ivec3 soil{
                at.x + static_cast<int>(r % farming::kBoneMealGrassSpanXZ) - halfXZ,
                at.y + static_cast<int>((r / 8u) % farming::kBoneMealGrassSpanY) - halfY,
                at.z + static_cast<int>((r / 64u) % farming::kBoneMealGrassSpanXZ) - halfXZ};
            // "on top of the block and nearby grass blocks" - grass, not dirt,
            // and the cell above it has to be free.
            if (blockAt(soil.x, soil.y, soil.z) != BlockId::Grass) {
                continue;
            }
            const glm::ivec3 cell{soil.x, soil.y + 1, soil.z};
            if (blockAt(cell.x, cell.y, cell.z) != BlockId::Air) {
                continue;
            }
            const std::uint32_t pick = roll();
            BlockId sprout = BlockId::TallGrass;
            if ((pick % farming::kBoneMealFlowerOdds) == 0u) {
                // The six-id run from `Cornflower` to `OrangeTulip` is the
                // small-flower block the generator's own table draws from; the
                // swamp's blue orchid is the one biome the reference singles
                // out and the generator already special-cases.
                sprout = swampy
                             ? BlockId::BlueOrchid
                             : static_cast<BlockId>(static_cast<int>(BlockId::Cornflower) +
                                                    static_cast<int>((pick / 8u) %
                                                                     static_cast<std::uint32_t>(
                                                                         kSmallFlowerRun)));
            } else if (ferny && ((pick / 8u) % farming::kBoneMealFernShare) == 0u) {
                sprout = BlockId::Fern;
            }
            setBlock(cell.x, cell.y, cell.z, sprout);
            grew = true;
        }
        return grew;
    }

    // A fern doubles. `LargeFern` is one id here where the reference's is two
    // blocks tall, which is an approximation `Block.hpp` already made and not a
    // decision taken in this function.
    if (here == BlockId::Fern) {
        setBlock(at.x, at.y, at.z, BlockId::LargeFern);
        return true;
    }

    // **Small flowers spread, `[BE only]`.** minecraft.wiki [[Bone Meal]]: in
    // Bedrock a flower spreads copies of itself to nearby grass; in Java bone
    // meal on a flower does nothing at all. Tall flowers are the branch above
    // and pay a copy as an item instead, which is the same page's other row.
    if (isFlower(here) && !isTallFlower(here)) {
        constexpr int reach = farming::kBoneMealFlowerSpreadReach;
        bool grew = false;
        for (int attempt = 0; attempt < farming::kBoneMealFlowerSpreadAttempts; ++attempt) {
            const std::uint32_t r = roll();
            const glm::ivec3 soil{at.x + static_cast<int>(r % (2 * reach + 1)) - reach,
                                  at.y + static_cast<int>((r / 8u) % 3u) - 1,
                                  at.z + static_cast<int>((r / 64u) % (2 * reach + 1)) - reach};
            const BlockId ground = blockAt(soil.x, soil.y, soil.z);
            if (ground != BlockId::Grass && ground != BlockId::Dirt &&
                ground != BlockId::Podzol && ground != BlockId::CoarseDirt) {
                continue;
            }
            const glm::ivec3 cell{soil.x, soil.y + 1, soil.z};
            if (blockAt(cell.x, cell.y, cell.z) != BlockId::Air) {
                continue;
            }
            setBlock(cell.x, cell.y, cell.z, here);
            grew = true;
        }
        return grew;
    }

    // Moss spreads over what it can reach. minecraft.wiki [[Moss Block]]: bone
    // meal "converts some blocks in a 5x3x5 area centered on the moss block
    // into moss blocks", and only ones with air above them.
    if (here == BlockId::MossBlock) {
        constexpr int span = 2 * farming::kBoneMealMossReach + 1;
        constexpr int height = 2 * farming::kBoneMealMossHeight + 1;
        bool grew = false;
        for (int attempt = 0; attempt < farming::kBoneMealMossAttempts; ++attempt) {
            const std::uint32_t r = roll();
            const glm::ivec3 cell{
                at.x + static_cast<int>(r % span) - farming::kBoneMealMossReach,
                at.y + static_cast<int>((r / 8u) % height) - farming::kBoneMealMossHeight,
                at.z + static_cast<int>((r / 64u) % span) - farming::kBoneMealMossReach};
            const BlockId ground = blockAt(cell.x, cell.y, cell.z);
            if (ground != BlockId::Grass && ground != BlockId::Dirt &&
                ground != BlockId::CoarseDirt && ground != BlockId::RootedDirt &&
                ground != BlockId::Podzol && ground != BlockId::Mycelium &&
                ground != BlockId::Stone) {
                continue;
            }
            if (blockAt(cell.x, cell.y + 1, cell.z) != BlockId::Air) {
                continue;
            }
            setBlock(cell.x, cell.y, cell.z, BlockId::MossBlock);
            grew = true;
        }
        return grew;
    }

    // Kelp grows one block, into the water above it. minecraft.wiki [[Kelp]]:
    // bone meal "causes the kelp to grow by 1 block". Applied at the top of the
    // strand however far down it was clicked, exactly as sugar cane is.
    if (here == BlockId::Kelp) {
        glm::ivec3 top = at;
        while (blockAt(top.x, top.y + 1, top.z) == BlockId::Kelp) {
            ++top.y;
        }
        if (!isWater(blockAt(top.x, top.y + 1, top.z))) {
            return false;
        }
        setBlock(top.x, top.y + 1, top.z, BlockId::Kelp);
        return true;
    }

    // Bamboo shoots up one or two stems, to the height the reference caps it
    // at. minecraft.wiki [[Bamboo]]: bone meal "grows the bamboo by 1-2 stems".
    if (here == BlockId::Bamboo) {
        glm::ivec3 top = at;
        while (blockAt(top.x, top.y + 1, top.z) == BlockId::Bamboo) {
            ++top.y;
        }
        int height = 1;
        while (blockAt(top.x, top.y - height, top.z) == BlockId::Bamboo) {
            ++height;
        }
        constexpr unsigned spread =
            farming::kBoneMealBambooMax - farming::kBoneMealBambooMin + 1u;
        const int wanted =
            farming::kBoneMealBambooMin + static_cast<int>(roll() % spread);
        bool grew = false;
        for (int step = 0; step < wanted && height < farming::kBambooMaxHeight;
             ++step, ++height) {
            if (blockAt(top.x, top.y + 1, top.z) != BlockId::Air) {
                break;
            }
            ++top.y;
            setBlock(top.x, top.y, top.z, BlockId::Bamboo);
            grew = true;
        }
        return grew;
    }

    // A dripleaf grows up. minecraft.wiki [[Small Dripleaf]]: bone meal turns
    // it into a big dripleaf. [[Big Dripleaf]]: bone meal "grows the stem by
    // one block" - ours has no separate stem id, so the leaf itself moves up
    // and leaves nothing behind. Filed as a `Block.hpp` finding.
    if (here == BlockId::SmallDripleaf) {
        setBlock(at.x, at.y, at.z, BlockId::BigDripleaf);
        return true;
    }
    if (here == BlockId::BigDripleaf) {
        if (blockAt(at.x, at.y + 1, at.z) != BlockId::Air) {
            return false;
        }
        setBlock(at.x, at.y + 1, at.z, BlockId::BigDripleaf);
        setBlock(at.x, at.y, at.z, BlockId::Air);
        return true;
    }

    // Rooted dirt hangs roots. minecraft.wiki [[Rooted Dirt]]: bone meal
    // "generates hanging roots on the block below, if it is air".
    if (here == BlockId::RootedDirt) {
        if (blockAt(at.x, at.y - 1, at.z) != BlockId::Air) {
            return false;
        }
        setBlock(at.x, at.y - 1, at.z, BlockId::HangingRoots);
        return true;
    }

    // Cave vines fruit. minecraft.wiki [[Glow Berries]]: bone meal on a cave
    // vine grows glow berries on it, which is a separate id here.
    if (here == BlockId::CaveVines) {
        setBlock(at.x, at.y, at.z, BlockId::CaveVinesBerries);
        return true;
    }

    // Glow lichen spreads onto a face beside it. One id and no facing here, so
    // "an empty cell one step over with something solid behind it" is as close
    // as the block table allows - the reference grows it across the face it is
    // already on.
    if (here == BlockId::GlowLichen) {
        for (std::size_t attempt = 0; attempt < kLightSteps.size(); ++attempt) {
            const glm::ivec3 cell = at + kLightSteps[roll() % kLightSteps.size()];
            if (blockAt(cell.x, cell.y, cell.z) != BlockId::Air) {
                continue;
            }
            // It has to have something to cling to, or it hangs in space.
            bool anchored = false;
            for (const glm::ivec3& face : kLightSteps) {
                const glm::ivec3 behind = cell + face;
                if (game::isSolid(blockAt(behind.x, behind.y, behind.z))) {
                    anchored = true;
                    break;
                }
            }
            if (!anchored) {
                continue;
            }
            setBlock(cell.x, cell.y, cell.z, BlockId::GlowLichen);
            return true;
        }
        return false;
    }

    if (!isCropBlock(here) && !isGrowingStem(here)) {
        return false;
    }
    m_growthRandom ^= m_growthRandom << 13;
    m_growthRandom ^= m_growthRandom >> 17;
    m_growthRandom ^= m_growthRandom << 5;
    // **Beetroot takes a single stage and only three times in four**, which is
    // the reference's own split and the reason bone meal is poor value on it.
    if (isCropBlock(here) && farming::boneMealIsSingleStage(cropFamily(here))) {
        if ((m_growthRandom % 4u) != 0u) {
            const int age = cropAge(here);
            if (age < 7) {
                setBlock(at.x, at.y, at.z, cropAt(cropFamily(here), age + 1));
            }
        }
        return true;
    }
    // Everything else jumps two to five stages. Straight to the advance,
    // skipping the chance roll: bone meal is not a faster tick, it is a
    // guaranteed one.
    const int steps = 2 + static_cast<int>(m_growthRandom % 4u);
    for (int step = 0; step < steps; ++step) {
        const BlockId current = blockAt(at.x, at.y, at.z);
        if (isCropBlock(current)) {
            const int age = cropAge(current);
            if (age >= 7) {
                break;
            }
            setBlock(at.x, at.y, at.z, cropAt(cropFamily(current), age + 1));
        } else if (isGrowingStem(current)) {
            // A stem is advanced but **never fruited** by bone meal, which is
            // the reference's rule and the whole reason a melon farm takes time.
            if (stemAge(current) >= 7) {
                break;
            }
            setBlock(at.x, at.y, at.z, static_cast<BlockId>(static_cast<int>(current) + 1));
        } else {
            break;
        }
    }
    return true;
}

void World::updateGrowth(const BudgetCheck& budgetSpent) {
    const auto now = Clock::now();
    // **Load-bearing, and it looks redundant - do not delete it.** A
    // default-constructed `time_point` is the clock epoch, which is in the
    // past, so *every* deadline test below is already true on the first frame
    // after a world load. Without this rebase the catch-up loop would run the
    // full `kMaxGrowthCatchUp` ticks in one frame, every single load: a field
    // that sprints the moment the world appears, which is exactly what the cap
    // further down exists to prevent. `m_nextGrowthTick` is **not** saved and
    // is default-constructed by `World.hpp`, so this is the state on every
    // load and not an edge case. Asked and answered 2026-08-19; the question
    // is easy to ask twice because the guard has no visible caller.
    //
    // What would make this comment false: `m_nextGrowthTick` gaining a
    // serialised value in `WorldStore`, or moving to a tick count - search
    // `m_nextGrowthTick`, which is the only name involved.
    if (m_nextGrowthTick.time_since_epoch().count() == 0) {
        m_nextGrowthTick = now;
    }
    if (now < m_nextGrowthTick) {
        return;
    }

    // **Advanced from the deadline it just met, not from `now`.** Writing
    // `now + kGrowthTick` folds the length of the frame that happened to notice
    // into the period: at 60fps a tick came round every 66ms rather than every
    // 50, so every farm in the world ran 25% slow, and at 30fps 40% slow - a
    // frame-rate-dependent game rule, which is the one thing a fixed tick exists
    // to prevent. Anchoring it here is what makes "per tick" mean the same
    // thing whatever the frame is doing, and it self-corrects: a frame that
    // arrives late still leaves the next deadline where it always was.
    int ticks = 0;
    while (m_nextGrowthTick <= now && ticks < kMaxGrowthCatchUp) {
        m_nextGrowthTick += kGrowthTick;
        ++ticks;
    }
    // Further behind than the cap can make up - a stall, a loading screen, a
    // breakpoint. The lost ticks are dropped rather than run as a burst: a
    // field that sprints for a second when the game unfreezes is worse than one
    // that misses a second.
    if (m_nextGrowthTick <= now) {
        m_nextGrowthTick = now + kGrowthTick;
    }

    const std::size_t chunkCount = m_chunks.size();
    if (chunkCount == 0) {
        return;
    }

    // **Where the walk starts moves.** It stops the moment the frame's budget is
    // gone and `m_chunks` iterates in a stable order, so a fixed start meant the
    // chunks at the far end were only ever sampled on a frame with time to
    // spare - which while flying is never, and those are exactly the chunks a
    // player leaves a farm in. Approximate on purpose: the map rehashes as
    // chunks stream in and out, so this only has to move, not to resume.
    if (m_growthCursor >= chunkCount) {
        m_growthCursor = 0;
    }
    auto it = m_chunks.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(m_growthCursor));

    for (std::size_t visited = 0; visited < chunkCount; ++visited) {
        if (it == m_chunks.end()) {
            it = m_chunks.begin();
        }
        if (budgetSpent()) {
            m_growthCursor = (m_growthCursor + visited) % chunkCount;
            return;
        }
        const ChunkCoord coord = it->first;
        ChunkSlot& slot = it->second;
        ++it;

        // Sampled straight out of the chunk rather than through `blockAt`, so
        // the common case - a cell of stone or air - costs one array read
        // instead of a hash lookup.
        //
        // Every tick owed is sampled, not just the latest one, or catching up
        // would advance the clock without doing the work it stands for.
        for (int tick = 0; tick < ticks; ++tick) {
            for (int sample = 0; sample < farming::kRandomTicksPerChunkPerTick; ++sample) {
                m_growthRandom ^= m_growthRandom << 13;
                m_growthRandom ^= m_growthRandom >> 17;
                m_growthRandom ^= m_growthRandom << 5;
                const std::uint32_t roll = m_growthRandom;
                const int lx = static_cast<int>((roll >> 2) % Chunk::kSize);
                const int ly = static_cast<int>((roll >> 10) % Chunk::kSize);
                const int lz = static_cast<int>((roll >> 18) % Chunk::kSize);
                const BlockId here = slot.blocks.at(lx, ly, lz);
                if (!randomTicks(here)) {
                    continue;
                }
                // **A world position on all three axes.** X and Z were converted
                // and Y was not, so every chunk above the bottom one handed
                // `growOne` a cell 32 or 64 blocks too low: nothing at or above
                // y=32 ever aged a stage, farmland never dried or re-moistened,
                // and the tick was then spent re-deciding whatever unrelated
                // cell sat down there. `gatherVolume` builds the same position
                // and is the shape this copies.
                growOne(glm::ivec3{coord.x * Chunk::kSize + lx, coord.y * Chunk::kSize + ly,
                                   coord.z * Chunk::kSize + lz});
            }
        }

        // **Weather, sampled once per column rather than once per chunk.**
        //
        // `m_chunks` holds every vertical slice of a column as a separate
        // entry, so the loop above visits this footprint `kWorldHeightChunks`
        // times per tick - which is exactly right for random ticks, because
        // each slice holds its own cells, and exactly wrong for weather,
        // because all three slices share one sky and one surface. Without this
        // gate a snowfield would fill three times as fast as the constant says
        // and the constant would be un-checkable against anything. `coord.y ==
        // 0` is the bottom slice, and `columnLoaded` requires every slice to be
        // present before a column counts as resident, so exactly one visit per
        // column passes and no column is missed.
        //
        // Riding on `updateGrowth` rather than becoming a twelfth pass in
        // `update` is deliberate: the tick catch-up above, the moving cursor
        // and the frame budget are three pieces of hard-won pacing, and the
        // most expensive bug shape in this project is a rule that exists in one
        // of the two places that need it. A second copy would drift.
        if (coord.y == 0) {
            for (int tick = 0; tick < ticks; ++tick) {
                for (int sample = 0; sample < kWeatherColumnsPerChunkPerTick; ++sample) {
                    m_growthRandom ^= m_growthRandom << 13;
                    m_growthRandom ^= m_growthRandom >> 17;
                    m_growthRandom ^= m_growthRandom << 5;
                    const std::uint32_t roll = m_growthRandom;
                    const int lx = static_cast<int>((roll >> 4) % Chunk::kSize);
                    const int lz = static_cast<int>((roll >> 20) % Chunk::kSize);
                    weatherTickColumn(coord.x * Chunk::kSize + lx, coord.z * Chunk::kSize + lz);
                }
            }
        }
    }
}

void World::weatherTickColumn(int worldX, int worldZ) {
    constexpr int worldTop = kWorldHeightChunks * Chunk::kSize - 1;

    const int chunkX = floorDiv(worldX, Chunk::kSize);
    const int chunkZ = floorDiv(worldZ, Chunk::kSize);

    // Hoisted once, the same shape the sky-light seeder uses: `kWorldHeightChunks`
    // hash lookups for the column, then plain array reads all the way down.
    // Walking with `blockAt` would be a lookup per cell of a ninety-six-block
    // descent, on four columns per chunk per tick, and this pass exists to be
    // cheap enough not to be noticed.
    //
    // The bail is also the residency test, so there is no separate
    // `columnResident` call: a column with a slice missing is one that is still
    // streaming in, and settling snow on a surface that is about to be replaced
    // by the real one is how a player ends up buried.
    std::array<const ChunkSlot*, kWorldHeightChunks> column{};
    for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
        const auto it = m_chunks.find({chunkX, cy, chunkZ});
        if (it == m_chunks.end()) {
            return;
        }
        column[static_cast<std::size_t>(cy)] = &it->second;
    }

    const int lx = floorMod(worldX, Chunk::kSize);
    const int lz = floorMod(worldZ, Chunk::kSize);

    // **The top of the column, not `highestSolid`.** That one skips water and
    // skips every flat block, which is correct for the questions it answers -
    // where a creature stands, where a village path is laid - and is precisely
    // wrong for both of the questions here: freezing needs the water surface it
    // skips, and settling snow needs the snow layer it skips, or every drift
    // would be re-founded at one layer for ever. So this walks for the first
    // cell that is not air, and because it starts at the world ceiling, finding
    // one *is* the sky-exposure test - there is nothing above it by
    // construction, which is why no `skyLightAt` call appears below.
    int top = -1;
    BlockId here = BlockId::Air;
    for (int y = worldTop; y >= 0; --y) {
        const BlockId id =
            column[static_cast<std::size_t>(y / Chunk::kSize)]->blocks.at(lx, y % Chunk::kSize, lz);
        if (id != BlockId::Air) {
            top = y;
            here = id;
            break;
        }
    }
    // Nothing at all in the column, or a surface pressed against the ceiling
    // with no room for a layer above it.
    if (top < 0 || top >= worldTop) {
        return;
    }

    // **The cheap gate, taken before anything expensive.** In clear weather the
    // only thing left to do is freeze standing water, so a column that is not
    // capped by a water source has already finished. That is nearly every
    // column nearly all the time, and it is what keeps this pass off the frame
    // time: without it every sampled column would pay for seven light lookups
    // and a biome evaluation to discover it had no work.
    const bool waterOnTop = here == BlockId::Water0;
    if (!waterOnTop && !m_weatherFalling) {
        return;
    }

    // The brightest block light this cell can see, computed once because both
    // rules below want it and it is the expensive part. **Block light only** -
    // counting the sky here would stop anything freezing in daylight, which is
    // the exact inverse of the reference and the reason the melting half three
    // hundred lines up reads `blockLightAt` too.
    int brightest = blockLightAt(worldX, top, worldZ);
    for (const glm::ivec3& step : kLightSteps) {
        brightest = std::max(brightest, blockLightAt(worldX + step.x, top + step.y, worldZ + step.z));
    }
    if (brightest > kIceLightThreshold) {
        // Above the threshold the melting half of `growOne` would undo whatever
        // was written here on its next visit. Sharing one constant is what makes
        // that a clean boundary instead of a band where a cell freezes and melts
        // on alternate ticks.
        return;
    }

    const BiomeSample biome = sampleBiome(m_seed, worldX, worldZ);
    const Biome& row = biomeInfo(biome.dominant);

    // ---- Ice. ----
    //
    // **Runs whatever the weather**, because the reference freezes ponds on a
    // clear night; it is a temperature rule, not a precipitation one. Asked as
    // `freezesAt` rather than through `precipitationFor`, and the difference is
    // real: that function answers `None` for a dry biome, which is right for
    // snowfall and wrong for ice, because a cold desert would still freeze.
    //
    // `Water0` and not `isWater`: only a full source block freezes, which is
    // what keeps a waterfall running and its pool solid. The write is the exact
    // inverse of the melt branch in `growOne`, which turns `Ice` back into
    // `Water0`, so the pair round-trips.
    if (waterOnTop) {
        if (freezesAt(m_seed, row.warmth, worldX, top, worldZ)) {
            setBlock(worldX, top, worldZ, BlockId::Ice);
        }
        return;
    }

    // ---- Settled snow. ----
    //
    // No `m_weatherFalling` test here: the gate above already required either
    // water on top or something falling, and the water case returned. Writing
    // it twice would read as belt-and-braces and would in fact be dead code,
    // which is worse than either.
    //
    // The column's own answer, jitter and all, so what settles here can never
    // disagree with the snow line the terrain was generated against.
    if (weather::precipitationFor(biome.dominant, top, m_seed, worldX, worldZ) !=
        weather::Precipitation::Snow) {
        return;
    }

    // How deep this particular column drifts, drawn once and stable for ever.
    //
    // **`snow` is published in blocks and the world stores layers**;
    // `snowLayersFrom` is the one place that conversion happens and
    // `SnowAccumulation`'s comment is where the unit is argued. A biome with no
    // pair - every warm one, and Meadow and Stony Peaks, which were checked and
    // genuinely have none - lands on zero here and leaves without writing.
    const int minLayers = snowLayersFrom(row.snow.minBlocks);
    const int maxLayers = snowLayersFrom(row.snow.maxBlocks);
    if (maxLayers <= 0) {
        return;
    }
    const int span = maxLayers - minLayers + 1;
    const int target =
        minLayers + static_cast<int>(columnNoise(m_seed, worldX, worldZ) % static_cast<std::uint32_t>(span));
    if (target <= 0) {
        return;
    }

    // Already as deep as this column goes. `BlockId::Snow` is the solid cube
    // `snowLayerAt` hands back at eight, so a column that reached its cap - or
    // one a snowy biome generated with snow as its surface material - reads as
    // full here and is left alone. Without this the cube would pass the floor
    // test below and start a second stack on top of itself, for ever.
    //
    // **It also stops melting at that point**, because `growOne`'s melt branch
    // tests `isSnowLayer` and the cube is not one - so eight layers is a
    // one-way door where seven is not. That follows from `Block.hpp` choosing
    // to represent the eighth layer as the solid block, which is another
    // owner's call and is filed rather than worked around here.
    if (here == BlockId::Snow) {
        return;
    }

    if (isSnowLayer(here)) {
        const int depth = snowLayerDepth(here);
        if (depth < target) {
            setBlock(worldX, top, worldZ, snowLayerAt(depth + 1));
        }
        return;
    }

    // A fresh first layer, on a floor.
    //
    // `coversTopOf` is the floor test the farmland rule already owns - a full
    // top surface, glass included, fences and torches and rails excluded - so a
    // drift never balances on a post. minecraft.wiki [[Snow]] adds two
    // exclusions this has to carry itself: snow "can be placed only on a solid
    // block that is not ice", and it "cannot be placed on farmland", which is
    // what stops a snowfall wrecking a field.
    //
    // **And a third, which is not hypothetical the moment `Biome.cpp` scatters
    // powder snow into a grove:** minecraft.wiki [[Powder Snow]], "Powder snow
    // cannot have a snow layer placed on it." Written as the bare enumerator
    // rather than a family test because there is exactly one of them.
    //
    // **This is probably already unreachable, and it stays anyway.**
    // `Block.hpp` now asserts `collisionBoxes(PowderSnow).count == 0` and
    // `!isSolid(PowderSnow)`, which kills `coversTopOf`'s second clause, so
    // unless `occludesFace(PowderSnow, 1)` is true - not checked, another
    // owner's table - the floor test refuses it first and this clause never
    // fires. Kept because the reference states it as a rule about powder snow,
    // not as a consequence of its shape: the day someone gives it a collision
    // box for climbing, or widens `coversTopOf`, the rule must not evaporate
    // with the accident that was enforcing it. A one-token cited guard is a
    // cheap price for that.
    if (!coversTopOf(here) || isIce(here) || isFarmland(here) || here == BlockId::PowderSnow) {
        return;
    }
    setBlock(worldX, top + 1, worldZ, snowLayerAt(1));
}

void World::updateFire(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_fireUpdates.empty() && !budgetSpent()) {
        if (m_fireUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_fireUpdates.front().position;
        m_fireUpdates.pop_front();
        // Everything below reads its neighbours through `blockAt`, which lies
        // about a chunk that is not resident; see `columnReadyOrDeferred`.
        // Without this a fire spreads into and "burns" air nobody has generated.
        if (!columnReadyOrDeferred(p, m_fireUpdates, kFireTickDelay)) {
            continue;
        }
        if (blockAt(p.x, p.y, p.z) != BlockId::Fire) {
            continue;
        }

        const BlockId under = blockAt(p.x, p.y - 1, p.z);
        // Netherrack and its cousins keep a fire for ever; everything else
        // needs either fuel beside it or a floor beneath it.
        const bool eternal = feedsEternalFire(under);

        bool fuelNearby = false;
        for (const glm::ivec3& step : kLightSteps) {
            if (isFlammable(blockAt(p.x + step.x, p.y + step.y, p.z + step.z))) {
                fuelNearby = true;
                break;
            }
        }

        if (!fireCanSurvive(p.x, p.y, p.z)) {
            setBlock(p.x, p.y, p.z, BlockId::Air);
            continue;
        }

        // Rain puts out anything the sky can reach. This is what makes a bolt
        // safe to hand a real fire to - the reference lights them during a
        // storm precisely because the same storm is already putting them out.
        //
        // **Water putting a fire out is the same hiss lava makes**, and it is
        // the same bank in the reference. The two branches above are not: a
        // fire that runs out of floor or out of fuel simply stops, with no
        // water involved and no sound.
        //
        // **`rainFallsOn`, not the bare storm bit**, which is where the sky test
        // and the biome test now live together: that bit is sampled in the
        // player's column, so this used to drown a desert campfire whenever it
        // drizzled wherever the player happened to be. Same call
        // `farmlandIsHydrated` makes.
        if (!feedsEternalFire(under) && rainFallsOn(p.x, p.y, p.z)) {
            setBlock(p.x, p.y, p.z, BlockId::Air);
            recordFizz(p, BlockId::Air);
            continue;
        }
        auto roll = [this]() {
            m_fireRandom ^= m_fireRandom << 13;
            m_fireRandom ^= m_fireRandom >> 17;
            m_fireRandom ^= m_fireRandom << 5;
            return m_fireRandom;
        };

        if (fuelNearby) {
            // **Spread is what consumes the fuel**, rather than a separate
            // burn-away pass: the block that catches is replaced by fire, so a
            // log wall is eaten one cell at a time and the flame front moves.
            for (const glm::ivec3& step : kLightSteps) {
                const glm::ivec3 n = p + step;
                const BlockId neighbour = blockAt(n.x, n.y, n.z);
                if (!isFlammable(neighbour)) {
                    continue;
                }
                // **Per-fuel odds, and the direction is part of the rule.**
                // minecraft.wiki [[Fire]]: `burnOdds / 300` sideways and
                // `burnOdds / 250` up and down - fire climbs and drops a little
                // more readily than it runs. Both denominators are far above
                // the largest odds in the table, so the modulus is a fair
                // fraction and needs no clamp.
                const unsigned denominator =
                    step.y != 0 ? kBurnDenominatorVertical : kBurnDenominatorSide;
                if ((roll() % denominator) >= burnOdds(neighbour)) {
                    continue;
                }
                // A charge does not burn away - it lights.
                if (neighbour == BlockId::Tnt) {
                    setBlock(n.x, n.y, n.z, BlockId::TntPrimed);
                    primeTnt(n);
                    continue;
                }
                setBlock(n.x, n.y, n.z, BlockId::Fire);
            }
        }

        // Left burning: come back and ask again. A fire with nothing left to eat
        // and no eternal floor dies on one of these passes.
        if (blockAt(p.x, p.y, p.z) == BlockId::Fire) {
            if (!eternal && !fuelNearby &&
                (roll() % kFireBurnoutDenominator) < kFireBurnoutOdds) {
                setBlock(p.x, p.y, p.z, BlockId::Air);
                continue;
            }
            scheduleFireUpdate(p.x, p.y, p.z);
        }
    }
}

std::vector<glm::ivec3> World::takeDetonations() {
    return std::exchange(m_detonations, {});
}

void World::updateTnt(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    // **Scanned, not drained from the front, and this queue alone.** The other
    // four cell queues hold their due order for free because every entry
    // carries the same delay (see `columnReadyOrDeferred`), so the head is
    // always the soonest and a `break` is safe. Charges stopped sharing one
    // delay the moment a blast-lit fuse became 0.5-1.5s where a hand-lit one is
    // 4s: a chain primed behind a hand-lit charge would sit unseen behind it
    // and go off up to a blink late, which on an explosion chain is exactly the
    // timing the short fuse exists to get right. The scan is over lit charges
    // only - tens, not thousands.
    for (std::size_t i = 0; i < m_tntFuses.size() && !budgetSpent();) {
        if (m_tntFuses[i].due > now) {
            ++i;
            continue;
        }
        const PendingFluid entry = m_tntFuses[i];
        m_tntFuses.erase(m_tntFuses.begin() + static_cast<std::ptrdiff_t>(i));
        const glm::ivec3 p = entry.position;
        const BlockId here = blockAt(p.x, p.y, p.z);
        // Broken or already gone. The fuse is not cancelled when the block is
        // mined - it is simply found missing here, which costs nothing and
        // means breaking a lit charge needs no bookkeeping of its own.
        if (!isTntBlock(here)) {
            continue;
        }
        if (entry.blinks > 0) {
            // **The blink is the block, not a texture swap.** Both animated
            // layer slots are spent on water and fire, and toggling the id
            // costs one remesh of one chunk a few times a second - which is
            // also what makes the countdown visible in the world's own state.
            setBlock(p.x, p.y, p.z,
                     here == BlockId::TntPrimed ? BlockId::Tnt : BlockId::TntPrimed);
            m_tntFuses.push_back({p, now + kTntBlink, entry.blinks - 1});
            continue;
        }
        setBlock(p.x, p.y, p.z, BlockId::Air);
        m_detonations.push_back(p);
    }
}

void World::scheduleFallUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_fallUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kFallDelay});
}

void World::updateFalls(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_fallUpdates.empty() && !budgetSpent()) {
        if (m_fallUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_fallUpdates.front().position;
        m_fallUpdates.pop_front();

        // The shared gate; see `columnReadyOrDeferred`. This queue is where the
        // rule started - a column on the edge of the loaded world would
        // otherwise pour itself into nothing and never come back - and the
        // other three read the same lie out of `blockAt`. Asked before anything
        // is decided, and it now defers rather than drops, so sand scheduled
        // while its column was still streaming in still falls once it arrives.
        if (!columnReadyOrDeferred(p, m_fallUpdates, kFallDelay)) {
            continue;
        }

        const BlockId falling = blockAt(p.x, p.y, p.z);
        if (p.y <= 0 || !isFalling(falling)) {
            continue;
        }
        if (!isReplaceable(blockAt(p.x, p.y - 1, p.z))) {
            continue;
        }

        // Detached, not moved. Where it ends up is the entity's business; all
        // the world does is take it out of the grid and say so - which also
        // schedules whatever was resting on top of it, so a column collapses
        // from the bottom without a loop here.
        setBlock(p.x, p.y, p.z, BlockId::Air);
        m_detachedBlocks.push_back({p, falling});
    }
}

std::vector<World::WashedBlock> World::takeDetachedBlocks() {
    return std::exchange(m_detachedBlocks, {});
}

void World::scheduleSupportUpdate(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_supportUpdates.push_back({{x, y, z}, std::chrono::steady_clock::now() + kFallDelay});
}

void World::scheduleLeafCheck(int x, int y, int z) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }
    m_leafChecks.push_back({{x, y, z}, std::chrono::steady_clock::now() + kLeafCheckDelay});
}

bool World::decayLeafIfOrphaned(const glm::ivec3& at) {
    const BlockId here = blockAt(at.x, at.y, at.z);
    if (!isLeafBlock(here)) {
        return false;
    }
    // minecraft.wiki [[Leaves]]: Bedrock's `persistent_bit`. A leaf someone
    // placed by hand never decays, however far it is from wood - which is what
    // makes a leaf a building material rather than a nuisance.
    if (stateBitAt(at.x, at.y, at.z)) {
        return false;
    }
    // **The residency gate belongs here, beside the rule, and not only on the
    // queue that feeds it.** Both callers reach this line - the queue, which is
    // gated and can defer, and `growOne`'s random tick, which is sampled
    // straight out of a resident chunk and has no gate at all. What follows
    // reads `farming::kLeafDecayReach` blocks in every direction through
    // `blockAt`, and `blockAt` answers `Air` for a chunk that is not resident:
    // a leaf whose trunk is in the next column along cannot see it, and this
    // function's whole job is to *destroy* what it decides is orphaned. Reach 4
    // against a 32-block column means roughly two cells in five are within
    // range of a boundary, so a canopy at the streaming frontier came apart on
    // its own. `CLAUDE.md` bug shape #1: the gate existed, was correct, and was
    // one column wide where the read is nine.
    //
    // A refusal is not a decision that the leaf lives - the queue re-asks and
    // the random tick comes round again - which is why it is safe for it to
    // share the "did not decay" answer.
    if (!columnsResidentAround(at.x, at.z, farming::kLeafDecayReach)) {
        return false;
    }
    if (leafHasWoodNearby(at)) {
        return false;
    }
    // **Out on the same channel a flow uses for a plant it sweeps aside**, so
    // the sapling, the sticks and the apple come off the one drop table rather
    // than off a second one written here.
    m_washedBlocks.push_back({at, here, Removal::Decay});
    // This write schedules the six cells around it, so the rest of the canopy
    // follows without anything here walking it.
    setBlock(at.x, at.y, at.z, BlockId::Air);
    return true;
}

void World::updateLeafDecay(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_leafChecks.empty() && !budgetSpent()) {
        if (m_leafChecks.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_leafChecks.front().position;
        m_leafChecks.pop_front();

        // A canopy straddling a chunk boundary must not be judged against a
        // neighbour that has not arrived, or half a tree's leaves vanish the
        // moment it streams in. Same gate the fall and support queues use -
        // **but as wide as the search**, because the wood scan reaches four
        // blocks into the columns around this one and those queues only ever
        // look straight down. Without the reach the entry was consumed here and
        // the decision taken on `Air`; with it the entry is deferred and
        // re-asked when the neighbour lands.
        if (!columnReadyOrDeferred(p, m_leafChecks, kLeafCheckDelay, farming::kLeafDecayReach)) {
            continue;
        }
        decayLeafIfOrphaned(p);
    }
}

void World::seedPendingColumns(const BudgetCheck& budgetSpent) {
    // **Always at least one, then as many as the frame can afford.** Without
    // the `first` term a frame whose budget was already spent upstream would
    // seed nothing, and a column that never seeds never lights - the queue
    // would stall permanently behind whatever else is busy. With it, progress
    // is guaranteed and the worst case is one column's ~98,000 cells, which is
    // what the old code paid per column anyway.
    bool first = true;
    while (!m_pendingColumnSeeds.empty() && (first || !budgetSpent())) {
        const ChunkCoord column = m_pendingColumnSeeds.front();
        m_pendingColumnSeeds.pop_front();
        first = false;
        // The player may have walked away between arrival and this frame, and
        // a partly-unloaded column cannot be traced from the top down.
        if (!columnLoaded(column.x, column.z)) {
            continue;
        }
        seedColumnLight(column.x, column.z);
    }
}

void World::rescanRestoredChunks(const BudgetCheck& budgetSpent) {
    while (!m_pendingRescan.empty() && !budgetSpent()) {
        const ChunkCoord coord = m_pendingRescan.front();
        m_pendingRescan.pop_front();
        // The player may have walked away again between arrival and this frame.
        if (!hasChunk(coord)) {
            continue;
        }
        rescanChunk(coord);
    }
}

void World::rescanChunk(const ChunkCoord& coord) {
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    // The chunk's own array rather than `blockAt`, so the sweep stays inside
    // one allocation: 32,768 hash lookups would cost more than the sweep.
    const Chunk& blocks = it->second.blocks;
    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    for (int y = 0; y < Chunk::kSize; ++y) {
        for (int z = 0; z < Chunk::kSize; ++z) {
            for (int x = 0; x < Chunk::kSize; ++x) {
                const BlockId here = blocks.at(x, y, z);
                if (here == BlockId::Air) {
                    continue;
                }
                const int wx = baseX + x;
                const int wy = baseY + y;
                const int wz = baseZ + z;

                // 1. **A leaf with air beside it.** Seeding only the exposed
                //    ones is enough because `decayLeafIfOrphaned` schedules the
                //    six cells around anything it removes, so one seed unravels
                //    a whole orphaned canopy and an interior leaf becomes
                //    exposed the moment its neighbour goes. A canopy that is
                //    *not* orphaned costs one wood scan per exposed leaf and
                //    then stops. A cell on the chunk face counts as exposed
                //    rather than being resolved against a neighbour that may
                //    not be resident - the queue re-asks the real question.
                if (isLeafBlock(here) && !blocks.stateBitAt(x, y, z)) {
                    const bool exposed =
                        (x == 0 || blocks.at(x - 1, y, z) == BlockId::Air) ||
                        (x == Chunk::kSize - 1 || blocks.at(x + 1, y, z) == BlockId::Air) ||
                        (y == 0 || blocks.at(x, y - 1, z) == BlockId::Air) ||
                        (y == Chunk::kSize - 1 || blocks.at(x, y + 1, z) == BlockId::Air) ||
                        (z == 0 || blocks.at(x, y, z - 1) == BlockId::Air) ||
                        (z == Chunk::kSize - 1 || blocks.at(x, y, z + 1) == BlockId::Air);
                    if (exposed) {
                        scheduleLeafCheck(wx, wy, wz);
                    }
                }

                // 2. **Anything standing on nothing.** The queue's position is
                //    the cell *below* the block in doubt - see `updateSupports`
                //    - so that is what goes in. The test here is the cheap
                //    in-chunk one and it is allowed to be wrong at the floor of
                //    the chunk, where the cell below lives in a neighbour that
                //    may not have arrived: `columnReadyOrDeferred` re-asks the
                //    real question when the queue drains, so a false positive
                //    costs one deferred check, and a false negative cannot
                //    happen because a missing neighbour reads as Air here.
                if (needsSupportBelow(here)) {
                    const BlockId below = y == 0 ? BlockId::Air : blocks.at(x, y - 1, z);
                    if (below == BlockId::Air || isFluid(below)) {
                        scheduleSupportUpdate(wx, wy - 1, wz);
                    }
                }

                // 3. **A flow that was still moving when the game closed.**
                //    Sources are deliberately not re-queued: an ocean is
                //    millions of them and every one is already at rest, while a
                //    *flowing* cell is by definition mid-spread and there are a
                //    handful per chunk. If the flow did in fact finish, the
                //    update runs once and changes nothing.
                if (isWater(here) && !isWaterSource(here)) {
                    scheduleFluidUpdate(wx, wy, wz);
                } else if (isLava(here) && !isLavaSource(here)) {
                    scheduleLavaUpdate(wx, wy, wz);
                }
            }
        }
    }
}

bool World::blockHasSupport(BlockId block, int x, int y, int z) const {
    const BlockId below = blockAt(x, y - 1, z);

    // A lily pad floats: the water under it is what holds it up where every
    // other block of its shape wants something solid.
    if (restsOnWater(block)) {
        return isWaterSource(below);
    }

    // **This asks whether the cell below is *empty*, not whether it is a valid
    // support, and the difference is the whole reason this function exists
    // rather than a call to `Main.cpp`'s `hasItsSupport`.**
    //
    // The obvious test is `isSolid(below)`, which is what the break path uses.
    // A probe over all 3,269 ids says why it cannot be used here: 803 ids
    // answer true to `needsSupportBelow` and **489 of those are not themselves
    // solid**, so `isSolid` says a sugar cane cannot stand on a sugar cane, a
    // kelp cannot stand on a kelp and a tall flower's head cannot stand on its
    // own stem. The break path gets away with it because it only ever runs on
    // the one cell above a block someone just mined; this runs on every write
    // in the game, so bone-mealing a sugar cane stack would have deleted the
    // top of it and a lilac would have lost its head to a passing water flow.
    //
    // Emptiness is the question the finding actually asks - "break the dirt
    // under a torch and it stays floating" - and it is one no per-family table
    // is needed to answer. It cannot destroy anything standing on any real
    // block, which is the right way to be wrong: a block left floating is a
    // blemish, a block deleted out of somebody's build is not.
    //
    // The strict rule needs `Main.cpp`'s per-family placement predicate, which
    // is not reachable from here. Filed as a finding.
    return !(below == BlockId::Air || isFluid(below));
}

bool World::dripstoneAnchored(int x, int y, int z) const {
    // Up to the top of the column, then ask what is holding it there. The walk
    // compares the id itself rather than asking `isSolid`, and **the reason is
    // the opposite of what an earlier draft of this comment claimed**, which is
    // why it is now a measured number rather than an assertion:
    // `isSolid(PointedDripstone)` is **false** (probed; `isSolid(Stone)` is
    // true in the same run, so the probe could still say yes). So a test that
    // led with `isSolid` would not call a dripstone anchored to itself - it
    // would call every cell of a real stalactite *un*anchored except the one
    // directly touching the ceiling, and drop four fifths of every column.
    //
    // Same code either way, and that is exactly what made the wrong version
    // dangerous: `CLAUDE.md` bug shape #16, a false comment beside right code.
    // A reader who checked the old claim would have found it false and had
    // every reason to delete the walk.
    int top = y;
    for (int i = 0;
         i < farming::kDripstoneMaxLength && blockAt(x, top + 1, z) == BlockId::PointedDripstone;
         ++i) {
        ++top;
    }
    if (game::isSolid(blockAt(x, top + 1, z))) {
        return true;
    }

    int bottom = y;
    for (int i = 0;
         i < farming::kDripstoneMaxLength && blockAt(x, bottom - 1, z) == BlockId::PointedDripstone;
         ++i) {
        --bottom;
    }
    return game::isSolid(blockAt(x, bottom - 1, z));
}

void World::updateSupports(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_supportUpdates.empty() && !budgetSpent()) {
        if (m_supportUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_supportUpdates.front().position;
        m_supportUpdates.pop_front();

        // The shared gate; see `columnReadyOrDeferred`. Without it a cell on
        // the edge of the loaded world reads Air out of a chunk that has not
        // arrived and everything standing on it is destroyed - which is the
        // same failure the fall queue was built to stop.
        if (!columnReadyOrDeferred(p, m_supportUpdates, kFallDelay)) {
            continue;
        }

        const glm::ivec3 above{p.x, p.y + 1, p.z};
        const BlockId resting = blockAt(above.x, above.y, above.z);

        // **Pointed dripstone is asked first, and the order is the whole
        // point.** It answers *false* to `needsSupportBelow` - correctly, it is
        // not held up from below - so placing this test after the early-out two
        // lines down would put it behind a `continue` it can never get past.
        // That is `CLAUDE.md` bug shape #2 exactly (a family predicate's
        // early-out silently swallowing a case added later), and it is worth a
        // comment because the natural place to add a new block is the bottom.
        //
        // **It detaches rather than washes**, which is the other difference
        // from everything below: the reference drops the whole unsupported run
        // as falling blocks that hurt what is under them, so this owes an
        // entity and not an item. `m_detachedBlocks` is the channel that
        // already means that, and `FallingBlock` owns the damage.
        //
        // The run comes down one block per pass with no loop here: this
        // `setBlock` schedules the cell above it, and it also schedules the cell
        // below it through the new rule in `setBlock`, so a column unravels
        // from the break in both directions.
        if (resting == BlockId::PointedDripstone) {
            if (!dripstoneAnchored(above.x, above.y, above.z)) {
                setBlock(above.x, above.y, above.z, BlockId::Air);
                m_detachedBlocks.push_back({above, BlockId::PointedDripstone});
            }
            continue;
        }

        if (!needsSupportBelow(resting)) {
            continue;
        }
        if (blockHasSupport(resting, above.x, above.y, above.z)) {
            continue;
        }

        // **The top half of a two-block plant is not a second plant, and this
        // was the seam it was being paid twice on.** Mine the dirt under a
        // lilac: the break path removes the lower half and pays a lilac, this
        // queue then finds the upper half standing on air and paid a second
        // one out of a plant that cost one - which is the item-conservation
        // bug the `Main.cpp` owner is on the other side of.
        //
        // **It cannot lose a drop, and the reason is worth stating rather than
        // trusting.** The only support an upper half has *is* its lower half,
        // so it can reach this line for exactly one reason: the lower half has
        // already gone. Whoever removed that - a break, a flow, a blast, this
        // very queue one pass earlier - has already paid the plant. There is no
        // path on which the upper half is the first half to fall.
        //
        // So: removed, silently. `Main`'s `clearPairedHalf` stays the one owner
        // of "which cell is the other half"; this is only the rule that the
        // orphan owes nothing, and it is here rather than there because this is
        // the one path that reaches a half without going through a break.
        if (isTallFlowerUpper(resting)) {
            setBlock(above.x, above.y, above.z, BlockId::Air);
            continue;
        }

        // **Through the drop channel, not straight to Air.** A torch, a flower,
        // a crop or a rail losing its floor owes its item exactly as a mined
        // one does, and `m_washedBlocks` is where `Main` already turns a
        // removed block into drops through the table - which is what makes a
        // stack of four candles pay four candles rather than one.
        m_washedBlocks.push_back({above, resting, Removal::Support});
        // Recursion comes free and cannot run away: this write schedules the
        // cell above *it*, so a stack of sugar cane or a column of bamboo comes
        // down one block per pass with no loop here and no C++ stack depth. The
        // walk only ever goes up, and the world has a ceiling.
        setBlock(above.x, above.y, above.z, BlockId::Air);
    }
}

namespace {

/// How far the reference looks for a way down before giving up: four blocks.
constexpr int kSlopeSearch = 4;

/// What an unreachable drop scores. The reference's own sentinel.
constexpr int kNoSlope = 1000;

/// Whether a flow standing on this can pool on it rather than fall through it.
///
/// **`isSolid` alone is not this question, and that gap was a live bug.**
/// `isSolid` is a *movement* test and answers true for every `BlockShape::Flat`
/// block - a rail, a carpet, moss carpet, redstone dust, a snow layer - while
/// `fluid::fluidMayOccupy` (the drain question, in `Fluid.hpp`) answers true
/// for that same set because the reference washes all of them away. So a carpet
/// under a stream was simultaneously somewhere the water drained *to* and solid
/// ground it pooled *on*, depending on which of the three fluid predicates
/// asked. Subtracting the drain set is what makes support and drainage one
/// answer instead of two.
///
/// A lily pad and a sculk vein are the reason this is not simply
/// `blockShape != Flat`: both are flat, neither is washed away, and both dam a
/// flow like any wall.
constexpr bool fluidRestsOn(BlockId id) {
    return game::isSolid(id) && !fluid::fluidMayOccupy(id);
}

// The whole point of the predicate: nothing may be support and drain at once.
// The single edit that fails this: dropping the `&& !fluidMayOccupy` term above
// and going back to a bare `isSolid`, which is what `fluidFeedsSideways` asked
// for twenty milestones.
static_assert(!fluidRestsOn(BlockId::CarpetRunFirst) && !fluidRestsOn(railRunFirst(0)) &&
                  !fluidRestsOn(BlockId::MossCarpet) &&
                  !fluidRestsOn(BlockId::RedstoneWireFirst) && !fluidRestsOn(snowLayerAt(3)),
              "a flow cannot pool on something the same flow destroys");
// And the counterweight, because widening the subtraction is the obvious next
// mistake. The single edit that fails this: writing `fluidMayOccupy` as
// `blockShape(id) == BlockShape::Flat`, which deletes every lily pad's footing.
static_assert(fluidRestsOn(BlockId::Stone) && fluidRestsOn(BlockId::LilyPad) &&
                  fluidRestsOn(BlockId::SculkVein),
              "the two flat blocks the reference keeps must still hold water up");

/// How far a sponge reaches, as a taxicab distance, and how much it can hold.
///
/// minecraft.wiki [[Sponge]], verbatim: "A sponge absorbs both flowing and
/// source blocks of water up to 6 blocks away (taken as a taxicab distance) in
/// all six directions around itself ... A sponge does not absorb more than 118
/// blocks of water however, and water closest to the sponge is absorbed first.
/// The absorption propagates only between adjacent water blocks and does not
/// 'jump over' non-water blocks, including air."
constexpr int kSpongeReach = 6;
constexpr int kSpongeCapacity = 118;

/// Drains the water a sponge can reach and answers whether it took any, which
/// is the same question as "is this sponge now wet".
///
/// Breadth-first from the sponge, because "closest first" is what decides which
/// 118 cells a big pool loses; and every step is between two water cells, which
/// is the no-jumping-over rule. The distance is measured from the sponge rather
/// than counted along the path - a walk can only ever be longer than the
/// straight-line taxicab distance, so the shorter measure is the reference's.
bool absorbWaterAround(World& world, const glm::ivec3& sponge) {
    constexpr int span = 2 * kSpongeReach + 1;
    std::array<bool, static_cast<std::size_t>(span) * span * span> seen{};
    const auto index = [&sponge](const glm::ivec3& cell) {
        const glm::ivec3 offset = cell - sponge + glm::ivec3{kSpongeReach};
        return static_cast<std::size_t>((offset.z * span + offset.y) * span + offset.x);
    };

    std::vector<glm::ivec3> found;
    found.reserve(static_cast<std::size_t>(kSpongeCapacity) + kLightSteps.size());
    // The sponge seeds the walk without being one of the results, which is why
    // the collection below starts at one.
    found.push_back(sponge);
    seen[index(sponge)] = true;

    for (std::size_t head = 0; head < found.size(); ++head) {
        const glm::ivec3 cell = found[head];
        for (const glm::ivec3& step : kLightSteps) {
            if (found.size() > static_cast<std::size_t>(kSpongeCapacity)) {
                break;
            }
            const glm::ivec3 n = cell + step;
            const glm::ivec3 offset = n - sponge;
            if (std::abs(offset.x) + std::abs(offset.y) + std::abs(offset.z) > kSpongeReach) {
                continue;
            }
            if (seen[index(n)]) {
                continue;
            }
            seen[index(n)] = true;
            if (!isWater(world.blockAt(n.x, n.y, n.z))) {
                continue;
            }
            found.push_back(n);
        }
    }

    for (std::size_t i = 1; i < found.size(); ++i) {
        world.setBlock(found[i].x, found[i].y, found[i].z, BlockId::Air);
    }
    return found.size() > 1;
}

/// A source of this kind. The one exception to "a cell standing on fluid is
/// partway down a column": a source spreads across its own fluid, which is how
/// a lake and a lava lake have a surface at all.
///
/// The "is this block that fluid" half of the pair is `fluid::isKind` and is
/// deliberately not restated here.
constexpr bool isOwnSource(fluid::FluidKind kind, BlockId id) {
    return kind == fluid::FluidKind::Water ? isWaterSource(id) : isLavaSource(id);
}

// Both halves have to agree about which fluid is which, and they are written in
// two different headers. The single edit that fails this: swapping the arms of
// either ternary, which would make lava pool on water and water pool on lava.
static_assert(fluid::isKind(BlockId::Water0, fluid::FluidKind::Water) &&
                  isOwnSource(fluid::FluidKind::Water, BlockId::Water0) &&
                  !isOwnSource(fluid::FluidKind::Lava, BlockId::Water0),
              "water's source must read as water to both halves");
static_assert(fluid::isKind(BlockId::Lava0, fluid::FluidKind::Lava) &&
                  isOwnSource(fluid::FluidKind::Lava, BlockId::Lava0) &&
                  !isOwnSource(fluid::FluidKind::Water, BlockId::Lava0),
              "lava's source must read as lava to both halves");

} // namespace

std::optional<BlockId> World::blockIfLoaded(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize),
                           floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return std::nullopt;
    }
    return chunk->at(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

bool World::fluidCanEnter(int x, int y, int z, FluidKind kind) const {
    // **Not `blockAt`.** The slope search below walks up to four blocks out,
    // which at the streaming frontier is into chunks that do not exist yet -
    // and those read as air, which is precisely the shape of a hole. Water then
    // scores the void as the nearest way down and every stream near the edge of
    // the loaded world turns toward it. A cell with no chunk is not somewhere
    // water can go: `setBlock` would refuse it anyway.
    const std::optional<BlockId> here = blockIfLoaded(x, y, z);
    if (!here.has_value()) {
        return false;
    }
    // A plant does not dam a stream, it is swept away by it - so the search has
    // to path straight through one, or a meadow full of grass turns every flow
    // into a maze. **One predicate, not a hand-written set**: `fluidMayOccupy`
    // is the same question `flowVector` and `fluidCanDrainFrom` ask, and the
    // three spelling it differently is how a carpet ended up draining and
    // damming at once. The fluid's own blocks are added back here and nowhere
    // else, because a cell already holding water is somewhere water can *be*
    // but not somewhere it can *fall to*.
    //
    // **The other fluid is a wall, not a path**, and that asymmetry is why this
    // takes a kind rather than testing `isFluid`. minecraft.wiki [[Fluid]]:
    // lava meeting water turns to stone or obsidian, so a pond is not somewhere
    // a lava flow is looking to reach - it is where the flow ends.
    return fluid::fluidMayOccupy(*here) || fluid::isKind(*here, kind);
}

bool World::fluidCanDrainFrom(int x, int y, int z) const {
    // Somewhere below that could *receive* water. Water already there does not
    // count, and that is the whole point: once a hole has filled it stops being
    // a hole, the weight search stops steering everything into it, and the flow
    // spreads on past. Counting any water below as a drop dead-ended every
    // stream at the first dip it found.
    //
    // A cell with no chunk under it is not a drop either, for the same reason
    // as in `fluidCanEnter`: it reads as air and is not.
    const std::optional<BlockId> below = blockIfLoaded(x, y - 1, z);
    return below.has_value() && fluid::fluidMayOccupy(*below);
}

bool World::fluidFeedsSideways(int x, int y, int z, FluidKind kind) const {
    // Solid ground underfoot is what makes a flow pool. Anything that can still
    // go down goes down instead - which is what keeps a waterfall one block
    // wide - and a cell resting on more fluid is partway down a column, not the
    // bottom of one. A source is the exception: it spreads across its own
    // fluid, which is how a lake has a surface at all.
    //
    // **`fluidRestsOn`, not `isSolid`.** See its definition: `isSolid` calls a
    // carpet ground while `fluidCanDrainFrom` calls the same carpet a hole, so
    // the bare test made a stream pool on and pour through one block.
    //
    // **Lava had this rule inlined into `updateLava` rather than calling it**,
    // and the copy had already drifted once. One body, two callers.
    const BlockId below = blockAt(x, y - 1, z);
    return fluidRestsOn(below) ||
           (isOwnSource(kind, blockAt(x, y, z)) && fluid::isKind(below, kind));
}

int World::slopeDistance(int x, int z, int y, int fromDirection, FluidKind kind) const {
    // Breadth-first, so the first hole found is genuinely the nearest. Depth is
    // capped at four, which is the reference's `slopeFindDistance` and is what
    // makes water seek a hole it can nearly reach and ignore one it cannot.
    struct Node {
        int x;
        int z;
        int depth;
        int from;
    };

    std::array<Node, 4 * kSlopeSearch * kSlopeSearch + 4> queue{};
    std::size_t head = 0;
    std::size_t tail = 0;
    queue[tail++] = {x, z, 1, fromDirection};

    while (head < tail) {
        const Node node = queue[head++];
        if (!fluidCanEnter(node.x, y, node.z, kind)) {
            continue;
        }
        if (fluidCanDrainFrom(node.x, y, node.z)) {
            return node.depth;
        }
        if (node.depth >= kSlopeSearch) {
            continue;
        }
        for (std::size_t i = 0; i < kFlowSteps.size(); ++i) {
            // Never turn straight back the way we came; the reference does the
            // same, and without it the search wastes most of its budget
            // re-examining the cell it just left.
            if (static_cast<int>(i ^ 1u) == node.from) {
                continue;
            }
            if (tail >= queue.size()) {
                break;
            }
            queue[tail++] = {node.x + kFlowSteps[i].x, node.z + kFlowSteps[i].z, node.depth + 1,
                             static_cast<int>(i)};
        }
    }
    return kNoSlope;
}

bool World::fluidSpreadsToward(const glm::ivec3& from, std::size_t direction,
                               FluidKind kind) const {
    // Every direction starts at 1000 and is replaced by the distance to the
    // nearest reachable drop. The flow then runs **only** the lowest-scoring
    // ways, which is what sends a stream one block wide at a cliff edge instead
    // of fanning into a diamond. With no hole in range every direction ties at
    // 1000, and a flat floor gets the even spread it should.
    //
    // **Lava calls this too now.** minecraft.wiki [[Lava]]: lava "flows more
    // slowly and does not spread as far" - three blocks in the overworld
    // against water's seven - but the *seeking* is the same rule, the same
    // `slopeFindDistance` of 4. It differs in reach and speed, not in whether
    // it looks for a hole, and ours did not look.
    int best = kNoSlope;
    // Length derived from the table that fills it, not written as 4. The loop
    // below runs to `kFlowSteps.size()` and `direction` indexes the same space,
    // so a hand-written length would be a second answer to "how many ways can
    // a fluid go?" - and adding a step to `kFlowSteps` would write past the end
    // with no diagnostic, because `std::array::operator[]` is unchecked.
    std::array<int, kFlowSteps.size()> weights{};
    for (std::size_t i = 0; i < kFlowSteps.size(); ++i) {
        const glm::ivec3 step = from + kFlowSteps[i];
        weights[i] = fluidCanEnter(step.x, step.y, step.z, kind)
                         ? slopeDistance(step.x, step.z, step.y, static_cast<int>(i), kind)
                         : kNoSlope;
        best = std::min(best, weights[i]);
    }
    return weights[direction] == best;
}

void World::updateFluids(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_fluidUpdates.empty() && !budgetSpent()) {
        // Every entry waits the same delay, so the queue is already in due
        // order and the front one not being ready means none of them is.
        if (m_fluidUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_fluidUpdates.front().position;
        m_fluidUpdates.pop_front();

        // Before any of it is decided; see `columnReadyOrDeferred`. This queue
        // is the one that suffers most from a missing chunk, because the slope
        // search *steers* on what it reads: an absent column scores as a hole
        // four blocks wide and the stream aims itself at the frontier.
        if (!columnReadyOrDeferred(p, m_fluidUpdates, kFluidSpreadDelay)) {
            continue;
        }

        const BlockId current = blockAt(p.x, p.y, p.z);
        // Concrete powder sets the instant water touches it, on any of the six
        // sides. It is answered before the "is this the fluid system's
        // business" test below, because a powder is neither air nor water and
        // would otherwise fall straight through.
        if (isConcretePowder(current)) {
            for (const glm::ivec3& step : kLightSteps) {
                if (isWater(blockAt(p.x + step.x, p.y + step.y, p.z + step.z))) {
                    setBlock(p.x, p.y, p.z, concreteFor(current));
                    break;
                }
            }
            continue;
        }
        // A sponge takes the water around it the moment water touches it, which
        // is what makes it a tool rather than a yellow cube. Answered here for
        // the same reason concrete powder is - a sponge is neither air nor
        // water - and **before the flow is recomputed**, because a spread
        // update that ran first would simply refill what was about to be taken.
        // `setBlock` queues this cell when the sponge is placed and again
        // whenever any of its six neighbours changes, so both halves of the
        // reference's rule - placed next to water, and water arriving at it -
        // come out of the one test.
        if (current == BlockId::Sponge) {
            if (absorbWaterAround(*this, p)) {
                setBlock(p.x, p.y, p.z, BlockId::WetSponge);
            }
            continue;
        }
        // Coral left out of water dies. **A scheduled test, not a random tick**:
        // the reference kills coral with `randomTickSpeed` at 0 (MC-129942,
        // resolved Invalid), so it belongs on this queue, which `setBlock`
        // already pokes for every neighbour of every edit. minecraft.wiki
        // [[Coral Block]] and [[Coral]]: coral turns into its dead variant when
        // none of the six adjacent blocks is water or waterlogged. Our delay is
        // this queue's five ticks against the reference's 45 for a block, which
        // is the one number that differs and is not worth a queue of its own.
        if (deadCoralFor(current) != current) {
            bool wet = waterloggedAt(p.x, p.y, p.z);
            for (const glm::ivec3& step : kLightSteps) {
                if (wet) {
                    break;
                }
                const glm::ivec3 n = p + step;
                wet = isWater(blockAt(n.x, n.y, n.z)) || waterloggedAt(n.x, n.y, n.z);
            }
            if (!wet) {
                setBlock(p.x, p.y, p.z, deadCoralFor(current));
            }
            continue;
        }
        // Only air, water and things a flow sweeps aside are the fluid system's
        // business. Testing for "not solid" was the same thing while every block
        // was a full cube or water, but a slab is neither - and falling through
        // here rewrites it to air.
        if (current != BlockId::Air && !isWater(current) && !isWashedAway(current)) {
            continue;
        }
        // **A waterlogged plant is sharing the cell with the water, not standing
        // in its way.** `canWaterlog` is `blockShape == Cross` and `isWashedAway`
        // starts with the same test, so every kelp strand, seagrass patch and
        // sea pickle the generator waterlogged passed the gate above, computed a
        // `wanted` of water, and was pushed out as a dropped item by the branch
        // at the bottom of this loop - which `setBlock` then triggered again on
        // all six neighbours, so breaking one block stripped a whole seabed.
        // minecraft.wiki [[Waterlogging]]: a waterlogged block coexists with the
        // water and is not washed away by it.
        if (waterloggedAt(p.x, p.y, p.z)) {
            continue;
        }
        // Sources are the fixed points of the whole system. Without something
        // that never drains, every body of water eventually empties itself.
        if (isWaterSource(current)) {
            continue;
        }

        int supply = kMaxWaterLevel + 1;
        int adjacentSources = 0;
        for (const glm::ivec3& step : kFlowSteps) {
            if (isWaterSource(blockAt(p.x + step.x, p.y, p.z + step.z))) {
                ++adjacentSources;
            }
        }

        // **The block above is answered before any neighbour**, because a cell
        // fed from overhead is *falling*: full, and on its way down. Ours used
        // to record it as merely "nearly full", which let a column poured off a
        // tower fan out sideways at every level it passed.
        const bool fedFromAbove = isWater(blockAt(p.x, p.y + 1, p.z));

        // **The slope only decides which empty cells a flow spreads into; it
        // never re-decides a cell that already holds water.** The reference
        // keeps these apart - its `getNewLiquid` recomputes a level from the
        // neighbours with no slope test in it at all, and the weights are
        // consulted only when spreading. Folding the two together meant a
        // settled cell could be starved by a weight that changed *because of
        // its own outflow*: water reaches an edge, the column below it fills,
        // that direction stops scoring as a drop, a rival direction wins, the
        // cell empties, the column drains, the weight flips back. Air, water,
        // air, water, once per tick, for ever - which is what "the water near
        // the edge goes mad" was.
        const bool arriving = !isWater(current);

        if (!fedFromAbove) {
            for (std::size_t i = 0; i < kFlowSteps.size(); ++i) {
                const glm::ivec3 n = p + kFlowSteps[i];
                const BlockId neighbour = blockAt(n.x, n.y, n.z);
                if (!isWater(neighbour)) {
                    continue;
                }

                // **A neighbour that can still go down does not run sideways at
                // all.** This one rule is what makes a waterfall a column: every
                // cell in mid-air has somewhere to drop, so none of them feeds
                // anything to the side, and only the cell that finally lands on
                // solid ground has nowhere left to go and pools.
                if (!fluidFeedsSideways(n.x, n.y, n.z, FluidKind::Water)) {
                    continue;
                }

                const int level = waterLevel(neighbour);
                if (level >= kMaxWaterLevel) {
                    continue;
                }
                // A cell that can itself drain is always worth flowing into -
                // nothing can score better than a drop one step away - so the
                // search is skipped rather than run to reach the same answer.
                if (arriving && !fluidCanDrainFrom(p.x, p.y, p.z) &&
                    !fluidSpreadsToward(n, i ^ 1u, FluidKind::Water)) {
                    continue;
                }
                // Competing flows resolve to whichever supply is strongest.
                supply = std::min(supply, level + 1);
            }
        }

        BlockId wanted = BlockId::Air;
        // **Bedrock counts a source directly overhead as one of the two.**
        // minecraft.wiki [[Fluid]] lists it first among Bedrock's differences -
        // "Having two sources one diagonally above the other also results in a
        // new source" - and [[Water]] spells the same rule out from the flowing
        // cell's side: a flowing block "adjacent to one source block
        // horizontally and one vertically above" becomes a source. Ours had
        // only Java's four-horizontal rule, so the standard two-bucket well did
        // not work.
        const int convertingSources =
            adjacentSources + (isWaterSource(blockAt(p.x, p.y + 1, p.z)) ? 1 : 0);
        const BlockId below = blockAt(p.x, p.y - 1, p.z);
        // **`fluidRestsOn`, not `isSolid`** - the third site of the same
        // mistake. The reference wants "a block that liquids cannot flow into
        // below itself" (minecraft.wiki [[Fluid]]), and `isSolid` is true for
        // every `BlockShape::Flat` block, so two sources laid either side of a
        // carpet, a rail, a redstone line or a snow layer made a permanent
        // source standing on a block the same water was draining through.
        if (convertingSources >= 2 && (fluidRestsOn(below) || isWaterSource(below))) {
            // Two sources meeting over solid ground - or over another source -
            // fill the gap permanently.
            wanted = BlockId::Water0;
        } else if (fedFromAbove) {
            wanted = BlockId::WaterFalling;
        } else if (supply <= kMaxWaterLevel) {
            wanted = waterAtLevel(supply);
        }

        if (wanted == current) {
            continue;
        }
        // Water only *arrives* in a plant's cell; it never leaves air behind in
        // one. Without this an update that decided on nothing would quietly mow
        // the lawn.
        if (isWashedAway(current)) {
            if (wanted == BlockId::Air) {
                continue;
            }
            // **Fire goes on the other channel.** It is washed away by the same
            // rule as a plant - `blockShape(Fire)` is `Cross`, which is what
            // gives it no collision and no support - but it is not an item and
            // never was, so handing it to a drop table asks a question with no
            // right answer and relies on that table's `default:` to say
            // nothing. What a flow drowning a fire actually produces is a
            // hiss, which is the same event rain produces in `updateFire`.
            if (current == BlockId::Fire) {
                recordFizz(p, wanted);
            } else {
                m_washedBlocks.push_back({p, current, Removal::Flow});
            }
        }
        setBlock(p.x, p.y, p.z, wanted);
    }
}

bool World::coolLava(const glm::ivec3& p, BlockId lava) {
    // The reference's mixing rules 1 and 3. **Which block wins is decided by
    // source-versus-flowing, not by where the water is**: a source touched
    // anywhere above or beside becomes obsidian, and anything flowing becomes
    // cobblestone. Water *below* is excluded from both - that case is lava
    // falling in, and it is the water that changes, not the lava.
    bool touchingWater = isWater(blockAt(p.x, p.y + 1, p.z));
    for (const glm::ivec3& step : kFlowSteps) {
        if (touchingWater) {
            break;
        }
        touchingWater = isWater(blockAt(p.x + step.x, p.y, p.z + step.z));
    }
    if (!touchingWater) {
        return false;
    }
    // A source sets to obsidian and a flow sets to cobblestone. **Both are the
    // reference's, with no roll between them** - minecraft.wiki [[Fluid]],
    // mixing rule 1: "If flowing lava contacts a water block (source or
    // flowing) in any direction except downward, the lava turns into
    // cobblestone", and a source touched by water becomes obsidian.
    //
    // A share of flowing lava used to come out as obsidian instead, on the
    // reasoning that always-cobblestone made a lava-and-water meeting worth
    // nothing. It is not worth nothing - it is a cobblestone generator, which is
    // exactly what the reference's is - and obsidian is still renewable from the
    // source case above, which is how every obsidian farm in the reference
    // works. **An invented number is worse than a boring rule**: nothing else in
    // this file could have told a reader that 20% was a choice rather than a
    // mis-port.
    //
    // **Each of the two mixing outcomes reports itself.** This is the general
    // case of the hiss and much the commoner one: two flows meeting somewhere
    // nobody is standing. `World` cannot make the noise - it has no visibility
    // of the sound banks and is not going to be given any - so it says what
    // happened and where, and the frame loop decides. See `takeFizzes`.
    const BlockId set = isLavaSource(lava) ? BlockId::Obsidian : BlockId::Cobblestone;
    setBlock(p.x, p.y, p.z, set);
    recordFizz(p, set);
    return true;
}

void World::updateLava(const BudgetCheck& budgetSpent) {
    const auto now = std::chrono::steady_clock::now();
    while (!m_lavaUpdates.empty() && !budgetSpent()) {
        if (m_lavaUpdates.front().due > now) {
            break;
        }
        const glm::ivec3 p = m_lavaUpdates.front().position;
        m_lavaUpdates.pop_front();

        // Same gate as water's, for the same reason; see
        // `columnReadyOrDeferred`.
        if (!columnReadyOrDeferred(p, m_lavaUpdates, kLavaSpreadDelay)) {
            continue;
        }

        const BlockId current = blockAt(p.x, p.y, p.z);

        if (isLava(current) && coolLava(p, current)) {
            continue;
        }

        // Lava sets light to what is near it. Bedrock's rule is the simple one:
        // the fuel must sit inside the 3x3x3 centred on the lava and have air
        // above it, and the fire appears in that air cell.
        //
        // **A settled lava cell would otherwise never tick again**, so this is
        // also the one place lava reschedules itself - and only while there is
        // something nearby worth burning, which is what keeps a lava lake in
        // open stone from queueing work for ever.
        if (isLava(current)) {
            bool fuelNearby = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const glm::ivec3 n{p.x + dx, p.y + dy, p.z + dz};
                        if (!isFlammable(blockAt(n.x, n.y, n.z))) {
                            continue;
                        }
                        fuelNearby = true;
                        if (blockAt(n.x, n.y + 1, n.z) == BlockId::Air) {
                            setBlock(n.x, n.y + 1, n.z, BlockId::Fire);
                        }
                    }
                }
            }
            if (fuelNearby) {
                scheduleLavaUpdate(p.x, p.y, p.z);
            }
        }

        // Rule 2, and the only case where the *water* is what changes: lava
        // arriving from directly overhead sets the water it lands in. The third
        // mixing outcome and the third hiss; see `coolLava` for why it is
        // reported rather than played.
        if (isWater(current) && isLava(blockAt(p.x, p.y + 1, p.z))) {
            setBlock(p.x, p.y, p.z, BlockId::Stone);
            recordFizz(p, BlockId::Stone);
            continue;
        }

        if (current != BlockId::Air && !isLava(current) && !isWashedAway(current)) {
            continue;
        }
        // A source never drains. Unlike water, nothing here ever *creates* one:
        // the reference does not let lava sources self-generate, so there is no
        // two-neighbours rule to mirror.
        if (isLavaSource(current)) {
            continue;
        }

        const bool fedFromAbove = isLava(blockAt(p.x, p.y + 1, p.z));
        // Same rule as water's, and see the long note there for why the slope
        // may only decide where a flow *spreads to* and never re-decide a cell
        // that already holds fluid: folding the two together makes a settled
        // cell oscillate once per tick against its own outflow.
        const bool arriving = !isLava(current);
        int supply = kMaxLavaLevel + 1;
        if (!fedFromAbove) {
            for (std::size_t i = 0; i < kFlowSteps.size(); ++i) {
                const glm::ivec3 n = p + kFlowSteps[i];
                const BlockId neighbour = blockAt(n.x, n.y, n.z);
                if (!isLava(neighbour)) {
                    continue;
                }
                // A neighbour with somewhere to fall does not also run sideways,
                // which is what keeps a lava fall a column.
                //
                // **A cell resting on more lava is partway down that column,
                // not the bottom of one**, and leaving that out is what the
                // user reported as lava stacking on top of itself: every cell
                // of a fall counted as pooling, so it shelved out sideways at
                // every height it passed and the drop grew a wall instead of a
                // stream. A source is the exception, because that is how a lava
                // lake has a surface at all.
                //
                // **This used to be that rule written out again here rather
                // than called**, which is `CLAUDE.md` bug shape #14 and had
                // already cost this pair of fluids twice. It is now literally
                // water's function with `FluidKind::Lava` passed in, so the two
                // cannot drift: `fluidRestsOn` rather than `isSolid` (lava
                // shelved out across a carpet it was destroying) and no water
                // clause (a pond's surface is not a floor for lava - [[Fluid]]
                // mixing rule 2 has it fall *into* the water and make stone).
                if (!fluidFeedsSideways(n.x, n.y, n.z, FluidKind::Lava)) {
                    continue;
                }
                const int level = lavaLevel(neighbour);
                if (level + kLavaSpreadStep > kMaxLavaLevel) {
                    continue;
                }
                // **The downhill search, which lava never ran.**
                // minecraft.wiki [[Lava]]: "Like water, lava attempts to form a
                // stream to the nearest downward slope within a 4-block
                // radius". Ours called `slopeDistance` from the water path
                // alone, so lava poured onto a ledge fanned out evenly in all
                // four directions and walked straight past a hole one block to
                // the side. Same skip as water's: a cell that can already drain
                // beats anything the search could find.
                if (arriving && !fluidCanDrainFrom(p.x, p.y, p.z) &&
                    !fluidSpreadsToward(n, i ^ 1u, FluidKind::Lava)) {
                    continue;
                }
                supply = std::min(supply, level + kLavaSpreadStep);
            }
        }

        BlockId wanted = BlockId::Air;
        if (fedFromAbove) {
            wanted = BlockId::LavaFalling;
        } else if (supply <= kMaxLavaLevel) {
            wanted = lavaAtLevel(supply);
        }

        if (wanted == current) {
            continue;
        }
        // Flowing lava destroys what it sweeps aside rather than dropping it,
        // which is the one place it differs from water.
        if (isWashedAway(current) && wanted == BlockId::Air) {
            continue;
        }
        setBlock(p.x, p.y, p.z, wanted);
    }
}

std::vector<World::WashedBlock> World::takeWashedBlocks() {
    return std::exchange(m_washedBlocks, {});
}

std::vector<World::FizzEvent> World::takeFizzes() {
    // `exchange`, not "copy then clear", which is what the other three channels
    // do and for the same reason: it hands the buffer over and leaves an empty
    // one behind in a single step, so there is no window in which an event is
    // both handed out and still queued. Every producer runs on this thread
    // between drains, so nothing can arrive during the swap.
    return std::exchange(m_fizzes, {});
}

void World::recordFizz(const glm::ivec3& p, BlockId became) {
    if (m_fizzes.size() >= kMaxPendingFizzes) {
        return;
    }
    m_fizzes.push_back({p, became});
}

// **This 64 is not the bound the save file will accept, and the gap loses the
// player's setting silently.** `Settings::kMaxRenderDistance` is 32, and
// `parseUnsigned` in `core/Settings.cpp` *rejects* rather than clamps - it
// returns false on `value > limit`. So a radius walked past 32 here is written
// to disk by the same keypress that moved it, refused on the next load, and the
// field falls back to its struct default of 12. Raise it to 40, quit, come
// back: 12. Not 40, not 32.
//
// **The clamp is deliberately not derived from that constant, and that is a
// layering fact rather than an oversight.** No file under `world/` has ever
// included `core/` - 0 of 60 on 2026-08-19, against 20 includes of `item/` from
// 13 of those same files, so the boundary is observed rather than accidental.
// Pulling settings in here to fix an off-by-32 would be the first such edge,
// and it would put a user-preference bound inside the layer that is supposed to
// supply mechanism. The bound that protects the player therefore belongs where
// both halves are already visible: the F7/F8 site in `Main.cpp`, which includes
// `core/Settings.hpp` today - search `setVisibleRadius` there. **Check whether
// that clamp is already present before adding one**; a second independently
// authored bound is the same defect again, one layer up.
//
// What would make this note false: `Settings::kMaxRenderDistance` changing to
// 64, `parseUnsigned` learning to clamp, or `world/` gaining a `core/` include.
// Search `kMaxRenderDistance` - it is the only name on the other side.
void World::setVisibleRadius(int chunks) {
    const int radius = std::clamp(chunks, 1, 64);
    if (radius == m_visibleRadius) {
        return;
    }

    const int previous = m_visibleRadius;
    m_visibleRadius = radius;
    m_loadRadius = m_visibleRadius + 1;
    m_unloadRadius = m_loadRadius + 2;

    // Chunks are kept loaded a few rings past the visible radius so pacing back
    // and forth does not thrash them. That band would otherwise stay on screen
    // after shrinking, making the change look like it did nothing.
    //
    // **`|=`, not `=`.** The flag means "there is a drop pass owing", and only
    // the drop pass may clear it - assigning here let a second call arriving
    // before the next `update` cancel the first one's debt. Dragging the render
    // distance slider is exactly that: 8 to 4 to 6 in one frame ends with the
    // flag false and the meshes for rings 7 and 8 still on screen, outside the
    // visible radius, with nothing left that will ever drop them. Same shape as
    // a save that clears the dirty flag before knowing the write landed.
    m_radiusShrunk = m_radiusShrunk || radius < previous;

    // Forces the next update to run its recentre path, which is what unloads
    // what no longer fits and re-queues what is newly wanted.
    m_hasCentre = false;
}

void World::setDetailRadius(int chunks) {
    const int radius = std::max(1, chunks);
    if (radius == m_detailRadius) {
        return;
    }
    m_detailRadius = radius;

    // Every chunk has to be re-asked, and the recentre path is what does that.
    m_hasCentre = false;
}

MeshDetail World::detailFor(const ChunkCoord& coord, bool currentlyDetailed) const {
    // At or beyond the render distance the tier is off, and every chunk that is
    // drawn at all is drawn whole. Written as a comparison rather than a
    // separate flag so there is one number to reason about.
    if (m_detailRadius >= m_visibleRadius) {
        return MeshDetail::Full;
    }
    const int distance = chebyshevDistance(coord, m_centre);
    const int limit = m_detailRadius + (currentlyDetailed ? kDetailHysteresisChunks : 0);
    return distance <= limit ? MeshDetail::Full : MeshDetail::TerrainOnly;
}

void World::refreshDetail(const ChunkCoord& centre) {
    // Deliberately not short-circuited when the tier is off: turning it off is
    // exactly the case where chunks built terrain-only have to be told to put
    // their decoration back, and `detailFor` already answers `Full` for every
    // chunk in that state.
    for (auto& [coord, slot] : m_chunks) {
        // Only what is drawable can be looked at, and a chunk that comes back
        // into range is re-asked when it is next dispatched.
        if (chebyshevDistance(coord, centre) > m_visibleRadius) {
            continue;
        }
        const bool wanted = detailFor(coord, slot.detailWanted) == MeshDetail::Full;
        if (wanted == slot.detailWanted) {
            continue;
        }
        // A tier crossing genuinely changes what the chunk looks like, so it
        // goes through the same door an edit does: bumping the revision throws
        // away any job already building the old tier.
        slot.detailWanted = wanted;
        invalidateMesh(coord);
    }
}

std::size_t World::detailedChunkCount() const {
    std::size_t count = 0;
    for (const auto& [coord, slot] : m_chunks) {
        if (slot.meshed && slot.detailBuilt) {
            ++count;
        }
    }
    return count;
}

World::DetailLag World::detailLag() const {    DetailLag lag;
    for (const auto& [coord, slot] : m_chunks) {
        const int distance = chebyshevDistance(coord, m_centre);
        if (distance > m_visibleRadius) {
            continue;
        }
        if (slot.meshed && slot.detailBuilt != slot.detailWanted) {
            ++lag.chunks;
            lag.nearest = lag.nearest < 0 ? distance : std::min(lag.nearest, distance);
        }
    }
    return lag;
}

void World::saveIfModified(const ChunkCoord& coord, ChunkSlot& slot) {
    if (!slot.modified) {
        return;
    }
    // **The flag is cleared only if the write landed.** `save` returns false for
    // a full disk, a rename blocked by antivirus, a read-only save folder - and
    // clearing regardless meant the chunk was never retried and was dropped
    // outright the moment it unloaded. Left modified, it is offered again on the
    // next unload pass and again at `saveAll`, which is the whole of the retry
    // this needs.
    if (!m_store->save(coord, slot.blocks)) {
        return;
    }
    slot.modified = false;
    ++m_savedChunkCount;
}

void World::saveAll() {
    for (auto& [coord, slot] : m_chunks) {
        saveIfModified(coord, slot);
    }
}

const Chunk* World::chunkAt(const ChunkCoord& coord) const {
    const auto it = m_chunks.find(coord);
    return it == m_chunks.end() ? nullptr : &it->second.blocks;
}

bool World::hasChunk(const ChunkCoord& coord) const {
    return m_chunks.find(coord) != m_chunks.end();
}

BlockId World::blockAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    if (chunk == nullptr) {
        return BlockId::Air;
    }
    return chunk->at(floorMod(x, Chunk::kSize), floorMod(y, Chunk::kSize), floorMod(z, Chunk::kSize));
}

bool World::waterloggedAt(int x, int y, int z) const {
    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize),
                           floorDiv(z, Chunk::kSize)};
    const Chunk* chunk = chunkAt(coord);
    return chunk != nullptr && chunk->waterloggedAt(floorMod(x, Chunk::kSize),
                                                    floorMod(y, Chunk::kSize),
                                                    floorMod(z, Chunk::kSize));
}

bool World::isSolid(int x, int y, int z) const {
    return game::isSolid(blockAt(x, y, z));}

int World::highestSolid(int x, int z) const {
    for (int y = kWorldHeightChunks * Chunk::kSize - 1; y >= 0; --y) {
        // **`isSolid` is a movement test, not a floor test**, and it answers
        // true for every `BlockShape::Flat` block - a rail, a carpet, moss
        // carpet, a redstone line, a snow layer. Those are things you stand
        // *through*, not on, so counting one as the top of the column put
        // everything this feeds - creature spawn heights, structure feet, the
        // ground a village path is laid on - a block into the air wherever the
        // surface happened to be decorated. `fluidRestsOn` is the same question
        // the fluid system already had to answer ("can something rest on this,
        // or does it pass through") and is the one owner of it.
        if (fluidRestsOn(blockAt(x, y, z))) {
            return y;
        }
    }
    return -1;
}

int World::groundHeight(int x, int z) const {
    const int top = highestSolid(x, z);
    if (top >= 0) {
        return top + 1;
    }
    // Nothing solid all the way down is not a real column - bedrock is - so
    // this only ever means the chunks are not here. Ask whoever made them.
    return surfaceHeightAt(m_seed, x, z) + 1;
}

void World::queueMesh(const ChunkCoord& coord) {
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    if (m_pendingMeshSet.insert(coord).second) {
        m_pendingMesh.push_back(coord);
    }
}

void World::invalidateMesh(const ChunkCoord& coord) {
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }
    // Stamped from the world-wide counter rather than incremented in place, so
    // no two versions of a chunk - including two lives of the same coordinate -
    // can ever wear the same number. See `m_nextRevision`.
    it->second.revision = ++m_nextRevision;
    queueMesh(coord);
}

void World::setBlock(int x, int y, int z, BlockId block, Placement by) {
    if (y < 0 || y >= kWorldHeightChunks * Chunk::kSize) {
        return;
    }

    const ChunkCoord coord{floorDiv(x, Chunk::kSize), floorDiv(y, Chunk::kSize), floorDiv(z, Chunk::kSize)};
    const auto it = m_chunks.find(coord);
    if (it == m_chunks.end()) {
        return;
    }

    const int lx = floorMod(x, Chunk::kSize);
    const int ly = floorMod(y, Chunk::kSize);
    const int lz = floorMod(z, Chunk::kSize);

    const BlockId previous = it->second.blocks.at(lx, ly, lz);
    if (previous == block) {
        return;
    }

    // **A waterlogged cell that loses its block becomes water, not air.** Take
    // the plant out of a patch of seagrass and the sea has to close over it;
    // leaving air there is the hole this whole feature exists to stop. And
    // anything that fills the cell displaces the water it was sharing.
    const bool wasLogged = it->second.blocks.waterloggedAt(lx, ly, lz);
    BlockId placed = block;
    if (wasLogged && block == BlockId::Air) {
        placed = BlockId::Water0;
        it->second.blocks.setWaterlogged(lx, ly, lz, false);
    } else if (!canWaterlog(block)) {
        it->second.blocks.setWaterlogged(lx, ly, lz, false);
    } else if (isWater(previous)) {
        // Put a plant into water and it takes the water with it, which is the
        // other half of the rule above.
        it->second.blocks.setWaterlogged(lx, ly, lz, true);
    }

    it->second.blocks.set(lx, ly, lz, placed);
    it->second.modified = true;

    // **The one-bit blockstate, rewritten on every single write.** One bit
    // array carries different meanings for different blocks - a leaf's
    // `persistent_bit` and a sapling's `age_bit` - and that is only safe
    // because no cell can ever inherit the meaning the last occupant gave it.
    // Miss this line and a sapling planted where a player's leaf used to be
    // starts life half-grown.
    it->second.blocks.setStateBit(lx, ly, lz, isLeafBlock(placed) && by == Placement::Player);

    // Relight around the change. Removals have to run before additions, because
    // stale light must be cleared out before anything fills the gap.
    const int oldSky = it->second.blocks.skyLightAt(lx, ly, lz);
    const int oldBlockLight = it->second.blocks.blockLightAt(lx, ly, lz);

    // **`admitsLight` names the gate; see its comment for why that is not the
    // triviality it looks like.** A fence, wall, gate, rail, redstone line or
    // snow layer used to be light-opaque and sky-transparent, so this branch was
    // taken for all 368 of them: placing one zeroed its cell and queued a
    // removal that darkened the whole column beneath it, while the same block
    // arriving from `seedColumnLight` - which asks `isSkyTransparent` - was left
    // at full sky. Lighting depended on whether a block had been placed or
    // reloaded.
    if (!admitsLight(placed)) {
        if (oldSky > 0) {
            it->second.blocks.setSkyLight(lx, ly, lz, 0);
            m_skyRemovals.push_back({{x, y, z}, oldSky});
        }
        if (oldBlockLight > 0) {
            it->second.blocks.setBlockLight(lx, ly, lz, 0);
            m_blockRemovals.push_back({{x, y, z}, oldBlockLight});
        }
    } else {
        // **The other direction of the same disagreement.** A block that still
        // passes light but stops the free fall - leaves, a copper grate, tinted
        // glass - has to re-dim the column under it, because `seedColumnLight`
        // ends the free fall at exactly these and the placed cell would
        // otherwise keep the full-strength sky it had as air. The removal walk
        // handles the cascade and the addition below refills this cell from its
        // neighbours at one level down, which is what a canopy is.
        if (!isSkyTransparent(placed) && oldSky == kMaxLight) {
            it->second.blocks.setSkyLight(lx, ly, lz, 0);
            m_skyRemovals.push_back({{x, y, z}, oldSky});
        }
        // Newly transparent: whatever surrounds it can now flow in.
        if (blockLightEmission(previous) > 0 && oldBlockLight > 0) {
            it->second.blocks.setBlockLight(lx, ly, lz, 0);
            m_blockRemovals.push_back({{x, y, z}, oldBlockLight});
        }
        for (const glm::ivec3& step : kLightSteps) {
            m_skyAdditions.push_back(glm::ivec3{x, y, z} + step);
            m_blockAdditions.push_back(glm::ivec3{x, y, z} + step);
        }
    }

    if (const int emission = blockLightEmission(block); emission > 0) {
        it->second.blocks.setBlockLight(lx, ly, lz, emission);
        m_blockAdditions.push_back({x, y, z});
    }

    lightChangedAt(x, y, z);

    // Water re-evaluates itself and everything touching it. Removing a block
    // under a stream is what makes it fall; removing its supply is what makes it
    // recede.
    scheduleFluidUpdate(x, y, z);
    for (const glm::ivec3& step : kLightSteps) {
        scheduleFluidUpdate(x + step.x, y + step.y, z + step.z);
    }

    // And the same for lava. **Both queues get every edit** rather than one
    // being chosen by what is in the cell right now: the cell that just changed
    // may be air that lava is about to reach, and a queue that finds the wrong
    // fluid there simply does nothing. Guessing here is how a flow stalls one
    // block short of where it should stop.
    scheduleLavaUpdate(x, y, z);
    for (const glm::ivec3& step : kLightSteps) {
        scheduleLavaUpdate(x + step.x, y + step.y, z + step.z);
    }

    // A fire that has just been placed needs its first tick; one that already
    // exists reschedules itself, so this is the only way in.
    if (block == BlockId::Fire) {
        scheduleFireUpdate(x, y, z);
    }

    // Only two cells can have lost their footing: this one, if what just landed
    // in it falls, and the one resting on top of it.
    scheduleFallUpdate(x, y, z);
    scheduleFallUpdate(x, y + 1, z);

    // And whatever was *leaning* on this cell. Scheduled unconditionally, for
    // the same reason the fluid queues are: guessing which edits can cost a
    // neighbour its support means a rule that is right for the break path and
    // silently wrong for water, fire, decay and falling sand - which is exactly
    // how this ended up being handled in one place out of five.
    scheduleSupportUpdate(x, y, z);

    // **And the cell *under* this one, but only where a dripstone hangs
    // there.** Everything else in the game is held up from below and is
    // covered by the line above, whose position means "check what rests on
    // this cell". Pointed dripstone is the one family that can lose its anchor
    // when the cell *above* it changes, so a stalactite whose ceiling was mined
    // would otherwise hang there for ever - and the queue's position is always
    // one below the block in doubt, which is why reaching `y - 1` means
    // scheduling `y - 2`.
    //
    // **Guarded by a read rather than scheduled unconditionally**, unlike its
    // neighbour above, because this runs on every write in the game: one
    // `blockAt` is much cheaper than doubling the support queue's traffic for
    // a family that is absent from all but a handful of cells. The read cannot
    // give a false negative - the dripstone is already standing there, and it
    // is its anchor that just changed, not itself.
    if (blockAt(x, y - 1, z) == BlockId::PointedDripstone) {
        scheduleSupportUpdate(x, y - 2, z);
    }

    // **Losing a log or a leaf is what puts a canopy in doubt, and nothing
    // else is.** minecraft.wiki [[Leaves]] says Bedrock leaves carry an
    // `update_bit` beside `persistent_bit`, and this is what that bit is: the
    // legacy algorithm the wiki says Bedrock still runs only performs its scan
    // on a leaf a neighbour has changed under. Without it the random tick alone
    // is the whole mechanism, and at this project's sampling rate - one visit
    // per cell every 204.8 s - a chopped canopy would hang around for minutes,
    // which is the very complaint this is meant to fix.
    //
    // Six faces, not the whole ball: a leaf that has just lost a path through
    // this cell is adjacent to it, and when *that* leaf goes it schedules its
    // own six. The cascade is therefore linear in the leaves that actually die
    // rather than 129 cells re-scanned per leaf.
    if (isLeafBlock(previous) || farming::leafKeeper(previous)) {
        for (const glm::ivec3& step : kLightSteps) {
            scheduleLeafCheck(x + step.x, y + step.y, z + step.z);
        }
    }

    invalidateMesh(coord);

    // Every *other* chunk whose geometry the change can reach - which on a
    // chunk corner includes the diagonal neighbours, because the mesher's
    // corner shading samples one step along all three axes at once. Six
    // single-axis edge tests can never name `{-1, 0, -1}`, so a block placed
    // hard against a chunk corner left a permanent smudge of stale shading on
    // the chunk across from it. See `forEachChunkTouchedBy`.
    //
    // These must invalidate rather than merely queue: a neighbour may already be
    // meshing against the old border, and that result would otherwise be
    // accepted as current.
    forEachChunkTouchedBy(x, y, z, [this, &coord](const ChunkCoord& touched) {
        if (touched != coord) {
            invalidateMesh(touched);
        }
    });

    // **A chest's neighbours are not the only chests it changes.** Pairing is
    // derived by walking to the start of the run and counting off in twos, so
    // taking one out of the middle flips the half every chest after it wears -
    // up to `kMaxChestRun` blocks, which is two chunks away. The rule that says
    // "only a block on a chunk face can reach a neighbour" is true of every
    // other block in the game and false of this one.
    if (isChest(previous)) {
        invalidateChestRun({x, y, z}, previous);
    }
    if (isChest(placed) && placed != previous) {
        invalidateChestRun({x, y, z}, placed);
    }

    // **Worked ground stops being worked the moment something covers it.**
    // minecraft.wiki [[Farmland]]: farmland "becomes a dirt block, regardless
    // of its state of hydration, if ... a solid block covers the top surface of
    // the farmland block such as when pumpkin or melon blocks appear, or when
    // trees grow", and [[Dirt Path]] carries the same rule. Instant, not on a
    // random tick, which is why it is here and not in `growOne` - and one rule
    // for both, because the fruit a stem grows, the log a sapling becomes and
    // the floor a player lays are the same event from the ground's point of
    // view.
    if (coversTopOf(placed) && y > 0) {
        const BlockId under = blockAt(x, y - 1, z);
        if (isFarmland(under) || under == BlockId::DirtPath) {
            setBlock(x, y - 1, z, BlockId::Dirt);
        }
    }

    // **A stem straightens when its fruit is taken, and only then.**
    // minecraft.wiki [[Melon Seeds]]: "When the melon is removed, the stem
    // returns to its straight shape", and [[Melon]]: "A single stem can grow an
    // unlimited number of melons". Without this an attached stem is inert
    // forever - `isGrowingStem` is false for one, so the random-tick filter
    // never looks at it again - and a farm yields exactly one fruit per seed.
    //
    // The stem sits one step the *other* way from the fruit, and the facing it
    // wears is the index of that step in `kFruitSides`, so the search is four
    // reads with no scan. It comes back ripe rather than at age 0: the reference
    // keeps the stem fully grown and only the fruit is the harvest.
    if ((previous == BlockId::Melon || previous == BlockId::Pumpkin) && placed != previous) {
        const bool melon = previous == BlockId::Melon;
        const BlockId attachedFirst =
            melon ? BlockId::MelonStemAttachedFirst : BlockId::PumpkinStemAttachedFirst;
        const BlockId ripe = melon ? BlockId::MelonStemLast : BlockId::PumpkinStemLast;
        for (int side = 0; side < static_cast<int>(kFruitSides.size()); ++side) {
            const glm::ivec3 stem{x - kFruitSides[side].x, y - kFruitSides[side].y,
                                  z - kFruitSides[side].z};
            if (blockAt(stem.x, stem.y, stem.z) ==
                static_cast<BlockId>(static_cast<int>(attachedFirst) + side)) {
                setBlock(stem.x, stem.y, stem.z, ripe);
            }
        }
    }
}

void World::invalidateChestRun(const glm::ivec3& at, BlockId chest) {
    if (!chestPairs(chest)) {
        return;
    }
    const glm::ivec3 axis = chestJoinsAlongX(chest) ? glm::ivec3{1, 0, 0} : glm::ivec3{0, 0, 1};

    // Both ways from the edit, because a chest removed from the middle of a run
    // splits it in two and the far half re-pairs from a new start. Walking the
    // run outward from here rather than from its start is what keeps this
    // bounded whichever end the edit landed at.
    for (const int direction : {1, -1}) {
        for (int step = 1; step <= kMaxChestRun; ++step) {
            const glm::ivec3 cell = at + axis * (direction * step);
            if (blockAt(cell.x, cell.y, cell.z) != chest) {
                break;
            }
            invalidateMesh({floorDiv(cell.x, Chunk::kSize), floorDiv(cell.y, Chunk::kSize),
                            floorDiv(cell.z, Chunk::kSize)});
        }
    }
}

bool World::neighboursLoaded(const ChunkCoord& coord) const {
    // **Horizontal only, and deliberately.** Chunks are generated and unloaded
    // as whole columns, and the world is three chunks tall - so `cy - 1` for the
    // bottom chunk and `cy + 1` for the top one name coordinates that will never
    // exist, and asking for them would stop those two ever being meshed. What a
    // missing vertical neighbour would cost is covered instead: `gatherVolume`
    // reads an absent chunk as open sky, which above the world is the truth.
    return hasChunk({coord.x - 1, coord.y, coord.z}) && hasChunk({coord.x + 1, coord.y, coord.z}) &&
           hasChunk({coord.x, coord.y, coord.z - 1}) && hasChunk({coord.x, coord.y, coord.z + 1});
}

std::optional<glm::ivec3> World::chestPartnerAt(const glm::ivec3& at) const {
    const BlockId id = blockAt(at.x, at.y, at.z);
    if (!isChest(id) || !chestPairs(id)) {
        return std::nullopt;
    }
    const glm::ivec3 axis = chestJoinsAlongX(id) ? glm::ivec3{1, 0, 0} : glm::ivec3{0, 0, 1};
    const auto sameChest = [&](const glm::ivec3& cell) {
        return blockAt(cell.x, cell.y, cell.z) == id;
    };
    // Walk back to the start of the run, then pair off in twos from there. Both
    // halves reach the same answer because they do the same walk.
    int back = 0;
    while (back < kMaxChestRun && sameChest(at - axis * (back + 1))) {
        ++back;
    }
    const glm::ivec3 partner = back % 2 == 0 ? at + axis : at - axis;
    return sameChest(partner) ? std::optional<glm::ivec3>{partner} : std::nullopt;
}

ChestHalf World::chestHalfAt(const glm::ivec3& at) const {
    const std::optional<glm::ivec3> partner = chestPartnerAt(at);
    if (!partner) {
        return ChestHalf::Single;
    }
    const glm::ivec3 offset = *partner - at;
    return chestHalfFor(chestFacing(blockAt(at.x, at.y, at.z)), offset.x, offset.z);
}

void World::gatherVolume(const ChunkCoord& coord, ChunkVolume& volume) const {
    // The padded region spans at most one chunk in each direction, so the 27
    // possible source chunks are looked up once rather than per cell.
    const Chunk* sources[3][3][3]{};
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                sources[dx + 1][dy + 1][dz + 1] = chunkAt({coord.x + dx, coord.y + dy, coord.z + dz});
            }
        }
    }

    constexpr int size = Chunk::kSize;
    constexpr int pad = ChunkVolume::kPad;

    // Blocks and light are overwritten in full below, but the flags are written
    // only where something is set, so they are cleared here. **Self-contained
    // on purpose**: relying on the caller to hand over zeroed storage is the
    // sort of unstated precondition that survives until somebody pools the
    // buffers, and then fails as stale waterlogging in a chunk that has none.
    volume.flags.fill(0u);

    // **Copied a row at a time, not a cell at a time.** X is the fastest-moving
    // axis in both layouts, so the thirty-two interior cells of every row are
    // one contiguous run in the source and one in the destination. This runs on
    // the main thread for every chunk that is meshed, which while flying is the
    // budget the whole streaming pipeline is metered against - the per-cell
    // version re-derived a chunk pointer, a local coordinate and three
    // bounds-checked accessors for each of thirty-nine thousand cells.
    const auto copyRow = [&](int y, int z) {
        const int dy = (y < 0) ? -1 : (y >= size ? 1 : 0);
        const int ly = y - dy * size;
        const int dz = (z < 0) ? -1 : (z >= size ? 1 : 0);
        const int lz = z - dz * size;

        // A row's three pieces: one cell from the -X neighbour, the whole
        // interior span, one cell from the +X neighbour.
        struct Piece {
            int dx;
            int fromX;
            int toX;
            int count;
        };
        const Piece pieces[3]{{-1, size - 1, -pad, pad}, {0, 0, 0, size}, {1, 0, size, pad}};

        for (const Piece& piece : pieces) {
            const Chunk* source = sources[piece.dx + 1][dy + 1][dz + 1];
            const std::size_t at = ChunkVolume::index(piece.toX, y, z);
            const auto count = static_cast<std::size_t>(piece.count);

            if (source == nullptr) {
                // Missing neighbours read as open sky rather than as solid, so
                // the streaming frontier does not draw a wall of faces or a
                // band of darkness.
                std::fill_n(volume.blocks.begin() + static_cast<std::ptrdiff_t>(at), count, BlockId::Air);
                std::fill_n(volume.light.begin() + static_cast<std::ptrdiff_t>(at), count,
                            static_cast<std::uint8_t>(0xF0));
                std::fill_n(volume.flags.begin() + static_cast<std::ptrdiff_t>(at), count,
                            static_cast<std::uint8_t>(0u));
                continue;
            }

            const std::size_t from = Chunk::cellIndex(piece.fromX, ly, lz);
            std::memcpy(volume.blocks.data() + at, source->data() + from, count * sizeof(BlockId));
            std::memcpy(volume.light.data() + at, source->lightData() + from, count);

            // The waterlogged bits are one per cell in the same order, so a run
            // along X is a run of consecutive bits - and an interior row starts
            // on a byte boundary, which is what lets the whole row be dismissed
            // with one test. **Almost every row in the world has none**, and
            // the caller hands us storage that is already zeroed, so the common
            // case is a single load and nothing written.
            const std::uint8_t* bits = source->waterloggedData();
            const std::size_t firstByte = from >> 3;
            const std::size_t byteCount = (count + 7) / 8;
            bool any = false;
            for (std::size_t b = 0; b < byteCount && !any; ++b) {
                any = bits[firstByte + b] != 0u;
            }
            if (!any) {
                continue;
            }
            for (std::size_t i = 0; i < count; ++i) {
                const std::size_t bit = from + i;
                if ((bits[bit >> 3] & (1u << (bit & 7u))) != 0u) {
                    volume.flags[at + i] |= ChunkVolume::kWaterlogged;
                }
            }
        }

        // Only a chest can answer anything but `Single`, and chests are rare, so
        // the run walk is never paid for on the cells that make up the world.
        for (int x = -pad; x < size + pad; ++x) {
            const std::size_t at = ChunkVolume::index(x, y, z);
            if (!isChest(volume.blocks[at])) {
                continue;
            }
            const glm::ivec3 world{coord.x * size + x, coord.y * size + y, coord.z * size + z};
            volume.flags[at] |= static_cast<std::uint8_t>(static_cast<int>(chestHalfAt(world))
                                                          << ChunkVolume::kChestHalfShift);
        }
    };

    for (int y = -pad; y < size + pad; ++y) {
        for (int z = -pad; z < size + pad; ++z) {
            copyRow(y, z);
        }
    }
}

void World::refreshQueues(const ChunkCoord& centre) {
    m_pendingLoad.clear();

    for (int dz = -m_loadRadius; dz <= m_loadRadius; ++dz) {
        for (int dx = -m_loadRadius; dx <= m_loadRadius; ++dx) {
            for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
                const ChunkCoord coord{centre.x + dx, cy, centre.z + dz};
                if (!hasChunk(coord)) {
                    m_pendingLoad.push_back(coord);
                }
            }
        }
    }

    // Sorted farthest-first so the nearest chunk is at the back, where removing
    // it is free. Draining from the front would be quadratic.
    std::sort(m_pendingLoad.begin(), m_pendingLoad.end(), [&](const ChunkCoord& a, const ChunkCoord& b) {
        return chebyshevDistance(a, centre) > chebyshevDistance(b, centre);
    });

    // Chunks that exist but were never meshed: they were dropped from the queue
    // because a neighbour was missing or because they sat outside the visible
    // radius. Nothing else would ever pick them up again, which shows as a
    // permanent hole when the player walks back toward them.
    //
    // **And chunks whose drawn tier is not the tier they should be at.** A
    // queued re-mesh can be dropped for the same two reasons, and a chunk that
    // is already meshed would otherwise never be looked at again - which is how
    // a plant fails to reappear until something unrelated dirties the chunk.
    //
    // **And chunks drawing geometry older than their contents.** Exactly the
    // same hole, one field along: chunks stay resident and drawn two rings past
    // the visible radius, and an edit out there - a crop growing, a flow
    // arriving, a fuse blinking - bumps `revision` and queues a re-mesh that
    // `dispatchMeshes` then drops for being out of range. `meshed` is still
    // true and the tiers still agree, so the chunk agreed with itself and drew
    // its pre-edit geometry for the rest of the session.
    //
    // Queued, not invalidated: their contents have not changed, and bumping the
    // revision here would throw away perfectly good work every time the player
    // crossed a chunk boundary.
    for (const auto& [coord, slot] : m_chunks) {
        if (chebyshevDistance(coord, centre) > m_visibleRadius) {
            continue;
        }
        if (!slot.meshed || slot.detailBuilt != slot.detailWanted || slot.builtRevision != slot.revision) {
            queueMesh(coord);
        }
    }

    // The player's chunk has moved, so the detail boundary has moved with it.
    // This is the one place a chunk crosses it in either direction.
    refreshDetail(centre);
}

std::size_t World::jobCapacity() const {
    // Enough to keep every worker fed with a little slack, and no more. Split
    // between the two kinds of work so a long stream of loads cannot starve
    // meshing and leave the player standing in an invisible world.
    return static_cast<std::size_t>(m_jobs.threadCount()) * 2 + 2;
}

void World::dispatchLoads(const BudgetCheck& budgetSpent, std::size_t capacity) {
    // The budget matters even with workers, and matters completely without them:
    // a pool with no threads runs `submit` inline, so this loop *is* the work.
    while (!m_pendingLoad.empty() && m_loadInFlight.size() < capacity && !budgetSpent()) {
        const ChunkCoord coord = m_pendingLoad.back();
        m_pendingLoad.pop_back();

        if (hasChunk(coord) || m_loadInFlight.count(coord) != 0) {
            continue;
        }

        m_loadInFlight.insert(coord);

        // Captures only copies and shared owners. Nothing here reaches back into
        // the world, which is what makes it safe to run anywhere.
        m_jobs.submit([coord, seed = m_seed, store = m_store, results = m_results] {
            // A saved chunk replaces generation entirely: it already contains
            // the generated terrain plus whatever the player did to it.
            std::optional<Chunk> stored = store->load(coord);
            const bool fromDisk = stored.has_value();
            Chunk blocks = fromDisk ? std::move(*stored) : generateChunk(seed, coord);

            std::lock_guard<std::mutex> lock(results->mutex);
            results->loaded.push_back(LoadedChunk{coord, std::move(blocks), fromDisk});
        });
    }
}

void World::dispatchMeshes(const ChunkCoord& centre, const BudgetCheck& budgetSpent, std::size_t capacity) {
    // **Nearest first, or the chunk you are flying toward waits behind the one
    // you just left.** The queue is drained from the back and entries arrive in
    // whatever order the chunk map iterates and light propagation dirties them,
    // which is effectively random - so with a deep queue a tier upgrade landed
    // at an arbitrary distance and plants appeared a couple of chunks away
    // rather than at the boundary.
    //
    // `nth_element` rather than a full sort: only the handful about to be
    // dispatched need to be the right ones, and this is linear where a sort of
    // several thousand entries every frame is not.
    if (m_pendingMesh.size() > capacity) {
        const auto farthestFirst = [&](const ChunkCoord& a, const ChunkCoord& b) {
            return chebyshevDistance(a, centre) > chebyshevDistance(b, centre);
        };
        std::nth_element(m_pendingMesh.begin(),
                         m_pendingMesh.end() - static_cast<std::ptrdiff_t>(capacity),
                         m_pendingMesh.end(), farthestFirst);
    }

    while (!m_pendingMesh.empty() && m_meshInFlight.size() < capacity && !budgetSpent()) {
        const ChunkCoord coord = m_pendingMesh.back();
        m_pendingMesh.pop_back();
        m_pendingMeshSet.erase(coord);

        const auto it = m_chunks.find(coord);
        if (it == m_chunks.end()) {
            continue;
        }
        if (chebyshevDistance(coord, centre) > m_visibleRadius || !neighboursLoaded(coord)) {
            continue;
        }
        // Already being meshed. Dropping it is safe: if the chunk has changed
        // since that job started, the revision check on collection re-queues it.
        if (m_meshInFlight.count(coord) != 0) {
            continue;
        }

        m_meshInFlight.insert(coord);

        // The tier is resolved here, on the main thread, and travels with the
        // volume. A job that asked where the player was would give two chunks
        // meshed in the same frame different answers.
        const MeshDetail detail = detailFor(coord, it->second.detailWanted);
        it->second.detailWanted = detail == MeshDetail::Full;

        // The chunk and its borders are copied here, on the main thread, so the
        // job owns everything it reads. That copy is what removes the whole
        // question of what the main thread may do to this chunk meanwhile.
        auto input = std::make_shared<MeshJobInput>();
        input->coord = coord;
        input->revision = it->second.revision;
        input->detail = detail;
        input->origin = glm::vec3{static_cast<float>(coord.x * Chunk::kSize),
                                  static_cast<float>(coord.y * Chunk::kSize),
                                  static_cast<float>(coord.z * Chunk::kSize)};
        gatherVolume(coord, input->volume);
        m_jobs.submit([input = std::move(input), results = m_results] {
            ChunkMeshes meshes = meshChunk(input->volume, input->origin, input->detail);

            std::lock_guard<std::mutex> lock(results->mutex);
            results->meshed.push_back(
                MeshedChunk{input->coord, std::move(meshes), input->revision, input->detail});
        });
    }
}

void World::collectFinishedJobs() {
    std::vector<LoadedChunk> loaded;
    std::vector<MeshedChunk> meshed;

    {
        std::lock_guard<std::mutex> lock(m_results->mutex);
        loaded.swap(m_results->loaded);
        meshed.swap(m_results->meshed);
    }

    for (LoadedChunk& result : loaded) {
        m_loadInFlight.erase(result.coord);

        // The player may have walked far enough that this is no longer wanted,
        // or a later pass may already have loaded it.
        if (hasChunk(result.coord)) {
            continue;
        }
        if (m_hasCentre && chebyshevDistance(result.coord, m_centre) > m_unloadRadius) {
            continue;
        }

        // **The revision is taken from the world-wide counter, not started at
        // zero.** A chunk that is unloaded while one of its mesh jobs is still
        // running comes back through here, and a fresh zero is exactly what
        // that job's recorded zero would match: it would land, be accepted as
        // current, and install geometry from the chunk's previous life - for
        // good, since the slot is then marked meshed with its tiers in
        // agreement and nothing ever looks at it again.
        m_chunks.emplace(result.coord,
                         ChunkSlot{std::move(result.blocks), false, false, false, false, ++m_nextRevision});

        // Sky light is traced from the top of the world down, so it can only be
        // done once every chunk in the column is present.
        //
        // **Queued rather than seeded here, and that is the whole fix.** This
        // function takes no budget and runs *before* `update` builds one, while
        // `seedColumnLight` is ~98,000 cells per column by its own measurement.
        // Doing it inline therefore paid for every column that finished since
        // the last frame, uncapped, ahead of the 3 ms streaming budget - and
        // with eleven workers several land together. `rescanRestoredChunks`
        // already carried exactly this rule for a sweep a third the size; this
        // is that rule reaching the second place that needed it.
        //
        // Re-checked at the drain, because the column can unload in between.
        if (columnLoaded(result.coord.x, result.coord.z)) {
            m_pendingColumnSeeds.push_back(result.coord);
        }

        // Nothing in the save file remembers a queued leaf check or a queued
        // support check, so a chunk that came off disk has to be read for them.
        // Queued rather than done here: see `rescanRestoredChunks`.
        if (result.fromDisk) {
            m_pendingRescan.push_back(result.coord);
        }

        // The new chunk and its neighbours may all have gained or lost visible
        // faces along the shared border, so a neighbour already being meshed is
        // now building against a border that no longer matches.
        //
        // **All twenty-six of them, not the four horizontal ones.** A chunk is
        // a diagonal sample for the corners of the eight chunks around each of
        // its own corners, and a vertical neighbour shares a whole face. The
        // vertical and diagonal cases happened to be covered by
        // `seedColumnLight` dirtying the column when it completed - three
        // unrelated facts holding up one property, none of them written down,
        // and all three untrue the day anything unloads a chunk on its own
        // rather than a whole column. Coordinates outside the world are simply
        // not found and cost a map lookup.
        queueMesh(result.coord);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0 && dz == 0) {
                        continue;
                    }
                    invalidateMesh({result.coord.x + dx, result.coord.y + dy, result.coord.z + dz});
                }
            }
        }
    }

    for (MeshedChunk& result : meshed) {
        m_meshInFlight.erase(result.coord);
        m_readyMeshes.push_back(std::move(result));
    }
}

std::vector<ChunkMeshUpdate> World::update(const glm::vec3& playerPosition, float budgetSeconds) {
    const auto start = Clock::now();
    std::vector<ChunkMeshUpdate> updates;

    const ChunkCoord centre{floorDiv(static_cast<int>(std::floor(playerPosition.x)), Chunk::kSize), 0,
                            floorDiv(static_cast<int>(std::floor(playerPosition.z)), Chunk::kSize)};

    if (!m_hasCentre || centre.x != m_centre.x || centre.z != m_centre.z) {
        m_centre = centre;
        m_hasCentre = true;

        // Unload first so memory is released before anything new is allocated.
        for (auto it = m_chunks.begin(); it != m_chunks.end();) {
            if (chebyshevDistance(it->first, centre) > m_unloadRadius) {
                // Must happen before the erase, or an edited chunk is lost the
                // moment the player walks away from it.
                saveIfModified(it->first, it->second);

                // **And a chunk whose write did not land is not unloaded at
                // all.** Retrying is only a retry if the thing being retried
                // still exists: a full disk or a rename an antivirus scanner
                // held open leaves the flag set, and erasing here would throw
                // the edits away exactly as clearing the flag used to. It stays
                // resident, is offered again on the next boundary crossing, and
                // gets a last go at `saveAll`. The cost is memory in the case
                // where the disk never comes back, which is the right way round
                // - the alternative is a player's build disappearing with no
                // warning at all.
                if (it->second.modified) {
                    ++it;
                    continue;
                }

                // A column is only "lit" while all of it is resident. Leaving
                // the key behind makes `seedColumnLight` return immediately when
                // the chunk comes back, so it reloads with no sky light at all -
                // which is what lowering render distance and raising it again
                // used to do.
                m_litColumns.erase((static_cast<std::uint64_t>(static_cast<std::uint32_t>(it->first.x))
                                    << 32) |
                                   static_cast<std::uint32_t>(it->first.z));

                if (it->second.meshed) {
                    updates.push_back(ChunkMeshUpdate{it->first, {}, {}, true});
                }

                // **The in-flight marks go with the chunk.** They exist only to
                // stop a second job being dispatched for a coordinate that
                // already has one - and a job cannot be recalled, so a mark
                // left behind by an unload goes on suppressing dispatches for a
                // coordinate whose job belongs to a chunk that no longer
                // exists. The reload's own first mesh is the one that gets
                // dropped, leaving the chunk resident and invisible until
                // something unrelated re-queues it. The old results are still
                // discarded when they land: the reloaded slot wears a stamp
                // from `m_nextRevision` that no earlier job can match.
                m_meshInFlight.erase(it->first);
                m_loadInFlight.erase(it->first);

                it = m_chunks.erase(it);
            } else {
                ++it;
            }
        }

        // Queued work for chunks that no longer exist or are out of range would
        // otherwise pile up forever as the player walks. In-flight jobs are left
        // alone; they are discarded on collection instead, because a running job
        // cannot be recalled.
        const auto outOfRange = [&](const ChunkCoord& c) {
            if (chebyshevDistance(c, centre) <= m_unloadRadius) {
                return false;
            }
            m_pendingMeshSet.erase(c);
            return true;
        };
        m_pendingMesh.erase(std::remove_if(m_pendingMesh.begin(), m_pendingMesh.end(), outOfRange),
                            m_pendingMesh.end());

        refreshQueues(centre);
    }

    if (m_radiusShrunk) {
        m_radiusShrunk = false;
        for (auto& [coord, slot] : m_chunks) {
            if (slot.meshed && chebyshevDistance(coord, centre) > m_visibleRadius) {
                slot.meshed = false;
                updates.push_back(ChunkMeshUpdate{coord, {}, {}, true});
            }
        }
    }

    collectFinishedJobs();

    const auto budgetSpent = [&] {
        return std::chrono::duration<float>(Clock::now() - start).count() >= budgetSeconds;
    };

    // **Before `propagateLight`, which drains what this fills.** The ordering
    // is identical to when `collectFinishedJobs` seeded inline; the only change
    // is that the cost is now metered instead of being paid ahead of the budget.
    seedPendingColumns(budgetSpent);

    propagateLight(budgetSpent);
    flushLightDirty();
    // **After `collectFinishedJobs`, which fills it, and before the queues it
    // feeds.** A chunk that arrived this frame gets whatever it owes into the
    // queues in the same frame rather than the next one - which only changes
    // how soon a restored floating canopy starts to go, but costs nothing.
    rescanRestoredChunks(budgetSpent);
    updateFluids(budgetSpent);
    updateLava(budgetSpent);
    updateFire(budgetSpent);
    updateGrowth(budgetSpent);
    updateTnt(budgetSpent);
    updateFalls(budgetSpent);
    updateLeafDecay(budgetSpent);
    // **Last of the world updaters, on purpose.** Every one above it can take
    // a block out from under something - a flow, a fire, a decayed leaf, a
    // grain of sand - and each of those went through `setBlock`, so by the time
    // this runs the queue holds everything this frame has knocked the legs out
    // from under.
    updateSupports(budgetSpent);

    // Uploading is the only part still on the main thread, so it is what the
    // budget now meters.
    while (!m_readyMeshes.empty() && !budgetSpent()) {
        MeshedChunk result = std::move(m_readyMeshes.back());
        m_readyMeshes.pop_back();

        const auto it = m_chunks.find(result.coord);
        if (it == m_chunks.end()) {
            continue; // Unloaded while the job ran.
        }
        if (it->second.revision != result.revision) {
            // Something changed while the job ran. The revision has already moved
            // on, so this only needs re-queueing, not another bump.
            queueMesh(result.coord);
            continue;
        }

        it->second.meshed = true;
        it->second.detailBuilt = result.detail == MeshDetail::Full;
        // What is now on the GPU, so a later edit that loses its queue entry can
        // still be told from a chunk that is up to date. Taken from the result
        // rather than from the slot: they are equal here by the test above, and
        // saying "the stamp this geometry was built from" is the thing that
        // stays true if that test ever moves.
        it->second.builtRevision = result.revision;
        // The ranges travel with the buffer they describe. Copied rather than
        // moved: they are twenty-eight bytes of plain data, and reading them
        // after `translucent` has been moved from is only safe because they are
        // a different member - saying so here is cheaper than the day someone
        // folds them into `MeshData`.
        updates.push_back(ChunkMeshUpdate{result.coord, std::move(result.meshes.opaque),
                                          std::move(result.meshes.translucent), false,
                                          result.meshes.translucentFacings,
                                          result.meshes.translucentShaped});
    }

    dispatchLoads(budgetSpent, jobCapacity());
    dispatchMeshes(centre, budgetSpent, jobCapacity());

    return updates;
}

bool World::isSettled() const {
    // **Every queue that terminates.** Falls and fuses do: sand drops one block
    // per step until it lands, and a charge spends a fixed fuse and goes off.
    // A world that reported settled while a cliff was still collapsing handed
    // the player control in the middle of it.
    //
    // Fire and lava are the two that are left out, and it is not an oversight:
    // both reschedule themselves for as long as there is anything left to burn,
    // so one lava lake with a tree beside it would hold the loading screen open
    // for ever. Whichever way the next person is tempted to "finish" this list,
    // that is the reason not to.
    return pendingChunkCount() == 0 && m_skyAdditions.empty() && m_blockAdditions.empty() &&
           m_skyRemovals.empty() && m_blockRemovals.empty() && m_lightDirty.empty() &&
           m_fluidUpdates.empty() && m_fallUpdates.empty() && m_tntFuses.empty();
}

World::LoadStatus World::loadStatus() const {
    LoadStatus status;

    const int loadSpan = 2 * m_loadRadius + 1;
    const auto wantedChunks = static_cast<float>(loadSpan * loadSpan * kWorldHeightChunks);
    status.generated =
        wantedChunks <= 0.0f ? 1.0f : std::min(1.0f, static_cast<float>(m_chunks.size()) / wantedChunks);

    if (!m_hasCentre) {
        return status;
    }

    // **Counted over the visible box, not over whatever happens to be loaded.**
    // The old measure was meshed-over-loaded, which reads high from the very
    // first frame - two chunks in with one meshed is "half drawn" - and could
    // never reach 1 anyway, because the outer ring is loaded on purpose and
    // never meshed.
    std::size_t drawn = 0;
    std::size_t wanted = 0;
    for (int dz = -m_visibleRadius; dz <= m_visibleRadius; ++dz) {
        for (int dx = -m_visibleRadius; dx <= m_visibleRadius; ++dx) {
            for (int cy = 0; cy < kWorldHeightChunks; ++cy) {
                ++wanted;
                const auto it = m_chunks.find(ChunkCoord{m_centre.x + dx, cy, m_centre.z + dz});
                if (it != m_chunks.end() && it->second.meshed) {
                    ++drawn;
                }
            }
        }
    }

    status.drawn = wanted == 0 ? 1.0f : static_cast<float>(drawn) / static_cast<float>(wanted);
    status.settled = isSettled();
    // Every chunk you could see is built and uploaded, every chunk the streamer
    // asked for exists, and every queue behind them is empty. Anything weaker
    // and the world carries on assembling after the bar has gone.
    status.complete = status.generated >= 1.0f && drawn == wanted && status.settled;
    return status;
}

float World::initialLoadProgress() const {
    const LoadStatus status = loadStatus();

    // Two weighted checkpoints rather than one number that guesses. Meshing gets
    // the larger share because it is the half you can actually see; the last
    // sliver is the drain, which is quick and would otherwise let the bar sit at
    // 100% while light and water finish behind it.
    constexpr float kGeneratedShare = 0.40f;
    constexpr float kDrawnShare = 0.59f;
    const float reported = kGeneratedShare * status.generated + kDrawnShare * status.drawn;

    return status.complete ? 1.0f : std::min(reported, 0.99f);
}

} // namespace game
