#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace game {

/// Which texels of each sprite are solid.
///
/// A dropped tool is not a picture standing on its edge, it is a **shape**: the
/// reference builds one by extruding the sprite a texel deep and walling in
/// every boundary between a solid texel and an empty one. That needs the
/// silhouette on the CPU, which is why the texture array keeps its alpha.
///
/// The threshold is the fragment shader's own. Anything it would discard has to
/// count as absent here too, or a side wall is left standing around a hole
/// nobody can see.
class SpriteMask {
public:
    SpriteMask() = default;
    SpriteMask(int width, int height, std::vector<std::uint8_t> alpha)
        : m_width(width), m_height(height), m_alpha(std::move(alpha)) {}

    int width() const { return m_width; }
    int height() const { return m_height; }

    /// Out of range is empty, so an edge test can walk off the sprite without
    /// checking first - the boundary of the image is a boundary of the shape.
    bool solid(int layer, int x, int y) const {
        if (layer < 0 || x < 0 || y < 0 || x >= m_width || y >= m_height) {
            return false;
        }
        const auto index =
            (static_cast<std::size_t>(layer) * m_height + static_cast<std::size_t>(y)) * m_width +
            static_cast<std::size_t>(x);
        return index < m_alpha.size() && m_alpha[index] >= 128;
    }

private:
    int m_width = 0;
    int m_height = 0;
    std::vector<std::uint8_t> m_alpha;
};

} // namespace game
