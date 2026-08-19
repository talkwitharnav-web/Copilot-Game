#include "engine/render/RenderTarget.hpp"

#include "engine/render/VulkanContext.hpp"
#include "render/GpuMemory.hpp"
#include "render/VulkanCheck.hpp"

#include <algorithm>
#include <stdexcept>

namespace engine {

std::uint32_t RenderTarget::mipCountFor(VkExtent2D extent, std::uint32_t limit) {
    std::uint32_t levels = 1;
    std::uint32_t side = std::min(extent.width, extent.height);
    while (side > 1 && levels < limit) {
        side /= 2;
        ++levels;
    }
    return levels;
}

RenderTarget::RenderTarget(const VulkanContext& context, VkExtent2D extent, VkFormat format,
                          VkImageUsageFlags usage, std::uint32_t mipLevels)
    : m_context(context), m_extent{std::max(1u, extent.width), std::max(1u, extent.height)}, m_format(format) {
    // See `DepthImage`: a constructor that throws gets no destructor, and there
    // are seven of these rebuilt on every resize and render-scale change.
    try {
        createResources(usage, mipLevels);
    } catch (...) {
        destroy();
        throw;
    }
}

void RenderTarget::createResources(VkImageUsageFlags usage, std::uint32_t mipLevels) {
    const VulkanContext& context = m_context;
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_format;
    imageInfo.extent = VkExtent3D{m_extent.width, m_extent.height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(context.device(), &imageInfo, nullptr, &m_image), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device(), m_image, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(context.physicalDevice(), requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (allocateDeviceMemory(context.device(), allocInfo, &m_memory) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateMemory failed for a render target");
    }
    vkCheck(vkBindImageMemory(context.device(), m_image, m_memory, 0), "vkBindImageMemory");

    m_views.resize(mipLevels, VK_NULL_HANDLE);
    for (std::uint32_t level = 0; level < mipLevels; ++level) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = level;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        vkCheck(vkCreateImageView(context.device(), &viewInfo, nullptr, &m_views[level]), "vkCreateImageView");
    }
}

RenderTarget::~RenderTarget() { destroy(); }

void RenderTarget::destroy() noexcept {
    for (VkImageView view : m_views) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_context.device(), view, nullptr);
        }
    }
    m_views.clear();
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_context.device(), m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    freeDeviceMemory(m_context.device(), m_memory);
    m_memory = VK_NULL_HANDLE;
}

VkExtent2D RenderTarget::mipExtent(std::uint32_t level) const {
    return VkExtent2D{std::max(1u, m_extent.width >> level), std::max(1u, m_extent.height >> level)};
}

} // namespace engine
