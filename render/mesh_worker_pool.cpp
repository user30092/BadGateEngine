#include "mesh_worker_pool.hpp"
#include "mesh_builder.hpp"
#include "../core/log.hpp"
#include <algorithm>

namespace mc {

MeshWorkerPool::MeshWorkerPool(int numThreads) {
    if (numThreads <= 0) {
        unsigned hw = std::thread::hardware_concurrency();
        numThreads = (hw >= 4) ? 2 : 1;
    }
    numThreads = std::min(numThreads, 4);

    LOGI("MeshWorkerPool: %d workers", numThreads);
    workers.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i) {
        workers.emplace_back([this, i] { workerLoop(i); });
    }
}

MeshWorkerPool::~MeshWorkerPool() {
    shutdown();
}

void MeshWorkerPool::shutdown() {
    bool expected = false;
    if (!stopFlag.compare_exchange_strong(expected, true)) return;

    cvPending.notify_all();
    cvReady.notify_all();

    for (auto& t : workers) if (t.joinable()) t.join();
    workers.clear();
}

bool MeshWorkerPool::request(MeshTask task) {
    uint64_t key = keyOf(task.center->chunkX(), task.center->chunkZ());
    {
        std::lock_guard<std::mutex> lk(pendingMutex);
        if (activeKeys.count(key)) return false;
        activeKeys.insert(key);
        pending.push(std::move(task));
    }
    cvPending.notify_one();
    return true;
}

int MeshWorkerPool::drainReady(std::vector<MeshDataPtr>& out, int maxCount) {
    std::lock_guard<std::mutex> lk(readyMutex);
    int n = 0;
    while (n < maxCount && !ready.empty()) {
        out.push_back(std::move(ready.front()));
        ready.pop();
        ++n;
    }
    return n;
}

size_t MeshWorkerPool::pendingCount() const {
    std::lock_guard<std::mutex> lk(pendingMutex);
    return pending.size();
}

size_t MeshWorkerPool::inFlightCount() const {
    std::lock_guard<std::mutex> lk(pendingMutex);
    return activeKeys.size() - pending.size();
}

void MeshWorkerPool::workerLoop(int idx) {
    LOGI("MeshWorker %d started", idx);

    while (!stopFlag.load(std::memory_order_relaxed)) {
        MeshTask task;
        {
            std::unique_lock<std::mutex> lk(pendingMutex);
            cvPending.wait(lk, [this] {
                return stopFlag.load() || !pending.empty();
            });
            if (stopFlag.load()) return;
            if (pending.empty()) continue;

            task = std::move(const_cast<MeshTask&>(pending.top()));
            pending.pop();
        }

        auto data = std::make_shared<MeshData>();
        data->cx = task.center->chunkX();
        data->cz = task.center->chunkZ();
        data->revision = task.centerRevision;

        buildChunkGeometry(*task.center,
                           task.north.get(), task.south.get(),
                           task.west.get(),  task.east.get(),
                           data->vertices);

        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            activeKeys.erase(keyOf(data->cx, data->cz));
        }
        {
            std::lock_guard<std::mutex> lk(readyMutex);
            ready.push(std::move(data));
        }
        cvReady.notify_one();
    }

    LOGI("MeshWorker %d stopped", idx);
}

} // namespace mc
