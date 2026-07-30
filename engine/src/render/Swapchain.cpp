#include "engine/render/Swapchain.hpp"

#include "engine/core/Log.hpp"
#include "engine/render/VulkanContext.hpp"
#include "render/VulkanCheck.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {
namespace {

VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& available) {
    for (const VkSurfaceFormatKHR& format : available) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return available.front();
}

VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& available) {
    // MAILBOX draws as fast as it can and shows the newest finished frame, which
    // avoids both tearing and the input lag of strict v-sync. FIFO is plain
    // v-sync and is the only mode Vulkan guarantees exists.
    for (VkPresentModeKHR mode : available) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D desired) {
    // A width of 0xFFFFFFFF means "the surface has no fixed size; you choose".
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    }
    return VkExtent2D{
        std::clamp(desired.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(desired.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };
}

} // namespace

Swapchain::Swapchain(const VulkanContext& context, VkExtent2D desiredExtent) : m_context(context) {
    create(desiredExtent);
}

Swapchain::~Swapchain() {
    destroy();
}

void Swapchain::recreate(VkExtent2D desiredExtent) {
    destroy();
    create(desiredExtent);
}

void Swapchain::create(VkExtent2D desiredExtent) {
    const VkPhysicalDevice physicalDevice = m_context.physicalDevice();
    const VkSurfaceKHR surface = m_context.surface();

    VkSurfaceCapabilitiesKHR capabilities{};
    vkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    std::uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    std::uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(formats);
    const VkPresentModeKHR presentMode = choosePresentMode(presentModes);
    m_imageFormat = surfaceFormat.format;
    m_extent = chooseExtent(capabilities, desiredExtent);

    // One more than the driver's minimum so the CPU is not forced to wait on the
    // GPU to hand an image back before it can start the next frame.
    std::uint32_t requestedImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && requestedImageCount > capabilities.maxImageCount) {
        requestedImageCount = capabilities.maxImageCount;
    }

    // TRANSFER_DST is required because this milestone fills the image with
    // vkCmdClearColorImage rather than drawing into it.
    constexpr VkImageUsageFlags requiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((capabilities.supportedUsageFlags & requiredUsage) != requiredUsage) {
        throw std::runtime_error("Swapchain does not support the required image usage flags");
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = requestedImageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = m_extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = requiredUsage;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    const std::uint32_t graphicsFamily = m_context.graphicsQueueFamily();
    const std::uint32_t presentFamily = m_context.presentQueueFamily();
    const std::uint32_t families[] = {graphicsFamily, presentFamily};

    if (graphicsFamily != presentFamily) {
        // Two different queues touch these images, so ownership is shared.
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = families;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    vkCheck(vkCreateSwapchainKHR(m_context.device(), &createInfo, nullptr, &m_swapchain), "vkCreateSwapchainKHR");

    std::uint32_t actualImageCount = 0;
    vkGetSwapchainImagesKHR(m_context.device(), m_swapchain, &actualImageCount, nullptr);
    m_images.resize(actualImageCount);
    vkGetSwapchainImagesKHR(m_context.device(), m_swapchain, &actualImageCount, m_images.data());

    m_imageViews.resize(actualImageCount);
    for (std::uint32_t i = 0; i < actualImageCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_imageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        vkCheck(vkCreateImageView(m_context.device(), &viewInfo, nullptr, &m_imageViews[i]), "vkCreateImageView");
    }

    logInfo("Swapchain created: " + std::to_string(m_extent.width) + "x" + std::to_string(m_extent.height) + ", " +
            std::to_string(actualImageCount) + " images, " +
            (presentMode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo"));
}

void Swapchain::destroy() {
    for (VkImageView view : m_imageViews) {
        vkDestroyImageView(m_context.device(), view, nullptr);
    }
    m_imageViews.clear();

    // Swapchain images are owned by the swapchain, so they are not destroyed here.
    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_context.device(), m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

} // namespace engine
