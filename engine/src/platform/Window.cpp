#include "engine/platform/Window.hpp"

#include "engine/core/Log.hpp"

#include <GLFW/glfw3.h>

#include <cstddef>
#include <stdexcept>
#include <string>

namespace engine {
namespace {

void glfwErrorCallback(int code, const char* description) {
    logError("GLFW error " + std::to_string(code) + ": " + (description ? description : "unknown"));
}

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
    case Key::F:
        return GLFW_KEY_F;
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
    case Key::F1:
        return GLFW_KEY_F1;
    case Key::F2:
        return GLFW_KEY_F2;
    case Key::F3:
        return GLFW_KEY_F3;
    case Key::F4:
        return GLFW_KEY_F4;
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
    case GLFW_KEY_F:
        out = Key::F;
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
    if (m_handle != nullptr) {
        glfwDestroyWindow(m_handle);
    }
    glfwTerminate();
}

void Window::pollEvents() {
    glfwPollEvents();
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
}

std::vector<MouseButton> Window::consumeMouseButtonPresses() {
    std::vector<MouseButton> presses;
    presses.swap(m_mouseButtonPresses);
    return presses;
}

void Window::recordCursorPosition(double x, double y) {
    // The first sample after capture has no previous position to compare
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
