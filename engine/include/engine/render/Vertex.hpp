#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>

namespace engine {

/// The single definition of what a vertex is.
///
/// The shader's `layout(location = ...)` inputs must match `attributeDescriptions()`
/// exactly. Keeping the struct and its descriptions together means a format change
/// is one edit here plus one in the shader, never a hunt across the renderer.
struct Vertex {
    float position[3];
    /// Face shading and opacity, eight bits a channel, multiplied with the
    /// sampled texel. The hardware expands it back to floats on the way into the
    /// shader, so nothing there changed when it stopped being four floats - and
    /// world vertices only ever carried values quantised to 0-15 or to a
    /// four-step table anyway.
    ///
    /// World geometry reads it as sky light, block light, face shade, opacity.
    /// Screen and creature geometry read it as an ordinary colour tint.
    std::uint32_t color;
    float uv[2];
    /// Which layer of the texture array to sample. **A float, and it must stay
    /// one**: negative values are the agreed signals for the HUD sheet, the
    /// font, a creature skin and the two damage tints, and they are compared
    /// against thresholds like -2.5 in three separate places.
    float layer;
    /// Which way this surface faces and how boxed-in it is. See
    /// `packVertexSurface`.
    std::uint32_t surface;

    static VkVertexInputBindingDescription bindingDescription();
    static std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions();
};

static_assert(sizeof(Vertex) == 32, "Resident vertex data is the renderer's largest cost; keep this small");

/// Packs a colour the way `VK_FORMAT_R8G8B8A8_UNORM` reads it: red in the lowest
/// byte, because a vertex attribute is read in memory order.
constexpr std::uint32_t packVertexColor(float r, float g, float b, float a) {
    const auto quantise = [](float value) {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
    };
    return quantise(r) | (quantise(g) << 8) | (quantise(b) << 16) | (quantise(a) << 24);
}

/// Which way a face points, as three bits.
///
/// Every face of a voxel world points along one of exactly six axes, so this is
/// **exact rather than an approximation** - an octahedral encoding would spend
/// sixteen bits being less accurate about something already known perfectly.
enum : std::uint32_t {
    kNormalPosX = 0,
    kNormalNegX = 1,
    kNormalPosY = 2,
    kNormalNegY = 3,
    kNormalPosZ = 4,
    kNormalNegZ = 5,
    /// The escape hatch, designed in rather than discovered later: a creature
    /// limb is a rotated box and a plant blade sits at 45 degrees, so neither
    /// has an axis to name. The shader falls back to the geometric normal.
    kNormalUnaligned = 7,
};

/// bits 0-2 the normal code, bits 3-10 ambient occlusion, bit 11 fluid top,
/// bits 12-15 how far above its own root a swaying vertex sits, in half blocks.
///
/// **A height rather than a flag, because a plant taller than one block is one
/// plant.** A flag can only say "this vertex moves", so every block of a sugar
/// cane bent its own top half against its own bottom half and the stalk came
/// apart at every boundary. Measured from the root instead, the value is
/// continuous across a block edge and the whole stalk leans as one thing.
/// Zero means it does not move at all.
constexpr std::uint32_t packVertexSurface(std::uint32_t normalCode, float ambientOcclusion,
                                          bool fluidTop = false, std::uint32_t swayHalfBlocks = 0) {
    const float clamped = ambientOcclusion < 0.0f ? 0.0f : (ambientOcclusion > 1.0f ? 1.0f : ambientOcclusion);
    const auto occlusion = static_cast<std::uint32_t>(clamped * 255.0f + 0.5f);
    return (normalCode & 7u) | (occlusion << 3) | (fluidTop ? (1u << 11) : 0u) |
           ((swayHalfBlocks > 15u ? 15u : swayHalfBlocks) << 12);
}

/// What one block's worth of height is, in the units above: the top of a single
/// blade of grass, which is what every sway amount is measured against.
inline constexpr std::uint32_t kSwayOneBlock = 2;

/// What everything that is not a merged voxel face carries: no axis to name and
/// nothing occluding it.
inline constexpr std::uint32_t kVertexSurfaceDefault = packVertexSurface(kNormalUnaligned, 1.0f);

static_assert(((packVertexSurface(kNormalUnaligned, 1.0f, false, 6) >> 12) & 15u) == 6 &&
                  ((packVertexSurface(kNormalUnaligned, 1.0f, false, 6) >> 3) & 255u) == 255,
              "the sway height and the occlusion must not overlap in the surface word");

} // namespace engine
