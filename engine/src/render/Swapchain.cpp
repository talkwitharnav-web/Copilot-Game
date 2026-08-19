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

    // **Any sRGB format beats the driver's first offer.** Every shader and the
    // tone mapper assume the hardware does the sRGB encode on write - the
    // comment at the top of `TextureArray.cpp` says so outright - so falling
    // straight through to `front()` can hand back a `UNORM` format and the
    // whole image comes out washed out or crushed, with nothing anywhere to
    // explain it. A second pass costs one loop and removes that silence.
    for (const VkSurfaceFormatKHR& format : available) {
        switch (format.format) {
        case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
            return format;
        default:
            break;
        }
    }

    logWarn("No sRGB swapchain format offered; colours will be encoded wrongly");
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
    // See `DepthImage`: a constructor that throws gets no destructor. This was
    // the one class in the set without the guard, and it is the one where a
    // leak does more than leak - `create` takes ownership of the
    // `VkSwapchainKHR` at `vkCreateSwapchainKHR` and can still throw at any of
    // the image views after it, and an abandoned swapchain stays bound to the
    // surface, so a later attempt to recover gets `VK_ERROR_NATIVE_WINDOW_IN_
    // USE_KHR` rather than a clean retry.
    //
    // `destroy()` is safe to run against a half-built object: it tolerates the
    // `VK_NULL_HANDLE` entries `m_imageViews.resize` leaves behind, which is
    // also why `recreate` can call it unconditionally.
    try {
        create(desiredExtent);
    } catch (...) {
        destroy();
        throw;
    }
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

    // **A minimised window reports a `currentExtent` of 0x0, and `chooseExtent`
    // returns it verbatim.** `drawFrame` returns early while minimised, so this
    // is normally unreachable - but the guard lives in the caller, and
    // minimising between that check and an `OUT_OF_DATE` acquire runs
    // `recreateSwapchain` against a zero-sized surface. Named here so a future
    // regression fails on one clear message rather than a wall of validation
    // errors - **but it throws, and the session-wide catch in `main` turns that
    // into an exit rather than a log line**. `Renderer::recreateSwapchain`
    // returns early on a zero extent precisely so this stays unreachable.
    if (m_extent.width == 0 || m_extent.height == 0) {
        throw std::runtime_error("Cannot create a swapchain for a zero-sized window");
    }

    // One more than the driver's minimum so the CPU is not forced to wait on the
    // GPU to hand an image back before it can start the next frame.
    std::uint32_t requestedImageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && requestedImageCount > capabilities.maxImageCount) {
        requestedImageCount = capabilities.maxImageCount;
    }

    // **`COLOR_ATTACHMENT` is the only usage the spec guarantees a surface
    // supports, and it is the only one used.** This asked for `TRANSFER_DST`
    // as well, left over from the milestone that filled the image with
    // `vkCmdClearColorImage` - there is no such call anywhere now, and the one
    // `vkCmdCopyImage` copies the scene into its own copy, never the swapchain.
    // Demanding it could refuse to start the game on a surface where nothing is
    // actually wrong, with an error naming the swapchain rather than this
    // stale constant. The same value is handed to `imageUsage` below, so a
    // usage the game does not need is no longer requested either.
    constexpr VkImageUsageFlags requiredUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
