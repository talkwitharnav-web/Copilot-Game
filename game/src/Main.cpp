#include <engine/core/FrameLimiter.hpp>
#include <engine/core/Log.hpp>
#include <engine/platform/Window.hpp>
#include <engine/render/Camera.hpp>
#include <engine/render/Renderer.hpp>
#include <engine/render/VulkanContext.hpp>

#include <glm/glm.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>

namespace {

constexpr std::uint32_t kWindowWidth = 1280;
constexpr std::uint32_t kWindowHeight = 720;
constexpr engine::ClearColor kBackgroundColor{0.05f, 0.08f, 0.14f, 1.0f};

constexpr float kMoveMetresPerSecond = 6.0f;
constexpr float kLookRadiansPerPixel = 0.0025f;

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
        camera.position = {6.5f, 5.5f, 9.0f};
        camera.yaw = -2.15f;
        camera.pitch = -0.42f;
        window.setCursorCaptured(true);

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
