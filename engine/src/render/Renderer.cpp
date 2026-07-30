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
constexpr float kFarPlane = 200.0f;

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

/// Face order everywhere in this file: front(+Z), back(-Z), left(-X), right(+X),
/// top(+Y), bottom(-Y).
using FaceColors = std::array<glm::vec3, 6>;

/// Appends an axis-aligned box, four vertices per face.
///
/// Sharing corners between faces would force them to share a colour there, which
/// blurs away the very edges that make a box look like a box. Per-face vertices
/// also match what voxel meshing will need, since normals and texture
/// coordinates differ per face at a shared corner too.
void appendBox(MeshData& mesh, const glm::vec3& center, const glm::vec3& halfExtents, const FaceColors& faceColors) {
    const glm::vec3 c = center;
    const glm::vec3 h = halfExtents;

    const glm::vec3 corners[8] = {
        {c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z},
        {c.x + h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y + h.y, c.z + h.z},
        {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z},
        {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z},
    };

    // Each row is one face's four corners, counter-clockwise seen from outside.
    // Reverse a row and that face disappears once backface culling is on.
    const int faceCorners[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0},
    };

    for (int face = 0; face < 6; ++face) {
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const glm::vec3& color = faceColors[static_cast<std::size_t>(face)];

        for (int corner = 0; corner < 4; ++corner) {
            const glm::vec3& p = corners[faceCorners[face][corner]];
            mesh.vertices.push_back(Vertex{{p.x, p.y, p.z}, {color.r, color.g, color.b}});
        }

        mesh.indices.insert(mesh.indices.end(),
                            {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3});
    }
}

/// One colour per face, brighter on top and darker underneath, so the shape of a
/// box is readable without any actual lighting yet.
FaceColors shadedFaces(const glm::vec3& base) {
    return FaceColors{
        base * 0.80f, base * 0.65f, base * 0.72f, base * 0.72f, base * 1.00f, base * 0.45f,
    };
}

MeshData buildScene() {
    MeshData mesh;

    // A wide, thin box as the ground, so there is a fixed reference to judge the
    // camera against.
    appendBox(mesh, {0.0f, -0.55f, 0.0f}, {14.0f, 0.25f, 14.0f}, shadedFaces({0.42f, 0.47f, 0.40f}));

    // Boxes at different heights and depths. If the depth test is broken, these
    // will visibly punch through each other as the camera moves.
    appendBox(mesh, {0.0f, 0.5f, 0.0f}, {0.5f, 0.5f, 0.5f}, shadedFaces({0.30f, 0.62f, 0.92f}));
    appendBox(mesh, {2.4f, 1.0f, -1.8f}, {1.0f, 1.0f, 1.0f}, shadedFaces({0.90f, 0.42f, 0.36f}));
    appendBox(mesh, {-2.6f, 0.2f, 1.4f}, {0.7f, 0.7f, 0.7f}, shadedFaces({0.45f, 0.80f, 0.45f}));
    appendBox(mesh, {-1.8f, 1.6f, -2.8f}, {0.6f, 1.6f, 0.6f}, shadedFaces({0.86f, 0.74f, 0.34f}));
    appendBox(mesh, {3.6f, 0.3f, 2.6f}, {0.8f, 0.3f, 0.8f}, shadedFaces({0.72f, 0.52f, 0.86f}));

    return mesh;
}

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
    createGeometry();
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

void Renderer::createGeometry() {
    const MeshData mesh = buildScene();
    m_indexCount = static_cast<std::uint32_t>(mesh.indices.size());

    const VkDeviceSize vertexBytes = mesh.vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBytes = mesh.indices.size() * sizeof(std::uint32_t);

    m_vertexBuffer = std::make_unique<Buffer>(m_context, vertexBytes,
                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    m_indexBuffer = std::make_unique<Buffer>(m_context, indexBytes,
                                             VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    uploadBufferData(m_context, m_commandPool, *m_vertexBuffer, mesh.vertices.data(), vertexBytes);
    uploadBufferData(m_context, m_commandPool, *m_indexBuffer, mesh.indices.data(), indexBytes);

    logInfo("Scene uploaded: " + std::to_string(mesh.vertices.size()) + " vertices, " +
            std::to_string(mesh.indices.size() / 3) + " triangles");
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
                              const glm::mat4& modelViewProjection) const {
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

    const MeshPushConstants push{modelViewProjection};
    vkCmdPushConstants(commandBuffer, m_trianglePipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(MeshPushConstants), &push);

    const VkBuffer vertexBuffers[] = {m_vertexBuffer->handle()};
    const VkDeviceSize vertexOffsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
    vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);

    vkCmdDrawIndexed(commandBuffer, m_indexCount, 1, 0, 0, 0);

    vkCmdEndRendering(commandBuffer);

    const VkImageMemoryBarrier toPresent =
        makeColorImageBarrier(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

void Renderer::drawFrame(const ClearColor& color, const glm::mat4& view) {
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
    recordCommands(commandBuffer, imageIndex, color, projectionMatrix() * view);

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
