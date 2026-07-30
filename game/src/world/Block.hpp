#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace game {

/// One byte per block: a 32-cubed chunk is then 32 KB, small enough to copy
/// cheaply when meshing moves to a worker thread.
enum class BlockId : std::uint8_t {
    Air = 0,
    Stone,
    Dirt,
    Grass,
    Sand,
};

constexpr bool isSolid(BlockId id) {
    return id != BlockId::Air;
}

/// Placeholder appearance until textures arrive at M9. These are working
/// materials, not the game's final visual identity.
inline glm::vec3 blockColor(BlockId id) {
    switch (id) {
    case BlockId::Stone:
        return {0.56f, 0.57f, 0.60f};
    case BlockId::Dirt:
        return {0.48f, 0.35f, 0.24f};
    case BlockId::Grass:
        return {0.36f, 0.62f, 0.30f};
    case BlockId::Sand:
        return {0.82f, 0.75f, 0.52f};
    case BlockId::Air:
        break;
    }
    return {0.0f, 0.0f, 0.0f};
}

} // namespace game
