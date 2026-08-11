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
/// The memory is a **slice of a shared block**, not an allocation of its own -
/// see `GpuMemory.hpp`. Vulkan caps the number of live allocations at a spec
/// floor of 4096, and a buffer each reached three thousand of them at render
/// distance 12.
class Buffer {
public:
    Buffer(const VulkanContext& context, VkDeviceSize size, VkBufferUsageFlags usage,
           VkMemoryPropertyFlags memoryProperties);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(Buffer&&) = delete;

    /// Copies `bytes` of `data` in at `offset`. Only valid on host-visible memory.
    void writeFromHost(const void* data, VkDeviceSize bytes, VkDeviceSize offset = 0);

    /// A pointer to this buffer's own bytes, or null if the memory is not host
    /// visible.
    ///
    /// **The mapping belongs to the whole block and is made once when the block
    /// is created**, because Vulkan forbids mapping one `VkDeviceMemory` twice
    /// and a block is shared. That is also what this wanted anyway: staging
    /// memory is written every frame and mapping is not free.
    void* persistentMap();

    VkBuffer handle() const { return m_buffer; }
    VkDeviceSize size() const { return m_size; }

private:
    const VulkanContext& m_context;
    VkDeviceSize m_size = 0;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    // Where this buffer's bytes live inside a shared block. Kept as plain
    // fields rather than as a copy of `MemoryRange`, which is defined in a
    // private header: a second definition of the same four values is exactly the
    // shape of bug this project keeps paying for.
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkDeviceSize m_memoryOffset = 0;
    VkDeviceSize m_memorySize = 0;
    void* m_mapped = nullptr;
};

/// Fills a device-local buffer by staging through host-visible memory.
///
/// The fast memory a GPU renders from is usually not writable by the CPU, so the
/// data goes into a temporary host-visible buffer first and the GPU copies it
/// across. Blocks until that copy has finished.
///
/// **One-shot and slow.** Every call allocates, submits and waits for the queue.
/// Fine for a texture uploaded once at startup; use `UploadContext` for anything
/// that happens more than once.
void uploadBufferData(const VulkanContext& context, VkCommandPool commandPool, Buffer& destination, const void* data,
                      VkDeviceSize bytes);

} // namespace engine
