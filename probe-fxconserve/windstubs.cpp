// Link stubs for wind.cpp. Weather.cpp also carries `strike`,
// `precipitationFor` and `buildPrecipitationMesh`, which reach into World and
// Biome; the probe calls none of them. Each stub aborts rather than returning a
// plausible value, so if the probe ever does reach one it dies loudly instead
// of quietly measuring a stub and reporting it as the real thing.

#include "world/Biome.hpp"
#include "world/World.hpp"
#include "world/Weather.hpp"

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
const Biome& biomeInfo(BiomeId) { reached("biomeInfo"); }
bool biomeHasAny(BiomeId, unsigned int) { reached("biomeHasAny"); }
BiomeSample sampleBiome(unsigned int, int, int) { reached("sampleBiome"); }

int World::highestSolid(int, int) const { reached("World::highestSolid"); }
int World::skyLightAt(int, int, int) const { reached("World::skyLightAt"); }

} // namespace game
