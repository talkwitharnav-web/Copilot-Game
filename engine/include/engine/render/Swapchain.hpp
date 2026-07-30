#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine {

class VulkanContext;

/// The small ring of images the GPU draws into and the OS displays.
///
/// Drawing and displaying never touch the same image at the same time: while
/// one is on screen, the next is being written. The set has to be rebuilt
/// whenever the window changes size.
class Swapchain {
public:
    Swapchain(const VulkanContext& context, VkExtent2D desiredExtent);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    Swapchain(Swapchain&&) = delete;
    Swapchain& operator=(Swapchain&&) = delete;

    /// Tears down and rebuilds for a new window size. The caller must ensure the
    /// GPU is idle first.
    void recreate(VkExtent2D desiredExtent);

    VkSwapchainKHR handle() const { return m_swapchain; }
    VkFormat imageFormat() const { return m_imageFormat; }
    VkExtent2D extent() const { return m_extent; }
    std::uint32_t imageCount() const { return static_cast<std::uint32_t>(m_images.size()); }

    const std::vector<VkImage>& images() const { return m_images; }
    const std::vector<VkImageView>& imageViews() const { return m_imageViews; }

private:
    void create(VkExtent2D desiredExtent);
    void destroy();

    const VulkanContext& m_context;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
};

} // namespace engine
