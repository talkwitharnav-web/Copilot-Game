#include "hud/HudPrimitives.hpp"

#include "world/Block.hpp"

#include <cstdint>

namespace game::hud {
namespace {

/// Font atlas layout: printable ASCII from 32, in a fixed grid.
constexpr float kFontCellWidth = 8.0f;
constexpr float kFontCellHeight = 14.0f;
constexpr int kFontColumns = 16;
constexpr int kFontFirstChar = 32;
constexpr glm::vec2 kFontSheetSize{128.0f, 84.0f};

} // namespace

void appendQuad(engine::MeshData& mesh, float centreX, float centreY, float halfWidth, float halfHeight, float depth,
                const glm::vec4& color, float layer, bool textured) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    const glm::vec2 offsets[4]{
        {-halfWidth, -halfHeight}, {halfWidth, -halfHeight}, {halfWidth, halfHeight}, {-halfWidth, halfHeight}};
    const glm::vec2 uvs[4]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    for (int corner = 0; corner < 4; ++corner) {
        const glm::vec2 uv = textured ? uvs[corner] : glm::vec2{0.5f, 0.5f};
        mesh.vertices.push_back(engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y, depth},
                                               {color.r, color.g, color.b, color.a},
                                               {uv.x, uv.y},
                                               layer});
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
                                               {tint.r, tint.g, tint.b, tint.a},
                                               {uvs[corner].x, uvs[corner].y},
                                               kHudLayer});
    }

    mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                             base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
}

void appendQuadCorners(engine::MeshData& mesh, const glm::vec2 (&corners)[4], const glm::vec2 (&uvs)[4], float depth,
                       const glm::vec4& color, float layer) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());

    for (int corner = 0; corner < 4; ++corner) {
        mesh.vertices.push_back(engine::Vertex{{corners[corner].x, corners[corner].y, depth},
                                               {color.r, color.g, color.b, color.a},
                                               {uvs[corner].x, uvs[corner].y},
                                               layer});
    }

    mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                             base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
}

void appendBlockIcon(engine::MeshData& mesh, BlockId block, float centreX, float centreY, float halfHeight,
                     float depth) {
    // Isometric projection of a unit cube: x runs right-and-down, z runs
    // left-and-down, y runs straight up. Only the three faces pointing at the
    // viewer are drawn, so the icon costs three quads rather than six.
    constexpr float kIsoX = 0.866f; // cos(30 degrees)
    constexpr float kIsoY = 0.5f;   // sin(30 degrees)

    const auto project = [&](float x, float y, float z) {
        return glm::vec2{centreX + (x - z) * kIsoX * halfHeight,
                         centreY + ((x + z) * kIsoY - y) * halfHeight};
    };

    // Matching order for every face, so one texture-coordinate set serves all.
    const glm::vec2 uvs[4]{{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    // Shades match the world mesher, so an icon reads the same way the placed
    // block does.
    const glm::vec2 top[4]{project(0, 1, 0), project(1, 1, 0), project(1, 1, 1), project(0, 1, 1)};
    appendQuadCorners(mesh, top, uvs, depth, glm::vec4{1.00f, 1.00f, 1.00f, 1.0f},
                      blockTextureLayer(block, BlockFace::Top));

    const glm::vec2 front[4]{project(0, 1, 1), project(1, 1, 1), project(1, 0, 1), project(0, 0, 1)};
    appendQuadCorners(mesh, front, uvs, depth, glm::vec4{0.86f, 0.86f, 0.86f, 1.0f},
                      blockTextureLayer(block, BlockFace::Side));

    const glm::vec2 right[4]{project(1, 1, 0), project(1, 1, 1), project(1, 0, 1), project(1, 0, 0)};
    appendQuadCorners(mesh, right, uvs, depth, glm::vec4{0.68f, 0.68f, 0.68f, 1.0f},
                      blockTextureLayer(block, BlockFace::Side));
}

float textWidth(std::string_view text, float charHeight) {
    return static_cast<float>(text.size()) * charHeight * kFontAspect;
}

float appendText(engine::MeshData& mesh, std::string_view text, float leftX, float centreY, float charHeight,
                 float depth, const glm::vec4& color) {
    const float charWidth = charHeight * kFontAspect;
    const glm::vec2 cell{kFontCellWidth, kFontCellHeight};

    for (std::size_t i = 0; i < text.size(); ++i) {
        const int code = static_cast<unsigned char>(text[i]);
        if (code <= kFontFirstChar || code > 126) {
            continue; // Space and anything unprintable leave a gap.
        }

        const int index = code - kFontFirstChar;
        const glm::vec2 pixelMin{static_cast<float>(index % kFontColumns) * kFontCellWidth,
                                 static_cast<float>(index / kFontColumns) * kFontCellHeight};

        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        const glm::vec2 uvMin = pixelMin / kFontSheetSize;
        const glm::vec2 uvMax = (pixelMin + cell) / kFontSheetSize;

        const float centreX = leftX + (static_cast<float>(i) + 0.5f) * charWidth;
        const float halfW = charWidth * 0.5f;
        const float halfH = charHeight * 0.5f;

        const glm::vec2 offsets[4]{{-halfW, -halfH}, {halfW, -halfH}, {halfW, halfH}, {-halfW, halfH}};
        const glm::vec2 uvs[4]{{uvMin.x, uvMin.y}, {uvMax.x, uvMin.y}, {uvMax.x, uvMax.y}, {uvMin.x, uvMax.y}};

        for (int corner = 0; corner < 4; ++corner) {
            mesh.vertices.push_back(engine::Vertex{{centreX + offsets[corner].x, centreY + offsets[corner].y, depth},
                                                   {color.r, color.g, color.b, color.a},
                                                   {uvs[corner].x, uvs[corner].y},
                                                   kFontLayer});
        }

        mesh.indices.insert(mesh.indices.end(), {base + 0, base + 1, base + 2, base + 0, base + 2, base + 3,
                                                 base + 0, base + 2, base + 1, base + 0, base + 3, base + 2});
    }

    return textWidth(text, charHeight);
}

} // namespace game::hud
