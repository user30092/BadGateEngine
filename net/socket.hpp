#pragma once
#include <cstdint>
#include <string>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#endif

namespace mc {

#ifdef _WIN32
using SocketHandle = SOCKET;
constexpr SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;
#endif

class TcpSocket {
public:
    TcpSocket() = default;
    explicit TcpSocket(SocketHandle acceptedHandle) : handle_(acceptedHandle) {}

    TcpSocket(const TcpSocket&) = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;
    TcpSocket(TcpSocket&& o) noexcept : handle_(o.handle_) {
        o.handle_ = INVALID_SOCKET_HANDLE;
    }
    TcpSocket& operator=(TcpSocket&& o) noexcept {
        if (this != &o) {
            disconnect();
            handle_ = o.handle_;
            o.handle_ = INVALID_SOCKET_HANDLE;
        }
        return *this;
    }

    ~TcpSocket();
    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    bool isConnected() const { return handle_ != INVALID_SOCKET_HANDLE; }
    int  send(const void* data, size_t n);
    int  recv(void* buffer, size_t n);
    void setNonBlocking(bool nb);
    bool lastErrorWasFatal() const;

private:
    SocketHandle handle_ = INVALID_SOCKET_HANDLE;
};

class TcpListener {
public:
    ~TcpListener();
    bool listen(uint16_t port, int backlog = 32);
    void close();
    bool isListening() const { return handle_ != INVALID_SOCKET_HANDLE; }
    SocketHandle acceptRaw();

private:
    SocketHandle handle_ = INVALID_SOCKET_HANDLE;
};

} // namespace mc
