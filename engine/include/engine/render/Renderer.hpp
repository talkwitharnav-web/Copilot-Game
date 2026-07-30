#pragma once

#include "engine/render/GraphicsPipeline.hpp"
#include "engine/render/Swapchain.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace engine {

class VulkanContext;
class Window;

struct ClearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

/// Drives one frame of GPU work: acquire an image, record commands, submit, present.
class Renderer {
public:
    Renderer(const VulkanContext& context, Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /// Renders and presents a single frame. Does nothing while the window is minimized.
    void drawFrame(const ClearColor& color);

private:
    void createCommandResources();
    void createSyncObjects();
    void destroySyncObjects();
    void recreateSwapchain();
    void recordCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, const ClearColor& color) const;

    /// How many frames the CPU is allowed to work on before waiting for the GPU.
    static constexpr std::uint32_t kFramesInFlight = 2;

    const VulkanContext& m_context;
    Window& m_window;
    Swapchain m_swapchain;
    GraphicsPipeline m_trianglePipeline;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Per frame-in-flight.
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkFence> m_frameInFlight;

    // Per swapchain image. Presentation can hold on to a semaphore for longer
    // than one frame-in-flight cycle, so reusing a per-frame semaphore here is a
    // real (and validation-flagged) synchronization bug.
    std::vector<VkSemaphore> m_renderFinished;

    std::uint32_t m_currentFrame = 0;
};

} // namespace engine
