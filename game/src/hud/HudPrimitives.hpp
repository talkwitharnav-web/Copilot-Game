#pragma once

#include "world/Block.hpp"

#include <engine/render/MeshData.hpp>

#include <glm/glm.hpp>

#include <string_view>

namespace game::hud {

/// Layer value that tells the shader to sample the HUD sprite sheet rather than
/// the block texture array.
constexpr float kHudLayer = -1.0f;

/// Layer value selecting the font atlas.
constexpr float kFontLayer = -2.0f;

/// Width of a character relative to its height, from the font atlas cell shape.
constexpr float kFontAspect = 8.0f / 14.0f;

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
float appendText(engine::MeshData& mesh, std::string_view text, float leftX, float centreY, float charHeight,
                 float depth, const glm::vec4& color);

/// Width `appendText` would consume, for laying out before drawing.
float textWidth(std::string_view text, float charHeight);

} // namespace game::hud
