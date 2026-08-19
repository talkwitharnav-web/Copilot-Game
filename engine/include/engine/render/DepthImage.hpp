#pragma once

#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;

/// The depth attachment: one value per pixel recording how far away whatever is
/// drawn there currently is.
///
/// Without it, whichever triangle is drawn last wins, so the far side of an
/// object can paint over the near side. Backface culling alone is enough for a
/// single convex object; the moment there are two objects, this is required.
///
/// Must match the render target's size, so it is rebuilt with the swapchain.
class DepthImage {
public:
    DepthImage(const VulkanContext& context, VkExtent2D extent);
    ~DepthImage();

    DepthImage(const DepthImage&) = delete;
    DepthImage& operator=(const DepthImage&) = delete;
    DepthImage(DepthImage&&) = delete;
    DepthImage& operator=(DepthImage&&) = delete;

    VkImage handle() const { return m_image; }
    VkImageView view() const { return m_view; }
    VkFormat format() const { return m_format; }

    /// Picks the best depth format this GPU actually supports.
    static VkFormat chooseFormat(VkPhysicalDevice physicalDevice);

private:
    /// Everything the constructor takes, so a throw part-way through can be
    /// released by the one function that also serves the destructor.
    void createResources(VkExtent2D extent);
    void destroy() noexcept;

    const VulkanContext& m_context;
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
};

} // namespace engine
