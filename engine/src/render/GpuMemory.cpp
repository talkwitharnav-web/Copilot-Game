#include "render/GpuMemory.hpp"

#include "engine/core/Log.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace engine {
namespace {

// **Every mutable in this file is process-wide and none of it is keyed by
// `VkDevice`.** That is a deliberate simplification, not an oversight: this
// engine creates exactly one device, `VulkanContext` owns it for the whole run,
// and `CLAUDE.md` forbids building structure for a second implementation that is
// not in sight. The mutex makes it thread-safe, which is a different question
// from making it correct for two devices.
//
// **What breaks if a second `VkDevice` ever appears**, so whoever adds one finds
// it here rather than in a driver crash: `g_blocks` would hold blocks belonging
// to both, `carve` would hand a slice of one device's memory to a buffer created
// on the other, and `destroyBufferMemoryPools(VkDevice)` - which already takes a
// device and ignores it beyond passing it to `vkFreeMemory` - would free every
// block on whichever device it happened to be called with. The fix at that point
// is to key the pool by device, not to add a lock.
std::atomic<std::uint32_t> g_liveAllocations{0};

/// How much memory one pooled block holds.
///
/// 32 MiB against a world that reaches roughly 700 MiB of mesh data at render
/// distance 16: about twenty blocks instead of five thousand allocations. Larger
/// blocks waste more on the last partly-used one; smaller ones give the count
/// back. This is the smallest size at which the count stops being the binding
/// constraint.
constexpr VkDeviceSize kBlockBytes = 32u * 1024u * 1024u;

/// Anything at least this large gets a block to itself rather than a slice.
///
/// A buffer that fills most of a block can never share it usefully, and letting
/// one in fragments the block permanently. Nothing in the world reaches this;
/// the texture arrays would, and they are images and never come here at all.
constexpr VkDeviceSize kDedicatedBytes = kBlockBytes / 4;

struct FreeRange {
    VkDeviceSize offset;
    VkDeviceSize size;
};

struct Block {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
    std::uint32_t memoryTypeIndex = 0;
    /// Sorted by offset, which is what makes coalescing a look at the two
    /// neighbours rather than a search.
    std::vector<FreeRange> free;
};

/// The pool itself. Process-wide and not keyed by `VkDevice` - see the note on
/// `g_liveAllocations` above before adding a second device.
std::mutex g_poolMutex;
std::vector<Block> g_blocks;
VkDeviceSize g_bytesInUse = 0;
VkDeviceSize g_bytesReserved = 0;

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) / alignment * alignment;
}

/// Takes `size` bytes at `alignment` out of a block, or reports failure.
///
/// The padding an alignment forces is left in the free list as its own range
/// rather than being handed out with the allocation, so it can still be used by
/// something with a weaker alignment.
bool carve(Block& block, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize& outOffset) {
    for (std::size_t i = 0; i < block.free.size(); ++i) {
        const FreeRange range = block.free[i];
        const VkDeviceSize aligned = alignUp(range.offset, alignment);
        const VkDeviceSize head = aligned - range.offset;
        if (head + size > range.size) {
            continue;
        }

        const VkDeviceSize tail = range.size - head - size;
        block.free.erase(block.free.begin() + static_cast<std::ptrdiff_t>(i));
        std::size_t at = i;
        if (head > 0) {
            block.free.insert(block.free.begin() + static_cast<std::ptrdiff_t>(at), FreeRange{range.offset, head});
            ++at;
        }
        if (tail > 0) {
            block.free.insert(block.free.begin() + static_cast<std::ptrdiff_t>(at),
                              FreeRange{aligned + size, tail});
        }
        outOffset = aligned;
        return true;
    }
    return false;
}

void giveBack(Block& block, VkDeviceSize offset, VkDeviceSize size) {
    const auto after = std::lower_bound(block.free.begin(), block.free.end(), offset,
                                        [](const FreeRange& range, VkDeviceSize at) { return range.offset < at; });
    const auto inserted = block.free.insert(after, FreeRange{offset, size});

    // Merge forward first: merging backward would move the iterator this one is
    // expressed against.
    const auto next = inserted + 1;
    if (next != block.free.end() && inserted->offset + inserted->size == next->offset) {
        inserted->size += next->size;
        block.free.erase(next);
    }
    if (inserted != block.free.begin()) {
        const auto previous = inserted - 1;
        if (previous->offset + previous->size == inserted->offset) {
            previous->size += inserted->size;
            block.free.erase(inserted);
        }
    }
}

} // namespace

std::uint32_t findMemoryType(VkPhysicalDevice physicalDevice, std::uint32_t typeFilter,
                             VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties available{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &available);

    for (std::uint32_t i = 0; i < available.memoryTypeCount; ++i) {
        const bool allowed = (typeFilter & (1u << i)) != 0;
        const bool suitable = (available.memoryTypes[i].propertyFlags & properties) == properties;
        if (allowed && suitable) {
            return i;
        }
    }
    throw std::runtime_error("No GPU memory type satisfies the requested properties");
}

VkResult allocateDeviceMemory(VkDevice device, const VkMemoryAllocateInfo& info, VkDeviceMemory* memory) {
    const VkResult result = vkAllocateMemory(device, &info, nullptr, memory);
    if (result == VK_SUCCESS) {
        g_liveAllocations.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

void freeDeviceMemory(VkDevice device, VkDeviceMemory memory) {
    if (memory == VK_NULL_HANDLE) {
        return;
    }
    vkFreeMemory(device, memory, nullptr);
    g_liveAllocations.fetch_sub(1, std::memory_order_relaxed);
}

std::uint32_t liveDeviceAllocations() {
    return g_liveAllocations.load(std::memory_order_relaxed);
}

MemoryRange allocateBufferMemory(VkDevice device, VkPhysicalDevice physicalDevice,
                                 const VkMemoryRequirements& requirements,
                                 VkMemoryPropertyFlags properties) {
    const std::uint32_t typeIndex = findMemoryType(physicalDevice, requirements.memoryTypeBits, properties);
    const bool hostVisible = (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

    std::lock_guard<std::mutex> lock(g_poolMutex);

    if (requirements.size < kDedicatedBytes) {
        for (Block& block : g_blocks) {
            // **A block is keyed by memory type, but only some blocks are
            // mapped**, and whether one got mapped was decided by the
            // *properties the call that created it asked for*. On any device
            // where the first `DEVICE_LOCAL` type is also `HOST_VISIBLE` -
            // AMD APUs, older Intel iGPUs, mobile and software drivers all
            // expose one - a mesh buffer and a staging buffer resolve to the
            // same type index, so a host-visible request could be handed a
            // slice of an unmapped block and come back with `mapped ==
            // nullptr`. The upload arena then memcpys through a null pointer
            // on startup, or `writeFromHost` throws "not host visible" from
            // the middle of a frame - either way pointing at the wrong thing
            // entirely. Neither GPU in this machine collides, which is exactly
            // why nothing here ever warned.
            const bool unusable = hostVisible && block.mapped == nullptr;
            if (block.memoryTypeIndex != typeIndex || unusable) {
                continue;
            }
            VkDeviceSize offset = 0;
            if (carve(block, requirements.size, requirements.alignment, offset)) {
                g_bytesInUse += requirements.size;
                return MemoryRange{block.memory, offset, requirements.size,
                                   block.mapped == nullptr
                                       ? nullptr
                                       : static_cast<void*>(static_cast<std::byte*>(block.mapped) + offset)};
            }
        }
    }

    // Either nothing had room or the request is too big to share. A dedicated
    // block is still a `Block`, with its whole span handed out at once, so
    // freeing takes exactly the same path.
    const VkDeviceSize blockSize = std::max(kBlockBytes, alignUp(requirements.size, requirements.alignment));
    const bool dedicated = requirements.size >= kDedicatedBytes;

    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = dedicated ? alignUp(requirements.size, requirements.alignment) : blockSize;
    info.memoryTypeIndex = typeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (allocateDeviceMemory(device, info, &memory) != VK_SUCCESS) {
        // **Ask again for what the caller actually wanted.** A shared block is
        // always requested at the full 32 MiB, so with 4 MiB of heap left every
        // 200 KB buffer fails - the allocator refuses on behalf of a size
        // nobody asked for, chunks stop appearing, and the frame dies on a
        // generic exception. A block sized to the request is immediately full,
        // which the whole-block-free test in `freeBufferMemory` already handles
        // correctly, so nothing downstream changes.
        if (!dedicated) {
            info.allocationSize = alignUp(requirements.size, requirements.alignment);
            if (allocateDeviceMemory(device, info, &memory) != VK_SUCCESS) {
                logError("vkAllocateMemory failed for " + std::to_string(requirements.size) +
                         " bytes (retried at exact size); " + std::to_string(liveDeviceAllocations()) +
                         " device allocations live");
                return MemoryRange{};
            }
        } else {
            logError("vkAllocateMemory failed for a dedicated block of " + std::to_string(info.allocationSize) +
                     " bytes; " + std::to_string(liveDeviceAllocations()) + " device allocations live");
            return MemoryRange{};
        }
    }

    void* mapped = nullptr;
    if (hostVisible && vkMapMemory(device, memory, 0, info.allocationSize, 0, &mapped) != VK_SUCCESS) {
        // Named separately from the failure above, or the two collapse into one
        // silent return and the log cannot say which happened.
        logError("vkMapMemory failed for " + std::to_string(info.allocationSize) + " bytes of host-visible memory");
        freeDeviceMemory(device, memory);
        return MemoryRange{};
    }

    Block block;
    block.memory = memory;
    block.size = info.allocationSize;
    block.mapped = mapped;
    block.memoryTypeIndex = typeIndex;
    block.free.push_back(FreeRange{0, info.allocationSize});
    g_bytesReserved += info.allocationSize;

    VkDeviceSize offset = 0;
    if (!carve(block, requirements.size, requirements.alignment, offset)) {
        // Cannot happen: the block was sized to hold it. Handled anyway rather
        // than handing back a range pointing into memory nothing owns.
        if (mapped != nullptr) {
            vkUnmapMemory(device, memory);
        }
        freeDeviceMemory(device, memory);
        g_bytesReserved -= info.allocationSize;
        return MemoryRange{};
    }

    g_blocks.push_back(std::move(block));
    g_bytesInUse += requirements.size;
    return MemoryRange{memory, offset, requirements.size,
                       mapped == nullptr ? nullptr
                                         : static_cast<void*>(static_cast<std::byte*>(mapped) + offset)};
}

void freeBufferMemory(VkDevice device, const MemoryRange& range) {
    if (!range.valid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_poolMutex);

    const auto found = std::find_if(g_blocks.begin(), g_blocks.end(),
                                    [&](const Block& block) { return block.memory == range.memory; });
    if (found == g_blocks.end()) {
        return;
    }

    giveBack(*found, range.offset, range.size);
    g_bytesInUse -= range.size;

    // A block nothing is using is released rather than kept, or walking away
    // from a large world would leave every byte of it reserved for the rest of
    // the session. One free range spanning the whole block is the test.
    if (found->free.size() == 1 && found->free[0].offset == 0 && found->free[0].size == found->size) {
        if (found->mapped != nullptr) {
            vkUnmapMemory(device, found->memory);
        }
        freeDeviceMemory(device, found->memory);
        g_bytesReserved -= found->size;
        g_blocks.erase(found);
    }
}

void destroyBufferMemoryPools(VkDevice device) {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    for (Block& block : g_blocks) {
        if (block.mapped != nullptr) {
            vkUnmapMemory(device, block.memory);
        }
        freeDeviceMemory(device, block.memory);
    }
    g_blocks.clear();
    g_bytesInUse = 0;
    g_bytesReserved = 0;
}

VkDeviceSize pooledBytesInUse() {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    return g_bytesInUse;
}

VkDeviceSize pooledBytesReserved() {
    std::lock_guard<std::mutex> lock(g_poolMutex);
    return g_bytesReserved;
}

} // namespace engine
