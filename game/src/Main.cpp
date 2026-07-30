#include <engine/core/FrameLimiter.hpp>
#include <engine/core/Log.hpp>
#include <engine/core/Paths.hpp>
#include <engine/platform/Window.hpp>
#include <engine/render/Camera.hpp>
#include <engine/render/Renderer.hpp>
#include <engine/render/VulkanContext.hpp>

#include "world/BlockOutline.hpp"
#include "world/Chunk.hpp"
#include "world/Player.hpp"
#include "world/Raycast.hpp"
#include "world/TerrainGenerator.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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
constexpr engine::ClearColor kBackgroundColor{0.45f, 0.62f, 0.80f, 1.0f};

constexpr float kLookRadiansPerPixel = 0.0025f;

// How far the player can reach to break or place, in metres.
constexpr float kReach = 12.0f;

// Holding the button keeps editing at this rate, so dragging across terrain
// does not need one click per block.
constexpr float kBreakRepeatSeconds = 0.15f;
constexpr float kPlaceRepeatSeconds = 0.18f;

// Time allowed per frame for generating and meshing chunks. Anything left over
// waits for the next frame, so a burst of new terrain slows the horizon down
// instead of freezing the game.
constexpr float kStreamingBudgetSeconds = 0.003f;

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
    switch (block) {
    case game::BlockId::Stone:
        return "Stone";
    case game::BlockId::Dirt:
        return "Dirt";
    case game::BlockId::Grass:
        return "Grass";
    case game::BlockId::Sand:
        return "Sand";
    default:
        return "Air";
    }
}

} // namespace

int main() {
    try {
        engine::Window window(kWindowWidth, kWindowHeight, "Voxel Game");
        engine::VulkanContext context(window);

        // Order defines the texture array layer indices, which must match
        // TextureLayer in Block.hpp.
        const std::filesystem::path textureDir = engine::executableDirectory() / "assets" / "textures" / "blocks";
        const std::vector<std::filesystem::path> blockTextures{
            textureDir / "stone.png", textureDir / "dirt.png", textureDir / "grass_top.png",
            textureDir / "grass_side.png", textureDir / "sand.png"};

        engine::Renderer renderer(context, window, blockTextures);

        std::size_t capIndex = kDefaultFpsCapIndex;
        engine::FrameLimiter frameLimiter(kFpsCapOptions[capIndex]);

        engine::Camera camera;
        camera.yaw = -1.57f;
        camera.pitch = -0.15f;
        window.setCursorCaptured(true);

        const auto buildStart = std::chrono::steady_clock::now();
        game::World world(kWorldSeed);

        // Spawn is chosen before any chunk exists, so the surface height comes
        // straight from the generator rather than from loaded blocks.
        const int spawnX = 8;
        const int spawnZ = 8;
        const glm::vec3 spawn{static_cast<float>(spawnX) + 0.5f,
                              static_cast<float>(game::surfaceHeightAt(kWorldSeed, spawnX, spawnZ) + 1),
                              static_cast<float>(spawnZ) + 0.5f};

        std::unordered_map<game::ChunkCoord, engine::MeshHandle> chunkMeshes;

        // Applies mesh changes and is the only place handles are created or
        // released. Anything that removes a chunk without going through here
        // would leak its GPU buffers for the rest of the session.
        const auto applyUpdates = [&](const std::vector<game::ChunkMeshUpdate>& updates) {
            for (const game::ChunkMeshUpdate& update : updates) {
                const auto existing = chunkMeshes.find(update.coord);

                if (update.removed) {
                    if (existing != chunkMeshes.end()) {
                        renderer.removeMesh(existing->second);
                        chunkMeshes.erase(existing);
                    }
                    continue;
                }

                if (existing != chunkMeshes.end()) {
                    renderer.updateMesh(existing->second, update.mesh);
                } else {
                    chunkMeshes.emplace(update.coord, renderer.addMesh(update.mesh));
                }
            }
        };

        applyUpdates(world.loadImmediately(spawn));
        const auto worldReady = std::chrono::steady_clock::now();

        renderer.setOverlayMesh(game::makeBlockOutline());

        game::Player player;
        player.position = spawn;
        player.position.y = static_cast<float>(world.highestSolid(spawnX, spawnZ) + 1);

        const auto ms = [](auto from, auto to) {
            return std::to_string(std::chrono::duration<float, std::milli>(to - from).count());
        };
        engine::logInfo("World seed " + std::to_string(kWorldSeed) + ", load radius " +
                        std::to_string(game::kLoadRadiusChunks) + " chunks");
        engine::logInfo("Initial load: " + std::to_string(world.loadedChunkCount()) + " chunks in " +
                        ms(buildStart, worldReady) + " ms");

        engine::logInfo("Frame cap: " + describeCap(kFpsCapOptions[capIndex]) + " (F1 lower, F2 raise)");
        engine::logInfo("Move: WASD. Space jump, Left Shift sneak, Left Ctrl sprint.");
        engine::logInfo("Left click breaks, right click places. 1-4 pick the block to place.");
        engine::logInfo("F toggles fly mode. Escape releases the mouse; click to recapture.");
        engine::logInfo("Entering main loop. Close the window to exit.");

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        auto lastReportTime = previousTime;
        int framesSinceReport = 0;

        game::BlockId heldBlock = game::BlockId::Stone;
        float breakTimer = 0.0f;
        float placeTimer = 0.0f;

        while (!window.shouldClose()) {
            window.pollEvents();

            for (const engine::Key key : window.consumeKeyPresses()) {
                if (key == engine::Key::Escape) {
                    window.setCursorCaptured(false);
                    continue;
                }
                if (key == engine::Key::F) {
                    player.flying = !player.flying;
                    player.velocity = glm::vec3{0.0f};
                    engine::logInfo(player.flying ? "Fly mode ON" : "Fly mode OFF");
                    continue;
                }
                if (key == engine::Key::Num1 || key == engine::Key::Num2 || key == engine::Key::Num3 ||
                    key == engine::Key::Num4) {
                    heldBlock = key == engine::Key::Num1   ? game::BlockId::Stone
                                : key == engine::Key::Num2 ? game::BlockId::Dirt
                                : key == engine::Key::Num3 ? game::BlockId::Grass
                                                           : game::BlockId::Sand;
                    engine::logInfo(std::string("Holding: ") + describeBlock(heldBlock));
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

            const auto now = Clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - previousTime).count();
            previousTime = now;

            // Drained every frame even when unused, so the queue cannot grow
            // without bound. It only exists to notice a click while the cursor
            // is free.
            const bool clicked = !window.consumeMouseButtonPresses().empty();
            const bool hadCursor = window.isCursorCaptured();
            if (!hadCursor && clicked) {
                window.setCursorCaptured(true);
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

            game::updatePlayer(player, move, world, deltaSeconds);
            camera.position = player.eyePosition();

            const game::RaycastHit target = game::raycast(world, camera.position, camera.forward(), kReach);

            // Gated on the cursor state from the start of the frame, so the
            // click that recaptures the cursor does not also swing at a block.
            const bool wantBreak = hadCursor && window.isMouseButtonDown(engine::MouseButton::Left);
            const bool wantPlace = hadCursor && window.isMouseButtonDown(engine::MouseButton::Right);

            // Timers only run down while the button is held, so releasing and
            // pressing again always acts immediately.
            if (!wantBreak) {
                breakTimer = 0.0f;
            } else {
                breakTimer -= deltaSeconds;
                if (breakTimer <= 0.0f && target.hit) {
                    world.setBlock(target.block.x, target.block.y, target.block.z, game::BlockId::Air);
                    breakTimer = kBreakRepeatSeconds;
                }
            }

            if (!wantPlace) {
                placeTimer = 0.0f;
            } else {
                placeTimer -= deltaSeconds;
                if (placeTimer <= 0.0f && target.hit && !game::playerOverlapsBlock(player, target.adjacent)) {
                    world.setBlock(target.adjacent.x, target.adjacent.y, target.adjacent.z, heldBlock);
                    placeTimer = kPlaceRepeatSeconds;
                }
            }

            // Streaming runs after edits so a broken block is re-meshed in the
            // same frame it changed, and shares the same budget.
            applyUpdates(world.update(player.position, kStreamingBudgetSeconds));

            std::optional<glm::mat4> highlight;
            if (target.hit) {
                highlight = glm::translate(glm::mat4{1.0f}, glm::vec3{target.block});
            }

            renderer.drawFrame(kBackgroundColor, camera.viewMatrix(), highlight);

            ++framesSinceReport;
            if (now - lastReportTime >= std::chrono::seconds(1)) {
                // Chunk, mesh and retired counts are reported together because a
                // streaming leak shows up as one of them climbing without bound
                // while the others hold steady.
                engine::logInfo(std::to_string(framesSinceReport) + " fps | chunks " +
                                std::to_string(world.loadedChunkCount()) + " | meshes " +
                                std::to_string(renderer.meshCount()) + " | pending " +
                                std::to_string(world.pendingChunkCount()) + " | retired " +
                                std::to_string(renderer.retiredMeshCount()));
                framesSinceReport = 0;
                lastReportTime = now;
            }

            frameLimiter.waitForNextFrame();
        }

        engine::logInfo("Window closed. Shutting down.");
    } catch (const std::exception& error) {
        engine::logError(std::string("Fatal: ") + error.what());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
