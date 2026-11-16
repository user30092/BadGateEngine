#pragma once
#include "chunk_task.hpp"
#include "terrain_gen.hpp"
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <cstdint>

namespace mc {

class ChunkWorkerPool {
public:
    explicit ChunkWorkerPool(uint32_t worldSeed, int numThreads = 0);
    ~ChunkWorkerPool();

    bool request(ChunkPtr chunk, float priority);
    int  drainReady(std::vector<ChunkPtr>& out, int maxCount = 32);

    void setCenter(float cx, float cz) { centerX = cx; centerZ = cz; }
    void cancelFar(float radiusChunks);

    size_t pendingCount() const;
    size_t inFlightCount() const;

    void shutdown();

private:
    void workerLoop(int workerIndex);

    static uint64_t keyOf(ChunkPos cx, ChunkPos cz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32)
             |  static_cast<uint32_t>(cz);
    }

    std::priority_queue<GenTask, std::vector<GenTask>, GenTaskCompare> pending;
    std::queue<ChunkPtr> ready;
    mutable std::mutex pendingMutex;
    mutable std::mutex readyMutex;
    std::condition_variable cvPending;
    std::condition_variable cvReady;

    std::unordered_set<uint64_t> activeKeys;

    std::atomic<bool> stopFlag{false};
    std::atomic<float> centerX{0}, centerZ{0};
    uint64_t nextRequestId = 1;

    TerrainGenerator generator;
    std::vector<std::thread> workers;
};

} // namespace mc
