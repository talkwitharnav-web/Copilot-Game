#pragma once

#include <array>
#include <cstddef>
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
    ///
    /// **Grouped at the end for reading, not for deciding.** What may repeat is
    /// `repeatsWhenHeld` below and nothing else - an ordering is not an
    /// invariant, and `Enter` sits here while being the one member of the run
    /// that must *not* repeat.
    Backspace,
    Enter,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    /// Not a key. Exists so `repeatsWhenHeld` can be checked over the whole
    /// enum below; `toGlfwKey` maps it to nothing.
    Count,
};

/// Whether holding this key down means "again" - the filter on the repeat
/// queue, applied where keys enter it so there is one gate rather than one per
/// caller.
///
/// **A mechanism, not a policy.** It says nothing about text fields or what any
/// key does; only that auto-repeat is meaningful for it. `Enter` is deliberately
/// out: it is grouped with the editing keys because the OS never sends it to the
/// character callback, not because leaning on a confirm key should confirm
/// forty times a second - that is the same bug as `E` strobing the inventory,
/// which is the entire reason repeats live in their own queue.
///
/// **No `default:`, and two asserts, because the `default:` alone proves
/// nothing here.** MSVC's C4062 - the warning for an enumerator a switch does
/// not handle - is **off by default even at `/W4`**, and this build does not
/// turn it on, so omitting a case is silent. Measured, not assumed: a probe
/// with `Key::Home` removed compiled clean at `/W4` and warned only under
/// `/w44062`. The asserts below are therefore the real net.
constexpr bool repeatsWhenHeld(Key key) {
    switch (key) {
    case Key::Backspace:
    case Key::Delete:
    case Key::Left:
    case Key::Right:
    case Key::Up:
    case Key::Down:
    case Key::Home:
    case Key::End:
        return true;

    // Movement is read with `isKeyDown`, so a held `W` needs no repeat and
    // queueing it would spend the cap on the one caller that cannot use it.
    case Key::W:
    case Key::A:
    case Key::S:
    case Key::D:
    case Key::Space:
    case Key::LeftShift:
    case Key::LeftControl:

    // Toggles and one-shots. Every one of these would fire ~30 times a second
    // if it repeated.
    case Key::E:
    case Key::F:
    case Key::G:
    case Key::C:
    case Key::V:
    case Key::Q:
    case Key::Escape:
    case Key::Enter:
    case Key::Num1:
    case Key::Num2:
    case Key::Num3:
    case Key::Num4:
    case Key::Num5:
    case Key::Num6:
    case Key::Num7:
    case Key::Num8:
    case Key::Num9:
    case Key::F1:
    case Key::F2:
    case Key::F3:
    case Key::F4:
    case Key::F5:
    case Key::F6:
    case Key::F7:
    case Key::F8:
    case Key::F9:
    case Key::F10:
    case Key::F11:
    case Key::F12:
    case Key::Count:
        return false;
    }
    return false;
}

/// **The single edit that fails this: adding or removing a `Key`.** Which is
/// exactly when someone must decide whether holding the new one means "again",
/// and this line is the only thing in the codebase that asks.
static_assert(static_cast<int>(Key::Count) == 44,
              "A key was added or removed. Answer for it in `repeatsWhenHeld` above, then "
              "correct this number.");

/// And the other half, because the count above cannot see a case label move
/// from one block to the other. This runs the real predicate over the real
/// enum rather than restating either, so it is not a derivation compared
/// against itself.
///
/// **The single edit that fails this: moving `Enter` up into the repeating
/// block** - the tempting one, since it sits with the editing keys - or letting
/// an editing key slip down into the toggles.
static_assert(
    [] {
        int repeating = 0;
        for (int key = 0; key < static_cast<int>(Key::Count); ++key) {
            if (repeatsWhenHeld(static_cast<Key>(key))) {
                ++repeating;
            }
        }
        return repeating;
    }() == 8,
    "The repeat group changed size: Backspace, Delete, the four arrows, Home and End are "
    "the eight keys that repeat, and Enter is deliberately not one of them.");

enum class MouseButton {
    Left,
    Right,
};

/// Cursor movement since the previous frame, in pixels.
struct CursorDelta {
    float x = 0.0f;
    float y = 0.0f;
};

/// Gamepad buttons, named for the Xbox layout.
///
/// **The order is the underlying library's order**, which is what lets the
/// translation be a cast instead of a table; `Window.cpp` asserts every
/// enumerator against the library's own constant so that stays true.
///
/// The triggers are deliberately absent: they are analogue, and how far one has
/// to travel before it counts as pressed is a judgement about feel, which
/// belongs to the game.
enum class GamepadButton {
    A,
    B,
    X,
    Y,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    Guide,
    LeftThumb,
    RightThumb,
    DpadUp,
    DpadRight,
    DpadDown,
    DpadLeft,
    Count,
};

/// Stick and trigger positions.
///
/// Sticks run -1 to 1, and **Y is negative upward** - that is the library's
/// convention, not a choice made here.
///
/// Triggers run 0 (released) to 1 (fully pressed). The device reports them from
/// -1 to 1 and they are shifted here, at the one place that knows the device's
/// units, because a resting trigger read as 0.5 would mine continuously.
///
/// No dead zone is applied. How much of a worn stick to ignore is a player
/// setting, so it belongs with the settings rather than in the transport.
struct GamepadAxes {
    float leftX = 0.0f;
    float leftY = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;
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

    /// Keys the OS is **auto-repeating** because they are being held, in the
    /// order they arrived. Clears the queue. The first press is not in here -
    /// it is in `consumeKeyPresses` - so a text field drains both and gets one
    /// deletion per press plus one per repeat.
    ///
    /// **Only keys `repeatsWhenHeld` accepts ever reach this**, so the queue is
    /// the editing group its name implies rather than every held key. That is
    /// what makes the cap mean something: a leaned-on `W` cannot spend it, so a
    /// caller that drains only while a field is open still gets every Backspace
    /// rather than finding the cap already full of movement.
    ///
    /// **A separate queue rather than repeats folded into `consumeKeyPresses`,
    /// and that is the whole point.** Every other consumer of that queue is a
    /// toggle or a one-shot: holding `E` would strobe the inventory open and
    /// shut, `F10` would race through the tone mappers, and a held number key
    /// would fight the hotbar. Repeat is only ever wanted by something that
    /// eats a key per repetition, which today is the catalogue's search field
    /// and nothing else, so it is opt-in by being somewhere else entirely.
    ///
    /// Draining every frame is still the tidier habit - a dropped repeat is
    /// invisible while a queued burst arriving late is not - but it is no
    /// longer load-bearing.
    std::vector<Key> consumeKeyRepeats();

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

    /// Whether a gamepad is connected and has a known button layout.
    ///
    /// Only the lowest-numbered one is read. This game is single-player by
    /// design, so a second pad has nothing to drive.
    bool isGamepadConnected() const { return m_gamepadConnected; }

    /// Whether the pad is connected **and** this window is the one being played.
    ///
    /// A pad reports to every process regardless of focus, so this is the one
    /// to ask before acting on it - and in particular before starting a motor,
    /// which would otherwise carry on running behind whatever the player
    /// alt-tabbed to.
    bool isGamepadLive() const { return m_gamepadLive; }

    /// The gamepad's sticks and triggers as of the last `pollEvents`.
    /// All zero while nothing is connected.
    const GamepadAxes& gamepadAxes() const { return m_gamepadAxes; }

    /// Whether a gamepad button is held right now.
    bool isGamepadButtonDown(GamepadButton button) const;

    /// Whether a gamepad button went down during the last `pollEvents`.
    ///
    /// **Not a queue that a caller drains**, unlike the keyboard. A gamepad is
    /// polled rather than delivered as events, so the edges are found once per
    /// frame and stay readable for the whole of it - which means two consumers
    /// can both see the same press, and the key queue's "first reader wins"
    /// trap does not exist here.
    bool wasGamepadButtonPressed(GamepadButton button) const;

    /// Runs the pad's two vibration motors, each 0 (off) to 1 (full).
    ///
    /// **`heavy` is the low-frequency motor and `light` the high-frequency
    /// one.** They are physically different weights, not a stereo pair: the
    /// heavy one thumps and the light one buzzes, and swapping them turns every
    /// impact in the game into a fizz.
    ///
    /// Silently does nothing where the platform cannot vibrate, or for a pad
    /// the vibration API does not recognise. **Stops on its own when the window
    /// loses focus and when the window is destroyed** - a pad left buzzing
    /// behind an alt-tab is the one fault this can inflict on the rest of the
    /// desktop, and it outlives the process that caused it.
    void setGamepadRumble(float heavy, float light);

    /// Called by the platform resize callback. Not intended for game code.
    void markResized() { m_resized = true; }

    /// Called by the platform key callback. Not intended for game code.
    void recordKeyPress(Key key) { m_keyPresses.push_back(key); }

    /// Called by the platform key callback for an auto-repeat. Not intended for
    /// game code. **Filters through `repeatsWhenHeld`** - the gate is here, not
    /// at the callback, so there is one of it.
    void recordKeyRepeat(Key key);

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
    /// Reads the first connected gamepad and works out this frame's edges.
    void updateGamepad();

    GLFWwindow* m_handle = nullptr;
    bool m_resized = false;
    std::vector<Key> m_keyPresses;
    /// Capped, unlike the press queue: a frame that does not drain this is the
    /// ordinary case, because only a text field wants repeats at all.
    std::vector<Key> m_keyRepeats;
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

    static constexpr std::size_t kGamepadButtonCount = static_cast<std::size_t>(GamepadButton::Count);
    bool m_gamepadConnected = false;
    /// Whether the previous update actually read the pad. Distinct from
    /// connected, because focus can be lost without unplugging anything.
    bool m_gamepadLive = false;
    GamepadAxes m_gamepadAxes;
    std::array<bool, kGamepadButtonCount> m_gamepadDown{};
    std::array<bool, kGamepadButtonCount> m_gamepadPressed{};

    /// The vibration entry point, resolved at run time. Typeless here so the
    /// header stays free of platform headers.
    void* m_rumbleProc = nullptr;
    bool m_rumbleResolved = false;
    /// Which of the vibration API's four slots this pad answers on, or -1 while
    /// unknown. Forgotten when a call fails, so a reconnected pad is found.
    int m_rumbleSlot = -1;
    /// What was last sent, so an unchanged level costs no call at all.
    std::uint16_t m_rumbleSentHeavy = 0;
    std::uint16_t m_rumbleSentLight = 0;
};

} // namespace engine
