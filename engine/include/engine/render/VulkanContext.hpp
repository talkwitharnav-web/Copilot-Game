#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine {

class Window;

/// One-time Vulkan setup that everything else depends on.
///
/// Vocabulary, in order of creation:
///   instance        - the connection between this program and the Vulkan library
///   surface         - the bridge between Vulkan and the OS window
///   physical device - a GPU that physically exists on this machine
///   logical device  - our own handle onto that GPU, with the features we asked for
///   queue           - a lane for submitting work to the GPU
class VulkanContext {
public:
    explicit VulkanContext(const Window& window);
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    /// The handles this context owns, exposed for the renderer to build on.
    ///
    /// **`instance()` has no caller in this tree, and never has had one** -
    /// checked 2026-08-19 over all 141 C++ files in `engine/` and `game/`, and
    /// again against `HEAD`, where the only hit is this line. That is a note,
    /// not a defect, and it is deliberately still here for two reasons: the
    /// members either side of it are read constantly (`device()` at 114 call
    /// sites, `physicalDevice()` at 12), so this is one member of a symmetric
    /// accessor family rather than a stray, and `VkInstance` is what every
    /// instance-level entry point needs - `vkEnumeratePhysicalDevices`, any
    /// `vkGetInstanceProcAddr` load of an extension function, and a second
    /// surface. `m_instance` itself is load-bearing regardless; only this
    /// read-back is idle.
    ///
    /// What would make the claim above false: any caller appearing, which is
    /// most likely the first time an extension is loaded by hand.
    VkInstance instance() const { return m_instance; }
    VkSurfaceKHR surface() const { return m_surface; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }

    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkQueue presentQueue() const { return m_presentQueue; }

    std::uint32_t graphicsQueueFamily() const { return m_graphicsQueueFamily; }
    std::uint32_t presentQueueFamily() const { return m_presentQueueFamily; }

private:
    void createInstance();
    void createDebugMessenger();
    void createSurface(const Window& window);
    void selectPhysicalDevice();
    void createLogicalDevice();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    std::uint32_t m_graphicsQueueFamily = 0;
    std::uint32_t m_presentQueueFamily = 0;

    bool m_validationEnabled = false;
};

} // namespace engine
