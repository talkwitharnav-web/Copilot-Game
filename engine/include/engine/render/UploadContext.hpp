#pragma once

#include "engine/render/Buffer.hpp"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace engine {

class VulkanContext;

/// Batches buffer uploads into one submission instead of one per buffer.
///
/// The naive path — allocate a staging buffer, submit a copy, wait for the
/// queue, per buffer — costs two full GPU stalls for every mesh. A world of 363
/// chunks paid 726 of them, and every block break paid two. This records all
/// outstanding copies into a single command buffer, submits once, and only ever
/// blocks when the staging arena has to be reused before the GPU has finished
/// reading it.
///
/// Copies are submitted on the graphics queue **before** the frame that reads
/// them, and end with a barrier from transfer writes to every graphics read.
/// Queue submission order plus that barrier is what makes the data visible;
/// there is no separate transfer queue and no semaphore.
class UploadContext {
public:
    UploadContext(const VulkanContext& context, VkDeviceSize arenaBytes);
    ~UploadContext();

    UploadContext(const UploadContext&) = delete;
    UploadContext& operator=(const UploadContext&) = delete;

    /// Queues a copy into `destination`. The data is consumed immediately, so
    /// the caller's memory need not outlive the call.
    void stage(Buffer& destination, const void* data, VkDeviceSize bytes);

    /// Submits everything queued. Does not block. Safe to call with nothing
    /// pending, in which case it does nothing.
    void flush();

    /// Blocks until submitted copies have completed. Only needed before
    /// destroying something they write into.
    void waitForCompletion();

    /// Submissions made, for confirming that batching is actually happening.
    std::size_t submissionCount() const { return m_submissions; }

private:
    void beginRecording();
    /// Frees the arena for reuse, waiting on the previous submission if needed.
    void recycleArena();

    const VulkanContext& m_context;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;

    std::unique_ptr<Buffer> m_arena;
    std::byte* m_arenaMapped = nullptr;
    VkDeviceSize m_arenaCapacity = 0;
    VkDeviceSize m_arenaOffset = 0;

    /// Buffers too large for the arena get their own staging allocation, held
    /// until the submission that reads them completes.
    std::vector<std::unique_ptr<Buffer>> m_oversized;

    bool m_recording = false;
    bool m_submitted = false;
    std::size_t m_submissions = 0;
};

} // namespace engine
