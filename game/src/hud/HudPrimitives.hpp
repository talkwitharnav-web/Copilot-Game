#pragma once

#include "item/Item.hpp"
#include "world/Block.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string_view>

namespace game::hud {

/// Layer value that tells the shader to sample the HUD sprite sheet rather than
/// the block texture array.
constexpr float kHudLayer = -1.0f;

/// Pixel dimensions of assets/textures/hud.png.
///
/// Sprite rectangles are given in sheet pixels and normalised against this, so
/// it has to follow the image. It lives here rather than in each screen because
/// the sheet grows whenever a panel is added, and a stale copy silently skews
/// every sprite that reads from it.
constexpr glm::vec2 kSheetSize{185.0f, 1687.0f};

/// Layer value selecting the font atlas.
constexpr float kFontLayer = -2.0f;

/// The font atlas is a 16x16 grid of square cells in which **the cell index is
/// the codepoint**, which is the reference's own `ascii.png` layout - so its
/// file is a drop-in replacement for ours and one set of constants serves both.
constexpr glm::vec2 kFontSheetSize{128.0f, 128.0f};
constexpr float kFontCell = 8.0f;
constexpr int kFontColumns = 16;

/// How far the shadow sits behind the glyph, in cell texels, how much of the
/// colour it keeps, and how far back in depth it goes. The first two are the
/// reference's: one texel down and right, at a quarter brightness. The third is
/// ours, and only has to be small enough to stay inside the caller's depth band.
constexpr float kFontShadowOffset = 1.0f;
constexpr float kFontShadowTint = 0.25f;
constexpr float kFontShadowDepth = 0.00002f;

/// Teaches the text routines how wide each glyph actually is, in cell texels.
///
/// **Measured from the atlas that loaded, never written down.** A variable
/// width font's advances are a property of its artwork, and a second copy of
/// them is exactly the bug this project keeps paying for. Called once at
/// startup; until it is, everything is treated as monospace.
void setFontAdvances(const std::array<std::uint8_t, 128>& advances);

/// Advance of one character, in cell texels.
float fontAdvance(char c);

/// Screen-space quad centred on (centreX, centreY).
///
/// `textured` maps the block texture across the quad; otherwise it samples flat
/// white so `color` is the whole appearance.
///
/// Every quad is emitted with both windings. Backface culling is on, and screen
/// geometry skips the projection that establishes which way is front — deriving
/// that has gone wrong before and fails silently, so paying two extra triangles
/// per quad buys certainty.
void appendQuad(engine::MeshData& mesh, float centreX, float centreY, float halfWidth, float halfHeight, float depth,
                const glm::vec4& color, float layer, bool textured);

/// Quad showing one sprite from the HUD sheet, addressed by pixel rectangle.
void appendSprite(engine::MeshData& mesh, float centreX, float centreY, float halfWidth, float halfHeight,
                  float depth, const glm::vec2& pixelMin, const glm::vec2& pixelSize, const glm::vec2& sheetSize,
                  const glm::vec4& tint = glm::vec4{1.0f});

/// Quad with four freely placed corners, for geometry that is not axis aligned.
/// Corners and texture coordinates are given in matching order.
void appendQuadCorners(engine::MeshData& mesh, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], float depth,
                       const glm::vec4& color, float layer);

/// Draws a block as an isometric cube: top face plus two sides, the way an item
/// icon reads as a solid object rather than a picture of one face.
///
/// `halfHeight` is half the icon's total vertical extent; the cube is narrower
/// than it is tall, so it leaves margin at the sides.
void appendBlockIcon(engine::MeshData& mesh, BlockId block, float centreX, float centreY, float halfHeight,
                     float depth);

/// Draws a string from the font atlas, left-aligned and vertically centred on
/// `centreY`. Returns the width consumed.
///
/// `shadow` draws the whole string again a texel down and right at a quarter
/// brightness, behind. It is most of what makes text read as the reference's
/// rather than as a label pasted on the screen, and it costs one extra quad per
/// character.
float appendText(engine::MeshData& mesh, std::string_view text, float leftX, float centreY, float charHeight,
                 float depth, const glm::vec4& color, bool shadow = false);

/// Draws what a slot holds: the item's icon, and a count where there is more
/// than one.
///
/// `slotHalf` is half a slot's extent, and the icon and label are sized from
/// it. The reference uses one 18-unit cell pitch for the entire screen -
/// storage, hotbar and catalogue alike - so one parameter is genuinely all the
/// scale information any caller has.
void appendStack(engine::MeshData& mesh, const ItemStack& stack, const glm::vec2& centre, float slotHalf,
                 float iconDepth, float countDepth);

/// Width `appendText` would consume, for laying out before drawing.
float textWidth(std::string_view text, float charHeight);

/// A floating label beside the cursor: dark panel, bright inner rule, text.
///
/// Sits clear of the cursor so it never covers what it describes, and is clamped
/// to the window, so a slot against an edge still gets a readable label rather
/// than one running off screen.
///
/// `depth` is the nearest layer it occupies; it draws itself and its frame in
/// front of that, so pass the smallest depth in use.
void appendTooltip(engine::MeshData& mesh, std::string_view text, float cursorX, float cursorY, float aspect,
                   float charHeight, float depth);

/// The gamepad's on-screen pointer: a white arrow with its tip exactly at
/// (x, y), falling down and to the right, outlined so it reads against a pale
/// slot as well as a dark panel.
///
/// **Geometry rather than a sprite.** It is three triangles of flat colour, so
/// it costs no row in the sprite sheet and no artwork - and the tip being the
/// anchor is what makes it agree with the hit tests, which take a point.
///
/// `height` is the arrow's full length. `depth` is the nearest layer it
/// occupies; the outline goes just behind that.
void appendPointer(engine::MeshData& mesh, float x, float y, float height, float depth);

} // namespace game::hud
