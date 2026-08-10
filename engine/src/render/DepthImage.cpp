#include "engine/render/DepthImage.hpp"

#include "engine/render/VulkanContext.hpp"
#include "render/GpuMemory.hpp"
#include "render/VulkanCheck.hpp"

#include <stdexcept>

namespace engine {

VkFormat DepthImage::chooseFormat(VkPhysicalDevice physicalDevice) {
    // D32 first: a full float of precision and no stencil we do not use.
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT};

    for (VkFormat candidate : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, candidate, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return candidate;
        }
    }
    throw std::runtime_error("No supported depth attachment format");
}

DepthImage::DepthImage(const VulkanContext& context, VkExtent2D extent) : m_context(context) {
    m_format = chooseFormat(context.physicalDevice());

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_format;
    imageInfo.extent = VkExtent3D{extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // SAMPLED, because the deferred lighting pass reads depth back to recover
    // where each pixel is in the world - and M24's shadows will want it too.
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
        vkDestroyImage(context.device(), m_image, nullptr);
        m_image = VK_NULL_HANDLE;
        throw std::runtime_error("vkAllocateMemory failed for depth image");
    }

    vkCheck(vkBindImageMemory(context.device(), m_image, m_memory, 0), "vkBindImageMemory");

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context.device(), &viewInfo, nullptr, &m_view) != VK_SUCCESS) {
        vkDestroyImage(context.device(), m_image, nullptr);
        freeDeviceMemory(context.device(), m_memory);
        m_image = VK_NULL_HANDLE;
        m_memory = VK_NULL_HANDLE;
        throw std::runtime_error("vkCreateImageView failed for depth image");
    }
}

DepthImage::~DepthImage() {
    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_context.device(), m_view, nullptr);
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_context.device(), m_image, nullptr);
    }
    freeDeviceMemory(m_context.device(), m_memory);
}

} // namespace engine
