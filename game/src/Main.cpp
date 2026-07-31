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
#include "hud/InventoryScreen.hpp"
#include "hud/LoadingScreen.hpp"
#include "item/Inventory.hpp"
#include "item/Recipe.hpp"
#include "item/SlotOps.hpp"
#include "world/ItemEntity.hpp"
#include "world/Biome.hpp"
#include "world/BlockOutline.hpp"
#include "world/Chunk.hpp"
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::uint32_t kWindowWidth = 1280;
constexpr std::uint32_t kWindowHeight = 720;
constexpr float kLookRadiansPerPixel = 0.0025f;

// How far the player can reach to break or place, in metres.
constexpr float kReach = 12.0f;

// Holding the button keeps editing at this rate, so dragging across terrain
// does not need one click per block.
constexpr float kBreakRepeatSeconds = 0.15f;
constexpr float kPlaceRepeatSeconds = 0.18f;

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

const char* describeBlock(game::BlockId block) {
    if (game::isStairs(block)) {
        return "Cobblestone Stairs";
    }
    switch (block) {
    case game::BlockId::Stone:
        return "Stone";
    case game::BlockId::Dirt:
        return "Dirt";
    case game::BlockId::Grass:
        return "Grass";
    case game::BlockId::Sand:
        return "Sand";
    case game::BlockId::Cobblestone:
        return "Cobblestone";
    case game::BlockId::Gravel:
        return "Gravel";
    case game::BlockId::Snow:
        return "Snow";
    case game::BlockId::Planks:
        return "Planks";
    case game::BlockId::Bricks:
        return "Bricks";
    case game::BlockId::Glowstone:
        return "Glowstone";
    case game::BlockId::TallGrass:
        return "Tall Grass";
    case game::BlockId::StoneSlab:
        return "Stone Slab";
    case game::BlockId::StoneSlabTop:
        return "Stone Slab";
    case game::BlockId::PlanksFence:
        return "Fence";
    default:
        return "Air";
    }
}

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
        const std::vector<std::filesystem::path> blockTextures{
            textureDir / "stone.png",       textureDir / "dirt.png",   textureDir / "grass_top.png",
            textureDir / "grass_side.png",  textureDir / "sand.png",   textureDir / "white.png",
            textureDir / "cobblestone.png", textureDir / "gravel.png", textureDir / "snow.png",
            textureDir / "planks.png",      textureDir / "bricks.png", textureDir / "glowstone.png",
            textureDir / "water.png",       textureDir / "log_side.png", textureDir / "log_top.png",
            textureDir / "leaves.png",      textureDir / "sun.png",      textureDir / "tall_grass.png",
            textureDir / "stick.png"};

        engine::Renderer renderer(context, window, blockTextures, textureDir.parent_path() / "hud.png",
                                  textureDir.parent_path() / "font.png");

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

        // The world streams in with the window already alive and drawing, rather
        // than blocking before the first frame. Queuing every chunk at once
        // pinned all cores, took the peak memory before anything was on screen,
        // and left the window unresponsive long enough for Windows to say so.
        {
            float shown = 0.0f;
            float lastDrawn = -1.0f;
            auto lastTick = std::chrono::steady_clock::now();

            while (!window.shouldClose()) {
                window.pollEvents();

                const auto now = std::chrono::steady_clock::now();
                const float delta = std::chrono::duration<float>(now - lastTick).count();
                lastTick = now;

                const std::vector<game::ChunkMeshUpdate> batch = world.update(spawn, kLoadingBudgetSeconds);
                applyUpdates(batch);

                // Eased rather than snapped: chunk loading arrives in coarse
                // steps and an unsmoothed bar jerks between them.
                const float target = world.initialLoadProgress();
                shown += (target - shown) * std::min(1.0f, delta * kLoadingBarEase);
                if (target >= 1.0f && shown > 0.998f) {
                    break;
                }

                // Rebuilt only when it would look different: this runs every
                // frame and each rebuild retires a GPU buffer.
                if (std::abs(shown - lastDrawn) > 0.001f) {
                    lastDrawn = shown;
                    renderer.setScreenMesh(game::hud::makeLoadingScreen(shown, renderer.aspectRatio()));
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
        renderer.setVerticalFov(kDefaultFov);

        game::Player player;
        if (const std::optional<game::SavedPlayer> saved = world.store().loadPlayer()) {
            player.position = saved->position;
            camera.yaw = saved->yaw;
            camera.pitch = saved->pitch;
            engine::logInfo("Resumed from the last saved position.");
        } else {
            player.position = spawn;
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
        engine::logInfo("Double-tap Space to fly. Descend onto the ground to land.");
        engine::logInfo("Escape releases the mouse; click to recapture.");
        engine::logInfo("F5 toggles the diagnostics overlay.");
        engine::logInfo("F6/F7 change render distance.");
        engine::logInfo("Entering main loop. Close the window to exit.");

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        auto lastReportTime = previousTime;
        int framesSinceReport = 0;

        float breakTimer = 0.0f;
        float placeTimer = 0.0f;
        float dropTimer = 0.0f;
        float secondsSinceSpacePress = kDoubleTapSeconds;

        // Creative starts with one of everything placeable; survival starts with
        // nothing and fills up from what you break.
        constexpr std::array<game::BlockId, game::kHotbarSlots> creativeKit{
            game::BlockId::Grass,     game::BlockId::Dirt,          game::BlockId::Stone,
            game::BlockId::StoneSlab, game::BlockId::CobbleStairs0, game::BlockId::TallGrass,
            game::BlockId::Planks,    game::BlockId::PlanksFence,   game::BlockId::Glowstone};

        // Raw materials, in the storage rows rather than the hotbar: they are
        // crafting inputs rather than things to place, and the hotbar is full.
        constexpr std::array<game::BlockId, 5> creativeStock{game::BlockId::Log, game::BlockId::Cobblestone,
                                                             game::BlockId::Sand, game::BlockId::Gravel,
                                                             game::BlockId::Bricks};

        game::Inventory inventory;
        bool creative = settings.creativeMode;
        const auto fillCreativeKit = [&] {
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
        bool inventoryOpen = false;
        game::ItemStack heldStack;
        std::array<game::ItemStack, game::inventoryScreen::kCraftSlots> craftSlots{};

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
        std::size_t selectedSlot = 0;
        bool hudDirty = true;

        bool overlayVisible = false;
        std::vector<float> frameHistory;
        frameHistory.reserve(kFrameHistoryLength);
        auto lastHudRebuild = previousTime;

        // Crosshair, hotbar and the diagnostics panel share one screen mesh.
        const auto rebuildHud = [&](const game::OverlayStats& stats) {
            // The inventory screen carries its own hotbar row, and a crosshair
            // over a pointer-driven panel is just clutter.
            engine::MeshData hud = inventoryOpen ? engine::MeshData{} : game::makeCrosshair();

            const auto append = [&](const engine::MeshData& part) {
                const auto base = static_cast<std::uint32_t>(hud.vertices.size());
                hud.vertices.insert(hud.vertices.end(), part.vertices.begin(), part.vertices.end());
                for (const std::uint32_t index : part.indices) {
                    hud.indices.push_back(base + index);
                }
            };

            if (inventoryOpen) {
                const engine::Extent2D extent = window.framebufferExtent();
                const float halfHeight = static_cast<float>(extent.height) * 0.5f;
                append(game::inventoryScreen::build(
                    inventory, craftSlots.data(),
                    game::craftResult(craftSlots.data(), game::inventoryScreen::kCraftSize), heldStack,
                    (static_cast<float>(window.cursorX()) - static_cast<float>(extent.width) * 0.5f) / halfHeight,
                    (static_cast<float>(window.cursorY()) - halfHeight) / halfHeight, renderer.aspectRatio()));
            } else {
                append(game::makeHotbar(inventory, selectedSlot));
            }
            if (overlayVisible) {
                append(game::makeDebugOverlay(stats, frameHistory, renderer.aspectRatio()));
            }

            renderer.setScreenMesh(hud);
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

            for (const engine::Key key : window.consumeKeyPresses()) {
                if (key == engine::Key::Escape) {
                    inventoryOpen = false;
                    window.setCursorCaptured(false);
                    continue;
                }
                if (key == engine::Key::E) {
                    inventoryOpen = !inventoryOpen;
                    // The screen needs a pointer, so opening it hands the cursor
                    // back and closing it takes it again.
                    window.setCursorCaptured(!inventoryOpen);
                    // Nothing is left stranded on the cursor when the screen
                    // closes.
                    if (!heldStack.empty()) {
                        const int left = inventory.add(heldStack.item, heldStack.count);
                        if (left > 0 && !creative) {
                            drops.spawn(camera.position + camera.forward() * 0.5f, heldStack.item, left,
                                        camera.forward() * kThrowSpeed, kThrowPickupDelay);
                        }
                        heldStack = game::ItemStack{};
                    }
                    // Ingredients go back too. Leaving them in a grid the player
                    // cannot see is how items quietly disappear.
                    for (game::ItemStack& slot : craftSlots) {
                        if (slot.empty()) {
                            continue;
                        }
                        const int left = inventory.add(slot.item, slot.count);
                        if (left > 0 && !creative) {
                            drops.spawn(camera.position + camera.forward() * 0.5f, slot.item, left,
                                        camera.forward() * kThrowSpeed, kThrowPickupDelay);
                        }
                        slot = game::ItemStack{};
                    }
                    hudDirty = true;
                    continue;
                }
                if (key == engine::Key::Q) {
                    // Only the cursor is emptied here. Dropping from the hotbar
                    // repeats while held, so it lives with the other held-key
                    // actions below.
                    if (inventoryOpen && !heldStack.empty()) {
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

            if (inventoryOpen) {
                // Pixels to the same space the screen is laid out in: relative
                // to window height, origin at the centre, Y down.
                const engine::Extent2D extent = window.framebufferExtent();
                const float halfHeight = static_cast<float>(extent.height) * 0.5f;
                const float cursorX =
                    (static_cast<float>(window.cursorX()) - static_cast<float>(extent.width) * 0.5f) / halfHeight;
                const float cursorY = (static_cast<float>(window.cursorY()) - halfHeight) / halfHeight;
                const auto hit = game::inventoryScreen::slotAt(cursorX, cursorY, renderer.aspectRatio());

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
                    return nullptr;
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
                        if (!game::inventoryScreen::insidePanel(cursorX, cursorY, renderer.aspectRatio()) &&
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

                    if (hit->region == Region::CraftResult) {
                        // The result is a preview until it is taken, which is why
                        // the ingredients are only spent here.
                        const game::ItemStack made =
                            game::craftResult(craftSlots.data(), game::inventoryScreen::kCraftSize);
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
                            game::consumeIngredients(craftSlots.data(), game::inventoryScreen::kCraftSize);
                            hudDirty = true;
                        }
                        continue;
                    }

                    game::ItemStack* stack = stackAt(*hit);
                    if (stack == nullptr) {
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
            } else if (!hadCursor && clicked) {
                window.setCursorCaptured(true);
            }

            // Scrolling away from the user moves right along the bar, and the
            // selection wraps at both ends.
            const float scroll = window.consumeScrollDelta();
            if (const int notches = static_cast<int>(scroll); notches != 0) {
                const auto slots = static_cast<int>(game::kHotbarSlots);
                int next = (static_cast<int>(selectedSlot) - notches) % slots;
                if (next < 0) {
                    next += slots;
                }
                selectedSlot = static_cast<std::size_t>(next);
                hudDirty = true;
            }

            // The overlay wants refreshing continuously, and so does the
            // inventory because the held stack follows the cursor. Everything
            // else only when the selection changes.
            const bool overlayDue = overlayVisible && now - lastHudRebuild >= kOverlayRefreshInterval;
            if (hudDirty || overlayDue || inventoryOpen) {
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

                rebuildHud(stats);
                lastHudRebuild = now;

                if (hudDirty) {
                    const game::ItemStack& held = inventory.slot(selectedSlot);
                    engine::logInfo(std::string("Holding: ") +
                                    (held.empty() ? "nothing"
                                                  : describeBlock(game::blockForItem(held.item))) +
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
            if (!inventoryOpen) {
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
                move.verticalWish = (window.isKeyDown(engine::Key::Space) ? 1.0f : 0.0f) -
                                    (window.isKeyDown(engine::Key::LeftShift) ? 1.0f : 0.0f);
            }

            game::updatePlayer(player, move, world, deltaSeconds);
            camera.position = player.eyePosition();

            const game::RaycastHit target = game::raycast(world, camera.position, camera.forward(), kReach);

            // Gated on the cursor state from the start of the frame, so the
            // click that recaptures the cursor does not also swing at a block.
            const bool wantBreak = hadCursor && window.isMouseButtonDown(engine::MouseButton::Left);
            const bool wantPlace = hadCursor && window.isMouseButtonDown(engine::MouseButton::Right);
            const bool wantDrop = !inventoryOpen && window.isKeyDown(engine::Key::Q);

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
            if (!wantBreak) {
                breakTimer = 0.0f;
            } else {
                breakTimer -= deltaSeconds;
                if (breakTimer <= 0.0f && target.hit) {
                    const game::BlockId broken = world.blockAt(target.block.x, target.block.y, target.block.z);
                    world.setBlock(target.block.x, target.block.y, target.block.z, game::BlockId::Air);
                    breakTimer = kBreakRepeatSeconds;

                    // Creative removes blocks outright; survival makes you pick
                    // them up.
                    if (!creative) {
                        const game::ItemId dropped = game::dropForBlock(broken);
                        if (dropped != game::ItemId::None) {
                            drops.spawn(glm::vec3{target.block} + glm::vec3{0.5f}, dropped, 1);
                        }
                    }
                }
            }

            if (!wantPlace) {
                placeTimer = 0.0f;
            } else {
                placeTimer -= deltaSeconds;
                const game::ItemStack& held = inventory.slot(selectedSlot);
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
                    }

                    world.setBlock(where.x, where.y, where.z, placing);
                    if (!creative) {
                        inventory.consumeOne(selectedSlot);
                        hudDirty = true;
                    }
                    placeTimer = kPlaceRepeatSeconds;
                }
            }

            // Streaming runs after edits so a broken block is re-meshed in the
            // same frame it changed, and shares the same budget.
            applyUpdates(world.update(player.position, kStreamingBudgetSeconds));

            // Dropped items live entirely on the main thread: there are a
            // handful of them and they touch the world only to read it.
            drops.update(world, player.position, deltaSeconds);
            for (const game::ItemEntities::Collectable& ready : drops.collectable(player.position)) {
                // Creative collects too. It has no need of the items, but a
                // drop that can never be picked up just orbits the player
                // forever, which is worse than a redundant pickup.
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

            const glm::vec3 sky = game::sky::skyColor(sunDirection);
            const engine::ClearColor background{sky.r, sky.g, sky.b, 1.0f};

            renderer.drawFrame(background, camera.viewMatrix(), highlight);

            ++framesSinceReport;
            if (now - lastReportTime >= std::chrono::seconds(1)) {
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
                                std::to_string(renderer.stats().triangles));
                framesSinceReport = 0;
                lastReportTime = now;
            }

            frameLimiter.waitForNextFrame();
        }

        engine::logInfo("Window closed. Saving world.");
        world.saveAll();
        world.store().savePlayer(game::SavedPlayer{player.position, camera.yaw, camera.pitch});
        engine::logInfo("Saved " + std::to_string(world.savedChunkCount()) + " modified chunks. Shutting down.");
    } catch (const std::exception& error) {
        engine::logError(std::string("Fatal: ") + error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
