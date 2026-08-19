#include "engine/render/Renderer.hpp"

#include "engine/core/Log.hpp"
#include "engine/core/Paths.hpp"
#include "engine/platform/Window.hpp"
#include "engine/render/PushConstants.hpp"
#include "engine/render/VulkanContext.hpp"
#include "engine/render/Vertex.hpp"
#include "render/GpuMemory.hpp"
#include "render/VulkanCheck.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace engine {
namespace {

// **Four is written down in two more places that no compiler can see.**
// `frame.glsl` declares `mat4 shadowMatrices[4]` as a literal, and
// `Renderer::m_shadowTexelWorld` is a `glm::vec4` indexed by cascade, so it
// holds exactly four floats. The `static_assert` in `FrameUniforms.hpp` is
// parameterised on `kMaxCascades` and would happily follow it to 5 while the
// GLSL block stayed at 4 and the texel widths ran off the end of the vector -
// a shadow map sampled with a matrix belonging to a different cascade, which
// reads as shadows sliding off their casters rather than as any kind of error.
//
// **The single edit that makes this fail is raising
// `ShadowMap::kMaxCascades` to 5**, which is exactly the edit that must not
// happen on its own.
static_assert(ShadowMap::kMaxCascades == 4u,
              "frame.glsl's shadowMatrices[4] and m_shadowTexelWorld's glm::vec4 both hard-code 4");

// A range the caller supplied, made safe against the buffer that actually got
// uploaded. The mesher and the upload are two separate steps and a slot is
// reused by a different chunk, so a tail that outruns the buffer is a live
// possibility rather than a theoretical one - and `vkCmdDrawIndexed` reading
// past the end is undefined behaviour that reads on screen as panes flickering
// somewhere unrelated to the chunk that caused it. Anything that does not fit
// is dropped whole, because half a pane is worse than none.
MeshIndexRange clampToMesh(const MeshIndexRange& range, std::uint32_t indexCount) {
    if (range.count == 0 || range.first >= indexCount || range.count > indexCount - range.first) {
        return MeshIndexRange{};
    }
    return range;
}

// Ten centimetres. Every step of depth precision is spent between here and
// `m_farPlane`, and an ordinary (non-reversed) depth buffer spends most of it
// close to the eye, so pulling this nearer costs precision everywhere else in
// the world. Ten centimetres is small enough that the camera can stand against
// a wall without the wall disappearing, and no smaller.
//
// **This is not the field of view.** That is `m_verticalFovDegrees`, it
// defaults to 70, and `Renderer::setVerticalFov` is where its range is argued.
constexpr float kNearPlane = 0.1f;

/// What each shadow quality level costs and buys. Level 0 is off.
///
/// The resolution is per cascade and the map holds all of them, so level 3 is
/// 2048 x 2048 x 4 at four bytes a texel - 64 MB, which is why the top level is
/// not 4096.
struct ShadowQualityLevel {
    std::uint32_t resolution;
    std::uint32_t cascades;
    /// How far from the camera shadows are drawn, in metres. Well short of the
    /// render distance on purpose: a shadow at three hundred metres is smaller
    /// than a pixel.
    float distance;
};

constexpr std::array<ShadowQualityLevel, 4> kShadowQualityLevels{{
    {0, 0, 0.0f},
    {1024, 2, 64.0f},
    {1536, 3, 128.0f},
    {2048, 4, 192.0f},
}};

/// **No quality level may ask for more cascades than the shadow map holds.**
///
/// The `4` in the last row above and the `4` in `ShadowMap::kMaxCascades` are
/// two unrelated literals, and only one direction of disagreement is caught.
/// Raising `kMaxCascades` alone fails the `static_assert` near the top of this
/// file. **Raising this table alone fails nothing** - that assert still sees
/// `kMaxCascades == 4`, because this edit does not touch it - and the damage is
/// at run time, writing a fifth matrix into a four-element array and a fifth
/// texel scale into a `glm::vec4`.
///
/// Both sides are read here, so the pair cannot drift apart silently again.
constexpr bool shadowQualityFitsCascadeBudget() {
    for (const ShadowQualityLevel& level : kShadowQualityLevels) {
        if (level.cascades > ShadowMap::kMaxCascades) {
            return false;
        }
    }
    return true;
}

static_assert(shadowQualityFitsCascadeBudget(),
              "a shadow quality level asks for more cascades than ShadowMap::kMaxCascades holds; "
              "raise kMaxCascades, frame.glsl's shadowMatrices[] and m_shadowTexelWorld together");

/// Pulled this far back behind each cascade, so something standing between the
/// sun and the slice still casts into it even though it is far outside the
/// camera's own view. The world is 96 blocks tall, so this clears all of it.
constexpr float kShadowCasterExtent = 128.0f;

/// Nearest distance a cascade starts at. Not the camera's near plane: splitting
/// logarithmically from 0.1 m would spend the first cascade on the inside of the
/// player's own nose.
constexpr float kShadowNear = 1.0f;

/// How far the split distances lean toward a logarithmic series rather than an
/// even one. Logarithmic gives the near cascade the detail; even gives the far
/// ones enough to work with. Neither alone is right.
constexpr float kShadowSplitBlend = 0.7f;

/// Subtracted from every shadow lookup. Small, because the slope-scaled bias in
/// the pipeline and the normal offset in the shader do most of the work.
constexpr float kShadowDepthBias = 0.0015f;

/// Below this the sun is at the horizon: its own strength has already faded to
/// nothing, and the shadows it would cast stretch the length of the world.
constexpr float kShadowMinSunHeight = 0.05f;

VkExtent2D toVkExtent(Extent2D extent) {
    return VkExtent2D{extent.width, extent.height};
}

/// The six clip-space planes of a view-projection matrix, each as (normal, d)
/// with the normal pointing inward.
///
/// Gribb-Hartmann: adding or subtracting a row of the matrix from the w row
/// gives a plane directly, because clip-space testing is exactly those
/// comparisons. GLM is column-major, so a "row" here reads across columns. The
/// near plane is row 2 alone rather than w+2 because the project builds with
/// `GLM_FORCE_DEPTH_ZERO_TO_ONE`, which is what Vulkan expects.
std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& viewProjection) {
    const glm::mat4& m = viewProjection;
    const glm::vec4 rowX{m[0][0], m[1][0], m[2][0], m[3][0]};
    const glm::vec4 rowY{m[0][1], m[1][1], m[2][1], m[3][1]};
    const glm::vec4 rowZ{m[0][2], m[1][2], m[2][2], m[3][2]};
    const glm::vec4 rowW{m[0][3], m[1][3], m[2][3], m[3][3]};

    std::array<glm::vec4, 6> planes{rowW + rowX, rowW - rowX, rowW + rowY, rowW - rowY, rowZ, rowW - rowZ};

    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3{plane});
        if (length > 0.0f) {
            plane /= length;
        }
    }
    return planes;
}

/// Conservative: false only when the box is provably outside. Tests the box
/// corner furthest along each plane normal, so a box straddling a plane counts
/// as visible.
bool boxInFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& min, const glm::vec3& max) {
    for (const glm::vec4& plane : planes) {
        const glm::vec3 nearestInside{plane.x >= 0.0f ? max.x : min.x, plane.y >= 0.0f ? max.y : min.y,
                                      plane.z >= 0.0f ? max.z : min.z};
        if (glm::dot(glm::vec3{plane}, nearestInside) + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}

VkImageMemoryBarrier makeColorImageBarrier(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                                           VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    return barrier;
}

/// Reorders screen geometry farthest-first, so blending alone decides what
/// covers what.
///
/// The HUD carries a hand-assigned depth per element - forty-odd distinct
/// bands, each with a comment saying why it sits where it does. That table is
/// already a correct back-to-front order; this does not invent one, it executes
/// the one that is written down. Sorting whole triangles rather than quads means
/// it makes no assumption about how the geometry was built, and a stable sort
/// leaves elements sharing a depth in the order they were appended.
void sortScreenIndices(const MeshData& mesh, std::vector<std::uint32_t>& order,
                       std::vector<std::uint32_t>& out) {
    const std::size_t triangles = mesh.indices.size() / 3;
    out.assign(mesh.indices.begin(), mesh.indices.end());
    if (triangles < 2) {
        return;
    }

    const auto depthOf = [&](std::uint32_t triangle) {
        const std::size_t base = static_cast<std::size_t>(triangle) * 3;
        return std::max({mesh.vertices[mesh.indices[base + 0]].position[2],
                         mesh.vertices[mesh.indices[base + 1]].position[2],
                         mesh.vertices[mesh.indices[base + 2]].position[2]});
    };

    order.resize(triangles);
    for (std::size_t i = 0; i < triangles; ++i) {
        order[i] = static_cast<std::uint32_t>(i);
    }
    std::stable_sort(order.begin(), order.end(),
                     [&](std::uint32_t left, std::uint32_t right) { return depthOf(left) > depthOf(right); });

    for (std::size_t i = 0; i < triangles; ++i) {
        const std::size_t from = static_cast<std::size_t>(order[i]) * 3;
        out[i * 3 + 0] = mesh.indices[from + 0];
        out[i * 3 + 1] = mesh.indices[from + 1];
        out[i * 3 + 2] = mesh.indices[from + 2];
    }
}

} // namespace

Renderer::Renderer(const VulkanContext& context, Window& window,
                   const std::vector<std::filesystem::path>& blockTextures,
                   const std::filesystem::path& hudTexture, const std::filesystem::path& fontTexture,
                   const std::filesystem::path& skinTexture)
    : m_context(context), m_window(window), m_swapchain(context, toVkExtent(window.framebufferExtent())),
      m_depthImage(std::make_unique<DepthImage>(context, m_swapchain.extent())) {
    createCommandResources();

    // Order matters: the texture upload needs the command pool, and the pipeline
    // needs the descriptor set layout that describes it.
    m_blockTextures = std::make_unique<TextureArray>(context, m_commandPool, blockTextures);
    m_hudTexture = std::make_unique<TextureArray>(context, m_commandPool, std::vector{hudTexture});
    m_fontTexture = std::make_unique<TextureArray>(context, m_commandPool, std::vector{fontTexture});
    m_skinTexture = std::make_unique<TextureArray>(context, m_commandPool, std::vector{skinTexture});
    createFrameUniformBuffers();
    createDescriptorResources();

    // 16 MB holds a good many chunk meshes at once. Bigger only raises the
    // memory floor; smaller just means more submissions.
    m_uploads = std::make_unique<UploadContext>(context, 16 * 1024 * 1024);

    createPostSamplers();
    // Before the render targets: writing the post descriptor sets points one of
    // their bindings at the shadow map, so it has to exist by then.
    createShadowResources();
    createRenderTargets();

    m_trianglePipeline = std::make_unique<GraphicsPipeline>(
        context.device(), PipelineDesc{
                              .vertexSpirv = executableDirectory() / "shaders" / "triangle.vert.spv",
                              .fragmentSpirv = executableDirectory() / "shaders" / "triangle.frag.spv",
                              .colorFormats = {kSceneColorFormat},
                              .depthFormat = m_depthImage->format(),
                              .descriptorSetLayout = m_descriptorSetLayout,
                              .pushConstantBytes = sizeof(MeshPushConstants),
                              // **These four are spelled out although they are
                              // the `PipelineDesc` defaults**, because an audit
                              // and two reviews have each had to go and look
                              // them up to find out what this pipeline does,
                              // and one of them then proposed enabling culling
                              // that was already on. They are load-bearing, not
                              // incidental, so they should not follow a change
                              // to the defaults either:
                              //
                              // Culling back faces is what presents one shell
                              // face per water volume from any angle - the
                              // mesher never marks a translucent quad
                              // double-sided, so each carries exactly one
                              // outward winding.
                              .cullMode = VK_CULL_MODE_BACK_BIT,
                              .depthTest = true,
                              // **Writes depth while blending**, which is the
                              // trap that has cost this project most. Kept on
                              // purpose: the rain draw below rejects drops
                              // behind a lake surface, which only works if the
                              // water stamped its distance. The costs are paid
                              // elsewhere instead - `triangle.frag` cutouts
                              // near-zero alpha so a hole cannot stamp depth,
                              // and the translucent loop is sorted farthest
                              // first so one water chunk cannot reject another.
                              .depthWrite = true,
                              .blend = BlendMode::Alpha,
                          });

    m_gbufferPipeline = std::make_unique<GraphicsPipeline>(
        context.device(),
        PipelineDesc{
            .vertexSpirv = executableDirectory() / "shaders" / "triangle.vert.spv",
            .fragmentSpirv = executableDirectory() / "shaders" / "gbuffer.frag.spv",
            .colorFormats = {kGbufferAlbedoFormat, kGbufferLightFormat, kGbufferMaterialFormat},
            .depthFormat = m_depthImage->format(),
            .descriptorSetLayout = m_descriptorSetLayout,
            .pushConstantBytes = sizeof(MeshPushConstants),
            // **Blending off, and that is not a tuning.** A blended normal is
            // not a normal and a blended roughness is not a roughness; the
            // G-buffer is written opaque or alpha-tested, never mixed.
            .blend = BlendMode::None,
        });

    m_deferredPipeline = std::make_unique<GraphicsPipeline>(
        context.device(), PipelineDesc{
                              .vertexSpirv = executableDirectory() / "shaders" / "fullscreen.vert.spv",
                              .fragmentSpirv = executableDirectory() / "shaders" / "deferred.frag.spv",
                              .colorFormats = {kSceneColorFormat},
                              .depthFormat = VK_FORMAT_UNDEFINED,
                              .descriptorSetLayout = m_deferredSetLayout,
                              .pushConstantBytes = sizeof(PostPushConstants),
                              .vertexInput = false,
                              .cullMode = VK_CULL_MODE_NONE,
                              .depthTest = false,
                              .depthWrite = false,
                              .blend = BlendMode::None,
                          });

    m_hudPipeline = std::make_unique<GraphicsPipeline>(
        context.device(), PipelineDesc{
                              // **Its own vertex stage, not the world's.** The
                              // interface reads three varyings; `triangle.vert`
                              // emits seven and runs the water wave and the
                              // wind sway on the way, neither of which a screen
                              // quad can ever want.
                              .vertexSpirv = executableDirectory() / "shaders" / "hud.vert.spv",
                              .fragmentSpirv = executableDirectory() / "shaders" / "hud.frag.spv",
                              .colorFormats = {m_swapchain.imageFormat()},
                              // No depth attachment at all: order decides what
                              // covers what, and the painter's sort below is
                              // what produces that order.
                              .depthFormat = VK_FORMAT_UNDEFINED,
                              .descriptorSetLayout = m_descriptorSetLayout,
                              .pushConstantBytes = sizeof(MeshPushConstants),
                              // Back-face culling stays. HUD quads are emitted
                              // with *both* windings, so drawing both sides
                              // would blend every panel over itself and turn an
                              // alpha of 0.55 into 0.80.
                              .cullMode = VK_CULL_MODE_BACK_BIT,
                              .depthTest = false,
                              .depthWrite = false,
                          });

    m_cloudPipeline = std::make_unique<GraphicsPipeline>(
        context.device(),
        PipelineDesc{
            .vertexSpirv = executableDirectory() / "shaders" / "fullscreen.vert.spv",
            .fragmentSpirv = executableDirectory() / "shaders" / "clouds.frag.spv",
            .colorFormats = {kSceneColorFormat},
            .depthFormat = m_depthImage->format(),
            // The main set, for the frame block alone - the deck is procedural,
            // so there is no cloud texture to bind.
            .descriptorSetLayout = m_descriptorSetLayout,
            .pushConstantBytes = sizeof(MeshPushConstants),
            .vertexInput = false,
            .cullMode = VK_CULL_MODE_NONE,
            // Tests, never writes. A cloud must not stamp its distance over the
            // water and the block outline that are drawn after it.
            .depthTest = true,
            .depthWrite = false,
        });

    m_precipitationPipeline = std::make_unique<GraphicsPipeline>(
        context.device(),
        PipelineDesc{
            .vertexSpirv = executableDirectory() / "shaders" / "triangle.vert.spv",
            .fragmentSpirv = executableDirectory() / "shaders" / "precipitation.frag.spv",
            .colorFormats = {kSceneColorFormat},
            .depthFormat = m_depthImage->format(),
            .descriptorSetLayout = m_descriptorSetLayout,
            .pushConstantBytes = sizeof(MeshPushConstants),
            // A curtain quad is seen from both sides as you turn, and there is
            // no inside to it.
            .cullMode = VK_CULL_MODE_NONE,
            // Tests, never writes. A blended fragment that stamps its distance
            // punches a rectangular hole through everything drawn after it.
            .depthTest = true,
            .depthWrite = false,
        });

    const PipelineDesc fullScreenBase{
        .vertexSpirv = executableDirectory() / "shaders" / "fullscreen.vert.spv",
        .fragmentSpirv = {},
        .colorFormats = {},
        .depthFormat = VK_FORMAT_UNDEFINED,
        .descriptorSetLayout = VK_NULL_HANDLE,
        .pushConstantBytes = sizeof(PostPushConstants),
        .vertexInput = false,
        .cullMode = VK_CULL_MODE_NONE,
        .depthTest = false,
        .depthWrite = false,
        .blend = BlendMode::None,
    };

    PipelineDesc bloomDown = fullScreenBase;
    bloomDown.fragmentSpirv = executableDirectory() / "shaders" / "bloom_down.frag.spv";
    bloomDown.colorFormats = {kBloomFormat};
    bloomDown.descriptorSetLayout = m_postSetLayout;
    m_bloomDownPipeline = std::make_unique<GraphicsPipeline>(context.device(), bloomDown);

    PipelineDesc bloomUp = bloomDown;
    bloomUp.fragmentSpirv = executableDirectory() / "shaders" / "bloom_up.frag.spv";
    // The pyramid accumulates: each coarse mip is added into the finer one that
    // is already there, which is why this pass loads rather than clears.
    bloomUp.blend = BlendMode::Additive;
    m_bloomUpPipeline = std::make_unique<GraphicsPipeline>(context.device(), bloomUp);

    PipelineDesc toneMap = fullScreenBase;
    toneMap.fragmentSpirv = executableDirectory() / "shaders" / "tonemap.frag.spv";
    toneMap.colorFormats = {m_swapchain.imageFormat()};
    toneMap.descriptorSetLayout = m_toneMapSetLayout;
    m_toneMapPipeline = std::make_unique<GraphicsPipeline>(context.device(), toneMap);

    createSyncObjects();
    createTimestampPool();
}

void Renderer::createPostSamplers() {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // Linear, unlike the world's NEAREST: these are screen-sized images being
    // deliberately blurred, not 16x16 art whose blockiness is the whole look.
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    vkCheck(vkCreateSampler(m_context.device(), &samplerInfo, nullptr, &m_borderSampler), "vkCreateSampler");

    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCheck(vkCreateSampler(m_context.device(), &samplerInfo, nullptr, &m_edgeSampler), "vkCreateSampler");

    // **Nearest, and only the depth buffer uses it.** The G-buffer bindings are
    // linear and harmless because `deferred.frag` samples them at `fragUv`
    // alone, which lands on exact texel centres; the contact-shadow taps do
    // not, so on depth the filtering is really running. Blending two depths
    // across a silhouette produces a value that unprojects to a point on
    // neither surface, and it is also a format feature Vulkan does not require
    // a depth format to have - see the member's own note.
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    vkCheck(vkCreateSampler(m_context.device(), &samplerInfo, nullptr, &m_pointSampler), "vkCreateSampler");

    // One sampled image for the two bloom passes, two for the tone mapper.
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    vkCheck(vkCreateDescriptorSetLayout(m_context.device(), &layoutInfo, nullptr, &m_postSetLayout),
            "vkCreateDescriptorSetLayout");

    std::array<VkDescriptorSetLayoutBinding, 2> toneMapBindings{binding, binding};
    toneMapBindings[1].binding = 1;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(toneMapBindings.size());
    layoutInfo.pBindings = toneMapBindings.data();
    vkCheck(vkCreateDescriptorSetLayout(m_context.device(), &layoutInfo, nullptr, &m_toneMapSetLayout),
            "vkCreateDescriptorSetLayout");

    // Sized for the largest pyramid rather than the current one, so a window
    // resize never has to rebuild the pool.
    constexpr std::uint32_t kPostSets = kMaxBloomMips * 2 + 1 + kFramesInFlight;
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = kMaxBloomMips * 2 + 2 + kFramesInFlight * 5;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = kFramesInFlight;

    // **A tripwire on the descriptor limits, in the same spirit as the
    // `maxImageArrayLayers` one in `TextureArray::createResources` - but on the
    // number that is actually bounded.**
    //
    // Two limits exist and neither of them bounds a pool.
    // `maxPerStageDescriptorSampledImages` (guaranteed floor 16) and
    // `maxDescriptorSetSampledImages` (floor 96) both limit what a single
    // *descriptor set layout* may declare; a `VkDescriptorPoolSize::
    // descriptorCount` is a budget handed out across many sets and the spec
    // places neither limit on it. This asserted `poolSizes[0].descriptorCount`
    // against 96 and named the per-stage limit in its own comment without ever
    // checking it, so it sat at 28 against a ceiling three and a half times
    // higher, measuring the wrong quantity, and would have stayed green through
    // exactly the change it claimed to catch. A second assert compared
    // `kPostSets` against the same 96, which has no meaning for `maxSets` at
    // all.
    //
    // What is genuinely at risk is the widest single set, and that is the world
    // set rather than anything in this function: `kTextureBindingCount` sheets
    // plus the scene copy plus the sun's shadow map, all declared to the
    // fragment stage at once. Six today against a floor of sixteen, and a
    // seventh sampler is one line in `createDescriptorResources`. Every post
    // set is far smaller - one image for a bloom mip, two for the tone map,
    // five for the deferred pass.
    constexpr std::uint32_t kWorldSetSampledImages = kTextureBindingCount + 2;
    static_assert(kWorldSetSampledImages <= 16,
                  "The world descriptor set declares more sampled images than Vulkan's guaranteed "
                  "maxPerStageDescriptorSampledImages of 16. Adding a sampler to the world set grew this - "
                  "either drop one or check the limit against the device at runtime.");

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kPostSets;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    vkCheck(vkCreateDescriptorPool(m_context.device(), &poolInfo, nullptr, &m_postPool), "vkCreateDescriptorPool");

    const std::vector<VkDescriptorSetLayout> singleLayouts(kMaxBloomMips * 2, m_postSetLayout);
    m_bloomDownSets.resize(kMaxBloomMips);
    m_bloomUpSets.resize(kMaxBloomMips);

    std::vector<VkDescriptorSet> allocated(kMaxBloomMips * 2);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_postPool;
    allocInfo.descriptorSetCount = static_cast<std::uint32_t>(singleLayouts.size());
    allocInfo.pSetLayouts = singleLayouts.data();
    vkCheck(vkAllocateDescriptorSets(m_context.device(), &allocInfo, allocated.data()), "vkAllocateDescriptorSets");
    for (std::uint32_t i = 0; i < kMaxBloomMips; ++i) {
        m_bloomDownSets[i] = allocated[i];
        m_bloomUpSets[i] = allocated[kMaxBloomMips + i];
    }

    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_toneMapSetLayout;
    vkCheck(vkAllocateDescriptorSets(m_context.device(), &allocInfo, &m_toneMapSet), "vkAllocateDescriptorSets");

    // The deferred pass: three G-buffer images, depth, the frame block, and the
    // sun's shadow map.
    std::array<VkDescriptorSetLayoutBinding, 6> deferredBindings{};
    for (std::uint32_t index = 0; index < 4; ++index) {
        deferredBindings[index] = binding;
        deferredBindings[index].binding = index;
    }
    deferredBindings[4].binding = 4;
    deferredBindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    deferredBindings[4].descriptorCount = 1;
    deferredBindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    deferredBindings[5] = binding;
    deferredBindings[5].binding = 5;

    layoutInfo.bindingCount = static_cast<std::uint32_t>(deferredBindings.size());
    layoutInfo.pBindings = deferredBindings.data();
    vkCheck(vkCreateDescriptorSetLayout(m_context.device(), &layoutInfo, nullptr, &m_deferredSetLayout),
            "vkCreateDescriptorSetLayout");

    const std::vector<VkDescriptorSetLayout> deferredLayouts(kFramesInFlight, m_deferredSetLayout);
    m_deferredSets.resize(kFramesInFlight);
    allocInfo.descriptorSetCount = kFramesInFlight;
    allocInfo.pSetLayouts = deferredLayouts.data();
    vkCheck(vkAllocateDescriptorSets(m_context.device(), &allocInfo, m_deferredSets.data()),
            "vkAllocateDescriptorSets");
}

void Renderer::createShadowResources() {
    const ShadowQualityLevel level =
        kShadowQualityLevels[static_cast<std::size_t>(std::clamp(m_shadowQuality, 0, kShadowQualityCount - 1))];

    // Level 0 still builds the smallest possible map. A descriptor cannot be
    // left unwritten and a shader cannot be told to skip a binding it declares,
    // so "off" is a live cascade count of zero rather than a missing image.
    m_shadowDistance = level.distance;
    m_shadowMap = std::make_unique<ShadowMap>(m_context, level.resolution > 0 ? level.resolution : 1,
                                              level.cascades > 0 ? level.cascades : 1);

    m_shadowPipeline = std::make_unique<GraphicsPipeline>(
        m_context.device(),
        PipelineDesc{
            .vertexSpirv = executableDirectory() / "shaders" / "shadow.vert.spv",
            .fragmentSpirv = executableDirectory() / "shaders" / "shadow.frag.spv",
            // Depth is the whole output. There is no colour attachment at all.
            .colorFormats = {},
            .depthFormat = m_shadowMap->format(),
            .descriptorSetLayout = m_descriptorSetLayout,
            .pushConstantBytes = sizeof(MeshPushConstants),
            .vertexInput = true,
            // **Culling nothing, and that is not laziness.** A voxel mesh is a
            // hollow shell: the mesher never emits the face between two solid
            // blocks, so the far side of a hill does not exist. Front-face
            // culling is the usual cure for shadow acne and here it would leave
            // flat ground casting no shadow whatsoever.
            .cullMode = VK_CULL_MODE_NONE,
            .depthTest = true,
            .depthWrite = true,
            .blend = BlendMode::None,
            .depthBiasConstant = 1.5f,
            .depthBiasSlope = 3.0f,
        });
}

void Renderer::updateShadowCascades(const glm::mat4& view) {
    m_liveShadowCascades = 0;
    if (m_shadowQuality <= 0 || m_shadowMap == nullptr || m_sunDirection.y < kShadowMinSunHeight) {
        return;
    }

    const std::uint32_t cascades = m_shadowMap->cascades();
    const float resolution = static_cast<float>(m_shadowMap->resolution());
    const float shadowFar = std::max(std::min(m_shadowDistance, m_farPlane), kShadowNear * 2.0f);
    const glm::mat4 inverseViewProjection = glm::inverse(projectionMatrix() * view);

    // The four rays from the near plane's corners to the far plane's. Every
    // cascade is a pair of slices along them, so they are found once.
    const glm::vec2 screenCorners[4]{{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};
    glm::vec3 nearCorners[4];
    glm::vec3 farCorners[4];
    for (int i = 0; i < 4; ++i) {
        // Not named `near`/`far`: those are macros in the Windows headers Vulkan
        // pulls in on this platform.
        const glm::vec4 nearPoint = inverseViewProjection * glm::vec4{screenCorners[i], 0.0f, 1.0f};
        const glm::vec4 farPoint = inverseViewProjection * glm::vec4{screenCorners[i], 1.0f, 1.0f};
        nearCorners[i] = glm::vec3{nearPoint} / nearPoint.w;
        farCorners[i] = glm::vec3{farPoint} / farPoint.w;
    }

    const glm::vec3 lightDirection = -glm::normalize(m_sunDirection);
    const glm::vec3 up =
        std::abs(lightDirection.y) > 0.99f ? glm::vec3{0.0f, 0.0f, 1.0f} : glm::vec3{0.0f, 1.0f, 0.0f};
    const glm::mat4 lightRotation = glm::lookAt(glm::vec3{0.0f}, lightDirection, up);
    const glm::mat4 inverseLightRotation = glm::inverse(lightRotation);

    float sliceNear = kShadowNear;
    for (std::uint32_t cascade = 0; cascade < cascades; ++cascade) {
        const float fraction = static_cast<float>(cascade + 1) / static_cast<float>(cascades);
        const float logarithmic = kShadowNear * std::pow(shadowFar / kShadowNear, fraction);
        const float even = kShadowNear + (shadowFar - kShadowNear) * fraction;
        const float sliceFar = kShadowSplitBlend * logarithmic + (1.0f - kShadowSplitBlend) * even;

        const float span = m_farPlane - kNearPlane;
        const float tNear = (sliceNear - kNearPlane) / span;
        const float tFar = (sliceFar - kNearPlane) / span;

        glm::vec3 corners[8];
        for (int i = 0; i < 4; ++i) {
            const glm::vec3 ray = farCorners[i] - nearCorners[i];
            corners[i] = nearCorners[i] + ray * tNear;
            corners[i + 4] = nearCorners[i] + ray * tFar;
        }

        // A sphere, not a box. A box fitted to the slice changes size as the
        // camera turns, so the shadow map's scale changes with it and every edge
        // in the world crawls. A sphere is the same whichever way you look.
        glm::vec3 centre{0.0f};
        for (const glm::vec3& corner : corners) {
            centre += corner;
        }
        centre /= 8.0f;
        float radius = 0.0f;
        for (const glm::vec3& corner : corners) {
            radius = std::max(radius, glm::length(corner - centre));
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // Snapped to whole texels in the light's own frame. Without this the map
        // slides a fraction of a texel every time the player moves, and every
        // shadow edge shimmers.
        const float texelsPerUnit = resolution / (2.0f * radius);
        glm::vec3 lightSpaceCentre = glm::vec3{lightRotation * glm::vec4{centre, 1.0f}};
        lightSpaceCentre.x = std::floor(lightSpaceCentre.x * texelsPerUnit) / texelsPerUnit;
        lightSpaceCentre.y = std::floor(lightSpaceCentre.y * texelsPerUnit) / texelsPerUnit;
        const glm::vec3 snapped = glm::vec3{inverseLightRotation * glm::vec4{lightSpaceCentre, 1.0f}};

        const glm::mat4 lightView =
            glm::lookAt(snapped - lightDirection * (radius + kShadowCasterExtent), snapped, up);
        const glm::mat4 lightProjection =
            glm::ortho(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + kShadowCasterExtent);

        m_shadowMatrices[cascade] = lightProjection * lightView;
        m_shadowTexelWorld[static_cast<glm::length_t>(cascade)] = 2.0f * radius / resolution;
        sliceNear = sliceFar;
    }

    m_liveShadowCascades = cascades;
}

void Renderer::setShadowQuality(int quality) {
    const int clamped = std::clamp(quality, 0, kShadowQualityCount - 1);
    if (clamped == m_shadowQuality && m_shadowMap != nullptr) {
        return;
    }
    m_shadowQuality = clamped;
    if (m_shadowMap == nullptr) {
        return;
    }

    // Frames already submitted may still be sampling the map that is about to be
    // destroyed. A full stall is the right price here: this happens when a
    // player changes a setting, not per frame.
    vkDeviceWaitIdle(m_context.device());
    createShadowResources();
    writePostDescriptorSets();
}

VkExtent2D Renderer::renderExtent() const {
    const VkExtent2D full = m_swapchain.extent();
    if (m_renderScale >= 0.999f) {
        return full;
    }
    // Rounded to an even number on each axis, because the bloom pyramid halves
    // this and an odd width leaves its first mip half a texel out of step with
    // the image it was filtered from.
    const auto scaled = [](std::uint32_t value, float scale) {
        const auto out = static_cast<std::uint32_t>(static_cast<float>(value) * scale + 0.5f);
        return std::max(2u, out & ~1u);
    };
    return VkExtent2D{scaled(full.width, m_renderScale), scaled(full.height, m_renderScale)};
}

void Renderer::setRenderScale(float scale) {
    const float clamped = std::clamp(scale, 0.5f, 1.0f);
    if (std::abs(clamped - m_renderScale) < 0.001f) {
        return;
    }
    m_renderScale = clamped;
    // The same work a resize does, for the same reason: every offscreen image
    // changes size and the descriptor sets name views that are about to be
    // destroyed. A full stall is the right price for something a player changes
    // between matches, not between frames.
    vkDeviceWaitIdle(m_context.device());
    m_depthImage = std::make_unique<DepthImage>(m_context, renderExtent());
    createRenderTargets();
}

void Renderer::createRenderTargets() {
    const VkExtent2D extent = renderExtent();
    m_sceneColor = std::make_unique<RenderTarget>(
        m_context, extent, kSceneColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    m_sceneCopy = std::make_unique<RenderTarget>(
        m_context, extent, kSceneColorFormat,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

    constexpr VkImageUsageFlags kGbufferUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    m_gbufferAlbedo = std::make_unique<RenderTarget>(m_context, extent, kGbufferAlbedoFormat, kGbufferUsage);
    m_gbufferLight = std::make_unique<RenderTarget>(m_context, extent, kGbufferLightFormat, kGbufferUsage);
    m_gbufferMaterial = std::make_unique<RenderTarget>(m_context, extent, kGbufferMaterialFormat, kGbufferUsage);

    // Starting the pyramid at half resolution: the first downsample is the
    // expensive one and nothing is lost, because bloom is a blur by definition.
    const VkExtent2D bloomExtent{std::max(1u, extent.width / 2), std::max(1u, extent.height / 2)};
    m_bloom = std::make_unique<RenderTarget>(m_context, bloomExtent, kBloomFormat,
                                             VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                             RenderTarget::mipCountFor(bloomExtent, kMaxBloomMips));

    writePostDescriptorSets();
}

void Renderer::writePostDescriptorSets() {
    const std::uint32_t mips = m_bloom->mipLevels();

    // **One owner for both counts.** Every `write` below pushes exactly one
    // entry to each vector, so the two stay index-for-index aligned; the fix-up
    // loop at the bottom depends on that and indexes `images` by a `writes`
    // index. The per-frame figure is 2 world-set bindings (scene copy, sun
    // shadow) plus 5 deferred ones (albedo, light, material, depth, shadow).
    // Deriving the reserve from the same constant the guard checks is what
    // stops the two drifting - they already had, silently: this reserve read
    // `kFramesInFlight * 5` against 6 actual writes before `kSunShadowBinding`
    // existed, and an under-reserve is invisible because `push_back` simply
    // reallocates.
    constexpr std::size_t kPerFrameImageWrites = 7;
    constexpr std::size_t kImageWriteCount =
        kMaxBloomMips * 2 + 2 + static_cast<std::size_t>(kFramesInFlight) * kPerFrameImageWrites;
    constexpr std::size_t kBufferWriteCount = kFramesInFlight;

    std::vector<VkDescriptorImageInfo> images;
    images.reserve(kImageWriteCount);
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(kImageWriteCount + kBufferWriteCount);

    const auto write = [&](VkDescriptorSet set, std::uint32_t binding, VkImageView view, VkSampler sampler,
                           VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        VkDescriptorImageInfo info{};
        info.imageLayout = layout;
        info.imageView = view;
        info.sampler = sampler;
        images.push_back(info);

        VkWriteDescriptorSet entry{};
        entry.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        entry.dstSet = set;
        entry.dstBinding = binding;
        entry.descriptorCount = 1;
        entry.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes.push_back(entry);
    };

    // Every set is written, including any beyond the current mip count: a set
    // left pointing at an image view destroyed by the last resize is a fault
    // waiting for the window to be made larger again.
    for (std::uint32_t i = 0; i < kMaxBloomMips; ++i) {
        const bool inRange = i < mips;
        const VkImageView source =
            (i == 0 || !inRange) ? m_sceneColor->view(0) : m_bloom->view(std::min(i - 1, mips - 1));
        write(m_bloomDownSets[i], 0, source, m_borderSampler);
    }
    for (std::uint32_t i = 0; i < kMaxBloomMips; ++i) {
        write(m_bloomUpSets[i], 0, m_bloom->view(std::min(i, mips - 1)), m_edgeSampler);
    }
    write(m_toneMapSet, 0, m_sceneColor->view(0), m_edgeSampler);
    write(m_toneMapSet, 1, m_bloom->view(0), m_edgeSampler);

    // The world set carries the scene copy too, because the forward pass is the
    // one that reads it and that pass uses the world set. Written here rather
    // than with the sheets, since a resize replaces the image.
    //
    // The shadow map is written here for the matching reason: `setShadowQuality`
    // destroys the old `ShadowMap` and calls this, so both sets that name it are
    // rewritten in one place. **This and the deferred write below are its only
    // two writers** - `createShadowResources` writes no descriptors at all.
    for (std::uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        write(m_descriptorSets[frame], kSceneCopyBinding, m_sceneCopy->view(0), m_edgeSampler);
        write(m_descriptorSets[frame], kSunShadowBinding, m_shadowMap->arrayView(), m_shadowMap->sampler());
    }

    for (std::uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        write(m_deferredSets[frame], 0, m_gbufferAlbedo->view(0), m_edgeSampler);
        write(m_deferredSets[frame], 1, m_gbufferLight->view(0), m_edgeSampler);
        write(m_deferredSets[frame], 2, m_gbufferMaterial->view(0), m_edgeSampler);
        // Depth is sampled while it sits in the layout a read-only depth
        // attachment uses, not the ordinary one - the descriptor has to name the
        // same layout the barrier put it in. **Point-sampled, unlike the three
        // above**: this is the one binding read at sub-texel offsets.
        write(m_deferredSets[frame], 3, m_depthImage->view(), m_pointSampler,
              VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
        // **The only writer of deferred binding 5.** `createShadowResources`
        // builds the `ShadowMap` and the shadow pipeline and writes no
        // descriptors at all, so `setShadowQuality` depends on this call
        // following it: it destroys the old image view, and without this write
        // the set would still name it. Deleting this line as redundant leaves a
        // destroyed view bound the moment shadow quality changes. The world
        // set's `kSunShadowBinding` above names the same image for the forward
        // pass and is written for the same reason.
        write(m_deferredSets[frame], 5, m_shadowMap->arrayView(), m_shadowMap->sampler());
    }

    // A tripwire, not a gate - the same shape as the push-constant one in
    // `createPipelines`. The loop below indexes `images` with a `writes` index,
    // which is only sound while the two are the same length, and that holds for
    // exactly one reason: `write` is the only thing that has pushed so far and
    // it pushes one entry to each. **The buffer writes below deliberately grow
    // `writes` past `images`**, which is why the fix-up has to run here rather
    // than at the end - move it after them and `&images[i]` walks off the end,
    // handing Vulkan a dangling pointer with nothing on screen to say so.
    // Checking the constant as well as the equality catches the other half: a
    // `write` added or removed without the count above being updated.
    if (writes.size() != images.size() || images.size() != kImageWriteCount) {
        throw std::runtime_error(
            "Post-process descriptor writes are out of step: " + std::to_string(writes.size()) +
            " writes against " + std::to_string(images.size()) + " image infos, expected " +
            std::to_string(kImageWriteCount) +
            " of each. Every image write must go through the `write` lambda, and any buffer write "
            "must come after the pImageInfo fix-up loop.");
    }

    // pImageInfo is filled only now: `images` reallocates as it grows, so any
    // pointer taken earlier would dangle.
    for (std::size_t i = 0; i < writes.size(); ++i) {
        writes[i].pImageInfo = &images[i];
    }

    // The frame block rides in the same set, so it is written here too.
    std::array<VkDescriptorBufferInfo, kFramesInFlight> frameBuffers{};
    for (std::uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        frameBuffers[frame].buffer = m_frameUniformBuffers[frame]->handle();
        frameBuffers[frame].offset = 0;
        frameBuffers[frame].range = sizeof(FrameUniforms);

        VkWriteDescriptorSet entry{};
        entry.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        entry.dstSet = m_deferredSets[frame];
        entry.dstBinding = 4;
        entry.descriptorCount = 1;
        entry.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        entry.pBufferInfo = &frameBuffers[frame];
        writes.push_back(entry);
    }

    vkUpdateDescriptorSets(m_context.device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0,
                           nullptr);
}

void Renderer::createTimestampPool() {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_context.physicalDevice(), &properties);

    // A tripwire, not a gate. The block fits inside Vulkan's 128-byte guarantee
    // now, so this can only fire if someone grows it again - which is exactly
    // when a clear message is worth having, because the alternative is corrupt
    // draws on a device that reports the minimum.
    if (properties.limits.maxPushConstantsSize < sizeof(MeshPushConstants)) {
        throw std::runtime_error("GPU allows only " +
                                 std::to_string(properties.limits.maxPushConstantsSize) +
                                 " bytes of push constants; this renderer needs " +
                                 std::to_string(sizeof(MeshPushConstants)));
    }

    m_maxMemoryAllocations = properties.limits.maxMemoryAllocationCount;

    // A period of zero means the device does not support timestamps at all.
    m_timestampPeriodNanoseconds = properties.limits.timestampPeriod;
    m_timestampsSupported = m_timestampPeriodNanoseconds > 0.0f;
    if (!m_timestampsSupported) {
        logWarn("GPU timestamps unsupported; GPU frame time will read as zero.");
        return;
    }

    VkQueryPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolInfo.queryCount = kFramesInFlight * 2;
    vkCheck(vkCreateQueryPool(m_context.device(), &poolInfo, nullptr, &m_timestampPool), "vkCreateQueryPool");

    m_timestampsPending.assign(kFramesInFlight, false);
}

void Renderer::readGpuTimestamps() {
    if (!m_timestampsSupported || !m_timestampsPending[m_currentFrame]) {
        return;
    }

    // Only safe because the caller has just waited on this frame's fence, so the
    // submission that wrote these timestamps has finished.
    std::uint64_t results[2]{};
    const VkResult result =
        vkGetQueryPoolResults(m_context.device(), m_timestampPool, m_currentFrame * 2, 2, sizeof(results), results,
                              sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);

    if (result == VK_SUCCESS && results[1] > results[0]) {
        const double ticks = static_cast<double>(results[1] - results[0]);
        m_stats.gpuMilliseconds = static_cast<float>(ticks * m_timestampPeriodNanoseconds / 1.0e6);
    }
}

Renderer::~Renderer() {
    // Copies may still be queued against buffers that are about to be freed.
    m_uploads->waitForCompletion();

    // The GPU may still be reading resources we are about to free.
    vkDeviceWaitIdle(m_context.device());

    // Explicit rather than left to member destruction order: everything here
    // owns Vulkan handles that must die while the device is still alive, and
    // the retired list in particular is easy to forget.
    m_retired.clear();
    m_meshes.clear();
    m_freeSlots.clear();
    m_overlayMesh = GpuMesh{};
    m_crackMesh = GpuMesh{};
    m_screenMesh = GpuMesh{};
    m_clippedScreenMesh = GpuMesh{};
    m_topScreenMesh = GpuMesh{};
    m_precipitationMesh = GpuMesh{};
    m_boltMesh = GpuMesh{};
    m_particleMesh = GpuMesh{};
    for (GpuMesh& sky : m_skyMeshes) {
        sky = GpuMesh{};
    }

    // Before the pool: freeing the pool invalidates the set allocated from it.
    m_trianglePipeline.reset();
    m_gbufferPipeline.reset();
    m_deferredPipeline.reset();
    m_hudPipeline.reset();
    m_bloomDownPipeline.reset();
    m_bloomUpPipeline.reset();
    m_toneMapPipeline.reset();
    m_shadowPipeline.reset();
    m_cloudPipeline.reset();
    m_precipitationPipeline.reset();

    m_sceneColor.reset();
    m_sceneCopy.reset();
    m_bloom.reset();
    m_gbufferAlbedo.reset();
    m_gbufferLight.reset();
    m_gbufferMaterial.reset();
    if (m_borderSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_context.device(), m_borderSampler, nullptr);
    }
    if (m_edgeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_context.device(), m_edgeSampler, nullptr);
    }
    if (m_pointSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_context.device(), m_pointSampler, nullptr);
    }
    if (m_postPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_context.device(), m_postPool, nullptr);
    }
    if (m_postSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_context.device(), m_postSetLayout, nullptr);
    }
    if (m_toneMapSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_context.device(), m_toneMapSetLayout, nullptr);
    }
    if (m_deferredSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_context.device(), m_deferredSetLayout, nullptr);
    }

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_context.device(), m_descriptorPool, nullptr);
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_context.device(), m_descriptorSetLayout, nullptr);
    }
    m_frameUniformMapped.clear();
    m_frameUniformBuffers.clear();
    m_materialBuffer.reset();
    m_blockTextures.reset();
    m_hudTexture.reset();
    m_fontTexture.reset();
    m_skinTexture.reset();
    m_uploads.reset();

    destroySyncObjects();

    if (m_timestampPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_context.device(), m_timestampPool, nullptr);
    }

    if (m_commandPool != VK_NULL_HANDLE) {
        // Destroying the pool frees every command buffer allocated from it.
        vkDestroyCommandPool(m_context.device(), m_commandPool, nullptr);
    }
}

void Renderer::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_context.graphicsQueueFamily();
    vkCheck(vkCreateCommandPool(m_context.device(), &poolInfo, nullptr, &m_commandPool), "vkCreateCommandPool");

    m_commandBuffers.resize(kFramesInFlight);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;
    vkCheck(vkAllocateCommandBuffers(m_context.device(), &allocInfo, m_commandBuffers.data()),
            "vkAllocateCommandBuffers");
}

void Renderer::createFrameUniformBuffers() {
    m_frameUniformBuffers.reserve(kFramesInFlight);
    m_frameUniformMapped.reserve(kFramesInFlight);
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        auto buffer = std::make_unique<Buffer>(m_context, sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        m_frameUniformMapped.push_back(buffer->persistentMap());
        m_frameUniformBuffers.push_back(std::move(buffer));
    }

    // Sized from the texture array, because there is exactly one row per layer.
    // Filled with a plausible dielectric so a caller that never supplies a table
    // gets a dull surface rather than a mirror.
    m_materialRowCount = std::max<std::size_t>(1, m_blockTextures->layerCount());
    m_materialBuffer = std::make_unique<Buffer>(
        m_context, m_materialRowCount * sizeof(std::uint32_t),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    constexpr std::uint32_t kDefaultRow = 217u; // roughness 0.85, no metal, no emission
    const std::vector<std::uint32_t> defaults(m_materialRowCount, kDefaultRow);
    uploadBufferData(m_context, m_commandPool, *m_materialBuffer, defaults.data(),
                     defaults.size() * sizeof(std::uint32_t));
}

void Renderer::setMaterialTable(const std::vector<std::uint32_t>& rows) {
    if (rows.empty()) {
        return;
    }
    const std::size_t count = std::min(rows.size(), m_materialRowCount);
    // One-shot and it waits, which is fine: this is called once at startup.
    uploadBufferData(m_context, m_commandPool, *m_materialBuffer, rows.data(), count * sizeof(std::uint32_t));
}

void Renderer::createDescriptorResources() {
    // Bindings 0..3 are the sampled sheets (`kTextureBindingCount`), then this
    // frame's uniform block (`kFrameUniformBinding`), the material table
    // (`kMaterialBinding`), the scene copy (`kSceneCopyBinding`) and the sun's
    // shadow map (`kSunShadowBinding`) - eight in all, and the uniform block is
    // not the last of them. Every one is a named constant rather than a
    // literal, because writing the number out separately in the layout, the
    // pool and the writes is how two of the three get updated and the missed
    // one becomes a validation error.
    std::array<VkDescriptorSetLayoutBinding, kSunShadowBinding + 1> bindings{};
    for (std::uint32_t index = 0; index < kTextureBindingCount; ++index) {
        bindings[index].binding = index;
        bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[index].descriptorCount = 1;
        bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    bindings[kFrameUniformBinding].binding = kFrameUniformBinding;
    bindings[kFrameUniformBinding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[kFrameUniformBinding].descriptorCount = 1;
    // The vertex stage reads it too: a water surface is displaced by the wave
    // field there, and the field's clock lives in this block.
    bindings[kFrameUniformBinding].stageFlags =
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[kMaterialBinding].binding = kMaterialBinding;
    bindings[kMaterialBinding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[kMaterialBinding].descriptorCount = 1;
    bindings[kMaterialBinding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[kSceneCopyBinding].binding = kSceneCopyBinding;
    bindings[kSceneCopyBinding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[kSceneCopyBinding].descriptorCount = 1;
    bindings[kSceneCopyBinding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[kSunShadowBinding].binding = kSunShadowBinding;
    bindings[kSunShadowBinding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[kSunShadowBinding].descriptorCount = 1;
    bindings[kSunShadowBinding].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<std::uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    vkCheck(vkCreateDescriptorSetLayout(m_context.device(), &layoutInfo, nullptr, &m_descriptorSetLayout),
            "vkCreateDescriptorSetLayout");

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // The four sheets, plus the scene copy, plus the shadow map. **Counted off
    // the bindings above rather than written as a number**: a pool one
    // descriptor short does not warn, it fails `vkAllocateDescriptorSets` at
    // startup with `OUT_OF_POOL_MEMORY` and the game never opens a window.
    poolSizes[0].descriptorCount = (kTextureBindingCount + 2) * kFramesInFlight;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = kFramesInFlight;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = kFramesInFlight;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = kFramesInFlight;
    poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    vkCheck(vkCreateDescriptorPool(m_context.device(), &poolInfo, nullptr, &m_descriptorPool),
            "vkCreateDescriptorPool");

    const std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight, m_descriptorSetLayout);
    m_descriptorSets.resize(kFramesInFlight);

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = kFramesInFlight;
    allocInfo.pSetLayouts = layouts.data();
    vkCheck(vkAllocateDescriptorSets(m_context.device(), &allocInfo, m_descriptorSets.data()),
            "vkAllocateDescriptorSets");

    // No texture ever changes, and each frame's uniform buffer is created once,
    // so the sets are written here rather than per frame.
    std::array<VkDescriptorImageInfo, kTextureBindingCount> images{};
    const TextureArray* sheets[kTextureBindingCount] = {m_blockTextures.get(), m_hudTexture.get(),
                                                        m_fontTexture.get(), m_skinTexture.get()};
    for (std::uint32_t index = 0; index < kTextureBindingCount; ++index) {
        images[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        images[index].imageView = sheets[index]->view();
        images[index].sampler = sheets[index]->sampler();
    }

    for (std::uint32_t frame = 0; frame < kFramesInFlight; ++frame) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_frameUniformBuffers[frame]->handle();
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(FrameUniforms);

        VkDescriptorBufferInfo materialInfo{};
        materialInfo.buffer = m_materialBuffer->handle();
        materialInfo.offset = 0;
        materialInfo.range = VK_WHOLE_SIZE;

        std::array<VkWriteDescriptorSet, kMaterialBinding + 1> writes{};
        for (std::uint32_t index = 0; index < kTextureBindingCount; ++index) {
            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = m_descriptorSets[frame];
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[index].pImageInfo = &images[index];
        }
        writes[kFrameUniformBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[kFrameUniformBinding].dstSet = m_descriptorSets[frame];
        writes[kFrameUniformBinding].dstBinding = kFrameUniformBinding;
        writes[kFrameUniformBinding].descriptorCount = 1;
        writes[kFrameUniformBinding].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[kFrameUniformBinding].pBufferInfo = &bufferInfo;
        writes[kMaterialBinding].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[kMaterialBinding].dstSet = m_descriptorSets[frame];
        writes[kMaterialBinding].dstBinding = kMaterialBinding;
        writes[kMaterialBinding].descriptorCount = 1;
        writes[kMaterialBinding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[kMaterialBinding].pBufferInfo = &materialInfo;

        vkUpdateDescriptorSets(m_context.device(), static_cast<std::uint32_t>(writes.size()), writes.data(), 0,
                               nullptr);
    }
}

void Renderer::retire(GpuMesh& slot) {
    if (slot.vertexBuffer != nullptr || slot.indexBuffer != nullptr) {
        RetiredMesh retired;
        retired.mesh.vertexBuffer = std::move(slot.vertexBuffer);
        retired.mesh.indexBuffer = std::move(slot.indexBuffer);
        retired.mesh.indexCount = slot.indexCount;
        retired.retiredOnFrame = m_frameIndex;
        m_retired.push_back(std::move(retired));
    }

    slot.vertexBuffer.reset();
    slot.indexBuffer.reset();
    slot.indexCount = 0;
}

void Renderer::freeRetiredMeshes() {
    if (m_retired.empty()) {
        return;
    }

    // **Counted against the upload batch, not against the draws.** Reasoning
    // from the draws alone - "a mesh retired on frame N may still be referenced
    // by frames N and N-1" - is true and is one frame short, because the copy
    // that fills a buffer is submitted later than the draws that read it.
    //
    // `retire` only ever runs between frames, so it stamps a buffer with the
    // frame that has already finished, call it N. A `vkCmdCopyBuffer` recorded
    // into the still-open upload batch at that moment is not submitted until
    // `m_uploads->flush()` inside `drawFrame` **N+1**. The fence that proves
    // N+1 is done is not waited on until frame N+3, and that wait happens
    // *after* this call, which `drawFrame` makes before it waits. So freeing at
    // N+3 destroyed a buffer while a transfer was still writing into it -
    // VUID-vkDestroyBuffer-buffer-00922 - and `freeBufferMemory` then returns
    // the slice to the pool, so the next `Buffer` can be carved out of memory a
    // live copy is filling. Reachable whenever one slot is uploaded twice
    // between two frames, or uploaded and then removed: a chunk remeshed twice
    // in an iteration, or loaded and unloaded in one.
    //
    // One extra frame of holding is the whole fix. A fence signal covers every
    // command earlier in submission order on the same queue, so frame N+1's
    // fence settles its upload flush as well as its draws, and there is nothing
    // further to synchronise.
    constexpr std::uint64_t framesToHold = kFramesInFlight + 2;

    const auto expired = [&](const RetiredMesh& retired) {
        return m_frameIndex - retired.retiredOnFrame >= framesToHold;
    };

    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(), expired), m_retired.end());
}

void Renderer::uploadInto(GpuMesh& slot, const MeshData& mesh) {
    uploadInto(slot, mesh.vertices, mesh.indices);
}

bool Renderer::beyondFog(const GpuMesh& mesh) const {
    if (m_fogDistance <= 0.0f || m_fogStartFraction <= 0.0f) {
        return false;
    }
    // Nearest point of the box to the eye, so a chunk is only dropped once
    // *all* of it is inside the fog. The visible box is square and the fog
    // radius is a sphere inside it, which is where the saving comes from.
    const glm::vec3 nearest = glm::clamp(m_eyePosition, mesh.boundsMin, mesh.boundsMax);
    const glm::vec3 offset = nearest - m_eyePosition;
    return glm::dot(offset, offset) > m_fogDistance * m_fogDistance;
}

void Renderer::uploadInto(GpuMesh& slot, const std::vector<Vertex>& vertices,
                          const std::vector<std::uint32_t>& indices) {
    // Retire first: the old buffers are dead weight while the new ones are
    // allocated, and holding both doubles peak device memory.
    retire(slot);

    if (vertices.empty() || indices.empty()) {
        return;
    }

    const VkDeviceSize vertexBytes = vertices.size() * sizeof(Vertex);
    const VkDeviceSize indexBytes = indices.size() * sizeof(std::uint32_t);

    slot.vertexBuffer =
        std::make_unique<Buffer>(m_context, vertexBytes,
                                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    slot.indexBuffer =
        std::make_unique<Buffer>(m_context, indexBytes,
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    m_uploads->stage(*slot.vertexBuffer, vertices.data(), vertexBytes);
    m_uploads->stage(*slot.indexBuffer, indices.data(), indexBytes);
    slot.indexCount = static_cast<std::uint32_t>(indices.size());

    slot.boundsMin = glm::vec3{std::numeric_limits<float>::max()};
    slot.boundsMax = glm::vec3{std::numeric_limits<float>::lowest()};
    for (const Vertex& vertex : vertices) {
        const glm::vec3 p{vertex.position[0], vertex.position[1], vertex.position[2]};
        slot.boundsMin = glm::min(slot.boundsMin, p);
        slot.boundsMax = glm::max(slot.boundsMax, p);
    }
}

MeshHandle Renderer::addMesh(const MeshData& mesh, bool translucent, const MeshIndexRange& shapedTail) {
    MeshHandle handle = kInvalidMesh;

    if (!m_freeSlots.empty()) {
        handle = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        handle = static_cast<MeshHandle>(m_meshes.size());
        m_meshes.emplace_back();
    }

    uploadInto(m_meshes[handle], mesh);
    m_meshes[handle].inUse = true;
    m_meshes[handle].translucent = translucent;
    m_meshes[handle].shapedTail = clampToMesh(shapedTail, m_meshes[handle].indexCount);
    return handle;
}

void Renderer::updateMesh(MeshHandle handle, const MeshData& mesh, const MeshIndexRange& shapedTail) {
    if (handle >= m_meshes.size() || !m_meshes[handle].inUse) {
        return;
    }
    uploadInto(m_meshes[handle], mesh);
    m_meshes[handle].inUse = true;
    // Re-clamped rather than carried over: a slot is reused by a different
    // chunk, and a stale tail would draw a range off the end of a shorter
    // buffer - which reads as panes flickering somewhere unrelated.
    m_meshes[handle].shapedTail = clampToMesh(shapedTail, m_meshes[handle].indexCount);
}

void Renderer::removeMesh(MeshHandle handle) {
    if (handle >= m_meshes.size() || !m_meshes[handle].inUse) {
        return;
    }
    retire(m_meshes[handle]);
    m_meshes[handle].inUse = false;
    m_freeSlots.push_back(handle);
}

void Renderer::setVerticalFov(float degrees) {
    m_verticalFovDegrees = std::clamp(degrees, 30.0f, 130.0f);
}

void Renderer::setFarPlane(float distance) {
    m_farPlane = std::max(distance, kNearPlane * 2.0f);
}

std::size_t Renderer::meshCount() const {
    return m_meshes.size() - m_freeSlots.size();
}

float Renderer::aspectRatio() const {
    const VkExtent2D extent = m_swapchain.extent();
    return extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);
}

std::uint32_t Renderer::textureWidth() const {
    return m_blockTextures ? m_blockTextures->width() : 0;
}

std::uint32_t Renderer::textureHeight() const {
    return m_blockTextures ? m_blockTextures->height() : 0;
}

std::uint32_t Renderer::textureLayerCount() const {
    return m_blockTextures ? m_blockTextures->layerCount() : 0;
}

std::uint8_t Renderer::textureAlphaAt(std::uint32_t layer, std::uint32_t x, std::uint32_t y) const {
    return m_blockTextures ? m_blockTextures->alphaAt(layer, x, y) : 0;
}

std::uint32_t Renderer::fontWidth() const {
    return m_fontTexture ? m_fontTexture->width() : 0;
}

std::uint32_t Renderer::fontHeight() const {
    return m_fontTexture ? m_fontTexture->height() : 0;
}

std::uint8_t Renderer::fontAlphaAt(std::uint32_t x, std::uint32_t y) const {
    return m_fontTexture ? m_fontTexture->alphaAt(0, x, y) : 0;
}

void Renderer::setOverlayMesh(const MeshData& mesh) {
    uploadInto(m_overlayMesh, mesh);
}

void Renderer::setCrackMesh(const MeshData& mesh) {
    uploadInto(m_crackMesh, mesh);
}

// No device wait: retiring the old buffers already defers their destruction
// past every in-flight frame, so this is cheap enough to call whenever the HUD
// changes.
void Renderer::setScreenMesh(const MeshData& mesh) {
    sortScreenIndices(mesh, m_sortOrder, m_sortedIndices);
    uploadInto(m_screenMesh, mesh.vertices, m_sortedIndices);
}

void Renderer::setClippedScreenMesh(const MeshData& mesh, const glm::vec2& min, const glm::vec2& max) {
    sortScreenIndices(mesh, m_sortOrder, m_sortedIndices);
    uploadInto(m_clippedScreenMesh, mesh.vertices, m_sortedIndices);
    m_screenClipMin = min;
    m_screenClipMax = max;
}

void Renderer::setTopScreenMesh(const MeshData& mesh) {
    sortScreenIndices(mesh, m_sortOrder, m_sortedIndices);
    uploadInto(m_topScreenMesh, mesh.vertices, m_sortedIndices);
}

void Renderer::setSkyMesh(const MeshData& mesh, std::size_t slot) {
    uploadInto(m_skyMeshes[slot < kSkySlots ? slot : 0], mesh);
}

void Renderer::setSunDirection(const glm::vec3& direction) {
    const float length = glm::length(direction);
    m_sunDirection = length > 0.0f ? direction / length : glm::vec3{0.0f, 1.0f, 0.0f};
}

void Renderer::setSunLighting(float ambient, float sun, float ambientFloor) {
    m_ambientLight = ambient;
    m_sunLight = sun;
    m_ambientFloor = ambientFloor;
}

void Renderer::setAnimatedLayer(float meshedLayer, float currentLayer) {
    m_animatedLayer = meshedLayer;
    m_animatedFrame = currentLayer;
}

void Renderer::setSecondAnimatedLayer(float meshedLayer, float currentLayer) {
    m_animatedLayer2 = meshedLayer;
    m_animatedFrame2 = currentLayer;
}

void Renderer::setFog(const glm::vec3& colour, float distance, float startFraction) {
    m_fogColour = colour;
    m_fogDistance = distance;
    m_fogStartFraction = startFraction;
}

void Renderer::setExposure(float exposure) {
    m_exposure = std::clamp(exposure, 0.05f, 8.0f);
}

void Renderer::setBloom(bool enabled, float strength) {
    m_bloomEnabled = enabled;
    m_bloomStrength = std::clamp(strength, 0.0f, 1.0f);
}

void Renderer::setClouds(int quality, float coverage, float shadowStrength) {
    m_cloudQuality = std::clamp(quality, 0, kCloudQualityCount - 1);
    m_cloudCoverage = std::clamp(coverage, 0.0f, 1.0f);
    m_cloudShadowStrength = std::clamp(shadowStrength, 0.0f, 1.0f);
}

void Renderer::setWater(float waveStrength, float reflection) {
    m_waterWaves = std::clamp(waveStrength, 0.0f, 4.0f);
    m_waterReflection = std::clamp(reflection, 0.0f, 1.0f);
}

void Renderer::setShadowDarkness(float darkness) {
    m_shadowDarkness = std::clamp(darkness, 0.0f, 1.0f);
}

void Renderer::setWaterDetail(float foam, float caustics, float refraction) {
    m_waterFoam = std::clamp(foam, 0.0f, 1.0f);
    m_waterCaustics = std::clamp(caustics, 0.0f, 1.0f);
    m_waterRefraction = std::clamp(refraction, 0.0f, 1.0f);
}

void Renderer::setPrecipitationMesh(const MeshData& mesh) {
    uploadInto(m_precipitationMesh, mesh);
}

void Renderer::setBoltMesh(const MeshData& mesh) {
    uploadInto(m_boltMesh, mesh);
}

void Renderer::setParticleMesh(const MeshData& mesh) {
    uploadInto(m_particleMesh, mesh);
}

void Renderer::setWind(const glm::vec2& direction, float bend, float clock) {
    m_wind = glm::vec4{direction.x, direction.y, std::max(0.0f, bend), clock};
}

void Renderer::setSkyGlow(const glm::vec3& colour, float strength) {
    m_glow = glm::vec4{colour, std::max(0.0f, strength)};
}

void Renderer::setImageQuality(float antiAlias, float contactOcclusion) {
    m_antiAlias = std::clamp(antiAlias, 0.0f, 1.0f);
    // In screen pixels at the near end. Past a few tens it stops being a corner
    // and starts being a shadow the geometry never cast.
    m_contactOcclusion = std::clamp(contactOcclusion, 0.0f, 64.0f);
}

void Renderer::setPrecipitation(float level, bool snow, float fallenBlocks, float slant) {
    m_weather = glm::vec4{std::clamp(level, 0.0f, 1.0f), snow ? 1.0f : 0.0f, fallenBlocks,
                          std::clamp(slant, -0.6f, 0.6f)};
}

void Renderer::setDebugView(int view) {
    m_debugView = std::clamp(view, 0, kDebugViewCount - 1);
}

void Renderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // Created already-signalled so the very first frame does not wait forever.
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    m_imageAvailable.resize(kFramesInFlight);
    m_frameInFlight.resize(kFramesInFlight);
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        vkCheck(vkCreateSemaphore(m_context.device(), &semaphoreInfo, nullptr, &m_imageAvailable[i]),
                "vkCreateSemaphore");
        vkCheck(vkCreateFence(m_context.device(), &fenceInfo, nullptr, &m_frameInFlight[i]), "vkCreateFence");
    }

    m_renderFinished.resize(m_swapchain.imageCount());
    for (VkSemaphore& semaphore : m_renderFinished) {
        vkCheck(vkCreateSemaphore(m_context.device(), &semaphoreInfo, nullptr, &semaphore), "vkCreateSemaphore");
    }
}

void Renderer::destroySyncObjects() {
    for (VkSemaphore semaphore : m_imageAvailable) {
        vkDestroySemaphore(m_context.device(), semaphore, nullptr);
    }
    m_imageAvailable.clear();

    for (VkSemaphore semaphore : m_renderFinished) {
        vkDestroySemaphore(m_context.device(), semaphore, nullptr);
    }
    m_renderFinished.clear();

    for (VkFence fence : m_frameInFlight) {
        vkDestroyFence(m_context.device(), fence, nullptr);
    }
    m_frameInFlight.clear();
}

void Renderer::recreateSwapchain() {
    // **The zero-extent check has to be here, not only in the caller.**
    // `drawFrame` samples `isMinimized()` once on the way in, but this line
    // asks the OS live - so minimising in the gap between those two, or
    // minimising part-way through a resize drag, reaches `Swapchain::create`
    // with a 0x0 extent and throws "Cannot create a swapchain for a zero-sized
    // window". `Main.cpp` wraps the whole session in a single try/catch, so
    // that throw *ends the game* rather than logging anything, which is not
    // what the comment beside the throw promises.
    //
    // Returning costs one frame and nothing else: the swapchain is untouched
    // and still valid, `drawFrame` early-returns for as long as the window is
    // away, and the first frame after it is restored fails to acquire, comes
    // back through here with a real extent, and rebuilds properly.
    const Extent2D framebuffer = m_window.framebufferExtent();
    if (framebuffer.width == 0 || framebuffer.height == 0) {
        return;
    }

    vkDeviceWaitIdle(m_context.device());

    m_swapchain.recreate(toVkExtent(framebuffer));

    // The depth attachment must match the colour attachment's size exactly.
    m_depthImage = std::make_unique<DepthImage>(m_context, renderExtent());
    // And so must the HDR scene image and the bloom pyramid derived from it.
    // This also rewrites the post-processing descriptor sets, which name image
    // views that have just been destroyed.
    createRenderTargets();

    // The image count can change, and semaphores are per image.
    destroySyncObjects();
    createSyncObjects();

    m_currentFrame = 0;
    // Otherwise the next frame reads query results that were never written.
    m_timestampsPending.assign(kFramesInFlight, false);
}

glm::mat4 Renderer::projectionMatrix() const {
    const VkExtent2D extent = m_swapchain.extent();
    const float width = static_cast<float>(extent.width);
    const float height = static_cast<float>(extent.height > 0 ? extent.height : 1);

    glm::mat4 projection =
        glm::perspective(glm::radians(m_verticalFovDegrees), width / height, kNearPlane, m_farPlane);

    // GLM builds this for OpenGL, whose Y axis points the opposite way to
    // Vulkan's. Without this flip the whole scene renders upside down, and
    // nothing warns you about it.
    projection[1][1] *= -1.0f;
    return projection;
}

void Renderer::recordCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, const ClearColor& color,
                              const glm::mat4& viewProjection,
                              const std::optional<glm::mat4>& overlayTransform) const {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    m_frameDrawCalls = 0;
    m_frameTriangles = 0;

    // Queries must be reset on the GPU timeline before being written again, and
    // this frame slot's previous results have already been read by now.
    if (m_timestampsSupported) {
        vkCmdResetQueryPool(commandBuffer, m_timestampPool, m_currentFrame * 2, 2);
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_timestampPool, m_currentFrame * 2);
    }

    const VkImage image = m_swapchain.images()[imageIndex];

    // First, because everything that shades reads it.
    recordShadowPass(commandBuffer);
    recordGeometryPass(commandBuffer, viewProjection);
    recordDeferredPass(commandBuffer, color);
    recordSceneCopy(commandBuffer);
    recordForwardPass(commandBuffer, viewProjection, overlayTransform);

    // The scene stops being something to write and becomes something to read.
    const VkImageMemoryBarrier sceneToSampled = makeColorImageBarrier(
        m_sceneColor->image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &sceneToSampled);

    if (m_bloomEnabled) {
        recordBloomPasses(commandBuffer);
    }

    const VkImageMemoryBarrier toColorAttachment =
        makeColorImageBarrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0,
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toColorAttachment);

    recordToneMapPass(commandBuffer, imageIndex);
    recordUiPass(commandBuffer, imageIndex);

    const VkImageMemoryBarrier toPresent =
        makeColorImageBarrier(image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &toPresent);

    if (m_timestampsSupported) {
        vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_timestampPool,
                            m_currentFrame * 2 + 1);
    }

    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

void Renderer::recordShadowPass(VkCommandBuffer commandBuffer) const {
    const std::uint32_t cascades = m_shadowMap->cascades();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_shadowMap->image();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = cascades;

    // Nothing to draw - shadows are off, or the sun is down. The map still has
    // to end the frame in the layout the lighting pass's descriptor names, so
    // it is moved there empty. A white border and a cascade count of zero mean
    // nothing ever reads it.
    if (m_liveShadowCascades == 0) {
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        // `FRAGMENT_SHADER` as the source, matching the live branch below and
        // for the same reason: the last frame's lighting pass sampled this
        // image, and a transition must be ordered after those reads. A source
        // of `TOP_OF_PIPE` is an *empty* scope - it waits for nothing at all -
        // so it promises ordering it does not deliver. This branch runs every
        // sunset, which is exactly when it is least convenient to find out.
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        return;
    }

    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    const std::uint32_t resolution = m_shadowMap->resolution();
    VkViewport viewport{};
    viewport.width = static_cast<float>(resolution);
    viewport.height = static_cast<float>(resolution);
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.extent = VkExtent2D{resolution, resolution};

    for (std::uint32_t cascade = 0; cascade < m_liveShadowCascades; ++cascade) {
        VkRenderingAttachmentInfo depthAttachment{};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView = m_shadowMap->cascadeView(cascade);
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil.depth = 1.0f;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = scissor.extent;
        renderingInfo.layerCount = 1;
        renderingInfo.pDepthAttachment = &depthAttachment;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline->handle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline->layout(), 0, 1,
                                &m_descriptorSets[m_currentFrame], 0, nullptr);

        // Culled against the light's box rather than the camera's: a chunk
        // behind the player still casts into the slice in front of them.
        const std::array<glm::vec4, 6> planes = frustumPlanes(m_shadowMatrices[cascade]);

        for (const GpuMesh& mesh : m_meshes) {
            if (mesh.translucent || mesh.indexCount == 0) {
                continue;
            }
            if (!boxInFrustum(planes, mesh.boundsMin, mesh.boundsMax) || beyondFog(mesh)) {
                continue;
            }
            ++m_frameDrawCalls;
            m_frameTriangles += mesh.indexCount / 3;

            MeshPushConstants push{};
            push.modelViewProjection = m_shadowMatrices[cascade];
            vkCmdPushConstants(commandBuffer, m_shadowPipeline->layout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(MeshPushConstants), &push);

            const VkBuffer vertexBuffers[] = {mesh.vertexBuffer->handle()};
            const VkDeviceSize vertexOffsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
            vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
        }

        vkCmdEndRendering(commandBuffer);
    }

    barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void Renderer::recordGeometryPass(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection) const {
    const VkExtent2D extent = renderExtent();

    // An image's "layout" is how the GPU has it arranged in memory. It has to be
    // moved into one that permits the operation about to be performed. UNDEFINED
    // as the old layout means "I don't care what was in here", which is true:
    // every pixel of the G-buffer is either written or discarded.
    const RenderTarget* targets[3] = {m_gbufferAlbedo.get(), m_gbufferLight.get(), m_gbufferMaterial.get()};
    std::array<VkImageMemoryBarrier, 3> toAttachment{};
    for (std::size_t i = 0; i < toAttachment.size(); ++i) {
        toAttachment[i] =
            makeColorImageBarrier(targets[i]->image(), VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(toAttachment.size()), toAttachment.data());

    VkImageMemoryBarrier toDepthAttachment{};
    toDepthAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDepthAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDepthAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    toDepthAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDepthAttachment.image = m_depthImage->handle();
    toDepthAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    toDepthAttachment.subresourceRange.levelCount = 1;
    toDepthAttachment.subresourceRange.layerCount = 1;
    // **This barrier has to name the stage that last wrote depth, and that is
    // not the fragment shader.** There is one depth image against two frames in
    // flight, so the previous frame is the other writer, and its last depth
    // write is the forward pass's - at `LATE_FRAGMENT_TESTS`, which is logically
    // *later* than `FRAGMENT_SHADER` and so falls outside a source scope naming
    // only that. Nothing observable breaks today, because `UNDEFINED` plus a
    // clear means the old contents are not read and ordering survives through
    // the chain the scene-copy barrier makes - but the write-after-write is not
    // made available, sync validation flags it, and ordinary validation never
    // will.
    toDepthAttachment.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    toDepthAttachment.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &toDepthAttachment);

    std::array<VkRenderingAttachmentInfo, 3> colorAttachments{};
    for (std::size_t i = 0; i < colorAttachments.size(); ++i) {
        colorAttachments[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[i].imageView = targets[i]->view(0);
        colorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachments[i].clearValue.color = VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}};
    }

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_depthImage->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    // Kept now, unlike before: the lighting pass reads it back to work out where
    // each pixel is in the world, and the forward pass tests against it.
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    // 1.0 is the far plane: every real fragment is nearer than an empty pixel.
    depthAttachment.clearValue.depthStencil.depth = 1.0f;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<std::uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.data();
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    // Supplied per frame rather than baked into the pipeline, so a window resize
    // does not require rebuilding it.
    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gbufferPipeline->handle());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gbufferPipeline->layout(), 0, 1,
                            &m_descriptorSets[m_currentFrame], 0, nullptr);

    // Chunks outside the view are the overwhelming majority at high render
    // distance, and skipping them costs one box test each.
    const std::array<glm::vec4, 6> planes = frustumPlanes(viewProjection);

    for (const GpuMesh& mesh : m_meshes) {
        if (mesh.translucent || mesh.indexCount == 0) {
            continue;
        }
        if (!boxInFrustum(planes, mesh.boundsMin, mesh.boundsMax) || beyondFog(mesh)) {
            continue;
        }
        ++m_frameDrawCalls;
        m_frameTriangles += mesh.indexCount / 3;

        MeshPushConstants push{};
        push.modelViewProjection = viewProjection;
        push.flags.x = kDrawFlagLit | kDrawFlagFogged;
        vkCmdPushConstants(commandBuffer, m_gbufferPipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(MeshPushConstants), &push);

        const VkBuffer vertexBuffers[] = {mesh.vertexBuffer->handle()};
        const VkDeviceSize vertexOffsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    }

    vkCmdEndRendering(commandBuffer);
}

void Renderer::recordDeferredPass(VkCommandBuffer commandBuffer, const ClearColor& color) const {
    const VkExtent2D extent = renderExtent();

    const RenderTarget* targets[3] = {m_gbufferAlbedo.get(), m_gbufferLight.get(), m_gbufferMaterial.get()};
    std::array<VkImageMemoryBarrier, 3> toSampled{};
    for (std::size_t i = 0; i < toSampled.size(); ++i) {
        toSampled[i] = makeColorImageBarrier(targets[i]->image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(toSampled.size()), toSampled.data());

    VkImageMemoryBarrier depthToSampled{};
    depthToSampled.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthToSampled.oldLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthToSampled.newLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    depthToSampled.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthToSampled.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthToSampled.image = m_depthImage->handle();
    depthToSampled.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthToSampled.subresourceRange.levelCount = 1;
    depthToSampled.subresourceRange.layerCount = 1;
    depthToSampled.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    depthToSampled.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &depthToSampled);

    const VkImageMemoryBarrier sceneToAttachment =
        makeColorImageBarrier(m_sceneColor->image(), VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &sceneToAttachment);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    // The world is lit into a floating-point image rather than straight onto the
    // screen, so brightness may exceed 1.0 on the way through.
    //
    // **The clear is overwritten in full, and is kept for the load op alone.**
    // It used to be described as what fills every pixel the lighting pass
    // discards - `deferred.frag` discards nothing: where depth is 1.0 it writes
    // the sky's own radiance with `kSkyDistance` in the alpha, which is what the
    // water and rain passes read back as "nothing is behind this". Clearing
    // rather than loading is still right, because a load would pull the whole
    // attachment back through memory for pixels that are all about to be
    // written.
    colorAttachment.imageView = m_sceneColor->view(0);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = VkClearColorValue{{color.r, color.g, color.b, color.a}};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredPipeline->handle());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_deferredPipeline->layout(), 0, 1,
                            &m_deferredSets[m_currentFrame], 0, nullptr);

    PostPushConstants push{};
    push.filterParams = glm::vec4{0.0f, m_contactOcclusion, 0.0f, 0.0f};
    push.imageParams = glm::vec4{m_exposure, static_cast<float>(m_toneMapper), 0.0f,
                                 static_cast<float>(m_debugView)};
    vkCmdPushConstants(commandBuffer, m_deferredPipeline->layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPushConstants),
                       &push);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    ++m_frameDrawCalls;
    m_frameTriangles += 1;

    vkCmdEndRendering(commandBuffer);
}

void Renderer::recordSceneCopy(VkCommandBuffer commandBuffer) const {
    const VkExtent2D extent = renderExtent();

    const std::array<VkImageMemoryBarrier, 2> toTransfer{
        makeColorImageBarrier(m_sceneColor->image(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                              VK_ACCESS_TRANSFER_READ_BIT),
        // Nothing in it is worth keeping, so the old contents are discarded
        // rather than transitioned - which is what UNDEFINED asks for.
        makeColorImageBarrier(m_sceneCopy->image(), VK_IMAGE_LAYOUT_UNDEFINED,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT)};
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                         static_cast<std::uint32_t>(toTransfer.size()), toTransfer.data());

    VkImageCopy region{};
    region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.srcSubresource.layerCount = 1;
    region.dstSubresource = region.srcSubresource;
    region.extent = VkExtent3D{extent.width, extent.height, 1};
    vkCmdCopyImage(commandBuffer, m_sceneColor->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   m_sceneCopy->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    const std::array<VkImageMemoryBarrier, 2> toUse{
        makeColorImageBarrier(m_sceneColor->image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                              VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT),
        makeColorImageBarrier(m_sceneCopy->image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                              VK_ACCESS_SHADER_READ_BIT)};
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, static_cast<std::uint32_t>(toUse.size()), toUse.data());
}

void Renderer::recordForwardPass(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection,
                                 const std::optional<glm::mat4>& overlayTransform) const {
    const VkExtent2D extent = renderExtent();

    // Back to a writable depth attachment. Water writes depth, which is what
    // stops two overlapping surfaces blending twice.
    VkImageMemoryBarrier depthToAttachment{};
    depthToAttachment.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    depthToAttachment.oldLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
    depthToAttachment.newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthToAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthToAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    depthToAttachment.image = m_depthImage->handle();
    depthToAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthToAttachment.subresourceRange.levelCount = 1;
    depthToAttachment.subresourceRange.layerCount = 1;
    depthToAttachment.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    depthToAttachment.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &depthToAttachment);

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_sceneColor->view(0);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingAttachmentInfo depthAttachment{};
    depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAttachment.imageView = m_depthImage->view();
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    renderingInfo.pDepthAttachment = &depthAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline->handle());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline->layout(), 0, 1,
                            &m_descriptorSets[m_currentFrame], 0, nullptr);

    // Draws exactly `range`, with no sentinel meaning "all of it" - an empty
    // range draws nothing. A chunk whose translucent geometry is *only* panes
    // has a shape tail starting at 0 and therefore an empty body, and under a
    // "0 means everything" convention that chunk would draw its whole buffer
    // and then its panes again, blending every pane over itself.
    const auto drawRange = [&](const GpuMesh& mesh, const glm::mat4& transform, bool lit, bool fogged,
                               std::uint32_t extraFlags, const MeshIndexRange& range) {
        if (range.count == 0) {
            return;
        }
        ++m_frameDrawCalls;
        m_frameTriangles += range.count / 3;

        MeshPushConstants push{};
        push.modelViewProjection = transform;
        push.flags.x = (lit ? kDrawFlagLit : 0u) | (fogged ? kDrawFlagFogged : 0u) | extraFlags;
        vkCmdPushConstants(commandBuffer, m_trianglePipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(MeshPushConstants), &push);

        const VkBuffer vertexBuffers[] = {mesh.vertexBuffer->handle()};
        const VkDeviceSize vertexOffsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, range.count, 1, range.first, 0, 0);
    };

    const auto drawMesh = [&](const GpuMesh& mesh, const glm::mat4& transform, bool lit, bool fogged = true,
                              std::uint32_t extraFlags = 0) {
        drawRange(mesh, transform, lit, fogged, extraFlags, MeshIndexRange{0u, mesh.indexCount});
    };

    const std::array<glm::vec4, 6> planes = frustumPlanes(viewProjection);

    // The sun and the moon, unlit because they do not shade themselves, and
    // emissive so they can be brighter than white and bloom around their edges.
    for (std::size_t slot = 0; slot < kSkySlots; ++slot) {
        drawMesh(m_skyMeshes[slot], viewProjection * m_skyTransforms[slot], false, true,
                 kDrawFlagEmissive | kDrawFlagSky);
    }

    // Clouds, **after** the sun and moon so a cloud passing in front of one
    // occludes it, and before the translucent pass so water has something to
    // catch. One full-screen triangle; the pipeline is swapped back afterwards.
    if (m_cloudQuality > 0) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_cloudPipeline->handle());
        MeshPushConstants cloudPush{};
        cloudPush.modelViewProjection = viewProjection;
        vkCmdPushConstants(commandBuffer, m_cloudPipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(MeshPushConstants), &cloudPush);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        ++m_frameDrawCalls;
        m_frameTriangles += 1;

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline->handle());
    }

    // Blended geometry after it, so what shows through has already been drawn.
    //
    // **Farthest first.** `m_meshes` is in slot order, which is streaming
    // order, so a nearer lake chunk could be drawn first, stamp its depth (this
    // pipeline writes depth, and rain rejection depends on that) and reject the
    // farther chunk outright - the seam then moved about as slots recycled.
    // Back-face culling is *already* on for this draw: `m_trianglePipeline`
    // sets `cullMode = VK_CULL_MODE_BACK_BIT` explicitly at its creation, and
    // cull mode is not dynamic state. Culling
    // settles which face of one volume is drawn; only ordering settles which of
    // two volumes is drawn first, and that is this loop's job because the
    // renderer is the only thing here that has a camera.
    //
    // Sorted on the box centre rather than `beyondFog`'s nearest point: the
    // nearest point is zero for every chunk the eye is inside, so it ties
    // exactly where the order matters most. Chunks are equal cubes on a grid,
    // for which centre distance is a valid back-to-front order. The slot index
    // rides in the pair as a tiebreak, so two chunks at equal distance keep the
    // same order every frame - without it they could swap and the seam would
    // flicker instead of disappearing.
    m_translucentOrder.clear();
    const auto slotCount = static_cast<std::uint32_t>(m_meshes.size());
    for (std::uint32_t slot = 0; slot < slotCount; ++slot) {
        const GpuMesh& mesh = m_meshes[slot];
        if (!mesh.translucent || mesh.indexCount == 0) {
            continue;
        }
        if (!boxInFrustum(planes, mesh.boundsMin, mesh.boundsMax) || beyondFog(mesh)) {
            continue;
        }
        const glm::vec3 offset = (mesh.boundsMin + mesh.boundsMax) * 0.5f - m_eyePosition;
        float distanceSquared = glm::dot(offset, offset);
        // **A NaN here is not a speck on screen, it is heap corruption.**
        // `a > b` on a pair is a strict weak ordering only while every float
        // compares normally; one NaN makes every comparison against it false,
        // `std::sort` loses its sentinel and walks off the end of the range.
        // Nothing reachable produces one today - the bounds come from the
        // mesher and the eye from the camera - but this is the same hazard
        // already guarded in `surfaceNormal()`, and the cost of being wrong is
        // far higher here. Coerced rather than dropped, so a corrupt chunk
        // still draws (nearest, hence last) instead of silently vanishing.
        if (!std::isfinite(distanceSquared)) {
            distanceSquared = 0.0f;
        }
        m_translucentOrder.emplace_back(distanceSquared, slot);
    }
    std::sort(m_translucentOrder.begin(), m_translucentOrder.end(),
              [](const std::pair<float, std::uint32_t>& a, const std::pair<float, std::uint32_t>& b) {
                  return a > b;
              });

    for (const std::pair<float, std::uint32_t>& entry : m_translucentOrder) {
        const GpuMesh& mesh = m_meshes[entry.second];
        // Flagged, so the fragment stage keeps the texture's own alpha instead
        // of running the cutout test on it. Only this loop carries it: the
        // particle draw below is blended too but its art is a cutout.
        //
        // **Two draws, not seven.** The mesher can hand over its whole
        // seven-way split, but only the shape-pass tail is acted on, and the
        // reason is measured rather than assumed. The six face directions were
        // offered so the renderer could order them back-facing first - but
        // back-face culling is already on for this pipeline, so the three
        // back-facing directions produce no fragments at all and reordering
        // them changes nothing on screen. Splitting them out to *skip*
        // issuing those three would save the GPU some vertex work and cost
        // two extra draw calls per chunk, and at 1.55 ms GPU against an
        // 8.33 ms frame - 18.6% utilisation, CPU-bound - that is the wrong
        // trade in the wrong direction. So the six stay as one contiguous
        // draw, which is what they already are: the ranges tile the buffer in
        // order, so indices 0 .. shapedTail.first are exactly the six.
        const std::uint32_t bodyCount = mesh.shapedTail.empty() ? mesh.indexCount : mesh.shapedTail.first;
        drawRange(mesh, viewProjection, true, true, kDrawFlagBlended, MeshIndexRange{0u, bodyCount});
        // The shape pass last: it belongs to no direction, and a stained pane
        // is far more often in front of water than inside it. Skipped
        // entirely when empty, which is every chunk without one.
        if (!mesh.shapedTail.empty()) {
            drawRange(mesh, viewProjection, true, true, kDrawFlagBlended, mesh.shapedTail);
        }
    }

    if (overlayTransform.has_value()) {
        drawMesh(m_overlayMesh, viewProjection * *overlayTransform, false);
    }

    // The breaking cracks, in world space and lit, so the cutout test throws
    // away everything but the crack lines themselves. After the outline because
    // both sit on the same block and the cage should not be cracked over.
    drawMesh(m_crackMesh, viewProjection, true);

    // Particles, lit and cutout like any other world surface. After the
    // translucent pass so a chip in front of water draws, and before the rain
    // so a splash is not drawn over its own drop.
    drawMesh(m_particleMesh, viewProjection, true);

    // Lightning, unlit and emissive, so it clears 1.0 and blooms rather than
    // being a white line.
    drawMesh(m_boltMesh, viewProjection, false, true, kDrawFlagEmissive);

    // Last of the world, and after water on purpose: rain in front of a lake has
    // to draw, and rain behind its surface is depth-rejected, which is what you
    // want looking down at one.
    if (m_weather.x > 0.0f && m_precipitationMesh.indexCount > 0) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          m_precipitationPipeline->handle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_precipitationPipeline->layout(), 0, 1,
                                &m_descriptorSets[m_currentFrame], 0, nullptr);
        drawMesh(m_precipitationMesh, viewProjection, false, false);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_trianglePipeline->handle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_trianglePipeline->layout(), 0, 1, &m_descriptorSets[m_currentFrame],
                                0, nullptr);
    }

    vkCmdEndRendering(commandBuffer);
}

namespace {

VkImageMemoryBarrier makeMipBarrier(VkImage image, std::uint32_t level, VkImageLayout oldLayout,
                                    VkImageLayout newLayout, VkAccessFlags srcAccess, VkAccessFlags dstAccess) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = level;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    return barrier;
}

} // namespace

void Renderer::recordBloomPasses(VkCommandBuffer commandBuffer) const {
    const std::uint32_t mips = m_bloom->mipLevels();
    // How far the upsample reaches, in screen fractions. A radius in UV rather
    // than in texels on purpose: it is the *apparent* size of the glow, and it
    // should not change when the window is resized.
    constexpr float kFilterRadius = 0.005f;
    // How much of each coarser level survives into the finer one. Below 1 the
    // pyramid's total is a converging sum, so the accumulated bloom cannot run
    // away with the number of mips.
    constexpr float kUpsampleWeight = 0.6f;

    const auto fullScreenPass = [&](const GraphicsPipeline& pipeline, VkDescriptorSet set, VkImageView target,
                                    VkExtent2D targetExtent, VkAttachmentLoadOp loadOp,
                                    const PostPushConstants& push) {
        VkRenderingAttachmentInfo attachment{};
        attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attachment.imageView = target;
        attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachment.loadOp = loadOp;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.extent = targetExtent;
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachments = &attachment;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        VkViewport viewport{};
        viewport.width = static_cast<float>(targetExtent.width);
        viewport.height = static_cast<float>(targetExtent.height);
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = targetExtent;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 1, &set, 0,
                                nullptr);
        vkCmdPushConstants(commandBuffer, pipeline.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PostPushConstants), &push);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
        ++m_frameDrawCalls;
        m_frameTriangles += 1;

        vkCmdEndRendering(commandBuffer);
    };

    for (std::uint32_t level = 0; level < mips; ++level) {
        const VkExtent2D sourceExtent =
            level == 0 ? m_sceneColor->extent() : m_bloom->mipExtent(level - 1);

        const VkImageMemoryBarrier toAttachment =
            makeMipBarrier(m_bloom->image(), level, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &toAttachment);

        PostPushConstants push{};
        push.filterParams = glm::vec4{1.0f / static_cast<float>(sourceExtent.width),
                                      1.0f / static_cast<float>(sourceExtent.height), kFilterRadius,
                                      kUpsampleWeight};
        fullScreenPass(*m_bloomDownPipeline, m_bloomDownSets[level], m_bloom->view(level),
                       m_bloom->mipExtent(level), VK_ATTACHMENT_LOAD_OP_DONT_CARE, push);

        const VkImageMemoryBarrier toSampled = makeMipBarrier(
            m_bloom->image(), level, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSampled);
    }

    // Back down the pyramid, adding each blurred level into the one below it.
    for (std::uint32_t level = mips; level-- > 1;) {
        const std::uint32_t target = level - 1;

        // LOAD, so the old layout must be the one the downsample left rather
        // than UNDEFINED: this pass adds to what is already there.
        const VkImageMemoryBarrier toAttachment = makeMipBarrier(
            m_bloom->image(), target, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &toAttachment);

        const VkExtent2D sourceExtent = m_bloom->mipExtent(level);
        PostPushConstants push{};
        push.filterParams = glm::vec4{1.0f / static_cast<float>(sourceExtent.width),
                                      1.0f / static_cast<float>(sourceExtent.height), kFilterRadius,
                                      kUpsampleWeight};
        fullScreenPass(*m_bloomUpPipeline, m_bloomUpSets[level], m_bloom->view(target),
                       m_bloom->mipExtent(target), VK_ATTACHMENT_LOAD_OP_LOAD, push);

        const VkImageMemoryBarrier toSampled = makeMipBarrier(
            m_bloom->image(), target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toSampled);
    }
}

void Renderer::recordToneMapPass(VkCommandBuffer commandBuffer, std::uint32_t imageIndex) const {
    const VkExtent2D extent = m_swapchain.extent();

    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = m_swapchain.imageViews()[imageIndex];
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Every pixel is written, so there is nothing to preserve or to clear.
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &attachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_toneMapPipeline->handle());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_toneMapPipeline->layout(), 0, 1,
                            &m_toneMapSet, 0, nullptr);

    PostPushConstants push{};
    push.filterParams = glm::vec4{m_antiAlias, 0.0f, 0.0f, 0.0f};
    push.imageParams = glm::vec4{m_exposure, static_cast<float>(m_toneMapper),
                                 m_bloomEnabled ? m_bloomStrength : 0.0f, 0.0f};
    vkCmdPushConstants(commandBuffer, m_toneMapPipeline->layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPushConstants),
                       &push);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    ++m_frameDrawCalls;
    m_frameTriangles += 1;

    vkCmdEndRendering(commandBuffer);
}

void Renderer::recordUiPass(VkCommandBuffer commandBuffer, std::uint32_t imageIndex) const {
    if (m_screenMesh.indexCount == 0 && m_clippedScreenMesh.indexCount == 0 && m_topScreenMesh.indexCount == 0) {
        return;
    }

    const VkExtent2D extent = m_swapchain.extent();

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = m_swapchain.imageViews()[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    // Keep what the world pass left there and draw over it.
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    // No depth attachment. That is the whole point of this pass.

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hudPipeline->handle());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hudPipeline->layout(), 0, 1,
                            &m_descriptorSets[m_currentFrame], 0, nullptr);

    // Screen space: no view, no projection. Only an aspect correction, so
    // geometry authored in height-relative units is not stretched horizontally.
    const float aspect =
        extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const glm::mat4 screenTransform = glm::scale(glm::mat4{1.0f}, glm::vec3{1.0f / aspect, 1.0f, 1.0f});

    const auto drawScreen = [&](const GpuMesh& mesh) {
        if (mesh.indexCount == 0) {
            return;
        }
        ++m_frameDrawCalls;
        m_frameTriangles += mesh.indexCount / 3;

        MeshPushConstants push{};
        push.modelViewProjection = screenTransform;
        vkCmdPushConstants(commandBuffer, m_hudPipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(MeshPushConstants), &push);

        const VkBuffer vertexBuffers[] = {mesh.vertexBuffer->handle()};
        const VkDeviceSize vertexOffsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, vertexOffsets);
        vkCmdBindIndexBuffer(commandBuffer, mesh.indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0, 0);
    };

    drawScreen(m_screenMesh);

    // Anything that has to stop at an edge. The mesh units are the screen
    // ones, so the rectangle converts through the same aspect correction the
    // geometry does and then out of clip space into pixels.
    if (m_clippedScreenMesh.indexCount != 0) {
        const auto toPixels = [&](const glm::vec2& point) {
            return glm::vec2{(point.x / aspect * 0.5f + 0.5f) * static_cast<float>(extent.width),
                             (point.y * 0.5f + 0.5f) * static_cast<float>(extent.height)};
        };
        const glm::vec2 lo = toPixels(m_screenClipMin);
        const glm::vec2 hi = toPixels(m_screenClipMax);

        VkRect2D clip{};
        clip.offset.x = std::max(0, static_cast<std::int32_t>(std::floor(std::min(lo.x, hi.x))));
        clip.offset.y = std::max(0, static_cast<std::int32_t>(std::floor(std::min(lo.y, hi.y))));
        const auto right = static_cast<std::int32_t>(std::ceil(std::max(lo.x, hi.x)));
        const auto bottom = static_cast<std::int32_t>(std::ceil(std::max(lo.y, hi.y)));
        clip.extent.width = static_cast<std::uint32_t>(
            std::clamp(right - clip.offset.x, 0, static_cast<std::int32_t>(extent.width) - clip.offset.x));
        clip.extent.height = static_cast<std::uint32_t>(
            std::clamp(bottom - clip.offset.y, 0, static_cast<std::int32_t>(extent.height) - clip.offset.y));

        vkCmdSetScissor(commandBuffer, 0, 1, &clip);
        drawScreen(m_clippedScreenMesh);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    // Last of all, and unclipped: what the cursor carries, and the diagnostics
    // overlay, both of which belong over everything else on the screen.
    drawScreen(m_topScreenMesh);

    vkCmdEndRendering(commandBuffer);
}

void Renderer::drawFrame(const ClearColor& color, const glm::mat4& view,
                         const std::optional<glm::mat4>& overlayTransform) {
    // Recovered from the view matrix rather than asked for separately, so it
    // cannot disagree with the matrix everything is actually drawn through.
    m_eyePosition = glm::vec3{glm::inverse(view)[3]};

    // Before any early return, so a minimized or resizing window still releases
    // retired buffers instead of accumulating them.
    //
    // **That is only safe because every drawn frame waits on the fence of the
    // slot it is about to reuse**, and a minimized iteration draws nothing: left
    // to itself it submits nothing and waits for nothing, while still moving the
    // counter `freeRetiredMeshes` measures against. Three of those iterations
    // take microseconds, so a buffer bound by the last real submission could be
    // destroyed while the GPU was still reading it - and `m_currentFrame` does
    // not advance either, so that slot's fence is never re-checked. The two
    // waits below are what put real evidence back under that counter.
    //
    // Waiting the device out **once, on the way in**, is what covers the
    // drawing half: it returns with nothing in flight, and no later frame is
    // submitted while the window is away, so every free below happens against
    // an idle GPU. Returning before the counter moved would have been simpler
    // and is exactly what the comment above says not to do - a session left
    // minimized keeps streaming chunks, and every buffer they retire would be
    // held until it was restored. One stall, at the moment the window stops
    // being drawn, costs nothing anyone can see.
    //
    // **Uploads are the other half, and they do not stop when drawing does.**
    // Nothing in the game loop asks whether the window is minimized, so chunks
    // keep streaming and `uploadInto` keeps recording copies into the upload
    // context - and keeps retiring the buffers it replaces - at loop speed,
    // while `m_uploads->flush()` sits on the drawing path below and is never
    // reached. The interleaving that used to break: iteration k allocates a
    // buffer and records a copy into it; the chunk changes again, so iteration
    // k+1 retires it; three iterations later, microseconds after that,
    // `freeRetiredMeshes` destroys it while the still-open upload command
    // buffer names it, which invalidates that command buffer and hands the
    // memory straight back to the pool for the next mesh to be allocated out
    // of. The wait above cannot cover any of it, because all of it was recorded
    // after the wait - and a batch that outgrows the staging arena submits
    // itself from inside `stage`, which is worse rather than better: that copy
    // is genuinely in flight while the counter runs past it.
    //
    // Draining submits what is recorded and waits for it, so every free below
    // happens with nothing recorded and nothing in flight - on the first
    // minimized iteration and on the hundredth. Doing it before the device wait
    // is also what makes that wait mean what it says, since idle only accounts
    // for work somebody submitted.
    const bool minimized = m_window.isMinimized();
    if (minimized) {
        m_uploads->waitForCompletion();
        if (!m_wasMinimized) {
            vkDeviceWaitIdle(m_context.device());
        }
    }
    m_wasMinimized = minimized;

    ++m_frameIndex;
    freeRetiredMeshes();

    if (minimized) {
        return;
    }

    const VkDevice device = m_context.device();

    vkCheck(vkWaitForFences(device, 1, &m_frameInFlight[m_currentFrame], VK_TRUE, UINT64_MAX), "vkWaitForFences");

    // The fence above guarantees this frame slot's previous submission is done,
    // which is exactly when its timestamps become readable.
    readGpuTimestamps();

    // And it is what makes writing this slot's uniform buffer safe: no queued
    // work is still reading it.
    const glm::mat4 viewProjection = projectionMatrix() * view;
    updateShadowCascades(view);

    FrameUniforms uniforms{};
    uniforms.sunDirection = glm::vec4{m_sunDirection, m_handheldLight};
    uniforms.lighting = glm::vec4{m_ambientLight, m_sunLight, m_ambientFloor, m_skyEmission};
    uniforms.animation = glm::vec4{m_animatedLayer, m_animatedFrame, m_animatedLayer2, m_animatedFrame2};
    uniforms.fog = glm::vec4{m_fogColour, m_fogDistance};
    uniforms.eye = glm::vec4{m_eyePosition, m_fogStartFraction};
    uniforms.inverseViewProjection = glm::inverse(viewProjection);
    for (std::uint32_t cascade = 0; cascade < ShadowMap::kMaxCascades; ++cascade) {
        uniforms.shadowMatrices[cascade] = m_shadowMatrices[cascade];
    }
    uniforms.shadowTexelWorld = m_shadowTexelWorld;
    uniforms.shadowParams = glm::vec4{static_cast<float>(m_liveShadowCascades), m_shadowDistance,
                                      kShadowDepthBias, 1.0f / static_cast<float>(m_shadowMap->resolution())};
    uniforms.cloud = glm::vec4{m_cloudDrift, m_cloudCoverage, m_cloudShadowStrength,
                               static_cast<float>(m_cloudQuality)};
    uniforms.water = glm::vec4{m_waterTime, m_waterWaves, m_waterReflection, m_shadowDarkness};
    uniforms.waterDetail = glm::vec4{m_waterFoam, m_waterCaustics, m_waterRefraction * 0.35f,
                                     static_cast<float>(m_blockTextures->width())};
    uniforms.weather = m_weather;
    uniforms.wind = m_wind;
    uniforms.glow = m_glow;
    // How deep the sun and moon sit, so the cloud pass can be held just in
    // front of them. Computed from the same distance the game draws them at
    // rather than guessed, because depth values at the far end of the buffer
    // are crowded close enough together that a hand-picked constant is a race:
    // the old one lost to the sun for every cloud further away than it, which
    // near the horizon is most of them.
    float cloudCeiling = 0.999999f;
    if (m_skyDistance > 0.0f) {
        const glm::mat4 projection = projectionMatrix();
        const float viewZ = -m_skyDistance;
        const float clipZ = projection[2][2] * viewZ + projection[3][2];
        const float clipW = projection[2][3] * viewZ + projection[3][3];
        if (clipW > 0.0f) {
            cloudCeiling = std::min(cloudCeiling, clipZ / clipW - 4.0e-6f);
        }
    }
    uniforms.volumetric = glm::vec4{m_volumetric.x, m_volumetric.y, cloudCeiling, 0.0f};
    std::memcpy(m_frameUniformMapped[m_currentFrame], &uniforms, sizeof(uniforms));

    // Live allocations only ever grow with render distance, so this is checked
    // as the world streams rather than once at startup. Warned about once: it is
    // a headroom report, not a fault, and repeating it every frame would bury
    // everything else.
    if (!m_allocationWarningIssued && m_maxMemoryAllocations > 0 &&
        liveDeviceAllocations() > m_maxMemoryAllocations / 4 * 3) {
        logWarn("GPU memory allocations at " + std::to_string(liveDeviceAllocations()) + " of a " +
                std::to_string(m_maxMemoryAllocations) +
                " limit. Lower the render distance, or mesh buffers need sub-allocating.");
        m_allocationWarningIssued = true;
    }

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = vkAcquireNextImageKHR(device, m_swapchain.handle(), UINT64_MAX,
                                                         m_imageAvailable[m_currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // The minimized path's rule, in the other place that needs it: this
        // returns without reaching the flush below, so copies recorded since
        // the last frame would stay unsubmitted while the counter moves on. A
        // window being dragged can fail to acquire several iterations running,
        // which is all it takes for `freeRetiredMeshes` to destroy a buffer one
        // of those copies still names.
        m_uploads->waitForCompletion();
        recreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquireResult, "vkAcquireNextImageKHR");
    }

    // Reset only once we know we are definitely submitting work this frame,
    // otherwise an early return above would leave the fence unsignalled forever.
    vkCheck(vkResetFences(device, 1, &m_frameInFlight[m_currentFrame]), "vkResetFences");

    const VkCommandBuffer commandBuffer = m_commandBuffers[m_currentFrame];
    vkCheck(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");

    // Uploads go in first. Same queue, so submission order plus the barrier at
    // the end of the batch is what makes the new geometry visible to these draws.
    m_uploads->flush();

    recordCommands(commandBuffer, imageIndex, color, viewProjection, overlayTransform);

    // Recording is const, so the counters it fills are published here.
    m_stats.drawCalls = m_frameDrawCalls;
    m_stats.triangles = m_frameTriangles;
    m_stats.deviceAllocations = liveDeviceAllocations();
    m_stats.deviceAllocationLimit = m_maxMemoryAllocations;
    constexpr VkDeviceSize kMegabyte = 1024u * 1024u;
    m_stats.pooledMegabytesUsed = static_cast<std::uint32_t>(pooledBytesInUse() / kMegabyte);
    m_stats.pooledMegabytesHeld = static_cast<std::uint32_t>(pooledBytesReserved() / kMegabyte);
    // `UploadContext` already counts its submissions and nothing ever read the
    // number, so the one measurement that would say whether staging is
    // batching or thrashing could not be taken. Surfacing it costs a load.
    m_stats.uploadSubmissions = static_cast<std::uint32_t>(m_uploads->submissionCount());
    if (m_timestampsSupported) {
        m_timestampsPending[m_currentFrame] = true;
    }

    // We now write the image as a colour attachment rather than a transfer
    // target, so the wait happens at the stage that actually does that writing.
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailable[m_currentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinished[imageIndex];

    vkCheck(vkQueueSubmit(m_context.graphicsQueue(), 1, &submitInfo, m_frameInFlight[m_currentFrame]),
            "vkQueueSubmit");

    const VkSwapchainKHR swapchain = m_swapchain.handle();

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinished[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    const VkResult presentResult = vkQueuePresentKHR(m_context.presentQueue(), &presentInfo);
    const bool windowResized = m_window.consumeResizedFlag();

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || windowResized) {
        recreateSwapchain();
        return;
    }
    vkCheck(presentResult, "vkQueuePresentKHR");

    m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
}

} // namespace engine
