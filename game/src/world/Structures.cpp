#include "world/Structures.hpp"

#include "world/Biome.hpp"
#include "world/Noise.hpp"
#include "world/TerrainGenerator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace game::structures {
namespace {

/// A deterministic per-tree stream. See `noise::Stream` for why every draw has
/// to come from one and happen in the same order both times a tree is built.
using Roll = noise::Stream;

/// How tall each variant may be drawn, as the `low` and the `span` a `range`
/// draw uses - so the tallest possible is `low + span - 1`.
///
/// **One table, read by both the draw and the reach assert.** The caps used to
/// be spelled out beside the draws and nowhere else, so nothing tied the two
/// numbers `kReach` depends on to the numbers that actually produce a tree: a
/// span raised by one is a canopy a block further out, and a canopy a block
/// further out than `kReach` is a tree one chunk builds and its neighbour does
/// not. Heights are the reference's, **scaled for a 96-block world against its
/// 384** - the ratios inside each builder are dimensionless and deliberately
/// are not scaled with them.
struct HeightRange {
    int low;
    int span;

    constexpr int most() const { return low + span - 1; }
};

/// **Trunk height, in blocks, as an inclusive range** - `low` through
/// `low + span - 1`, which `most()` above spells out. The canopy sits on top of
/// this, so a number here is not the height of the finished tree.
///
/// Oak's 4-6 and the small jungle's ceiling of 10 are minecraft.wiki
/// [[Tree/Structure]] read straight off, and the branching oak's 6-10 is that
/// page's fancy oak, which it bounds at "2 less than the vertical space".
///
/// **Spruce and pine are ours, not the reference's.** The wiki gives spruce a
/// single range, 5-12, covering both shapes; splitting it into 5-7 and 6-8 is
/// this generator's choice, and the top was pulled in because the world is 96
/// blocks tall and a 12-block trunk plus its canopy can reach the ceiling from
/// high ground. **Do not "restore" the 12** without counting what the canopy
/// adds above `most()`.
constexpr HeightRange kOakHeight{4, 3};
constexpr HeightRange kBranchingHeight{6, 5};
constexpr HeightRange kSpruceHeight{5, 3};
constexpr HeightRange kPineHeight{6, 3};
/// **Acacia, 5..7.** The reference's savanna tree is 5-8 tall; the top of that
/// range is trimmed by one because this world's trees are scaled for a
/// ninety-six block ceiling like every other height here. SECONDARY - see the
/// source note on `TreeVariant::Acacia`'s case in `buildTree`.
constexpr HeightRange kAcaciaHeight{5, 3};
constexpr HeightRange kJungleSmallHeight{5, 6};
/// The reference's giant runs to 31 in a 384-block world; a quarter of that is
/// the 18 here, drawn from 11 so a jungle still has an understory.
constexpr HeightRange kJungleMegaHeight{11, 8};

/// How far the widest thing each variant draws lands from its trunk.
///
/// **This is the whole justification for `kReach`, and it is derived rather than
/// asserted by hand.** A branching oak's cluster is the worst case: the branch
/// runs `(0.328 + unit()) * height * 0.30` sideways - `unit()` is under one, so
/// the factor is under 1.328 - and the cluster it carries is a `blob` two
/// blocks wide about that point. `std::lround` is not `constexpr`, so the same
/// rounding is spelled out; it rounds up, which is the safe direction.
constexpr int roundedReach(float distance) {
    return static_cast<int>(distance + 0.5f);
}

constexpr int kBranchingReach =
    roundedReach(1.328f * static_cast<float>(kBranchingHeight.most()) * 0.30f) + 2;
/// A giant's branch runs `2 + range(2)` and carries a `blob` of radius 2; its
/// own canopy is a radius-3 disc offset by the two-by-two trunk.
constexpr int kJungleMegaReach = (3 + 2) > (3 + 1) ? (3 + 2) : (3 + 1);
/// Everything else is a `blob` of radius 2 centred on the trunk, plus the widest
/// disc a spruce skirt can reach, which is `2 + range(2)` capped at 3.
constexpr int kPlainReach = 3;

static_assert(kBranchingReach <= kReach,
              "A branching oak's furthest cluster must fit inside kReach. Raise "
              "kBranchingHeight.span and this is the assert that fires.");
static_assert(kJungleMegaReach <= kReach,
              "A giant jungle tree's branches must fit inside kReach. Widen its branch "
              "reach in buildTree and this is the assert that fires.");
static_assert(kPlainReach <= kReach,
              "An ordinary canopy must fit inside kReach. Raise a disc radius in "
              "buildTree and this is the assert that fires.");

/// **A village claims no further out than one structure reach inside its own.**
///
/// A claimed column suppresses a tree, and a tree is planted by every chunk its
/// canopy touches - so a chunk up to `kReach` from the trunk asks `occupies`
/// about it. That chunk only holds villages whose origin is within
/// `village::kReach` of itself, so if a claim could reach further than
/// `village::kReach - kReach` from the origin, one chunk would suppress a tree
/// its neighbour plants: half a tree standing at a chunk border, the worst
/// failure either file has.
///
/// `Village.hpp` cannot include this header - it is the other way round - so it
/// spells the 6 out and this is the check that the copy has not rotted. Change
/// `kReach` here without changing that 6 and the build fails.
static_assert(village::kClaimReach + kReach <= village::kReach,
              "A village's claim plus a tree's reach must stay inside the radius a chunk "
              "looks for villages in. Raise structures::kReach without lowering "
              "village::kClaimReach and this is the assert that fires.");
static_assert(village::kClaimReach + kReach >= village::kReach,
              "...and it must not be looser than it needs to be, or a village stops "
              "suppressing trees it overlaps. Lower structures::kReach without raising "
              "village::kClaimReach and this is the assert that fires.");

/// What a tree actually is, rather than which of two silhouettes it copies.
///
/// The reference does not have a "round tree"; it has a trunk placer and a
/// foliage placer, and a biome picks between several with lopsided weights so
/// one species dominates and the others punctuate. These seven are the cheapest
/// set that covers both of ours.
///
/// **`Count` exists to force a visit to `canopyOvershoot`.** That function ends
/// in a bare `return 1;` after its switch, which is `CLAUDE.md` bug shape #10 -
/// a catch-all returning a real value, so a variant nobody added a case for
/// gets a plausible wrong answer instead of a diagnostic. MSVC's C4062 (an
/// unhandled enumerator in a switch) **is off at `/W4`**, so nothing warns.
/// The `static_assert` on this count beside that function is the only thing
/// that will stop the next variant - acacia, per finding 761 - from silently
/// inheriting an overshoot of 1 and poking its canopy through a ceiling it was
/// told was high enough.
enum class TreeVariant : std::uint8_t {
    Oak,
    Branching,
    Spruce,
    Pine,
    /// A straight jungle trunk under an oak-shaped canopy, vined all over and
    /// sometimes carrying cocoa.
    JungleSmall,
    /// The two-by-two giant. Up to thirty-one blocks in the reference; ours is
    /// scaled for a ninety-six block world like every other height here.
    JungleMega,
    /// One log and a ball of **oak** leaves - the reference's own choice, and
    /// the reason a jungle floor is not uniformly jungle-green.
    JungleBush,
    /// The savanna's forking tree. **The fork is the whole silhouette** - an
    /// acacia is not an oak in different wood, it is a diagonal trunk under a
    /// flat plate, and drawing it round was the visible half of finding 761.
    Acacia,
    /// Not a tree. The number of them, and never assigned to a `Tree`.
    Count,
};

/// Where a tree stands inside its cell, and what it is. Everything is derived
/// from the cell coordinates alone, so any chunk that asks about this cell gets
/// the same tree.
struct Tree {
    int x = 0;
    int z = 0;
    /// The whole height for a branching oak, the log column for everything else.
    int height = 0;
    TreeVariant variant = TreeVariant::Oak;
    /// Re-seeds `Roll` in `buildTree`, so shape draws are repeatable.
    std::uint32_t shapeSeed = 0;
    /// **Only ever set by a sapling.** Worldgen leaves both `Air` and the wood
    /// falls out of the variant exactly as it always did, so the world a seed
    /// produces is byte-for-byte what it was before saplings existed. A sapling
    /// knows its species and the variant does not - acacia and dark oak have no
    /// silhouette of their own here, and a birch sapling must not come up oak
    /// three times in four - so it says so instead.
    BlockId logOverride = BlockId::Air;
    BlockId leafOverride = BlockId::Air;
    /// **Worldgen only.** A sapling leaves this false, so a grown tree never
    /// carries a nest - the reference does give sapling trees one near flowers,
    /// but that needs a flower search around the sapling's own cell, which this
    /// struct is filled in nowhere near.
    bool beeNest = false;
};

/// Chance that a tree in this biome carries a bee nest, as a fraction of 1.
///
/// **Read off the reference's own table, not reproduced from memory.**
/// minecraft.wiki [[Bee Nest]] carries a per-biome table with separate Java and
/// Bedrock columns; it is stripped by every markdown conversion of the page, so
/// it was pulled as **raw wikitext** through the wiki's own API
/// (`action=parse&prop=wikitext`) on 2026-08-19. The **Bedrock** column reads:
/// meadow 100%; plains, sunflower plains and cherry grove 5%; mangrove swamp
/// 1%; flower forest 3% (Java 2%); and forest, birch forest, old growth birch
/// forest, wooded hills, birch forest hills and tall birch hills 0.035% (Java
/// 0.2%). The footnote on the column says it is "the chance for each
/// naturally-generated oak, birch, mangrove tree, or cherry tree to have a bee
/// nest", which is where the oak-family gate in `treeInCell` comes from.
///
/// **Corroborated a second time on the same page and independently of the
/// table**, by the Bedrock history section: beta 1.16.0.57 gives flower forest
/// 3%, plains and sunflower plains 5%, and "forest, wooded hills, birch forest,
/// tall birch forest, birch forest hills, and tall birch hills" 0.035%. Two
/// readings, one of them a changelog, agreeing on the three numbers that
/// matter here. Meadow's 100% is table-only - it spans both editions in one
/// cell - and is the one row to re-check first if the table is ever re-read.
///
/// **`Mojang/bedrock-samples` cannot settle it and it was asked**: worldgen
/// chances are engine-side, and its `behavior_pack/` has no `blocks/` directory
/// at all - the same wall `Farming.hpp`'s dripstone hits. Searching that repo
/// for `bee_nest` returns textures, `blocks.json` and `entities/bee.json`, and
/// nothing about generation.
///
/// Our biome list has no flower forest, cherry grove, sunflower plains or
/// mangrove swamp, and birch is a *variant roll inside oak* rather than a biome,
/// so the three rows below are every row we can express. `DenseForest` reads the
/// forest rate, and that is safe from either direction: every Bedrock biome in
/// the 0.035% row is a forest of some kind, so whichever of them it stands for,
/// the number is the same.
///
/// **This table's proper home is a column on `Biome`**, beside `treeDensity`,
/// and `Biome.cpp` says so in the note above `maxTreeDensity`. It is here
/// because `Biome.*` belongs to another owner; move it rather than copying it,
/// or this becomes the two-places-one-rule shape `CLAUDE.md` calls #14.
constexpr float beeNestChance(BiomeId biome) {
    switch (biome) {
    case BiomeId::Meadow:
        return 1.0f;
    case BiomeId::Plains:
        return 0.05f;
    case BiomeId::Forest:
    case BiomeId::DenseForest:
        return 0.00035f;
    default:
        return 0.0f;
    }
}

/// **CONTROL for the row above, and it is the mistake this table invites.**
/// Java's column sits beside Bedrock's on the same wiki page, one cell to the
/// left, and its forest rate is 0.2% - nearly six times ours. Reading the wrong
/// column produces a table that looks exactly as deliberate as the right one
/// and puts roughly six times as many nests in every forest in the world.
constexpr float beeNestChanceJava(BiomeId biome) {
    switch (biome) {
    case BiomeId::Meadow:
        return 1.0f;
    case BiomeId::Plains:
        return 0.05f;
    case BiomeId::Forest:
    case BiomeId::DenseForest:
        return 0.002f;
    default:
        return 0.0f;
    }
}

static_assert(beeNestChance(BiomeId::Forest) != beeNestChanceJava(BiomeId::Forest) &&
                  beeNestChance(BiomeId::Meadow) == beeNestChanceJava(BiomeId::Meadow),
              "the forest rate must be Bedrock's 0.035%, not Java's 0.2% - and the two editions "
              "genuinely agree on meadow, so a control that differed everywhere would be "
              "proving nothing about which column was read");
static_assert(beeNestChance(BiomeId::Desert) == 0.0f && beeNestChance(BiomeId::Jungle) == 0.0f &&
                  beeNestChance(BiomeId::Swamp) == 0.0f && beeNestChance(BiomeId::Taiga) == 0.0f,
              "a biome the reference gives no nest chance must get none here - the `default` is "
              "the whole of that rule and this is what stops a case label being added to it");

bool treeInCell(std::uint32_t seed, int cellX, int cellZ, Tree& out) {
    const float presence = noise::hashUnit2D(seed ^ 0x7ee50001u, cellX, cellZ);

    // Most cells are empty, and finding that out costs one hash. Sampling the
    // biome and surface height first would mean three noise evaluations per
    // candidate for a question already answered.
    if (presence >= maxTreeDensity()) {
        return false;
    }

    // Kept away from the cell edges so neighbouring trunks cannot collide across
    // a boundary. Canopies are free to overlap, and should: interpenetrating
    // crowns are most of what separates a forest from a plantation.
    constexpr int margin = 2;
    constexpr int span = kCellSize - 2 * margin;

    Roll roll{noise::hash2D(seed ^ 0x7ee50002u, cellX, cellZ)};

    out.x = cellX * kCellSize + margin + roll.range(span);
    out.z = cellZ * kCellSize + margin + roll.range(span);
    out.shapeSeed = roll.next();

    const BiomeSample sample = sampleBiome(seed, out.x, out.z);
    const Biome& biome = biomeInfo(sample.dominant);
    if (presence >= biome.treeDensity || biome.treeShape == TreeShape::None) {
        return false;
    }

    // Heights come from the one table above. The *ratios* inside each builder -
    // 0.618 for the trunk fraction, 0.381 for the branch slope - are
    // dimensionless and are deliberately not scaled with them.
    const auto drawHeight = [&roll](const HeightRange& range) {
        return range.low + roll.range(range.span);
    };

    if (biome.treeShape == TreeShape::Tall) {
        out.variant = roll.unit() < 0.33f ? TreeVariant::Pine : TreeVariant::Spruce;
        out.height = drawHeight(out.variant == TreeVariant::Pine ? kPineHeight : kSpruceHeight);
    } else if (biome.treeShape == TreeShape::Jungle) {
        // The reference's own `trees_jungle` mix, resolved: bush 45 %, mega
        // 15 %, fancy oak 10 %, and the small jungle tree taking the rest. The
        // bushes are what make a jungle floor impassable rather than shaded.
        const float pick = roll.unit();
        if (pick < 0.45f) {
            out.variant = TreeVariant::JungleBush;
            out.height = 1;
        } else if (pick < 0.60f) {
            out.variant = TreeVariant::JungleMega;
            out.height = drawHeight(kJungleMegaHeight);
        } else if (pick < 0.70f) {
            out.variant = TreeVariant::Branching;
            out.height = drawHeight(kBranchingHeight);
        } else {
            out.variant = TreeVariant::JungleSmall;
            out.height = drawHeight(kJungleSmallHeight);
        }
    } else if (biome.treeShape == TreeShape::Acacia) {
        // **The savanna, and the last link in finding 761.** The enumerator and
        // the `Savanna` row landed first and were a deliberate no-op until this
        // branch existed - without it `Acacia` fell into the bare `else` below
        // and drew exactly the oaks it drew before, which is the failure mode
        // where a change looks landed and changes nothing.
        //
        // **SECONDARY, ratio included.** Worldgen composition is not published
        // any more than tree shape is, and the reference's savanna is *not*
        // pure acacia - it carries oaks too, which is what stops a savanna
        // reading as an orchard of one model. 4 in 5 is matched by eye against
        // <https://minecraft.wiki/w/Savanna>, read 2026-08-19, and is stated as
        // a guess rather than dressed up as data.
        //
        // > Fails if: someone finds a published `trees_savanna` weight. Then
        // > this number is wrong and should be replaced, not defended.
        out.variant = roll.unit() < 0.20f ? TreeVariant::Oak : TreeVariant::Acacia;
        out.height = drawHeight(out.variant == TreeVariant::Oak ? kOakHeight : kAcaciaHeight);
    } else if (biome.treeShape == TreeShape::Round) {
        out.variant = roll.unit() < 0.12f ? TreeVariant::Branching : TreeVariant::Oak;
        out.height = drawHeight(out.variant == TreeVariant::Branching ? kBranchingHeight
                                                                     : kOakHeight);
    } else {
        // **This was a bare `else` drawing oaks, which is `CLAUDE.md` bug shape
        // #10** - a catch-all returning a real value. `Round` is named above
        // now, so the only thing that reaches here is a `TreeShape` nobody gave
        // a branch. Growing nothing is the loud answer: a biome that is
        // conspicuously bald is a bug someone reports in the first minute,
        // where a biome quietly full of the wrong tree is the shape that hid
        // acacia for twenty milestones. There is no `TreeShape::Count` to
        // `static_assert` against - the enum lives in `Biome.hpp` and has no
        // sentinel - so this runtime silence is the guard available.
        return false;
    }

    // Nothing grows in the sea.
    const int surface = surfaceHeightAt(seed, out.x, out.z);
    if (surface <= kSeaLevel + 1) {
        return false;
    }

    // The ground it would stand on has to still be there. A cave mouth removes
    // it, and the tree used to be built anyway - trunk, canopy and the dirt
    // block that roots it, all hanging over the hole.
    if (surfaceCarvedAt(seed, out.x, out.z)) {
        return false;
    }

    // The reference gates every tree on `would_survive`, which is a test on the
    // block below. We cannot see that block from here, so the case that
    // actually matters is tested directly: a **steep** face, where the surface
    // rules deliberately put bare rock and a tree would be standing on a cliff.
    //
    // Blocks of **absolute** height difference across a **single** column, asked
    // of each of the four neighbours: a step of 4 or more refuses the tree, so
    // ground that climbs up to 3 blocks per side still gets one.
    //
    // **Deliberately not the generator's `kSteepDropOverTwoColumns`, and reading
    // that one instead would be a bug rather than tidiness.** Its span is two
    // columns and its form is signed - north against south, one facing - so its
    // 4 means an average of two blocks per column and fires on one side only.
    // Borrowing the figure into this four-way one-column absolute test silently
    // halves the footing trees have always had. Two numbers, two spans, two
    // forms; the fix for the old duplication is that each name now says which.
    //
    // **Nothing here is calibrated against how often that one fires.** This test
    // reads `surfaceHeightAt`, and the generator's steep rule only chooses which
    // *block* tops a column, never how tall it is - so the 2 -> 4 re-derivation
    // moved no tree by a single block. It did close a gap in the prose above:
    // at 2-over-two-columns the generator called a one-block-per-column slope
    // bare rock while a tree needed four, so trees stood on rock routinely. At
    // 4 the two disagree over half the range they used to.
    constexpr int kTreeSteepNeighbourStep = 4;
    if (!(std::abs(surfaceHeightAt(seed, out.x - 1, out.z) - surface) < kTreeSteepNeighbourStep &&
          std::abs(surfaceHeightAt(seed, out.x + 1, out.z) - surface) < kTreeSteepNeighbourStep &&
          std::abs(surfaceHeightAt(seed, out.x, out.z - 1) - surface) < kTreeSteepNeighbourStep &&
          std::abs(surfaceHeightAt(seed, out.x, out.z + 1) - surface) < kTreeSteepNeighbourStep)) {
        return false;
    }

    // A bee nest, last and from **its own hash**. Drawing it from `roll` would
    // advance the shared sequence and change the variant, height and shape of
    // every tree in every existing world - so the salt is a fresh one and this
    // runs after the last rejection, where it cannot be reached by a cell that
    // grows nothing. With `beeNestChance` returning 0 for every biome but three,
    // a world's trees are byte-for-byte what they were before this field
    // existed everywhere else, and the three that change were nestless anyway.
    //
    // **Oak family only.** The reference puts nests on oak, birch, cherry and
    // mangrove; of those we have oak, and birch is a *shapeSeed* roll inside it
    // rather than a variant of its own. The biome gate already implies this
    // today, because all three nest-bearing biomes carry `TreeShape` default -
    // the variant test is here so that giving `Forest` a tall shape later moves
    // a nest off a spruce instead of silently putting one on it.
    if (out.variant == TreeVariant::Oak || out.variant == TreeVariant::Branching) {
        const float chance = beeNestChance(sample.dominant);
        out.beeNest = chance > 0.0f && noise::hashUnit2D(seed ^ 0x7ee50003u, cellX, cellZ) < chance;
    }
    return true;
}

/// Where `buildTree` writes. **Two of these exist and that is why it is a
/// template**: worldgen writes straight into the chunk it is generating, and a
/// sapling collects the same blocks into a list for the world to apply on the
/// main thread. One tree builder, two destinations - because a tree grown from
/// a sapling and a tree grown by the generator that were two functions would
/// drift apart, which is the mistake `CLAUDE.md` calls "a rule that did not
/// travel" and this project has already paid for twice.
///
/// Duck-typed rather than virtual: the sink is a template parameter, so there
/// is no indirect call in the inner loop and no base class to inherit.
///
/// The chunk sink. Writes every block that falls inside this chunk and drops
/// the rest, which is what lets the same tree be built by several chunks.
struct ChunkSink {
    Chunk& chunk;
    ChunkCoord coord;

    void place(int worldX, int worldY, int worldZ, BlockId block, bool onlyIntoAir) {
        const int lx = worldX - coord.x * Chunk::kSize;
        const int ly = worldY - coord.y * Chunk::kSize;
        const int lz = worldZ - coord.z * Chunk::kSize;

        if (!Chunk::contains(lx, ly, lz)) {
            return;
        }
        if (onlyIntoAir && chunk.at(lx, ly, lz) != BlockId::Air) {
            return;
        }
        chunk.set(lx, ly, lz, block);
    }

    /// Adds one side to whatever vine already stands here, or starts a new one.
    ///
    /// **Merging rather than overwriting is what lets a cell between two trunks
    /// cling to both.** Sixteen ids exist for exactly that, and writing a fresh
    /// single-sided vine over an existing one would throw half of them away.
    void vine(int worldX, int worldY, int worldZ, std::uint8_t side) {
        const int lx = worldX - coord.x * Chunk::kSize;
        const int ly = worldY - coord.y * Chunk::kSize;
        const int lz = worldZ - coord.z * Chunk::kSize;
        if (!Chunk::contains(lx, ly, lz)) {
            return;
        }
        const BlockId there = chunk.at(lx, ly, lz);
        if (there == BlockId::Air) {
            chunk.set(lx, ly, lz, vineWith(side));
        } else if (isVine(there)) {
            chunk.set(lx, ly, lz, vineWith(static_cast<std::uint8_t>(vineSides(there) | side)));
        }
    }
};

/// The list sink. Collects the tree instead of writing it, so `World` can check
/// the plan against the real world and either apply the whole thing or throw it
/// away - which is the "compute the result, then apply the result" split, and
/// the reason a sapling can never leave half a tree behind.
///
/// **The lookup is a linear scan on purpose.** A tree is two or three hundred
/// blocks, this runs once when a sapling grows, and a hash map here would be an
/// allocation and a rehash to save microseconds that nobody is waiting on.
struct ListSink {
    std::vector<PlannedBlock>& out;

    std::size_t find(int x, int y, int z) const {
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (out[i].x == x && out[i].y == y && out[i].z == z) {
                return i;
            }
        }
        return out.size();
    }

    void place(int worldX, int worldY, int worldZ, BlockId block, bool onlyIntoAir) {
        const std::size_t at = find(worldX, worldY, worldZ);
        if (at != out.size()) {
            // The chunk sink's rule, on the plan rather than on the world: a
            // leaf laid `onlyIntoAir` must not paint over a log this same tree
            // already put here, or the trunk comes out hollow.
            if (onlyIntoAir) {
                return;
            }
            out[at].block = block;
            out[at].onlyIntoAir = false;
            return;
        }
        out.push_back(PlannedBlock{worldX, worldY, worldZ, block, onlyIntoAir});
    }

    void vine(int worldX, int worldY, int worldZ, std::uint8_t side) {
        const std::size_t at = find(worldX, worldY, worldZ);
        if (at == out.size()) {
            out.push_back(PlannedBlock{worldX, worldY, worldZ, vineWith(side), true});
            return;
        }
        if (isVine(out[at].block)) {
            out[at].block = vineWith(static_cast<std::uint8_t>(vineSides(out[at].block) | side));
        }
    }
};

/// Wraps a jungle tree in vines, and hangs cocoa on the small one.
///
/// **Computed from the tree's own numbers rather than by looking at what was
/// built**, because a tree straddling a chunk border is built once per chunk
/// and each of those only sees its own cells. Every draw comes from the shared
/// `Roll` in a fixed order, so both chunks lay the same vines.
template <typename Sink>
void dressJungleTree(Sink& sink, const Tree& tree, int base, Roll& roll) {
    const bool mega = tree.variant == TreeVariant::JungleMega;
    const int span = mega ? 1 : 0;
    const int top = base + tree.height;

    // **Cocoa goes on before the vines**, because a vine already in the cell
    // refuses the pod and the trunk is 75 % vined - putting this second meant
    // no jungle tree in the world carried a single pod.
    //
    // Cocoa is the small tree's alone: the reference does not put it on a
    // giant, and a sapling-grown tree gets none either. A fifth of trees carry
    // any at all, and on those each face of the lowest three logs is its own
    // quarter chance.
    if (!mega && roll.unit() < 0.2f) {
        for (int y = base + 1; y <= base + 3; ++y) {
            struct Face {
                int dx;
                int dz;
                FaceDirection facing;
            };
            // The facing is the side the **log** is on, which is the opposite
            // of the offset the pod sits at.
            constexpr std::array<Face, 4> faces{{{-1, 0, FaceDirection::PosX},
                                                 {1, 0, FaceDirection::NegX},
                                                 {0, -1, FaceDirection::PosZ},
                                                 {0, 1, FaceDirection::NegZ}}};
            for (const Face& face : faces) {
                if (roll.unit() >= 0.25f) {
                    continue;
                }
                sink.place(tree.x + face.dx, y, tree.z + face.dz,
                           cocoaAt(face.facing, roll.range(3)), true);
            }
        }
    }

    // The reference's `trunk_vine`: a vine on **each side of each trunk block
    // at 75 %**. Implemented as anything less than that leaves jungle trees
    // looking bald - it is a sheath, not a garnish.
    for (int y = base + 1; y <= top; ++y) {
        for (int dz = 0; dz <= span; ++dz) {
            for (int dx = 0; dx <= span; ++dx) {
                const int x = tree.x + dx;
                const int z = tree.z + dz;
                if (dx == 0 && roll.unit() < 0.75f) {
                    sink.vine(x - 1, y, z, ConnectEast);
                }
                if (dx == span && roll.unit() < 0.75f) {
                    sink.vine(x + 1, y, z, ConnectWest);
                }
                if (dz == 0 && roll.unit() < 0.75f) {
                    sink.vine(x, y, z - 1, ConnectSouth);
                }
                if (dz == span && roll.unit() < 0.75f) {
                    sink.vine(x, y, z + 1, ConnectNorth);
                }
            }
        }
    }

    // The reference's `leave_vine`: a quarter of the canopy's outer positions
    // get a vine, and **each one is extended up to four blocks downward** -
    // which is the whole of why a jungle has curtains rather than fringes.
    const int radius = mega ? 3 : 2;
    for (int row = 0; row < 2; ++row) {
        const int y = top - row;
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (std::abs(dx) != radius && std::abs(dz) != radius) {
                    continue;
                }
                if (roll.unit() >= 0.25f) {
                    continue;
                }
                // Which side of its own cell the vine hangs on: the one facing
                // back toward the canopy it is attached to.
                std::uint8_t side = ConnectNorth;
                if (std::abs(dx) >= std::abs(dz)) {
                    side = dx > 0 ? ConnectWest : ConnectEast;
                } else {
                    side = dz > 0 ? ConnectNorth : ConnectSouth;
                }
                for (int drop = 0; drop < 5; ++drop) {
                    sink.vine(tree.x + dx, y - drop, tree.z + dz, side);
                }
            }
        }
    }
}

template <typename Sink>
void buildTree(Sink& sink, int base, const Tree& tree) {
    Roll roll{tree.shapeSeed};

    // Species wood. Conifers are spruce and the branching oaks stay oak; birch
    // rides on the plain round tree, so a forest is a mix rather than one model
    // repeated. The blocks are chosen once here and every placement below uses
    // them, or half a tree would come out the wrong wood.
    const bool conifer =
        tree.variant == TreeVariant::Spruce || tree.variant == TreeVariant::Pine;
    const bool jungle = tree.variant == TreeVariant::JungleSmall ||
                        tree.variant == TreeVariant::JungleMega ||
                        tree.variant == TreeVariant::JungleBush;
    const bool birch = tree.variant == TreeVariant::Oak && (tree.shapeSeed & 3u) == 0u;
    // **Acacia is the first variant whose wood is not a re-skin of oak.** It is
    // tested before the others below only because it must not fall through to
    // `BlockId::Log`; the order carries no other meaning.
    const bool acacia = tree.variant == TreeVariant::Acacia;
    const BlockId variantLog = acacia   ? BlockId::AcaciaLog
                               : conifer  ? BlockId::SpruceLog
                               : jungle ? BlockId::JungleLog
                               : birch  ? BlockId::BirchLog
                                        : BlockId::Log;
    // **A jungle bush wears oak leaves**, which is the reference's own choice
    // and not an oversight: it is what stops a jungle floor being one flat
    // green. Only the two real jungle trees carry jungle leaves.
    const BlockId variantLeaf = acacia ? BlockId::AcaciaLeaves
                                : conifer                                ? BlockId::SpruceLeaves
                                : tree.variant == TreeVariant::JungleSmall ||
                                          tree.variant == TreeVariant::JungleMega
                                    ? BlockId::JungleLeaves
                                : birch ? BlockId::BirchLeaves
                                        : BlockId::Leaves;
    // A sapling knows its species outright; worldgen leaves both `Air` and gets
    // exactly the wood it always got.
    const BlockId logBlock = tree.logOverride == BlockId::Air ? variantLog : tree.logOverride;
    const BlockId leafBlock = tree.leafOverride == BlockId::Air ? variantLeaf : tree.leafOverride;

    // The lowest leaf the tree writes, tracked in the one funnel every variant
    // and every helper - `disc`, `blob`, `cluster` - already goes through, so
    // the bee nest below needs no per-variant arithmetic and cannot drift when
    // a seventh canopy shape arrives.
    //
    // **Chunk-independent on purpose.** `leaf` is called for every canopy
    // position and `sink.place` is what drops the ones outside this chunk, so
    // this minimum is a property of the tree rather than of who is looking at
    // it. A tree straddling a border therefore hangs its nest in the same
    // block from both sides - which is the same property `treeInCell`'s purity
    // buys, and the reason the two chunks agree at all.
    int lowestLeafY = std::numeric_limits<int>::max();
    const auto leaf = [&](int x, int y, int z) {
        lowestLeafY = std::min(lowestLeafY, y);
        sink.place(x, y, z, leafBlock, true);
    };

    /// One squat layer of leaves. `cut` is rolled per corner rather than fixed,
    /// which is the single cheapest thing that stops two canopies being the
    /// same shape.
    const auto disc = [&](int cx, int y, int cz, int radius, bool cutCorners) {
        for (int dz = -radius; dz <= radius; ++dz) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (cutCorners && std::abs(dx) == radius && std::abs(dz) == radius &&
                    radius > 0 && roll.range(2) == 0) {
                    continue;
                }
                leaf(cx + dx, y, cz + dz);
            }
        }
    };

    /// The reference's blob: two wide layers, two narrow, a plus on top.
    const auto blob = [&](int cx, int top, int cz, int radius) {
        disc(cx, top - 2, cz, radius, true);
        disc(cx, top - 1, cz, radius, true);
        disc(cx, top, cz, radius - 1, false);
        for (int d = -1; d <= 1; ++d) {
            leaf(cx + d, top + 1, cz);
            leaf(cx, top + 1, cz + d);
        }
    };

    /// Foliage carried by a branch, sized to how far that branch actually went.
    ///
    /// **A short branch gets a sheath, not a canopy.** The reference hangs
    /// full-size clusters off trees up to 16 blocks tall, so a cluster always
    /// lands well clear of the trunk and of its neighbours. Ours are a third
    /// that height, so anything more than enough leaves to cover the log reads
    /// as a second crown bolted onto the side of the first.
    const auto cluster = [&](int cx, int cy, int cz, int reach) {
        if (reach >= 3) {
            blob(cx, cy, cz, 2);
            return;
        }
        for (int d = -1; d <= 1; ++d) {
            leaf(cx + d, cy, cz);
            leaf(cx, cy, cz + d);
        }
        leaf(cx, cy + 1, cz);
        leaf(cx, cy - 1, cz);
    };

    /// The reference's line rasteriser, used for branches. Stepping the longest
    /// axis is what keeps a diagonal branch connected instead of dotted.
    const auto branch = [&](int x0, int y0, int z0, int x1, int y1, int z1) {
        const int dx = x1 - x0;
        const int dy = y1 - y0;
        const int dz = z1 - z0;
        const int steps = std::max(std::max(std::abs(dx), std::abs(dy)), std::abs(dz));
        for (int i = 0; i <= steps; ++i) {
            const float t = steps == 0 ? 0.0f : static_cast<float>(i) / static_cast<float>(steps);
            sink.place(x0 + static_cast<int>(std::lround(dx * t)),
                  y0 + static_cast<int>(std::lround(dy * t)),
                  z0 + static_cast<int>(std::lround(dz * t)), logBlock, false);
        }
    };

    switch (tree.variant) {
    case TreeVariant::Oak: {
        const int top = base + tree.height;
        blob(tree.x, top, tree.z, 2);
        for (int y = base + 1; y <= top; ++y) {
            sink.place(tree.x, y, tree.z, logBlock, false);
        }
        break;
    }

    case TreeVariant::Branching: {
        // The reference's fancy oak: the bare trunk is 0.618 of the whole, and
        // clusters hang off branches that rise at 0.381 and never start below
        // 30% of the height. Those three ratios are the shape.
        const int trunkTop = base + std::max(3, static_cast<int>(tree.height * 0.618f));
        // Scaled to the height, because the number of places a cluster can go
        // without landing on top of another one is what the height buys.
        const int clusters = 1 + roll.range(1 + (tree.height - 5) / 2);

        blob(tree.x, base + tree.height, tree.z, 2);

        for (int i = 0; i < clusters; ++i) {
            const float angle = roll.unit() * 6.2831853f;
            const float reach = (0.328f + roll.unit()) * static_cast<float>(tree.height) * 0.30f;
            const int cx = tree.x + static_cast<int>(std::lround(std::sin(angle) * reach));
            const int cz = tree.z + static_cast<int>(std::lround(std::cos(angle) * reach));
            const int lowest = base + static_cast<int>(tree.height * 0.3f);
            const int cy = lowest + roll.range(std::max(1, base + tree.height - lowest));

            const int spread = std::max(std::abs(cx - tree.x), std::abs(cz - tree.z));
            if (spread == 0) {
                // Landed on the trunk. There is no branch to draw and the crown
                // already covers it, so foliage here is pure thickening.
                continue;
            }

            // Where the branch meets the trunk, from the reference's slope.
            const float run = std::sqrt(static_cast<float>((cx - tree.x) * (cx - tree.x) +
                                                           (cz - tree.z) * (cz - tree.z)));
            const int attach = std::min(trunkTop, cy - static_cast<int>(run * 0.381f));

            branch(tree.x, std::max(base + 1, attach), tree.z, cx, cy, cz);
            cluster(cx, cy + 1, cz, spread);
        }

        for (int y = base + 1; y <= trunkTop; ++y) {
            sink.place(tree.x, y, tree.z, logBlock, false);
        }
        break;
    }

    case TreeVariant::Spruce: {
        // The saw-tooth skirt, straight from the reference: the radius climbs to
        // a cap, drops back, and the cap grows one step each time. That single
        // rule is what makes a spruce read as tiered rather than as a cone.
        const int bare = 1 + roll.range(2);
        const int maxRadius = 2 + roll.range(2);
        const int rows = tree.height - bare;

        int radius = roll.range(2);
        int cap = 1;
        int carry = 0;
        for (int row = 0; row <= rows; ++row) {
            disc(tree.x, base + tree.height - row, tree.z, radius, radius > 0);
            if (radius >= cap) {
                radius = carry;
                carry = 1;
                cap = std::min(cap + 1, maxRadius);
            } else {
                ++radius;
            }
        }

        // **The bare tip is drawn once, not per iteration.** Written into the
        // loop condition it drew a fresh number every step - deterministic
        // today, because nothing after it reads the stream, but the shape of
        // fault that turns a one-line addition into a forest that changes when
        // you look away.
        const int bareTip = roll.range(2);
        for (int y = base + 1; y <= base + tree.height - bareTip; ++y) {
            sink.place(tree.x, y, tree.z, logBlock, false);
        }
        break;
    }

    case TreeVariant::Pine: {
        // A bare pole under a flat crown, which is the reference's other conifer
        // and the reason a taiga is not all one silhouette.
        const int bare = std::max(2, tree.height - roll.range(2) - 3);
        const int maxRadius = 1 + roll.range(tree.height - bare + 1);

        int radius = 0;
        for (int y = base + tree.height; y >= base + bare; --y) {
            disc(tree.x, y, tree.z, radius, radius > 0);
            if (radius >= 1 && y == base + bare + 1) {
                --radius;
            } else if (radius < maxRadius) {
                ++radius;
            }
        }

        for (int y = base + 1; y <= base + tree.height - 1; ++y) {
            sink.place(tree.x, y, tree.z, logBlock, false);
        }
        break;
    }

    case TreeVariant::Acacia: {
        // **SECONDARY source, and stated as such deliberately.** Worldgen tree
        // shape is *not published* - `Mojang/bedrock-samples` carries no tree
        // definitions in its behaviour pack, so unlike the collision boxes or
        // `honey_level` there is no JSON to port and nothing here is a ported
        // number. These are mapped from the reference's silhouette as drawn in
        // `reference/minecraft-assets-26.2` and described at
        // <https://minecraft.wiki/w/Tree#Acacia>, read 2026-08-19. Treat them
        // as a shape someone matched by eye, because that is what they are.
        //
        // What actually makes an acacia an acacia: **the trunk forks off the
        // vertical and the canopy is a flat plate.** A round crown on a
        // straight trunk in acacia wood is not a savanna tree - that was the
        // visible half of finding 761, and the reason this is a placer rather
        // than a `logOverride`.
        //
        //   bare    half the height, upright, before the fork
        //   arm     the remainder, run diagonally 1:1 so the tip lands at
        //           `base + height` and the plate sits above it
        //   arms    1 or 2; a second is turned 90 degrees so they never overlay
        //   plate   radius-2 slab with a radius-1 cap - **two layers, never
        //           three**, because a third reads as a round crown again and
        //           undoes the entire point of the variant
        const int bare = std::max(1, tree.height / 2);
        const int tipY = base + tree.height;
        const int run = std::max(1, tipY - (base + bare));
        const int arms = 1 + roll.range(2);

        for (int y = base + 1; y <= base + bare; ++y) {
            sink.place(tree.x, y, tree.z, logBlock, false);
        }

        const int quarter = roll.range(4);
        for (int a = 0; a < arms; ++a) {
            // The four diagonals, as a pair of signs. A second arm takes the
            // next quarter round, which is a right angle from the first.
            const int dir = (quarter + a) & 3;
            const int dx = (dir == 0 || dir == 1) ? 1 : -1;
            const int dz = (dir == 0 || dir == 3) ? 1 : -1;
            const int tipX = tree.x + dx * run;
            const int tipZ = tree.z + dz * run;

            branch(tree.x, base + bare, tree.z, tipX, tipY, tipZ);
            disc(tipX, tipY + 1, tipZ, 2, true);
            disc(tipX, tipY + 2, tipZ, 1, false);
        }
        break;
    }

    case TreeVariant::JungleBush: {
        // One log and a ball of oak leaves, which is the whole feature.
        sink.place(tree.x, base + 1, tree.z, logBlock, false);
        disc(tree.x, base + 2, tree.z, 2, true);
        disc(tree.x, base + 3, tree.z, 1, false);
        break;
    }

    case TreeVariant::JungleSmall:
    case TreeVariant::JungleMega: {
        const bool mega = tree.variant == TreeVariant::JungleMega;
        const int top = base + tree.height;
        // **A giant's trunk is two by two.** That is the whole of what separates
        // the two here, and it is what the vines then have to wrap round.
        const int span = mega ? 1 : 0;

        for (int dz = 0; dz <= span; ++dz) {
            for (int dx = 0; dx <= span; ++dx) {
                for (int y = base + 1; y <= top - (mega ? 1 : 0); ++y) {
                    sink.place(tree.x + dx, y, tree.z + dz, logBlock, false);
                }
                sink.place(tree.x + dx, base, tree.z + dz, BlockId::Dirt, false);
            }
        }

        if (mega) {
            // The reference caps the giant with a single block and hangs
            // branches off the top half, each carrying its own foliage.
            sink.place(tree.x, top, tree.z, logBlock, false);
            const int branches = 1 + roll.range(3);
            for (int i = 0; i < branches; ++i) {
                const float angle = roll.unit() * 6.2831853f;
                const int reach = 2 + roll.range(2);
                const int bx = tree.x + static_cast<int>(std::lround(std::sin(angle) * reach));
                const int bz = tree.z + static_cast<int>(std::lround(std::cos(angle) * reach));
                const int by = top - 2 - roll.range(std::max(1, tree.height / 3));
                branch(tree.x, by, tree.z, bx, by + 1, bz);
                blob(bx, by + 2, bz, 2);
            }
            // Rounder and wider than a small tree's crown: radii 3, 2, 1 rather
            // than the blob's 2, 2, 1.
            disc(tree.x, top, tree.z, 3, true);
            disc(tree.x, top + 1, tree.z, 2, true);
            disc(tree.x, top + 2, tree.z, 1, false);
        } else {
            blob(tree.x, top, tree.z, 2);
        }
        break;
    }
    }

    // Roots the tree, so it never appears to float over a one-block dip.
    sink.place(tree.x, base, tree.z, BlockId::Dirt, false);

    // Vines and cocoa, which only the two real jungle trees carry. A bush gets
    // neither, and neither does anything outside a jungle.
    if (tree.variant == TreeVariant::JungleSmall || tree.variant == TreeVariant::JungleMega) {
        dressJungleTree(sink, tree, base, roll);
    }

    // A bee nest, hung under the south edge of the canopy.
    //
    // **Level 0 and that is not an oversight.** A natural nest generates empty
    // and the bees living in it fill it, one honey level per pollinated bee
    // coming home. **Do not "fix" this to 5 to make honeycomb obtainable**: it
    // would work, and it would invert the mechanic - a hive that is full the
    // moment it is generated makes the bee decorative and the whole cycle
    // pointless, and it is a one-character edit that no build or checker can
    // catch. `Creature.cpp`'s `Pollinate` is what fills it; `Main.cpp` drains
    // `takePollinated` and applies the level.
    //
    // **Facing south is Bedrock's, the offset below is not.** minecraft.wiki
    // [[Bee Nest]] says in unmarked prose - so both editions - that natural
    // nests "always face south", which is `PosZ` under the usual convention.
    // That the nest hangs one block under the lowest leaf is from the Java
    // decorator and is **unverified for Bedrock**, as `Biome.cpp` already
    // flagged; it is the cheapest rule that puts a nest where a player sees it,
    // and it is the line to change if the reference is ever read properly.
    //
    // **The reference's "generates with 2-3 bees in them" is not expressed
    // here and cannot be.** A nest holds no occupants in this game - there is
    // no inside - so its bees are whatever the ordinary spawner puts nearby,
    // and `Creature.cpp`'s hive search is what pairs them up. The visible
    // difference is that a freshly generated nest may sit unattended until a
    // bee wanders within its sixteen blocks.
    //
    // **The reference also refuses to generate a nest whose front face is
    // blocked, and that rule is deliberately not ported.** Reading the cell in
    // front means reading a column this chunk may not own, and a generator that
    // asks a neighbour a question is a generator whose answer depends on which
    // chunk ran first - the one thing this file may never do.
    //
    // `true` keeps it out of anything already written - its own trunk included,
    // since a canopy that reaches the ground leaves no air to hang in.
    if (tree.beeNest && lowestLeafY != std::numeric_limits<int>::max()) {
        sink.place(tree.x, lowestLeafY - 1, tree.z + 1,
                   beeNestAtLevel(FaceDirection::PosZ, 0), true);
    }
}

} // namespace

void generateInto(Chunk& chunk, std::uint32_t seed, ChunkCoord coord,
                  const village::Nearby& villages) {
    const int baseX = coord.x * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    // Every cell whose structure could reach into this chunk. The margin is what
    // makes a tree straddling the border come out identical from both sides.
    //
    // **The last cell is taken from the last column, not from one past it.**
    // `baseX + Chunk::kSize` is the first column of the *next* chunk, so the
    // old form scanned one extra cell column whenever a chunk ended flush with a
    // cell boundary - harmless, because out-of-range writes are dropped, but it
    // read as though a tree could reach a block further than `kReach` says, and
    // `kReach` is now asserted against the canopies that actually justify it.
    //
    // These are `int` and `baseX` is `coord.x * Chunk::kSize`, so this is sound
    // for any chunk coordinate the world can hold. The bound on that comes from
    // whatever clamps the spawn setting, which is another owner's; nothing here
    // adds slack of its own.
    const int lastX = baseX + Chunk::kSize - 1;
    const int lastZ = baseZ + Chunk::kSize - 1;
    const int firstCellX = floorDivInt(baseX - kReach, kCellSize);
    const int lastCellX = floorDivInt(lastX + kReach, kCellSize);
    const int firstCellZ = floorDivInt(baseZ - kReach, kCellSize);
    const int lastCellZ = floorDivInt(lastZ + kReach, kCellSize);

    for (int cellZ = firstCellZ; cellZ <= lastCellZ; ++cellZ) {
        for (int cellX = firstCellX; cellX <= lastCellX; ++cellX) {
            Tree tree;
            if (!treeInCell(seed, cellX, cellZ, tree)) {
                continue;
            }
            // A tree standing in a village grows through a roof. The test is
            // pure, so both chunks either drop it or keep it.
            if (village::occupies(villages, tree.x, tree.z)) {
                continue;
            }
            ChunkSink sink{chunk, coord};
            buildTree(sink, surfaceHeightAt(seed, tree.x, tree.z), tree);
        }
    }
}

// ---------------------------------------------------------------------------
// Saplings. The same builder, a different destination.
// ---------------------------------------------------------------------------

namespace {

/// What each variant draws **above** its trunk top, so the clearance a plan
/// asks for is derived from the shape rather than guessed beside it.
///
/// > Fails if: change a `blob` or a top `disc` in `buildTree` without changing
/// > this, and a grown tree starts poking through the ceiling it was told was
/// > high enough.
int canopyOvershoot(TreeVariant variant) {
    switch (variant) {
    case TreeVariant::Oak:
    case TreeVariant::Branching:
    case TreeVariant::JungleSmall:
    case TreeVariant::JungleBush:
        // `blob` finishes with a plus at `top + 1`.
        return 1;
    case TreeVariant::Spruce:
    case TreeVariant::Pine:
        // The topmost `disc` sits on `top` itself.
        return 0;
    case TreeVariant::JungleMega:
        // Discs at `top`, `top + 1` and `top + 2`.
        return 2;
    case TreeVariant::Acacia:
        // The plate sits above the arm tip, and the arm tip is `base + height`
        // by construction: `disc` at `top + 1` and a cap at `top + 2`.
        return 2;
    case TreeVariant::Count:
        break;
    }
    return 1;
}

/// **The trailing `return 1;` above is a `CLAUDE.md` #10 catch-all and this is
/// what stops it hiding a missing row.** A new `TreeVariant` that nobody gave a
/// `case` here does not warn - MSVC's C4062 is off at `/W4` - it silently takes
/// an overshoot of 1, and the doc comment on this function already names the
/// symptom: a grown tree poking through the ceiling it was told was high
/// enough. Bumping the enum without visiting this function is the whole bug, so
/// the count is pinned and the next variant has to come here first.
///
/// **This assert lives in a `.cpp`, not a header, so its blast radius is one
/// translation unit** - the cheapest kind there is.
///
/// > Fails if: giant spruce is added (finding 761, the remaining half). That
/// > failure is the point. Add the `case`, then raise this number.
/// >
/// > **Acacia landed 2026-08-19 and this count went 7 -> 8.** The assert did
/// > exactly its job: it named `canopyOvershoot`, `buildTree` and the leaf
/// > selection, and all three were visited before this line was touched.
static_assert(static_cast<int>(TreeVariant::Count) == 8,
              "a TreeVariant was added or removed - give it a case in canopyOvershoot and in "
              "buildTree, check the conifer/jungle/acacia tests near the leaf selection, then "
              "update this count. See finding 761.");

/// The horizontal half-width the reference clears for each species, in blocks
/// from the trunk. **These are the reference's own published footprints** -
/// oak, birch and jungle 3x3, spruce and acacia 5x5, and the two-by-two forms
/// 6x6 - rather than the full canopy radius, because the reference lets a
/// canopy grow into leaves and hillsides and only guards the trunk's own box.
/// <https://minecraft.wiki/w/Sapling#Growth>
int clearRadiusFor(BlockId sapling, bool twoByTwo) {
    if (twoByTwo) {
        // 6x6 around a 2x2 trunk: two blocks out on each side.
        return 2;
    }
    switch (sapling) {
    case BlockId::SpruceSapling:
    case BlockId::AcaciaSapling:
        return 2;
    default:
        return 1;
    }
}

} // namespace

bool saplingHasGiantForm(BlockId sapling) {
    return sapling == BlockId::SpruceSapling || sapling == BlockId::JungleSapling ||
           sapling == BlockId::DarkOakSapling;
}

bool saplingNeedsGiantForm(BlockId sapling) { return sapling == BlockId::DarkOakSapling; }

SaplingPlan planSaplingTree(BlockId sapling, int x, int y, int z, std::uint32_t rollSeed,
                            bool twoByTwo) {
    SaplingPlan plan;
    if (saplingNeedsGiantForm(sapling) && !twoByTwo) {
        return plan;
    }
    if (twoByTwo && !saplingHasGiantForm(sapling)) {
        return plan;
    }

    Roll roll{rollSeed};

    Tree tree;
    tree.x = x;
    tree.z = z;
    tree.shapeSeed = rollSeed;

    // Species and silhouette. **The height is drawn before anything is
    // checked**, which is the reference's order and the reason a sapling under
    // a low ceiling grows on some attempts and not others.
    switch (sapling) {
    case BlockId::OakSapling:
        // The reference's oak sapling picks the fancy shape one time in ten.
        // <https://minecraft.wiki/w/Tree#Oak>
        tree.variant = roll.range(10) == 0 ? TreeVariant::Branching : TreeVariant::Oak;
        tree.height = tree.variant == TreeVariant::Branching
                          ? kBranchingHeight.low + roll.range(kBranchingHeight.span)
                          : kOakHeight.low + roll.range(kOakHeight.span);
        tree.logOverride = BlockId::Log;
        tree.leafOverride = BlockId::Leaves;
        break;
    case BlockId::BirchSapling:
        tree.variant = TreeVariant::Oak;
        tree.height = kOakHeight.low + roll.range(kOakHeight.span);
        tree.logOverride = BlockId::BirchLog;
        tree.leafOverride = BlockId::BirchLeaves;
        break;
    case BlockId::SpruceSapling:
        if (twoByTwo) {
            // No giant-spruce silhouette exists here, so the two-by-two trunk
            // and wide crown come from the giant jungle shape wearing spruce.
            // Filed as a finding: the reference's giant spruce is a distinct
            // trunk placer with a long tapering skirt.
            tree.variant = TreeVariant::JungleMega;
            tree.height = kJungleMegaHeight.low + roll.range(kJungleMegaHeight.span);
        } else {
            // Taiga's two conifers, which is what stops a planted forest being
            // one silhouette repeated.
            tree.variant = roll.range(2) == 0 ? TreeVariant::Spruce : TreeVariant::Pine;
            tree.height = tree.variant == TreeVariant::Spruce
                              ? kSpruceHeight.low + roll.range(kSpruceHeight.span)
                              : kPineHeight.low + roll.range(kPineHeight.span);
        }
        tree.logOverride = BlockId::SpruceLog;
        tree.leafOverride = BlockId::SpruceLeaves;
        break;
    case BlockId::JungleSapling:
        tree.variant = twoByTwo ? TreeVariant::JungleMega : TreeVariant::JungleSmall;
        tree.height = twoByTwo ? kJungleMegaHeight.low + roll.range(kJungleMegaHeight.span)
                               : kJungleSmallHeight.low + roll.range(kJungleSmallHeight.span);
        tree.logOverride = BlockId::JungleLog;
        tree.leafOverride = BlockId::JungleLeaves;
        break;
    case BlockId::AcaciaSapling:
        // **Acacia has its own silhouette as of 2026-08-19** (finding 761). It
        // no longer borrows the round shape, so an acacia sapling now grows a
        // forked trunk under a flat plate. The overrides stay because the
        // variant still does not carry species - `logOverride` is what a
        // sapling uses to say what it knows and the variant does not.
        tree.variant = TreeVariant::Acacia;
        tree.height = kAcaciaHeight.low + roll.range(kAcaciaHeight.span);
        tree.logOverride = BlockId::AcaciaLog;
        tree.leafOverride = BlockId::AcaciaLeaves;
        break;
    case BlockId::DarkOakSapling:
        // Two-by-two by definition, and **short**: the reference's dark oak is
        // squat and broad where a giant jungle is a tower, so the height comes
        // off the branching range rather than the giant one.
        tree.variant = TreeVariant::JungleMega;
        tree.height = kBranchingHeight.low + roll.range(kBranchingHeight.span);
        tree.logOverride = BlockId::DarkOakLog;
        tree.leafOverride = BlockId::DarkOakLeaves;
        break;
    default:
        return plan;
    }

    plan.trunkSpan = twoByTwo ? 2 : 1;
    plan.clearRadius = clearRadiusFor(sapling, twoByTwo);
    plan.clearHeight = tree.height + canopyOvershoot(tree.variant);
    plan.blocks.reserve(256);

    ListSink sink{plan.blocks};
    // `y - 1` is the soil: `buildTree` measures from the block the trunk stands
    // **on**, and roots the tree by writing dirt there.
    buildTree(sink, y - 1, tree);

    // **Nothing outside the box the caller checked may overwrite anything.**
    //
    // The generator's own sink writes a branch or a trunk block unconditionally,
    // which is right when a chunk is being built out of nothing and wrong when
    // a sapling grows in a world someone lives in: a fancy oak's branches reach
    // five blocks out where its checked footprint is one, so without this a
    // grown tree quietly ate five blocks of anybody's house. A probe found it;
    // no warning and no assert could have.
    //
    // Demoting them to "only where there is room" is the reference's own
    // arrangement rather than a compromise - Java's tree feature validates the
    // trunk column and then places every branch and every leaf through
    // `isAirOrLeaves`, so a branch that meets a wall is simply not drawn and
    // the tree grows anyway.
    for (PlannedBlock& block : plan.blocks) {
        if (!plan.insideClearedBox(block.x - x, block.y - y, block.z - z)) {
            block.onlyIntoAir = true;
        }
    }

    plan.valid = !plan.blocks.empty();
    return plan;
}

} // namespace game::structures
