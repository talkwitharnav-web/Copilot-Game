#include "engine/platform/Window.hpp"

#include "engine/core/Log.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <string>

namespace engine {
namespace {

void glfwErrorCallback(int code, const char* description) {
    logError("GLFW error " + std::to_string(code) + ": " + (description ? description : "unknown"));
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
        switch (key) {
        case GLFW_KEY_F1:
            self->recordKeyPress(Key::F1);
            break;
        case GLFW_KEY_F2:
            self->recordKeyPress(Key::F2);
            break;
        default:
            break;
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

} // namespace engine
