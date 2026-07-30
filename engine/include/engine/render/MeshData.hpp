#pragma once

#include "engine/render/Vertex.hpp"

#include <cstdint>
#include <vector>

namespace engine {

/// CPU-side geometry, ready to be uploaded.
///
/// Deliberately plain data with no GPU handles in it. Whatever generates content
/// produces one of these and hands it over; the renderer only consumes it. That
/// separation is what will let mesh generation move to a worker thread later
/// without the renderer being involved.
struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;

    bool empty() const { return vertices.empty() || indices.empty(); }
};

} // namespace engine
