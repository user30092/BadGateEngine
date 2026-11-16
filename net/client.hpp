#pragma once
#include "socket.hpp"
#include "packet.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <utility>

namespace mc {

class NetworkClient {
public:
    ~NetworkClient();
    bool start(const std::string& host, uint16_t port);
    void stop();
    bool isRunning() const { return running; }

    // true, если соединение оборвалось само (сервер закрыл, ошибка сокета,
    // рассинхрон протокола) — в отличие от stop(), вызванного пользователем.
    // main.cpp должен проверять это раз в кадр, чтобы показать меню
    // "соединение потеряно" и вернуть игрока в главное меню.
    bool connectionWasLost() const { return connectionLost; }

    void sendPacket(PacketType type, const std::vector<uint8_t>& payload);
    bool pollPacket(PacketType& outType, std::vector<uint8_t>& outPayload);

    // Храним последний адрес — нужно для кнопки "Переподключиться" в UI,
    // не заставляя пользователя вводить host:port заново.
    const std::string& lastHost() const { return host_; }
    uint16_t lastPort() const { return port_; }

private:
    void threadLoop();

    TcpSocket socket;
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> connectionLost{false};
    std::mutex qMutex;
    std::queue<std::pair<PacketType, std::vector<uint8_t>>> incoming;

    std::string host_;
    uint16_t port_ = 0;
};

} // namespace mc
