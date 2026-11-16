#pragma once
#include "chunk.hpp"

namespace mc {

struct GenTask {
    ChunkPtr chunk;
    float priority = 0;      // меньше = важнее
    uint64_t requestId = 0;
};

struct GenTaskCompare {
    bool operator()(const GenTask& a, const GenTask& b) const {
        return a.priority > b.priority;   // min-heap
    }
};

} // namespace mc
