#include "chunk_worker_pool.hpp"
#include "../core/log.hpp"
#include <algorithm>

namespace mc {

ChunkWorkerPool::ChunkWorkerPool(uint32_t worldSeed, int numThreads)
    : generator(worldSeed) {

    if (numThreads <= 0) {
        unsigned hw = std::thread::hardware_concurrency();
        numThreads = (hw > 2) ? static_cast<int>(hw - 1) : 2;
    }
    numThreads = std::min(numThreads, 8);

    LOGI("ChunkWorkerPool: %d workers", numThreads);

    workers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back([this, i] { workerLoop(i); });
    }
}

ChunkWorkerPool::~ChunkWorkerPool() {
    shutdown();
}

void ChunkWorkerPool::shutdown() {
    bool expected = false;
    if (!stopFlag.compare_exchange_strong(expected, true)) return;

    cvPending.notify_all();
    cvReady.notify_all();

    for (auto& t : workers) if (t.joinable()) t.join();
    workers.clear();
}

bool ChunkWorkerPool::request(ChunkPtr chunk, float priority) {
    uint64_t key = keyOf(chunk->chunkX(), chunk->chunkZ());
    {
        std::lock_guard<std::mutex> lk(pendingMutex);
        if (activeKeys.count(key)) return false;
        activeKeys.insert(key);

        GenTask task;
        task.chunk = std::move(chunk);
        task.priority = priority;
        task.requestId = nextRequestId++;
        pending.push(std::move(task));
    }
    cvPending.notify_one();
    return true;
}

int ChunkWorkerPool::drainReady(std::vector<ChunkPtr>& out, int maxCount) {
    std::lock_guard<std::mutex> lk(readyMutex);
    int n = 0;
    while (n < maxCount && !ready.empty()) {
        out.push_back(std::move(ready.front()));
        ready.pop();
        ++n;
    }
    return n;
}

void ChunkWorkerPool::cancelFar(float radiusChunks) {
    float r2 = radiusChunks * radiusChunks;
    float cx = centerX.load();
    float cz = centerZ.load();

    std::priority_queue<GenTask, std::vector<GenTask>, GenTaskCompare> fresh;

    std::lock_guard<std::mutex> lk(pendingMutex);
    while (!pending.empty()) {
        GenTask t = std::move(const_cast<GenTask&>(pending.top()));
        pending.pop();

        float dx = static_cast<float>(t.chunk->chunkX()) - cx;
        float dz = static_cast<float>(t.chunk->chunkZ()) - cz;
        if (dx * dx + dz * dz > r2) {
            activeKeys.erase(keyOf(t.chunk->chunkX(), t.chunk->chunkZ()));
            continue;
        }
        fresh.push(std::move(t));
    }
    pending.swap(fresh);
}

size_t ChunkWorkerPool::pendingCount() const {
    std::lock_guard<std::mutex> lk(pendingMutex);
    return pending.size();
}

size_t ChunkWorkerPool::inFlightCount() const {
    std::lock_guard<std::mutex> lk(pendingMutex);
    return activeKeys.size() - pending.size();
}

void ChunkWorkerPool::workerLoop(int workerIndex) {
    LOGI("Worker %d started", workerIndex);

    while (!stopFlag.load(std::memory_order_relaxed)) {
        GenTask task;
        {
            std::unique_lock<std::mutex> lk(pendingMutex);
            cvPending.wait(lk, [this] {
                return stopFlag.load() || !pending.empty();
            });
            if (stopFlag.load()) return;
            if (pending.empty()) continue;

            task = std::move(const_cast<GenTask&>(pending.top()));
            pending.pop();
        }

        try {
            generator.generate(*task.chunk);
        } catch (...) {
            LOGE("Chunk generation failed");
        }

        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            activeKeys.erase(keyOf(task.chunk->chunkX(), task.chunk->chunkZ()));
        }
        {
            std::lock_guard<std::mutex> lk(readyMutex);
            ready.push(std::move(task.chunk));
        }
        cvReady.notify_one();
    }

    LOGI("Worker %d stopped", workerIndex);
}

} // namespace mc
