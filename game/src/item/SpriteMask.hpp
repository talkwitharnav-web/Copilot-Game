#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace game {

/// **The fragment shader's alpha cutoff, restated here because a GLSL constant
/// cannot be included and this is the C++ side of one number.**
///
/// `engine/shaders/gbuffer.frag` discards a texel whose alpha is below this,
/// and `engine/shaders/shadow.frag` repeats it with a comment saying the two
/// must not drift. This header is the **third** holder of that number and the
/// only one nothing pointed at.
///
/// **It fails silently, in one place, and only on dropped and thrown items** -
/// which is why the number is derived below rather than merely commented. Set
/// this above the shader's and the mask calls a texel empty that the shader
/// draws, so a solid patch of the sprite gets a wall built through the middle
/// of it; set it below and the mask calls a hole solid, so the wall meant to
/// close that hole is never raised and the item is see-through edge on. Neither
/// is a crash, a warning or a validation error, and neither shows on a block.
///
/// **Verified equal 2026-08-19: `gbuffer.frag:101` and `shadow.frag:30` both
/// read `< 0.5`.** What would make that false is an edit to either shader.
/// Re-check by searching the shader directory for `discard` rather than for a
/// remembered line number - both of those have moved before.
constexpr float kSpriteAlphaCutoff = 0.5f;

/// The lowest byte the shader keeps, for a given cutoff: a byte `v` survives
/// when `v / 255.0 >= cutoff`, so this is the **ceiling** of the product.
///
/// **Takes the cutoff rather than reading it**, so the graded pin below can
/// hand it answers that are known independently of `kSpriteAlphaCutoff` - a
/// converter wired to the one value it will ever be asked about cannot be shown
/// to work. Written as a `constexpr` function rather than an immediately-called
/// `constexpr` lambda for the reason `kUnwaxed` in `Recipe.cpp` is at namespace
/// scope: MSVC will accept a construct like that and then quietly decline to
/// constant-evaluate it.
constexpr std::uint8_t solidAlphaFor(float cutoff) {
    const float exact = cutoff * 255.0f;
    const int floored = static_cast<int>(exact);
    return static_cast<std::uint8_t>(static_cast<float>(floored) < exact ? floored + 1 : floored);
}

/// A graded pin: four cutoffs whose answers are known without running this.
/// **0.55 is the one that earns its place** - 0.55 x 255 is 140.25, so a
/// rounding implementation gives 140 and only a ceiling gives 141, and 140
/// would be a byte the shader discards being counted as solid.
static_assert(solidAlphaFor(0.0f) == 0 && solidAlphaFor(1.0f) == 255 &&
                  solidAlphaFor(0.5f) == 128 && solidAlphaFor(0.55f) == 141,
              "the alpha converter rounds instead of taking a ceiling, so the mask keeps a texel "
              "one byte below the shader's cutoff");

/// The byte this game's shader keeps, **worked out from the cutoff rather than
/// typed as the 128 it comes to today.**
constexpr std::uint8_t kSpriteSolidAlpha = solidAlphaFor(kSpriteAlphaCutoff);

/// Asserts the whole expression `solid` below evaluates, both sides of the
/// boundary - that this byte survives the shader and that the one beneath it
/// does not. Comparing `kSpriteSolidAlpha == 128` would be one side of the
/// derivation against itself and would prove nothing.
static_assert(static_cast<float>(kSpriteSolidAlpha) / 255.0f >= kSpriteAlphaCutoff &&
                  static_cast<float>(kSpriteSolidAlpha - 1) / 255.0f < kSpriteAlphaCutoff,
              "the solid-texel byte and the shader's cutoff disagree - the mask would call a texel "
              "empty that the shader draws, or solid that it discards, and the wall between them "
              "would be built in the wrong place");

/// Which texels of each sprite are solid.
///
/// A dropped tool is not a picture standing on its edge, it is a **shape**: the
/// reference builds one by extruding the sprite a texel deep and walling in
/// every boundary between a solid texel and an empty one. That needs the
/// silhouette on the CPU, which is why the texture array keeps its alpha.
class SpriteMask {
public:
    SpriteMask() = default;
    SpriteMask(int width, int height, std::vector<std::uint8_t> alpha)
        : m_width(width), m_height(height), m_alpha(std::move(alpha)) {}

    int width() const { return m_width; }
    int height() const { return m_height; }

    /// Out of range is empty, so an edge test can walk off the sprite without
    /// checking first - the boundary of the image is a boundary of the shape.
    /// The threshold is the fragment shader's own, `kSpriteSolidAlpha` above.
    /// Anything it would discard has to count as absent here too, or a side
    /// wall is left standing around a hole nobody can see.
    bool solid(int layer, int x, int y) const {
        if (layer < 0 || x < 0 || y < 0 || x >= m_width || y >= m_height) {
            return false;
        }
        const auto index =
            (static_cast<std::size_t>(layer) * m_height + static_cast<std::size_t>(y)) * m_width +
            static_cast<std::size_t>(x);
        return index < m_alpha.size() && m_alpha[index] >= kSpriteSolidAlpha;
    }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_alpha;
};

} // namespace game
