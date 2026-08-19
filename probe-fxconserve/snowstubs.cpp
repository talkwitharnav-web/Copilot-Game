// Link stubs for snow.cpp. This probe links the REAL Biome.cpp so the biome
// table cannot drift from what ships, which means it must not restate the three
// symbols Biome.cpp defines - that was a duplicate-symbol link error, and the
// fix is to stub strictly the remainder.
//
// `climateAt` and `shapeAt` are reached only through `sampleBiome`, which this
// probe never calls: it asks `precipitationFor(biome, y)` directly so it can
// sweep every biome rather than whichever ones a seed happens to produce. Each
// stub aborts rather than returning a plausible value.

#include "world/Biome.hpp"
#include "world/Climate.hpp"
#include "world/Weather.hpp"
#include "world/World.hpp"

#include <cstdio>
#include <cstdlib>

namespace {
[[noreturn]] void reached(const char* name) {
    std::printf("\nPROBE ERROR: reached stub '%s' - the measurement is invalid.\n", name);
    std::abort();
}
} // namespace

namespace game {

bool freezesAt(unsigned int, float, int, int, int) { reached("freezesAt"); }
Climate climateAt(unsigned int, int, int) { reached("climateAt"); }
Shape shapeAt(unsigned int, const Climate&, int, int) { reached("shapeAt"); }

int World::highestSolid(int, int) const { reached("World::highestSolid"); }
int World::skyLightAt(int, int, int) const { reached("World::skyLightAt"); }

} // namespace game
