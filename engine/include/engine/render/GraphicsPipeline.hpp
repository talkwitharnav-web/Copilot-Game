#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace engine {

/// How a fragment combines with what is already in the attachment.
enum class BlendMode {
    /// Overwrite. What a G-buffer and every intermediate post pass want:
    /// a blended normal is not a normal and a blended material id is nonsense.
    None,
    /// The ordinary transparency equation, weighted by the fragment's alpha.
    Alpha,
    /// Straight sum. Used by the bloom upsample, which accumulates a pyramid.
    Additive,
};

/// Everything that varies between the renderer's pipelines.
///
/// This was twelve hardcoded decisions in one constructor while there was
/// exactly one pipeline. There are several now and they disagree on most of
/// them, so each is a field - but deliberately a plain struct of overrides
/// rather than a "material system": one case each is not a second case.
struct PipelineDesc {
    std::filesystem::path vertexSpirv;
    std::filesystem::path fragmentSpirv;

    /// One entry per colour attachment, in order. Held by the caller across the
    /// constructor call, because Vulkan reads the array rather than copying it.
    std::vector<VkFormat> colorFormats;
    /// `VK_FORMAT_UNDEFINED` means this pass has no depth attachment at all -
    /// which is different from having one and not testing against it.
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    /// 0 for a pipeline that pushes nothing.
    std::uint32_t pushConstantBytes = 0;

    /// False for a full-screen pass, which builds its triangle from
    /// `gl_VertexIndex` and binds no vertex buffer.
    bool vertexInput = true;

    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    bool depthTest = true;
    bool depthWrite = true;
    BlendMode blend = BlendMode::Alpha;

    /// Pushes every fragment slightly further from the viewer before the depth
    /// test. Zero on both leaves it disabled. Only the shadow pass uses it: a
    /// surface compared against a depth map of itself otherwise shadows itself
    /// wherever the sampled texel rounds the wrong way.
    ///
    /// The slope term is the one that matters, because the error grows with how
    /// obliquely the light strikes the surface.
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
};

/// One complete configuration for drawing: both shader stages plus all the
/// fixed-function state (topology, rasterizer, blending) baked into a single
/// object the GPU validates once instead of per draw call.
///
/// Viewport and scissor are deliberately left as dynamic state so resizing the
/// window does not require rebuilding the pipeline.
class GraphicsPipeline {
public:
    GraphicsPipeline(VkDevice device, const PipelineDesc& desc);
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;
    GraphicsPipeline(GraphicsPipeline&&) = delete;
    GraphicsPipeline& operator=(GraphicsPipeline&&) = delete;

    VkPipeline handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace engine
