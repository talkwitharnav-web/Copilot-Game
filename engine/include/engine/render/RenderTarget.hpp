#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine {

class VulkanContext;

/// An offscreen image the renderer draws into and later samples.
///
/// Distinct from `TextureArray`, which loads pixels from files and never
/// changes. This owns nothing but empty storage, is resized with the window,
/// and carries **one image view per mip level** - a bloom pyramid renders into
/// mip N while sampling mip N-1, and a view spanning the whole chain cannot
/// express either half of that.
class RenderTarget {
public:
    RenderTarget(const VulkanContext& context, VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                 std::uint32_t mipLevels = 1);
    ~RenderTarget();

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;
    RenderTarget(RenderTarget&&) = delete;
    RenderTarget& operator=(RenderTarget&&) = delete;

    VkImage image() const { return m_image; }
    VkFormat format() const { return m_format; }
    VkExtent2D extent() const { return m_extent; }
    std::uint32_t mipLevels() const { return static_cast<std::uint32_t>(m_views.size()); }

    /// A view of exactly one mip, so sampling it reads that level and nothing
    /// else.
    VkImageView view(std::uint32_t level = 0) const { return m_views[level]; }

    VkExtent2D mipExtent(std::uint32_t level) const;

    /// How many mips an image of this size can hold before a side reaches one
    /// texel, capped at `limit`.
    static std::uint32_t mipCountFor(VkExtent2D extent, std::uint32_t limit);

private:
    const VulkanContext& m_context;
    VkExtent2D m_extent{};
    VkFormat m_format = VK_FORMAT_UNDEFINED;
    VkImage m_image = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    std::vector<VkImageView> m_views;
};

} // namespace engine
