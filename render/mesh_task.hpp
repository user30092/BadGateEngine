#pragma once
#include "mesh_data.hpp"
#include "../world/chunk.hpp"

namespace mc {

struct MeshTask {
    ChunkPtr center;
    ChunkPtr north;   // cz - 1
    ChunkPtr south;   // cz + 1
    ChunkPtr west;    // cx - 1
    ChunkPtr east;    // cx + 1
    uint32_t centerRevision = 0;
    float    priority       = 0;
};

struct MeshTaskCompare {
    bool operator()(const MeshTask& a, const MeshTask& b) const {
        return a.priority > b.priority;   // min-heap
    }
};

} // namespace mc
