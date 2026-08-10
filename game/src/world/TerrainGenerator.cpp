#include "world/TerrainGenerator.hpp"

#include "world/Biome.hpp"
#include "world/Climate.hpp"
#include "world/Noise.hpp"
#include "world/Structures.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace game {
namespace {

// ---------------------------------------------------------------------------
// Terrain height
// ---------------------------------------------------------------------------

constexpr int kWorldHeight = kWorldHeightChunks * Chunk::kSize;
constexpr int kWorldTop = kWorldHeight - 1;

/// The surface never reaches the bedrock floor or the build ceiling. These
/// replace the reference's top and bottom density slides, which exist to do
/// exactly this and are only needed when the answer is a density field rather
/// than a height.
constexpr int kMinSurface = 4;
constexpr int kMaxSurface = 90;

/// Spectrum of the noise that displaces the target height.
///
/// **Halving amplitude per octave is the reference's shape and it matters more
/// than it looks.** Its octaves carry amplitude proportional to wavelength, so
/// every octave contributes the same vertical slope and the total stays gentle.
/// The first cut here used `{1, 1, 0.5, 0.25}`, which puts most of the energy
/// in the short wavelengths and produced terrain the ramp could not connect to
/// the ground.
constexpr std::array<float, 4> kBaseAmplitudes{1.0f, 0.5f, 0.25f, 0.125f};
constexpr float kBaseWavelengthXZ = 84.0f;

/// The noise is read at the column's own target height rather than at a fixed
/// plane, which decorrelates a mountain's texture from a lowland's for free.
constexpr float kBaseWavelengthY = 210.0f;

/// How much a face must fall across two columns to count as steep.
///
/// The reference compares the height one step *north* against one step *south*
/// and asks for 4 over that span; ours is 96 blocks tall against its 384, so 2
/// is about the same fraction of the relief available.
constexpr int kSteepDrop = 2;

/// How far below the waterline the bed is still soil rather than gravel.
/// The reference's `water offset -6`, and it is what stops a shoreline being
/// a hard line between grass and gravel.
constexpr int kShallowBedDepth = 6;

/// The 2D field that scatters a biome's patches over its ordinary top block.
///
/// 64 blocks is the reference's `noise/surface.json` — `firstOctave -6` with
/// three octaves, so 64/32/16 — and it takes no `xz_scale`, which makes this one
/// of the few numbers we can copy outright. At 34 the same patches came out
/// half the size and read as streaks rather than as patches.
constexpr std::array<float, 3> kPatchAmplitudes{1.0f, 0.6f, 0.3f};
constexpr float kPatchWavelength = 64.0f;

// ---------------------------------------------------------------------------
// Caves
// ---------------------------------------------------------------------------

/// **Spaghetti** — long winding tunnels, taken as a band either side of a
/// signed field's zero crossing. Thresholding the field itself instead gives
/// disconnected blobs, which read as holes rather than as caves.
constexpr std::array<float, 3> kSpaghettiAmplitudes{1.0f, 0.6f, 0.3f};
constexpr float kSpaghettiWavelength = 58.0f;
constexpr float kSpaghettiYStretch = 0.62f;
constexpr float kSpaghettiWidth = 0.060f;

/// **Cheese** — occasional open caverns, a low-frequency field thresholded
/// high. Separate from the spaghetti because the two have completely different
/// surface-area budgets, which is exactly why the reference keeps three systems
/// rather than one: geometry cost tracks cave *surface*, not hollow volume.
constexpr std::array<float, 2> kCheeseAmplitudes{1.0f, 0.5f};
constexpr float kCheeseWavelength = 104.0f;
constexpr float kCheeseYStretch = 0.72f;
constexpr float kCheeseThreshold = 0.44f;

/// **Noodle** — thin branching passages that break up the cheese. Gated on a
/// low-frequency patchiness field so they arrive in clusters instead of
/// riddling the entire underground.
constexpr std::array<float, 2> kNoodleAmplitudes{1.0f, 0.5f};
constexpr float kNoodleWavelength = 27.0f;
constexpr float kNoodleWidth = 0.032f;
constexpr std::array<float, 2> kNoodlePatchAmplitudes{1.0f, 0.5f};
constexpr float kNoodlePatchWavelength = 190.0f;
constexpr float kNoodlePatchThreshold = 0.10f;
constexpr int kNoodleCeiling = 34;

/// No caves within this distance of the surface, fading in below it. Without it
/// tunnels breach open ground constantly and the landscape reads as rotten
/// rather than as hollow.
constexpr int kCaveSurfaceMargin = 6;
constexpr int kCaveFadeDepth = 10;

/// The same for caverns, and it is **never** relaxed by an entrance. A tunnel
/// that surfaces is a cave mouth; a cavern that surfaces is a crater.
constexpr int kCavernSurfaceMargin = 12;

/// Where that margin is allowed to close to nothing, which is a cave mouth.
/// A 2D field, so an entrance is a *place* you can come back to rather than
/// something that happens at random along a tunnel.
///
/// **The threshold is quoted against a measured distribution, not against
/// [-1, 1].** Two octaves of value noise have a mean |v| of 0.27 and clear 0.46
/// on 8.6% of columns — so the old 0.46 with a 0.22 ramp needed 0.68 to open
/// fully, which is the top 1-2% of the field, and cave mouths were rare enough
/// that a playtest found none at all. 0.26 with a 0.14 ramp opens fully above
/// 0.40, which the same measurement puts at 12% of the world.
constexpr std::array<float, 2> kEntranceAmplitudes{1.0f, 0.5f};
constexpr float kEntranceWavelength = 230.0f;
constexpr float kEntranceThreshold = 0.26f;
constexpr float kEntranceWidth = 0.14f;

/// The world's floor is never carved, so there is always something to stand on.
constexpr int kBedrockSolid = 0;
constexpr int kBedrockTop = 4;

/// Stone gives way to deepslate across this band rather than at a line, on a
/// per-block roll — the reference's `vertical_gradient`, which is one of the
/// very few places it uses a genuine dice roll instead of a noise field.
constexpr int kDeepslateAlways = 9;
constexpr int kDeepslateNever = 15;

/// How far the sand of a desert turns to sandstone before it reaches stone.
constexpr int kSandstoneDepth = 3;

/// A flower region is 64 blocks across, and this fraction of its blooms are some
/// other species. Both together are what make a meadow read as mostly one
/// flower without being a monoculture with a seam at the region edge.
constexpr int kFlowerRegionShift = 6;
constexpr float kFlowerStrays = 0.30f;

/// How wide a stand of ground cover is, and how bare the gaps between them get.
/// Squaring the field is what puts most of the world at the thin end and gives
/// the thick patches somewhere to stand out from.
constexpr float kCoverPatchWavelength = 42.0f;
constexpr float kCoverPatchFloor = 0.30f;

/// How far below a column's top the surface rules can still reach. Only used to
/// bound the classification walk; the counter itself is exact.
constexpr float kSurfaceDepthWavelength = 12.0f;

// ---------------------------------------------------------------------------
// Ore veins
// ---------------------------------------------------------------------------

/// Where each ore appears, how many veins are attempted, and how big each is.
///
/// **A vein is a placed feature with a hard size cap, not a thresholded noise
/// field.** The old version tested a smooth 3D field against a high threshold,
/// which has no cap at all: wherever the field happened to stay high, the blob
/// kept growing, and a playtest found coal seams the size of a room. The
/// reference never does this for ore — it attempts `count` veins per chunk and
/// each one lays down at most `size` blocks. The largest vein_size anywhere in
/// vanilla is 20, and nothing it generates can exceed 52 blocks.
///
/// The bands are ours, mapped onto our world: it runs y 0-96 with sea level 24,
/// against the reference's -64 to 320 with sea level 63, so depths below sea
/// level compress by about 0.17 and heights above it by 0.28.
///
/// **`attempts` is quoted per 32x32 column, not per chunk**, because that is how
/// the reference counts and because a vein's height comes from its own band
/// rather than from whichever chunk is asking.
struct OreVein {
    BlockId block;
    std::uint32_t salt;
    /// The reference's `vein_size`: how many spheres are strung along the
    /// spindle. Realised block counts land near half of it.
    int size;
    int attempts;
    int minY;
    int peakY;
    int maxY;
    /// Refuses to generate against open air, so it cannot be spotted from
    /// inside a cave. The reference's `discard_chance_on_air_exposure`, tested
    /// **per block** rather than per vein, and what makes the rarest ores
    /// something you dig for rather than find.
    float airDiscard;
};

/// **Rarest first**, so a common ore can never overwrite a scarce one where
/// their bands overlap.
///
/// Sizes are the reference's own, shrunk about a quarter: a vein is a gameplay
/// unit measured against the player, so it does not scale with world height the
/// way the bands do. Attempt counts were then set so the totals stay where a
/// previous session's measurement had already put them, since the reported
/// problem was the size of a seam and not how much ore there is.
constexpr std::array<OreVein, 9> kOreVeins{{
    {BlockId::AncientDebris, 0xd41f07u, 3, 3, 3, 8, 16, 1.0f},
    {BlockId::EmeraldOre, 0x51c3b7u, 3, 36, 10, 46, 62, 1.0f},
    {BlockId::DiamondOre, 0x2f9a41u, 5, 2, 3, 6, 17, 0.7f},
    {BlockId::LapisOre, 0x7b31d9u, 6, 3, 3, 13, 25, 0.0f},
    {BlockId::GoldOre, 0x1de4a3u, 7, 3, 3, 9, 20, 0.5f},
    {BlockId::RedstoneOre, 0x64b8f2u, 7, 10, 3, 5, 17, 0.0f},
    {BlockId::IronOre, 0x3ac05eu, 7, 35, 3, 14, 27, 0.0f},
    {BlockId::CopperOre, 0x9e271bu, 8, 26, 10, 26, 39, 0.0f},
    {BlockId::CoalOre, 0x0c7d86u, 12, 70, 8, 26, 70, 0.5f},
}};

/// How far outside its own column a vein may reach, in blocks. The spindle is
/// `size/4` long and its fattest sphere has radius about `size/16 + 0.5`, so
/// this covers the largest one twice over.
constexpr int kVeinReach = 6;

// ---------------------------------------------------------------------------
// Height
// ---------------------------------------------------------------------------

/// Surface height at a column, in blocks.
///
/// **There is no density field to threshold.** Terrain is single-valued, so the
/// noise displaces the target height directly and the answer is exact for every
/// block. That is what removed the last of the reported artefacts: an
/// *interpolated* density is piecewise-linear across a lattice cell, so its
/// contour lines cluster on the cell boundaries and a gentle slope comes out
/// banded every four blocks - regular enough to read as a pattern rather than
/// as landscape.
///
/// The reference needs its lattice because its terrain is genuinely 3D and it
/// cannot afford a density evaluation per block. We gave up overhangs when we
/// made terrain single-valued, so the lattice was buying nothing but its own
/// artefact, and one noise evaluation per column is *cheaper* than the four
/// climate samples and fifty-two density samples the interpolated version cost.
///
/// The noise is still sampled in three dimensions, at the target height. It is
/// a function of `(x, z)` either way, but reading it at the column's own
/// altitude decorrelates a mountain's texture from a lowland's for free.
float columnHeightFrom(std::uint32_t seed, int worldX, int worldZ, const Shape& shape) {
    const float wobble =
        noise::octaves3D(seed ^ 0x7e44a1c3u, static_cast<float>(worldX) / kBaseWavelengthXZ,
                         shape.offsetY / kBaseWavelengthY, static_cast<float>(worldZ) / kBaseWavelengthXZ,
                         kBaseAmplitudes.data(), static_cast<int>(kBaseAmplitudes.size()));

    return shape.offsetY + wobble * kReliefBlocks / shape.factor;
}

int columnHeight(std::uint32_t seed, int worldX, int worldZ) {
    const Climate climate = climateAt(seed, worldX, worldZ);
    const Shape shape = shapeAt(seed, climate, worldX, worldZ);
    const float height = columnHeightFrom(seed, worldX, worldZ, shape);
    return std::clamp(static_cast<int>(std::floor(height)), kMinSurface, kMaxSurface);
}


// ---------------------------------------------------------------------------
// Caves
// ---------------------------------------------------------------------------

/// True where a cave should hollow out the rock.
bool isCave(std::uint32_t seed, int worldX, int worldY, int worldZ, int surfaceHeight) {
    if (worldY <= kBedrockTop) {
        return false;
    }

    const auto fx = static_cast<float>(worldX);
    const auto fy = static_cast<float>(worldY);
    const auto fz = static_cast<float>(worldZ);

    const float depth = static_cast<float>(surfaceHeight - worldY);

    // **Only the tunnel carver is ever allowed near daylight.** This is the
    // reference's `sloped_cheese < 1.5625 -> entrances only` gate, and it is the
    // whole of the "caves do not eat the landscape" rule. Letting the shared
    // surface margin relax for *every* system opened cheese caverns at ground
    // level, and a cavern that surfaces is not a cave mouth - it is a crater the
    // size of the cavern, with the trees that were standing on it left in
    // mid-air.
    //
    // A submerged column is excluded outright: its top blocks are holding the
    // sea up, and carving them stranded water in the air. The reference reaches
    // the same place from the other side, by letting its aquifer flood any cave
    // it cuts under the waterline instead of leaving a hole.
    const float entrance = noise::octaves2D(seed ^ 0x0be11a5eu, fx / kEntranceWavelength,
                                            fz / kEntranceWavelength, kEntranceAmplitudes.data(),
                                            static_cast<int>(kEntranceAmplitudes.size()));
    const float opening =
        surfaceHeight < kSeaLevel
            ? 0.0f
            : std::clamp((entrance - kEntranceThreshold) / kEntranceWidth, 0.0f, 1.0f);

    const float tunnelMargin = static_cast<float>(kCaveSurfaceMargin) * (1.0f - opening);
    if (depth >= tunnelMargin) {
        // Inside an entrance the tunnel keeps its full width right up to the
        // open air; outside one it fades in with depth, so ordinary ground stays
        // intact. Relaxing the margin without also relaxing the fade left a
        // mouth choked to a tenth of its width, which is a pinhole nobody sees.
        const float fadeDepth = static_cast<float>(kCaveFadeDepth) * (1.0f - opening);
        const float fade =
            fadeDepth <= 0.0f ? 1.0f : std::clamp((depth - tunnelMargin) / fadeDepth, 0.0f, 1.0f);
        if (fade > 0.0f) {
            const float spaghetti =
                noise::octaves3D(seed ^ 0x5eed1234u, fx / kSpaghettiWavelength,
                                 fy / kSpaghettiWavelength * (1.0f / kSpaghettiYStretch),
                                 fz / kSpaghettiWavelength, kSpaghettiAmplitudes.data(),
                                 static_cast<int>(kSpaghettiAmplitudes.size()));
            if (std::abs(spaghetti) < kSpaghettiWidth * fade) {
                return true;
            }
        }
    }

    // Caverns and noodles keep the full margin whatever the entrance field says.
    const float deepMargin = static_cast<float>(kCavernSurfaceMargin);
    if (depth < deepMargin) {
        return false;
    }
    const float fade =
        std::clamp((depth - deepMargin) / static_cast<float>(kCaveFadeDepth), 0.0f, 1.0f);
    if (fade <= 0.0f) {
        return false;
    }

    const float cheese = noise::octaves3D(seed ^ 0x0cbee5e0u, fx / kCheeseWavelength,
                                          fy / kCheeseWavelength * (1.0f / kCheeseYStretch),
                                          fz / kCheeseWavelength, kCheeseAmplitudes.data(),
                                          static_cast<int>(kCheeseAmplitudes.size()));
    if (cheese > kCheeseThreshold + (1.0f - fade)) {
        return true;
    }

    if (worldY > kNoodleCeiling) {
        return false;
    }

    // The patch test comes first because it is 2D and rejects most of the
    // world, which keeps the third noise off the common path entirely.
    const float patch = noise::octaves2D(seed ^ 0x0d1e0d1eu, fx / kNoodlePatchWavelength,
                                         fz / kNoodlePatchWavelength, kNoodlePatchAmplitudes.data(),
                                         static_cast<int>(kNoodlePatchAmplitudes.size()));
    if (patch < kNoodlePatchThreshold) {
        return false;
    }

    const float noodle = noise::octaves3D(seed ^ 0x0000d1e5u, fx / kNoodleWavelength, fy / kNoodleWavelength,
                                          fz / kNoodleWavelength, kNoodleAmplitudes.data(),
                                          static_cast<int>(kNoodleAmplitudes.size()));
    return std::abs(noodle) < kNoodleWidth * fade;
}

// ---------------------------------------------------------------------------
// Ores
// ---------------------------------------------------------------------------

/// A height inside the vein's band, weighted toward its own depth.
///
/// The reference's `trapezoid` height provider with a zero plateau, which is
/// just a triangle: the average of two uniform rolls. That is what makes a
/// depth feel like a depth rather than like a floor you cross.
int veinHeight(const OreVein& vein, noise::Stream& roll) {
    const int lowSpan = vein.peakY - vein.minY;
    const int highSpan = vein.maxY - vein.peakY;
    const int low = vein.minY + roll.range(std::max(1, lowSpan + 1));
    const int high = vein.peakY + roll.range(std::max(1, highSpan + 1));
    return roll.range(2) == 0 ? low : high;
}

/// The reference's `OreFeature`: a spindle of overlapping spheres strung along a
/// short, randomly angled segment through the origin.
///
/// **This is the shape, and it is why a vein reads as a vessel rather than a
/// dot.** Three things do the work. The segment gives it a direction, and its
/// two ends drift independently in y so it is never an axis-aligned slab. The
/// `sin(pi t)` term tapers the radius to nothing at both tips. And the scale is
/// re-rolled at every step, so consecutive spheres jump in size and the
/// silhouette comes out knobbly instead of smooth.
void placeVein(Chunk& chunk, const ChunkCoord& coord, const OreVein& vein, int originX, int originY,
               int originZ, noise::Stream& roll,
               const std::function<bool(int, int, int)>& exposedToAir) {
    const float angle = roll.unit() * 3.14159265f;
    const float half = static_cast<float>(vein.size) / 8.0f;

    const float x0 = static_cast<float>(originX) + std::sin(angle) * half;
    const float x1 = static_cast<float>(originX) - std::sin(angle) * half;
    const float z0 = static_cast<float>(originZ) + std::cos(angle) * half;
    const float z1 = static_cast<float>(originZ) - std::cos(angle) * half;
    const float y0 = static_cast<float>(originY + roll.range(3) - 2);
    const float y1 = static_cast<float>(originY + roll.range(3) - 2);

    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    for (int step = 0; step < vein.size; ++step) {
        const float t = static_cast<float>(step) / static_cast<float>(vein.size);
        const float cx = x0 + (x1 - x0) * t;
        const float cy = y0 + (y1 - y0) * t;
        const float cz = z0 + (z1 - z0) * t;

        // Re-rolled every step even when the sphere lands outside this chunk,
        // or the same vein would come out different from a neighbouring chunk.
        const float scale = roll.unit() * static_cast<float>(vein.size) / 16.0f;
        const float radius = ((std::sin(3.14159265f * t) + 1.0f) * scale + 1.0f) * 0.5f;
        const int reach = static_cast<int>(radius) + 1;

        for (int dy = -reach; dy <= reach; ++dy) {
            for (int dz = -reach; dz <= reach; ++dz) {
                for (int dx = -reach; dx <= reach; ++dx) {
                    const int wx = static_cast<int>(std::floor(cx)) + dx;
                    const int wy = static_cast<int>(std::floor(cy)) + dy;
                    const int wz = static_cast<int>(std::floor(cz)) + dz;

                    const float nx = (static_cast<float>(wx) + 0.5f - cx) / radius;
                    const float ny = (static_cast<float>(wy) + 0.5f - cy) / radius;
                    const float nz = (static_cast<float>(wz) + 0.5f - cz) / radius;
                    if (nx * nx + ny * ny + nz * nz >= 1.0f) {
                        continue;
                    }

                    // **Rolled for every cell the sphere covers**, before any
                    // test that depends on which chunk is asking. Rolling it
                    // after the bounds check would consume a different number of
                    // values from each side of a chunk border, and the vein
                    // would come out as two mismatched halves.
                    const bool discard = vein.airDiscard > 0.0f && roll.unit() < vein.airDiscard;

                    const int lx = wx - baseX;
                    const int ly = wy - baseY;
                    const int lz = wz - baseZ;
                    if (!Chunk::contains(lx, ly, lz)) {
                        continue;
                    }

                    // Only ever replaces plain rock, so a vein cannot eat the
                    // surface, a cave wall or another ore.
                    const BlockId here = chunk.at(lx, ly, lz);
                    if (here != BlockId::Stone && here != BlockId::Deepslate) {
                        continue;
                    }
                    if (discard && exposedToAir(wx, wy, wz)) {
                        continue;
                    }
                    // **Deepslate rock carries the deepslate form of the ore.**
                    // Read off the block actually being replaced rather than
                    // from a depth test of its own, so the swap lands on exactly
                    // the dithered boundary the stone-to-deepslate transition
                    // already drew instead of a block either side of it.
                    const bool deep = here == BlockId::Deepslate && hasDeepslateForm(vein.block);
                    chunk.set(lx, ly, lz, deep ? deepslateOreFor(vein.block) : vein.block);
                }
            }
        }
    }
}

} // namespace

int surfaceHeightAt(std::uint32_t seed, int worldX, int worldZ) {
    // Chunk generation calls the identical function, so the two cannot disagree
    // about where the ground is - which is what stops a tree hanging in the air.
    return columnHeight(seed, worldX, worldZ);
}

bool surfaceCarvedAt(std::uint32_t seed, int worldX, int worldZ) {
    const int surface = columnHeight(seed, worldX, worldZ);
    return isCave(seed, worldX, surface, worldZ, surface);
}

Chunk generateChunk(std::uint32_t seed, ChunkCoord coord) {
    Chunk chunk;
    const int baseX = coord.x * Chunk::kSize;
    const int baseY = coord.y * Chunk::kSize;
    const int baseZ = coord.z * Chunk::kSize;

    // The terrain top of every column in this chunk **and one ring around it**,
    // because the `steep` rule has to ask its neighbours how far they drop.
    //
    // This one array is the whole world model for the chunk: terrain is
    // single-valued, so "is this cell rock" is `y <= top` and nothing else.
    constexpr int kSpan = Chunk::kSize + 2;
    std::array<int, static_cast<std::size_t>(kSpan) * kSpan> heights{};
    for (int z = -1; z <= Chunk::kSize; ++z) {
        for (int x = -1; x <= Chunk::kSize; ++x) {
            heights[static_cast<std::size_t>(z + 1) * kSpan + static_cast<std::size_t>(x + 1)] =
                columnHeight(seed, baseX + x, baseZ + z);
        }
    }
    const auto heightAt = [&](int lx, int lz) {
        return heights[static_cast<std::size_t>(lz + 1) * kSpan + static_cast<std::size_t>(lx + 1)];
    };

    const int chunkTop = std::min(kWorldTop, baseY + Chunk::kSize - 1);

    for (int z = 0; z < Chunk::kSize; ++z) {
        for (int x = 0; x < Chunk::kSize; ++x) {
            const int worldX = baseX + x;
            const int worldZ = baseZ + z;
            const int surface = heightAt(x, z);

            const Climate climate = climateAt(seed, worldX, worldZ);
            const BiomeId biomeId = biomeFor(climate);
            const Biome& biome = biomeInfo(biomeId);

            // **Steep is one facing, not all four.** The reference's condition
            // compares the column one step north against one step south and
            // fires only when the north side is the high one, so a summit shows
            // rock on one face and keeps its snow on the other. Testing all four
            // neighbours symmetrically, as ours did, fires on every slope from
            // either side and frosts the whole mountain in bare stone instead.
            const bool steep = heightAt(x, z + 1) >= heightAt(x, z - 1) + kSteepDrop;

            const bool submerged = surface < kSeaLevel;

            // **First match wins, and every branch that does not match falls
            // through to the biome's ordinary top block.** That is the
            // reference's "no else" pattern, and writing it the other way round
            // \u2014 a solid stone top on a biome that occupies a narrow band \u2014 is
            // what put rings of bare stone across the landscape.
            BlockId filler = biome.filler;
            BlockId top = biome.top;
            const float patch = noise::octaves2D(
                seed ^ 0x5a7c4e11u, static_cast<float>(worldX) / kPatchWavelength,
                static_cast<float>(worldZ) / kPatchWavelength, kPatchAmplitudes.data(),
                static_cast<int>(kPatchAmplitudes.size()));
            if (submerged) {
                // **Above the waterline a bed material is not reachable at all
                // in the reference.** Gravel is placed only after both of its
                // water tests have failed, which cannot happen with no water
                // overhead - which is why `river` is named nowhere in its
                // surface rules and a dry river strip through the mountains
                // comes out as plain grass. Ours painted the biome's own top
                // block regardless of the waterline, so a channel that never
                // reached the sea came out as a long ribbon of gravel and sand
                // across dry grassland. Sand never appears on a riverbed there.
                const bool warm = biomeHasAny(biomeId, BiomeTag::Sandy | BiomeTag::Hot);
                const bool shallow = (kSeaLevel - surface) <= kShallowBedDepth;
                top = shallow ? BlockId::Dirt : (warm ? BlockId::Sand : BlockId::Gravel);
                filler = shallow ? BlockId::Dirt : (warm ? BlockId::Sandstone : BlockId::Stone);

                // The one bed material a biome may name for itself. Everything
                // else about a seabed is decided by depth, so this is opt-in per
                // biome rather than a rule the shoreline has to dodge.
                if (!shallow && biome.patch == BlockId::Prismarine &&
                    patch >= biome.patchThreshold) {
                    top = BlockId::Prismarine;
                    filler = BlockId::Prismarine;
                }
            } else if (steep && biome.steep != BlockId::Air) {
                top = biome.steep;
            } else if (biome.patch != BlockId::Air) {
                if (patch >= biome.patchThreshold) {
                    top = biome.patch;
                }
            }

            // Snow is an altitude rule against the **biome's own** temperature,
            // so a warm lowland never whitens however cold its neighbour is,
            // and a cold one is white at sea level. Deliberately does not
            // override bare rock on a steep face — that is the point of `steep`.
            if (top == BlockId::Grass && freezesAt(seed, biome.warmth, worldX, surface, worldZ)) {
                top = BlockId::Snow;
            }

            // Varying the filler depth is most of why the reference's ground
            // does not look extruded: a constant one puts the dirt-to-stone
            // boundary on a contour line.
            const float depthNoise =
                noise::value2D(seed ^ 0x5c1f00du, static_cast<float>(worldX) / kSurfaceDepthWavelength,
                               static_cast<float>(worldZ) / kSurfaceDepthWavelength);
            const int fillerDepth = biome.fillerDepth + static_cast<int>(depthNoise * 3.0f);

            // One downward walk over the column. `stoneDepth` is exact without
            // having to start above this chunk, because terrain is single-valued
            // and the top is already known.
            for (int y = std::min(chunkTop, surface); y >= baseY; --y) {
                const int stoneDepth = surface - y + 1;

                BlockId block = BlockId::Stone;

                if (y <= kBedrockSolid ||
                    (y <= kBedrockTop &&
                     noise::hashUnit3D(seed ^ 0xbed0c4u, worldX, y, worldZ) <
                         static_cast<float>(kBedrockTop + 1 - y) / static_cast<float>(kBedrockTop + 1))) {
                    block = BlockId::Bedrock;
                } else if (stoneDepth == 1) {
                    // The reference's top rule is exactly one block deep. Depth
                    // belongs to the filler underneath it.
                    block = top;
                } else if (stoneDepth <= 1 + fillerDepth) {
                    block = filler;
                } else if (filler == BlockId::Sand &&
                           stoneDepth <= 1 + fillerDepth + kSandstoneDepth) {
                    // Sand sits on sandstone rather than straight on stone.
                    // Keyed off the filler so it needs no biome name.
                    block = BlockId::Sandstone;
                } else if (y <= kDeepslateAlways ||
                           (y < kDeepslateNever &&
                            noise::hashUnit3D(seed ^ 0xdee9a1eu, worldX, y, worldZ) <
                                static_cast<float>(kDeepslateNever - y) /
                                    static_cast<float>(kDeepslateNever - kDeepslateAlways))) {
                    block = BlockId::Deepslate;
                }

                // A cave is a hole in a finished world rather than a different
                // world, which is why carving comes after classification and
                // never after the surface rules have read the ground.
                if (block != BlockId::Bedrock && isCave(seed, worldX, y, worldZ, surface)) {
                    continue;
                }

                chunk.set(x, y - baseY, z, block);
            }

            // Everything the **uncarved** terrain leaves empty below sea level
            // is ocean. Testing against the terrain top rather than against what
            // is in the chunk now is the whole difference between a sea and a
            // drowned world: a cave cut into rock that happens to sit under the
            // waterline stays dry, which is what the reference's aquifers are
            // for and what we get here for nothing. Sea water is all source, so
            // it never drains into whatever the player digs.
            // Ice on the water is the *same* rule as snow on the ground — one
            // `freeze_top_layer` does both in the reference — so it asks the
            // same question rather than consulting a hand-written tag that
            // means the same thing and can disagree with it.
            const bool frozen = freezesAt(seed, biome.warmth, worldX, kSeaLevel, worldZ);
            const int waterTop = std::min(kSeaLevel - baseY, Chunk::kSize - 1);
            for (int y = 0; y <= waterTop; ++y) {
                const int worldY = y + baseY;
                if (worldY <= surface || chunk.at(x, y, z) != BlockId::Air) {
                    continue;
                }
                chunk.set(x, y, z, frozen && worldY == kSeaLevel ? BlockId::Ice : BlockId::Water0);
            }

            // Seabed cover. Kelp and seagrass need water over them and coral
            // wants the warm shallows, so all three ride on the same test the
            // water fill just did rather than re-deriving where the sea is.
            const int bedY = surface + 1 - baseY;
            if (submerged && bedY >= 1 && bedY < Chunk::kSize && surface < kSeaLevel &&
                chunk.at(x, bedY, z) != BlockId::Air &&
                chunk.at(x, bedY - 1, z) != BlockId::Air) {
                const float roll = noise::hashUnit2D(seed ^ 0x5ea9a55u, worldX, worldZ);
                const bool warm = biomeHasAny(biomeId, BiomeTag::Hot | BiomeTag::Sandy);
                if (warm && roll < 0.05f) {
                    constexpr std::array<BlockId, 5> kCorals{
                        BlockId::TubeCoralBlock, BlockId::BrainCoralBlock, BlockId::BubbleCoralBlock,
                        BlockId::FireCoralBlock, BlockId::HornCoralBlock};
                    chunk.set(x, bedY - 1, z,
                              kCorals[noise::hash2D(seed ^ 0xc0a1u, worldX, worldZ) % kCorals.size()]);
                    // Sea pickles cluster on a reef and nowhere else, which is
                    // what makes finding one mean something.
                    if (roll < 0.018f) {
                        chunk.set(x, bedY, z, BlockId::SeaPickle);
                        chunk.setWaterlogged(x, bedY, z, true);
                    }
                } else if (roll < 0.14f) {
                    // Waterlogged rather than written over the water: the plant
                    // shares the cell, which is the whole point of the bit.
                    chunk.set(x, bedY, z, roll < 0.07f ? BlockId::Seagrass : BlockId::Kelp);
                    chunk.setWaterlogged(x, bedY, z, true);
                }
            }

            // Ground cover, on whatever the surface turned out to be rather than
            // on the biome's nominal top block: a snow line, a steep face or a
            // shoreline may already have overridden it.
            const int plantY = surface + 1 - baseY;
            if (plantY >= 1 && plantY < Chunk::kSize && surface > kSeaLevel + 1 &&
                chunk.at(x, plantY, z) == BlockId::Air &&
                chunk.at(x, plantY - 1, z) != BlockId::Air) {
                // Grows a stalk upward, stopping at the top of the chunk. A
                // stalk that runs off the top is simply shorter here; the cell
                // above belongs to another chunk and writing it from this one
                // would break generation purity.
                const auto growColumn = [&](BlockId block, int height) {
                    for (int i = 0; i < height && plantY + i < Chunk::kSize; ++i) {
                        chunk.set(x, plantY + i, z, block);
                    }
                };

                const bool wetBiome = biomeHasAny(biomeId, BiomeTag::Wet | BiomeTag::Swamp);
                const bool coldBiome = biomeHasAny(biomeId, BiomeTag::Cold | BiomeTag::Snowy);

                // **Cover comes in patches, not as an even speckle.** A flat
                // per-column probability spreads the same plant every N columns
                // across a whole continent, which reads as noise rather than as
                // meadow; the reference scatters ~32 tries inside a small radius
                // and leaves the ground between them bare. One low-frequency
                // field multiplying the density buys the same thing: thick
                // stands with clearings between them, at the same average.
                const float coverPatch =
                    noise::value2D(seed ^ 0x6d1e5a3u,
                                   static_cast<float>(worldX) / kCoverPatchWavelength,
                                   static_cast<float>(worldZ) / kCoverPatchWavelength);
                const float coverDensity =
                    biome.grassDensity *
                    (kCoverPatchFloor + (2.0f - kCoverPatchFloor) * coverPatch * coverPatch);

                // Snow is a top block here rather than a layer over grass, so
                // asking only for grass left the three snowy biomes with no
                // ground cover at all - and their `grassDensity` rows unread.
                if ((top == BlockId::Grass || top == BlockId::Snow) &&
                    noise::hashUnit2D(seed ^ 0x91a5eedu, worldX, worldZ) < coverDensity) {
                    const float pick = noise::hashUnit2D(seed ^ 0x5f3aa17u, worldX, worldZ);
                    BlockId cover = BlockId::TallGrass;
                    // A share of the plain cover is fern rather than grass, and
                    // only where it belongs - the reference puts it in forest
                    // and taiga, not on open plains.
                    if (biomeHasTag(biomeId, BiomeTag::Forest) &&
                        noise::hashUnit2D(seed ^ 0x3e5b1c9u, worldX, worldZ) < 0.35f) {
                        cover = BlockId::Fern;
                    }
                    if (pick < biome.flowerShare) {                        // Which flower is a **regional** choice, so a meadow
                        // reads as a meadow rather than as confetti - but the
                        // region is a *discrete cell hash*, not a smooth field.
                        // A smooth field piles up around its midpoint, so
                        // mapping one across the list handed most of the world
                        // whichever species happened to sit in the middle of it.
                        // A hash is flat, so all of them are equally likely.
                        constexpr std::array<BlockId, 12> kFlowers{
                            BlockId::Dandelion,  BlockId::Poppy,      BlockId::Cornflower,
                            BlockId::OxeyeDaisy, BlockId::AzureBluet, BlockId::Allium,
                            BlockId::RedTulip,   BlockId::OrangeTulip, BlockId::PinkTulip,
                            BlockId::WhiteTulip, BlockId::LilyOfTheValley, BlockId::Cornflower};
                        const int cellX = worldX >> kFlowerRegionShift;
                        const int cellZ = worldZ >> kFlowerRegionShift;
                        std::size_t index =
                            noise::hash2D(seed ^ 0x1f0e9a3u, cellX, cellZ) % kFlowers.size();

                        // A minority of every patch is something else. Without
                        // it each region is a monoculture with a hard seam at
                        // the cell edge.
                        if (noise::hashUnit2D(seed ^ 0x77c1a2bu, worldX, worldZ) < kFlowerStrays) {
                            index = noise::hash2D(seed ^ 0x2b90c17u, worldX, worldZ) % kFlowers.size();
                        }
                        cover = kFlowers[index];
                        // The blue orchid is the swamp's alone in the reference,
                        // and giving it a home is what stops it being one more
                        // face in the meadow crowd.
                        if (biomeHasTag(biomeId, BiomeTag::Swamp)) {
                            cover = BlockId::BlueOrchid;
                        }
                    }
                    chunk.set(x, plantY, z, cover);
                } else if (biomeHasTag(biomeId, BiomeTag::Dry) && top == BlockId::Sand &&
                           surface > kSeaLevel + 2) {
                    const float scrub = noise::hashUnit2D(seed ^ 0x2c9b4d1u, worldX, worldZ);
                    // Clear of the shoreline, so a beach does not sprout scrub.
                    if (scrub < 0.010f) {
                        // Cactus stands in ones and twos, never in a thicket -
                        // the reference refuses to place one touching another,
                        // and a low enough roll is the same thing statistically
                        // without needing to read a neighbouring column.
                        growColumn(BlockId::Cactus,
                                   1 + static_cast<int>(noise::hashUnit2D(seed ^ 0x7c2a91u, worldX,
                                                                          worldZ) *
                                                        3.0f));
                    } else if (scrub < 0.030f) {
                        chunk.set(x, plantY, z, BlockId::DeadBush);
                    }
                } else if (top == BlockId::Grass && (wetBiome || coldBiome) &&
                           noise::hashUnit2D(seed ^ 0x6b1f7a3u, worldX, worldZ) < 0.006f) {
                    // Mushrooms in the damp and the dark, and berries in the
                    // cold. Both existed as blocks and generated nowhere.
                    if (coldBiome) {
                        chunk.set(x, plantY, z, BlockId::SweetBerryBush);
                    } else {
                        chunk.set(x, plantY, z,
                                  noise::hash2D(seed ^ 0x11c7e5u, worldX, worldZ) % 2u == 0u
                                      ? BlockId::BrownMushroom
                                      : BlockId::RedMushroom);
                    }
                }

                // Bamboo, which wants the wet forest rather than the open
                // grassland, and stands taller than anything else here.
                if (chunk.at(x, plantY, z) == BlockId::Air && top == BlockId::Grass &&
                    biomeHasTag(biomeId, BiomeTag::Wet) &&
                    biomeHasTag(biomeId, BiomeTag::Forest) &&
                    noise::hashUnit2D(seed ^ 0x4d90b13u, worldX, worldZ) < 0.045f) {
                    growColumn(BlockId::Bamboo,
                               3 + static_cast<int>(
                                       noise::hashUnit2D(seed ^ 0x2f77c1u, worldX, worldZ) * 5.0f));
                }
            }

            // Sugar cane, which needs the waterline rather than open ground: it
            // stands on sand or grass **one block above sea level**, which is
            // exactly the strip a river or lake edge produces.
            const int caneY = surface + 1 - baseY;
            if (caneY >= 1 && caneY < Chunk::kSize && surface == kSeaLevel + 1 &&
                chunk.at(x, caneY, z) == BlockId::Air &&
                (top == BlockId::Grass || top == BlockId::Sand) &&
                !biomeHasAny(biomeId, BiomeTag::Frozen | BiomeTag::Snowy) &&
                noise::hashUnit2D(seed ^ 0x58c31a9u, worldX, worldZ) < 0.18f) {
                const int height =
                    2 + static_cast<int>(noise::hashUnit2D(seed ^ 0x9b12f7u, worldX, worldZ) * 2.0f);
                for (int i = 0; i < height && caneY + i < Chunk::kSize; ++i) {
                    chunk.set(x, caneY + i, z, BlockId::SugarCane);
                }
            }

            // Lily pads, which float rather than stand: the cell is the water
            // surface itself, so this is the one plant placed *at* sea level.
            // **Rivers as well as swamps**, because the swamp is two tenths of
            // a percent of the world and almost none of it is under water - a
            // scan of three hundred chunk columns found not one pad until the
            // river was allowed too.
            const int padY = kSeaLevel + 1 - baseY;
            if (padY >= 1 && padY < Chunk::kSize && surface < kSeaLevel &&
                surface > kSeaLevel - 7 &&
                biomeHasAny(biomeId, BiomeTag::Swamp | BiomeTag::River) &&
                !biomeHasAny(biomeId, BiomeTag::Frozen | BiomeTag::Snowy) &&
                chunk.at(x, padY, z) == BlockId::Air &&
                isWater(chunk.at(x, padY - 1, z)) &&
                noise::hashUnit2D(seed ^ 0x3ab7e91u, worldX, worldZ) < 0.10f) {
                chunk.set(x, padY, z, BlockId::LilyPad);
            }
        }
    }

    // Ores go in after the rock is finished and after caves are cut, so a vein
    // can only ever replace stone that survived, and a cave wall stays rock.
    //
    // Veins belong to a **column**, not to this chunk: a vein's height comes
    // from its own band, so the column that owns it may be asking about any of
    // the three chunks stacked above it. Neighbouring columns are rolled too,
    // because a vein anchored near a border reaches across one - and every roll
    // happens whether or not the block lands here, or the same vein would come
    // out differently depending on which chunk generated it.
    const auto exposedToAir = [&](int wx, int wy, int wz) {
        const auto open = [&](int nx, int ny, int nz) {
            const int nTop = heightAt(nx - baseX, nz - baseZ);
            return ny > nTop || isCave(seed, nx, ny, nz, nTop);
        };
        return open(wx - 1, wy, wz) || open(wx + 1, wy, wz) || open(wx, wy, wz - 1) ||
               open(wx, wy, wz + 1) || open(wx, wy + 1, wz) || open(wx, wy - 1, wz);
    };

    const int columnSpan = (kVeinReach + Chunk::kSize - 1) / Chunk::kSize;
    for (int nz = -columnSpan; nz <= columnSpan; ++nz) {
        for (int nx = -columnSpan; nx <= columnSpan; ++nx) {
            const int columnX = coord.x + nx;
            const int columnZ = coord.z + nz;

            for (const OreVein& vein : kOreVeins) {
                noise::Stream site{noise::hash2D(seed ^ vein.salt, columnX, columnZ)};
                for (int attempt = 0; attempt < vein.attempts; ++attempt) {
                    const int originX = columnX * Chunk::kSize + site.range(Chunk::kSize);
                    const int originZ = columnZ * Chunk::kSize + site.range(Chunk::kSize);
                    const int originY = veinHeight(vein, site);

                    // Each vein carries a stream of its own, drawn from the
                    // column's. That is what lets a vein nowhere near this chunk
                    // be dropped outright without shifting every vein after it -
                    // and two thirds of them are nowhere near, because a column
                    // spans three chunks and a vein sits in one.
                    noise::Stream shape{site.next()};
                    if (originY + kVeinReach < baseY ||
                        originY - kVeinReach >= baseY + Chunk::kSize) {
                        continue;
                    }
                    placeVein(chunk, coord, vein, originX, originY, originZ, shape, exposedToAir);
                }
            }
        }
    }

    structures::generateInto(chunk, seed, coord);

    return chunk;
}

} // namespace game
