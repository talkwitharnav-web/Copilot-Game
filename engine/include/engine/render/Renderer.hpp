#pragma once

#include "engine/render/Buffer.hpp"
#include "engine/render/DepthImage.hpp"
#include "engine/render/GraphicsPipeline.hpp"
#include "engine/render/MeshData.hpp"
#include "engine/render/Swapchain.hpp"
#include "engine/render/TextureArray.hpp"
#include "engine/render/UploadContext.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
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

/// Identifies one mesh for the renderer's lifetime. Slots are reused after
/// removal, so a stale handle refers to whatever took its place — drop handles
/// when you remove them.
using MeshHandle = std::uint32_t;
inline constexpr MeshHandle kInvalidMesh = ~MeshHandle{0};

/// What the last completed frame cost. GPU time is measured on the device with
/// timestamp queries, so it reports actual work rather than how long the CPU
/// waited.
struct RenderStats {
    float gpuMilliseconds = 0.0f;
    std::uint32_t drawCalls = 0;
    std::uint32_t triangles = 0;
};

/// Drives one frame of GPU work: acquire an image, record commands, submit, present.
class Renderer {
public:
    /// `blockTextures` are loaded into a texture array in the order given; the
    /// index into that list is what a vertex's `layer` refers to. `hudTexture`
    /// and `fontTexture` are separate sheets, selected by negative layers.
    Renderer(const VulkanContext& context, Window& window, const std::vector<std::filesystem::path>& blockTextures,
             const std::filesystem::path& hudTexture, const std::filesystem::path& fontTexture);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /// Uploads a new mesh and returns its handle. An empty mesh is valid and
    /// occupies a slot without any GPU memory.
    MeshHandle addMesh(const MeshData& mesh);

    /// Replaces a mesh's contents, keeping its handle.
    void updateMesh(MeshHandle handle, const MeshData& mesh);

    /// Releases a mesh and frees its slot for reuse.
    void removeMesh(MeshHandle handle);

    /// Geometry drawn after the world with its own transform, supplied per
    /// frame. Uploaded once; moving it costs nothing.
    void setOverlayMesh(const MeshData& mesh);

    /// Flat geometry drawn last, in screen space, ignoring the camera entirely.
    ///
    /// Coordinates run -1 to 1 over the window *height* on both axes, so a
    /// square stays square: the renderer divides X by the aspect ratio. Z should
    /// be near zero to sit in front of the world.
    void setScreenMesh(const MeshData& mesh);

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

    /// Vertical field of view in degrees. Wider shows more of the world and
    /// exaggerates perspective; narrower feels zoomed in. Clamped to a sane
    /// range, because past roughly 130 the distortion at screen edges makes the
    /// game unplayable rather than merely ugly.
    void setVerticalFov(float degrees);
    float verticalFov() const { return m_verticalFovDegrees; }

    /// Distance beyond which nothing is drawn. Must reach past the furthest
    /// visible geometry or the world is visibly clipped into a dome.
    void setFarPlane(float distance);

    std::size_t meshCount() const;

    const RenderStats& stats() const { return m_stats; }

    /// Width divided by height. Screen-space geometry is built in units relative
    /// to window height, so anything anchored to a left or right edge needs this.
    float aspectRatio() const;

    /// Buffers freed but still held back until in-flight frames finish with
    /// them. Should hover near zero; sustained growth means the release logic
    /// has stopped running.
    std::size_t retiredMeshCount() const { return m_retired.size(); }

private:
    void createCommandResources();
    void createDescriptorResources();
    void createTimestampPool();
    void readGpuTimestamps();
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
        /// World-space bounds, for frustum culling. Computed on upload.
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        /// Distinguishes a live-but-empty mesh from a free slot. Without it a
        /// double remove would push the same handle onto the free list twice and
        /// hand it to two different chunks.
        bool inUse = false;
    };

    /// Allocates and fills a slot's buffers. Any previous contents are retired
    /// first, so peak memory is one copy rather than two.
    void uploadInto(GpuMesh& slot, const MeshData& mesh);

    /// Hands a mesh's buffers to the delayed-destruction list.
    ///
    /// Frames already submitted may still be reading them, and streaming
    /// replaces meshes far too often to stall the GPU each time. Holding them
    /// for a few frames costs a little memory and removes the stall entirely.
    void retire(GpuMesh& slot);
    void freeRetiredMeshes();

    void recordCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, const ClearColor& color,
                        const glm::mat4& viewProjection, const std::optional<glm::mat4>& overlayTransform) const;
    /// How many frames the CPU is allowed to work on before waiting for the GPU.
    static constexpr std::uint32_t kFramesInFlight = 2;

    const VulkanContext& m_context;
    Window& m_window;
    Swapchain m_swapchain;
    std::unique_ptr<DepthImage> m_depthImage;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::unique_ptr<TextureArray> m_blockTextures;
    std::unique_ptr<TextureArray> m_hudTexture;
    std::unique_ptr<TextureArray> m_fontTexture;
    std::unique_ptr<UploadContext> m_uploads;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    std::unique_ptr<GraphicsPipeline> m_trianglePipeline;
    std::vector<GpuMesh> m_meshes;
    std::vector<MeshHandle> m_freeSlots;
    GpuMesh m_overlayMesh;
    GpuMesh m_screenMesh;

    struct RetiredMesh {
        GpuMesh mesh;
        std::uint64_t retiredOnFrame = 0;
    };
    std::vector<RetiredMesh> m_retired;
    std::uint64_t m_frameIndex = 0;
    float m_verticalFovDegrees = 70.0f;
    float m_farPlane = 500.0f;

    std::vector<VkCommandBuffer> m_commandBuffers;

    // Per frame-in-flight.
    std::vector<VkSemaphore> m_imageAvailable;
    std::vector<VkFence> m_frameInFlight;

    // Per swapchain image. Presentation can hold on to a semaphore for longer
    // than one frame-in-flight cycle, so reusing a per-frame semaphore here is a
    // real (and validation-flagged) synchronization bug.
    std::vector<VkSemaphore> m_renderFinished;

    std::uint32_t m_currentFrame = 0;

    RenderStats m_stats;
    /// Two timestamps per frame in flight: one before any work, one after.
    VkQueryPool m_timestampPool = VK_NULL_HANDLE;
    float m_timestampPeriodNanoseconds = 0.0f;
    bool m_timestampsSupported = false;
    std::vector<bool> m_timestampsPending;

    // Counted while recording, which is const, so they are mutable.
    mutable std::uint32_t m_frameDrawCalls = 0;
    mutable std::uint32_t m_frameTriangles = 0;
};

} // namespace engine
