#include "engine/render/Buffer.hpp"

#include "engine/render/VulkanContext.hpp"
#include "render/GpuMemory.hpp"
#include "render/VulkanCheck.hpp"

#include <cstring>
#include <stdexcept>

namespace engine {
namespace {

/// Frees the command buffer on every exit path, including an exception mid-upload.
class ScopedCommandBuffer {
public:
    ScopedCommandBuffer(VkDevice device, VkCommandPool pool) : m_device(device), m_pool(pool) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkCheck(vkAllocateCommandBuffers(device, &allocInfo, &m_commandBuffer), "vkAllocateCommandBuffers");
    }

    ~ScopedCommandBuffer() {
        if (m_commandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(m_device, m_pool, 1, &m_commandBuffer);
        }
    }

    ScopedCommandBuffer(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer& operator=(const ScopedCommandBuffer&) = delete;

    VkCommandBuffer handle() const { return m_commandBuffer; }

private:
    VkDevice m_device;
    VkCommandPool m_pool;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
};

} // namespace

Buffer::Buffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
               VkMemoryPropertyFlags memoryProperties)
    : m_context(context), m_size(size) {
    if (size == 0) {
        throw std::runtime_error("Cannot create a zero-sized buffer");
    }

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(context.device(), &bufferInfo, nullptr, &m_buffer), "vkCreateBuffer");

    // Creating a buffer does not allocate storage for it; that is a separate step.
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(context.device(), m_buffer, &requirements);

    // A slice of a shared block, not an allocation of its own. Vulkan caps how
    // many allocations may exist and a buffer each blew past the spec floor at
    // high render distances.
    const MemoryRange range =
        allocateBufferMemory(context.device(), context.physicalDevice(), requirements, memoryProperties);
    if (!range.valid()) {
        // The buffer already exists; without this it would leak on a failed allocation.
        vkDestroyBuffer(context.device(), m_buffer, nullptr);
        m_buffer = VK_NULL_HANDLE;
        throw std::runtime_error("vkAllocateMemory failed");
    }
    m_memory = range.memory;
    m_memoryOffset = range.offset;
    m_memorySize = range.size;
    m_mapped = range.mapped;

    vkCheck(vkBindBufferMemory(context.device(), m_buffer, m_memory, m_memoryOffset), "vkBindBufferMemory");
}

Buffer::~Buffer() {
    // **Never unmapped here.** A host-visible block is mapped once for its whole
    // life and shared by every buffer in it, so unmapping on one buffer's death
    // would pull the pointer out from under all the others.
    if (m_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(m_context.device(), m_buffer, nullptr);
    }
    freeBufferMemory(m_context.device(), MemoryRange{m_memory, m_memoryOffset, m_memorySize, m_mapped});
}

void* Buffer::persistentMap() {
    return m_mapped;
}

void Buffer::writeFromHost(const void* data, VkDeviceSize bytes, VkDeviceSize offset) {
    if (offset + bytes > m_size) {
        throw std::runtime_error("Write exceeds buffer size");
    }
    if (m_mapped == nullptr) {
        throw std::runtime_error("Cannot write to buffer memory that is not host visible");
    }
    std::memcpy(static_cast<std::byte*>(m_mapped) + offset, data, static_cast<std::size_t>(bytes));
}

void uploadBufferData(const VulkanContext& context, VkCommandPool commandPool, Buffer& destination, const void* data,
                      VkDeviceSize bytes) {
    Buffer staging(context, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    staging.writeFromHost(data, bytes);

    const ScopedCommandBuffer transfer(context.device(), commandPool);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(transfer.handle(), &beginInfo), "vkBeginCommandBuffer");

    VkBufferCopy region{};
    region.size = bytes;
    vkCmdCopyBuffer(transfer.handle(), staging.handle(), destination.handle(), 1, &region);

    vkCheck(vkEndCommandBuffer(transfer.handle()), "vkEndCommandBuffer");

    VkCommandBuffer commandBuffer = transfer.handle();
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    vkCheck(vkQueueSubmit(context.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");

    // The staging buffer is destroyed the moment this function returns, so the
    // copy has to have finished reading from it first.
    vkCheck(vkQueueWaitIdle(context.graphicsQueue()), "vkQueueWaitIdle");
}

} // namespace engine
