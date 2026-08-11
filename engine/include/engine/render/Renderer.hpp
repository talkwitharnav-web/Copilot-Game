#pragma once

#include "engine/render/Buffer.hpp"
#include "engine/render/DepthImage.hpp"
#include "engine/render/FrameUniforms.hpp"
#include "engine/render/GraphicsPipeline.hpp"
#include "engine/render/MeshData.hpp"
#include "engine/render/PushConstants.hpp"
#include "engine/render/RenderTarget.hpp"
#include "engine/render/ShadowMap.hpp"
#include "engine/render/Swapchain.hpp"
#include "engine/render/TextureArray.hpp"
#include "engine/render/UploadContext.hpp"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <array>
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
    /// Live `vkAllocateMemory` results, and what the device will allow. **A
    /// hard limit with a spec floor of 4096**, and until it was reported here
    /// the only way to see it approaching was the warning at three quarters.
    std::uint32_t deviceAllocations = 0;
    std::uint32_t deviceAllocationLimit = 0;
    /// Megabytes of pooled buffer memory handed out, and held. The gap is what
    /// sharing blocks costs.
    std::uint32_t pooledMegabytesUsed = 0;
    std::uint32_t pooledMegabytesHeld = 0;
};

/// Drives one frame of GPU work: acquire an image, record commands, submit, present.
class Renderer {
public:
    /// `blockTextures` are loaded into a texture array in the order given; the
    /// index into that list is what a vertex's `layer` refers to. `hudTexture`,
    /// `fontTexture` and `skinTexture` are separate sheets, selected by negative
    /// layers, because a texture array needs every layer the same size.
    Renderer(const VulkanContext& context, Window& window, const std::vector<std::filesystem::path>& blockTextures,
             const std::filesystem::path& hudTexture, const std::filesystem::path& fontTexture,
             const std::filesystem::path& skinTexture);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    /// Uploads a new mesh and returns its handle. An empty mesh is valid and
    /// occupies a slot without any GPU memory.
    /// `translucent` geometry is drawn in a second pass, after every opaque mesh
    /// in the scene. Blending is order-dependent, so it cannot simply sit in the
    /// same buffer and be drawn whenever its chunk comes up.
    MeshHandle addMesh(const MeshData& mesh, bool translucent = false);

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

    /// The same, but cut off at a rectangle.
    ///
    /// A second draw rather than a flag on the first, because the cut is a
    /// scissor and a scissor is per draw call. It exists for lists that scroll:
    /// the reference clips its last row **through** a cell, and there is no
    /// other way to end geometry at an arbitrary line - scaling the contents to
    /// fit changes their size, and clipping by hand is impossible for anything
    /// that is not an axis-aligned rectangle, which an isometric block icon is
    /// not.
    ///
    /// `min` and `max` are in the same units as the mesh, so the caller never
    /// has to know about pixels or the aspect ratio.
    void setClippedScreenMesh(const MeshData& mesh, const glm::vec2& min, const glm::vec2& max);

    /// Screen geometry drawn **after** the clipped layer.
    ///
    /// The depth test keeps the first fragment at a given distance and a
    /// blended fragment still writes depth, so anything drawn nearer than the
    /// clipped layer but *earlier* than it stamps a hole through it wherever
    /// its own texture is transparent. That is exactly what the stack on the
    /// cursor did to the catalogue behind it. Ordering is the only fix while
    /// the HUD shares the world pipeline.
    void setTopScreenMesh(const MeshData& mesh);

    /// Geometry drawn in world space with its own transform and no directional
    /// lighting, for the sun and anything else pinned to the sky.
    ///
    /// Two slots, because there are exactly two things in the sky and they are
    /// on opposite sides of it - one mesh and one transform cannot describe
    /// both. Not a list: a third celestial body is not coming.
    static constexpr std::size_t kSkySlots = 2;
    void setSkyMesh(const MeshData& mesh, std::size_t slot = 0);
    void setSkyTransform(const glm::mat4& transform, std::size_t slot = 0) {
        m_skyTransforms[slot] = transform;
    }

    /// Direction *toward* the sun. Drives the directional term on world
    /// geometry; sky and HUD geometry ignore it.
    void setSunDirection(const glm::vec3& direction);

    /// `ambient` is the floor every surface receives, `sun` is what a surface
    /// facing the sun adds on top.
    void setSunLighting(float ambient, float sun, float ambientFloor);

    /// Redirects one texture layer to another for this frame.
    ///
    /// Geometry meshed with `meshedLayer` samples `currentLayer` instead, which
    /// is how an animated surface plays without a single chunk being rebuilt.
    /// Pass the same value twice to leave it alone.
    void setAnimatedLayer(float meshedLayer, float currentLayer);

    /// A second independent swap, riding the two spare components of the same
    /// push constant. Two is all there is room for without growing a block that
    /// is already past what Vulkan guarantees.
    void setSecondAnimatedLayer(float meshedLayer, float currentLayer);

    /// Fades world geometry toward `colour`, reaching it at `distance` metres
    /// and beginning at `startFraction` of the way there. A distance of 0 turns
    /// it off. Screen-space geometry is never fogged.
    ///
    /// `startFraction` of 0 fades from the eye outward and applies to unlit
    /// geometry too, which is what a fully submerged view wants. Anything above
    /// 0 leaves the sky and the sun alone — they are drawn unlit through this
    /// same pipeline, and fogging them would dissolve the sun into the sky.
    void setFog(const glm::vec3& colour, float distance, float startFraction = 0.0f);

    /// Which curve squashes the high dynamic range image back into something a
    /// monitor can show. Purely a matter of taste, which is why it is a setting.
    void setToneMapper(ToneMapper mapper) { m_toneMapper = mapper; }
    ToneMapper toneMapper() const { return m_toneMapper; }

    /// Multiplies the scene before the tone curve. Deliberately manual: an
    /// automatic exposure would make a torchlit cave's brightness depend on
    /// where you were looking two seconds ago, and light level 0-15 is a game
    /// mechanic that has to stay readable from the screen.
    void setExposure(float exposure);
    float exposure() const { return m_exposure; }

    /// `strength` is how far the final image is mixed toward the blurred copy.
    void setBloom(bool enabled, float strength);
    bool bloomEnabled() const { return m_bloomEnabled; }
    float bloomStrength() const { return m_bloomStrength; }

    /// Multiplies anything drawn through `setSkyMesh`, so the sun can genuinely
    /// be brighter than white and bloom around its edge.
    void setSkyEmission(float scale) { m_skyEmission = scale; }

    /// How much of the world the sun's own view is rendered into, and therefore
    /// how crisp a cast shadow is and how far it reaches. 0 turns cast shadows
    /// off entirely and skips the whole pass.
    ///
    /// Changing it rebuilds the shadow map, which means waiting for the GPU - so
    /// it is a settings action, not something to call per frame.
    void setShadowQuality(int quality);
    int shadowQuality() const { return m_shadowQuality; }
    static constexpr int kShadowQualityCount = 4;

    /// A point light sitting on the camera. **Off by default, and that is a
    /// decision rather than a default value.** Its direction is the view
    /// direction, so every surface faced head-on sits at the peak of its own
    /// specular highlight - a bright spot glued to the middle of the screen that
    /// follows the player around. Offered because it is the only light indoors
    /// with a position, and left off because it does not look like daylight.
    void setHandheldLight(float intensity) { m_handheldLight = intensity; }
    float handheldLight() const { return m_handheldLight; }

    /// The cloud deck. `quality` is 0 off, 1 fast, 2 fancy - which is how many
    /// steps each ray takes. `coverage` runs 0 to 1 and is how much of the sky
    /// is cloud. `shadowStrength` is how far a cloud darkens the ground under
    /// it, which is the cheapest part of the whole effect and most of what
    /// makes it read.
    void setClouds(int quality, float coverage, float shadowStrength);
    int cloudQuality() const { return m_cloudQuality; }
    static constexpr int kCloudQualityCount = 3;

    /// How far the deck has drifted, in blocks. Supplied by the game rather than
    /// accumulated here, so it can be tied to the world clock.
    void setCloudDrift(float blocks) { m_cloudDrift = blocks; }

    /// The water surface. `waveStrength` is how steeply the ripples tilt it;
    /// `reflection` scales how much sky it returns, where 1 is the physical
    /// answer and lower is a stylistic choice.
    void setWater(float waveStrength, float reflection);

    /// Foam where water meets land, the caustic net under it, and how far the
    /// surface bends what is seen through it. All three are 0 to 1 and all three
    /// are matters of taste rather than of cost.
    void setWaterDetail(float foam, float caustics, float refraction);

    /// The ripple clock, in seconds. Supplied by the game and wrapped there, for
    /// the same reason the cloud drift is.
    void setWaterTime(float seconds) { m_waterTime = seconds; }

    /// How far a cast shadow darkens the ambient sky as well as the sun, 0 to 1.
    ///
    /// Cutting the directional half is all a shadow map can do on its own, and
    /// that alone leaves shadows reading as pale patches. A point the sun cannot
    /// see is also cut off from a good part of the sky, so this is both the
    /// honest answer and the only dial with real range in it.
    void setShadowDarkness(float darkness);

    /// The falling curtain, as one mesh of camera-following quads. An empty mesh
    /// draws nothing, which is how the pass is switched off.
    void setPrecipitationMesh(const MeshData& mesh);

    /// `level` is how hard it is coming down, 0 to 1. `snow` picks the other
    /// look entirely - slower, opaque, white and wandering. `fallenBlocks` is
    /// the curtain's clock and `slant` the tangent the wind leans it by.
    void setPrecipitation(float level, bool snow, float fallenBlocks, float slant);

    /// Lightning, as world-space geometry. Empty draws nothing.
    void setBoltMesh(const MeshData& mesh);

    /// Short-lived world geometry, rebuilt every frame by the game. Cutout and
    /// lit through the same path world geometry uses, so it needs no pipeline
    /// of its own.
    void setParticleMesh(const MeshData& mesh);

    /// `direction` is which way the wind blows, `bend` how far it pushes a blade
    /// of grass in blocks, and `clock` the sway's own wrapped timer.
    void setWind(const glm::vec2& direction, float bend, float clock);

    /// The halo around whichever body is above the horizon. Warm and strong for
    /// the sun, cool and faint for the moon - there is one directional light, so
    /// the shader cannot tell which it is drawing.
    void setSkyGlow(const glm::vec3& colour, float strength);

    /// How much of the screen's resolution the world is actually drawn at.
    ///
    /// **The world shrinks; the interface does not.** Everything up to and
    /// including the lighting, water and bloom runs at this fraction, and the
    /// tone map pass is what stretches it back to the window - so the HUD, the
    /// text and the inventory are still drawn at full resolution afterwards and
    /// stay sharp. That split is the whole reason this is worth having.
    ///
    /// 1.0 is off. Below about 0.5 the block grid starts to alias into itself,
    /// which is why the setting clamps there.
    void setRenderScale(float scale);
    float renderScale() const { return m_renderScale; }

    /// The size the world is drawn at, as opposed to the size it is presented
    /// at. Every offscreen image and every pass before the tone map uses this.
    VkExtent2D renderExtent() const;

    /// How far out the game draws the sun and moon.
    ///
    /// **The cloud deck needs this to stay in front of them.** Both are drawn
    /// against the far end of the depth buffer, where the values are crowded so
    /// close together that a hand-picked clamp in the cloud shader is a race
    /// rather than an ordering - and one the sun won for every cloud further
    /// away than it, which is most of them near the horizon. Given the distance,
    /// the depth of the bodies can be computed exactly and the clouds held just
    /// in front of it.
    void setSkyDistance(float distance) { m_skyDistance = distance; }

    /// Where the water surface is above a submerged eye, and how strongly light
    /// comes down through it. A strength of zero is off, which is what every
    /// frame above water passes.
    void setUnderwaterShafts(float surfaceHeight, float strength) {
        m_volumetric.x = surfaceHeight;
        m_volumetric.y = strength;
    }

    /// `antiAlias` 0 to 1 softens hard edges after the tone curve; **off by
    /// default**, because everything here is a hard-edged square and softening
    /// diagonals also softens every texel boundary in the world.
    /// `contactOcclusion` is a radius in screen pixels for the depth-based
    /// darkening where surfaces meet, 0 to switch it off.
    void setImageQuality(float antiAlias, float contactOcclusion);

    /// One packed word per texture layer saying what that surface is made of:
    /// roughness, whether it is metal, how much light it gives off, and a flag
    /// byte. Indexed by the layer a vertex already carries.
    ///
    /// The engine deliberately does not know how these are decided - it is
    /// handed a table and reads it. `game/src/world/Material.hpp` owns the
    /// meaning, and the packing is stated in both places.
    void setMaterialTable(const std::vector<std::uint32_t>& rows);

    /// Replaces the final image with one of the G-buffer's own channels. 0 is
    /// off; 1 albedo, 2 normal, 3 roughness, 4 metallic, 5 ambient occlusion,
    /// 6 emissive, 7 sky and block light, 8 distance, 9 cast shadow.
    ///
    /// This is how a deferred renderer is debugged: when the picture is wrong,
    /// one of these says which input is wrong.
    void setDebugView(int view);
    int debugView() const { return m_debugView; }
    static constexpr int kDebugViewCount = 10;

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

    /// The block texture array's dimensions, and the alpha it was loaded with.
    ///
    /// Handed out as plain numbers rather than as the texture itself, so nothing
    /// outside the engine has to touch a Vulkan type to ask what shape a sprite
    /// cuts out.
    std::uint32_t textureWidth() const;
    std::uint32_t textureHeight() const;
    std::uint32_t textureLayerCount() const;
    std::uint8_t textureAlphaAt(std::uint32_t layer, std::uint32_t x, std::uint32_t y) const;

    /// The same for the font atlas. A variable-width font's advances are a
    /// property of the artwork, so measuring them off whatever actually loaded
    /// is the only way they can have one owner - a written-down table goes
    /// stale the moment the atlas is swapped for the reference's.
    std::uint32_t fontWidth() const;
    std::uint32_t fontHeight() const;
    std::uint8_t fontAlphaAt(std::uint32_t x, std::uint32_t y) const;

    /// Buffers freed but still held back until in-flight frames finish with
    /// them. Should hover near zero; sustained growth means the release logic
    /// has stopped running.
    std::size_t retiredMeshCount() const { return m_retired.size(); }

private:
    void createCommandResources();
    void createFrameUniformBuffers();
    void createDescriptorResources();
    void createPostSamplers();
    /// Builds the shadow map and its pipeline at the current quality, and points
    /// the lighting pass's descriptor sets at it.
    void createShadowResources();
    /// Fits every cascade to its slice of the view frustum. Writes
    /// `m_shadowMatrices`, so it runs before the frame's uniforms are uploaded.
    void updateShadowCascades(const glm::mat4& view);
    /// The HDR scene image and the bloom pyramid, both sized from the swapchain
    /// and therefore rebuilt whenever it is.
    void createRenderTargets();
    /// Points the post-processing descriptor sets at the current targets. Must
    /// run again after every resize, because the images they name are new ones.
    void writePostDescriptorSets();
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
        /// Drawn in the second pass. Blending depends on draw order, so these
        /// cannot be interleaved with opaque geometry.
        bool translucent = false;
    };

    /// Allocates and fills a slot's buffers. Any previous contents are retired
    /// first, so peak memory is one copy rather than two.
    void uploadInto(GpuMesh& slot, const MeshData& mesh);
    /// The same, with an index order of the caller's choosing. Screen geometry
    /// uses it to upload a painter's-order index list without copying vertices.
    void uploadInto(GpuMesh& slot, const std::vector<Vertex>& vertices,
                    const std::vector<std::uint32_t>& indices);

    /// Hands a mesh's buffers to the delayed-destruction list.
    ///
    /// Frames already submitted may still be reading them, and streaming
    /// replaces meshes far too often to stall the GPU each time. Holding them
    /// for a few frames costs a little memory and removes the stall entirely.
    void retire(GpuMesh& slot);
    void freeRetiredMeshes();

    /// True when a mesh is wholly past the point where fog reaches full
    /// opacity, so it would be drawn and then painted over with solid sky.
    ///
    /// **Only the distance fog may cull.** Underwater fog is dense and close,
    /// and culling on it would throw away everything more than a few metres off
    /// while the surface above is still perfectly visible.
    bool beyondFog(const GpuMesh& mesh) const;

    void recordCommands(VkCommandBuffer commandBuffer, std::uint32_t imageIndex, const ClearColor& color,
                        const glm::mat4& viewProjection, const std::optional<glm::mat4>& overlayTransform) const;
    /// The world as the sun sees it, depth only, once per cascade.
    void recordShadowPass(VkCommandBuffer commandBuffer) const;
    /// Opaque geometry, written into the G-buffer rather than shaded.
    void recordGeometryPass(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection) const;
    /// Shades every opaque pixel at once from what the geometry pass wrote.
    void recordDeferredPass(VkCommandBuffer commandBuffer, const ClearColor& color) const;
    /// Duplicates the lit scene before anything translucent is drawn into it.
    ///
    /// Water reflects the world by marching a ray through this copy. It cannot
    /// read the image it is drawing into, and it must not see itself.
    void recordSceneCopy(VkCommandBuffer commandBuffer) const;
    /// Sky, water and the block outline: everything that blends, and so cannot
    /// go through a G-buffer at all.
    void recordForwardPass(VkCommandBuffer commandBuffer, const glm::mat4& viewProjection,
                           const std::optional<glm::mat4>& overlayTransform) const;
    /// Builds the blurred copy of the scene that the tone map pass mixes in.
    void recordBloomPasses(VkCommandBuffer commandBuffer) const;
    /// Reads the HDR scene, applies exposure and a tone curve, writes the screen.
    void recordToneMapPass(VkCommandBuffer commandBuffer, std::uint32_t imageIndex) const;
    /// Screen geometry, drawn into the swapchain after everything else with no
    /// depth attachment at all.
    void recordUiPass(VkCommandBuffer commandBuffer, std::uint32_t imageIndex) const;
    /// How many frames the CPU is allowed to work on before waiting for the GPU.
    static constexpr std::uint32_t kFramesInFlight = 2;

    /// Sampled sheets: block array, HUD, font, creature skins. Named rather
    /// than written as a literal in three places, which is how a binding count
    /// gets changed in two of them.
    static constexpr std::uint32_t kTextureBindingCount = 4;
    /// The per-frame uniform block sits directly after them.
    static constexpr std::uint32_t kFrameUniformBinding = kTextureBindingCount;
    /// The per-layer material table, read straight out of a buffer rather than
    /// a texture - which sidesteps the trap that every texture in this engine is
    /// an _SRGB format, and a roughness of 0.5 gamma-decoded is 0.214.
    static constexpr std::uint32_t kMaterialBinding = kFrameUniformBinding + 1;
    /// The lit scene as it stood before anything translucent was drawn into it,
    /// with each pixel's distance from the eye in its alpha. Water reflects the
    /// world out of this.
    static constexpr std::uint32_t kSceneCopyBinding = kMaterialBinding + 1;

    /// Sixteen bits per channel with a sign bit. R11G11B10 would halve the
    /// bandwidth but has no sign, an uneven mantissa that discolours smooth
    /// gradients, and roughly twice the error near 1.0 - and translucent world
    /// geometry blends into this image, which is exactly where that shows.
    static constexpr VkFormat kSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    /// Nothing blends into the bloom chain but its own upsample, so the smaller
    /// format is free here.
    static constexpr VkFormat kBloomFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    /// Past eight levels the coarsest mip is a handful of texels and adds
    /// nothing but cost.
    static constexpr std::uint32_t kMaxBloomMips = 8;

    /// What each surface **is**, rather than what it looks like once lit.
    /// Albedo is _SRGB so dark values keep their precision through eight bits;
    /// the other two hold plain 0-1 numbers and are UNORM.
    static constexpr VkFormat kGbufferAlbedoFormat = VK_FORMAT_R8G8B8A8_SRGB;
    static constexpr VkFormat kGbufferLightFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr VkFormat kGbufferMaterialFormat = VK_FORMAT_R8G8B8A8_UNORM;

    const VulkanContext& m_context;
    Window& m_window;
    Swapchain m_swapchain;
    std::unique_ptr<DepthImage> m_depthImage;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::unique_ptr<TextureArray> m_blockTextures;
    std::unique_ptr<TextureArray> m_hudTexture;
    std::unique_ptr<TextureArray> m_fontTexture;
    std::unique_ptr<TextureArray> m_skinTexture;
    std::unique_ptr<UploadContext> m_uploads;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    /// One per frame in flight, because each carries that frame's own uniform
    /// buffer. The four texture bindings are identical in all of them.
    std::vector<VkDescriptorSet> m_descriptorSets;

    /// Written by the CPU and read by the GPU in the same frame, so there is one
    /// per frame in flight and the fence wait is what makes writing it safe.
    /// Mapped once at creation: mapping is not free and this is written every
    /// frame for the life of the process.
    std::vector<std::unique_ptr<Buffer>> m_frameUniformBuffers;
    std::vector<void*> m_frameUniformMapped;

    std::unique_ptr<Buffer> m_materialBuffer;
    std::size_t m_materialRowCount = 0;

    std::unique_ptr<GraphicsPipeline> m_trianglePipeline;
    /// Opaque world geometry. Writes what a surface is, and shades nothing.
    std::unique_ptr<GraphicsPipeline> m_gbufferPipeline;
    /// Reads that back and lights the whole screen in one draw.
    std::unique_ptr<GraphicsPipeline> m_deferredPipeline;
    /// Screen space only. Its whole reason for existing is that it has no depth
    /// attachment - see `hud.frag`.
    std::unique_ptr<GraphicsPipeline> m_hudPipeline;
    std::unique_ptr<GraphicsPipeline> m_bloomDownPipeline;
    std::unique_ptr<GraphicsPipeline> m_bloomUpPipeline;
    std::unique_ptr<GraphicsPipeline> m_toneMapPipeline;
    /// Depth only, with a slope-scaled bias. Nothing else in the renderer needs
    /// one, which is why the bias is a pipeline field rather than dynamic state.
    std::unique_ptr<GraphicsPipeline> m_shadowPipeline;
    /// A full-screen raymarch drawn inside the forward pass. It writes
    /// `gl_FragDepth` so terrain occludes it, which is what lets it depth-test
    /// without sampling a depth buffer that is bound as a writable attachment.
    std::unique_ptr<GraphicsPipeline> m_cloudPipeline;
    /// Rain and snow. Blended, depth-tested, and **never depth-writing**: a
    /// transparent fragment that stamps its distance punches a hole through
    /// everything drawn after it.
    std::unique_ptr<GraphicsPipeline> m_precipitationPipeline;

    std::unique_ptr<ShadowMap> m_shadowMap;
    int m_shadowQuality = 2;
    /// How far cast shadows reach, in metres. Derived from the quality and
    /// capped by the far plane, so a low render distance does not spend a whole
    /// cascade on geometry nobody can see.
    float m_shadowDistance = 0.0f;
    /// How many cascades were actually fitted this frame. Zero whenever the sun
    /// is at or below the horizon, which skips the pass entirely at night.
    std::uint32_t m_liveShadowCascades = 0;
    std::array<glm::mat4, ShadowMap::kMaxCascades> m_shadowMatrices{};
    /// One shadow texel in world metres, per cascade.
    glm::vec4 m_shadowTexelWorld{0.0f};
    float m_handheldLight = 0.0f;
    int m_cloudQuality = 2;
    float m_cloudCoverage = 0.5f;
    float m_cloudShadowStrength = 0.55f;
    float m_cloudDrift = 0.0f;
    float m_waterTime = 0.0f;
    float m_waterWaves = 1.0f;
    float m_waterReflection = 1.0f;
    float m_waterFoam = 1.0f;
    float m_waterCaustics = 1.0f;
    float m_waterRefraction = 1.0f;
    float m_shadowDarkness = 0.55f;
    glm::vec4 m_weather{0.0f};
    glm::vec4 m_wind{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 m_glow{1.0f, 0.84f, 0.62f, 0.0f};
    glm::vec4 m_volumetric{0.0f, 0.0f, 0.0f, 0.0f};
    float m_skyDistance = 0.0f;
    float m_renderScale = 1.0f;
    float m_antiAlias = 0.0f;
    float m_contactOcclusion = 24.0f;

    std::unique_ptr<RenderTarget> m_sceneColor;
    std::unique_ptr<RenderTarget> m_sceneCopy;
    std::unique_ptr<RenderTarget> m_bloom;
    std::unique_ptr<RenderTarget> m_gbufferAlbedo;
    std::unique_ptr<RenderTarget> m_gbufferLight;
    std::unique_ptr<RenderTarget> m_gbufferMaterial;
    /// Clamped to a black border, so the widest downsample taps do not wrap the
    /// far edge of the screen into the near one.
    VkSampler m_borderSampler = VK_NULL_HANDLE;
    /// Clamped to the edge texel, which is what stops an upsample brightening
    /// the frame's outer rim.
    VkSampler m_edgeSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_postSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_toneMapSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_postPool = VK_NULL_HANDLE;
    /// One per mip: index 0 reads the scene, the rest read the mip above.
    std::vector<VkDescriptorSet> m_bloomDownSets;
    /// One per mip, reading that mip to blend into the finer one below it.
    std::vector<VkDescriptorSet> m_bloomUpSets;
    VkDescriptorSet m_toneMapSet = VK_NULL_HANDLE;
    /// The deferred pass needs the three G-buffer images, depth, and the frame's
    /// own uniform block - so there is one per frame in flight even though only
    /// the last of the five differs between them.
    VkDescriptorSetLayout m_deferredSetLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_deferredSets;

    ToneMapper m_toneMapper = ToneMapper::PbrNeutral;
    float m_exposure = 1.0f;
    bool m_bloomEnabled = true;
    float m_bloomStrength = 0.12f;
    float m_skyEmission = 1.0f;
    int m_debugView = 0;

    std::vector<GpuMesh> m_meshes;
    std::vector<MeshHandle> m_freeSlots;
    GpuMesh m_overlayMesh;
    GpuMesh m_screenMesh;
    GpuMesh m_clippedScreenMesh;
    GpuMesh m_topScreenMesh;
    glm::vec2 m_screenClipMin{0.0f};
    glm::vec2 m_screenClipMax{0.0f};
    GpuMesh m_skyMeshes[kSkySlots];
    GpuMesh m_precipitationMesh;
    GpuMesh m_boltMesh;
    GpuMesh m_particleMesh;
    glm::mat4 m_skyTransforms[kSkySlots]{glm::mat4{1.0f}, glm::mat4{1.0f}};
    glm::vec3 m_sunDirection{0.0f, 1.0f, 0.0f};
    float m_ambientLight = 0.55f;
    float m_sunLight = 0.45f;
    float m_ambientFloor = 0.0f;
    /// Negative means nothing is redirected, which no real layer can match.
    float m_animatedLayer = -1.0f;
    float m_animatedFrame = -1.0f;
    float m_animatedLayer2 = -1.0f;
    float m_animatedFrame2 = -1.0f;
    glm::vec3 m_fogColour{0.0f};
    float m_fogDistance = 0.0f;
    float m_fogStartFraction = 0.0f;
    glm::vec3 m_eyePosition{0.0f};

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

    /// Vulkan caps how many memory allocations may exist at once. Nothing else
    /// watches it, and the ceiling is reachable at high render distances.
    std::uint32_t m_maxMemoryAllocations = 0;
    bool m_allocationWarningIssued = false;

    // Counted while recording, which is const, so they are mutable.
    mutable std::uint32_t m_frameDrawCalls = 0;
    mutable std::uint32_t m_frameTriangles = 0;

    /// Reused by the screen-mesh painter's sort so it allocates nothing after
    /// the first HUD rebuild.
    std::vector<std::uint32_t> m_sortedIndices;
    std::vector<std::uint32_t> m_sortOrder;
};

} // namespace engine
