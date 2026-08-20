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
/// Space between the widest label and the column of values.
///
/// **The column itself is measured, not written down.** It was a flat 0.20,
/// which is 41 texels at this character height, and `tone F10` alone is 44 -
/// so the two longest labels ran straight into their own values with no gap at
/// all. A literal cannot follow a label being renamed; `hud::textWidth` can.
constexpr float kColumnGap = 0.024f;
constexpr float kGraphHeight = 0.15f;
constexpr float kGraphGap = 0.026f;

/// A frame at or under this is comfortably inside a 60 fps budget.
constexpr float kGoodMilliseconds = 16.67f;
constexpr float kGraphMaxMilliseconds = 25.0f;

// Depth bands. A HUD element hidden behind another produces no warning of any
// kind, so these are spaced far more generously than precision requires.
//
// **Not a depth test.** The UI pass has no depth attachment at all - the HUD
// pipeline in `Renderer.cpp` sets `depthFormat = VK_FORMAT_UNDEFINED`,
// `depthTest = false` and `depthWrite = false` - so z is only the key its
// `stable_sort` runs on, far first, and equal keys fall back to append order.
// `StatusBars.cpp` says this at its own band and `InventoryScreen.cpp` at its
// held-stack append; this file is the third that needs it and the one where it
// bites.
//
// **Ordering is decided by mesh first and z only within a mesh, so these
// numbers may not be compared against another screen's.** `Main.cpp` appends
// this overlay to the `top` mesh, drawn after `main` and `clipped` whatever the
// numbers say - and that is load-bearing rather than incidental, because these
// bands sit *inside* `InventoryScreen.cpp`'s range: 0.0050 is farther than its
// `kDimDepth` of 0.0044, and `kTextDepth` here is exactly its `kLabelDepth`.
// Read as one scale, this panel is buried by an open screen's dim quad - which
// is not a hypothetical, it is what happened until the overlay was moved to
// `top`, and only the text survived. Moving it back to the main mesh, or
// renumbering these to "fit" that file's scheme, restores the bug.
//
// Falsified by `Main.cpp` appending `makeDebugOverlay` to anything but
// `topLayer`. Re-read that call site rather than trusting this paragraph.
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

/// Abbreviates large counts so a long one stays a few characters wide.
///
/// **Only the rows that call it, and it is not a clip.** It was commented as
/// though it stopped values overflowing the column, which put every other row
/// beyond suspicion - `fitToWidth` is what actually enforces the width, and
/// this only keeps triangle counts readable.
std::string formatCount(std::uint32_t value) {
    if (value < 10'000) {
        return std::to_string(value);
    }
    if (value < 1'000'000) {
        return formatFloat(static_cast<float>(value) / 1'000.0f, 1) + "k";
    }
    return formatFloat(static_cast<float>(value) / 1'000'000.0f, 2) + "M";
}

/// The longest head of `text` that fits, with two dots marking what was cut.
///
/// Nothing clipped these before, and the overlay reports numbers that grow
/// without limit - allocation counts, chunk totals, a biome name from a table
/// that keeps getting longer. A value that outgrows the panel is drawn straight
/// across the world with no border under it, which reads as corruption rather
/// than as a long number.
std::string fitToWidth(std::string text, float charHeight, float available) {
    if (hud::textWidth(text, charHeight) <= available) {
        return text;
    }
    while (!text.empty() && hud::textWidth(text + "..", charHeight) > available) {
        text.pop_back();
    }
    return text + "..";
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

    // **The row count is deduced, never written down.** This was
    // `std::array<Row, 20>`, and `std::array` is silent about a *short*
    // initialiser: deleting a row would have left a zero-filled `Row` whose
    // `label` is a null `const char*`, handed straight to `appendText` and to
    // the width measurement below. Too *long* is a hard error, too short is
    // not, so only one direction of that edit is caught. `std::to_array` takes
    // the bound from the list, which makes both directions impossible rather
    // than merely checked - the same reason `kArmourSlots` is derived from
    // `ArmourSlot::None` instead of being written as 4.
    const auto rows = std::to_array<Row>({
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
        // **Two facts, two rows.** Written as one, this was the longest value
        // on the panel by half again - the megabytes, two allocation counts and
        // a slash, which no plausible column width fits. They are also read for
        // different reasons: the first is "am I near the memory budget", the
        // second is "am I near the driver's allocation limit".
        {"gpu mem", std::to_string(stats.gpuMegabytes) + " MB"},
        {"allocs", std::to_string(stats.deviceAllocations) + "/" +
                       std::to_string(stats.deviceAllocationLimit)},
        {"workers", std::to_string(stats.workerThreads)},
        {"tone F10", stats.toneMapper},
        {"shade G", stats.shadows},
        {"cloud C", stats.clouds},
    });

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

    // Labels in a dim column, values aligned in a brighter one. **The column is
    // the widest label plus a gap**, measured through the same function that
    // draws the text, so renaming a row moves the values instead of running
    // into them.
    float widestLabel = 0.0f;
    for (const Row& row : rows) {
        widestLabel = std::max(widestLabel, hud::textWidth(row.label, kCharHeight));
    }
    const float valueColumn = contentLeft + widestLabel + kColumnGap;
    const float valueRoom = contentRight - valueColumn;

    float rowCentre = rowsTop + kLineHeight * 0.5f;
    for (const Row& row : rows) {
        hud::appendText(mesh, row.label, contentLeft, rowCentre, kCharHeight, kTextDepth, kLabelColor);
        hud::appendText(mesh, fitToWidth(row.value, kCharHeight, valueRoom), valueColumn, rowCentre,
                        kCharHeight, kTextDepth, kValueColor);
        rowCentre += kLineHeight;
    }

    return mesh;
}

} // namespace game
