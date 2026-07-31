#include "engine/render/UploadContext.hpp"

#include "engine/render/VulkanContext.hpp"
#include "render/VulkanCheck.hpp"

#include <cstring>

namespace engine {
namespace {

/// Copy offsets only need 4-byte alignment for our vertex and index data, but
/// keeping everything 256-aligned costs a few bytes and avoids ever tripping
/// over a stricter requirement later.
constexpr VkDeviceSize kAlignment = 256;

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

UploadContext::UploadContext(const VulkanContext& context, VkDeviceSize arenaBytes)
    : m_context(context), m_arenaCapacity(arenaBytes) {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.graphicsQueueFamily();
    vkCheck(vkCreateCommandPool(context.device(), &poolInfo, nullptr, &m_commandPool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(context.device(), &allocInfo, &m_commandBuffer), "vkAllocateCommandBuffers");

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCheck(vkCreateFence(context.device(), &fenceInfo, nullptr, &m_fence), "vkCreateFence");

    m_arena = std::make_unique<Buffer>(context, arenaBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_arenaMapped = static_cast<std::byte*>(m_arena->persistentMap());
}

UploadContext::~UploadContext() {
    if (m_submitted) {
        vkWaitForFences(m_context.device(), 1, &m_fence, VK_TRUE, UINT64_MAX);
    }
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_context.device(), m_fence, nullptr);
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_context.device(), m_commandPool, nullptr);
    }
}

void UploadContext::beginRecording() {
    if (m_recording) {
        return;
    }

    // The arena is only free once the submission that read it has finished.
    recycleArena();

    vkCheck(vkResetCommandBuffer(m_commandBuffer, 0), "vkResetCommandBuffer");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(m_commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    m_recording = true;
}

void UploadContext::recycleArena() {
    if (m_submitted) {
        vkCheck(vkWaitForFences(m_context.device(), 1, &m_fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        vkCheck(vkResetFences(m_context.device(), 1, &m_fence), "vkResetFences");
        m_submitted = false;
    }
    m_arenaOffset = 0;
    m_oversized.clear();
}

void UploadContext::stage(Buffer& destination, const void* data, VkDeviceSize bytes) {
    if (bytes == 0) {
        return;
    }

    // Too big to ever fit: give it a dedicated staging buffer rather than
    // growing the arena for one outlier.
    if (bytes > m_arenaCapacity) {
        beginRecording();

        auto staging = std::make_unique<Buffer>(m_context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging->writeFromHost(data, bytes);

        VkBufferCopy region{};
        region.size = bytes;
        vkCmdCopyBuffer(m_commandBuffer, staging->handle(), destination.handle(), 1, &region);

        m_oversized.push_back(std::move(staging));
        return;
    }

    // Out of room: submit what is queued, then wait for the arena to come back.
    if (m_recording && m_arenaOffset + bytes > m_arenaCapacity) {
        flush();
    }

    beginRecording();

    const VkDeviceSize offset = m_arenaOffset;
    std::memcpy(m_arenaMapped + offset, data, static_cast<std::size_t>(bytes));
    m_arenaOffset = alignUp(offset + bytes, kAlignment);

    VkBufferCopy region{};
    region.srcOffset = offset;
    region.size = bytes;
    vkCmdCopyBuffer(m_commandBuffer, m_arena->handle(), destination.handle(), 1, &region);
}

void UploadContext::flush() {
    if (!m_recording) {
        return;
    }

    // Makes the copies visible to the draws that follow on this queue. Without
    // it the data is in the right buffers but the vertex fetch may not see it,
    // which shows up as flickering or missing geometry rather than an error.
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
    vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);

    vkCheck(vkEndCommandBuffer(m_commandBuffer), "vkEndCommandBuffer");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    vkCheck(vkQueueSubmit(m_context.graphicsQueue(), 1, &submitInfo, m_fence), "vkQueueSubmit");

    m_recording = false;
    m_submitted = true;
    ++m_submissions;
}

void UploadContext::waitForCompletion() {
    flush();
    recycleArena();
}

} // namespace engine
