#pragma once
#include "../core/types.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace mc {

// Ключ чанка в координатах чанков. Единый тип для World / WorldRenderer /
// ChunkWorkerPool — устраняет несовместимость unordered_map<ChunkKey>::find().
struct ChunkKey {
    ChunkPos x, z;
    bool operator==(const ChunkKey& o) const { return x == o.x && z == o.z; }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const {
        return (static_cast<uint64_t>(static_cast<uint32_t>(k.x)) << 32)
             |  static_cast<uint32_t>(k.z);
    }
};

class Chunk {
public:
    Chunk(ChunkPos cx, ChunkPos cz) : cx(cx), cz(cz) {
        blocks.fill(0);
        // dirty = 1, revision = 0
        state.store(1ULL, std::memory_order_relaxed);
    }

    BlockId get(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_SIZE ||
            y < 0 || y >= CHUNK_HEIGHT ||
            z < 0 || z >= CHUNK_SIZE) return 0;
        return blocks[index(x, y, z)];
    }

    void set(int x, int y, int z, BlockId id) {
        if (x < 0 || x >= CHUNK_SIZE ||
            y < 0 || y >= CHUNK_HEIGHT ||
            z < 0 || z >= CHUNK_SIZE) return;
        BlockId& slot = blocks[index(x, y, z)];
        if (slot == id) return;
        slot = id;
        markNeedsRemesh();
    }

    // Помечает чанк как требующий перестройки меша.
    // Атомарно инкрементирует revision и ставит dirty = 1.
    void markNeedsRemesh() {
        uint64_t oldState = state.load(std::memory_order_relaxed);
        uint64_t newState;
        do {
            uint32_t rev = static_cast<uint32_t>(oldState >> 32) + 1;
            newState = (static_cast<uint64_t>(rev) << 32) | 1ULL;
        } while (!state.compare_exchange_weak(oldState, newState,
                                              std::memory_order_release,
                                              std::memory_order_relaxed));
    }

    uint32_t revision() const {
        return static_cast<uint32_t>(state.load(std::memory_order_acquire) >> 32);
    }

    bool isDirty() const {
        return (state.load(std::memory_order_acquire) & 1ULL) != 0;
    }

    // Сброс dirty, если revision не изменился с момента чтения.
    // true  — dirty сброшен (меш актуален).
    // false — revision ушёл, меш устарел, dirty остаётся.
    bool markCleanIfRevision(uint32_t expectedRevision) {
        uint64_t oldState = state.load(std::memory_order_acquire);
        while (true) {
            uint32_t rev = static_cast<uint32_t>(oldState >> 32);
            if (rev != expectedRevision) return false;
            if ((oldState & 1ULL) == 0) return true;   // уже clean
            uint64_t newState = static_cast<uint64_t>(rev) << 32; // dirty=0
            if (state.compare_exchange_weak(oldState, newState,
                                            std::memory_order_release,
                                            std::memory_order_acquire)) {
                return true;
            }
        }
    }

    BlockId*       rawData()       { return blocks.data(); }
    const BlockId* rawData() const { return blocks.data(); }

    ChunkPos chunkX() const { return cx; }
    ChunkPos chunkZ() const { return cz; }

private:
    static constexpr int index(int x, int y, int z) {
        return (y * CHUNK_SIZE + z) * CHUNK_SIZE + x;
    }

    ChunkPos cx, cz;
    std::array<BlockId, CHUNK_VOLUME> blocks{};

    // Упакованное состояние: [revision:32][dirty:1][reserved:31]
    std::atomic<uint64_t> state{1ULL};
};

using ChunkPtr = std::shared_ptr<Chunk>;

} // namespace mc
