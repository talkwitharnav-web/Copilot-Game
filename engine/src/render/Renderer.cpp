#include "engine/render/Renderer.hpp"

#include "engine/core/Log.hpp"
#include "engine/core/Paths.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/PushConstants.hpp"
#include "engine/render/VulkanContext.hpp"
#include "engine/render/Vertex.hpp"
#include "render/VulkanCheck.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <vector>

namespace engine {
namespace {

// 60 degrees or wider makes anything close to the camera visibly warp at the
// frame edges, the same way an ultrawide phone lens does. 45 reads as natural.
constexpr float kNearPlane = 0.1f;

VkExtent2D toVkExtent(Extent2D extent) {
    return VkExtent2D{extent.width, extent.height};
}

/// The six clip-space planes of a view-projection matrix, each as (normal, d)
/// with the normal pointing inward.
///
/// Gribb-Hartmann: adding or subtracting a row of the matrix from the w row
/// gives a plane directly, because clip-space testing is exactly those
/// comparisons. GLM is column-major, so a "row" here reads across columns. The
/// near plane is row 2 alone rather than w+2 because the project builds with
/// `GLM_FORCE_DEPTH_ZERO_TO_ONE`, which is what Vulkan expects.
std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& viewProjection) {
    const glm::mat4& m = viewProjection;
    const glm::vec4 rowX{m[0][0], m[1][0], m[2][0], m[3][0]};
    const glm::vec4 rowY{m[0][1], m[1][1], m[2][1], m[3][1]};
    const glm::vec4 rowZ{m[0][2], m[1][2], m[2][2], m[3][2]};
    const glm::vec4 rowW{m[0][3], m[1][3], m[2][3], m[3][3]};

    std::array<glm::vec4, 6> planes{rowW + rowX, rowW - rowX, rowW + rowY, rowW - rowY, rowZ, rowW - rowZ};

    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3{plane});
        if (length > 0.0f) {
            plane /= length;
        }
    }
    return planes;
}

/// Conservative: false only when the box is provably outside. Tests the box
/// corner furthest along each plane normal, so a box straddling a plane counts
/// as visible.
bool boxInFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& min, const glm::vec3& max) {
    for (const glm::vec4& plane : planes) {
        const glm::vec3 nearestInside{plane.x >= 0.0f ? max.x : min.x, plane.y >= 0.0f ? max.y : min.y,
                                      plane.z >= 0.0f ? max.z : min.z};
        if (glm::dot(glm::vec3{plane}, nearestInside) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
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

Renderer::Renderer(const VulkanContext& context, Window& window,
                   const std::vector<std::filesystem::path>& blockTextures,
                   const std::filesystem::path& hudTexture, const std::filesystem::path& fontTexture)
    : m_context(context), m_window(window), m_swapchain(context, toVkExtent(window.framebufferExtent())),
      m_depthImage(std::make_unique<DepthImage>(context, m_swapchain.extent())) {
    createCommandResources();

    // Order matters: the texture upload needs the command pool, and the pipeline
    // needs the descriptor set layout that describes it.
    m_blockTextures = std::make_unique<TextureArray>(context, m_commandPool, blockTextures);
    m_hudTexture = std::make_unique<TextureArray>(context, m_commandPool, std::vector{hudTexture});
    m_fontTexture = std::make_unique<TextureArray>(context, m_commandPool, std::vector{fontTexture});
    createDescriptorResources();

    // 16 MB holds a good many chunk meshes at once. Bigger only raises the
    // memory floor; smaller just means more submissions.
    m_uploads = std::make_unique<UploadContext>(context, 16 * 1024 * 1024);

    m_trianglePipeline = std::make_unique<GraphicsPipeline>(
        context.device(), executableDirectory() / "shaders" / "triangle.vert.spv",
        executableDirectory() / "shaders" / "triangle.frag.spv", m_swapchain.imageFormat(), m_depthImage->format(),
        m_descriptorSetLayout);

    createSyncObjects();
    createTimestampPool();
}

void Renderer::createTimestampPool() {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_context.physicalDevice(), &properties);

    // A period of zero means the device does not support timestamps at all.
    m_timestampPeriodNanoseconds = properties.limits.timestampPeriod;
    m_timestampsSupported = m_timestampPeriodNanoseconds > 0.0f;
    if (!m_timestampsSupported) {
        logWarn("GPU timestamps unsupported; GPU frame time will read as zero.");
        return;
    }

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = kFramesInFlight * 2;
    vkCheck(vkCreateQueryPool(m_context.device(), &poolInfo, nullptr, &m_timestampPool), "vkCreateQueryPool");

    m_timestampsPending.assign(kFramesInFlight, false);
}

void Renderer::readGpuTimestamps() {
    if (!m_timestampsSupported || !m_timestampsPending[m_currentFrame]) {
        return;
    }

    // Only safe because the caller has just waited on this frame's fence, so the
    // submission that wrote these timestamps has finished.
    std::uint64_t results[2]{};
    const VkResult result =
        vkGetQueryPoolResults(m_context.device(), m_timestampPool, m_currentFrame * 2, 2, sizeof(results), results,
                              sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);

    if (result == VK_SUCCESS && results[1] > results[0]) {
        const double ticks = static_cast<double>(results[1] - results[0]);
        m_stats.gpuMilliseconds = static_cast<float>(ticks * m_timestampPeriodNanoseconds / 1.0e6);
    }
}

Renderer::~Renderer() {
    // Copies may still be queued against buffers that are about to be freed.
    m_uploads->waitForCompletion();

    // The GPU may still be reading resources we are about to free.
    vkDeviceWaitIdle(m_context.device());

    // Explicit rather than left to member destruction order: everything here
    // owns Vulkan handles that must die while the device is still alive, and
    // the retired list in particular is easy to forget.
    m_retired.clear();
    m_meshes.clear();
    m_freeSlots.clear();
    m_overlayMesh = GpuMesh{};
    m_screenMesh = GpuMesh{};
    m_skyMesh = GpuMesh{};

    // Before the pool: freeing the pool invalidates the set allocated from it.
    m_trianglePipeline.reset();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_context.device(), m_descriptorPool, nullptr);
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_context.device(), m_descriptorSetLayout, nullptr);
    }
    m_blockTextures.reset();
    m_hudTexture.reset();
    m_fontTexture.reset();
    m_uploads.reset();

    destroySyncObjects();

    if (m_timestampPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_context.device(), m_timestampPool, nullptr);
    }

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

void Renderer::createDescriptorResources() {
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (std::uint32_t index = 0; index < bindings.size(); ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCheck(vkCreateDescriptorSetLayout(m_context.device(), &layoutInfo, nullptr, &m_descriptorSetLayout),
            "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = static_cast<std::uint32_t>(bindings.size());

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    vkCheck(vkCreateDescriptorPool(m_context.device(), &poolInfo, nullptr, &m_descriptorPool),
            "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;
    vkCheck(vkAllocateDescriptorSets(m_context.device(), &allocInfo, &m_descriptorSet), "vkAllocateDescriptorSets");

    // Neither texture ever changes, so the set is written once here rather than
    // per frame.
    std::array<VkDescriptorImageInfo, 3> images{};
    images[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    images[0].imageView = m_blockTextures->view();
    images[0].sampler = m_blockTextures->sampler();
    images[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    images[1].imageView = m_hudTexture->view();
    images[1].sampler = m_hudTexture->sampler();
    images[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    images[2].imageView = m_fontTexture->view();
    images[2].sampler = m_fontTexture->sampler();

    std::array<VkWriteDescriptorSet, 3> writes{};
    for (std::uint32_t index = 0; index < writes.size(); ++index) {
        writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[index].dstSet = m_descriptorSet;
        writes[index].dstBinding = index;
        writes[index].descriptorCount = 1;
        writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[index].pImageInfo = &images[index];
    }

    vkUpdateDescriptorSets(m_context.device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void Renderer::retire(GpuMesh& slot) {
    if (slot.vertexBuffer != nullptr || slot.indexBuffer != nullptr) {
        RetiredMesh retired;
        retired.mesh.vertexBuffer = std::move(slot.vertexBuffer);
        retired.mesh.indexBuffer = std::move(slot.indexBuffer);
        retired.mesh.indexCount = slot.indexCount;
        retired.retiredOnFrame = m_frameIndex;
        m_retired.push_back(std::move(retired));
    }

    slot.vertexBuffer.reset();
    slot.indexBuffer.reset();
    slot.indexCount = 0;
}

void Renderer::freeRetiredMeshes() {
    if (m_retired.empty()) {
        return;
    }

    // A mesh retired on frame N may still be referenced by frames N and N-1.
    // Once that many frames have been started since, nothing can touch it.
    constexpr std::uint64_t framesToHold = kFramesInFlight + 1;

    const auto expired = [&](const RetiredMesh& retired) {
        return m_frameIndex - retired.retiredOnFrame >= framesToHold;
    };

    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(), expired), m_retired.end());
}

void Renderer::uploadInto(GpuMesh& slot, const MeshData& mesh) {
    // Retire first: the old buffers are dead weight while the new ones are
    // allocated, and holding both doubles peak device memory.
    retire(slot);

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

    m_uploads->stage(*slot.vertexBuffer, mesh.vertices.data(), vertexBytes);
    m_uploads->stage(*slot.indexBuffer, mesh.indices.data(), indexBytes);
    slot.indexCount = static_cast<std::uint32_t>(mesh.indices.size());

    slot.boundsMin = glm::vec3{std::numeric_limits<float>::max()};
    slot.boundsMax = glm::vec3{std::numeric_limits<float>::lowest()};
    for (const Vertex& vertex : mesh.vertices) {
        const glm::vec3 p{vertex.position[0], vertex.position[1], vertex.position[2]};
        slot.boundsMin = glm::min(slot.boundsMin, p);
        slot.boundsMax = glm::max(slot.boundsMax, p);
    }
}

MeshHandle Renderer::addMesh(const MeshData& mesh) {
    MeshHandle handle = kInvalidMesh;

    if (!m_freeSlots.empty()) {
        handle = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        handle = static_cast<MeshHandle>(m_meshes.size());
        m_meshes.emplace_back();
    }

    uploadInto(m_meshes[handle], mesh);
    m_meshes[handle].inUse = true;
    return handle;
}

void Renderer::updateMesh(MeshHandle handle, const MeshData& mesh) {
    if (handle >= m_meshes.size() || !m_meshes[handle].inUse) {
        return;
    }
    uploadInto(m_meshes[handle], mesh);
    m_meshes[handle].inUse = true;
}

void Renderer::removeMesh(MeshHandle handle) {
    if (handle >= m_meshes.size() || !m_meshes[handle].inUse) {
        return;
    }
    retire(m_meshes[handle]);
    m_meshes[handle].inUse = false;
    m_freeSlots.push_back(handle);
}

void Renderer::setVerticalFov(float degrees) {
    m_verticalFovDegrees = std::clamp(degrees, 30.0f, 130.0f);
}

void Renderer::setFarPlane(float distance) {
    m_farPlane = std::max(distance, kNearPlane * 2.0f);
}

std::size_t Renderer::meshCount() const {
    return m_meshes.size() - m_freeSlots.size();
}

float Renderer::aspectRatio() const {
    const VkExtent2D extent = m_swapchain.extent();
    return extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);
}

void Renderer::setOverlayMesh(const MeshData& mesh) {
    uploadInto(m_overlayMesh, mesh);
}

// No device wait: retiring the old buffers already defers their destruction
// past every in-flight frame, so this is cheap enough to call whenever the HUD
// changes.
void Renderer::setScreenMesh(const MeshData& mesh) {
    uploadInto(m_screenMesh, mesh);
}

void Renderer::setSkyMesh(const MeshData& mesh) {
    uploadInto(m_skyMesh, mesh);
}

void Renderer::setSunDirection(const glm::vec3& direction) {
    const float length = glm::length(direction);
    m_sunDirection = length > 0.0f ? direction / length : glm::vec3{0.0f, 1.0f, 0.0f};
}

void Renderer::setSunLighting(float ambient, float sun) {
    m_ambientLight = ambient;
    m_sunLight = sun;
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

    glm::mat4 projection =
        glm::perspective(glm::radians(m_verticalFovDegrees), width / height, kNearPlane, m_farPlane);

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

    m_frameDrawCalls = 0;
    m_frameTriangles = 0;

    // Queries must be reset on the GPU timeline before being written again, and
    // this frame slot's previous results have already been read by now.
    if (m_timestampsSupported) {
        vkCmdResetQueryPool(commandBuffer, m_timestampPool, m_currentFrame * 2, 2);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampPool, m_currentFrame * 2);
    }

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

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline->handle());

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline->layout(), 0, 1,
                            &m_descriptorSet, 0, nullptr);

    const auto drawMesh = [&](const GpuMesh& mesh, const glm::mat4& transform, bool lit) {
        if (mesh.indexCount == 0) {
            return;
        }
        ++m_frameDrawCalls;
        m_frameTriangles += mesh.indexCount / 3;

        MeshPushConstants push{};
        push.modelViewProjection = transform;
        push.sunDirection = glm::vec4{m_sunDirection, 0.0f};
        push.lighting = glm::vec4{m_ambientLight, m_sunLight, lit ? 1.0f : 0.0f, 0.0f};
        vkCmdPushConstants(commandBuffer, m_trianglePipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT |
                                                                           VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(MeshPushConstants), &push);

        const VkBuffer vertexBuffers[] = {mesh.vertexBuffer->handle()};
        const VkDeviceSize vertexOffsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    };

    // Chunks outside the view are the overwhelming majority at high render
    // distance, and skipping them costs one box test each.
    const std::array<glm::vec4, 6> planes = frustumPlanes(viewProjection);
    for (const GpuMesh& mesh : m_meshes) {
        if (mesh.indexCount != 0 && !boxInFrustum(planes, mesh.boundsMin, mesh.boundsMax)) {
            continue;
        }
        drawMesh(mesh, viewProjection, true);
    }

    // After the world so it is depth-tested against terrain, and unlit because
    // the sun does not shade itself.
    drawMesh(m_skyMesh, viewProjection * m_skyTransform, false);

    if (overlayTransform.has_value()) {
        drawMesh(m_overlayMesh, viewProjection * *overlayTransform, false);
    }

    // Screen space: no view, no projection. Only an aspect correction, so
    // geometry authored in height-relative units is not stretched horizontally.
    if (m_screenMesh.indexCount != 0) {
        const float aspect = extent.height == 0
                                 ? 1.0f
                                 : static_cast<float>(extent.width) / static_cast<float>(extent.height);
        drawMesh(m_screenMesh, glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f / aspect, 1.0f, 1.0f}), false);
    }

    vkCmdEndRendering(commandBuffer);

    const VkImageMemoryBarrier toPresent =
        makeColorImageBarrier(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    if (m_timestampsSupported) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampPool,
                            m_currentFrame * 2 + 1);
    }

    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

void Renderer::drawFrame(const ClearColor& color, const glm::mat4& view,
                         const std::optional<glm::mat4>& overlayTransform) {
    // Before any early return, so a minimized or resizing window still releases
    // retired buffers instead of accumulating them.
    ++m_frameIndex;
    freeRetiredMeshes();

    if (m_window.isMinimized()) {
        return;
    }

    const VkDevice device = m_context.device();

    vkCheck(vkWaitForFences(device, 1, &m_frameInFlight[m_currentFrame], VK_TRUE, UINT64_MAX), "vkWaitForFences");

    // The fence above guarantees this frame slot's previous submission is done,
    // which is exactly when its timestamps become readable.
    readGpuTimestamps();

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

    // Uploads go in first. Same queue, so submission order plus the barrier at
    // the end of the batch is what makes the new geometry visible to these draws.
    m_uploads->flush();

    recordCommands(commandBuffer, imageIndex, color, projectionMatrix() * view, overlayTransform);

    // Recording is const, so the counters it fills are published here.
    m_stats.drawCalls = m_frameDrawCalls;
    m_stats.triangles = m_frameTriangles;
    if (m_timestampsSupported) {
        m_timestampsPending[m_currentFrame] = true;
    }

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
