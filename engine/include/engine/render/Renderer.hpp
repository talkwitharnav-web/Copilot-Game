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
#include <optional>
#include <utility>
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

    /// Replaces every mesh drawn each frame. Blocks until the GPU has finished
    /// with the previous set, so call it at load time, not per frame.
    ///
    /// One slot is kept per entry, including empty ones, so an index into
    /// `meshes` stays valid for `updateMesh` afterwards.
    void uploadMeshes(const std::vector<MeshData>& meshes);

    /// Replaces meshes in place, keeping every other slot untouched. An empty
    /// mesh releases its slot's buffers rather than leaving stale geometry.
    ///
    /// Takes a batch because one world edit usually invalidates several chunks,
    /// and each call has to stall until the GPU is idle. Doing that once per
    /// batch instead of once per chunk is the difference between a hitch and no
    /// hitch.
    void updateMeshes(const std::vector<std::pair<std::size_t, MeshData>>& updates);

    /// Geometry drawn after the world with its own transform, supplied per
    /// frame. Uploaded once; moving it costs nothing.
    void setOverlayMesh(const MeshData& mesh);

    /// Renders and presents a single frame. Does nothing while the window is minimized.
    ///
    /// Scene geometry is already in world space, so the caller supplies only
    /// where it is viewed from. Projection is built here, from the render
    /// target's own dimensions, so the aspect ratio can never disagree with what
    /// is actually drawn.
    ///
    /// The overlay is drawn only when a transform is given.
    void drawFrame(const ClearColor& color, const glm::mat4& view,
                   const std::optional<glm::mat4>& overlayTransform = std::nullopt);

private:
    void createCommandResources();
    void createSyncObjects();
    void destroySyncObjects();
    void recreateSwapchain();
    glm::mat4 projectionMatrix() const;

    /// One uploaded mesh. Both buffers are owned here and freed together.
    /// A slot with no buffers is a valid empty mesh and is skipped when drawing.
    struct GpuMesh {
        std::unique_ptr<Buffer> vertexBuffer;
        std::unique_ptr<Buffer> indexBuffer;
        std::uint32_t indexCount = 0;
    };

    /// Allocates and fills a slot's buffers. Any previous contents are released
    /// first, so peak memory is one copy rather than two.
    void uploadInto(GpuMesh& slot, const MeshData& mesh);

    void recordCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, const ClearColor& color,
                        const glm::mat4& viewProjection, const std::optional<glm::mat4>& overlayTransform) const;

    /// How many frames the CPU is allowed to work on before waiting for the GPU.
    static constexpr std::uint32_t kFramesInFlight = 2;

    const VulkanContext& m_context;
    Window& m_window;
    Swapchain m_swapchain;
    std::unique_ptr<DepthImage> m_depthImage;
    GraphicsPipeline m_trianglePipeline;
    std::vector<GpuMesh> m_meshes;
    GpuMesh m_overlayMesh;

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
