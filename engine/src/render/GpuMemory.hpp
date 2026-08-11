#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace engine {

/// GPUs expose several memory types with different speed/visibility tradeoffs.
/// `typeFilter` is the set Vulkan says a given resource may use; `properties` is
/// what we need from it. Throws if nothing satisfies both.
std::uint32_t findMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t typeFilter,
                             VkMemoryPropertyFlags properties);

/// `vkAllocateMemory` and `vkFreeMemory`, counting what is currently live.
///
/// Vulkan caps how many allocations may exist at once and the spec floor is
/// 4096. The failure mode is `vkAllocateMemory` returning `TOO_MANY_OBJECTS`
/// with nothing having warned first, so the count is kept and the renderer
/// reports it once it passes three quarters of the device's own limit.
///
/// The count is a process-wide static, which the rest of the engine avoids on
/// principle. It is the honest shape here: the limit is a property of the one
/// device, not of any object that could own the number.
VkResult allocateDeviceMemory(VkDevice device, const VkMemoryAllocateInfo& info, VkDeviceMemory* memory);
void freeDeviceMemory(VkDevice device, VkDeviceMemory memory);
std::uint32_t liveDeviceAllocations();

/// A slice of a shared `VkDeviceMemory` block, or a whole dedicated one.
///
/// `mapped` is only non-null for host-visible memory, which is mapped once per
/// block and never unmapped - Vulkan forbids mapping one `VkDeviceMemory` twice,
/// so a per-buffer map becomes impossible the moment buffers start sharing a
/// block.
struct MemoryRange {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    void* mapped = nullptr;

    bool valid() const { return memory != VK_NULL_HANDLE; }
};

/// Carves buffer storage out of a few large allocations instead of taking one
/// per buffer.
///
/// **The number of live allocations is the resource being managed here, not the
/// bytes.** Two buffers per chunk mesh and up to two meshes per chunk reached
/// roughly three thousand at render distance 12 against a spec floor of 4096,
/// and render distance 16 went past it - on a device reporting exactly the
/// floor the world would simply stop appearing, with no warning that made sense.
///
/// **Buffers only, never images.** Mixing linear and optimal-tiled resources in
/// one allocation drags in the `bufferImageGranularity` padding rules; images
/// here number in the tens and are created once, so they keep their own
/// dedicated allocations and the question does not arise.
///
/// First fit with coalescing of free neighbours. Deliberately the simplest
/// allocator that works: mesh buffers are handed back and taken again in a churn
/// of similar sizes, which is exactly the case first fit handles well, and
/// anything cleverer would need evidence that this one is a problem.
MemoryRange allocateBufferMemory(VkDevice device, VkPhysicalDevice physicalDevice,
                                 const VkMemoryRequirements& requirements,
                                 VkMemoryPropertyFlags properties);

void freeBufferMemory(VkDevice device, const MemoryRange& range);

/// Releases every pooled block. **Must be called before `vkDestroyDevice`**, or
/// the blocks outlive the device they came from. `VulkanContext` does it.
void destroyBufferMemoryPools(VkDevice device);

/// Bytes currently handed out, and bytes held in blocks. The difference between
/// them is what the pooling costs.
VkDeviceSize pooledBytesInUse();
VkDeviceSize pooledBytesReserved();

} // namespace engine
