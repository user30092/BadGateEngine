#pragma once
#include "../core/types.hpp"
#include "../core/log.hpp"
#include <cstdint>
#include <cstdio>
#include <string>
#include <filesystem>
#include <system_error>

namespace mc {

// FNV-1a 64-bit. Deterministic: same name → same UUID → same player across
// sessions and across client/server. This is the fix for "same nickname after
// reconnect was treated as a different player" — the network `playerId` is
// per-session, but the persistent identity is the UUID.
inline uint64_t uuidFromName(const std::string& name) {
    uint64_t h = 1469598103934665603ULL;   // FNV offset basis
    for (unsigned char c : name) {
        h ^= c;
        h *= 1099511628211ULL;             // FNV prime
    }
    return h;
}

inline std::string uuidToHex(uint64_t uuid) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(uuid));
    return buf;
}

struct SavedPlayer {
    uint64_t    uuid = 0;
    std::string name;
    float       x = 0, y = 64, z = 0;
    float       yaw = 0, pitch = 0;
    int64_t     lastSeenUnix = 0;
    int         hotbarSlot = 1;
};

inline std::string playerDirPath(const std::string& worldDir) {
    return worldDir + "/players";
}

inline std::string playerFilePath(const std::string& worldDir, uint64_t uuid) {
    return playerDirPath(worldDir) + "/" + uuidToHex(uuid) + ".dat";
}

inline bool ensurePlayerDir(const std::string& worldDir) {
    std::error_code ec;
    std::filesystem::create_directories(playerDirPath(worldDir), ec);
    return !ec;
}

inline bool savePlayer(const std::string& worldDir, const SavedPlayer& p) {
    if (!ensurePlayerDir(worldDir)) return false;
    std::string path = playerFilePath(worldDir, p.uuid);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { LOGE("SavedPlayer: cannot open %s for write", path.c_str()); return false; }

    uint32_t magic = 0x504C5952;   // "PLYR"
    uint32_t version = 1;
    std::fwrite(&magic, sizeof(magic), 1, f);
    std::fwrite(&version, sizeof(version), 1, f);
    std::fwrite(&p.uuid, sizeof(p.uuid), 1, f);

    uint32_t nameLen = static_cast<uint32_t>(p.name.size());
    std::fwrite(&nameLen, sizeof(nameLen), 1, f);
    if (nameLen) std::fwrite(p.name.data(), 1, nameLen, f);

    std::fwrite(&p.x, sizeof(p.x), 1, f);
    std::fwrite(&p.y, sizeof(p.y), 1, f);
    std::fwrite(&p.z, sizeof(p.z), 1, f);
    std::fwrite(&p.yaw, sizeof(p.yaw), 1, f);
    std::fwrite(&p.pitch, sizeof(p.pitch), 1, f);
    std::fwrite(&p.lastSeenUnix, sizeof(p.lastSeenUnix), 1, f);
    std::fwrite(&p.hotbarSlot, sizeof(p.hotbarSlot), 1, f);

    std::fclose(f);
    return true;
}

inline bool loadPlayer(const std::string& worldDir, uint64_t uuid, SavedPlayer& out) {
    std::string path = playerFilePath(worldDir, uuid);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    uint32_t magic = 0, version = 0;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x504C5952) {
        std::fclose(f); return false;
    }
    if (std::fread(&version, sizeof(version), 1, f) != 1 || version != 1) {
        std::fclose(f); return false;
    }

    uint64_t savedUuid = 0;
    if (std::fread(&savedUuid, sizeof(savedUuid), 1, f) != 1 || savedUuid != uuid) {
        std::fclose(f); return false;   // file is for another player — treat as not found
    }
    out.uuid = savedUuid;

    uint32_t nameLen = 0;
    if (std::fread(&nameLen, sizeof(nameLen), 1, f) != 1 || nameLen > 4096) {
        std::fclose(f); return false;
    }
    out.name.resize(nameLen);
    if (nameLen && std::fread(&out.name[0], 1, nameLen, f) != nameLen) {
        std::fclose(f); return false;
    }

    bool ok = std::fread(&out.x, sizeof(out.x), 1, f) == 1;
    ok = ok && std::fread(&out.y, sizeof(out.y), 1, f) == 1;
    ok = ok && std::fread(&out.z, sizeof(out.z), 1, f) == 1;
    ok = ok && std::fread(&out.yaw, sizeof(out.yaw), 1, f) == 1;
    ok = ok && std::fread(&out.pitch, sizeof(out.pitch), 1, f) == 1;
    ok = ok && std::fread(&out.lastSeenUnix, sizeof(out.lastSeenUnix), 1, f) == 1;

    // hotbarSlot добавлен в SavedPlayer v1-extension: старые файлы его
    // не содержат, читаем опционально. Если EOF — дефолт (слот 1).
    int hs = 1;
    if (std::fread(&hs, sizeof(hs), 1, f) == 1 && hs >= 0 && hs < 5)
        out.hotbarSlot = hs;
    else
        out.hotbarSlot = 1;

    std::fclose(f);
    return ok;
}

// player.dat in the world root — records which UUID "owns" this world.
// Singleplayer: the local player. When listing worlds we can show "owner: X"
// without loading full player state.
struct WorldOwner {
    uint64_t uuid = 0;
    bool isValid() const { return uuid != 0; }
};

inline bool saveWorldOwner(const std::string& worldDir, const WorldOwner& o) {
    std::string path = worldDir + "/player.dat";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;

    uint32_t magic = 0x4F574E45;   // "OWNE"
    uint32_t version = 1;
    std::fwrite(&magic, sizeof(magic), 1, f);
    std::fwrite(&version, sizeof(version), 1, f);
    std::fwrite(&o.uuid, sizeof(o.uuid), 1, f);

    std::fclose(f);
    return true;
}

inline bool loadWorldOwner(const std::string& worldDir, WorldOwner& out) {
    std::string path = worldDir + "/player.dat";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;

    uint32_t magic = 0, version = 0;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 || magic != 0x4F574E45 ||
        std::fread(&version, sizeof(version), 1, f) != 1 || version != 1) {
        std::fclose(f); return false;
    }

    bool ok = std::fread(&out.uuid, sizeof(out.uuid), 1, f) == 1;
    std::fclose(f);
    return ok && out.uuid != 0;
}

} // namespace mc
