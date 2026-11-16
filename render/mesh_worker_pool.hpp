#pragma once
#include "mesh_data.hpp"
#include "mesh_task.hpp"
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_set>
#include <cstdint>

namespace mc {

class MeshWorkerPool {
public:
    explicit MeshWorkerPool(int numThreads = 0);
    ~MeshWorkerPool();

    bool request(MeshTask task);
    int  drainReady(std::vector<MeshDataPtr>& out, int maxCount = 8);

    size_t pendingCount()  const;
    size_t inFlightCount() const;
    int    workerCount()   const { return static_cast<int>(workers.size()); }

    void shutdown();

private:
    void workerLoop(int index);

    static uint64_t keyOf(ChunkPos cx, ChunkPos cz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32)
             |  static_cast<uint32_t>(cz);
    }

    std::priority_queue<MeshTask, std::vector<MeshTask>, MeshTaskCompare> pending;
    std::queue<MeshDataPtr> ready;
    std::unordered_set<uint64_t> activeKeys;

    mutable std::mutex pendingMutex;
    mutable std::mutex readyMutex;
    std::condition_variable cvPending;
    std::condition_variable cvReady;

    std::atomic<bool> stopFlag{false};
    std::vector<std::thread> workers;
};

} // namespace mc
