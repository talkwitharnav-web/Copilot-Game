#include "hud/HudPrimitives.hpp"

#include "world/Block.hpp"
#include "world/FaceShading.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace game::hud {
namespace {

/// The reference's own space width, and the fallback every other glyph gets
/// before `setFontAdvances` has run. A blank cell has no rightmost column to
/// measure, so a space can only ever come from a number.
constexpr std::uint8_t kSpaceAdvance = 4;
constexpr std::uint8_t kDefaultAdvance = 6;

std::array<std::uint8_t, 128>& advanceTable() {
    // Function-local so it is initialised before first use and cannot be read
    // half-built. Written once at startup and only read afterwards.
    static std::array<std::uint8_t, 128> table = [] {
        std::array<std::uint8_t, 128> initial{};
        initial.fill(kDefaultAdvance);
        initial[' '] = kSpaceAdvance;
        return initial;
    }();
    return table;
}

} // namespace

void setFontAdvances(const std::array<std::uint8_t, 128>& advances) { advanceTable() = advances; }

float fontAdvance(char c) {
    const auto code = static_cast<unsigned char>(c);
    return code < advanceTable().size() ? static_cast<float>(advanceTable()[code])
                                        : static_cast<float>(kDefaultAdvance);
}

void appendQuad(engine::MeshData& mesh, float centreX, float centreY, float halfWidth, float halfHeight, float depth,
                const glm::vec4& color, float layer, bool textured) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    const glm::vec2 offsets[4]{
        {-halfWidth, -halfHeight}, {halfWidth, -halfHeight}, {halfWidth, halfHeight}, {-halfWidth, halfHeight}};
    const glm::vec2 uvs[4]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (int corner = 0; corner < 4; ++corner) {
        const glm::vec2 uv = textured ? uvs[corner] : glm::vec2{0.5f, 0.5f};
        mesh.vertices.push_back(engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y, depth},
                                               engine::packVertexColor(color.r, color.g, color.b, color.a),
                                               {uv.x, uv.y},
                                               layer,
                                               engine::kVertexSurfaceDefault});
    }

    mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                             base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
}

void appendSprite(engine::MeshData& mesh, float centreX, float centreY, float halfWidth, float halfHeight,
                  float depth, const glm::vec2& pixelMin, const glm::vec2& pixelSize, const glm::vec2& sheetSize,
                  const glm::vec4& tint) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    const glm::vec2 uvMin = pixelMin / sheetSize;
    const glm::vec2 uvMax = (pixelMin + pixelSize) / sheetSize;

    const glm::vec2 offsets[4]{
        {-halfWidth, -halfHeight}, {halfWidth, -halfHeight}, {halfWidth, halfHeight}, {-halfWidth, halfHeight}};
    const glm::vec2 uvs[4]{{uvMin.x, uvMin.y}, {uvMax.x, uvMin.y}, {uvMax.x, uvMax.y}, {uvMin.x, uvMax.y}};

    for (int corner = 0; corner < 4; ++corner) {
        mesh.vertices.push_back(engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y, depth},
                                               engine::packVertexColor(tint.r, tint.g, tint.b, tint.a),
                                               {uvs[corner].x, uvs[corner].y},
                                               kHudLayer,
                                               engine::kVertexSurfaceDefault});
    }

    mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                             base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
}

void appendQuadCorners(engine::MeshData& mesh, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], float depth,
                       const glm::vec4& color, float layer) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    for (int corner = 0; corner < 4; ++corner) {
        mesh.vertices.push_back(engine::Vertex{{corners[corner].x, corners[corner].y, depth},
                                               engine::packVertexColor(color.r, color.g, color.b, color.a),
                                               {uvs[corner].x, uvs[corner].y},
                                               layer,
                                               engine::kVertexSurfaceDefault});
    }

    mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                             base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
}

void appendBlockIcon(engine::MeshData& mesh, BlockId block, float centreX, float centreY, float halfHeight,
                     float depth) {
    // Matching order for every face, so one texture-coordinate set serves all.
    const glm::vec2 uvs[4]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    const BlockShape shape = blockShape(block);

    // A plant is drawn flat, as the artwork actually is. Wrapping it around a
    // cube shows it as a box of grass, which is not what gets placed. A ladder,
    // a vine and a pane go the same way, for the same reason.
    if (usesFlatIcon(shape)) {
        const float half = halfHeight * 0.95f;
        const glm::vec2 corners[4]{{centreX - half, centreY - half},
                                   {centreX + half, centreY - half},
                                   {centreX + half, centreY + half},
                                   {centreX - half, centreY + half}};
        appendQuadCorners(mesh, corners, uvs, depth, glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                          blockTextureLayer(block, BlockFace::Side));
        return;
    }

    // Isometric projection of a unit cube: x runs right-and-down, z runs
    // left-and-down, y runs straight up. Only the three faces pointing at the
    // viewer are drawn, so the icon costs three quads rather than six.
    constexpr float kIsoX = 0.866f; // cos(30 degrees)
    constexpr float kIsoY = 0.5f;   // sin(30 degrees)

    // **Each box of a multi-box icon is offset by how near it is to the eye.**
    // The depth test keeps the *first* fragment at a given distance, so drawing
    // two boxes at one depth left the slab's lit top face standing in front of
    // the step that sits on it - which is what made a stair's upper prism read
    // with the wrong shading. The key is the box centre summed along the three
    // axes, because the view direction here is (1,1,1) and nothing else about
    // the projection matters.
    constexpr float kBoxStep = 0.00002f;

    const auto project = [&](float x, float y, float z) {
        return glm::vec2{centreX + (x - z) * kIsoX * halfHeight,
                         centreY + ((x + z) * kIsoY - y) * halfHeight};
    };

    // The icon's two visible side faces point different ways, and a block whose
    // sides differ has to be told which is which - handing both the same
    // direction is what put a furnace's mouth on the right-hand face as well as
    // the front. A faceless block answers `Unknown` to both and is untouched.
    const FaceDirection frontFacing = blockFacing(block);
    const FaceDirection rightFacing = quarterTurn(frontFacing);

    // One box of the icon: the three faces an isometric view can see. `model`
    // is null for anything cut out of a cube, which samples from where the box
    // sits; a model names its own rectangle of the sheet per face.
    const auto box = [&](const BlockBox& b, const ModelBox* model) {
        const float boxDepth =
            depth - kBoxStep * ((b.minX + b.maxX + b.minY + b.maxY + b.minZ + b.maxZ) * 0.5f);

        // A model box names its own rectangle of the sheet per face, and may
        // name its own layer with it. Where it does not, the face falls back to
        // **the block's own layer for that face** - the lid to its top and a
        // wall to its side.
        //
        // The lid used to fall back to the *side* layer instead, which left an
        // end portal frame wearing its sandstone flank where its eye socket
        // should be and reading as a plain sandy cube. The dropped-item version
        // of this same drawing had it right all along; this is the second of
        // the three places a block is drawn, and they now agree.
        const auto lidLayer = [&] {
            return model != nullptr && model->lidLayer >= 0.0f ? model->lidLayer
                                                               : blockTextureLayer(block, BlockFace::Top);
        };
        const auto wallLayer = [&](FaceDirection direction) {
            return model != nullptr && model->sideLayer >= 0.0f
                       ? model->sideLayer
                       : blockTextureLayer(block, BlockFace::Side, direction);
        };

        glm::vec2 lid[4]{uvs[0], uvs[1], uvs[2], uvs[3]};
        glm::vec2 wall[4]{uvs[0], uvs[1], uvs[2], uvs[3]};
        if (model != nullptr) {
            lid[0] = {model->topUMin, model->topVMin};
            lid[1] = {model->topUMax, model->topVMin};
            lid[2] = {model->topUMax, model->topVMax};
            lid[3] = {model->topUMin, model->topVMax};
            wall[0] = {model->uMin, model->vMin};
            wall[1] = {model->uMax, model->vMin};
            wall[2] = {model->uMax, model->vMax};
            wall[3] = {model->uMin, model->vMax};
        }

        const glm::vec2 topFace[4]{project(b.minX, b.maxY, b.minZ), project(b.maxX, b.maxY, b.minZ),
                                   project(b.maxX, b.maxY, b.maxZ), project(b.minX, b.maxY, b.maxZ)};
        // **The mesher's own shades, not literals.** The right face was written
        // as 0.68 against the table's 0.72, so an icon was lit differently from
        // the block it stands for.
        const float topShade = kFaceShades[static_cast<std::size_t>(AxisFace::PosY)];
        const float frontShade = kFaceShades[static_cast<std::size_t>(AxisFace::PosZ)];
        const float rightShade = kFaceShades[static_cast<std::size_t>(AxisFace::PosX)];
        appendQuadCorners(mesh, topFace, lid, boxDepth, glm::vec4{topShade, topShade, topShade, 1.0f},
                          lidLayer());

        const glm::vec2 front[4]{project(b.minX, b.maxY, b.maxZ), project(b.maxX, b.maxY, b.maxZ),
                                 project(b.maxX, b.minY, b.maxZ), project(b.minX, b.minY, b.maxZ)};
        appendQuadCorners(mesh, front, wall, boxDepth,
                          glm::vec4{frontShade, frontShade, frontShade, 1.0f}, wallLayer(frontFacing));

        const glm::vec2 right[4]{project(b.maxX, b.maxY, b.minZ), project(b.maxX, b.maxY, b.maxZ),
                                 project(b.maxX, b.minY, b.maxZ), project(b.maxX, b.minY, b.minZ)};
        appendQuadCorners(mesh, right, wall, boxDepth,
                          glm::vec4{rightShade, rightShade, rightShade, 1.0f}, wallLayer(rightFacing));
    };

    // A lantern and an end rod are *models*, and drawing either as a flat crop
    // of its sheet is what made the rod's slot picture a two-texel sliver. A
    // bed joins them so its icon and its dropped form are a little bed rather
    // than a cube of mattress.
    if (usesModelIcon(shape)) {
        const ModelBoxes model = postModel(block);
        for (int i = 0; i < model.count; ++i) {
            box(model.boxes[i].box, &model.boxes[i]);
        }
        return;
    }

    // Everything else that is a set of boxes draws as those boxes, so a fence in
    // the slot reads as a fence rather than as a cube of planks. **`iconBoxes`
    // owns that choice** - a dropped block reads the same function, so what is
    // on the floor and what is in the hotbar cannot disagree.
    const BlockBoxes parts = iconBoxes(block);
    for (int i = 0; i < parts.count; ++i) {
        box(parts.boxes[i], nullptr);
    }
}

float textWidth(std::string_view text, float charHeight) {
    // The scale from a cell texel to the screen. A glyph is drawn at full cell
    // size and only its *advance* varies, which is the reference's arrangement
    // and why a proportional font needs no per-glyph geometry.
    const float texel = charHeight / kFontCell;
    float width = 0.0f;
    for (const char c : text) {
        width += fontAdvance(c) * texel;
    }
    return width;
}

float appendText(engine::MeshData& mesh, std::string_view text, float leftX, float centreY, float charHeight,
                 float depth, const glm::vec4& color, bool shadow) {
    const float texel = charHeight / kFontCell;
    const float half = charHeight * 0.5f;

    // Behind the glyphs and drawn first, so a shadow can never land on top of
    // the letter in front of it.
    const int passes = shadow ? 2 : 1;
    for (int pass = 0; pass < passes; ++pass) {
        const bool drawingShadow = shadow && pass == 0;
        const glm::vec4 tint = drawingShadow ? glm::vec4{color.r * kFontShadowTint, color.g * kFontShadowTint,
                                                         color.b * kFontShadowTint, color.a}
                                             : color;
        const float shift = drawingShadow ? kFontShadowOffset * texel : 0.0f;
        const float passDepth = drawingShadow ? depth + kFontShadowDepth : depth;

        float pen = leftX;
        for (const char c : text) {
            const int code = static_cast<unsigned char>(c);
            const float advance = fontAdvance(c);
            // **Cropped to the advance, not drawn at full cell width.** The font
            // layer has no alpha discard, so a glyph's *transparent* texels
            // still write depth - and a full-width quad overlaps the next
            // letter by the two columns of spacing, which the depth test then
            // rejects. That ate the left of every character after the first.
            // Cropping puts the quads edge to edge again, which is what the old
            // monospace atlas got for free.
            const float columns = std::min(advance, kFontCell);
            if (code > ' ' && code < 127) {
                const glm::vec2 pixelMin{static_cast<float>(code % kFontColumns) * kFontCell,
                                         static_cast<float>(code / kFontColumns) * kFontCell};
                const glm::vec2 uvMin = pixelMin / kFontSheetSize;
                const glm::vec2 uvMax = (pixelMin + glm::vec2{columns, kFontCell}) / kFontSheetSize;

                const float halfW = columns * texel * 0.5f;
                const float centreX = pen + halfW + shift;
                const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
                const glm::vec2 offsets[4]{{-halfW, -half}, {halfW, -half}, {halfW, half}, {-halfW, half}};
                const glm::vec2 uvs[4]{
                    {uvMin.x, uvMin.y}, {uvMax.x, uvMin.y}, {uvMax.x, uvMax.y}, {uvMin.x, uvMax.y}};

                for (int corner = 0; corner < 4; ++corner) {
                    mesh.vertices.push_back(
                        engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y + shift, passDepth},
                                       engine::packVertexColor(tint.r, tint.g, tint.b, tint.a),
                                       {uvs[corner].x, uvs[corner].y},
                                       kFontLayer,
                                       engine::kVertexSurfaceDefault});
                }

                mesh.indices.insert(mesh.indices.end(),
                                    {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3, base + 0,
                                     base + 2, base + 1, base + 0, base + 3, base + 2});
            }
            pen += advance * texel;
        }
    }

    return textWidth(text, charHeight);
}

void appendStack(engine::MeshData& mesh, const ItemStack& stack, const glm::vec2& centre, float slotHalf,
                 float iconDepth, float countDepth) {
    if (stack.empty()) {
        return;
    }
    const float iconHalf = slotHalf * 0.72f;
    if (isBlockItem(stack.item)) {
        appendBlockIcon(mesh, blockForItem(stack.item), centre.x, centre.y, iconHalf, iconDepth);
    } else if (const int layer = itemTextureLayer(stack.item); layer >= 0) {
        // Flat, because there is no block to build a little cube out of.
        appendQuad(mesh, centre.x, centre.y, iconHalf, iconHalf, iconDepth, {1.0f, 1.0f, 1.0f, 1.0f},
                   static_cast<float>(layer), true);
    }

    if (stack.count > 1) {
        const float countHeight = slotHalf * 0.84f;
        const std::string label = std::to_string(stack.count);
        appendText(mesh, label, centre.x - slotHalf + 0.006f, centre.y + slotHalf - countHeight * 0.5f - 0.006f,
                   countHeight, countDepth, {1.0f, 1.0f, 1.0f, 1.0f});
    }
}

void appendTooltip(engine::MeshData& mesh, std::string_view text, float cursorX, float cursorY, float aspect,
                   float charHeight, float depth) {
    if (text.empty()) {
        return;
    }

    // Proportional to the text, so the box keeps its shape whatever size the
    // label is drawn at.
    const float padding = charHeight * 0.45f;
    const float rule = charHeight * 0.11f;
    const float gap = charHeight * 0.35f;

    // Layers are a twentieth of the gap between the screen's own depth bands,
    // which is the same margin the hotbar's selected cell already relies on.
    constexpr float kLayer = 0.00005f;

    const glm::vec4 panel{0.05f, 0.03f, 0.09f, 0.94f};
    const glm::vec4 edge{0.32f, 0.16f, 0.62f, 1.0f};
    const glm::vec4 label{0.94f, 0.94f, 0.98f, 1.0f};

    const float innerHalfWidth = textWidth(text, charHeight) * 0.5f + padding;
    const float innerHalfHeight = charHeight * 0.5f + padding;
    const float outerHalfWidth = innerHalfWidth + rule * 2.0f;
    const float outerHalfHeight = innerHalfHeight + rule * 2.0f;

    // Offset down and to the right of the pointer, the way a pointer's own
    // label sits, then pulled back inside the window if that would overflow.
    float centreX = cursorX + gap + outerHalfWidth;
    float centreY = cursorY + gap + outerHalfHeight;

    centreX = std::min(centreX, aspect - outerHalfWidth);
    centreX = std::max(centreX, -aspect + outerHalfWidth);
    // Flips above the cursor rather than merely clamping, or a label near the
    // bottom edge would sit on top of the slot it describes.
    if (centreY + outerHalfHeight > 1.0f) {
        centreY = cursorY - gap - outerHalfHeight;
    }
    centreY = std::max(centreY, -1.0f + outerHalfHeight);

    const float white = static_cast<float>(TextureLayer::White);
    appendQuad(mesh, centreX, centreY, outerHalfWidth, outerHalfHeight, depth, panel, white, false);
    appendQuad(mesh, centreX, centreY, outerHalfWidth - rule, outerHalfHeight - rule, depth - kLayer, edge, white,
               false);
    appendQuad(mesh, centreX, centreY, innerHalfWidth, innerHalfHeight, depth - kLayer * 2.0f, panel, white, false);

    appendText(mesh, text, centreX - innerHalfWidth + padding, centreY, charHeight, depth - kLayer * 3.0f, label);
}

void appendPointer(engine::MeshData& mesh, float x, float y, float height, float depth) {
    // The arrow every desktop has drawn since 1984, as a fraction of its own
    // height with the tip at the origin. Nothing here needs explaining to a
    // player, which is the whole reason for using this shape.
    const glm::vec2 shape[3]{
        glm::vec2{0.0f, 0.0f},
        glm::vec2{0.0f, 1.0f},
        glm::vec2{0.70f, 0.70f},
    };
    const glm::vec2 centroid = (shape[0] + shape[1] + shape[2]) / 3.0f;

    // Grown about the centroid rather than stroked, which keeps the tip in the
    // same place as the point being tested.
    constexpr float kOutlineGrowth = 0.22f;
    constexpr float kOutlineDepth = 0.00004f;

    const glm::vec2 uvs[4]{{0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}};
    const float white = static_cast<float>(TextureLayer::White);

    // Outline first and behind it, so the body draws over its own edge.
    for (int pass = 0; pass < 2; ++pass) {
        const bool outline = pass == 0;
        const float scale = outline ? 1.0f + kOutlineGrowth : 1.0f;

        glm::vec2 corners[4];
        for (int point = 0; point < 3; ++point) {
            const glm::vec2 placed = centroid + (shape[point] - centroid) * scale;
            corners[point] = glm::vec2{x + placed.x * height, y + placed.y * height};
        }
        // A triangle, drawn as a quad whose last two corners coincide.
        corners[3] = corners[2];

        appendQuadCorners(mesh, corners, uvs, outline ? depth + kOutlineDepth : depth,
                          outline ? glm::vec4{0.05f, 0.05f, 0.07f, 0.9f} : glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                          white);
    }
}

} // namespace game::hud
