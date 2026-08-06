#include <engine/core/FrameLimiter.hpp>
#include <engine/core/JobSystem.hpp>
#include <engine/core/Log.hpp>
#include <engine/core/Paths.hpp>
#include <engine/platform/Window.hpp>
#include <engine/render/Camera.hpp>
#include <engine/render/Renderer.hpp>
#include <engine/render/VulkanContext.hpp>

#include "core/Settings.hpp"
#include "hud/Crosshair.hpp"
#include "hud/DebugOverlay.hpp"
#include "hud/Hotbar.hpp"
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
#include "world/Biome.hpp"
#include "world/BlockOutline.hpp"
#include "world/Chunk.hpp"
#include "world/Creature.hpp"
#include "world/Explosion.hpp"
#include "world/Player.hpp"
#include "world/Raycast.hpp"
#include "world/Sky.hpp"
#include "world/TerrainGenerator.hpp"
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

int main() {
    try {
        const std::filesystem::path settingsPath = engine::executableDirectory() / "settings.cfg";
        game::Settings settings = game::loadSettings(settingsPath);

        // Declared before the world, and therefore destroyed after it: the world
        // submits jobs to this pool and must not outlive it.
        engine::JobSystem jobs(settings.workerThreads);

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

        engine::Renderer renderer(context, window, spriteLayers, hudTexture,
                                  textureDir.parent_path() / "font.png", skinTexture);

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

        // Far enough to reach the diagonal corner of the furthest drawn chunk,
        // or the world visibly clips into a dome at high render distances.
        renderer.setFarPlane(static_cast<float>(world.loadRadius() * game::Chunk::kSize) * 1.8f);

        // Spawn is chosen before any chunk exists, so the surface height comes
        // straight from the generator rather than from loaded blocks.
        const int spawnX = settings.spawnX;
        const int spawnZ = settings.spawnZ;
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
        float outlineHeight = 1.0f;
        renderer.setSkyMesh(game::sky::makeSunQuad());

        // Starts mid-morning rather than at sunrise, so the first thing seen is
        // a lit world with the sun clearly off to one side.
        float timeOfDay = 0.18f;
        float waterAnimationSeconds = 0.0f;
        renderer.setVerticalFov(kDefaultFov);

        if (savedPlayer.has_value()) {
            engine::logInfo("Resumed from the last saved position.");
        } else {
            // Only the fresh-world placement is left here: it reads blocks, so
            // it cannot run until the chunks exist. A resumed position was read
            // before the loading screen, because it is what the world had to be
            // streamed around.
            player.position.y = static_cast<float>(world.highestSolid(spawnX, spawnZ) + 1);

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
        engine::logInfo("F8 spawns a Bramble ahead of you, F9 a charged one.");
        engine::logInfo("Right click a spawn egg to place that creature; the inventory's left card has them all.");
        engine::logInfo("Entering main loop. Close the window to exit.");

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        auto lastReportTime = previousTime;
        int framesSinceReport = 0;

        float placeTimer = 0.0f;
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
        engine::MeshHandle dropMesh = engine::kInvalidMesh;

        // Creatures live beside the drops: few of them, main thread, and they
        // only ever read the world.
        game::Creatures creatures(kWorldSeed ^ 0x9E3779B9u);
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

        float swingTimer = 0.0f;
        // Which panel is up, if any. A crafting table reuses the inventory
        // screen with a wider grid rather than owning a screen of its own.
        std::optional<game::inventoryScreen::Kind> openScreen;
        // The catalogue card's own state. Kept out here rather than inside the
        // screen module, so `build` stays a pure function of what it is given.
        game::inventoryScreen::CatalogueState catalogue;
        game::ItemStack heldStack;
        // Sized for the largest grid any screen offers, so moving between the
        // inventory's 2x2 and a table's 3x3 is a change of extent, not of storage.
        // A furnace borrows the first two for its input and fuel.
        std::array<game::ItemStack, game::kMaxCraftSlots> craftSlots{};

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
                                  saved.scale, saved.charged != 0);
                ++restored;
            }
            if (restored > 0) {
                engine::logInfo("Restored " + std::to_string(restored) + " creatures.");
            }
        }

        // Which furnace the open screen is looking at, if any.
        glm::ivec3 openFurnacePosition{0};
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
        bool hudDirty = true;
        /// Whether the camera itself is inside a water cell, which is a
        /// different question from `Player::inWater` - that one asks about the
        /// whole body, and being waist deep does not change what you see.
        bool eyeUnderwater = false;

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
            // view onto the block. A crafting grid does not, because its
            // contents exist only while it is on screen.
            if (openScreen != game::inventoryScreen::Kind::Furnace) {
                for (game::ItemStack& slot : craftSlots) {
                    giveBack(slot);
                }
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

        // Crosshair, hotbar and the diagnostics panel share one screen mesh.
        const auto rebuildHud = [&](const game::OverlayStats& stats) {
            // A panel carries its own hotbar row, and a crosshair over a
            // pointer-driven screen is just clutter.
            engine::MeshData hud = openScreen.has_value() ? engine::MeshData{} : game::makeCrosshair();
            // Catalogue entries, which are the one part of the HUD that has to
            // stop at an edge rather than simply being drawn or not.
            engine::MeshData clipped;

            const auto append = [&](const engine::MeshData& part) {
                const auto base = static_cast<std::uint32_t>(hud.vertices.size());
                hud.vertices.insert(hud.vertices.end(), part.vertices.begin(), part.vertices.end());
                for (const std::uint32_t index : part.indices) {
                    hud.indices.push_back(base + index);
                }
            };

            if (openScreen.has_value()) {
                const engine::Extent2D extent = window.framebufferExtent();
                const float halfHeight = static_cast<float>(extent.height) * 0.5f;

                // A furnace shows what it has actually made; every other screen
                // shows what its grid *would* make.
                game::ItemStack shownResult;
                game::inventoryScreen::FurnaceProgress progress;
                if (*openScreen == game::inventoryScreen::Kind::Furnace) {
                    const auto found = furnaces.find(openFurnacePosition);
                    if (found != furnaces.end()) {
                        shownResult = found->second.output;
                        progress.burn = found->second.burnFraction();
                        progress.cook = found->second.cookFraction();
                    }
                } else {
                    shownResult = game::craftResult(craftSlots.data(),
                                                    game::inventoryScreen::craftSize(*openScreen));
                }

                append(game::inventoryScreen::build(
                    *openScreen, inventory, craftSlots.data(), shownResult, heldStack,
                    (static_cast<float>(window.cursorX()) - static_cast<float>(extent.width) * 0.5f) / halfHeight,
                    (static_cast<float>(window.cursorY()) - halfHeight) / halfHeight, renderer.aspectRatio(),
                    catalogue, progress, clipped));
            } else {
                append(game::makeHotbar(inventory, selectedSlot));

                // Digging takes time now, so it needs to show that it is
                // happening - without this a slow block looks like a dead click.
                if (breakProgress > 0.0f) {
                    constexpr float kBarHalfWidth = 0.085f;
                    constexpr float kBarHalfHeight = 0.009f;
                    constexpr float kBarCentreY = 0.11f;
                    constexpr float kTrackDepth = 0.00095f;
                    constexpr float kFillDepth = 0.00090f;
                    const float white = static_cast<float>(game::TextureLayer::White);

                    engine::MeshData bar;
                    game::hud::appendQuad(bar, 0.0f, kBarCentreY, kBarHalfWidth, kBarHalfHeight, kTrackDepth,
                                          {0.0f, 0.0f, 0.0f, 0.55f}, white, false);
                    const float filled = kBarHalfWidth * std::min(breakProgress, 1.0f);
                    game::hud::appendQuad(bar, -kBarHalfWidth + filled, kBarCentreY, filled,
                                          kBarHalfHeight * 0.6f, kFillDepth, {0.92f, 0.92f, 0.95f, 1.0f}, white,
                                          false);
                    append(bar);
                }
            }
            if (overlayVisible) {
                append(game::makeDebugOverlay(stats, frameHistory, renderer.aspectRatio()));
            }

            renderer.setScreenMesh(hud);
            const auto [clipMin, clipMax] = game::inventoryScreen::catalogueListBounds();
            renderer.setClippedScreenMesh(clipped, clipMin, clipMax);
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

            // Drained every frame whether or not anything wants it, so a
            // keystroke can never be delivered late. Read before the key loop,
            // so the `E` that opens a screen is discarded with the frame it
            // belonged to rather than arriving as the first typed character.
            {
                const std::string typed = window.consumeTypedText();
                // Slice 1 proof, replaced by the search field in slice 7.
                if (!typed.empty() && openScreen.has_value()) {
                    engine::logInfo("Typed: " + typed);
                }
            }

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

            // Drained every frame even when unused, so the queue cannot grow
            // without bound. It only exists to notice a click while the cursor
            // is free.
            const std::vector<engine::MouseButton> presses = window.consumeMouseButtonPresses();
            const bool clicked = !presses.empty();
            const bool hadCursor = window.isCursorCaptured();

            // Consumed here with the rest of the presses but acted on after the
            // raycast, so it tests the block under the crosshair *this* frame
            // rather than where the camera was last frame.
            bool wantInteract = false;

            if (openScreen.has_value()) {
                // Pixels to the same space the screen is laid out in: relative
                // to window height, origin at the centre, Y down.
                const engine::Extent2D extent = window.framebufferExtent();
                const float halfHeight = static_cast<float>(extent.height) * 0.5f;
                const float cursorX =
                    (static_cast<float>(window.cursorX()) - static_cast<float>(extent.width) * 0.5f) / halfHeight;
                const float cursorY = (static_cast<float>(window.cursorY()) - halfHeight) / halfHeight;
                const int craftExtent = game::inventoryScreen::craftSize(*openScreen);
                const bool furnaceOpen = *openScreen == game::inventoryScreen::Kind::Furnace;
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

                    if (!hit.has_value()) {
                        if (const auto tab = game::inventoryScreen::tabAt(*openScreen, cursorX, cursorY);
                            tab.has_value()) {
                            catalogue.tab = *tab;
                            // A new tab starts at the top. Keeping the offset
                            // would open a short list scrolled past its end.
                            catalogue.scrollRow = 0;
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
                                    game::inventoryScreen::catalogueItems(catalogue.tab);
                                if (*cell < listed.size()) {
                                    const game::ItemId picked = listed[*cell];
                                    if (window.isKeyDown(engine::Key::LeftShift) && !right) {
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
                            if (game::inventoryScreen::insideCatalogueList(*openScreen, cursorX, cursorY)) {
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

                    const bool shiftHeld = window.isKeyDown(engine::Key::LeftShift);

                    if (shiftHeld && !right) {
                        if (hit->region == Region::CraftResult && !furnaceOpen) {
                            // Crafts as many as will fit rather than one. Every
                            // pass spends at least one ingredient, so this always
                            // terminates.
                            while (true) {
                                const game::ItemStack batch =
                                    game::craftResult(craftSlots.data(), craftExtent);
                                if (batch.empty() || !inventory.hasRoomFor(batch.item, batch.count)) {
                                    break;
                                }
                                inventory.add(batch.item, batch.count);
                                game::consumeIngredients(craftSlots.data(), craftExtent);
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
                        const game::ItemStack made = game::craftResult(craftSlots.data(), craftExtent);
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
                            game::consumeIngredients(craftSlots.data(), craftExtent);
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
            } else if (hadCursor) {
                // Sneaking suppresses this, which is what lets you place a block
                // on top of a table rather than opening it.
                wantInteract = !window.isKeyDown(engine::Key::LeftShift) &&
                               std::any_of(presses.begin(), presses.end(), [](engine::MouseButton button) {
                                   return button == engine::MouseButton::Right;
                               });
            }

            // Scrolling away from the user moves right along the bar, and the
            // selection wraps at both ends.
            //
            // **While a screen is open the wheel belongs to the catalogue**, the
            // same way the movement keys do. Without that guard it kept driving
            // the hotbar underneath - silently, because the bar is hidden behind
            // the panel while you are looking at it.
            const float scroll = window.consumeScrollDelta();
            if (const int notches = static_cast<int>(scroll); notches != 0) {
                if (openScreen.has_value() && game::inventoryScreen::showsCatalogue(*openScreen)) {
                    const int limit = game::inventoryScreen::catalogueMaxScroll(
                        game::inventoryScreen::catalogueItems(catalogue.tab).size());
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
            const bool overlayDue = overlayVisible && now - lastHudRebuild >= kOverlayRefreshInterval;
            if (breakProgress <= 0.0f && lastBreakProgress > 0.0f) {
                hudDirty = true;
            }
            lastBreakProgress = breakProgress;
            if (hudDirty || overlayDue || openScreen.has_value() || breakProgress > 0.0f) {
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
                stats.biome = game::biomeInfo(game::sampleBiome(kWorldSeed,
                                                                static_cast<int>(std::floor(player.position.x)),
                                                                static_cast<int>(std::floor(player.position.z)))
                                                  .dominant)
                                  .name;
                stats.air = player.air;

                rebuildHud(stats);
                lastHudRebuild = now;

                if (hudDirty) {
                    const game::ItemStack& held = inventory.slot(selectedSlot);
                    engine::logInfo(std::string("Holding: ") +
                                    (held.empty()             ? "nothing"
                                     : game::isSpawnEgg(held.item) ? game::spawnEggName(held.item)
                                                                   : game::itemDisplayName(held.item)) +
                                    (held.empty() ? "" : " x" + std::to_string(held.count)));
                    hudDirty = false;
                }
            }

            const engine::CursorDelta look = window.consumeCursorDelta();
            if (window.isCursorCaptured()) {
                // Screen Y grows downward, so moving the mouse down must pitch down.
                camera.addLook(look.x * kLookRadiansPerPixel, -look.y * kLookRadiansPerPixel);
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
                move.jump = window.isKeyDown(engine::Key::Space);
                move.sprint = window.isKeyDown(engine::Key::LeftControl);
                move.sneak = window.isKeyDown(engine::Key::LeftShift);
                move.lookY = glm::normalize(camera.forward()).y;
                move.verticalWish = (window.isKeyDown(engine::Key::Space) ? 1.0f : 0.0f) -
                                    (window.isKeyDown(engine::Key::LeftShift) ? 1.0f : 0.0f);
            }

            game::updatePlayer(player, move, world, deltaSeconds);
            camera.position = player.renderEyePosition();

            // Being underwater has to be visible, not merely felt. Without this
            // the only evidence is the physics changing, which reads as gravity
            // being broken rather than as swimming.
            {
                const bool submergedNow =
                    game::isWater(world.blockAt(static_cast<int>(std::floor(camera.position.x)),
                                                static_cast<int>(std::floor(camera.position.y)),
                                                static_cast<int>(std::floor(camera.position.z))));
                eyeUnderwater = submergedNow;
            }

            const game::RaycastHit target = game::raycast(world, camera.position, camera.forward(), kBlockReach);

            if (wantInteract && target.hit &&
                game::isInteractive(world.blockAt(target.block.x, target.block.y, target.block.z))) {
                const game::BlockId opened = world.blockAt(target.block.x, target.block.y, target.block.z);
                if (game::isFurnace(opened)) {
                    openFurnacePosition = target.block;
                    // Created on first use rather than when placed, so a furnace
                    // nobody has touched costs nothing.
                    furnaces.try_emplace(openFurnacePosition);
                    openScreen = game::inventoryScreen::Kind::Furnace;
                } else {
                    openScreen = game::inventoryScreen::Kind::CraftingTable;
                }
                window.setCursorCaptured(false);
                hudDirty = true;
            }

            // Gated on the cursor state from the start of the frame, so the
            // click that recaptures the cursor does not also swing at a block,
            // and on the screen state *now*, so the click that just opened a
            // table does not also place against it.
            const bool playing = hadCursor && !openScreen.has_value();
            const bool wantBreak = playing && window.isMouseButtonDown(engine::MouseButton::Left);
            const bool wantPlace = playing && window.isMouseButtonDown(engine::MouseButton::Right);
            const bool wantDrop = !openScreen.has_value() && window.isKeyDown(engine::Key::Q);

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

            // A creature in the way takes the swing instead of the block behind
            // it. Asked every frame rather than only when a swing is ready,
            // because the swing's cooldown would otherwise let the block behind
            // a creature be mined straight through it.
            const float entityReach = creative ? kCreativeEntityReach : kEntityReach;
            const bool creatureInWay =
                wantBreak && creatures.aimedAt(camera.position, camera.forward(), entityReach);
            if (creatureInWay && swingTimer <= 0.0f) {
                const game::ToolProperties swung = game::toolFor(inventory.slot(selectedSlot).item);
                const int damage = swung.kind == game::ToolKind::Sword ? (swung.tier >= game::kStoneTier ? 5 : 4)
                                                                       : 1 + swung.tier;
                if (creatures.strike(camera.position, camera.forward(), entityReach, damage)) {
                    swingTimer = kSwingSeconds;
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
                    // Only an instant break needs holding back. A timed one is
                    // already paced by its own progress bar, and pausing it too
                    // would make ordinary mining stutter.
                    breakCooldown = seconds <= 0.0f ? kBreakRepeatSeconds : 0.0f;
                    const game::BlockId broken = aimed;
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

                    // Breaking yields its drop in every mode, but only if what
                    // you are holding is good enough for it. Stone mined by hand
                    // gives nothing, which is what makes a pickaxe worth making.
                    const game::ItemId dropped = game::dropForBlock(broken);
                    if (dropped != game::ItemId::None && (creative || game::yieldsDrop(broken, tool.item))) {
                        drops.spawn(glm::vec3{target.block} + glm::vec3{0.5f}, dropped,
                                    game::dropCountForBlock(broken));
                    }

                    // Tools wear only on blocks that actually resist them.
                    if (!creative && game::blockHardness(broken) > 0.0f) {
                        const game::ToolProperties properties = game::toolFor(tool.item);
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
                    if (game::needsSupportBelow(resting) && !world.isSolid(above.x, above.y - 1, above.z)) {
                        world.setBlock(above.x, above.y, above.z, game::BlockId::Air);
                        const game::ItemId shed = game::dropForBlock(resting);
                        if (shed != game::ItemId::None) {
                            drops.spawn(glm::vec3{above} + glm::vec3{0.5f}, shed, 1);
                        }
                    }
                }
            }

            if (!wantPlace) {
                placeTimer = 0.0f;
            } else {
                placeTimer -= deltaSeconds;
                const game::ItemStack& held = inventory.slot(selectedSlot);

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

                // A bucket is used rather than placed, and needs its own ray:
                // water has no selection geometry, so the ordinary aim passes
                // straight through it to the riverbed.
                if (placeTimer <= 0.0f && !held.empty() &&
                    (held.item == game::ItemId::Bucket || held.item == game::ItemId::WaterBucket)) {
                    const bool filling = held.item == game::ItemId::Bucket;
                    const game::RaycastHit reached =
                        game::raycast(world, camera.position, camera.forward(), kBlockReach, filling);

                    bool used = false;
                    if (filling && reached.hit &&
                        game::isWaterSource(world.blockAt(reached.block.x, reached.block.y, reached.block.z))) {
                        world.setBlock(reached.block.x, reached.block.y, reached.block.z,
                                       game::BlockId::Air);
                        used = true;
                    } else if (!filling && reached.hit &&
                               !game::playerOverlapsBlock(player, reached.adjacent)) {
                        // Level 0 is a *source*, not merely a full cell. Placing
                        // a flowing level instead would drain itself the moment
                        // the fluid update ran.
                        world.setBlock(reached.adjacent.x, reached.adjacent.y, reached.adjacent.z,
                                       game::BlockId::Water0);
                        used = true;
                    }

                    if (used) {
                        // Emptying and filling is the *cost* of using a bucket,
                        // which is the one thing creative is allowed to skip.
                        if (!creative) {
                            game::ItemStack& slot = inventory.slot(selectedSlot);
                            const game::ItemId became =
                                filling ? game::ItemId::WaterBucket : game::ItemId::Bucket;
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

                const bool canPlace = !held.empty() && game::isBlockItem(held.item);
                if (placeTimer <= 0.0f && canPlace && target.hit &&
                    !game::playerOverlapsBlock(player, target.adjacent)) {
                    game::BlockId placing = game::blockForItem(held.item);
                    glm::ivec3 where = target.adjacent;

                    const bool clickedAbove = target.adjacent.y > target.block.y;
                    const bool clickedBelow = target.adjacent.y < target.block.y;

                    // Two halves meeting in one cell become a whole block. Left
                    // as separate halves they stack as slab, gap, slab, which is
                    // never what anyone is trying to build.
                    const game::BlockId aimedAt = world.blockAt(target.block.x, target.block.y, target.block.z);
                    const bool completesSlab = game::isSlab(placing) && game::isSlab(aimedAt) &&
                                               (game::isUpperHalf(aimedAt) ? clickedBelow : clickedAbove);

                    if (completesSlab) {
                        placing = game::BlockId::Stone;
                        where = target.block;
                    } else if (game::isSlab(placing)) {
                        // Clicking an underside puts the half up against it.
                        placing = clickedBelow ? game::BlockId::StoneSlabTop : game::BlockId::StoneSlab;
                    } else if (game::isStairs(placing)) {
                        // Oriented blocks take their facing from the camera and
                        // their half from which end of the block was clicked,
                        // which is what lets you build a staircase that turns.
                        const glm::vec3 aim = camera.forward();
                        const game::Facing facing =
                            std::abs(aim.x) > std::abs(aim.z)
                                ? (aim.x > 0.0f ? game::Facing::West : game::Facing::East)
                                : (aim.z > 0.0f ? game::Facing::North : game::Facing::South);
                        placing = game::stairsAt(facing, clickedBelow);
                    } else if (game::isFurnace(placing)) {
                        // The mouth turns to face whoever placed it, which is
                        // the reference's rule and the only way three plain
                        // sides and one front can be told apart.
                        const glm::vec3 aim = camera.forward();
                        const game::FaceDirection front =
                            std::abs(aim.x) > std::abs(aim.z)
                                ? (aim.x > 0.0f ? game::FaceDirection::NegX : game::FaceDirection::PosX)
                                : (aim.z > 0.0f ? game::FaceDirection::NegZ : game::FaceDirection::PosZ);
                        placing = game::furnaceFacing(front, false);
                    }

                    // Nothing that needs a floor may be placed without one.
                    if (game::needsSupportBelow(placing) && !world.isSolid(where.x, where.y - 1, where.z)) {
                        placeTimer = kPlaceRepeatSeconds;
                    } else {
                        world.setBlock(where.x, where.y, where.z, placing);
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
                const bool lit = game::tickFurnace(furnace, deltaSeconds);
                const game::BlockId present = world.blockAt(position.x, position.y, position.z);
                if (!game::isFurnace(present)) {
                    continue;
                }
                // Lighting one must not turn it round: the facing and the lit
                // state are both in the id, so they are recombined rather than
                // one being overwritten with a default.
                const game::BlockId wanted = game::furnaceFacing(game::furnaceFacing(present), lit);
                if (present != wanted) {
                    world.setBlock(position.x, position.y, position.z, wanted);
                }
            }

            // Anything a spreading flow swept aside drops, the same way a plant
            // left hanging by mining below it does.
            for (const game::World::WashedBlock& washed : world.takeWashedBlocks()) {
                const game::ItemId shed = game::dropForBlock(washed.block);
                if (shed != game::ItemId::None) {
                    drops.spawn(glm::vec3{washed.position} + glm::vec3{0.5f}, shed, 1);
                }
            }

            // Dropped items live entirely on the main thread: there are a
            // handful of them and they touch the world only to read it.
            drops.update(world, player.position, deltaSeconds);
            for (const game::ItemEntities::Collectable& ready : drops.collectable(player.position)) {
                // Collection is mode-independent, like dropping. Skipping it in
                // creative left anything dropped orbiting the player forever.
                if (!inventory.hasRoomFor(ready.item, ready.count)) {
                    continue;
                }
                const int left = inventory.add(ready.item, ready.count);
                drops.reduce(ready.index, ready.count - left);
                hudDirty = true;
                break; // Indices shift as drops are removed.
            }

            // Rebuilt rather than transformed: world meshes are drawn with an
            // identity model matrix, which is what lets the shader recover
            // normals from world position. The handle is reused rather than
            // recreated, or every frame would retire a GPU buffer.
            {
                engine::MeshData dropGeometry = drops.buildMesh(world, timeOfDay * 1000.0f);
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
            }

            // Creatures move and decide, then a fresh mesh is built for them the
            // same way, and for the same reason. The showcase holds them still,
            // because a model being reviewed should not walk out of frame.
            const bool night = game::sky::sunDirection(timeOfDay).y < -0.05f;
            if (settings.creatureShowcase == 0) {
                std::vector<game::CreatureExplosion> blasts;
                const game::CreatureAttack blow =
                    creatures.update(world, player.position, deltaSeconds, night, player.sneaking,
                                     blasts);
                if (blow.landed) {
                    // Knockback only. Health and damage are M21's, and a hostile
                    // that shoves you is honest feedback until then.
                    player.velocity += blow.push;
                    player.onGround = false;
                }

                // Only the strongest blast of a frame throws the player.
                glm::vec3 blastPush{0.0f};
                float strongestBlast = 0.0f;

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
                            blastPush = away / reach * impact * 20.0f;
                        }
                    }

                    // Everything else in range takes it too. The blocks are the
                    // caller's business and the population is the creature
                    // system's, which is why this is a call rather than a loop.
                    const int caught = creatures.applyExplosion(world, blast.centre, blast.power);

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
                    player.velocity += blastPush;
                    player.onGround = false;
                }

                creatures.manage(world, player.position, deltaSeconds, night);
            }
            {
                engine::MeshData creatureShells;
                engine::MeshData creatureGeometry = creatures.buildMesh(world, creatureShells);
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
                const game::BlockBoxes aimed =
                    game::selectionBoxes(world.blockAt(target.block.x, target.block.y, target.block.z));
                if (aimed.count > 0) {
                    float lowY = 1.0f;
                    float highY = 0.0f;
                    for (int i = 0; i < aimed.count; ++i) {
                        lowY = std::min(lowY, aimed.boxes[i].minY);
                        highY = std::max(highY, aimed.boxes[i].maxY);
                    }

                    // Rebuilt only when the targeted height changes, which is a
                    // handful of times a session rather than every frame.
                    const float height = highY - lowY;
                    if (height != outlineHeight) {
                        outlineHeight = height;
                        renderer.setOverlayMesh(game::makeBlockOutline(height));
                    }
                    highlight = glm::translate(glm::mat4{1.0f},
                                               glm::vec3{target.block} + glm::vec3{0.0f, lowY, 0.0f});
                }
            }

            timeOfDay += deltaSeconds / static_cast<float>(settings.dayLengthSeconds);
            timeOfDay -= std::floor(timeOfDay);

            const glm::vec3 sunDirection = game::sky::sunDirection(timeOfDay);
            float ambient = 0.0f;
            float sunStrength = 0.0f;
            game::sky::sunLighting(sunDirection, ambient, sunStrength);

            renderer.setSunDirection(sunDirection);
            renderer.setSunLighting(ambient, sunStrength, game::sky::kAmbientFloor);
            renderer.setSkyTransform(game::sky::sunTransform(camera.position, sunDirection));

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

            // Underwater everything fades to one colour over a fixed distance,
            // and your eyes open out over the first half-minute. Dimmed by the
            // daylight the surface is getting, or a night dive glows.
            const glm::vec3 sky = game::sky::skyColor(sunDirection);
            glm::vec3 background = sky;
            if (eyeUnderwater) {
                // A flat colour and a fixed distance, both constant. Anything
                // that varies with the time of day or with how long you have
                // been under makes the water look like a different colour every
                // time you dip into it.
                renderer.setFog(game::fluid::kFogColour, game::fluid::kFogDistance);
                // Anything with no geometry behind it is water too, so the sky
                // must not show through from sixty metres down.
                background = game::fluid::kFogColour;
            } else {
                renderer.setFog(glm::vec3{0.0f}, 0.0f);
            }

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
                                std::to_string(renderer.stats().triangles) + " | creatures " +
                                std::to_string(creatures.count()) + " (" + censusText + ") hunting " +
                                std::to_string(creatures.hunting()) + " | drops " +
                                std::to_string(drops.count()));
                framesSinceReport = 0;
                lastReportTime = now;
            }

            frameLimiter.waitForNextFrame();
        }

        engine::logInfo("Window closed. Saving world.");
        world.saveAll();
        world.store().savePlayer(game::SavedPlayer{player.position, camera.yaw, camera.pitch});

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

        // And it must not write one back either. The showcase population is
        // three copies of whatever was being looked at; saving that would
        // replace the world's animals with it.
        std::vector<game::SavedCreature> savedCreatures;
        if (settings.creatureShowcase == 0) {
            savedCreatures.reserve(creatures.all().size());
            for (const game::Creature& creature : creatures.all()) {
                savedCreatures.push_back(game::SavedCreature{static_cast<std::uint8_t>(creature.kind),
                                                             creature.position.x, creature.position.y,
                                                             creature.position.z, creature.yaw,
                                                             creature.health, creature.scale,
                                                             static_cast<std::uint8_t>(creature.charged ? 1 : 0)});
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
