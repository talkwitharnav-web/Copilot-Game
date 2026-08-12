#include "engine/platform/Window.hpp"

#include "engine/core/Log.hpp"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace engine {
namespace {

/// Enough for any field we will show; beyond it keystrokes are dropped rather
/// than queued, so a frame that forgets to drain cannot grow the buffer.
constexpr std::size_t kMaxTypedText = 256;

void glfwErrorCallback(int code, const char* description) {
    logError("GLFW error " + std::to_string(code) + ": " + (description ? description : "unknown"));
}

// `GamepadButton` is translated by a cast rather than a table, so every
// enumerator has to line up with the library's own constant. These are what
// make that safe: a reordered enum stops the build instead of quietly binding
// jump to sneak.
static_assert(static_cast<int>(GamepadButton::A) == GLFW_GAMEPAD_BUTTON_A);
static_assert(static_cast<int>(GamepadButton::B) == GLFW_GAMEPAD_BUTTON_B);
static_assert(static_cast<int>(GamepadButton::X) == GLFW_GAMEPAD_BUTTON_X);
static_assert(static_cast<int>(GamepadButton::Y) == GLFW_GAMEPAD_BUTTON_Y);
static_assert(static_cast<int>(GamepadButton::LeftBumper) == GLFW_GAMEPAD_BUTTON_LEFT_BUMPER);
static_assert(static_cast<int>(GamepadButton::RightBumper) == GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER);
static_assert(static_cast<int>(GamepadButton::Back) == GLFW_GAMEPAD_BUTTON_BACK);
static_assert(static_cast<int>(GamepadButton::Start) == GLFW_GAMEPAD_BUTTON_START);
static_assert(static_cast<int>(GamepadButton::Guide) == GLFW_GAMEPAD_BUTTON_GUIDE);
static_assert(static_cast<int>(GamepadButton::LeftThumb) == GLFW_GAMEPAD_BUTTON_LEFT_THUMB);
static_assert(static_cast<int>(GamepadButton::RightThumb) == GLFW_GAMEPAD_BUTTON_RIGHT_THUMB);
static_assert(static_cast<int>(GamepadButton::DpadUp) == GLFW_GAMEPAD_BUTTON_DPAD_UP);
static_assert(static_cast<int>(GamepadButton::DpadRight) == GLFW_GAMEPAD_BUTTON_DPAD_RIGHT);
static_assert(static_cast<int>(GamepadButton::DpadDown) == GLFW_GAMEPAD_BUTTON_DPAD_DOWN);
static_assert(static_cast<int>(GamepadButton::DpadLeft) == GLFW_GAMEPAD_BUTTON_DPAD_LEFT);
static_assert(static_cast<int>(GamepadButton::Count) == GLFW_GAMEPAD_BUTTON_LAST + 1,
              "every button the library reports needs a name here");

/// A trigger as the device reports it, turned into 0 released to 1 pressed.
///
/// The shift is not a taste: the Windows backend computes the axis as
/// `raw / 127.5 - 1`, so a released trigger genuinely sits at -1.
constexpr float triggerFromAxis(float axis) {
    return (axis + 1.0f) * 0.5f;
}

// The two ends of that derivation, checked rather than trusted. **The first is
// the one that matters**: a released trigger read as half pressed would mine
// continuously from the moment the game started, and nothing about the code
// would look wrong.
static_assert(triggerFromAxis(-1.0f) == 0.0f, "a released trigger must read as no press at all");
static_assert(triggerFromAxis(1.0f) == 1.0f, "a fully pulled trigger must read as a full press");
static_assert(triggerFromAxis(0.0f) == 0.5f, "and half its travel as half a press");

#ifdef _WIN32

/// The two motor speeds, in the vibration API's own layout and units.
///
/// **Left is the low-frequency motor and right the high-frequency one**, which
/// is the platform's naming, not a guess. Both run 0 to 65535.
struct XInputVibration {
    std::uint16_t left;
    std::uint16_t right;
};
using XInputSetStateFn = DWORD(WINAPI*)(DWORD, XInputVibration*);

/// Finds the vibration entry point at run time.
///
/// **Loaded rather than linked**, so the build needs no import library and the
/// game still starts on a machine carrying a different version. The library's
/// name has changed three times; the function's has not.
void* loadRumbleProc() {
    static constexpr const char* kLibraries[]{"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll"};
    for (const char* name : kLibraries) {
        // Never freed: the process wants it until it exits, and unloading it
        // while a motor is running is how a pad gets left buzzing.
        if (HMODULE library = LoadLibraryA(name); library != nullptr) {
            if (FARPROC found = GetProcAddress(library, "XInputSetState"); found != nullptr) {
                return reinterpret_cast<void*>(found);
            }
        }
    }
    return nullptr;
}

#endif

/// The one place engine key names are translated to the windowing library's.
int toGlfwKey(Key key) {
    switch (key) {
    case Key::W:
        return GLFW_KEY_W;
    case Key::A:
        return GLFW_KEY_A;
    case Key::S:
        return GLFW_KEY_S;
    case Key::D:
        return GLFW_KEY_D;
    case Key::E:
        return GLFW_KEY_E;
    case Key::F:
        return GLFW_KEY_F;
    case Key::G:
        return GLFW_KEY_G;
    case Key::C:
        return GLFW_KEY_C;
    case Key::V:
        return GLFW_KEY_V;
    case Key::Q:
        return GLFW_KEY_Q;
    case Key::Space:
        return GLFW_KEY_SPACE;
    case Key::LeftShift:
        return GLFW_KEY_LEFT_SHIFT;
    case Key::LeftControl:
        return GLFW_KEY_LEFT_CONTROL;
    case Key::Escape:
        return GLFW_KEY_ESCAPE;
    case Key::Num1:
        return GLFW_KEY_1;
    case Key::Num2:
        return GLFW_KEY_2;
    case Key::Num3:
        return GLFW_KEY_3;
    case Key::Num4:
        return GLFW_KEY_4;
    case Key::Num5:
        return GLFW_KEY_5;
    case Key::Num6:
        return GLFW_KEY_6;
    case Key::Num7:
        return GLFW_KEY_7;
    case Key::Num8:
        return GLFW_KEY_8;
    case Key::Num9:
        return GLFW_KEY_9;
    case Key::F1:
        return GLFW_KEY_F1;
    case Key::F2:
        return GLFW_KEY_F2;
    case Key::F3:
        return GLFW_KEY_F3;
    case Key::F4:
        return GLFW_KEY_F4;
    case Key::F5:
        return GLFW_KEY_F5;
    case Key::F6:
        return GLFW_KEY_F6;
    case Key::F7:
        return GLFW_KEY_F7;
    case Key::F8:
        return GLFW_KEY_F8;
    case Key::F9:
        return GLFW_KEY_F9;
    case Key::F10:
        return GLFW_KEY_F10;
    case Key::F11:
        return GLFW_KEY_F11;
    case Key::F12:
        return GLFW_KEY_F12;
    case Key::Backspace:
        return GLFW_KEY_BACKSPACE;
    case Key::Enter:
        return GLFW_KEY_ENTER;
    }
    return GLFW_KEY_UNKNOWN;
}

bool fromGlfwKey(int glfwKey, Key& out) {
    switch (glfwKey) {
    case GLFW_KEY_W:
        out = Key::W;
        return true;
    case GLFW_KEY_A:
        out = Key::A;
        return true;
    case GLFW_KEY_S:
        out = Key::S;
        return true;
    case GLFW_KEY_D:
        out = Key::D;
        return true;
    case GLFW_KEY_E:
        out = Key::E;
        return true;
    case GLFW_KEY_F:
        out = Key::F;
        return true;
    case GLFW_KEY_G:
        out = Key::G;
        return true;
    case GLFW_KEY_C:
        out = Key::C;
        return true;
    case GLFW_KEY_V:
        out = Key::V;
        return true;
    case GLFW_KEY_Q:
        out = Key::Q;
        return true;
    case GLFW_KEY_SPACE:
        out = Key::Space;
        return true;
    case GLFW_KEY_LEFT_SHIFT:
        out = Key::LeftShift;
        return true;
    case GLFW_KEY_LEFT_CONTROL:
        out = Key::LeftControl;
        return true;
    case GLFW_KEY_ESCAPE:
        out = Key::Escape;
        return true;
    case GLFW_KEY_1:
        out = Key::Num1;
        return true;
    case GLFW_KEY_2:
        out = Key::Num2;
        return true;
    case GLFW_KEY_3:
        out = Key::Num3;
        return true;
    case GLFW_KEY_4:
        out = Key::Num4;
        return true;
    case GLFW_KEY_5:
        out = Key::Num5;
        return true;
    case GLFW_KEY_6:
        out = Key::Num6;
        return true;
    case GLFW_KEY_7:
        out = Key::Num7;
        return true;
    case GLFW_KEY_8:
        out = Key::Num8;
        return true;
    case GLFW_KEY_9:
        out = Key::Num9;
        return true;
    case GLFW_KEY_F1:
        out = Key::F1;
        return true;
    case GLFW_KEY_F2:
        out = Key::F2;
        return true;
    case GLFW_KEY_F3:
        out = Key::F3;
        return true;
    case GLFW_KEY_F4:
        out = Key::F4;
        return true;
    case GLFW_KEY_F5:
        out = Key::F5;
        return true;
    case GLFW_KEY_F6:
        out = Key::F6;
        return true;
    case GLFW_KEY_F7:
        out = Key::F7;
        return true;
    case GLFW_KEY_F8:
        out = Key::F8;
        return true;
    case GLFW_KEY_F9:
        out = Key::F9;
        return true;
    case GLFW_KEY_F10:
        out = Key::F10;
        return true;
    case GLFW_KEY_F11:
        out = Key::F11;
        return true;
    case GLFW_KEY_F12:
        out = Key::F12;
        return true;
    case GLFW_KEY_BACKSPACE:
        out = Key::Backspace;
        return true;
    case GLFW_KEY_ENTER:
        out = Key::Enter;
        return true;
    default:
        return false;
    }
}

} // namespace

Window::Window(std::uint32_t width, std::uint32_t height, const std::string& title) {
    glfwSetErrorCallback(glfwErrorCallback);

    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // GLFW defaults to creating an OpenGL context; Vulkan manages its own.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    m_handle = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title.c_str(), nullptr, nullptr);
    if (m_handle == nullptr) {
        glfwTerminate();
        throw std::runtime_error("Failed to create window");
    }

    glfwSetWindowUserPointer(m_handle, this);
    glfwSetFramebufferSizeCallback(m_handle, [](GLFWwindow* handle, int, int) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self != nullptr) {
            self->markResized();
        }
    });

    glfwSetKeyCallback(m_handle, [](GLFWwindow* handle, int key, int, int action, int) {
        if (action != GLFW_PRESS) {
            return;
        }
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self == nullptr) {
            return;
        }
        Key mapped{};
        if (fromGlfwKey(key, mapped)) {
            self->recordKeyPress(mapped);
        }
    });

    glfwSetCharCallback(m_handle, [](GLFWwindow* handle, unsigned int codepoint) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self != nullptr) {
            self->recordTypedCharacter(codepoint);
        }
    });

    glfwSetCursorPosCallback(m_handle, [](GLFWwindow* handle, double x, double y) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self != nullptr) {
            self->recordCursorPosition(x, y);
        }
    });

    glfwSetMouseButtonCallback(m_handle, [](GLFWwindow* handle, int button, int action, int) {
        if (action != GLFW_PRESS && action != GLFW_RELEASE) {
            return;
        }
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self == nullptr) {
            return;
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            self->recordMouseButton(MouseButton::Left, action == GLFW_PRESS);
        } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
            self->recordMouseButton(MouseButton::Right, action == GLFW_PRESS);
        }
    });

    glfwSetScrollCallback(m_handle, [](GLFWwindow* handle, double, double y) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self != nullptr) {
            self->recordScroll(y);
        }
    });

    // Losing focus means any release event goes to another window, which would
    // leave a button or the cursor stuck in this one.
    glfwSetWindowFocusCallback(m_handle, [](GLFWwindow* handle, int focused) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(handle));
        if (self != nullptr && focused == GLFW_FALSE) {
            self->clearInputState();
            self->setCursorCaptured(false);
        }
    });

    logInfo("Window created (" + std::to_string(width) + "x" + std::to_string(height) + ")");
}

Window::~Window() {
    // Before the window goes, or the motors keep running with nothing left to
    // stop them.
    setGamepadRumble(0.0f, 0.0f);
    if (m_handle != nullptr) {
        glfwDestroyWindow(m_handle);
    }
    glfwTerminate();
}

void Window::pollEvents() {
    glfwPollEvents();
    // A gamepad is polled rather than delivered as events, so it is sampled
    // here alongside them and every reader sees one consistent frame.
    updateGamepad();
}

void Window::updateGamepad() {
    GLFWgamepadstate state{};
    bool found = false;
    for (int id = GLFW_JOYSTICK_1; id <= GLFW_JOYSTICK_LAST && !found; ++id) {
        // `glfwGetGamepadState` answers only for a pad the library has a button
        // mapping for, which is what keeps a flight stick from being read as an
        // Xbox controller.
        if (glfwJoystickIsGamepad(id) == GLFW_TRUE && glfwGetGamepadState(id, &state) == GLFW_TRUE) {
            found = true;
        }
    }

    // **A gamepad keeps reporting to every process, focused or not** - the
    // keyboard does not, so nothing else here needs this check. Without it the
    // player walks on while you are working in another window.
    const bool focused = glfwGetWindowAttrib(m_handle, GLFW_FOCUSED) == GLFW_TRUE;
    const bool wasLive = m_gamepadLive;

    if (!found || !focused) {
        // Still "connected" when only focus is missing, so the game can go on
        // saying a pad is there; it simply reports nothing until we are back.
        m_gamepadConnected = found;
        m_gamepadLive = false;
        m_gamepadAxes = GamepadAxes{};
        m_gamepadDown.fill(false);
        m_gamepadPressed.fill(false);
        return;
    }

    m_gamepadConnected = true;
    m_gamepadLive = true;

    for (std::size_t i = 0; i < kGamepadButtonCount; ++i) {
        const bool down = state.buttons[i] == GLFW_PRESS;
        // **A button already held on the first live frame is not a press.**
        // Held state is cleared while unplugged or unfocused, so without this
        // every held button would fire again the moment the window came back.
        m_gamepadPressed[i] = down && !m_gamepadDown[i] && wasLive;
        m_gamepadDown[i] = down;
    }

    m_gamepadAxes.leftX = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X];
    m_gamepadAxes.leftY = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y];
    m_gamepadAxes.rightX = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X];
    m_gamepadAxes.rightY = state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y];
    m_gamepadAxes.leftTrigger = triggerFromAxis(state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER]);
    m_gamepadAxes.rightTrigger = triggerFromAxis(state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER]);
}

bool Window::isGamepadButtonDown(GamepadButton button) const {
    return m_gamepadDown[static_cast<std::size_t>(button)];
}

bool Window::wasGamepadButtonPressed(GamepadButton button) const {
    return m_gamepadPressed[static_cast<std::size_t>(button)];
}

void Window::setGamepadRumble(float heavy, float light) {
#ifdef _WIN32
    const auto quantise = [](float level) {
        return static_cast<std::uint16_t>(std::clamp(level, 0.0f, 1.0f) * 65535.0f);
    };
    XInputVibration wanted{quantise(heavy), quantise(light)};

    // **Nothing changed means no call at all**, which is what keeps this off
    // the frame budget: a still pad is the overwhelmingly common case, and this
    // also means a machine with no vibration API never even looks for one.
    if (wanted.left == m_rumbleSentHeavy && wanted.right == m_rumbleSentLight) {
        return;
    }

    if (!m_rumbleResolved) {
        m_rumbleProc = loadRumbleProc();
        m_rumbleResolved = true;
    }
    if (m_rumbleProc == nullptr) {
        return;
    }
    auto* setState = reinterpret_cast<XInputSetStateFn>(m_rumbleProc);

    // The pad's slot, which is not the same numbering the window library uses,
    // so it is found by asking rather than assumed. Silence is a valid probe.
    if (m_rumbleSlot < 0) {
        XInputVibration silence{0, 0};
        for (int slot = 0; slot < 4; ++slot) {
            if (setState(static_cast<DWORD>(slot), &silence) == ERROR_SUCCESS) {
                m_rumbleSlot = slot;
                break;
            }
        }
    }
    if (m_rumbleSlot < 0) {
        return;
    }

    if (setState(static_cast<DWORD>(m_rumbleSlot), &wanted) != ERROR_SUCCESS) {
        // Unplugged mid-effect. Forget the slot so a reconnected pad is found,
        // and forget what was sent so the next level is not skipped as a repeat.
        m_rumbleSlot = -1;
        m_rumbleSentHeavy = 0;
        m_rumbleSentLight = 0;
        return;
    }

    m_rumbleSentHeavy = wanted.left;
    m_rumbleSentLight = wanted.right;
#else
    (void)heavy;
    (void)light;
#endif
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(m_handle) == GLFW_TRUE;
}

Extent2D Window::framebufferExtent() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_handle, &width, &height);
    return Extent2D{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
}

bool Window::isMinimized() const {
    const Extent2D extent = framebufferExtent();
    return extent.width == 0 || extent.height == 0;
}

bool Window::consumeResizedFlag() {
    const bool wasResized = m_resized;
    m_resized = false;
    return wasResized;
}

std::vector<Key> Window::consumeKeyPresses() {
    std::vector<Key> presses;
    presses.swap(m_keyPresses);
    return presses;
}

bool Window::isKeyDown(Key key) const {
    return glfwGetKey(m_handle, toGlfwKey(key)) == GLFW_PRESS;
}

void Window::recordTypedCharacter(unsigned int codepoint) {
    if (codepoint < 0x20 || codepoint > 0x7E) {
        return;
    }
    if (m_typedText.size() >= kMaxTypedText) {
        return;
    }
    m_typedText.push_back(static_cast<char>(codepoint));
}

std::string Window::consumeTypedText() {
    std::string typed;
    typed.swap(m_typedText);
    return typed;
}

bool Window::isMouseButtonDown(MouseButton button) const {
    return m_mouseButtonDown[static_cast<std::size_t>(button)];
}

void Window::recordMouseButton(MouseButton button, bool down) {
    m_mouseButtonDown[static_cast<std::size_t>(button)] = down;
    if (down) {
        m_mouseButtonPresses.push_back(button);
    }
}

void Window::clearInputState() {
    m_mouseButtonDown.fill(false);
    m_mouseButtonPresses.clear();
    m_keyPresses.clear();
    // A character typed while alt-tabbing away must not arrive on the way back.
    m_typedText.clear();
    m_scrollDelta = 0.0f;
    // Held state only; the pad is re-read next frame anyway. Dropping `live`
    // with it is what stops a button held across the focus change from being
    // reported as a fresh press on the way back.
    m_gamepadLive = false;
    m_gamepadDown.fill(false);
    m_gamepadPressed.fill(false);
    // Focus went elsewhere, so nothing here will be asking for rumble. A motor
    // left running would keep going in somebody else's window.
    setGamepadRumble(0.0f, 0.0f);
}

std::vector<MouseButton> Window::consumeMouseButtonPresses() {
    std::vector<MouseButton> presses;
    presses.swap(m_mouseButtonPresses);
    return presses;
}

void Window::recordCursorPosition(double x, double y) {    // The first sample after capture has no previous position to compare
    // against; using it would produce one enormous jump in view direction.
    if (m_hasLastCursorPosition) {
        m_cursorDelta.x += static_cast<float>(x - m_lastCursorX);
        m_cursorDelta.y += static_cast<float>(y - m_lastCursorY);
    }
    m_lastCursorX = x;
    m_lastCursorY = y;
    m_hasLastCursorPosition = true;
}

CursorDelta Window::consumeCursorDelta() {
    const CursorDelta delta = m_cursorDelta;
    m_cursorDelta = CursorDelta{};
    return delta;
}

float Window::consumeScrollDelta() {
    const float delta = m_scrollDelta;
    m_scrollDelta = 0.0f;
    return delta;
}

void Window::setCursorCaptured(bool captured) {
    if (captured == m_cursorCaptured) {
        return;
    }
    m_cursorCaptured = captured;
    glfwSetInputMode(m_handle, GLFW_CURSOR, captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);

    // Raw motion skips the OS pointer acceleration curve, which is what makes
    // mouse-look feel consistent rather than mushy.
    if (glfwRawMouseMotionSupported() == GLFW_TRUE) {
        glfwSetInputMode(m_handle, GLFW_RAW_MOUSE_MOTION, captured ? GLFW_TRUE : GLFW_FALSE);
    }

    m_hasLastCursorPosition = false;
    m_cursorDelta = CursorDelta{};

    // Releasing the cursor is also the escape hatch for a button that somehow
    // got stuck down.
    if (!captured) {
        m_mouseButtonDown.fill(false);
    }
}

} // namespace engine
