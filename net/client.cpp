#include "client.hpp"                    // <- самая первая строка
#include "../core/log.hpp"
#include <chrono>
#include <cstring>

namespace mc {

namespace {

bool sendAll(TcpSocket& sock, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t sent = 0;
    while (sent < n) {
        int rc = sock.send(p + sent, n - sent);
        if (rc > 0) {
            sent += static_cast<size_t>(rc);
            continue;
        }
        if (rc == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (!sock.isConnected() || sock.lastErrorWasFatal()) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

} // namespace

NetworkClient::~NetworkClient() { stop(); }

bool NetworkClient::start(const std::string& host, uint16_t port) {
    if (running) return false;
    if (worker.joinable()) worker.join();
    if (!socket.connect(host, port)) return false;
    socket.setNonBlocking(true);
    host_ = host;
    port_ = port;
    connectionLost = false;
    running = true;
    worker = std::thread([this] { threadLoop(); });
    return true;
}

void NetworkClient::stop() {
    if (!running) return;
    running = false;
    socket.disconnect();
    if (worker.joinable()) worker.join();
}

void NetworkClient::sendPacket(PacketType type, const std::vector<uint8_t>& payload) {
    if (!socket.isConnected()) return;

    PacketHeader h{
        static_cast<uint8_t>(type),
        static_cast<uint32_t>(payload.size()),
        0
    };

    if (!sendAll(socket, &h, sizeof(h))) {
        connectionLost = true;
        return;
    }
    if (!payload.empty()) {
        if (!sendAll(socket, payload.data(), payload.size())) {
            connectionLost = true;
            return;
        }
    }
}

bool NetworkClient::pollPacket(PacketType& outType, std::vector<uint8_t>& outPayload) {
    std::lock_guard<std::mutex> lk(qMutex);
    if (incoming.empty()) return false;
    auto p = std::move(incoming.front());
    incoming.pop();
    outType = p.first;
    outPayload = std::move(p.second);
    return true;
}

void NetworkClient::threadLoop() {
    std::vector<uint8_t> buf(8192);
    PacketAssembler assembler;

    auto lastKeepAlive = std::chrono::steady_clock::now();

    while (running) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastKeepAlive > std::chrono::seconds(5)) {
            lastKeepAlive = now;
            sendPacket(PacketType::KeepAlive, {});
        }

        int n = socket.recv(buf.data(), buf.size());
        if (n > 0) {
            assembler.feed(buf.data(), static_cast<size_t>(n));

            PacketType t;
            std::vector<uint8_t> payload;
            while (assembler.tryPop(t, payload)) {
                std::lock_guard<std::mutex> lk(qMutex);
                incoming.push({t, std::move(payload)});
            }
            if (assembler.isDesynced()) {
                LOGE("NetworkClient: protocol desync, dropping connection");
                connectionLost = true;
                break;
            }
        } else if (n == 0) {
            LOGI("NetworkClient: server closed connection");
            connectionLost = true;
            break;
        } else {
            if (!socket.isConnected() || socket.lastErrorWasFatal()) {
                LOGE("NetworkClient: socket error, dropping connection");
                connectionLost = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    running = false;
}

} // namespace mc
