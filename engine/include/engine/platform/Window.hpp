#pragma once

#include <array>
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
    W,
    A,
    S,
    D,
    E,
    F,
    G,
    C,
    V,
    Q,
    Space,
    LeftShift,
    LeftControl,
    Escape,
    // Kept contiguous and in order: game code maps a hotbar slot by subtracting
    // Num1 from the key.
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    /// Editing commands are not characters: the OS never sends them to the
    /// character callback, so text editing needs them from the key path.
    Backspace,
    Enter,
};

enum class MouseButton {
    Left,
    Right,
};

/// Cursor movement since the previous frame, in pixels.
struct CursorDelta {
    float x = 0.0f;
    float y = 0.0f;
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

    /// Whether a key is held right now. Use this for continuous movement, and
    /// `consumeKeyPresses` for one-shot actions.
    bool isKeyDown(Key key) const;

    /// Characters typed since the last call, in order. Clears the queue.
    ///
    /// This is deliberately **not** derived from `consumeKeyPresses`. A key
    /// reports a physical button; a character is what the OS produces after
    /// applying the keyboard layout, the shift state and any dead keys — so
    /// mapping keys to letters ourselves would type the wrong thing on every
    /// layout that is not US QWERTY.
    ///
    /// Restricted to printable ASCII, because the font atlas holds nothing
    /// else. Filtering at the boundary means a caller can never be handed a
    /// character it has no glyph for.
    std::string consumeTypedText();

    /// Whether a mouse button is held right now.
    ///
    /// Tracked from press and release events rather than polled from the OS, so
    /// it can be force-cleared. A button whose release event is never delivered
    /// — which a touchpad gesture or a focus change can cause — would otherwise
    /// stay stuck down for the rest of the session.
    bool isMouseButtonDown(MouseButton button) const;

    /// Mouse buttons pressed since the last call, in press order. Clears the
    /// queue. Use this for one-shot actions; `isMouseButtonDown` reports held
    /// state and would fire every frame.
    std::vector<MouseButton> consumeMouseButtonPresses();

    /// Forgets all held buttons and queued presses. Called automatically when
    /// the window loses focus.
    void clearInputState();

    /// Cursor movement since the last call. Clears the accumulator.
    CursorDelta consumeCursorDelta();

    /// Cursor position in pixels from the window's top-left. Only meaningful
    /// while the cursor is released; mouse-look uses the delta instead.
    double cursorX() const { return m_lastCursorX; }
    double cursorY() const { return m_lastCursorY; }

    /// Mouse wheel movement since the last call, in notches. Positive is away
    /// from the user. Clears the accumulator.
    float consumeScrollDelta();

    /// Captured means the cursor is hidden and locked to this window, which is
    /// what mouse-look needs. Releasing it hands the pointer back to the OS.
    void setCursorCaptured(bool captured);
    bool isCursorCaptured() const { return m_cursorCaptured; }

    /// Called by the platform resize callback. Not intended for game code.
    void markResized() { m_resized = true; }

    /// Called by the platform key callback. Not intended for game code.
    void recordKeyPress(Key key) { m_keyPresses.push_back(key); }

    /// Called by the platform character callback. Not intended for game code.
    void recordTypedCharacter(unsigned int codepoint);

    /// Called by the platform mouse callback. Not intended for game code.
    void recordMouseButton(MouseButton button, bool down);

    /// Called by the platform cursor callback. Not intended for game code.
    void recordCursorPosition(double x, double y);

    /// Called by the platform scroll callback. Not intended for game code.
    void recordScroll(double amount) { m_scrollDelta += static_cast<float>(amount); }

    GLFWwindow* handle() const { return m_handle; }

private:
    GLFWwindow* m_handle = nullptr;
    bool m_resized = false;
    std::vector<Key> m_keyPresses;
    /// Capped, so a frame that never drains it cannot grow without bound while
    /// someone leans on the keyboard.
    std::string m_typedText;
    std::vector<MouseButton> m_mouseButtonPresses;
    std::array<bool, 2> m_mouseButtonDown{};

    bool m_cursorCaptured = false;
    bool m_hasLastCursorPosition = false;
    double m_lastCursorX = 0.0;
    double m_lastCursorY = 0.0;
    CursorDelta m_cursorDelta;
    float m_scrollDelta = 0.0f;
};

} // namespace engine
