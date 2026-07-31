#pragma once

#include <cstdint>

namespace game {

/// Smooth, repeatable pseudo-randomness.
///
/// Everything here is a pure function of its arguments and uses integer hashing
/// rather than any floating-point random source, so the same seed and position
/// give bit-identical results on any machine, compiler or thread. That property
/// is what makes a world reproducible from a single number.
namespace noise {

/// Value noise in the range [0, 1]. Nearby positions give similar results, which
/// is what separates this from plain randomness.
float value2D(std::uint32_t seed, float x, float z);

/// Several octaves of `value2D` summed, each at double the frequency and half
/// the strength. One octave is smooth blobs; four or five gives large landforms
/// with believable finer detail on top. Returns [0, 1].
float fbm2D(std::uint32_t seed, float x, float z, int octaves);

/// Value noise in three dimensions, [0, 1]. Needed for anything that varies with
/// height as well as position — caves and overhangs cannot come from a heightmap.
float value3D(std::uint32_t seed, float x, float y, float z);

/// Octaves of `value3D`, same rules as `fbm2D`. Returns [0, 1].
float fbm3D(std::uint32_t seed, float x, float y, float z, int octaves);

} // namespace noise
} // namespace game
