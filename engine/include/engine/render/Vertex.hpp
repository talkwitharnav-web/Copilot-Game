#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
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
    /// against thresholds like -2.5 in three shaders - `triangle.frag`,
    /// `shadow.frag` and `gbuffer.frag` - four comparisons in all.
    float layer;
    /// Which way this surface faces and how boxed-in it is. See
    /// `packVertexSurface`.
    std::uint32_t surface;

    static VkVertexInputBindingDescription bindingDescription();

    /// **`constexpr` on purpose**: it is what lets the `static_assert`s below
    /// check the exact table `GraphicsPipeline` hands to Vulkan, rather than a
    /// restatement of it that can agree with nothing.
    static constexpr std::array<VkVertexInputAttributeDescription, 5> attributeDescriptions();
};

constexpr std::array<VkVertexInputAttributeDescription, 5> Vertex::attributeDescriptions() {
    std::array<VkVertexInputAttributeDescription, 5> attributes{};

    attributes[0].location = 0;
    attributes[0].binding = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, position);

    // UNORM, so the shader still receives a vec4 in 0..1 and never learns that
    // the buffer holds bytes.
    attributes[1].location = 1;
    attributes[1].binding = 0;
    attributes[1].format = VK_FORMAT_R8G8B8A8_UNORM;
    attributes[1].offset = offsetof(Vertex, color);

    attributes[2].location = 2;
    attributes[2].binding = 0;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = offsetof(Vertex, uv);

    attributes[3].location = 3;
    attributes[3].binding = 0;
    attributes[3].format = VK_FORMAT_R32_SFLOAT;
    attributes[3].offset = offsetof(Vertex, layer);

    attributes[4].location = 4;
    attributes[4].binding = 0;
    attributes[4].format = VK_FORMAT_R32_UINT;
    attributes[4].offset = offsetof(Vertex, surface);

    return attributes;
}

static_assert(sizeof(Vertex) == 32, "Resident vertex data is the renderer's largest cost; keep this small");

/// The layout `triangle.vert`, `hud.vert` and `shadow.vert` are compiled
/// against, in bytes from the start of the struct.
///
/// **Spelled out rather than derived**, because a derivation cannot disagree
/// with itself: reordering two members of `Vertex` moves both the struct and
/// every `offsetof` that reads it, and the only thing left holding still is the
/// shader, which nothing here compiles.
static_assert(offsetof(Vertex, position) == 0 && offsetof(Vertex, color) == 12 &&
                  offsetof(Vertex, uv) == 16 && offsetof(Vertex, layer) == 24 &&
                  offsetof(Vertex, surface) == 28,
              "a member of Vertex moved; every vertex shader hard-codes this order by location");

/// What `attributeDescriptions()` says about one `layout(location = ...)`, or a
/// sentinel if nothing claims that location at all.
///
/// A search rather than an index, so it answers the question the shader asks -
/// "what arrives at location 2" - instead of the question the table happens to
/// be written in, which is "what is the third row".
constexpr VkVertexInputAttributeDescription vertexAttributeAt(std::uint32_t location) {
    constexpr std::uint32_t kNone = 0xffffffffu;
    for (const VkVertexInputAttributeDescription& attribute : Vertex::attributeDescriptions()) {
        if (attribute.location == location) {
            return attribute;
        }
    }
    return VkVertexInputAttributeDescription{kNone, kNone, VK_FORMAT_UNDEFINED, kNone};
}

constexpr bool vertexAttributeIs(std::uint32_t location, VkFormat format, std::size_t offset) {
    const VkVertexInputAttributeDescription attribute = vertexAttributeAt(location);
    return attribute.location == location && attribute.binding == 0 && attribute.format == format &&
           attribute.offset == static_cast<std::uint32_t>(offset);
}

/// One assert per attribute, tying a location index to the member it reads and
/// the format it is read as.
///
/// **`sizeof(Vertex) == 32` above is not enough and never was.** It still passes
/// if two rows exchange their `location`, exchange their `format`, or point at
/// each other's member - and a hand-maintained table that agrees with its twin
/// until it silently does not is the defect that mirrored every shaped block in
/// this game for four milestones. Verified live rather than decorative: swapping
/// the locations of rows 2 and 3 in a copy of this table fails the two asserts
/// below and nothing else.
static_assert(vertexAttributeIs(0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)),
              "location 0 is inPosition, a vec3 of floats");
static_assert(vertexAttributeIs(1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Vertex, color)),
              "location 1 is inColor, four bytes the hardware expands to a vec4 in 0..1");
static_assert(vertexAttributeIs(2, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)),
              "location 2 is inUv, a vec2 of floats");
static_assert(vertexAttributeIs(3, VK_FORMAT_R32_SFLOAT, offsetof(Vertex, layer)),
              "location 3 is inLayer, and it must stay a float: negative values are sheet selectors");
static_assert(vertexAttributeIs(4, VK_FORMAT_R32_UINT, offsetof(Vertex, surface)),
              "location 4 is inSurface, an unsigned int of packed bit fields");

/// **The array's declared size against the rows actually written**, which is the
/// one thing the five asserts above cannot check.
///
/// `attributeDescriptions()` does not brace-initialise a literal table; it
/// value-initialises the array and then fills it by index. So the five asserts
/// answer "is the row I named correct" and nothing answers "is there a row
/// nobody named". Those differ: raise the size to six, add a member, forget the
/// `attributes[5]` block, and the leftover row is all zeros - which is
/// `location = 0` and `VK_FORMAT_UNDEFINED`, a second claimant to location 0.
/// **Every assert above still passes**, because `vertexAttributeAt` searches in
/// order and finds the real location 0 first, so the duplicate is unreachable
/// through the only accessor they use. The build is green and the pipeline is
/// handed an undefined format.
///
/// Counting non-`UNDEFINED` rows catches exactly that, and both sides come off
/// the same table, so it cannot drift from what `GraphicsPipeline` submits.
constexpr bool everyVertexAttributeIsWritten() {
    const auto attributes = Vertex::attributeDescriptions();
    std::size_t written = 0;
    for (const VkVertexInputAttributeDescription& attribute : attributes) {
        if (attribute.format != VK_FORMAT_UNDEFINED) {
            ++written;
        }
    }
    return written == attributes.size();
}

static_assert(everyVertexAttributeIsWritten(),
              "a row of attributeDescriptions() was never assigned; an unwritten row is not "
              "absent, it is location 0 with VK_FORMAT_UNDEFINED and it duplicates the real one");

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

/// The codes as `triangle.frag` and `gbuffer.frag` spell them, in literals.
///
/// **Spelled out rather than derived, for the same reason the `offsetof` block
/// above is**: both shaders answer this with a `switch` on the bare numbers -
/// `case 0u:` through `case 5u:` in `surfaceNormal()` in each of them, and
/// `const uint kNormalUnaligned = 7u;` in `triangle.frag` under a comment
/// saying "Must match the kNormal* codes in Vertex.hpp". Nothing compiled those
/// three copies together, so inserting a seventh direction, or moving the
/// escape hatch down to 6 to close the gap, renumbered everything below it here
/// and moved nothing there. Every face of the world would then be lit as though
/// it pointed somewhere else, with no warning from anything.
///
/// It cannot catch a shader edited on its own - nothing here can - but it turns
/// the half of the mistake that starts in C++ into a build failure naming the
/// two files that have to move with it.
static_assert(kNormalPosX == 0 && kNormalNegX == 1 && kNormalPosY == 2 && kNormalNegY == 3 && kNormalPosZ == 4 &&
                  kNormalNegZ == 5 && kNormalUnaligned == 7,
              "a normal code moved; triangle.frag and gbuffer.frag both switch on these literals");

/// bits 0-2 the normal code, bits 3-10 ambient occlusion, bit 11 fluid top,
/// bits 12-15 how far above its own root a swaying vertex sits, in half blocks.
///
/// **Every one of those four positions is re-spelled as a bare literal in GLSL,
/// and nothing in the build compares the two ends.** The `static_assert`s below
/// prove `packVertexSurface` against itself - substitute the definitions and
/// they reduce to arithmetic on this one function - so they are worth having
/// and they cannot see a shader at all. That is the same gap the `kNormal*`
/// block above closes for the *values*; this closes it for the *positions*,
/// which had nothing. The decoders, as of 2026-08-19:
///
/// - `triangle.vert` fills `fragNormalCode` from `inSurface & 7u`
/// - `triangle.vert` fills `fragOcclusion` from `(inSurface >> 3) & 0xffu`
/// - `displace.glsl` fills `fluidTop` from `(surface & (1u << 11))`
/// - `displace.glsl` fills `swayUnits` from `(surface >> 12) & 15u`
///
/// **Cited by symbol rather than by line, deliberately.** The first draft of
/// this list gave line numbers and two of the four were stale within minutes,
/// because adding the reciprocal note to `displace.glsl` moved both of its
/// decoders down nine lines. The variable names above do not drift.
///
/// Move a field here and the build stays green while grass sways at the wrong
/// height, ambient occlusion decodes as a normal code, and water tops stop
/// rising. None of it raises a validation error.
///
/// **Re-run the search rather than trusting that list** - a list rots, a search
/// does not: `inSurface`, `surface >>` and `surface &` across `engine/shaders`.
/// Today it returns four files, and the distinction matters: `triangle.vert`
/// and `displace.glsl` *decode* these bits, while `hud.vert` and `shadow.vert`
/// only declare the attribute or hand it to `displacedPosition` untouched, so
/// they follow a change for free. Only the first two have to move with it.
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

/// The same word again with every field somewhere other than its boundary.
///
/// **The assert above passes `1.0f` and `false`, and both are the case that
/// hides a mistake.** An occlusion of 1.0 sets all eight of its bits, so it
/// reads back as 255 under more than one wrong arrangement; a `fluidTop` of
/// `false` contributes nothing at all, so bit 11 was exercised by no assert in
/// this file and could have been moved on top of either neighbour without a
/// word of complaint. The live caller is fractional too - `ChunkMesher` passes
/// a per-corner ratio - while every assert here passed an endpoint.
///
/// Each clause fails on its own, for a different single-bit mistake: putting
/// the fluid bit inside the occlusion run or inside the sway run breaks the
/// third and nothing else, and shifting the occlusion one place breaks the
/// second and nothing else. The normal is a named code rather than zero so a
/// cleared field is not mistaken for a correct one.
///
/// 0.5 is exact in binary and `0.5f * 255.0f + 0.5f` is exactly 128.0, so the
/// quantiser's round-to-nearest has no say in the answer.
inline constexpr std::uint32_t kVertexSurfaceProbe = packVertexSurface(kNormalNegY, 0.5f, true, 3);

static_assert((kVertexSurfaceProbe & 7u) == 3u && ((kVertexSurfaceProbe >> 3) & 255u) == 128u &&
                  ((kVertexSurfaceProbe >> 11) & 1u) == 1u && ((kVertexSurfaceProbe >> 12) & 15u) == 3u,
              "every field of the surface word must read back exactly where it was written");

} // namespace engine
