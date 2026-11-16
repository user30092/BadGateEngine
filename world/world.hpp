#pragma once
#include "chunk.hpp"
#include "chunk_worker_pool.hpp"
#include "world_save.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <cmath>
#include <utility>

namespace mc {

class World {
public:
    // saveDir пустой => режим без сохранения на диск (например, при
    // подключении к чужому серверу — там авторитетный мир на стороне сервера).
    explicit World(uint32_t seed, int workerThreads = 0, std::string saveDir = "")
        : pool(seed, workerThreads), seed_(seed), saveDir_(std::move(saveDir)) {
        if (!saveDir_.empty()) ensureDir(saveDir_);
    }

    // --- Чтение/запись ---

    BlockId getBlock(int wx, int wy, int wz) const {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return 0;
        ChunkPos cx = floorDiv(wx, CHUNK_SIZE);
        ChunkPos cz = floorDiv(wz, CHUNK_SIZE);
        auto it = chunks.find(ChunkKey{cx, cz});
        if (it == chunks.end()) return 8;  // Bedrock — невидимая стена
        int lx = wx - cx * CHUNK_SIZE;
        int lz = wz - cz * CHUNK_SIZE;
        return it->second->get(lx, wy, lz);
    }

    void setBlock(int wx, int wy, int wz, BlockId id) {
        if (wy < 0 || wy >= CHUNK_HEIGHT) return;
        ChunkPos cx = floorDiv(wx, CHUNK_SIZE);
        ChunkPos cz = floorDiv(wz, CHUNK_SIZE);
        auto it = chunks.find(ChunkKey{cx, cz});
        if (it == chunks.end()) return;

        int lx = wx - cx * CHUNK_SIZE;
        int lz = wz - cz * CHUNK_SIZE;

        it->second->set(lx, wy, lz, id);

        // Соседние чанки нужно перестроить, если блок на границе.
        if (lx == 0)              markNeedsRemesh(cx - 1, cz);
        if (lx == CHUNK_SIZE - 1) markNeedsRemesh(cx + 1, cz);
        if (lz == 0)              markNeedsRemesh(cx, cz - 1);
        if (lz == CHUNK_SIZE - 1) markNeedsRemesh(cx, cz + 1);
    }

    // --- Многопользовательский клиент ---
    // В мультиплеере чанки приходят по сети (ChunkData от сервера) —
    // локальная генерация отключается, а updateStreaming() продолжает
    // работать только как механизм эвикции дальних чанков из памяти
    // клиента (см. ниже). Сервер авторитетен.
    void setNetworkOnly(bool v) { networkOnly_ = v; }

    void injectChunk(ChunkPtr chunk) {
        chunks[ChunkKey{chunk->chunkX(), chunk->chunkZ()}] = std::move(chunk);
    }

    // --- Стриминг (одиночная игра / клиент мультиплеера) ---

    void updateStreaming(float playerBlockX, float playerBlockZ, int renderDistance) {
        pool.setCenter(playerBlockX / CHUNK_SIZE, playerBlockZ / CHUNK_SIZE);

        std::vector<ChunkPtr> ready;
        ready.reserve(32);
        pool.drainReady(ready, 32);
        for (auto& c : ready) {
            ChunkKey k{c->chunkX(), c->chunkZ()};
            chunks[k] = std::move(c);
        }

        int pcx = static_cast<int>(std::floor(playerBlockX / CHUNK_SIZE));
        int pcz = static_cast<int>(std::floor(playerBlockZ / CHUNK_SIZE));
        int r2 = renderDistance * renderDistance;

        if (!networkOnly_) {
            for (int dz = -renderDistance; dz <= renderDistance; ++dz)
            for (int dx = -renderDistance; dx <= renderDistance; ++dx) {
                if (dx * dx + dz * dz > r2) continue;
                ChunkPos cx = pcx + dx;
                ChunkPos cz = pcz + dz;
                ChunkKey k{cx, cz};
                if (chunks.count(k)) continue;

                // Сначала пробуем поднять регион с диска — если игрок тут
                // уже был и что-то поменял, генерировать заново нельзя, это
                // стёрло бы его правки. Регион грузится синхронно, но
                // только один раз на игровую сессию.
                if (!saveDir_.empty()) loadRegionIfNeeded(cx, cz);
                if (chunks.count(k)) continue;

                float ddx = static_cast<float>(dx);
                float ddz = static_cast<float>(dz);
                float prio = ddx * ddx + ddz * ddz;

                auto chunk = std::make_shared<Chunk>(cx, cz);
                pool.request(std::move(chunk), prio);
            }
        }

        if (++evictCounter >= 60) {
            evictCounter = 0;
            evictFarChunks(pcx, pcz, renderDistance + 2);
        }
        if (!saveDir_.empty() && ++autosaveCounter >= AUTOSAVE_INTERVAL_TICKS) {
            autosaveCounter = 0;
            saveAll();
        }
    }

    void shutdown() {
        pool.shutdown();
        if (!saveDir_.empty()) saveAll();
    }

    // Сохраняет все изменённые регионы на диск. Вызывается периодически
    // (см. AUTOSAVE_INTERVAL_TICKS) и один раз при shutdown().
    void saveAll() {
        std::unordered_set<uint64_t> regionsToSave;
        for (auto& [key, chunk] : chunks) regionsToSave.insert(regionKey(key.x, key.z));
        for (uint64_t rk : regionsToSave) saveRegion(rk);
    }

    // --- Многопользовательский сервер ---
    // updateStreaming() выше рассчитан на ОДНОГО зрителя: у него единый
    // центр для pool.setCenter() и единая точка эвикции. На сервере
    // одновременно несколько игроков с разными позициями — наивный вызов
    // updateStreaming() по очереди для каждого игрока дёргал бы приоритеты
    // генерации и, что хуже, evictFarChunks() от игрока A мог бы выгрузить
    // чанки, нужные игроку B. Поэтому для сервера — отдельная пара методов
    // без единого центра: запрос без эвикции + эвикция по объединению
    // радиусов всех игроков разом.

    // Запрашивает чанки вокруг точки, ничего не эвиктит. Чанки, которые
    // уже есть в памяти или уже стоят в очереди генерации, пропускаются.
    void requestChunksNear(float worldX, float worldZ, int radiusChunks) {
        std::vector<ChunkPtr> ready;
        ready.reserve(32);
        pool.drainReady(ready, 32);
        for (auto& c : ready) chunks[ChunkKey{c->chunkX(), c->chunkZ()}] = std::move(c);

        int pcx = static_cast<int>(std::floor(worldX / CHUNK_SIZE));
        int pcz = static_cast<int>(std::floor(worldZ / CHUNK_SIZE));
        int r2 = radiusChunks * radiusChunks;

        for (int dz = -radiusChunks; dz <= radiusChunks; ++dz)
        for (int dx = -radiusChunks; dx <= radiusChunks; ++dx) {
            if (dx * dx + dz * dz > r2) continue;
            ChunkPos cx = pcx + dx, cz = pcz + dz;
            ChunkKey k{cx, cz};
            if (chunks.count(k)) continue;
            if (!saveDir_.empty()) loadRegionIfNeeded(cx, cz);
            if (chunks.count(k)) continue;

            float ddx = static_cast<float>(dx), ddz = static_cast<float>(dz);
            pool.request(std::make_shared<Chunk>(cx, cz), ddx * ddx + ddz * ddz);
        }
    }

    // Выгружает чанки, не попадающие в радиус НИ ОДНОГО из переданных
    // игроков. Вызывать редко (раз в несколько секунд с полным списком
    // позиций всех подключённых игроков) — это дорогой O(chunks) проход.
    void evictFarFromAllViewers(const std::vector<std::pair<float, float>>& viewerWorldXZ,
                                 int keepRadiusChunks) {
        int r2 = keepRadiusChunks * keepRadiusChunks;
        auto keepsChunk = [&](ChunkPos cx, ChunkPos cz) {
            for (auto& [wx, wz] : viewerWorldXZ) {
                int pcx = static_cast<int>(std::floor(wx / CHUNK_SIZE));
                int pcz = static_cast<int>(std::floor(wz / CHUNK_SIZE));
                int dx = cx - pcx, dz = cz - pcz;
                if (dx * dx + dz * dz <= r2) return true;
            }
            return false;
        };

        std::unordered_set<uint64_t> regionsLosingChunks;
        for (auto& [key, chunk] : chunks) {
            if (!keepsChunk(key.x, key.z)) regionsLosingChunks.insert(regionKey(key.x, key.z));
        }
        if (!saveDir_.empty()) for (uint64_t rk : regionsLosingChunks) saveRegion(rk);

        for (auto it = chunks.begin(); it != chunks.end(); ) {
            if (!keepsChunk(it->first.x, it->first.z)) it = chunks.erase(it);
            else ++it;
        }
    }

    size_t loadedCount() const { return chunks.size(); }
    size_t pendingCount() const { return pool.pendingCount(); }
    size_t inFlightCount() const { return pool.inFlightCount(); }

    const std::unordered_map<ChunkKey, ChunkPtr, ChunkKeyHash>& loaded() const {
        return chunks;
    }

    int groundHeight(int wx, int wz) { return generatorHeight(wx, wz); }

private:
    static ChunkPos floorDiv(int a, int b) {
        int q = a / b;
        if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
        return q;
    }

    void markNeedsRemesh(ChunkPos cx, ChunkPos cz) {
        auto it = chunks.find(ChunkKey{cx, cz});
        if (it != chunks.end()) it->second->markNeedsRemesh();
    }

    void evictFarChunks(int pcx, int pcz, int keepRadius) {
        int r2 = keepRadius * keepRadius;

        std::unordered_set<uint64_t> regionsLosingChunks;
        for (auto& [key, chunk] : chunks) {
            int dx = key.x - pcx, dz = key.z - pcz;
            if (dx * dx + dz * dz > r2) regionsLosingChunks.insert(regionKey(key.x, key.z));
        }
        if (!saveDir_.empty()) {
            for (uint64_t rk : regionsLosingChunks) saveRegion(rk);
        }

        for (auto it = chunks.begin(); it != chunks.end(); ) {
            int dx = it->first.x - pcx, dz = it->first.z - pcz;
            if (dx * dx + dz * dz > r2) it = chunks.erase(it);
            else ++it;
        }

        // FIX: забыть регионы, из которых выгрузили чанки — иначе при
        // возврате loadRegionIfNeeded решит, что регион уже загружен,
        // и чанки сгенерируются заново (потеряв правки), а не
        // загрузятся с диска.
        if (!saveDir_.empty()) {
            for (uint64_t rk : regionsLosingChunks) loadedRegions.erase(rk);
        }
    }

    static ChunkPos regionCoord(ChunkPos c) {
        return (c >= 0) ? c / REGION_SIZE : (c - REGION_SIZE + 1) / REGION_SIZE;
    }

    static uint64_t regionKey(ChunkPos cx, ChunkPos cz) {
        ChunkPos rx = regionCoord(cx), rz = regionCoord(cz);
        return (static_cast<uint64_t>(static_cast<uint32_t>(rx)) << 32)
             |  static_cast<uint32_t>(rz);
    }

    void loadRegionIfNeeded(ChunkPos cx, ChunkPos cz) {
        uint64_t rk = regionKey(cx, cz);
        if (loadedRegions.count(rk)) return;
        loadedRegions.insert(rk);

        ChunkPos rx = regionCoord(cx), rz = regionCoord(cz);
        std::vector<ChunkPtr> loaded;
        if (RegionFile::load(saveDir_, rx, rz, loaded)) {
            for (auto& c : loaded) {
                ChunkKey k{c->chunkX(), c->chunkZ()};
                // Не затираем чанк, если он почему-то уже в памяти
                // (на практике не должно случаться — loadedRegions страхует).
                chunks.emplace(k, std::move(c));
            }
        }
        // Если файла региона нет — это нормально, регион ещё не был
        // сгенерирован/сохранён. Ниже чанк уйдёт на обычную генерацию.
    }

    // Чанк может быть в середине региона, даже если сам регион не целиком
    // прогружен в память (мы не держим "пустые" соседние чанки). Собираем
    // всё, что сейчас есть в `chunks` для данного региона, и перезаписываем
    // файл региона целиком.
    void saveRegion(uint64_t rk) {
        std::vector<ChunkPtr> inRegion;
        ChunkPos anyX = 0, anyZ = 0;
        bool haveAny = false;
        for (auto& [key, chunk] : chunks) {
            if (regionKey(key.x, key.z) != rk) continue;
            inRegion.push_back(chunk);
            if (!haveAny) { anyX = key.x; anyZ = key.z; haveAny = true; }
        }
        if (!haveAny) return;
        RegionFile::save(saveDir_, inRegion, anyX, anyZ);
    }

    // Для groundHeight — не держим отдельный TerrainGenerator в World,
    // чтобы не гонять два экземпляра. Но heightAt нужен в Player::spawn...
    // Здесь используем пул-генератор через временный объект (редкий вызов).
    int generatorHeight(int wx, int wz) {
        // Локальный TerrainGenerator — только для точечных вызовов
        // (спавн игрока, отладка). Не участвует в основном пайплайне.
        static thread_local TerrainGenerator tmp(seed_);
        return tmp.heightAt(wx, wz);
    }

    std::unordered_map<ChunkKey, ChunkPtr, ChunkKeyHash> chunks;
    ChunkWorkerPool pool;
    uint32_t seed_;
    std::string saveDir_;
    std::unordered_set<uint64_t> loadedRegions;
    bool networkOnly_ = false; // true на клиенте мультиплеера — см. setNetworkOnly()
    int evictCounter = 0;
    int autosaveCounter = 0;
    // 60 тиков ~ 1 сек (см. вызов updateStreaming раз в кадр в main.cpp
    // при ~60 fps), так что 1800 ~ раз в 30 секунд.
    static constexpr int AUTOSAVE_INTERVAL_TICKS = 1800;
};

} // namespace mc
