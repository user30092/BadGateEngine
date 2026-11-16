#pragma once
#include "packet.hpp"
#include "../world/chunk.hpp"
#include <string>
#include <cstring>
#include <memory>

namespace mc {

// ---- Handshake ----
// Клиент -> сервер: имя игрока.
// Сервер -> клиент: присвоенный id + точка спавна.

inline std::vector<uint8_t> writeHandshakeRequest(const std::string& name) {
    PacketWriter w;
    w.writeVarInt(static_cast<uint32_t>(name.size()));
    w.writeBytes(name.data(), name.size());
    return w.data();
}

inline std::string readHandshakeRequest(const std::vector<uint8_t>& payload) {
    PacketReader r(payload.data(), payload.size());
    uint32_t len = r.readVarInt();
    std::string name;
    for (uint32_t i = 0; i < len && !r.eof(); ++i) name.push_back(static_cast<char>(r.readByte()));
    return name;
}

struct HandshakeAck {
    uint32_t playerId = 0;
    float x = 0, y = 64, z = 0;
};

inline std::vector<uint8_t> writeHandshakeAck(const HandshakeAck& a) {
    PacketWriter w;
    w.writeU32(a.playerId);
    w.writeFloat(a.x); w.writeFloat(a.y); w.writeFloat(a.z);
    return w.data();
}

inline HandshakeAck readHandshakeAck(const std::vector<uint8_t>& payload) {
    PacketReader r(payload.data(), payload.size());
    HandshakeAck a;
    a.playerId = r.readU32();
    a.x = r.readFloat(); a.y = r.readFloat(); a.z = r.readFloat();
    return a;
}

// ---- PlayerMove / состояние игрока ----
// Один и тот же формат используется и когда клиент шлёт свою позицию
// серверу, и когда сервер рассылает позиции других игроков клиентам.

struct PlayerState {
    uint32_t playerId = 0;
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0;
};

inline std::vector<uint8_t> writePlayerState(const PlayerState& p) {
    PacketWriter w;
    w.writeU32(p.playerId);
    w.writeFloat(p.x); w.writeFloat(p.y); w.writeFloat(p.z);
    w.writeFloat(p.yaw); w.writeFloat(p.pitch);
    return w.data();
}

inline PlayerState readPlayerState(const std::vector<uint8_t>& payload) {
    PacketReader r(payload.data(), payload.size());
    PlayerState p;
    p.playerId = r.readU32();
    p.x = r.readFloat(); p.y = r.readFloat(); p.z = r.readFloat();
    p.yaw = r.readFloat(); p.pitch = r.readFloat();
    return p;
}

// ---- BlockUpdate ----

struct BlockUpdateMsg {
    int32_t x = 0, y = 0, z = 0;
    uint8_t blockId = 0;
};

inline std::vector<uint8_t> writeBlockUpdate(const BlockUpdateMsg& b) {
    PacketWriter w;
    w.writeI32(b.x); w.writeI32(b.y); w.writeI32(b.z);
    w.writeByte(b.blockId);
    return w.data();
}

inline BlockUpdateMsg readBlockUpdate(const std::vector<uint8_t>& payload) {
    PacketReader r(payload.data(), payload.size());
    BlockUpdateMsg b;
    b.x = r.readI32(); b.y = r.readI32(); b.z = r.readI32();
    b.blockId = r.readByte();
    return b;
}

// ---- PlayerLeave ----

inline std::vector<uint8_t> writePlayerLeave(uint32_t playerId) {
    PacketWriter w; w.writeU32(playerId); return w.data();
}
inline uint32_t readPlayerLeave(const std::vector<uint8_t>& payload) {
    PacketReader r(payload.data(), payload.size());
    return r.readU32();
}

// ---- ChatMessage ----

inline std::vector<uint8_t> writeChatMessage(const std::string& text) {
    PacketWriter w;
    w.writeVarInt(static_cast<uint32_t>(text.size()));
    w.writeBytes(text.data(), text.size());
    return w.data();
}
inline std::string readChatMessage(const std::vector<uint8_t>& payload) {
    PacketReader r(payload.data(), payload.size());
    uint32_t len = r.readVarInt();
    std::string s;
    for (uint32_t i = 0; i < len && !r.eof(); ++i) s.push_back(static_cast<char>(r.readByte()));
    return s;
}

// ---- ChunkData ----
// Тот же RLE-формат, что и в world_save.hpp::ChunkIO, но пишется/читается
// прямо в/из байтового буфера (payload пакета), а не в файл. Формат
// намеренно идентичен файловому — если понадобится, один и тот же RLE-блок
// можно переиспользовать и для сети, и для сохранения на диск.

inline std::vector<uint8_t> serializeChunk(const Chunk& chunk) {
    std::vector<uint8_t> out;
    out.reserve(4096);

    int32_t cx = chunk.chunkX(), cz = chunk.chunkZ();
    uint8_t cxBytes[4], czBytes[4];
    std::memcpy(cxBytes, &cx, 4);
    std::memcpy(czBytes, &cz, 4);
    out.insert(out.end(), cxBytes, cxBytes + 4);
    out.insert(out.end(), czBytes, czBytes + 4);

    const BlockId* data = chunk.rawData();
    size_t i = 0;
    while (i < CHUNK_VOLUME) {
        BlockId id = data[i];
        size_t start = i;
        while (i < CHUNK_VOLUME && data[i] == id && (i - start) < 0xFFFF) ++i;
        uint16_t count = static_cast<uint16_t>(i - start);
        out.push_back(static_cast<uint8_t>(count & 0xFF));
        out.push_back(static_cast<uint8_t>(count >> 8));
        out.push_back(id);
    }
    return out;
}

// Возвращает nullptr, если payload битый/слишком короткий.
inline ChunkPtr deserializeChunk(const std::vector<uint8_t>& payload) {
    if (payload.size() < 8) return nullptr;
    int32_t cx, cz;
    std::memcpy(&cx, payload.data(), 4);
    std::memcpy(&cz, payload.data() + 4, 4);

    auto chunk = std::make_shared<Chunk>(cx, cz);
    BlockId* out = chunk->rawData();
    size_t outPos = 0;
    size_t p = 8;
    while (p + 3 <= payload.size() && outPos < CHUNK_VOLUME) {
        uint16_t count = static_cast<uint16_t>(payload[p]) |
                         (static_cast<uint16_t>(payload[p + 1]) << 8);
        uint8_t id = payload[p + 2];
        p += 3;
        if (outPos + count > CHUNK_VOLUME) count = static_cast<uint16_t>(CHUNK_VOLUME - outPos);
        std::memset(out + outPos, id, count);
        outPos += count;
    }
    return chunk;
}

} // namespace mc
