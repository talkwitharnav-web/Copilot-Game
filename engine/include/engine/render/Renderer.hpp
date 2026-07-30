#pragma once

#include "engine/render/Buffer.hpp"
#include "engine/render/DepthImage.hpp"
#include "engine/render/GraphicsPipeline.hpp"
#include "engine/render/MeshData.hpp"
#include "engine/render/Swapchain.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
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

    /// Replaces the geometry drawn every frame. Blocks until the GPU has
    /// finished with the previous mesh, so call it at load time, not per frame.
    /// An empty mesh simply draws nothing.
    void uploadMesh(const MeshData& mesh);

    /// Renders and presents a single frame. Does nothing while the window is minimized.
    ///
    /// Scene geometry is already in world space, so the caller supplies only
    /// where it is viewed from. Projection is built here, from the render
    /// target's own dimensions, so the aspect ratio can never disagree with what
    /// is actually drawn.
    void drawFrame(const ClearColor& color, const glm::mat4& view);

private:
    void createCommandResources();
    void createSyncObjects();
    void destroySyncObjects();
    void recreateSwapchain();
    glm::mat4 projectionMatrix() const;
    void recordCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, const ClearColor& color,
                        const glm::mat4& modelViewProjection) const;

    /// How many frames the CPU is allowed to work on before waiting for the GPU.
    static constexpr std::uint32_t kFramesInFlight = 2;

    const VulkanContext& m_context;
    Window& m_window;
    Swapchain m_swapchain;
    std::unique_ptr<DepthImage> m_depthImage;
    GraphicsPipeline m_trianglePipeline;
    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    std::uint32_t m_indexCount = 0;

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
