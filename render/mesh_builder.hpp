#pragma once
#include "mesh.hpp"
#include "../world/chunk.hpp"
#include <vector>

namespace mc {

// Строит геометрию чанка center. Соседи могут быть nullptr — тогда
// граничные грани считаются открытыми по правилу «нет данных = твердый».
void buildChunkGeometry(
    const Chunk& center,
    const Chunk* north,
    const Chunk* south,
    const Chunk* west,
    const Chunk* east,
    std::vector<Vertex>& out);

} // namespace mc
