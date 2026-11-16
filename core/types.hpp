#pragma once
#include <cstdint>

namespace mc {

using BlockId = uint8_t;
using ChunkPos = int32_t;

constexpr int CHUNK_SIZE   = 16;
constexpr int CHUNK_HEIGHT = 128;
constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_HEIGHT * CHUNK_SIZE;

} // namespace mc
