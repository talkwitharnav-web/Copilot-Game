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
