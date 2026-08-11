#include "hud/DebugOverlay.hpp"

#include "hud/HudPrimitives.hpp"
#include "world/Block.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace game {
namespace {

const float kWhite = static_cast<float>(TextureLayer::White);

// Units are relative to window height: Y runs -1 (top) to +1 (bottom), and X
// runs -aspect to +aspect. The renderer applies the aspect correction, so a
// width and a height written here are already the same physical size.
constexpr float kMargin = 0.035f;
constexpr float kPadding = 0.024f;
constexpr float kPanelWidth = 0.56f;
/// The font cell is 8 px square and this draws it at a little under twice that,
/// which is close to the size the overlay has always been. It is deliberately
/// **not** derived from the cell: the panel is sized in screen units and the
/// atlas can be swapped for one drawn at another resolution.
constexpr float kCharHeight = 14.0f / 360.0f;
constexpr float kLineHeight = 0.052f;
constexpr float kValueColumn = 0.20f; // Values start here, so they line up.
constexpr float kGraphHeight = 0.15f;
constexpr float kGraphGap = 0.026f;

/// A frame at or under this is comfortably inside a 60 fps budget.
constexpr float kGoodMilliseconds = 16.67f;
constexpr float kGraphMaxMilliseconds = 25.0f;

// Depth bands. A HUD element hidden behind another produces no warning of any
// kind, so these are spaced far more generously than precision requires.
constexpr float kPanelDepth = 0.0050f;
constexpr float kGraphBackDepth = 0.0045f;
constexpr float kGraphInkDepth = 0.0040f;
constexpr float kTextDepth = 0.0035f;

constexpr glm::vec4 kPanelColor{0.035f, 0.038f, 0.048f, 0.90f};
constexpr glm::vec4 kPanelEdge{0.33f, 0.36f, 0.42f, 0.60f};
constexpr glm::vec4 kGraphBack{0.012f, 0.014f, 0.020f, 0.95f};
constexpr glm::vec4 kGuideColor{0.38f, 0.41f, 0.47f, 0.80f};
constexpr glm::vec4 kLabelColor{0.58f, 0.62f, 0.69f, 1.0f};
constexpr glm::vec4 kValueColor{0.93f, 0.94f, 0.96f, 1.0f};
constexpr glm::vec4 kBarGood{0.27f, 0.56f, 0.40f, 1.0f};
constexpr glm::vec4 kBarBad{0.80f, 0.47f, 0.28f, 1.0f};

/// Formats a float with a fixed number of decimals. `snprintf` rather than
/// `std::to_string`, which always emits six decimals and cannot be told not to.
std::string formatFloat(float value, int decimals) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.*f", decimals, static_cast<double>(value));
    return std::string{buffer.data()};
}

/// Abbreviates large counts so a value never overflows its column.
std::string formatCount(std::uint32_t value) {
    if (value < 10'000) {
        return std::to_string(value);
    }
    if (value < 1'000'000) {
        return formatFloat(static_cast<float>(value) / 1'000.0f, 1) + "k";
    }
    return formatFloat(static_cast<float>(value) / 1'000'000.0f, 2) + "M";
}

/// Draws a filled rectangle from its edges rather than its centre, which is how
/// every piece of this layout is naturally described.
void appendRect(engine::MeshData& mesh, float left, float top, float right, float bottom, float depth,
                const glm::vec4& color) {
    hud::appendQuad(mesh, (left + right) * 0.5f, (top + bottom) * 0.5f, (right - left) * 0.5f, (bottom - top) * 0.5f,
                    depth, color, kWhite, false);
}

struct Row {
    const char* label;
    std::string value;
};

} // namespace

engine::MeshData makeDebugOverlay(const OverlayStats& stats, const std::vector<float>& history, float aspect) {
    engine::MeshData mesh;

    const std::array<Row, 19> rows{{
        {"fps", std::to_string(stats.fps)},
        {"cpu", formatFloat(stats.frameMilliseconds, 2) + " ms"},
        {"gpu", formatFloat(stats.gpuMilliseconds, 2) + " ms"},
        {"biome", stats.biome},
        {"air", formatFloat(stats.air, 1) + " s"},
        {"dist", std::to_string(stats.renderDistance)},
        {"detail", stats.detailDistance >= stats.renderDistance
                       ? std::string("off")
                       : std::to_string(stats.detailDistance) + "  " +
                             std::to_string(stats.detailedChunks) + " ch"},
        {"tier lag", stats.detailLagChunks == 0
                         ? std::string("0")
                         : std::to_string(stats.detailLagChunks) + " at " +
                               std::to_string(stats.detailLagNearest)},
        {"chunks", std::to_string(stats.loadedChunks)},
        {"meshes", std::to_string(stats.meshes)},
        {"queued", std::to_string(stats.pending)},
        {"retired", std::to_string(stats.retired)},
        {"draws", std::to_string(stats.drawCalls)},
        {"tris", formatCount(stats.triangles)},
        {"gpu mem", std::to_string(stats.gpuMegabytes) + " MB  " +
                       std::to_string(stats.deviceAllocations) + "/" +
                       std::to_string(stats.deviceAllocationLimit)},
        {"workers", std::to_string(stats.workerThreads)},
        {"tone F10", stats.toneMapper},
        {"shade G", stats.shadows},
        {"cloud C", stats.clouds},
    }};

    const float panelLeft = -aspect + kMargin;
    const float panelTop = -1.0f + kMargin;
    const float panelRight = panelLeft + kPanelWidth;

    const float contentLeft = panelLeft + kPadding;
    const float contentRight = panelRight - kPadding;

    const float graphTop = panelTop + kPadding;
    const float graphBottom = graphTop + kGraphHeight;
    const float rowsTop = graphBottom + kGraphGap;
    const float panelBottom = rowsTop + static_cast<float>(rows.size()) * kLineHeight + kPadding;

    // A hairline border rather than a hard edge, so the panel reads as an
    // overlay instead of a hole punched in the scene.
    constexpr float kEdge = 0.004f;
    appendRect(mesh, panelLeft - kEdge, panelTop - kEdge, panelRight + kEdge, panelBottom + kEdge,
               kPanelDepth + 0.0002f, kPanelEdge);
    appendRect(mesh, panelLeft, panelTop, panelRight, panelBottom, kPanelDepth, kPanelColor);

    appendRect(mesh, contentLeft, graphTop, contentRight, graphBottom, kGraphBackDepth, kGraphBack);

    const auto heightFor = [](float milliseconds) {
        return kGraphHeight * std::clamp(milliseconds / kGraphMaxMilliseconds, 0.0f, 1.0f);
    };

    // One guide line, at the 60 fps budget. It is the only threshold on the
    // graph that means anything, so it gets a line instead of a labelled axis.
    const float guideY = graphBottom - heightFor(kGoodMilliseconds);
    appendRect(mesh, contentLeft, guideY - 0.0015f, contentRight, guideY + 0.0015f, kGraphInkDepth, kGuideColor);

    if (!history.empty()) {
        const float barPitch = (contentRight - contentLeft) / static_cast<float>(history.size());
        for (std::size_t i = 0; i < history.size(); ++i) {
            const float sample = history[i];
            const float height = heightFor(sample);
            if (height <= 0.0f) {
                continue;
            }

            const float left = contentLeft + static_cast<float>(i) * barPitch;
            const glm::vec4& color = (sample <= kGoodMilliseconds) ? kBarGood : kBarBad;
            appendRect(mesh, left, graphBottom - height, left + barPitch, graphBottom, kGraphInkDepth, color);
        }
    }

    // Labels in a dim column, values aligned in a brighter one.
    float rowCentre = rowsTop + kLineHeight * 0.5f;
    for (const Row& row : rows) {
        hud::appendText(mesh, row.label, contentLeft, rowCentre, kCharHeight, kTextDepth, kLabelColor);
        hud::appendText(mesh, row.value, contentLeft + kValueColumn, rowCentre, kCharHeight, kTextDepth, kValueColor);
        rowCentre += kLineHeight;
    }

    return mesh;
}

} // namespace game
