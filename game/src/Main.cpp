#include <engine/core/FrameLimiter.hpp>
#include <engine/core/Log.hpp>
#include <engine/platform/Window.hpp>
#include <engine/render/Camera.hpp>
#include <engine/render/Renderer.hpp>
#include <engine/render/VulkanContext.hpp>

#include "world/Chunk.hpp"
#include "world/ChunkMesher.hpp"
#include "world/TerrainGenerator.hpp"

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kWindowWidth = 1280;
constexpr std::uint32_t kWindowHeight = 720;
constexpr engine::ClearColor kBackgroundColor{0.45f, 0.62f, 0.80f, 1.0f};

constexpr float kMoveMetresPerSecond = 22.0f;
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

std::size_t chunkIndex(int x, int y, int z) {
    return static_cast<std::size_t>((y * kWorldChunksZ + z) * kWorldChunksX + x);
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
        camera.position = {96.0f, 78.0f, 250.0f};
        camera.yaw = -1.57f;
        camera.pitch = -0.35f;
        window.setCursorCaptured(true);

        const auto generateStart = std::chrono::steady_clock::now();

        std::vector<game::Chunk> chunks(static_cast<std::size_t>(kWorldChunksX * kWorldChunksY * kWorldChunksZ));
        for (int y = 0; y < kWorldChunksY; ++y) {
            for (int z = 0; z < kWorldChunksZ; ++z) {
                for (int x = 0; x < kWorldChunksX; ++x) {
                    chunks[chunkIndex(x, y, z)] = game::generateChunk(kWorldSeed, game::ChunkCoord{x, y, z});
                }
            }
        }

        const auto generateEnd = std::chrono::steady_clock::now();

        // Determinism is this milestone's acceptance criterion, so it is checked
        // rather than assumed: regenerating a chunk must reproduce it byte for byte.
        const game::Chunk repeat = game::generateChunk(kWorldSeed, game::ChunkCoord{2, 0, 3});
        const bool deterministic =
            std::memcmp(&repeat, &chunks[chunkIndex(2, 0, 3)], sizeof(game::Chunk)) == 0;
        if (deterministic) {
            engine::logInfo("Determinism check passed: same seed reproduces the same chunk.");
        } else {
            engine::logError("Determinism check FAILED: generation is not a pure function of (seed, coord).");
        }

        std::vector<engine::MeshData> meshes;
        meshes.reserve(chunks.size());
        std::size_t totalFaces = 0;

        for (int y = 0; y < kWorldChunksY; ++y) {
            for (int z = 0; z < kWorldChunksZ; ++z) {
                for (int x = 0; x < kWorldChunksX; ++x) {
                    game::ChunkNeighbours neighbours;
                    if (x > 0) {
                        neighbours.negativeX = &chunks[chunkIndex(x - 1, y, z)];
                    }
                    if (x + 1 < kWorldChunksX) {
                        neighbours.positiveX = &chunks[chunkIndex(x + 1, y, z)];
                    }
                    if (y > 0) {
                        neighbours.negativeY = &chunks[chunkIndex(x, y - 1, z)];
                    }
                    if (y + 1 < kWorldChunksY) {
                        neighbours.positiveY = &chunks[chunkIndex(x, y + 1, z)];
                    }
                    if (z > 0) {
                        neighbours.negativeZ = &chunks[chunkIndex(x, y, z - 1)];
                    }
                    if (z + 1 < kWorldChunksZ) {
                        neighbours.positiveZ = &chunks[chunkIndex(x, y, z + 1)];
                    }

                    const glm::vec3 origin{static_cast<float>(x * game::Chunk::kSize),
                                           static_cast<float>(y * game::Chunk::kSize),
                                           static_cast<float>(z * game::Chunk::kSize)};

                    engine::MeshData mesh = game::meshChunk(chunks[chunkIndex(x, y, z)], neighbours, origin);
                    totalFaces += mesh.indices.size() / 6;
                    meshes.push_back(std::move(mesh));
                }
            }
        }

        const auto meshEnd = std::chrono::steady_clock::now();
        renderer.uploadMeshes(meshes);

        const auto ms = [](auto from, auto to) {
            return std::to_string(std::chrono::duration<float, std::milli>(to - from).count());
        };
        engine::logInfo("World: " + std::to_string(chunks.size()) + " chunks, seed " + std::to_string(kWorldSeed) +
                        ", " + std::to_string(kWorldChunksX * game::Chunk::kSize) + " blocks across");
        engine::logInfo("Generated in " + ms(generateStart, generateEnd) + " ms, meshed in " +
                        ms(generateEnd, meshEnd) + " ms");
        engine::logInfo("Visible faces: " + std::to_string(totalFaces));

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
