#pragma once
#include "../net/socket.hpp"
#include "../net/packet.hpp"
#include "../net/protocol.hpp"
#include "../world/world.hpp"
#include "../world/block.hpp"
#include "../world/player_save.hpp"
#include "../core/math.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <cmath>
#include <ctime>

namespace mc {

// Авторитетная модель: ЕДИНСТВЕННЫЙ "тик-поток" владеет World и состоянием
// всех игроков — только он вызывает world.setBlock/requestChunksNear/
// evictFarFromAllViewers и шлёт пакеты. Потоки клиентских соединений
// только читают сокет и складывают разобранные пакеты в per-client очередь
// (под мьютексом) — никакого доступа к World из них.
//
// Идентификация игрока:
//   - connId — уникален per-connection, используется как ключ в clients map.
//   - uuid   — hash(playerName), стабилен между сессиями. Из него же
//              получается playerId (uint32_t) для сетевых пакетов.
//   - playerId = (uint32_t)uuid — стабилен per-name. Именно это видит
//     клиент в PlayerMove/PlayerLeave — поэтому при реконнекте под тем же
//     ником для других клиентов это тот же игрок.
class NetworkServer {
public:
    NetworkServer(uint32_t worldSeed, std::string saveDir, int renderDistanceChunks = 6)
        : world(worldSeed, /*workerThreads=*/0, saveDir),
          viewRadius(renderDistanceChunks),
          saveDir_(std::move(saveDir)) {}

    ~NetworkServer() { stop(); }

    bool start(uint16_t port) {
        if (!listener.listen(port)) return false;
        running = true;
        acceptThread = std::thread([this] { acceptLoop(); });
        tickThread   = std::thread([this] { tickLoop(); });
        LOGI("NetworkServer: listening on port %u", static_cast<unsigned>(port));
        return true;
    }

    void stop() {
        bool expected = true;
        if (!running.compare_exchange_strong(expected, false)) return;
        listener.close();
        if (acceptThread.joinable()) acceptThread.join();
        if (tickThread.joinable())   tickThread.join();

        std::lock_guard<std::mutex> lk(clientsMutex);
        for (auto& [connId, c] : clients) {
            c->connected = false;
            c->socket.disconnect();
            if (c->recvThread.joinable()) c->recvThread.join();
            saveClientState(*c);
        }
        clients.clear();
        world.shutdown();
    }

private:
    struct IncomingPacket { PacketType type; std::vector<uint8_t> payload; };

    struct ClientConnection {
        uint32_t connId   = 0;   // unique per connection, map key
        uint32_t playerId = 0;   // = (uint32_t)uuid, stable per-name, network-visible
        uint64_t uuid     = 0;   // FNV-1a hash of name

        TcpSocket socket;
        std::thread recvThread;
        std::atomic<bool> connected{true};
        std::string name;
        bool handshakeDone = false;

        float x = 0, y = 64, z = 0, yaw = 0, pitch = 0;

        std::mutex incomingMutex;
        std::vector<IncomingPacket> incoming;

        std::unordered_set<uint64_t> sentChunks;
    };

    // ------------------------------------------------------------------------
    // accept thread — только accept() + спавн recv-потока
    // ------------------------------------------------------------------------

    void acceptLoop() {
        while (running) {
            SocketHandle fd = listener.acceptRaw();
            if (fd == INVALID_SOCKET_HANDLE) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                continue;
            }

            auto conn = std::make_shared<ClientConnection>();
            conn->socket = TcpSocket(fd);
            conn->socket.setNonBlocking(true);
            conn->connId = nextConnId.fetch_add(1);

            {
                std::lock_guard<std::mutex> lk(clientsMutex);
                clients[conn->connId] = conn;
            }
            conn->recvThread = std::thread([this, conn] { clientRecvLoop(conn); });
            LOGI("NetworkServer: connection #%u accepted", conn->connId);
        }
    }

    void clientRecvLoop(std::shared_ptr<ClientConnection> conn) {
        std::vector<uint8_t> buf(8192);
        PacketAssembler assembler;

        while (running && conn->connected) {
            int n = conn->socket.recv(buf.data(), buf.size());
            if (n > 0) {
                assembler.feed(buf.data(), static_cast<size_t>(n));
                PacketType t; std::vector<uint8_t> payload;
                while (assembler.tryPop(t, payload)) {
                    std::lock_guard<std::mutex> lk(conn->incomingMutex);
                    conn->incoming.push_back({t, std::move(payload)});
                }
                if (assembler.isDesynced()) break;
            } else if (n == 0) {
                break;
            } else {
                if (!conn->socket.isConnected() || conn->socket.lastErrorWasFatal()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        conn->connected = false;
    }

    // ------------------------------------------------------------------------
    // send helpers (тик-поток — единственный писатель в сокеты)
    // ------------------------------------------------------------------------

    static bool sendAll(TcpSocket& s, const void* data, size_t n) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        size_t sent = 0;
        while (sent < n) {
            int rc = s.send(p + sent, n - sent);
            if (rc > 0) { sent += static_cast<size_t>(rc); continue; }
            if (rc == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (!s.isConnected() || s.lastErrorWasFatal()) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    void sendTo(ClientConnection& c, PacketType type, const std::vector<uint8_t>& payload) {
        PacketHeader h{static_cast<uint8_t>(type), static_cast<uint32_t>(payload.size()), 0};
        sendAll(c.socket, &h, sizeof(h));
        if (!payload.empty()) sendAll(c.socket, payload.data(), payload.size());
    }

    // exceptPlayerId — не слать этому игроку (обычно автору изменения).
    // Сравниваем по playerId (стабильному), а не по connId.
    void broadcast(PacketType type, const std::vector<uint8_t>& payload,
                   uint32_t exceptPlayerId = 0) {
        for (auto& [connId, c] : clients) {
            if (!c->connected || !c->handshakeDone) continue;
            if (c->playerId == exceptPlayerId) continue;
            sendTo(*c, type, payload);
        }
    }

    // ------------------------------------------------------------------------
    // persistence
    // ------------------------------------------------------------------------

    void saveClientState(const ClientConnection& c) {
        if (!c.handshakeDone || c.uuid == 0) return;
        SavedPlayer s;
        s.uuid    = c.uuid;
        s.name    = c.name;
        s.x       = c.x;
        s.y       = c.y;
        s.z       = c.z;
        s.yaw     = c.yaw;
        s.pitch   = c.pitch;
        s.lastSeenUnix = static_cast<int64_t>(std::time(nullptr));
        savePlayer(saveDir_, s);
    }

    // ------------------------------------------------------------------------
    // tick thread — единственный, кто трогает World
    // ------------------------------------------------------------------------

    void tickLoop() {
        using clock = std::chrono::steady_clock;
        auto lastPeriodic = clock::now();

        while (running) {
            std::vector<std::shared_ptr<ClientConnection>> snapshot;
            {
                std::lock_guard<std::mutex> lk(clientsMutex);
                for (auto it = clients.begin(); it != clients.end(); ) {
                    if (!it->second->connected) {
                        if (it->second->recvThread.joinable()) it->second->recvThread.join();
                        auto c = it->second;

                        if (c->handshakeDone) {
                            saveClientState(*c);
                            LOGI("NetworkServer: '%s' (playerId=%u, uuid=%016llx) saved",
                                 c->name.c_str(), c->playerId,
                                 static_cast<unsigned long long>(c->uuid));
                            broadcast(PacketType::PlayerLeave, writePlayerLeave(c->playerId));
                        }
                        it = clients.erase(it);
                    } else {
                        snapshot.push_back(it->second);
                        ++it;
                    }
                }
            }

            for (auto& conn : snapshot) processIncoming(*conn);

            for (auto& conn : snapshot) {
                if (!conn->handshakeDone) continue;
                streamChunksTo(*conn);
            }

            if (clock::now() - lastPeriodic > std::chrono::seconds(5)) {
                lastPeriodic = clock::now();

                // Периодическое сохранение позиций всех игроков.
                for (auto& conn : snapshot) saveClientState(*conn);

                // Эвикция чанков, не нужных ни одному игроку.
                std::vector<std::pair<float, float>> positions;
                for (auto& conn : snapshot)
                    if (conn->handshakeDone) positions.emplace_back(conn->x, conn->z);
                if (!positions.empty())
                    world.evictFarFromAllViewers(positions, viewRadius + 3);

                // Забыть выгруженные чанки из sentChunks, чтобы дослать снова
                // при возврате игрока в этот район.
                for (auto& conn : snapshot) {
                    for (auto it = conn->sentChunks.begin(); it != conn->sentChunks.end(); ) {
                        if (!world.loaded().count(unpackChunkKey(*it))) it = conn->sentChunks.erase(it);
                        else ++it;
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    static uint64_t packChunkKey(ChunkPos cx, ChunkPos cz) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32)
             |  static_cast<uint32_t>(cz);
    }
    static ChunkKey unpackChunkKey(uint64_t k) {
        return ChunkKey{static_cast<ChunkPos>(k >> 32),
                        static_cast<ChunkPos>(k & 0xFFFFFFFFu)};
    }

    // ------------------------------------------------------------------------
    // packet processing
    // ------------------------------------------------------------------------

    void processIncoming(ClientConnection& c) {
        std::vector<IncomingPacket> batch;
        {
            std::lock_guard<std::mutex> lk(c.incomingMutex);
            batch.swap(c.incoming);
        }

        for (auto& pkt : batch) {
            switch (pkt.type) {
                case PacketType::Handshake: {
                    if (c.handshakeDone) break;

                    c.name = readHandshakeRequest(pkt.payload);
                    if (c.name.empty()) c.name = "Player";

                    c.uuid     = uuidFromName(c.name);
                    c.playerId = static_cast<uint32_t>(c.uuid);

                    // Отбиваем дубликат ника: если тот же uuid уже online на
                    // другом коннекте — новый отключаем. Иначе оба получат
                    // один playerId и будут визуально слипаться.
                    {
                        std::lock_guard<std::mutex> lk(clientsMutex);
                        for (auto& [otherConnId, other] : clients) {
                            if (other.get() == &c) continue;
                            if (!other->handshakeDone) continue;
                            if (other->uuid == c.uuid && other->connected) {
                                LOGW("NetworkServer: rejecting duplicate name '%s' "
                                     "(uuid=%016llx) from conn #%u — already online on #%u",
                                     c.name.c_str(),
                                     static_cast<unsigned long long>(c.uuid),
                                     c.connId, otherConnId);
                                c.socket.disconnect();
                                c.connected = false;
                                return;
                            }
                        }
                    }

                    // Загружаем сохранённое состояние игрока (позиция, yaw/pitch).
                    SavedPlayer saved;
                    bool resumed = loadPlayer(saveDir_, c.uuid, saved);
                    if (resumed) {
                        c.x     = saved.x;
                        c.y     = saved.y;
                        c.z     = saved.z;
                        c.yaw   = std::remainder(saved.yaw, 360.0f);
                        c.pitch = clampf(saved.pitch, -89.0f, 89.0f);
                        LOGI("NetworkServer: '%s' (uuid=%016llx) resumed at %.1f %.1f %.1f",
                             c.name.c_str(),
                             static_cast<unsigned long long>(c.uuid),
                             c.x, c.y, c.z);
                    } else {
                        // Новый игрок: спавн на поверхности. WATER_LEVEL=32,
                        // значит если (0,0) оказалось океаном — поднимаем
                        // на 2 блока над водой.
                        int h = world.groundHeight(0, 0);
                        int spawnY = h + 2;
                        if (spawnY < TerrainGenerator::WATER_LEVEL + 2) {
                            spawnY = TerrainGenerator::WATER_LEVEL + 2;
                        }
                        c.x = 0.5f;
                        c.y = static_cast<float>(spawnY);
                        c.z = 0.5f;
                        c.yaw = 0.0f;
                        c.pitch = 0.0f;
                        LOGI("NetworkServer: '%s' (uuid=%016llx) spawned",
                             c.name.c_str(),
                             static_cast<unsigned long long>(c.uuid));
                    }

                    c.handshakeDone = true;

                    HandshakeAck ack{c.playerId, c.x, c.y, c.z};
                    sendTo(c, PacketType::Handshake, writeHandshakeAck(ack));
                    break;
                }

                case PacketType::PlayerMove: {
                    PlayerState ps = readPlayerState(pkt.payload);
                    c.x     = ps.x;
                    c.y     = ps.y;
                    c.z     = ps.z;
                    c.yaw   = std::remainder(ps.yaw, 360.0f);   // wrap
                    c.pitch = clampf(ps.pitch, -89.0f, 89.0f);  // clamp

                    PlayerState out{c.playerId, c.x, c.y, c.z, c.yaw, c.pitch};
                    broadcast(PacketType::PlayerMove, writePlayerState(out),
                              /*exceptPlayerId=*/c.playerId);
                    break;
                }

                case PacketType::BlockUpdate: {
                    BlockUpdateMsg b = readBlockUpdate(pkt.payload);

                    float dx = static_cast<float>(b.x) + 0.5f - c.x;
                    float dy = static_cast<float>(b.y) + 0.5f - c.y;
                    float dz = static_cast<float>(b.z) + 0.5f - c.z;
                    float distSq = dx * dx + dy * dy + dz * dz;

                    if (distSq > MAX_REACH * MAX_REACH) {
                        LOGW("NetworkServer: playerId=%u BlockUpdate rejected "
                             "(dist=%.1f > %.1f)", c.playerId, std::sqrt(distSq), MAX_REACH);
                        BlockId actual = world.getBlock(b.x, b.y, b.z);
                        sendTo(c, PacketType::BlockUpdate,
                               writeBlockUpdate({b.x, b.y, b.z, actual}));
                        break;
                    }
                    if (b.blockId >= BLOCK_DEF_COUNT) {
                        LOGW("NetworkServer: playerId=%u BlockUpdate rejected "
                             "(bad blockId=%u)", c.playerId, static_cast<unsigned>(b.blockId));
                        break;
                    }

                    world.setBlock(b.x, b.y, b.z, b.blockId);
                    broadcast(PacketType::BlockUpdate, writeBlockUpdate(b));
                    break;
                }

                case PacketType::ChatMessage: {
                    std::string text = readChatMessage(pkt.payload);
                    broadcast(PacketType::ChatMessage,
                              writeChatMessage(c.name + ": " + text));
                    break;
                }

                case PacketType::KeepAlive:
                default: break;
            }
        }
    }

    void streamChunksTo(ClientConnection& c) {
        world.requestChunksNear(c.x, c.z, viewRadius);

        int pcx = static_cast<int>(std::floor(c.x / CHUNK_SIZE));
        int pcz = static_cast<int>(std::floor(c.z / CHUNK_SIZE));
        int r2  = viewRadius * viewRadius;
        int sentThisTick = 0;

        for (auto& [key, chunk] : world.loaded()) {
            if (sentThisTick >= MAX_CHUNKS_PER_TICK) break;
            int dx = key.x - pcx, dz = key.z - pcz;
            if (dx * dx + dz * dz > r2) continue;

            uint64_t packed = packChunkKey(key.x, key.z);
            if (c.sentChunks.count(packed)) continue;

            sendTo(c, PacketType::ChunkData, serializeChunk(*chunk));
            c.sentChunks.insert(packed);
            ++sentThisTick;
        }
    }

    static constexpr int   MAX_CHUNKS_PER_TICK = 8;
    static constexpr float MAX_REACH           = 8.0f;

    World        world;
    int          viewRadius;
    std::string  saveDir_;
    TcpListener  listener;

    std::thread acceptThread, tickThread;
    std::atomic<bool>     running{false};
    std::atomic<uint32_t> nextConnId{1};   // идёт в connId, НЕ в playerId

    std::mutex clientsMutex;
    // key = connId (unique per connection), NOT playerId (stable per name)
    std::unordered_map<uint32_t, std::shared_ptr<ClientConnection>> clients;
};

} // namespace mc
