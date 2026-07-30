#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>

namespace engine {

/// One complete configuration for drawing: both shader stages plus all the
/// fixed-function state (topology, rasterizer, blending) baked into a single
/// object the GPU validates once instead of per draw call.
///
/// Viewport and scissor are deliberately left as dynamic state so resizing the
/// window does not require rebuilding the pipeline.
class GraphicsPipeline {
public:
    /// `colorFormat` must match the format of the images this pipeline renders
    /// into; dynamic rendering validates the two against each other.
    GraphicsPipeline(VkDevice device, const std::filesystem::path& vertexSpirv,
                     const std::filesystem::path& fragmentSpirv, VkFormat colorFormat);
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
