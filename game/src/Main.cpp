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
#include "item/Inventory.hpp"
#include "item/Recipe.hpp"
#include "item/SlotOps.hpp"
#include "item/Smelting.hpp"
#include "item/Tool.hpp"
#include "world/Furnace.hpp"
#include "world/ItemEntity.hpp"
#include "world/Projectile.hpp"
#include "world/Biome.hpp"
#include "world/BlockOutline.hpp"
#include "world/Chunk.hpp"
#include "world/Collision.hpp"
#include "world/Creature.hpp"
#include "world/Explosion.hpp"
#include "world/Farming.hpp"

#include <map>
#include "world/FallingBlock.hpp"
#include "world/Material.hpp"
#include "world/Player.hpp"
#include "world/Raycast.hpp"
#include "world/Sky.hpp"
#include "world/TerrainGenerator.hpp"
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
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
constexpr float kBlastKnockback = 7.0f;

/// A pearl may be thrown once a second, the reference's own cooldown.
constexpr float kPearlCooldownSeconds = 1.0f;

/// How far above the impact the teleport will search for room to stand, in
/// whole blocks. Two is enough to clear a slab or a stair underfoot; more than
/// that and you are being moved somewhere you did not aim.
constexpr int kPearlLandingLift = 3;

// Rate a held button lands blows on a creature, distinct from digging: a swing
// connects once and then has to be wound up again.
constexpr float kSwingSeconds = 0.45f;
// How often a block that takes no time at all may be broken. Creative breaks
// are instant, so without this the dig completes every frame and each one
// exposes the block behind it - one click strips the whole reach in a line.
constexpr float kBreakRepeatSeconds = 0.22f;

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

/// Slower than breaking or placing: emptying a stack by accident is more
/// annoying than having to hold the key a moment longer.
constexpr float kDropRepeatSeconds = 0.22f;

// Change this and the entire world changes, reproducibly.
constexpr std::uint32_t kWorldSeed = 1337u;
// Selectable frame caps, lowest to highest. 0 means uncapped.
// Temporary keyboard-driven stand-in until there is a real settings screen.
constexpr std::array<double, 8> kFpsCapOptions{30.0, 60.0, 90.0, 120.0, 144.0, 165.0, 240.0, 0.0};
constexpr std::size_t kDefaultFpsCapIndex = 3;

// In the order `tonemap.frag` tests for them, which is also the order
// `Settings::toneMapper` counts in. Short on purpose: these are shown on the F5
// overlay as well as logged, and the overlay has one column to fit them in.
constexpr std::array<const char*, 4> kToneMapperNames{"PBR neutral", "Hable", "Reinhard", "ACES"};
static_assert(kToneMapperNames.size() == game::Settings::kToneMapperCount);

// What F12 cycles through, in the order `deferred.frag` tests for them. These
// are how a deferred renderer is debugged: when the picture is wrong, one of
// them says which input is wrong.
constexpr std::array<const char*, 10> kDebugViewNames{
    "off",      "albedo",   "normal",   "roughness",   "metallic",
    "occlusion", "emissive", "sky/block light", "distance", "cast shadow"};
static_assert(kDebugViewNames.size() == engine::Renderer::kDebugViewCount);

// What G cycles through. Each step raises the shadow map's resolution, how many
// slices of the view it is split across, and how far shadows are drawn.
constexpr std::array<const char*, 4> kShadowQualityNames{"off", "low", "medium", "high"};
static_assert(kShadowQualityNames.size() == game::Settings::kShadowQualityCount);
static_assert(game::Settings::kShadowQualityCount ==
              static_cast<unsigned>(engine::Renderer::kShadowQualityCount));

// What C cycles through: how many steps each ray takes through the cloud deck.
constexpr std::array<const char*, 3> kCloudQualityNames{"off", "fast", "fancy"};
static_assert(kCloudQualityNames.size() == game::Settings::kCloudQualityCount);
static_assert(game::Settings::kCloudQualityCount ==
              static_cast<unsigned>(engine::Renderer::kCloudQualityCount));

// Blocks a second the deck drifts west. The reference's own figure is not
// published anywhere; this is ours, chosen so a cloud crosses the view in about
// a minute rather than the several the reference's estimated 0.6 would take.
constexpr float kCloudDriftPerSecond = 1.1f;

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
        // or whether the thing is standing on a plinth over a hole — and every
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
                    // than re-deriving it — a second copy of that rule here is
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
                        constexpr std::array<glm::ivec3, 6> steps{
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
        audio.setMasterVolume(settings.soundVolume);
        audio.setMusicVolume(settings.musicVolume);

        engine::Window window(kWindowWidth, kWindowHeight, "Voxel Game");
        engine::VulkanContext context(window);

        // Order defines the texture array layer indices, which must match
        // TextureLayer in Block.hpp.
        const std::filesystem::path textureDir = engine::executableDirectory() / "assets" / "textures" / "blocks";

        // Reference block and item art, staged beside the exe rather than under
        // assets/ so it can never ship. Preferred per texture, so anything the
        // reference has no counterpart for - the white utility layer, the sun -
        // silently keeps ours.
        const std::filesystem::path referenceBlocks = engine::executableDirectory() / "blocks-reference";
        std::vector<std::string> missingTextures;
        const auto blockTexture = [&](const char* name) {
            std::filesystem::path staged = referenceBlocks / name;
            if (std::filesystem::exists(staged)) {
                return staged;
            }
            std::filesystem::path own = textureDir / name;
            if (std::filesystem::exists(own)) {
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

        if (!missingTextures.empty()) {
            std::string names;
            for (const std::string& name : missingTextures) {
                names += (names.empty() ? "" : ", ") + name;
            }
            engine::logError(std::to_string(missingTextures.size()) +
                             " block textures are missing and will draw blank: " + names);
        }


        // Spawn egg sprites, staged beside the exe rather than under assets/ for
        // the same reason the reference skins are: they are placeholder art and
        // the asset copy step must not be able to carry them into a build.
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
                  "campfire_log.png", "campfire_log_lit.png", "soul_campfire_fire.png",
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
                  // The seventy-four appended items, in `ItemId` order. Armour
                  // runs helmet-chest-legs-boots by rising material, which is
                  // the order the enum uses, so a piece's icon is one offset.
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
                  "music_disc_13.png", "music_disc_cat.png", "music_disc_blocks.png",
                  "music_disc_chirp.png", "music_disc_far.png", "music_disc_mall.png",
                  "music_disc_drift.png", "music_disc_ember.png", "music_disc_vale.png",
                  "music_disc_hollow.png", "music_disc_11.png", "music_disc_wait.png",
                  "music_disc_hoofbeat.png", "music_disc_otherside.png", "music_disc_5.png",
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
            for (const char* name : {"blaze_rod.png", "blaze_powder.png",
                                     "fermented_spider_eye.png", "ghast_tear.png",
                                     "dragon_breath.png"}) {
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

        // Reference skins are placeholder art for every species whose own skin
        // has not been drawn yet. They sit beside the exe rather than under
        // assets/, so the asset copy step cannot carry them into a build, and
        // they are simply absent unless the tool has been run.
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
        // one texel of spacing. That is the reference's own rule, and it        // reproduces its published widths exactly - 'i' 2, 'l' 3, 'I' 4, 'a' 6,
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

                const auto apply = [&](engine::MeshHandle& handle, const engine::MeshData& mesh, bool translucent) {
                    if (handle != engine::kInvalidMesh) {
                        renderer.updateMesh(handle, mesh);
                    } else if (!mesh.empty()) {
                        handle = renderer.addMesh(mesh, translucent);
                    }
                };

                apply(handles.opaque, update.mesh, false);
                apply(handles.translucent, update.translucentMesh, true);
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
            player.position = savedPlayer->position;
            camera.yaw = savedPlayer->yaw;
            camera.pitch = savedPlayer->pitch;
            // Clamped on the way in rather than trusted: this is the one place
            // the game reads bytes it did not write this run, and a health of
            // zero out of a corrupt file would kill you on the first frame.
            player.health = std::clamp(savedPlayer->health, 1, game::survival::kMaxHealth);
            player.food = std::clamp(savedPlayer->food, 0, game::survival::kMaxFood);
            player.saturation =
                game::survival::clampSaturation(std::max(0.0f, savedPlayer->saturation), player.food);
            player.exhaustion = std::clamp(savedPlayer->exhaustion, 0.0f,
                                           game::survival::kExhaustionPerLevel);
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
        engine::logInfo("Right click a spawn egg to place that creature; the inventory's left card has them all.");
        engine::logInfo("Entering main loop. Close the window to exit.");

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        auto lastReportTime = previousTime;
        int framesSinceReport = 0;

        float placeTimer = 0.0f;
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
        std::vector<std::pair<glm::ivec3, game::ItemId>> jukeboxDiscs;
        /// **One inventory for every ender chest in the world.** It belongs to
        /// the player rather than to any block, which is the whole point of it,
        /// so it lives here and not in the chest map.
        game::Chest enderChest;
        /// Where a bed that has been slept in stands, if any. Not saved yet, so
        /// it lasts the session - the reference keeps it on the player, which
        /// would mean a `player.dat` format bump.
        glm::ivec3 respawnPoint{0};
        bool hasRespawnPoint = false;
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
        float lastFallDistance = 0.0f;
        /// Whether the player was standing last frame, so a landing is an edge
        /// rather than a state - trampling must fire once per fall, not every
        /// frame you stand on the field afterwards.
        bool wasOnGround = true;
        float caveTimer = 0.0f;
        constexpr float kStepDistance = 2.1f;
        constexpr float kBigFallDistance = 7.0f;
        // Rolled rarely rather than every frame; the reference's own cave
        // ambience is sparse enough that a check a few times a minute is
        // indistinguishable from one every tick.
        constexpr float kCaveCheckSeconds = 22.0f;
        // What the status bars last drew. Compared rather than flagged, because
        // health, food and air all change from inside the physics and nothing
        // there knows the HUD exists.
        int lastShownHealth = -1;
        int lastShownFood = -1;
        game::hud::AirRow lastShownAir;
        bool lastShownHurt = false;

        // Creative starts with one of everything placeable; survival starts with
        // nothing and fills up from what you break.
        constexpr std::array<game::BlockId, game::kHotbarSlots> creativeKit{
            game::BlockId::Grass,     game::BlockId::Dirt,          game::BlockId::Stone,
            game::BlockId::StoneSlab, game::BlockId::CobbleStairs0, game::BlockId::TallGrass,
            game::BlockId::Planks,    game::BlockId::PlanksFence,   game::BlockId::Glowstone};

        // Raw materials, in the storage rows rather than the hotbar: they are
        // crafting inputs rather than things to place, and the hotbar is full.
        constexpr std::array<game::BlockId, 8> creativeStock{
            game::BlockId::Log,     game::BlockId::Cobblestone,   game::BlockId::Sand,
            game::BlockId::Gravel,  game::BlockId::CraftingTable, game::BlockId::Furnace,
            game::BlockId::Torch,   game::BlockId::Bricks};

        game::Inventory inventory;
        bool creative = settings.creativeMode;
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
        // laid out in a row so all of them can be judged in one look. A dropped
        // item is never saved, so this writes nothing to the world.
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
        std::unordered_map<glm::ivec3, game::Furnace, BlockPositionHash> furnaces;
        for (const game::PlacedFurnace& placed : world.store().loadFurnaces()) {
            furnaces.emplace(placed.position, placed.furnace);
        }
        if (!furnaces.empty()) {
            engine::logInfo("Restored " + std::to_string(furnaces.size()) + " furnaces.");
        }

        // Chests, on the same arrangement and for the same reasons.
        std::unordered_map<glm::ivec3, game::Chest, BlockPositionHash> chests;
        for (const game::PlacedChest& placed : world.store().loadChests()) {
            chests.emplace(placed.position, placed.chest);
        }
        if (!chests.empty()) {
            engine::logInfo("Restored " + std::to_string(chests.size()) + " chests.");
        }

        // What is inside every stowbox that is currently an item rather than a
        // block. The key rides in the stack's `damage`, so it survives being
        // dropped, picked up, saved and reloaded with no new field anywhere.
        std::unordered_map<int, game::Chest> stowed;
        int nextStowHandle = 1;
        for (const game::StowedBox& box : world.store().loadStowboxes()) {
            stowed.emplace(box.handle, box.contents);
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
        const auto moveOneItem = [](game::Chest& from, std::size_t fromSlots, game::Chest& to,
                                    std::size_t toSlots) {
            for (std::size_t i = 0; i < fromSlots; ++i) {
                game::ItemStack& source = from.slots[i];
                if (source.empty()) {
                    continue;
                }
                for (int pass = 0; pass < 2; ++pass) {
                    for (std::size_t j = 0; j < toSlots; ++j) {
                        game::ItemStack& into = to.slots[j];
                        const bool usable =
                            pass == 0 ? (!into.empty() && into.item == source.item &&
                                         into.space() > 0)
                                      : into.empty();
                        if (!usable) {
                            continue;
                        }
                        if (into.empty()) {
                            into = game::ItemStack{source.item, 1};
                        } else {
                            ++into.count;
                        }
                        if (--source.count <= 0) {
                            source = game::ItemStack{};
                        }
                        return true;
                    }
                }
            }
            return false;
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
        if (settings.creatureShowcase == 0) {
            std::size_t restored = 0;
            for (const game::SavedCreature& saved : world.store().loadCreatures()) {
                if (saved.kind >= static_cast<std::uint8_t>(game::CreatureKind::Count)) {
                    continue;
                }
                creatures.restore(static_cast<game::CreatureKind>(saved.kind),
                                  glm::vec3{saved.x, saved.y, saved.z}, saved.yaw, saved.health,
                                  saved.scale, saved.charged != 0, saved.playerBuilt != 0,
                                  saved.profession);
                ++restored;
            }
            if (restored > 0) {
                engine::logInfo("Restored " + std::to_string(restored) + " creatures.");
            }
        }

        // Which furnace the open screen is looking at, if any.
        glm::ivec3 openFurnacePosition{0};
        glm::ivec3 openChestPosition{0};
        // The second half of an open double chest, and equal to the first when
        // there is only one, so nothing has to ask which case it is.
        glm::ivec3 openChestPartner{0};
        game::ItemStack furnaceOutput;

        // A sweep with the button held spreads the cursor's stack over every
        // slot it crosses.
        //
        // The distribution is recomputed from scratch every frame rather than
        // applied incrementally, which is why each slot's contents from before
        // the drag are kept: replaying from the original state is the only way
        // to show a live preview that stays correct as more slots are added.
        enum class DragButton { None, Left, Right };
        DragButton dragButton = DragButton::None;
        game::ItemStack dragOriginalCursor;
        std::vector<std::pair<game::inventoryScreen::SlotHit, game::ItemStack>> draggedSlots;

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
                const game::ItemStack& stored = savedPlayer->inventory[i];
                game::ItemStack& into = inventory.slot(i);
                // Assigned rather than merged, so the saved inventory is the
                // whole answer: a slot deliberately emptied stays empty instead
                // of being restocked by the starting kit above.
                if (stored.item <= game::ItemId::None || stored.item > game::ItemId::kLastItem ||
                    stored.count <= 0) {
                    into = {};
                    continue;
                }
                into.item = stored.item;
                into.count = std::min(stored.count, game::maxStackFor(stored.item));
                into.damage = std::max(0, stored.damage);
            }
            selectedSlot = static_cast<std::size_t>(
                std::clamp<std::int32_t>(savedPlayer->selectedSlot, 0,
                                         static_cast<std::int32_t>(game::kHotbarSlots) - 1));
        }
        bool hudDirty = true;
        /// Whether the camera itself is inside a water cell, which is a
        /// different question from `Player::inWater` - that one asks about the
        /// whole body, and being waist deep does not change what you see.
        bool eyeUnderwater = false;
        bool eyeInLava = false;

        // Hands back everything the screen was holding: the cursor stack, then
        // the crafting grid. **Every** way of closing has to do this, which is
        // why it is one function - Escape used to close without it and stranded
        // whatever was in the grid.
        const auto closeScreen = [&] {
            const auto giveBack = [&](game::ItemStack& stack) {
                if (stack.empty()) {
                    return;
                }
                const int left = inventory.add(stack.item, stack.count);
                if (left > 0) {
                    drops.spawn(camera.position + camera.forward() * 0.5f, stack.item, left,
                                camera.forward() * kThrowSpeed, kThrowPickupDelay);
                }
                stack = game::ItemStack{};
            };

            giveBack(heldStack);
            // A furnace keeps what is inside it - the slots were only ever a
            // view onto the block, so they are DISCARDED here rather than
            // handed back. Leaving them populated would show the furnace's
            // contents in the next screen's crafting grid, and hand the player
            // a free copy when *that* screen closed. A crafting grid's own
            // contents do come back, because they exist only while it is up.
            if (openScreen == game::inventoryScreen::Kind::Furnace) {
                craftSlots.fill(game::ItemStack{});
                furnaceOutput = game::ItemStack{};
            } else {
                for (game::ItemStack& slot : craftSlots) {
                    giveBack(slot);
                }
            }
            catalogue.searchFocused = false;
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
        };

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

                append(game::inventoryScreen::build(
                    *openScreen, inventory, craftSlots.data(),
                    *openScreen == game::inventoryScreen::Kind::Stonecutter ? shownCuts.data()
                                                                           : &shownResult,
                    heldStack, pointerX, pointerY, renderer.aspectRatio(),
                    catalogue, progress, creative,
                    chests.count(openChestPosition) != 0 ? &chests.at(openChestPosition) : nullptr,
                    chests.count(openChestPartner) != 0 ? &chests.at(openChestPartner) : nullptr, clipped,
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
        // every other plant needs something solid.
        const auto hasSupportUnder = [&world](game::BlockId block, int x, int y, int z) {
            return game::restsOnWater(block) ? game::isWaterSource(world.blockAt(x, y - 1, z))
                                             : world.isSolid(x, y - 1, z);
        };

        // A T of iron blocks with a carved pumpkin on its head becomes an iron
        // golem. Checked from the pumpkin outward, in all three orientations.
        //
        // **The four corner cells must be exactly `Air`, not merely
        // non-occluding.** That reads like the mistake this codebase keeps
        // making — asking `== Air` where `occludesFace` is the general
        // question — but here it is what the reference actually tests: a snow
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
            {
                const std::string typed = window.consumeTypedText();
                if (typingSearch && !typed.empty()) {
                    // Capped at what the field can show; a longer query would
                    // scroll out of the box with no way to see it.
                    constexpr std::size_t kMaxQuery = 22;
                    for (const char c : typed) {
                        if (catalogue.query.size() < kMaxQuery) {
                            catalogue.query.push_back(c);
                        }
                    }
                    catalogue.scrollRow = 0;
                    hudDirty = true;
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
                    if (key == engine::Key::Backspace && !catalogue.query.empty()) {
                        catalogue.query.pop_back();
                        catalogue.scrollRow = 0;
                        hudDirty = true;
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
                        drops.spawn(camera.position + camera.forward() * 0.5f, heldStack.item, heldStack.count,
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
                    const int wanted = world.visibleRadius() + step;
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
                    static constexpr std::array<const char*, 4> kForcedNames{"cycling", "rain",
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
                            catalogue.searchFocused = false;
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

                // Puts every dragged slot back as it was and hands the cursor
                // its stack back, so a distribution can be replayed over a
                // longer list without compounding.
                const auto rewindDrag = [&] {
                    for (auto& [at, before] : draggedSlots) {
                        if (game::ItemStack* stack = stackAt(at); stack != nullptr) {
                            *stack = before;
                        }
                    }
                    heldStack = dragOriginalCursor;
                };

                const auto applyDrag = [&] {
                    rewindDrag();
                    std::vector<game::ItemStack*> targets;
                    targets.reserve(draggedSlots.size());
                    for (auto& [at, before] : draggedSlots) {
                        if (game::ItemStack* stack = stackAt(at); stack != nullptr) {
                            targets.push_back(stack);
                        }
                    }
                    game::slots::distribute(targets, heldStack, dragButton == DragButton::Right);
                };

                if (dragButton != DragButton::None) {
                    const bool stillHeld = dragButton == DragButton::Left ? leftDown : rightDown;

                    if (stillHeld) {
                        if (hit.has_value() && hit->region != Region::CraftResult) {
                            const bool already =
                                std::any_of(draggedSlots.begin(), draggedSlots.end(), [&](const auto& seen) {
                                    return seen.first.region == hit->region && seen.first.index == hit->index;
                                });
                            if (!already) {
                                rewindDrag();
                                if (game::ItemStack* stack = stackAt(*hit); stack != nullptr) {
                                    draggedSlots.emplace_back(*hit, *stack);
                                }
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
                                if (game::ItemStack* stack = stackAt(draggedSlots.front().first); stack != nullptr) {
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
                            catalogue.searchFocused = onField;
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
                            catalogue.searchFocused = false;
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
                            drops.spawn(camera.position + camera.forward() * 0.5f, heldStack.item, thrown,
                                        camera.forward() * kThrowSpeed, kThrowPickupDelay);
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
                                if (batch.empty() || !inventory.hasRoomFor(batch.item, batch.count)) {
                                    break;
                                }
                                inventory.add(batch.item, batch.count);
                                spendIngredients();
                            }
                        } else if (game::ItemStack* moving = stackAt(*hit); moving != nullptr) {
                            game::slots::quickMove(*moving, quickMoveTargets(*hit));
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
                        const bool ontoSame =
                            !intoCursor && heldStack.item == made.item && heldStack.space() >= made.count;
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
                            if (heldStack.empty()) {
                                heldStack = *stack;
                                *stack = game::ItemStack{};
                            } else if (heldStack.item == stack->item) {
                                const int moved = std::min(stack->count, heldStack.space());
                                heldStack.count += moved;
                                stack->count -= moved;
                                if (stack->count <= 0) {
                                    *stack = game::ItemStack{};
                                }
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
                        sources.reserve(game::kInventorySlots);
                        for (std::size_t i = 0; i < game::kInventorySlots; ++i) {
                            sources.push_back(&inventory.slot(i));
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
                        dragOriginalCursor = heldStack;
                        draggedSlots.clear();
                        draggedSlots.emplace_back(*hit, *stack);
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
            } else if (hadCursor) {
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
            const bool statusMoved = player.health != lastShownHealth ||
                                     player.food != lastShownFood || air != lastShownAir ||
                                     (player.hurtFlash > 0.0f) != lastShownHurt;
            if (statusMoved) {
                hudDirty = true;
                lastShownHealth = player.health;
                lastShownFood = player.food;
                lastShownAir = air;
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
                move.invulnerable = creative;
                move.verticalWish = (jumping ? 1.0f : 0.0f) - (sneaking ? 1.0f : 0.0f);
            }

            game::updatePlayer(player, move, world, deltaSeconds);
            camera.position = player.renderEyePosition();

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
                            const game::ItemId yield = game::dropForBlock(crop);
                            if (yield != game::ItemId::None) {
                                drops.spawn(glm::vec3{plant} + glm::vec3{0.5f}, yield,
                                            game::dropCountForBlock(crop));
                            }
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
            if (player.inWater != wasInWater) {
                sounds.play(audio, game::SoundEvent::Splash, player.position, 0.5f);
            }
            wasInWater = player.inWater;

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
            }
            wasAlive = player.alive();

            if (!player.alive()) {
                hudDirty = true;
                if (player.deathSeconds >= game::survival::kRespawnSeconds) {
                    // Everything carried is thrown down where it fell, which is
                    // the reference's rule and the only reason death costs
                    // anything at all.
                    if (!creative) {
                        for (std::size_t slot = 0; slot < inventory.size(); ++slot) {
                            game::ItemStack& stack = inventory.slot(slot);
                            if (!stack.empty()) {
                                drops.spawn(player.position + glm::vec3{0.0f, 0.5f, 0.0f},
                                            stack.item, stack.count);
                                stack = game::ItemStack{};
                            }
                        }
                    }
                    closeScreen();
                    // A bed that has been slept in wins over the world spawn,
                    // and losing the bed falls back rather than stranding you.
                    int rx = spawnX;
                    int rz = spawnZ;
                    if (hasRespawnPoint &&
                        game::isBed(world.blockAt(respawnPoint.x, respawnPoint.y,
                                                  respawnPoint.z))) {
                        rx = respawnPoint.x;
                        rz = respawnPoint.z;
                    } else if (hasRespawnPoint) {
                        hasRespawnPoint = false;
                        engine::logInfo("Your bed was missing or blocked.");
                    }
                    game::respawnPlayer(
                        player, glm::vec3{static_cast<float>(rx) + 0.5f,
                                          static_cast<float>(world.groundHeight(rx, rz)),
                                          static_cast<float>(rz) + 0.5f});
                    engine::logInfo("Respawned.");
                }
            }

            // Being underwater has to be visible, not merely felt. Without this
            // the only evidence is the physics changing, which reads as gravity
            // being broken rather than as swimming.
            {
                const game::BlockId eyeIn =
                    world.blockAt(static_cast<int>(std::floor(camera.position.x)),
                                  static_cast<int>(std::floor(camera.position.y)),
                                  static_cast<int>(std::floor(camera.position.z)));
                eyeUnderwater = game::isWater(eyeIn);
                eyeInLava = game::isLava(eyeIn);
            }

            const game::RaycastHit target = game::raycast(world, camera.position, camera.forward(), kBlockReach);

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
                    drops.spawn(glm::vec3{target.block} + glm::vec3{0.5f, 1.1f, 0.5f},
                                loaded->second, 1, glm::vec3{0.0f});
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
                    // nobody has touched costs nothing.
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
                    chests.try_emplace(openChestPosition);
                    chests.try_emplace(openChestPartner);
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
                    openScreen = game::inventoryScreen::Kind::Stonecutter;
                } else if (opened == game::BlockId::SmithingTable ||
                           opened == game::BlockId::Grindstone || opened == game::BlockId::Anvil ||
                           opened == game::BlockId::ChippedAnvil ||
                           opened == game::BlockId::DamagedAnvil ||
                           opened == game::BlockId::BrewingStand) {
                    // All of them are two inputs and a previewed result, so they
                    // share one screen and differ only in what that result is.
                    openBench = opened;
                    openScreen = game::inventoryScreen::Kind::SmithingTable;
                } else {
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
                } else if (aimed == game::BlockId::Bell) {
                    // A bell has one state and one behaviour: it rings. The
                    // reference's swing is an animated model rather than a
                    // block state, and we have no animated block models, so the
                    // sound is the whole of it.
                    sounds.play(audio, game::SoundEvent::Bell,
                                glm::vec3{target.block} + glm::vec3{0.5f}, 1.0f);
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
            const bool playing = hadCursor && !openScreen.has_value() && !swallowClickUntilRelease;
            const bool wantBreak = playing && (window.isMouseButtonDown(engine::MouseButton::Left) ||
                                               pad.down(game::PadButton::RightTrigger));
            const bool wantPlace = playing && (window.isMouseButtonDown(engine::MouseButton::Right) ||
                                               pad.down(game::PadButton::LeftTrigger));
            const bool wantDrop = !openScreen.has_value() && (window.isKeyDown(engine::Key::Q) ||
                                                              pad.down(game::PadButton::DpadDown));

            if (!wantDrop) {
                dropTimer = 0.0f;
            } else {
                dropTimer -= deltaSeconds;
                if (dropTimer <= 0.0f) {
                    game::ItemStack& held = inventory.slot(selectedSlot);
                    if (!held.empty()) {
                        drops.spawn(camera.position + camera.forward() * 0.5f, held.item, 1,
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
                        const glm::vec3 from = camera.position - glm::vec3{0.0f, 0.1f, 0.0f};
                        // Blocks per tick, which is the unit the whole
                        // projectile system is written in.
                        glm::vec3 launch =
                            camera.forward() * (game::projectileInfo(game::ProjectileKind::Arrow).power * charge);
                        // The shooter's own momentum carries into the shot, but
                        // only the vertical part, and only while airborne -
                        // otherwise running along the ground would lift your aim.
                        launch += player.velocity * (1.0f / 20.0f) *
                                  glm::vec3{1.0f, player.onGround ? 0.0f : 1.0f, 1.0f};

                        const bool spendsArrows = !creative;
                        projectiles.spawn(game::ProjectileKind::Arrow, from, launch,
                                          charge >= 1.0f, spendsArrows);
                        // Pitched by the charge, so a snap shot sounds thinner
                        // than a full draw - one number doing two jobs.
                        sounds.playGlobal(audio, game::SoundEvent::Bow, 0.7f, 0.85f + charge * 0.35f);
                        game::playRumble(rumble, game::RumbleEvent::BowLoosed,
                                         game::rumbleStrength(charge, game::kMinBowCharge, 1.0f));
                        if (spendsArrows) {
                            inventory.consume(game::ItemId::Arrow, 1);
                            game::ItemStack& bow = inventory.slot(selectedSlot);
                            if (++bow.damage >= game::kBowDurability) {
                                bow = game::ItemStack{};
                            }
                            hudDirty = true;
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
                world, camera.position, camera.position + camera.forward() * armsLength);
            const float entityReach = swingBlocked.hit ? swingBlocked.distance : armsLength;
            const bool creatureInWay =
                wantBreak && creatures.aimedAt(camera.position, camera.forward(), entityReach);
            if (creatureInWay && swingTimer <= 0.0f) {
                const game::ToolProperties swung = game::toolFor(inventory.slot(selectedSlot).item);
                const int damage = swung.kind == game::ToolKind::Sword ? (swung.tier >= game::kStoneTier ? 5 : 4)
                                                                       : 1 + swung.tier;
                if (creatures.strike(camera.position, camera.forward(), entityReach, damage)) {
                    swingTimer = kSwingSeconds;
                    player.exhaustion += game::survival::kExhaustAttack;
                    // A bare fist deals 1 and the best tool in the game deals 6,
                    // which is the whole range a blow of ours can land in.
                    game::playRumble(rumble, game::RumbleEvent::HitCreature,
                                     game::rumbleStrength(static_cast<float>(damage), 1.0f, 6.0f));
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
                const float seconds = creative ? 0.0f : game::breakSeconds(aimed, tool.item);

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
                    // Only an instant break needs holding back. A timed one is
                    // already paced by its own progress bar, and pausing it too
                    // would make ordinary mining stutter.
                    breakCooldown = seconds <= 0.0f ? kBreakRepeatSeconds : 0.0f;
                    const game::BlockId broken = aimed;
                    // **Before the block goes**, because the pairing is derived
                    // from what is standing there and asking afterwards returns
                    // nothing.
                    const std::optional<glm::ivec3> brokenPartner =
                        game::isChest(broken) ? chestPartnerAt(target.block) : std::nullopt;
                    // Which stored contents the dropped item will carry, or 0
                    // for everything that is not a stowbox with something in it.
                    int brokenStowHandle = 0;
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
                                    drops.spawn(centre, stack->item, stack->count);
                                }
                            }
                            furnaces.erase(found);
                        }
                        if (openScreen == game::inventoryScreen::Kind::Furnace &&
                            openFurnacePosition == target.block) {
                            closeScreen();
                        }
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
                                        drops.spawn(centre, stack.item, stack.count,
                                                    glm::vec3{0.0f}, 0.35f, stack.damage);
                                    }
                                }
                            }
                            chests.erase(found);
                        }
                        if (openScreen.has_value() &&
                            game::inventoryScreen::isContainer(*openScreen) &&
                            (openChestPosition == target.block || openChestPartner == target.block)) {
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
                            world.setBlock(other.x, other.y, other.z, game::BlockId::Air);
                            const auto pair = chests.find(other);
                            if (pair != chests.end()) {
                                const glm::vec3 centre = glm::vec3{other} + glm::vec3{0.5f};
                                for (const game::ItemStack& stack : pair->second.slots) {
                                    if (!stack.empty()) {
                                        drops.spawn(centre, stack.item, stack.count);
                                    }
                                }
                                chests.erase(pair);
                            }
                            const game::ItemId half = game::dropForBlock(otherBlock);
                            if (half != game::ItemId::None &&
                                (creative || game::yieldsDrop(otherBlock, tool.item))) {
                                drops.spawn(glm::vec3{other} + glm::vec3{0.5f}, half, 1);
                            }
                        }
                    }

                    // Breaking yields its drop in every mode, but only if what
                    // you are holding is good enough for it. Stone mined by hand
                    // gives nothing, which is what makes a pickaxe worth making.
                    const game::ItemId dropped = game::dropForBlock(broken);
                    if (dropped != game::ItemId::None && (creative || game::yieldsDrop(broken, tool.item))) {
                        drops.spawn(glm::vec3{target.block} + glm::vec3{0.5f}, dropped,
                                    game::dropCountForBlock(broken), glm::vec3{0.0f}, 0.35f,
                                    brokenStowHandle);

                        // Gravel gives up flint about one time in ten, the
                        // reference's own rate and the only source of the one
                        // ingredient an arrow cannot be made without.
                        //
                        // **Rolled off the block's own position, not a running
                        // random.** Every other drop in the game is a pure
                        // function of what was broken, and a roll that changed
                        // between two identical actions would be the one place
                        // that stopped being true.
                        if (broken == game::BlockId::Gravel) {
                            const auto mix = static_cast<unsigned>(target.block.x * 73856093 ^
                                                                   target.block.y * 19349663 ^
                                                                   target.block.z * 83492791);
                            if ((mix ^ (mix >> 13)) % 10u == 0u) {
                                drops.spawn(glm::vec3{target.block} + glm::vec3{0.5f},
                                            game::ItemId::Flint, 1);
                            }
                        }
                    }

                    // A door is two blocks and one thing: breaking either half
                    // takes the other, and only one door drops because only the
                    // lower half is a canonical item.
                    if (game::isDoor(broken)) {
                        const int step = game::doorIsUpper(broken) ? -1 : 1;
                        const glm::ivec3 other{target.block.x, target.block.y + step,
                                               target.block.z};
                        const game::BlockId twin = world.blockAt(other.x, other.y, other.z);
                        if (game::isDoor(twin) &&
                            game::doorFamily(twin) == game::doorFamily(broken)) {
                            world.setBlock(other.x, other.y, other.z, game::BlockId::Air);
                        }
                    }
                    // A bed is the same idea lying down: the other half is one
                    // step along the facing, forward from the foot or back from
                    // the head.
                    if (game::isBed(broken)) {
                        const game::FaceDirection along =
                            game::bedIsHead(broken) ? game::oppositeDirection(game::bedFacing(broken))
                                                    : game::bedFacing(broken);
                        const glm::ivec3 step =
                            along == game::FaceDirection::PosX   ? glm::ivec3{1, 0, 0}
                            : along == game::FaceDirection::NegX ? glm::ivec3{-1, 0, 0}
                            : along == game::FaceDirection::PosZ ? glm::ivec3{0, 0, 1}
                                                                 : glm::ivec3{0, 0, -1};
                        const glm::ivec3 other = target.block + step;
                        const game::BlockId twin = world.blockAt(other.x, other.y, other.z);
                        if (game::isBed(twin) && game::bedColour(twin) == game::bedColour(broken)) {
                            world.setBlock(other.x, other.y, other.z, game::BlockId::Air);
                        }
                    }

                    // Tools wear only on blocks that actually resist them.
                    if (!creative && game::blockHardness(broken) > 0.0f) {                        const game::ToolProperties properties = game::toolFor(tool.item);
                        if (properties.durability > 0) {
                            game::ItemStack& held = inventory.slot(selectedSlot);
                            if (++held.damage >= properties.durability) {
                                held = game::ItemStack{};
                            }
                            hudDirty = true;
                        }
                    }

                    // Whatever was resting on it comes down too, rather than
                    // being left hanging in the air.
                    const glm::ivec3 above{target.block.x, target.block.y + 1, target.block.z};
                    const game::BlockId resting = world.blockAt(above.x, above.y, above.z);
                    if (game::needsSupportBelow(resting) &&
                        !hasSupportUnder(resting, above.x, above.y, above.z)) {
                        world.setBlock(above.x, above.y, above.z, game::BlockId::Air);
                        const game::ItemId shed = game::dropForBlock(resting);
                        if (shed != game::ItemId::None) {
                            drops.spawn(glm::vec3{above} + glm::vec3{0.5f}, shed, 1);
                        }
                    }

                    // A ladder is held up sideways rather than from below, so
                    // it is the four neighbours that have to be checked instead
                    // of the one cell above.
                    for (const glm::ivec3& step : {glm::ivec3{1, 0, 0}, glm::ivec3{-1, 0, 0},
                                                   glm::ivec3{0, 0, 1}, glm::ivec3{0, 0, -1}}) {
                        const glm::ivec3 beside = target.block + step;
                        const game::BlockId hung = world.blockAt(beside.x, beside.y, beside.z);
                        if (!game::isLadder(hung)) {
                            continue;
                        }
                        const game::FaceDirection wall = game::ladderFacing(hung);
                        const bool lostIt = (wall == game::FaceDirection::PosX && step.x < 0) ||
                                            (wall == game::FaceDirection::NegX && step.x > 0) ||
                                            (wall == game::FaceDirection::PosZ && step.z < 0) ||
                                            (wall == game::FaceDirection::NegZ && step.z > 0);
                        if (lostIt) {
                            world.setBlock(beside.x, beside.y, beside.z, game::BlockId::Air);
                            drops.spawn(glm::vec3{beside} + glm::vec3{0.5f},
                                        game::dropForBlock(hung), 1);
                        }
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
                if (drinking || (!held.empty() && game::survival::isEdible(held.item) &&
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
                            // timer, and theirs would be zero.
                            if (brew.effect == game::effects::Effect::InstantHealth) {
                                game::healPlayer(
                                    player, static_cast<int>(game::effects::instantAmount(
                                                brew.effect, brew.amplifier)));
                            } else if (brew.effect == game::effects::Effect::InstantDamage) {
                                game::damagePlayer(
                                    player, static_cast<int>(game::effects::instantAmount(
                                                brew.effect, brew.amplifier)));
                            } else if (brew.effect != game::effects::Effect::None) {
                                player.effects.apply(brew.effect, brew.amplifier, brew.seconds);
                            }
                            // Only the turtle master carries a second effect,
                            // and it is the reason `PotionKind` has room for one.
                            if (brew.second != game::effects::Effect::None) {
                                player.effects.apply(brew.second, brew.secondAmplifier,
                                                     brew.seconds);
                            }
                            if (!creative) {
                                inventory.consumeOne(selectedSlot);
                                // The glass survives. If there is nowhere to put
                                // it, it goes on the floor rather than nowhere.
                                if (inventory.add(game::ItemId::GlassBottle, 1) > 0) {
                                    drops.spawn(camera.position + camera.forward() * 0.6f,
                                                game::ItemId::GlassBottle, 1, glm::vec3{0.0f});
                                }
                            }
                        } else {
                            game::feedPlayer(player, game::survival::foodValue(held.item));
                            if (!creative) {
                                inventory.consumeOne(selectedSlot);
                            }
                        }
                        sounds.playGlobal(audio, game::SoundEvent::Burp, 0.5f);
                        hudDirty = true;
                    } else if (player.eatingSeconds - deltaSeconds <= 0.0f) {
                        // One bite at the start rather than a chew loop, which
                        // is a whole timer for something nobody would miss.
                        sounds.playGlobal(audio, game::SoundEvent::Eat, 0.5f);
                    }
                } else {
                    player.eatingSeconds = 0.0f;
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
                        game::raycast(world, camera.position, camera.forward(), kBlockReach, true);
                    // A cauldron with water in it fills one too, and does not
                    // empty itself doing so - the reference takes a level, but
                    // ours has no bottle-sized level to take.
                    const bool fromWater =
                        reached.hit && game::isWaterSource(world.blockAt(
                                           reached.block.x, reached.block.y, reached.block.z));
                    if (fromWater || player.underwater) {
                        if (!creative) {
                            inventory.consumeOne(selectedSlot);
                            if (inventory.add(game::ItemId::WaterBottle, 1) > 0) {
                                drops.spawn(camera.position + camera.forward() * 0.6f,
                                            game::ItemId::WaterBottle, 1, glm::vec3{0.0f});
                            }
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
                        game::raycast(world, camera.position, camera.forward(), kBlockReach, filling);

                    bool used = false;
                    game::ItemId became = game::ItemId::Bucket;

                    if (held.item == game::ItemId::MilkBucket) {
                        // Drinking, and **milk clears every effect there is** -
                        // which is the one thing it is for, and the reason it is
                        // worth carrying beside a potion of harming.
                        player.effects.clear();
                        used = true;
                    } else if (filling) {
                        // A cow first: an empty bucket aimed at one milks it,
                        // and only falls through to the world if it misses.
                        const std::size_t milked =
                            creatures.findMilkable(camera.position, camera.forward(), kBlockReach);
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
                        world.setBlock(reached.adjacent.x, reached.adjacent.y, reached.adjacent.z,
                                       lava ? game::BlockId::Lava0 : game::BlockId::Water0);
                        sounds.play(audio,
                                    lava ? game::SoundEvent::BucketEmptyLava
                                         : game::SoundEvent::BucketEmpty,
                                    glm::vec3{reached.adjacent} + glm::vec3{0.5f}, 0.8f);
                        used = true;
                    }

                    if (used) {
                        // Emptying and filling is the *cost* of using a bucket,
                        // which is the one thing creative is allowed to skip.
                        if (!creative) {
                            game::ItemStack& slot = inventory.slot(selectedSlot);
                            if (slot.count > 1) {
                                // A stack of empties gives back one full bucket,
                                // so the swap has to go somewhere else.
                                --slot.count;
                                inventory.add(became, 1);
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
                            (inCell == game::BlockId::Air || game::isWashedAway(inCell)) &&
                            world.fireCanSurvive(lightCell.x, lightCell.y, lightCell.z)) {
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
                            inventory.add(became, 1);
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
                        inventory.add(shelf.item, shelf.count);
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
                        // it yields exactly one bone meal and empties it.
                        inventory.add(game::ItemId::BoneMeal, 1);
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
                            world.setBlock(target.block.x, target.block.y, target.block.z,
                                           game::composterAt(level + 1));
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
                    // does, so this is the break path's own rule rather than a
                    // second copy of it that could drift.
                    const auto wearHeldTool = [&] {
                        if (creative) {
                            return;
                        }
                        const game::ToolProperties properties = game::toolFor(used);
                        if (properties.durability <= 0) {
                            return;
                        }
                        game::ItemStack& slot = inventory.slot(selectedSlot);
                        if (++slot.damage >= properties.durability) {
                            slot = game::ItemStack{};
                        }
                        hudDirty = true;
                    };

                    if (used == game::ItemId::GlassBottle && target.hit) {
                        // A bottle fills from any water, source or flowing -
                        // unlike a bucket, which needs a source.
                        const game::RaycastHit wet = game::raycast(
                            world, camera.position, camera.forward(), kBlockReach, true);
                        if (wet.hit &&
                            game::isWater(world.blockAt(wet.block.x, wet.block.y, wet.block.z))) {
                            if (!creative) {
                                game::ItemStack& slot = inventory.slot(selectedSlot);
                                if (slot.count > 1) {
                                    --slot.count;
                                    inventory.add(game::ItemId::WaterBottle, 1);
                                } else {
                                    slot = game::ItemStack{game::ItemId::WaterBottle, 1};
                                }
                                hudDirty = true;
                            }
                            placeTimer = kPlaceRepeatSeconds;
                        }
                    } else if ((used == game::ItemId::VoidPearl && pearlCooldown <= 0.0f) ||
                               used == game::ItemId::Egg || game::isThrownPotion(used)) {
                        // A thrown entity that arcs, rather than a look-ray
                        // that dropped you where you were already aiming. Same
                        // anchor as the bow: spawning at the eye itself puts it
                        // through your own head at point-blank range.
                        const game::ProjectileKind kind =
                            used == game::ItemId::Egg          ? game::ProjectileKind::Egg
                            : game::isSplashPotion(used)       ? game::ProjectileKind::SplashPotion
                            : game::isLingeringPotion(used)    ? game::ProjectileKind::LingeringPotion
                                                               : game::ProjectileKind::Pearl;
                        const glm::vec3 from = camera.position - glm::vec3{0.0f, 0.1f, 0.0f};
                        glm::vec3 launch = camera.forward() * game::projectileInfo(kind).power;
                        // Blocks per tick, and only the vertical part while
                        // airborne - the same rule the bow uses, for the same
                        // reason: running along the ground must not lift a throw.
                        launch += player.velocity * (1.0f / 20.0f) *
                                  glm::vec3{1.0f, player.onGround ? 0.0f : 1.0f, 1.0f};
                        // The brew rides on the shot, because forty-one of them
                        // share two projectile kinds.
                        projectiles.spawn(kind, from, launch, false, false,
                                          game::isThrownPotion(used) ? used : game::ItemId::None);
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
                        const game::BlockId stripped = game::strippedFor(bark);
                        if (stripped != bark) {
                            world.setBlock(target.block.x, target.block.y, target.block.z, stripped);
                            placeTimer = kPlaceRepeatSeconds;
                        }
                    } else if (game::toolFor(used).kind == game::ToolKind::Hoe && target.hit) {
                        // Tilling. Only the **top** face may be worked and only
                        // with air above it, which is the reference's rule and
                        // the reason you cannot hoe the underside of an
                        // overhang into a field.
                        const game::BlockId soil =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        const game::BlockId tilled = game::farming::tilledFrom(soil);
                        const game::BlockId above = world.blockAt(
                            target.block.x, target.block.y + 1, target.block.z);
                        if (tilled != soil && (above == game::BlockId::Air ||
                                               game::isWashedAway(above))) {
                            world.setBlock(target.block.x, target.block.y, target.block.z, tilled);
                            sounds.play(audio, game::SoundEvent::DigGravel,
                                        glm::vec3{target.block} + glm::vec3{0.5f}, 0.7f);
                            wearHeldTool();
                            placeTimer = kPlaceRepeatSeconds;
                        }
                    } else if (game::toolFor(used).kind == game::ToolKind::Shovel && target.hit) {
                        const game::BlockId soil =
                            world.blockAt(target.block.x, target.block.y, target.block.z);
                        const game::BlockId path = game::farming::pathFrom(soil);
                        const game::BlockId above = world.blockAt(
                            target.block.x, target.block.y + 1, target.block.z);
                        if (path != soil && (above == game::BlockId::Air ||
                                             game::isWashedAway(above))) {
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
                        if (game::farming::canSowOn(used, ground) &&
                            (inCell == game::BlockId::Air || game::isWashedAway(inCell))) {
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
                        if (game::farming::canSowOn(used, ground) &&
                            (inCell == game::BlockId::Air || game::isWashedAway(inCell))) {
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
                        drops.spawn(glm::vec3{target.block} + glm::vec3{0.5f, 1.0f, 0.5f},
                                    game::ItemId::PumpkinSeeds, 1);
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
                if (placeTimer <= 0.0f && target.hit) {
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
                    floating ? game::raycast(world, camera.position, camera.forward(), kBlockReach, true)
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
                if (placeTimer <= 0.0f && canPlace && placeTarget.hit &&
                    !game::playerOverlapsBlock(player, placeCell)) {
                    game::BlockId placing = game::blockForItem(held.item);
                    glm::ivec3 where = placeCell;

                    const bool clickedAbove = target.adjacent.y > target.block.y;
                    const bool clickedBelow = target.adjacent.y < target.block.y;

                    // Two halves meeting in one cell become a whole block. Left
                    // as separate halves they stack as slab, gap, slab, which is
                    // never what anyone is trying to build. **Both halves have
                    // to be the same material**, or a spruce slab dropped on a
                    // stone one silently produced a block of stone.
                    const bool completesSlab =
                        game::isSlab(placing) && game::isSlab(aimedAt) &&
                        game::slabFamily(placing) == game::slabFamily(aimedAt) &&
                        (game::isUpperHalf(aimedAt) ? clickedBelow : clickedAbove);

                    if (completesSlab) {
                        placing = game::kSlabFamilies[static_cast<std::size_t>(
                                                          game::slabFamily(placing))]
                                      .parent;
                        where = target.block;
                    } else if (game::isSlab(placing)) {
                        // Clicking an underside puts the half up against it.
                        placing = game::slabAt(game::slabFamily(placing), clickedBelow);
                    } else if (game::isStairs(placing)) {
                        // Oriented blocks take their facing from the camera and
                        // their half from which end of the block was clicked,
                        // which is what lets you build a staircase that turns.
                        const glm::vec3 aim = camera.forward();
                        const game::Facing facing =
                            std::abs(aim.x) > std::abs(aim.z)
                                ? (aim.x > 0.0f ? game::Facing::West : game::Facing::East)
                                : (aim.z > 0.0f ? game::Facing::North : game::Facing::South);
                        placing = game::stairsAt(game::stairFamily(placing), facing, clickedBelow);
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
                            placing = game::lightningRodAt(
                                into.y > 0 ? game::Facing6Up
                                : into.y < 0
                                    ? game::Facing6Down
                                    : game::directionAsFacing6(game::oppositeDirection(wall)),
                                false);
                        } else if (game::isTripwireHook(placing)) {
                            placing = wall == game::FaceDirection::Unknown
                                          ? game::BlockId::Air
                                          : game::tripwireHookAt(
                                                game::oppositeDirection(wall), false, false);
                        } else if (game::isRail(placing)) {
                            // Flat, running the way the player is facing. The
                            // reference derives the shape from its neighbours
                            // and re-derives it whenever one changes; ours is a
                            // named divergence and lays straight track.
                            const bool alongX = std::abs(aim.x) > std::abs(aim.z);
                            placing = game::railAt(game::railFamily(placing), alongX ? 1 : 0, false);
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
                        if (!game::isReplaceable(above)) {
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
                        // Top or bottom half by which way you were looking when
                        // you hung it, which is the only way to get one under a
                        // ceiling without a per-face click position.
                        const bool top = camera.forward().y > 0.0f;
                        placing = game::trapdoorAt(
                            game::trapdoorFamily(placing),
                            game::facingToward(camera.forward().x, camera.forward().z), false, top);
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
                        const game::BlockId underHead =
                            world.blockAt(headCell.x, headCell.y - 1, headCell.z);
                        if (!game::isReplaceable(atHead) || !game::isSolid(underHead)) {
                            placing = game::BlockId::Air;
                        } else {
                            const int colour = game::bedColour(placing);
                            placing = game::bedAt(colour, away, false);
                            world.setBlock(headCell.x, headCell.y, headCell.z,
                                           game::bedAt(colour, away, true));
                        }
                    } else if (game::isBeehive(placing)) {
                        // Same rule again: a hive has one entrance, and the bees
                        // that will use it need to know which side it is on.
                        const glm::vec3 aim = camera.forward();
                        const game::FaceDirection front =
                            std::abs(aim.x) > std::abs(aim.z)
                                ? (aim.x > 0.0f ? game::FaceDirection::NegX : game::FaceDirection::PosX)
                                : (aim.z > 0.0f ? game::FaceDirection::NegZ : game::FaceDirection::PosZ);
                        placing = game::beehiveAt(front, game::beehiveHasHoney(placing));
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
                            placing = game::cocoaAt(wall, 0);
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
                               !hasSupportUnder(placing, where.x, where.y, where.z)) {
                        placeTimer = kPlaceRepeatSeconds;
                    } else {
                        world.setBlock(where.x, where.y, where.z, placing);
                        // The one construction in the game: a T of iron blocks
                        // with a carved pumpkin on top becomes an iron golem.
                        // **Hung off the placement of the pumpkin**, because
                        // that is the reference's rule — the head must go on
                        // last — and because this is already the single point
                        // where the main thread writes a block.
                        if (game::isCarvedPumpkin(placing) || game::isJackOLantern(placing)) {
                            tryRaiseGolem(where);
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
                const bool lit = game::tickFurnace(furnace, deltaSeconds * game::cookSpeed(present));
                // Lighting one must not turn it round or change what kind it is:
                // all three live in the id, so they are recombined rather than
                // one being overwritten with a default.
                const game::BlockId wanted =
                    game::cookerAt(present, game::furnaceFacing(present), lit);
                if (present != wanted) {
                    world.setBlock(position.x, position.y, position.z, wanted);
                }
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
                        moveOneItem(chests[cell], containerSlots(self), chests[pours],
                                    containerSlots(ahead));
                    }

                    const glm::ivec3 over = cell + glm::ivec3{0, 1, 0};
                    const game::BlockId above = world.blockAt(over.x, over.y, over.z);
                    if (game::isChest(above) || game::isHopper(above)) {
                        moveOneItem(chests[over], containerSlots(above), chests[cell],
                                    containerSlots(self));
                    }
                }
            }

            // Anything a spreading flow swept aside - or a falling block landed
            // on - drops, the same way a plant left hanging by mining below it
            // does.
            for (const game::World::WashedBlock& washed : world.takeWashedBlocks()) {
                const game::ItemId shed = game::dropForBlock(washed.block);
                if (shed != game::ItemId::None) {
                    drops.spawn(glm::vec3{washed.position} + glm::vec3{0.5f}, shed, 1);
                }
            }

            // Blocks the world just took out of the grid become entities, and
            // the entities put themselves back when they land.
            for (const game::World::WashedBlock& detached : world.takeDetachedBlocks()) {
                fallingBlocks.spawn(detached.position, detached.block);
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
            // other drop.
            for (const game::Creatures::Loot& loot : creatures.takeLoot()) {
                drops.spawn(loot.position + glm::vec3{0.0f, 0.4f, 0.0f}, loot.item, loot.count);
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
                projectiles.spawn(kind, shot.origin, shot.velocity, false, false, shot.payload);
            }
            for (const game::FallingBlocks::Crushed& hit : fallingBlocks.update(world, deltaSeconds)) {
                const game::ItemId shed = game::dropForBlock(hit.block);
                if (shed != game::ItemId::None) {
                    drops.spawn(glm::vec3{hit.position} + glm::vec3{0.5f}, shed, 1);
                }
            }

            // Dropped items live entirely on the main thread: there are a
            // handful of them and they touch the world only to read it.
            drops.update(world, player.position, deltaSeconds);
            projectiles.update(world, creatures, deltaSeconds);

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
                if (scale <= 0.0f) {
                    return;
                }
                const game::PotionKind brew = game::potionKind(potion);
                if (brew.effect == game::effects::Effect::InstantHealth) {
                    game::healPlayer(player, static_cast<int>(game::effects::instantAmount(
                                                 brew.effect, brew.amplifier) *
                                                 scale));
                } else if (brew.effect == game::effects::Effect::InstantDamage) {
                    game::damagePlayer(player, static_cast<int>(game::effects::instantAmount(
                                                   brew.effect, brew.amplifier) *
                                                   scale));
                } else if (brew.effect != game::effects::Effect::None) {
                    player.effects.apply(brew.effect, brew.amplifier, brew.seconds * scale);
                }
                if (brew.second != game::effects::Effect::None) {
                    player.effects.apply(brew.second, brew.secondAmplifier, brew.seconds * scale);
                }
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
                    // `egg.json`'s own odds: one throw in eight leaves a chick,
                    // and one of those in four leaves four instead.
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
                if (inventory.add(ready.item, ready.count) != 0) {
                    continue; // No room for this one; the next may still fit.
                }
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
            const bool night = game::sky::sunDirection(timeOfDay).y < -0.05f;
            if (settings.creatureShowcase == 0) {
                std::vector<game::CreatureExplosion> blasts;
                const game::CreatureAttack blow =
                    creatures.update(world, player.position, deltaSeconds, night, player.sneaking,
                                     timeOfDay, blasts);
                if (blow.landed) {
                    // The damage was computed and discarded for four milestones
                    // because there was nothing to apply it to. `damagePlayer`
                    // owns the half-second invulnerability window, so a pack
                    // standing on you still only lands twice a second.
                    if (!creative) {
                        game::damagePlayer(player, blow.damage);
                    }
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

                // Charges that finished their fuse join the creature blasts, so
                // the whole destroy-spill-drop path below is shared rather than
                // written twice. Power 4 is the reference's TNT.
                for (const glm::ivec3& cell : world.takeDetonations()) {
                    blasts.push_back({glm::vec3{cell} + glm::vec3{0.5f}, kTntPower});
                }

                for (const game::CreatureExplosion& blast : blasts) {
                    // Applied here rather than inside the creature system, which
                    // reads the world and never writes it.
                    blastRandom ^= blastRandom << 13;                    blastRandom ^= blastRandom >> 17;
                    blastRandom ^= blastRandom << 5;
                    const std::vector<glm::ivec3> destroyed =
                        game::explosionBlocks(world, blast.centre, blast.power, blastRandom);
                    int dropped = 0;
                    for (const glm::ivec3& cell : destroyed) {
                        const game::BlockId removed = world.blockAt(cell.x, cell.y, cell.z);
                        world.setBlock(cell.x, cell.y, cell.z, game::BlockId::Air);

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
                                        drops.spawn(centre, stack->item, stack->count);
                                    }
                                }
                                furnaces.erase(found);
                            }
                        }
                        if (game::isChest(removed)) {
                            const auto found = chests.find(cell);
                            if (found != chests.end()) {
                                const glm::vec3 centre = glm::vec3{cell} + glm::vec3{0.5f};
                                for (const game::ItemStack& stack : found->second.slots) {
                                    if (!stack.empty()) {
                                        drops.spawn(centre, stack.item, stack.count);
                                    }
                                }
                                chests.erase(found);
                            }
                        }

                        // Whatever was blown up has already spilled its contents
                        // as drops, so a screen still open on it is showing a
                        // copy the player could take a second time.
                        if (openScreen.has_value() &&
                            ((*openScreen == game::inventoryScreen::Kind::Furnace && openFurnacePosition == cell) ||
                             ((*openScreen == game::inventoryScreen::Kind::Chest ||
                               *openScreen == game::inventoryScreen::Kind::DoubleChest) &&
                              (openChestPosition == cell || openChestPartner == cell)))) {
                            closeScreen();
                        }

                        // One block in `power` survives as an item, which is the
                        // reference's rule and the reason a blast is a net loss
                        // rather than a mining technique.
                        const game::ItemId drop = game::dropForBlock(removed);
                        blastRandom ^= blastRandom << 13;
                        blastRandom ^= blastRandom >> 17;
                        blastRandom ^= blastRandom << 5;
                        const float roll = static_cast<float>(blastRandom & 0xFFFFFFu) /
                                           static_cast<float>(0x1000000u);
                        if (drop != game::ItemId::None && roll * blast.power < 1.0f) {
                            drops.spawn(glm::vec3{cell} + glm::vec3{0.5f}, drop, 1);
                            ++dropped;
                        }
                    }

                    constexpr float kHalfWidth = game::player_constants::kWidth * 0.5f;
                    const game::Aabb body{
                        player.position - glm::vec3{kHalfWidth, 0.0f, kHalfWidth},
                        player.position + glm::vec3{kHalfWidth, game::player_constants::kHeight,
                                                    kHalfWidth}};
                    const float exposure = game::explosionExposure(world, blast.centre, body);
                    const float impact =
                        game::explosionImpact(blast.centre, blast.power, player.position, exposure);
                    if (impact > strongestBlast) {
                        // Aimed at the eyes rather than the feet, which is what
                        // gives a close blast its upward throw for free.
                        const glm::vec3 away = player.eyePosition() - blast.centre;
                        const float reach = glm::length(away);
                        if (reach > 0.001f) {
                            // Blocks per tick in the reference; ours is per
                            // second, so twenty times over. Only the strongest
                            // blast of the frame throws you - several each
                            // adding their own is the accumulator bug that once
                            // launched the player clear off the map.
                            strongestBlast = impact;
                            blastPush = away / reach * impact * kBlastKnockback;
                            blastDamage = game::explosionDamage(blast.power, impact);
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

                if (strongestBlast > 0.0f) {
                    // Damage follows the same "strongest of the frame" rule the
                    // throw does, rather than summing: several charges going off
                    // together should hit as hard as the worst of them, not as
                    // hard as all of them added up.
                    if (!creative) {
                        game::damagePlayer(player, blastDamage);
                    }
                    player.velocity += blastPush;
                    player.onGround = false;
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

            // Wrapped against the field's own period so a long session cannot
            // lose the fraction the noise is sampled at.
            cloudDrift = std::fmod(cloudDrift + deltaSeconds * kCloudDriftPerSecond, 100000.0f);
            renderer.setCloudDrift(cloudDrift);

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
            const float weatherWind = rainAmount * 4.0f + thunderAmount * 5.0f;
            const float windStrength = weatherWind + gust * (2.0f + weatherWind * 0.5f);

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
                        // on the strike.
                        if (std::abs(camera.position.x - bolt.position.x) <= 3.0f &&
                            std::abs(camera.position.z - bolt.position.z) <= 3.0f &&
                            camera.position.y > bolt.position.y - 3.0f &&
                            camera.position.y < bolt.position.y + 9.0f) {
                            game::damagePlayer(player, 5);
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
                // One bit, for the fire update: anything under an open sky goes
                // out while this holds.
                world.setPrecipitating(falls && rainAmount > 0.2f);

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
                if (settleTimer <= 0.0f && rainAmount > 0.2f) {
                    settleTimer = 0.08f;
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
                    const auto falling = game::weather::precipitationFor(
                        here, std::max(top, 0));
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
                        const bool warmed = world.blockLightAt(sx, top + 1, sz) >= 12;
                        if (open && !warmed) {
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
                            if (world.blockAt(sx, y + 1, sz) == game::BlockId::Air &&
                                game::isWaterSource(cell) &&
                                world.skyLightAt(sx, y + 1, sz) >= game::kMaxLight &&
                                world.blockLightAt(sx, y, sz) < 12) {
                                // A source only. Freezing a flowing cell is
                                // undone by the next fluid tick, which would
                                // leave the shoreline flickering.
                                world.setBlock(sx, y, sz, game::BlockId::Ice);
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
                // loop point. Eight recordings and a slightly early restart mean
                // the seam never lands twice in the same place. Silent under a
                // roof, which the sky light answers for free.
                rainSoundTimer -= deltaSeconds;
                if (level > 0.05f && !snow && rainSoundTimer <= 0.0f) {
                    const int sky = world.skyLightAt(static_cast<int>(std::floor(camera.position.x)),
                                                     static_cast<int>(std::floor(camera.position.y)),
                                                     static_cast<int>(std::floor(camera.position.z)));
                    const float sheltered = static_cast<float>(sky) / static_cast<float>(game::kMaxLight);
                    sounds.playGlobal(audio, game::SoundEvent::Rain, level * sheltered * 0.6f);
                    rainSoundTimer = 3.4f;
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
                if (rainAmount > 0.2f && precipitationKind == game::weather::Precipitation::Rain &&
                    splashTimer <= 0.0f) {
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
            renderer.setSunLighting(ambient, light.strength * overcast, game::sky::kAmbientFloor);
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

            frameLimiter.waitForNextFrame();
        }

        engine::logInfo("Window closed. Saving world.");
        world.saveAll();
        {
            game::SavedPlayer saved{player.position,   camera.yaw,        camera.pitch,
                                    player.health,     player.food,       player.saturation,
                                    player.exhaustion};
            for (std::size_t i = 0; i < game::kInventorySlots; ++i) {
                saved.inventory[i] = inventory.slot(i);
            }
            saved.selectedSlot = static_cast<std::int32_t>(selectedSlot);
            world.store().savePlayer(saved);
        }

        // Empty ones are dropped rather than written: a furnace nobody has used
        // is indistinguishable from one that has never been opened.
        std::vector<game::PlacedFurnace> savedFurnaces;
        savedFurnaces.reserve(furnaces.size());
        for (const auto& [position, furnace] : furnaces) {
            if (!furnace.idle()) {
                savedFurnaces.push_back(game::PlacedFurnace{position, furnace});
            }
        }
        world.store().saveFurnaces(savedFurnaces);

        std::vector<game::PlacedChest> savedChests;
        savedChests.reserve(chests.size());
        for (const auto& [position, chest] : chests) {
            if (!chest.empty()) {
                savedChests.push_back(game::PlacedChest{position, chest});
            }
        }
        world.store().saveChests(savedChests);

        std::vector<game::StowedBox> savedStowboxes;
        savedStowboxes.reserve(stowed.size());
        for (const auto& [handle, contents] : stowed) {
            if (!contents.empty()) {
                savedStowboxes.push_back(game::StowedBox{handle, contents});
            }
        }
        world.store().saveStowboxes(savedStowboxes);

        // And it must not write one back either. The showcase population is
        // three copies of whatever was being looked at; saving that would
        // replace the world's animals with it.
        std::vector<game::SavedCreature> savedCreatures;
        if (settings.creatureShowcase == 0) {
            savedCreatures.reserve(creatures.all().size());
            for (const game::Creature& creature : creatures.all()) {
                // Something caught part-way through falling over is already
                // gone; the body is only still there so the fall can finish.
                // Writing it out would reload a corpse that dies again on sight.
                if (creature.health <= 0) {
                    continue;
                }
                savedCreatures.push_back(game::SavedCreature{static_cast<std::uint8_t>(creature.kind),
                                                             creature.position.x, creature.position.y,
                                                             creature.position.z, creature.yaw,
                                                             creature.health, creature.scale,
                                                             static_cast<std::uint8_t>(creature.charged ? 1 : 0),
                                                             static_cast<std::uint8_t>(creature.playerBuilt ? 1 : 0),
                                                             creature.profession});
            }
            world.store().saveCreatures(savedCreatures);
        }

        engine::logInfo("Saved " + std::to_string(world.savedChunkCount()) + " modified chunks, " +
                        std::to_string(savedFurnaces.size()) + " furnaces and " +
                        std::to_string(savedCreatures.size()) + " creatures. Shutting down.");
    } catch (const std::exception& error) {
        engine::logError(std::string("Fatal: ") + error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
