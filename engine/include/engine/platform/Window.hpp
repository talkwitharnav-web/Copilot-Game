#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace engine {

struct Extent2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

/// Keys the game currently binds. Grows as the game needs more; deliberately
/// not a full keyboard map yet.
enum class Key {
    F1,
    F2,
};

/// Owns a single OS window and the windowing library's lifetime.
///
/// Only one Window may exist at a time: the underlying library (GLFW) has
/// process-wide global state that this class initializes and shuts down.
class Window {
public:
    Window(std::uint32_t width, std::uint32_t height, const std::string& title);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    /// Processes queued OS events (input, resize, close). Must be called every frame.
    void pollEvents();

    bool shouldClose() const;

    /// Size of the drawable area in real pixels, which can differ from the
    /// requested size on high-DPI displays. Reports 0x0 while minimized.
    Extent2D framebufferExtent() const;

    bool isMinimized() const;

    /// Returns whether a resize happened since the last call, and clears the flag.
    bool consumeResizedFlag();

    /// Keys pressed since the last call, in press order. Clears the queue.
    /// Auto-repeat is ignored, so holding a key yields exactly one press.
    std::vector<Key> consumeKeyPresses();

    /// Called by the platform resize callback. Not intended for game code.
    void markResized() { m_resized = true; }

    /// Called by the platform key callback. Not intended for game code.
    void recordKeyPress(Key key) { m_keyPresses.push_back(key); }

    GLFWwindow* handle() const { return m_handle; }

private:
    GLFWwindow* m_handle = nullptr;
    bool m_resized = false;
    std::vector<Key> m_keyPresses;
};

} // namespace engine
