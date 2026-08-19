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
        // **Both bits, the same pair `ShadowMap::chooseFormat` asks for.** This
        // image is created `SAMPLED` a few lines below and the deferred lighting
        // pass does sample it, so a format that can only be rendered to is no
        // use here - and asking for the attachment bit alone would have accepted
        // one. Latent on this machine, where `D32_SFLOAT` reports both, which is
        // exactly why it needed writing down rather than leaving to luck.
        constexpr VkFormatFeatureFlags kNeeded =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((properties.optimalTilingFeatures & kNeeded) == kNeeded) {
            return candidate;
        }
    }
    throw std::runtime_error("No depth format supports being both rendered to and sampled");
}

DepthImage::DepthImage(const VulkanContext& context, VkExtent2D extent) : m_context(context) {
    // **C++ does not run a destructor for an object whose constructor threw**,
    // so every handle taken before the throw leaks - and this class is rebuilt
    // on every window resize and every render-scale change, so the leak repeats
    // within a session rather than being reclaimed at exit. Routing the failure
    // path through the same `destroy()` the destructor uses means there is one
    // place that knows what has to be released, instead of two that can drift.
    try {
        createResources(extent);
    } catch (...) {
        destroy();
        throw;
    }
}

void DepthImage::createResources(VkExtent2D extent) {
    const VulkanContext& context = m_context;
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

    vkCheck(vkCreateImageView(context.device(), &viewInfo, nullptr, &m_view), "vkCreateImageView");
}

DepthImage::~DepthImage() { destroy(); }

void DepthImage::destroy() noexcept {
    if (m_view != VK_NULL_HANDLE) {
        vkDestroyImageView(m_context.device(), m_view, nullptr);
        m_view = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
        vkDestroyImage(m_context.device(), m_image, nullptr);
        m_image = VK_NULL_HANDLE;
    }
    freeDeviceMemory(m_context.device(), m_memory);
    m_memory = VK_NULL_HANDLE;
}

} // namespace engine
