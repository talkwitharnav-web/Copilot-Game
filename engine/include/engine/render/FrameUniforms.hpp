#pragma once

#include "engine/render/ShadowMap.hpp"

#include <glm/glm.hpp>

namespace engine {

/// Everything that is identical for every draw in a frame.
///
/// This used to ride in push constants, which meant 80 bytes of unchanging data
/// were re-pushed for every one of the two hundred-odd draws in a frame, and it
/// pushed the block past the 128 bytes Vulkan guarantees. It lives in a uniform
/// buffer now - one per frame in flight, mapped once and written straight
/// through - so the per-draw block holds only what genuinely differs per draw.
///
/// Every member is a `vec4` on purpose: std140, the layout rule a uniform block
/// follows, pads anything smaller out to sixteen bytes anyway, so a `float`
/// here would occupy four bytes and waste twelve. Adding a member means adding
/// it to the `Frame` block in every shader that reads it, in the same order.
struct FrameUniforms {
    /// Direction *toward* the sun, normalised. `w` is how bright the handheld
    /// light is, which is nothing to do with the sun and everything to do with
    /// there being a spare float here rather than a spare sixteen bytes.
    glm::vec4 sunDirection{0.0f, 1.0f, 0.0f, 0.0f};
    /// x: the ambient light every surface receives, y: what a surface facing the
    /// sun adds on top, z: the floor below which nothing goes dark.
    ///
    /// **w is the sky emission scale, and calling it spare was false.**
    /// `Renderer::drawFrame` writes `m_skyEmission` into it and
    /// `triangle.frag` reads it as `rgb *= frame.lighting.w;` - the whole
    /// of what `kDrawFlagEmissive` does, so it is what lets the sun be brighter
    /// than white and bloom around its edge instead of clipping to a flat disc.
    /// Zeroing it on the strength of the old word "spare" turns the sun, the
    /// moon and every lightning bolt black.
    glm::vec4 lighting{1.0f, 0.0f, 0.0f, 0.0f};
    /// Two independent layer redirections, as (meshed layer, layer to sample
    /// this frame) pairs. Swapping the layer here rather than in the mesh is
    /// what lets water and fire animate without rebuilding a single chunk.
    /// A negative meshed layer matches nothing and so redirects nothing.
    glm::vec4 animation{-1.0f, -1.0f, -1.0f, -1.0f};
    /// rgb: what everything fades to with distance, w: how far away it is fully
    /// faded, or 0 for no fog at all.
    glm::vec4 fog{0.0f, 0.0f, 0.0f, 0.0f};
    /// xyz: the eye in world space. w: where the fog ramp starts, as a fraction
    /// of `fog.w`.
    ///
    /// The eye is here so fog can measure the **radial** distance to a fragment.
    /// Using depth along the view axis instead leaves the corners of the screen
    /// at about 0.7 of the true distance, so they stay clear and chunks are seen
    /// loading in exactly the place peripheral vision picks up movement.
    glm::vec4 eye{0.0f, 0.0f, 0.0f, 0.0f};
    /// Turns a pixel and its depth back into a world position. The deferred
    /// lighting pass has no vertices to interpolate one from, and inverting the
    /// matrix everything was drawn with is what stops the two disagreeing.
    glm::mat4 inverseViewProjection{1.0f};
    /// One per cascade: world space into that cascade's own shadow map.
    /// Unused cascades hold anything at all - `shadowParams.x` says how many are
    /// live, and the lighting pass never looks past it.
    glm::mat4 shadowMatrices[ShadowMap::kMaxCascades]{};
    /// How wide one shadow texel is in world metres, per cascade. The offset
    /// that stops a surface shadowing itself has to be measured in these,
    /// because a distant cascade's texel covers a metre where the nearest one
    /// covers a centimetre.
    glm::vec4 shadowTexelWorld{0.0f};
    /// x: how many cascades are live, 0 for no shadows at all. y: how far
    /// shadows reach, in metres. z: the constant depth bias. w: one texel of the
    /// shadow map in UV.
    glm::vec4 shadowParams{0.0f};
    /// x: how far the deck has drifted west, in blocks. y: the coverage bias -
    /// higher leaves more sky. z: how much a cloud darkens the ground beneath
    /// it. w: quality, 0 off, 1 fast, 2 fancy.
    glm::vec4 cloud{0.0f, 0.5f, 0.0f, 0.0f};
    /// x: a clock in seconds for the ripples, wrapped. y: how steep they are.
    /// z: how much sky a water surface returns.
    ///
    /// w is **how far a cast shadow darkens the ambient sky as well as the
    /// sun**, which has nothing to do with water and is here for the same
    /// reason the handheld light rides on the sun's direction: a spare float
    /// costs nothing and a spare `vec4` costs sixteen bytes.
    glm::vec4 water{0.0f, 1.0f, 1.0f, 0.55f};
    /// x: how strongly foam draws the line where water meets land. y: how bright
    /// the caustic net under water is. z: how far the surface bends what is seen
    /// through it.
    ///
    /// w is **how many pixels a block face's texture is across**, which is the
    /// grid everything water-related snaps to so that it comes out in the same
    /// squares the world is drawn in. Published here rather than asked of the
    /// sampler, because the lighting pass has no block texture bound and two
    /// ways of answering one number is how they end up disagreeing.
    glm::vec4 waterDetail{1.0f, 1.0f, 1.0f, 16.0f};
    /// x: how hard it is precipitating, 0 to 1. y: 1 for snow, 0 for rain.
    /// z: how far the curtain has fallen, in blocks, wrapped. w: the tangent of
    /// the slant the wind gives it.
    glm::vec4 weather{0.0f, 0.0f, 0.0f, 0.0f};
    /// xy: which way the wind blows, normalised. z: how far it bends a blade of
    /// grass, in blocks. w: a clock for the sway, in seconds, wrapped.
    ///
    /// **One vector drives the deck's drift, the rain's slant and the grass**, so
    /// they cannot disagree about which way the weather is going - which is
    /// something neither the reference nor the well-known shader packs do.
    glm::vec4 wind{1.0f, 0.0f, 0.0f, 0.0f};
    /// rgb: what colour the halo around whichever body is up should be. w: how
    /// strong it is.
    ///
    /// **Supplied rather than fixed in the shader, because there is only one
    /// directional light and the moon uses it too.** A hardcoded warm glow put
    /// a sunset around the moon every single night.
    glm::vec4 glow{1.0f, 0.84f, 0.62f, 0.0f};
    /// x: the world height of the water surface above the eye, while the eye is
    /// under it. y: how strongly light shafts come down through it.
    ///
    /// **z is the cloud deck's depth ceiling, and calling it spare was false.**
    /// `Renderer::drawFrame` writes the sky's own depth less a hair into it and
    /// `clouds.frag` reads it as
    /// `gl_FragDepth = min(clip.z / max(clip.w, 1e-4), frame.volumetric.z);`,
    /// which is what holds the deck just in front of the sun and moon. Zeroing
    /// it clamps every cloud fragment to the near plane, and the deck draws in
    /// front of the entire world.
    ///
    /// **`w` is the one genuinely free slot in this vec4, checked 2026-08-19 at
    /// 11:35**: `Renderer::drawFrame` writes it as a literal `0.0f`, and no
    /// shader reads it under any spelling. The spellings matter more than the
    /// search did - a component can be reached as `.w`, as `.a`, as `.q`, as
    /// `[3]`, or by assigning the whole `vec4` to a local and taking a
    /// component off that, and only the first of those five is what anyone
    /// types when they go looking. All five were checked; the sole
    /// `= frame.volumetric` in the tree is `deferred.frag` taking `.x`.
    /// It is therefore the channel to take if a new per-frame float is needed -
    /// and the reason to say so here is that `z` was described as spare for
    /// several milestones after it stopped being spare, which nearly cost the
    /// cloud deck's depth clamp.
    ///
    /// **What would falsify this:** any shader reading the fourth component by
    /// any of those five spellings, or `drawFrame` writing anything but `0.0f`
    /// into it. **Re-check before claiming it** - eleven agents are writing to
    /// this tree, and a reachability result is a measurement with a timestamp
    /// rather than a property of the code.
    ///
    /// **Nothing else tells a shader how far under water it is.** Being
    /// submerged is one bit today (`eye.w`), which is enough to fog and to hang
    /// caustics off, and not enough to know where the light is coming from.
    glm::vec4 volumetric{0.0f, 0.0f, 0.0f, 0.0f};
};

/// The size is spelled out in counts rather than derived from the members,
/// because a derivation cannot disagree with itself. Read it as: five `vec4`s
/// before the matrices, the inverse view-projection, one matrix per cascade,
/// then nine `vec4`s. Adding a member means changing the count it belongs to,
/// and the three groups are in declaration order above.
///
/// The totals were 160 and 128 until 2026-08-19. Both were wrong by one `vec4`
/// in opposite directions, so the sum stayed at 544 and the assert kept
/// passing while describing a struct this is not - which would have sent anyone
/// adding a member to bump whichever term they guessed at.
static_assert(sizeof(FrameUniforms) ==
                  5 * 16 + 64 + 64 * ShadowMap::kMaxCascades + 9 * 16,
              "FrameUniforms must match the std140 Frame block in the shaders");

} // namespace engine
