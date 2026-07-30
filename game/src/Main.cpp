#include <engine/core/FrameLimiter.hpp>
#include <engine/core/Log.hpp>
#include <engine/platform/Window.hpp>
#include <engine/render/Camera.hpp>
#include <engine/render/Renderer.hpp>
#include <engine/render/VulkanContext.hpp>

#include "world/Chunk.hpp"
#include "world/Player.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kWindowWidth = 1280;
constexpr std::uint32_t kWindowHeight = 720;
constexpr engine::ClearColor kBackgroundColor{0.45f, 0.62f, 0.80f, 1.0f};

constexpr float kLookRadiansPerPixel = 0.0025f;

// Change this and the entire world changes, reproducibly.
constexpr std::uint32_t kWorldSeed = 1337u;

// A fixed patch of world. Endless streaming is M8.
constexpr int kWorldChunksX = 6;
constexpr int kWorldChunksY = 2;
constexpr int kWorldChunksZ = 6;

// Selectable frame caps, lowest to highest. 0 means uncapped.
// Temporary keyboard-driven stand-in until there is a real settings screen.
constexpr std::array<double, 8> kFpsCapOptions{30.0, 60.0, 90.0, 120.0, 144.0, 165.0, 240.0, 0.0};
constexpr std::size_t kDefaultFpsCapIndex = 3;

std::string describeCap(double fps) {
    return fps > 0.0 ? std::to_string(static_cast<int>(fps)) + " fps" : "uncapped";
}

} // namespace

int main() {
    try {
        engine::Window window(kWindowWidth, kWindowHeight, "Voxel Game");
        engine::VulkanContext context(window);
        engine::Renderer renderer(context, window);

        std::size_t capIndex = kDefaultFpsCapIndex;
        engine::FrameLimiter frameLimiter(kFpsCapOptions[capIndex]);

        engine::Camera camera;
        camera.yaw = -1.57f;
        camera.pitch = -0.15f;
        window.setCursorCaptured(true);

        const auto buildStart = std::chrono::steady_clock::now();
        const game::World world(kWorldSeed, kWorldChunksX, kWorldChunksY, kWorldChunksZ);
        const auto generateEnd = std::chrono::steady_clock::now();

        const std::vector<engine::MeshData> meshes = world.buildMeshes();
        const auto meshEnd = std::chrono::steady_clock::now();

        renderer.uploadMeshes(meshes);

        std::size_t totalFaces = 0;
        for (const engine::MeshData& mesh : meshes) {
            totalFaces += mesh.indices.size() / 6;
        }

        // Drop the player onto the surface at the middle of the world rather
        // than at a fixed height, which would either bury them or drop them far.
        game::Player player;
        const int spawnX = world.blocksX() / 2;
        const int spawnZ = world.blocksZ() / 2;
        player.position = {static_cast<float>(spawnX) + 0.5f,
                           static_cast<float>(world.highestSolid(spawnX, spawnZ) + 1),
                           static_cast<float>(spawnZ) + 0.5f};

        const auto ms = [](auto from, auto to) {
            return std::to_string(std::chrono::duration<float, std::milli>(to - from).count());
        };
        engine::logInfo("World: " + std::to_string(world.chunkCount()) + " chunks, seed " +
                        std::to_string(kWorldSeed) + ", " + std::to_string(world.blocksX()) + " blocks across");
        engine::logInfo("Generated in " + ms(buildStart, generateEnd) + " ms, meshed in " +
                        ms(generateEnd, meshEnd) + " ms");
        engine::logInfo("Visible faces: " + std::to_string(totalFaces));

        engine::logInfo("Frame cap: " + describeCap(kFpsCapOptions[capIndex]) + " (F1 lower, F2 raise)");
        engine::logInfo("Move: WASD. Space jump, Left Shift sneak, Left Ctrl sprint.");
        engine::logInfo("F toggles fly mode. Escape releases the mouse; click to recapture.");
        engine::logInfo("Entering main loop. Close the window to exit.");

        using Clock = std::chrono::steady_clock;
        auto previousTime = Clock::now();
        auto lastReportTime = previousTime;
        int framesSinceReport = 0;

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

            if (!window.isCursorCaptured() && window.isMouseButtonDown(engine::MouseButton::Left)) {
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

            renderer.drawFrame(kBackgroundColor, camera.viewMatrix());

            ++framesSinceReport;
            if (now - lastReportTime >= std::chrono::seconds(1)) {
                engine::logInfo(std::to_string(framesSinceReport) + " fps");
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
