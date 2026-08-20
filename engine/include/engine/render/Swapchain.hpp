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

    /// Whether these images may be the *source* of a copy - which is what
    /// reading the finished picture back to the CPU needs.
    ///
    /// **A usage bit is decided when an image is created and can never be added
    /// afterwards**, so this is not a question about the copy command: it is
    /// whether `create` was allowed to ask for `TRANSFER_SRC` at all. The spec
    /// guarantees a surface supports `COLOR_ATTACHMENT` and nothing else, so
    /// the request can be refused, and a refusal must not stop the game
    /// starting. Callers that read pixels back check this first and do nothing
    /// when it is false.
    bool supportsTransferSrc() const { return m_supportsTransferSrc; }

    const std::vector<VkImage>& images() const { return m_images; }
    const std::vector<VkImageView>& imageViews() const { return m_imageViews; }

private:
    void create(VkExtent2D desiredExtent);
    void destroy();

    const VulkanContext& m_context;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D m_extent{};

    /// Recomputed by every `create`, because a surface's capabilities are
    /// re-queried there and a display change can move them.
    bool m_supportsTransferSrc = false;
    /// So the refusal is reported once rather than once per rebuild. Resizing a
    /// window by dragging its edge runs `recreate` many times a second, and a
    /// warning repeated at that rate buries everything else in the log.
    bool m_transferSrcWarningIssued = false;

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
};

} // namespace engine
