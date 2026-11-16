#pragma once
#include "mesh.hpp"
#include "../core/types.hpp"
#include <vector>
#include <memory>

namespace mc {

// Данные меша — обмен между воркером и главным потоком.
struct MeshData {
    std::vector<Vertex> vertices;
    ChunkPos cx = 0, cz = 0;
    uint32_t revision = 0;
};

using MeshDataPtr = std::shared_ptr<MeshData>;

} // namespace mc
