#include "engine/render/Renderer.hpp"

#include "engine/core/Log.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/VulkanContext.hpp"
#include "render/VulkanCheck.hpp"

namespace engine {
namespace {

VkExtent2D toVkExtent(Extent2D extent) {
    return VkExtent2D{extent.width, extent.height};
}

VkImageMemoryBarrier makeColorImageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                           VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    return barrier;
}

} // namespace

Renderer::Renderer(const VulkanContext& context, Window& window)
    : m_context(context), m_window(window), m_swapchain(context, toVkExtent(window.framebufferExtent())) {
    createCommandResources();
    createSyncObjects();
}

Renderer::~Renderer() {
    // The GPU may still be reading resources we are about to free.
    vkDeviceWaitIdle(m_context.device());

    destroySyncObjects();

    if (m_commandPool != VK_NULL_HANDLE) {
        // Destroying the pool frees every command buffer allocated from it.
        vkDestroyCommandPool(m_context.device(), m_commandPool, nullptr);
    }
}

void Renderer::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_context.graphicsQueueFamily();
    vkCheck(vkCreateCommandPool(m_context.device(), &poolInfo, nullptr, &m_commandPool), "vkCreateCommandPool");

    m_commandBuffers.resize(kFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;
    vkCheck(vkAllocateCommandBuffers(m_context.device(), &allocInfo, m_commandBuffers.data()),
            "vkAllocateCommandBuffers");
}

void Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Created already-signalled so the very first frame does not wait forever.
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    m_imageAvailable.resize(kFramesInFlight);
    m_frameInFlight.resize(kFramesInFlight);
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        vkCheck(vkCreateSemaphore(m_context.device(), &semaphoreInfo, nullptr, &m_imageAvailable[i]),
                "vkCreateSemaphore");
        vkCheck(vkCreateFence(m_context.device(), &fenceInfo, nullptr, &m_frameInFlight[i]), "vkCreateFence");
    }

    m_renderFinished.resize(m_swapchain.imageCount());
    for (VkSemaphore& semaphore : m_renderFinished) {
        vkCheck(vkCreateSemaphore(m_context.device(), &semaphoreInfo, nullptr, &semaphore), "vkCreateSemaphore");
    }
}

void Renderer::destroySyncObjects() {
    for (VkSemaphore semaphore : m_imageAvailable) {
        vkDestroySemaphore(m_context.device(), semaphore, nullptr);
    }
    m_imageAvailable.clear();

    for (VkSemaphore semaphore : m_renderFinished) {
        vkDestroySemaphore(m_context.device(), semaphore, nullptr);
    }
    m_renderFinished.clear();

    for (VkFence fence : m_frameInFlight) {
        vkDestroyFence(m_context.device(), fence, nullptr);
    }
    m_frameInFlight.clear();
}

void Renderer::recreateSwapchain() {
    vkDeviceWaitIdle(m_context.device());

    m_swapchain.recreate(toVkExtent(m_window.framebufferExtent()));

    // The image count can change, and semaphores are per image.
    destroySyncObjects();
    createSyncObjects();

    m_currentFrame = 0;
}

void Renderer::recordClearCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex,
                                   const ClearColor& color) const {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    const VkImage image = m_swapchain.images()[imageIndex];

    // An image's "layout" is how the GPU has it arranged in memory. It has to be
    // moved into a layout that permits the operation we are about to perform, and
    // then into the layout presentation requires. UNDEFINED as the old layout
    // means "I don't care what was in here", which is true: we overwrite it all.
    const VkImageMemoryBarrier toTransferDst = makeColorImageBarrier(
        image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toTransferDst);

    VkClearColorValue clearValue{};
    clearValue.float32[0] = color.r;
    clearValue.float32[1] = color.g;
    clearValue.float32[2] = color.b;
    clearValue.float32[3] = color.a;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    vkCmdClearColorImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &range);

    const VkImageMemoryBarrier toPresent =
        makeColorImageBarrier(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toPresent);

    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

void Renderer::drawFrame(const ClearColor& color) {
    if (m_window.isMinimized()) {
        return;
    }

    const VkDevice device = m_context.device();

    vkCheck(vkWaitForFences(device, 1, &m_frameInFlight[m_currentFrame], VK_TRUE, UINT64_MAX), "vkWaitForFences");

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(device, m_swapchain.handle(), UINT64_MAX,
                                                         m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquireResult, "vkAcquireNextImageKHR");
    }

    // Reset only once we know we are definitely submitting work this frame,
    // otherwise an early return above would leave the fence unsignalled forever.
    vkCheck(vkResetFences(device, 1, &m_frameInFlight[m_currentFrame]), "vkResetFences");

    const VkCommandBuffer commandBuffer = m_commandBuffers[m_currentFrame];
    vkCheck(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
    recordClearCommands(commandBuffer, imageIndex, color);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailable[m_currentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinished[imageIndex];

    vkCheck(vkQueueSubmit(m_context.graphicsQueue(), 1, &submitInfo, m_frameInFlight[m_currentFrame]),
            "vkQueueSubmit");

    const VkSwapchainKHR swapchain = m_swapchain.handle();

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinished[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(m_context.presentQueue(), &presentInfo);
    const bool windowResized = m_window.consumeResizedFlag();

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || windowResized) {
        recreateSwapchain();
        return;
    }
    vkCheck(presentResult, "vkQueuePresentKHR");

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}

} // namespace engine
