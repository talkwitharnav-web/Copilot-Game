#include <engine/core/FrameLimiter.hpp>
#include <engine/core/Log.hpp>
#include <engine/platform/Window.hpp>
#include <engine/render/Camera.hpp>
#include <engine/render/Renderer.hpp>
#include <engine/render/VulkanContext.hpp>

#include "world/Chunk.hpp"
#include "world/ChunkMesher.hpp"

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

constexpr std::uint32_t kWindowWidth = 1280;
constexpr std::uint32_t kWindowHeight = 720;
constexpr engine::ClearColor kBackgroundColor{0.45f, 0.62f, 0.80f, 1.0f};

constexpr float kMoveMetresPerSecond = 12.0f;
constexpr float kLookRadiansPerPixel = 0.0025f;

// Selectable frame caps, lowest to highest. 0 means uncapped.
// Temporary keyboard-driven stand-in until there is a real settings screen.
constexpr std::array<double, 8> kFpsCapOptions{30.0, 60.0, 90.0, 120.0, 144.0, 165.0, 240.0, 0.0};
constexpr std::size_t kDefaultFpsCapIndex = 3;

std::string describeCap(double fps) {
    return fps > 0.0 ? std::to_string(static_cast<int>(fps)) + " fps" : "uncapped";
}

/// Hand-written surface shape, not procedural generation. Seeded noise is M5;
/// this exists only so the first chunk is interesting enough to fly around.
int surfaceHeight(int x, int z) {
    const auto fx = static_cast<float>(x);
    const auto fz = static_cast<float>(z);
    const float hills = 4.0f * std::sin(fx * 0.22f) * std::cos(fz * 0.19f);
    const float ridge = 2.5f * std::sin((fx + fz) * 0.11f);
    return static_cast<int>(14.0f + hills + ridge);
}

game::Chunk buildChunk() {
    game::Chunk chunk;

    for (int z = 0; z < game::Chunk::kSize; ++z) {
        for (int x = 0; x < game::Chunk::kSize; ++x) {
            const int surface = surfaceHeight(x, z);

            for (int y = 0; y <= surface && y < game::Chunk::kSize; ++y) {
                if (y == surface) {
                    chunk.set(x, y, z, surface > 16 ? game::BlockId::Grass : game::BlockId::Sand);
                } else if (y > surface - 4) {
                    chunk.set(x, y, z, game::BlockId::Dirt);
                } else {
                    chunk.set(x, y, z, game::BlockId::Stone);
                }
            }
        }
    }

    // Carve a hollow so there are interior surfaces to look at. If face culling
    // were wrong, a cave is where it would be obvious.
    constexpr glm::vec3 caveCenter{16.0f, 8.0f, 16.0f};
    constexpr float caveRadius = 5.5f;
    for (int y = 0; y < game::Chunk::kSize; ++y) {
        for (int z = 0; z < game::Chunk::kSize; ++z) {
            for (int x = 0; x < game::Chunk::kSize; ++x) {
                const glm::vec3 p{x, y, z};
                if (glm::length(p - caveCenter) < caveRadius) {
                    chunk.set(x, y, z, game::BlockId::Air);
                }
            }
        }
    }

    return chunk;
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
        camera.position = {48.0f, 30.0f, 48.0f};
        camera.yaw = -2.36f;
        camera.pitch = -0.34f;
        window.setCursorCaptured(true);

        const game::Chunk chunk = buildChunk();
        const engine::MeshData chunkMesh = game::meshChunk(chunk, glm::vec3{0.0f});
        renderer.uploadMesh(chunkMesh);

        // A solid 32-cubed chunk holds 32768 blocks; drawing every face would be
        // 196608 of them. Only the ones touching air are ever created.
        engine::logInfo("Chunk meshed: " + std::to_string(chunkMesh.indices.size() / 6) + " faces, " +
                        std::to_string(chunkMesh.vertices.size()) + " vertices");

        engine::logInfo("Frame cap: " + describeCap(kFpsCapOptions[capIndex]) + " (F1 lower, F2 raise)");
        engine::logInfo("Move: WASD, Space up, Left Shift down. Look: mouse.");
        engine::logInfo("Escape releases the mouse; click the window to recapture.");
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

            glm::vec3 movement{0.0f};
            if (window.isKeyDown(engine::Key::W)) {
                movement += camera.forward();
            }
            if (window.isKeyDown(engine::Key::S)) {
                movement -= camera.forward();
            }
            if (window.isKeyDown(engine::Key::D)) {
                movement += camera.right();
            }
            if (window.isKeyDown(engine::Key::A)) {
                movement -= camera.right();
            }
            if (window.isKeyDown(engine::Key::Space)) {
                movement.y += 1.0f;
            }
            if (window.isKeyDown(engine::Key::LeftShift)) {
                movement.y -= 1.0f;
            }

            // Normalising stops diagonal movement outrunning straight movement,
            // and scaling by delta keeps speed independent of frame rate.
            if (glm::dot(movement, movement) > 0.0f) {
                camera.position += glm::normalize(movement) * kMoveMetresPerSecond * deltaSeconds;
            }

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
