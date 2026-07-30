#include "engine/render/Renderer.hpp"

#include "engine/core/Log.hpp"
#include "engine/core/Paths.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/PushConstants.hpp"
#include "engine/render/VulkanContext.hpp"
#include "engine/render/Vertex.hpp"
#include "render/VulkanCheck.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>
#include <vector>

namespace engine {
namespace {

// 60 degrees or wider makes anything close to the camera visibly warp at the
// frame edges, the same way an ultrawide phone lens does. 45 reads as natural.
constexpr float kVerticalFovDegrees = 45.0f;
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 500.0f;

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
    : m_context(context), m_window(window), m_swapchain(context, toVkExtent(window.framebufferExtent())),
      m_depthImage(std::make_unique<DepthImage>(context, m_swapchain.extent())),
      m_trianglePipeline(context.device(), executableDirectory() / "shaders" / "triangle.vert.spv",
                         executableDirectory() / "shaders" / "triangle.frag.spv", m_swapchain.imageFormat(),
                         m_depthImage->format()) {
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

void Renderer::uploadInto(GpuMesh& slot, const MeshData& mesh) {
    // Release first: the old buffers are dead weight while the new ones are
    // allocated, and holding both doubles peak device memory.
    slot.vertexBuffer.reset();
    slot.indexBuffer.reset();
    slot.indexCount = 0;

    if (mesh.empty()) {
        return;
    }

    const VkDeviceSize vertexBytes = mesh.vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBytes = mesh.indices.size() * sizeof(std::uint32_t);

    slot.vertexBuffer =
        std::make_unique<Buffer>(m_context, vertexBytes,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    slot.indexBuffer =
        std::make_unique<Buffer>(m_context, indexBytes,
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    uploadBufferData(m_context, m_commandPool, *slot.vertexBuffer, mesh.vertices.data(), vertexBytes);
    uploadBufferData(m_context, m_commandPool, *slot.indexBuffer, mesh.indices.data(), indexBytes);

    slot.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
}

void Renderer::uploadMeshes(const std::vector<MeshData>& meshes) {
    // Replacing buffers the GPU may still be reading from is undefined behaviour.
    // Waiting is free at load time and this is not a per-frame path.
    vkDeviceWaitIdle(m_context.device());

    m_meshes.clear();
    m_meshes.resize(meshes.size());

    for (std::size_t i = 0; i < meshes.size(); ++i) {
        uploadInto(m_meshes[i], meshes[i]);
    }
}

void Renderer::updateMeshes(const std::vector<std::pair<std::size_t, MeshData>>& updates) {
    if (updates.empty()) {
        return;
    }

    // The slots' current buffers may still be referenced by frames in flight.
    // Destroying them without this wait is a use-after-free the validation
    // layers will not always catch.
    vkDeviceWaitIdle(m_context.device());

    for (const auto& [index, mesh] : updates) {
        if (index < m_meshes.size()) {
            uploadInto(m_meshes[index], mesh);
        }
    }
}

void Renderer::setOverlayMesh(const MeshData& mesh) {
    vkDeviceWaitIdle(m_context.device());
    uploadInto(m_overlayMesh, mesh);
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

    // The depth attachment must match the colour attachment's size exactly.
    m_depthImage = std::make_unique<DepthImage>(m_context, m_swapchain.extent());

    // The image count can change, and semaphores are per image.
    destroySyncObjects();
    createSyncObjects();

    m_currentFrame = 0;
}

glm::mat4 Renderer::projectionMatrix() const {
    const VkExtent2D extent = m_swapchain.extent();
    const float width = static_cast<float>(extent.width);
    const float height = static_cast<float>(extent.height > 0 ? extent.height : 1);

    glm::mat4 projection = glm::perspective(glm::radians(kVerticalFovDegrees), width / height, kNearPlane, kFarPlane);

    // GLM builds this for OpenGL, whose Y axis points the opposite way to
    // Vulkan's. Without this flip the whole scene renders upside down, and
    // nothing warns you about it.
    projection[1][1] *= -1.0f;
    return projection;
}

void Renderer::recordCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, const ClearColor& color,
                              const glm::mat4& viewProjection,
                              const std::optional<glm::mat4>& overlayTransform) const {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    const VkImage image = m_swapchain.images()[imageIndex];
    const VkExtent2D extent = m_swapchain.extent();

    // An image's "layout" is how the GPU has it arranged in memory. It has to be
    // moved into a layout that permits the operation we are about to perform, and
    // then into the layout presentation requires. UNDEFINED as the old layout
    // means "I don't care what was in here", which is true: we overwrite it all.
    const VkImageMemoryBarrier toColorAttachment =
        makeColorImageBarrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toColorAttachment);

    VkImageMemoryBarrier toDepthAttachment{};
    toDepthAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDepthAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDepthAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    toDepthAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.image = m_depthImage->handle();
    toDepthAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    toDepthAttachment.subresourceRange.levelCount = 1;
    toDepthAttachment.subresourceRange.layerCount = 1;
    toDepthAttachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toDepthAttachment);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_swapchain.imageViews()[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = VkClearColorValue{{color.r, color.g, color.b, color.a}};

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_depthImage->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Nothing reads depth after the frame, so it need not be written back out.
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // 1.0 is the far plane: every real fragment is nearer than an empty pixel.
    depthAttachment.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    // Supplied per frame rather than baked into the pipeline, so a window resize
    // does not require rebuilding it.
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline.handle());

    const auto drawMesh = [&](const GpuMesh& mesh, const glm::mat4& transform) {
        if (mesh.indexCount == 0) {
            return;
        }
        const MeshPushConstants push{transform};
        vkCmdPushConstants(commandBuffer, m_trianglePipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(MeshPushConstants), &push);

        const VkBuffer vertexBuffers[] = {mesh.vertexBuffer->handle()};
        const VkDeviceSize vertexOffsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    };

    for (const GpuMesh& mesh : m_meshes) {
        drawMesh(mesh, viewProjection);
    }

    if (overlayTransform.has_value()) {
        drawMesh(m_overlayMesh, viewProjection * *overlayTransform);
    }

    vkCmdEndRendering(commandBuffer);

    const VkImageMemoryBarrier toPresent =
        makeColorImageBarrier(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

void Renderer::drawFrame(const ClearColor& color, const glm::mat4& view,
                         const std::optional<glm::mat4>& overlayTransform) {
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
    recordCommands(commandBuffer, imageIndex, color, projectionMatrix() * view, overlayTransform);

    // We now write the image as a colour attachment rather than a transfer
    // target, so the wait happens at the stage that actually does that writing.
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

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
