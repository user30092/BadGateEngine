#pragma once
#include "chunk.hpp"
#include "../core/types.hpp"
#include "../core/log.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace mc {

// Метаданные мира — лежат в saves/<name>/world.meta.
// Отдельно от чанков, чтобы список миров в UI читать быстро,
// не разбирая гигабайты чанковых файлов.
struct WorldMeta {
    std::string name;
    uint32_t    seed = 0;
    std::string salt;
    int64_t     createdUnix = 0;
    int64_t     lastPlayedUnix = 0;

    static constexpr uint32_t MAGIC   = 0x4D455441;   // "META"
    static constexpr uint32_t VERSION = 3;

    bool save(const std::string& dir) const {
        std::string path = dir + "/world.meta";
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return false;

        auto writeStr = [&](const std::string& s) {
            uint32_t len = static_cast<uint32_t>(s.size());
            std::fwrite(&len, sizeof(len), 1, f);
            if (len) std::fwrite(s.data(), 1, len, f);
        };

        std::fwrite(&MAGIC,   sizeof(MAGIC),   1, f);
        std::fwrite(&VERSION, sizeof(VERSION), 1, f);
        writeStr(name);
        std::fwrite(&seed, sizeof(seed), 1, f);
        writeStr(salt);
        std::fwrite(&createdUnix, sizeof(createdUnix), 1, f);
        std::fwrite(&lastPlayedUnix, sizeof(lastPlayedUnix), 1, f);

        std::fclose(f);
        return true;
    }

    static bool load(const std::string& dir, WorldMeta& out) {
        std::string path = dir + "/world.meta";
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;

        auto readStr = [&](std::string& s) -> bool {
            uint32_t len = 0;
            if (std::fread(&len, sizeof(len), 1, f) != 1) return false;
            if (len > (16u << 20)) return false;
            s.resize(len);
            if (len && std::fread(&s[0], 1, len, f) != len) return false;
            return true;
        };

        uint32_t magic = 0, version = 0;
        bool ok = std::fread(&magic, sizeof(magic), 1, f) == 1 && magic == MAGIC;
        ok = ok && std::fread(&version, sizeof(version), 1, f) == 1;
        if (!ok || version < 1 || version > VERSION) { std::fclose(f); return false; }

        ok = ok && readStr(out.name);
        ok = ok && std::fread(&out.seed, sizeof(out.seed), 1, f) == 1;
        ok = ok && readStr(out.salt);
        ok = ok && std::fread(&out.createdUnix, sizeof(out.createdUnix), 1, f) == 1;
        ok = ok && std::fread(&out.lastPlayedUnix, sizeof(out.lastPlayedUnix), 1, f) == 1;

        // v2 had inline player fields (u32 hasPlayer + 5×f32 = 24 bytes).
        // They're deprecated — player state now lives in players/<uuid>.dat.
        // Skip them on read to keep old saves readable.
        if (version == 2) {
            uint8_t legacy[24];
            ok = ok && std::fread(legacy, 1, sizeof(legacy), f) == sizeof(legacy);
        }

        std::fclose(f);
        return ok;
    }
};

// Сериализация одного чанка в формате простого RLE:
// повторяющиеся подряд BlockId кодируются как (count:u16, id:u8).
class ChunkIO {
public:
    static void writeChunk(FILE* f, const Chunk& chunk) {
        int32_t cx = chunk.chunkX();
        int32_t cz = chunk.chunkZ();
        std::fwrite(&cx, sizeof(cx), 1, f);
        std::fwrite(&cz, sizeof(cz), 1, f);

        const BlockId* data = chunk.rawData();
        std::vector<uint8_t> rle;
        rle.reserve(4096);

        size_t i = 0;
        while (i < CHUNK_VOLUME) {
            BlockId id = data[i];
            size_t runStart = i;
            while (i < CHUNK_VOLUME && data[i] == id &&
                   (i - runStart) < 0xFFFF) {
                ++i;
            }
            uint16_t count = static_cast<uint16_t>(i - runStart);
            rle.push_back(static_cast<uint8_t>(count & 0xFF));
            rle.push_back(static_cast<uint8_t>(count >> 8));
            rle.push_back(id);
        }

        uint32_t rleSize = static_cast<uint32_t>(rle.size());
        std::fwrite(&rleSize, sizeof(rleSize), 1, f);
        std::fwrite(rle.data(), 1, rle.size(), f);
    }

    static ChunkPtr readChunk(FILE* f) {
        int32_t cx, cz;
        if (std::fread(&cx, sizeof(cx), 1, f) != 1) return nullptr;
        if (std::fread(&cz, sizeof(cz), 1, f) != 1) return nullptr;

        uint32_t rleSize = 0;
        if (std::fread(&rleSize, sizeof(rleSize), 1, f) != 1) return nullptr;
        if (rleSize > (64u << 20)) return nullptr;

        std::vector<uint8_t> rle(rleSize);
        if (rleSize && std::fread(rle.data(), 1, rleSize, f) != rleSize) return nullptr;

        auto chunk = std::make_shared<Chunk>(cx, cz);
        BlockId* out = chunk->rawData();
        size_t outPos = 0;
        for (size_t p = 0; p + 3 <= rle.size(); p += 3) {
            uint16_t count = static_cast<uint16_t>(rle[p]) |
                             (static_cast<uint16_t>(rle[p + 1]) << 8);
            uint8_t id = rle[p + 2];
            if (outPos + count > CHUNK_VOLUME) count = static_cast<uint16_t>(CHUNK_VOLUME - outPos);
            std::memset(out + outPos, id, count);
            outPos += count;
            if (outPos >= CHUNK_VOLUME) break;
        }
        return chunk;
    }
};

constexpr int REGION_SIZE = 16;

inline std::string regionFileName(ChunkPos cx, ChunkPos cz) {
    ChunkPos rx = (cx >= 0) ? cx / REGION_SIZE : (cx - REGION_SIZE + 1) / REGION_SIZE;
    ChunkPos rz = (cz >= 0) ? cz / REGION_SIZE : (cz - REGION_SIZE + 1) / REGION_SIZE;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "r.%d.%d.dat", rx, rz);
    return buf;
}

// Идемпотентно: true, если папка существует после вызова (неважно, кем создана).
// std::filesystem::create_directories сам по себе идемпотентен, но возвращает
// false, если папка уже была, поэтому комбинируем с is_directory.
inline bool ensureDir(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) return false;
    return std::filesystem::is_directory(path, ec);
}

class RegionFile {
public:
    static bool save(const std::string& worldDir,
                      const std::vector<ChunkPtr>& chunksInRegion,
                      ChunkPos anyX, ChunkPos anyZ) {
        if (chunksInRegion.empty()) return true;
        std::string path = worldDir + "/" + regionFileName(anyX, anyZ);
        std::string tmpPath = path + ".tmp";

        FILE* f = std::fopen(tmpPath.c_str(), "wb");
        if (!f) { LOGE("RegionFile: cannot open %s for write", tmpPath.c_str()); return false; }

        uint32_t magic = 0x52474E31; // "RGN1"
        uint32_t count = static_cast<uint32_t>(chunksInRegion.size());
        std::fwrite(&magic, sizeof(magic), 1, f);
        std::fwrite(&count, sizeof(count), 1, f);
        for (auto& c : chunksInRegion) ChunkIO::writeChunk(f, *c);
        std::fclose(f);

        // std::remove (cstdio) — файл уже закрыт, на Windows это работает.
        // std::filesystem::remove тоже подошёл бы, но не тащим его сюда зря.
        std::remove(path.c_str());
        return std::rename(tmpPath.c_str(), path.c_str()) == 0;
    }

    static bool load(const std::string& worldDir, ChunkPos rx, ChunkPos rz,
                      std::vector<ChunkPtr>& out) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "r.%d.%d.dat", rx, rz);
        std::string path = worldDir + "/" + buf;

        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) return false;

        uint32_t magic = 0, count = 0;
        bool ok = std::fread(&magic, sizeof(magic), 1, f) == 1 && magic == 0x52474E31;
        ok = ok && std::fread(&count, sizeof(count), 1, f) == 1;
        if (ok) {
            for (uint32_t i = 0; i < count; ++i) {
                ChunkPtr c = ChunkIO::readChunk(f);
                if (!c) { ok = false; break; }
                out.push_back(std::move(c));
            }
        }
        std::fclose(f);
        return ok;
    }
};

// Список миров: обходим saves/ и читаем meta каждого подкаталога.
// std::filesystem с error_code-перегрузками — без исключений, совместимо
// с -fno-exceptions и -fno-rtti.
inline std::vector<WorldMeta> listSavedWorlds(const std::string& savesRoot) {
    std::vector<WorldMeta> result;

    std::error_code ec;
    std::filesystem::directory_iterator it(savesRoot, ec);
    if (ec) return result;

    const std::filesystem::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const auto& entry = *it;

        std::error_code itemEc;
        if (!entry.is_directory(itemEc) || itemEc) continue;

        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.') continue;

        WorldMeta meta;
        if (WorldMeta::load(entry.path().string(), meta))
            result.push_back(std::move(meta));
    }
    return result;
}

// Удаляет папку мира целиком, со всем содержимым.
// std::filesystem::remove_all рекурсивен — в отличие от старой реализации
// через dirent, которая удаляла только файлы (и падала, если внутри
// оказывался подкаталог). Для кнопки «Delete World» нужен именно этот
// вариант: пользователь ждёт, что мир исчезнет целиком.
inline bool deleteWorld(const std::string& savesRoot, const std::string& worldName) {
    std::error_code ec;
    std::filesystem::remove_all(savesRoot + "/" + worldName, ec);
    return !ec;
}

} // namespace mc
