#pragma once

#include <cstdint>

namespace game {

/// The five noise fields every decision about the landscape is made from.
///
/// **This is the structural idea worth understanding before changing anything
/// here.** Up to 2026-08-07 a biome was chosen from temperature and humidity,
/// and then the *biome* supplied a base height and an amplitude — so the land
/// was a consequence of the label. The reference reversed that in its 1.18
/// rewrite and we now follow it: terrain and biome are **both outputs of the
/// same fields**, so they agree without either driving the other. A biome no
/// longer knows how tall it is.
///
/// Ranges are nominally [-1, 1] and are hard-clamped, so a row in the biome
/// table can always say `{-1, 1}` and mean "any".
struct Climate {
    /// Hot to cold. Decides snow, ice, and which of a biome family you get.
    float temperature;

    /// Wet to dry. Independent of temperature on purpose — a single "climate"
    /// value can only order regions along a line, which is why it could never
    /// separate desert from plains from tundra convincingly.
    float humidity;

    /// Ocean to inland. **The field that makes a coastline structural** rather
    /// than "wherever the height noise happened to dip below sea level".
    float continentalness;

    /// How worn down the land is. Low is mountainous, high is flat — but its
    /// real job is `Shape::factor`, not picking a biome.
    float erosion;

    /// The raw ridge field. Only ever used through `ridges`.
    float weirdness;

    /// Peaks and valleys, derived from `weirdness` by `ridgesFromWeirdness`.
    /// Worth knowing: it reaches -1 exactly where `weirdness` crosses **zero**,
    /// and the zero contour of a smooth 2D field is a network of curves — which
    /// is where rivers come from here, with no river system of their own.
    float ridges;
};

/// `1 - |3|w| - 2|`, the reference's own expression.
///
/// It folds the ridge field so that a single smooth noise gives *two* valley
/// lines and one peak line per period, which is what makes mountain ranges read
/// as ranges rather than as isolated lumps.
float ridgesFromWeirdness(float weirdness);

Climate climateAt(std::uint32_t seed, int worldX, int worldZ);

/// What the land is trying to be at a column, before any 3D noise touches it.
///
/// These are the reference's `offset` / `factor` / `jaggedness`, and the second
/// one is the least obvious and the most important: **`factor` is not a height,
/// it is how hard `offsetY` is enforced.** A high factor pins the surface to the
/// target and the land reads as flat or as a smooth massif; a low factor lets
/// the 3D noise run and the same target height comes out rugged. That single
/// split is what lets flat plains and jagged hills exist at the same
/// temperature, humidity and altitude.
struct Shape {
    /// Target surface height, in blocks. Already in our world's units — the
    /// reference works in abstract density units and converts later, which
    /// buys nothing here and hides every number behind a scale factor.
    float offsetY;

    /// Blocks of vertical wobble the 3D noise is allowed is `kReliefBlocks /
    /// factor`, so bigger is smoother.
    float factor;

    /// Extra high-frequency height added at peaks only. Zero everywhere else,
    /// because applying it globally just makes the whole world noisy.
    ///
    /// **A 0-to-1 weight, not a number of blocks** - `shapeAt` multiplies it by
    /// `kJaggedBlocks` before adding it to `offsetY`, and uses the same weight a
    /// second time to blend `factor` toward `kRuggedFactor`. Reading it as a
    /// height and adding it straight to a y would be off by that factor of 13.
    float jaggedness;
};

Shape shapeAt(std::uint32_t seed, const Climate& climate, int worldX, int worldZ);

/// True where an exposed surface freezes — snow on the ground, ice on the water.
///
/// **`warmth` is the biome's own constant, and using anything else is the bug
/// this signature exists to prevent.** The reference's `freeze_top_layer` asks
/// `Biome.warmEnoughToRain`, which reads the fixed `temperature` off the biome
/// — plains 0.8, snowy plains 0.0 — and never once looks at the noise field
/// that placed the biome there. Ours read the noise field, and that is exactly
/// what put patches of snow on plains: the field is *continuous* across a biome
/// edge while the biome flips at a hard box boundary, so every warm column that
/// happened to fall in the cold tail whitened, and a hundred of them together
/// read as a snowy biome bleeding over the border.
///
/// The arithmetic is the reference's: threshold 0.15, no lapse at all below its
/// sea level + 17, then 0.00125 per block, jittered by a noise worth eight
/// blocks of altitude. **The jitter is not decoration** — a narrow interval of
/// a smooth field is a contour line, so without it the snow line is a clean
/// ring drawn around every hill. Only the vertical scale is ours: our 66 blocks
/// of land stand in for its 257.
bool freezesAt(std::uint32_t seed, float warmth, int worldX, int worldY, int worldZ);

/// How far the 3D noise can move the surface at `factor == 1`, in blocks.
/// Exposed because the density function and the shaping splines have to agree
/// on it, and a second copy of this number would be the usual bug.
inline constexpr float kReliefBlocks = 22.0f;

} // namespace game
