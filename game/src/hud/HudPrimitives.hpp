#pragma once

#include "hud/Hotbar.hpp"
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

/// The reference's own hotbar width, in the reference's own pixels. The XP bar
/// and the boss bar are the same 182 wide, which is why anchoring to it costs no
/// second constant when either of those lands.
constexpr float kReferenceHotbarWidth = 182.0f;

/// **One art pixel in screen units, and the only one.** `UI.md` R1.
///
/// There were two, 4.19% apart and both live: the container screens scaled off
/// their own panel height and the hotbar off this. Every panel, slot, tooltip
/// and catalogue cell was therefore drawn 4.19% larger than the bar it replaces
/// - invisible only because the hotbar is hidden while a screen is open, so the
/// eye has to compare across a keypress.
///
/// **The hotbar anchor wins, so no other file may define an art pixel.** R1
/// eventually widens this to `pixel(aspect, guiScale)` - a `min` against the
/// reference's 320-pixel virtual width, times a settings multiplier - and
/// neither term exists yet: nothing clamps the layout to a narrow window and
/// there is no `gui_scale` key. Both are additive, and both need the layout to
/// stop being `constexpr`, which is a change to every screen rather than to
/// this line.
constexpr float kArtPixel = kHotbarWidth / kReferenceHotbarWidth;

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

/// **How far in front of the depth it is handed an icon can reach**, and how far
/// its decorations reach in each direction. A screen that gives an icon and its
/// count adjacent depths has to clear both, and until these were published no
/// screen could state that as anything but a hoped-for gap.
///
/// `appendBlockIcon` normalises a model's boxes onto `depth - kIconDepthSpan` at
/// the nearest and `depth` at the farthest, so the span is fixed however far a
/// box sticks out. It is only reachable by a block drawn as boxes - **which is
/// five more blocks than it was before candles, bamboo, sea pickles, lecterns
/// and dragon eggs stopped being flat sprites**, and a flat sprite spends none
/// of it. That change is exactly why these asserts exist now.
///
/// `appendStackDecorations` puts the durability fill one step in front of the
/// depth it is handed and the count text two, and `appendText` puts a glyph's
/// shadow `kFontShadowDepth` behind it. So a decoration set occupies
/// `[depth - kDecorationSpan, depth + kFontShadowDepth]`.
constexpr float kIconDepthSpan = 0.00002f;
constexpr float kDecorationSpan = 0.00001f;

/// Whether an icon at `iconDepth` stays behind the marks drawn over it at
/// `markDepth`. **Smaller is nearer** in this pass, which has no depth buffer at
/// all - the renderer sorts on this key, far first - so the icon's *nearest*
/// reach must still be behind the marks' *farthest*.
///
/// Written as one function so the five bands in `Hotbar.cpp` and
/// `InventoryScreen.cpp` cannot each invent their own idea of the margin, which
/// is how one of them ends up with none.
constexpr bool iconStaysBehindItsMarks(float iconDepth, float markDepth) {
    return iconDepth - kIconDepthSpan > markDepth + kFontShadowDepth;
}

/// Teaches the text routines how wide each glyph actually is, in cell texels.
///
/// **Measured from the atlas that loaded, never written down.** A variable
/// width font's advances are a property of its artwork, and a second copy of
/// them is exactly the bug this project keeps paying for. Called once at
/// startup; until it is, everything is treated as monospace.
void setFontAdvances(const std::array<std::uint8_t, 128>& advances);

/// Advance of one character, in cell texels.
float fontAdvance(char c);

/// **A quad is twelve indices, and this is the only copy of them.** `UI.md` R3.
///
/// Both windings are emitted and back-face culling then rejects exactly one
/// triangle of each pair. That is a *correctness* requirement rather than a
/// tolerance: screen geometry skips the projection that decides which way is
/// front, so the surviving winding is whichever way the caller's half-extents
/// happened to be signed. A six-index quad therefore does not draw half as
/// much - it vanishes outright for half of the sizes it can be handed, and
/// only sometimes, which is the hardest kind of bug to be shown.
///
/// Four call sites wrote this literal out by hand until the corner table taught
/// this file what that costs: the dropped item was a hand transcription of the
/// mesher's rows with two vertices exchanged, and it mirrored every block in
/// the game for twenty milestones. One table, proved below, so nobody has to
/// compare four literals character by character again.
constexpr std::uint32_t kQuadIndices[12]{0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};

/// Whether a quad table really carries each triangle in both windings.
///
/// **Derived rather than restated**: it reads the table's own front faces and
/// requires the back faces to be those with two corners exchanged, so editing
/// either half alone fails it. Comparing the table against a second copy of
/// itself would prove nothing - `CLAUDE.md` bug shape #11. The second clause is
/// the one a winding test cannot see: exchanging two corners is invisible to
/// "is it reversed", so the front pair is also required to name all four
/// corners, which is what stops two copies of one triangle passing as a quad.
constexpr bool quadCarriesBothWindings(const std::uint32_t (&q)[12]) {
    for (int triangle = 0; triangle < 2; ++triangle) {
        const int front = triangle * 3;
        const int back = 6 + front;
        if (q[back + 0] != q[front + 0] || q[back + 1] != q[front + 2] || q[back + 2] != q[front + 1]) {
            return false;
        }
    }
    bool named[4]{};
    for (int i = 0; i < 6; ++i) {
        if (q[i] > 3u) {
            return false;
        }
        named[q[i]] = true;
    }
    return named[0] && named[1] && named[2] && named[3];
}

static_assert(quadCarriesBothWindings(kQuadIndices),
              "every UI quad must carry both windings, or culling deletes it at half the sizes it can "
              "be asked to draw");

/// Negative control. A pass above is only worth reading if a fault still fails,
/// so here are the two shapes the rule exists to reject: the six-index quad
/// `UI.md` D6 warns about, padded back to twelve, and a pair that reverses
/// correctly but never names its fourth corner.
constexpr std::uint32_t kRepeatedWindingFault[12]{0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3};
constexpr std::uint32_t kMissingCornerFault[12]{0, 1, 2, 0, 2, 1, 0, 2, 1, 0, 1, 2};
static_assert(!quadCarriesBothWindings(kRepeatedWindingFault),
              "a quad whose second pair repeats the first is exactly what R3 forbids");
static_assert(!quadCarriesBothWindings(kMissingCornerFault),
              "a reversed pair that never names its fourth corner is not a quad");

/// Screen-space quad centred on (centreX, centreY).
///
/// `textured` maps the block texture across the quad; otherwise it samples flat
/// white so `color` is the whole appearance.
///
/// Every quad is emitted with both windings. Backface culling is on, and screen
/// geometry skips the projection that establishes which way is front - deriving
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
/// **DO NOT REORDER THE LAST THREE PARAMETERS, and do not let a caller pass
/// literals for them.** Written 2026-08-19.
///
/// `slotHalf`, `iconDepth` and `countDepth` are three adjacent `float`s, so any
/// two of them can be swapped at a call site with no diagnostic from anything
/// this project runs - not the compiler at `/W4`, not validation, not a soak.
/// The two depths are not interchangeable: the count is meant to sit in FRONT
/// of the icon, by the span named above, and swapping them puts the number
/// behind the picture it is counting. That reads as a missing count rather than
/// as a wrong depth, so it does not look like this function's fault.
///
/// **`InventoryScreen.cpp` calls this seven times and is not mine.** Checked
/// 2026-08-19: all seven pass a NAMED pair - `kIconDepth, kCountDepth`,
/// `kCatalogueIconDepth, kCatalogueCountDepth`, `kHeldIconDepth,
/// kHeldCountDepth` - so a swap would be visible as a swap while reading. That
/// is the only thing protecting them, and it is a habit rather than a rule.
///
/// **So the constraint is on THIS declaration**: appending a parameter is safe,
/// reordering these three is not, and a reorder here mis-draws six screens in a
/// file whose owner has no reason to reopen it. If the order must change,
/// rename the parameters in the same edit so every call site fails to compile
/// rather than fails to look right.
///
/// `appendStackDecorations` below takes a single `depth` and derives both spans
/// itself, which is why it carries no such warning - the same hazard, closed by
/// having one argument instead of two.
///
/// **DO NOT "improve" this function into that shape. Measured 2026-08-19 and
/// the data refuses it.** The three call-site pairs do not share a span:
/// catalogue separates icon from count by 0.0001, while the grid and the held
/// stack both use 0.0004 - three pairs, two spans. Deriving `countDepth` from
/// `iconDepth` would therefore have to pick one, and picking either silently
/// re-spaces the other. Both spans clear `kDecorationSpan` comfortably, so
/// **nothing is wrong today**; they are simply not the same number, and the
/// obvious tidy-up is a behaviour change wearing the clothes of a refactor.
///
/// **Falsified by**: the six constants in `InventoryScreen.cpp` - search
/// `*IconDepth` and `*CountDepth` rather than trusting this paragraph - coming
/// to share one delta. Then, and only then, collapse the two parameters into
/// one and delete this whole warning, because the hazard goes with it.
///
/// **Falsified by**: these three parameters no longer being the same type, or a
/// call site passing a float literal where a named constant is expected.
void appendStack(engine::MeshData& mesh, const ItemStack& stack, const glm::vec2& centre, float slotHalf,
                 float iconDepth, float countDepth);

/// How much of a slot the item square inside it fills.
///
/// **One owner, because three things are placed against it**: the icon, the
/// count in a corner of it and the durability bar at (2,13) of it. The hotbar
/// used to size its own icon at 0.56 of its cell and its count at a flat 0.042
/// screen units, so the same stack was drawn 19% smaller and its count 41%
/// larger one keypress apart. `UI.md` §6.5 names this as the thing to fix
/// before the bar exists, because a bar measured against two different boxes
/// lands in two different places on the same screen.
constexpr float kItemBoxScale = 0.72f;
constexpr float itemBoxHalf(float slotHalf) { return slotHalf * kItemBoxScale; }

/// The decorations that sit on top of an item's icon: the stack count, and the
/// durability bar of anything that wears.
///
/// `iconHalf` is half the item square the icon was drawn in - **not** the slot,
/// because the hotbar's own cell art has a thicker bevel than the reference's
/// and its icon is smaller than a container slot's. Everything here is a
/// fraction of that square, so both draw paths put the same marks in the same
/// place relative to the picture they annotate.
///
/// **Only the two HUD paths draw these.** The ground drop and the thrown item
/// get no count and no bar: the reference draws neither, and a 13x2 strip on a
/// billboarded sprite out in the world is unreadable at any distance you would
/// see one from. Said here so the next reader of "a block is drawn in four
/// places" knows this was decided rather than missed.
void appendStackDecorations(engine::MeshData& mesh, const ItemStack& stack, const glm::vec2& centre,
                            float iconHalf, float depth);

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
