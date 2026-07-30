#pragma once

#include <vulkan/vulkan.h>

namespace engine {

class VulkanContext;

/// A GPU buffer together with the memory backing it.
///
/// Vulkan allocates the memory and the buffer separately and reference-counts
/// neither, so they are kept in one object that releases both. Copy and move are
/// deleted: two objects owning one allocation is how double-frees happen.
///
/// One allocation per buffer is fine at this scale. Once there are many small
/// buffers this should move to a sub-allocator (see TIMELINE.md, VMA at M4+).
class Buffer {
public:
    Buffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags memoryProperties);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    /// Copies `bytes` of `data` in. Only valid on host-visible memory.
    void writeFromHost(const void* data, VkDeviceSize bytes);

    VkBuffer handle() const { return m_buffer; }
    VkDeviceSize size() const { return m_size; }

private:
    const VulkanContext& m_context;
    VkDeviceSize m_size = 0;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
};

/// Fills a device-local buffer by staging through host-visible memory.
///
/// The fast memory a GPU renders from is usually not writable by the CPU, so the
/// data goes into a temporary host-visible buffer first and the GPU copies it
/// across. Blocks until that copy has finished, which is acceptable at load time
/// and nowhere else.
void uploadBufferData(const VulkanContext& context, VkCommandPool commandPool, Buffer& destination, const void* data,
                      VkDeviceSize bytes);

} // namespace engine
