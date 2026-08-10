#include "engine/render/ShadowMap.hpp"

#include "engine/render/VulkanContext.hpp"
#include "render/GpuMemory.hpp"
#include "render/VulkanCheck.hpp"

#include <algorithm>
#include <stdexcept>

namespace engine {

VkFormat ShadowMap::chooseFormat(VkPhysicalDevice physicalDevice) {
    // No stencil: a shadow map is sampled, and a combined depth-stencil image
    // cannot be read through an ordinary depth sampler without a second view.
    const VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM};
    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
        constexpr VkFormatFeatureFlags kNeeded =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & kNeeded) == kNeeded) {
            return format;
        }
    }
    throw std::runtime_error("No depth format supports being both rendered to and sampled");
}

ShadowMap::ShadowMap(const VulkanContext& context, std::uint32_t resolution, std::uint32_t cascades)
    : m_context(context), m_resolution(std::max(1u, resolution)) {
    cascades = std::clamp(cascades, 1u, kMaxCascades);
    m_format = chooseFormat(context.physicalDevice());

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = m_format;
    imageInfo.extent = VkExtent3D{m_resolution, m_resolution, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = cascades;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
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
        throw std::runtime_error("vkAllocateMemory failed for the shadow map");
    }
    vkCheck(vkBindImageMemory(context.device(), m_image, m_memory, 0), "vkBindImageMemory");

    m_cascadeViews.resize(cascades, VK_NULL_HANDLE);
    for (std::uint32_t cascade = 0; cascade < cascades; ++cascade) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = cascade;
        viewInfo.subresourceRange.layerCount = 1;
        vkCheck(vkCreateImageView(context.device(), &viewInfo, nullptr, &m_cascadeViews[cascade]),
                "vkCreateImageView");
    }

    VkImageViewCreateInfo arrayInfo{};
    arrayInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    arrayInfo.image = m_image;
    arrayInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    arrayInfo.format = m_format;
    arrayInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    arrayInfo.subresourceRange.levelCount = 1;
    arrayInfo.subresourceRange.layerCount = cascades;
    vkCheck(vkCreateImageView(context.device(), &arrayInfo, nullptr, &m_arrayView), "vkCreateImageView");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    // White border, so anything sampled outside a cascade reads as "the sun saw
    // nothing at all here" - the far plane - and is lit rather than black.
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_TRUE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    samplerInfo.maxLod = 0.0f;
    vkCheck(vkCreateSampler(context.device(), &samplerInfo, nullptr, &m_sampler), "vkCreateSampler");
}

ShadowMap::~ShadowMap() {
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_context.device(), m_sampler, nullptr);
    }
    if (m_arrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_context.device(), m_arrayView, nullptr);
    }
    for (VkImageView view : m_cascadeViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(m_context.device(), view, nullptr);
        }
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_context.device(), m_image, nullptr);
    }
    freeDeviceMemory(m_context.device(), m_memory);
}

} // namespace engine
