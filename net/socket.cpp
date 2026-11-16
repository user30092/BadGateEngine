#include "socket.hpp"
#include "../core/log.hpp"

#include <cstring>
#include <cerrno>
#include <cstdio>

#ifndef _WIN32
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <netdb.h>
#endif

namespace mc {

namespace {

#ifdef _WIN32
void ensureNetworkInit() {
    static const bool ok = [] {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)ok;
}
void closeSocketHandle(SocketHandle s) { ::closesocket(s); }
int  lastSocketError()                 { return WSAGetLastError(); }
bool isWouldBlockError(int e)          { return e == WSAEWOULDBLOCK || e == WSAEINTR; }
#else
void ensureNetworkInit()               {}
void closeSocketHandle(SocketHandle s) { ::close(s); }
int  lastSocketError()                 { return errno; }
bool isWouldBlockError(int e)          { return e == EAGAIN || e == EWOULDBLOCK || e == EINTR; }
#endif

} // namespace

TcpSocket::~TcpSocket() { disconnect(); }

bool TcpSocket::connect(const std::string& host, uint16_t port) {
    disconnect();
    ensureNetworkInit();

    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* res = nullptr;
    char portStr[8];
    std::snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));

    if (getaddrinfo(host.c_str(), portStr, &hints, &res) != 0) {
        LOGE("getaddrinfo failed for %s", host.c_str());
        return false;
    }

    for (addrinfo* ai = res; ai != nullptr; ai = ai->ai_next) {
        SocketHandle s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET_HANDLE) continue;
        if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
            freeaddrinfo(res);
            handle_ = s;
            return true;
        }
        closeSocketHandle(s);
    }
    freeaddrinfo(res);
    LOGE("connect failed");
    return false;
}

void TcpSocket::disconnect() {
    if (handle_ != INVALID_SOCKET_HANDLE) {
        closeSocketHandle(handle_);
        handle_ = INVALID_SOCKET_HANDLE;
    }
}

int TcpSocket::send(const void* data, size_t n) {
    if (handle_ == INVALID_SOCKET_HANDLE) return -1;
#ifdef MSG_NOSIGNAL
    return static_cast<int>(::send(handle_,
        static_cast<const char*>(data), static_cast<int>(n), MSG_NOSIGNAL));
#else
    return static_cast<int>(::send(handle_,
        static_cast<const char*>(data), static_cast<int>(n), 0));
#endif
}

int TcpSocket::recv(void* buffer, size_t n) {
    if (handle_ == INVALID_SOCKET_HANDLE) return -1;
    return static_cast<int>(::recv(handle_,
        static_cast<char*>(buffer), static_cast<int>(n), 0));
}

void TcpSocket::setNonBlocking(bool nb) {
    if (handle_ == INVALID_SOCKET_HANDLE) return;
#ifdef _WIN32
    u_long mode = nb ? 1u : 0u;
    ioctlsocket(handle_, FIONBIO, &mode);
#else
    int flags = fcntl(handle_, F_GETFL, 0);
    if (nb) flags |= O_NONBLOCK;
    else    flags &= ~O_NONBLOCK;
    fcntl(handle_, F_SETFL, flags);
#endif
}

bool TcpSocket::lastErrorWasFatal() const {
    return !isWouldBlockError(lastSocketError());
}

// ---------------- TcpListener ----------------

TcpListener::~TcpListener() { close(); }

bool TcpListener::listen(uint16_t port, int backlog) {
    close();
    ensureNetworkInit();

    handle_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (handle_ == INVALID_SOCKET_HANDLE) return false;

    int reuse = 1;
    ::setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(handle_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOGE("TcpListener: bind(%u) failed", static_cast<unsigned>(port));
        close();
        return false;
    }
    if (::listen(handle_, backlog) < 0) {
        LOGE("TcpListener: listen failed");
        close();
        return false;
    }

#ifdef _WIN32
    u_long mode = 1u;
    ioctlsocket(handle_, FIONBIO, &mode);
#else
    int flags = fcntl(handle_, F_GETFL, 0);
    fcntl(handle_, F_SETFL, flags | O_NONBLOCK);
#endif
    return true;
}

void TcpListener::close() {
    if (handle_ != INVALID_SOCKET_HANDLE) {
        closeSocketHandle(handle_);
        handle_ = INVALID_SOCKET_HANDLE;
    }
}

SocketHandle TcpListener::acceptRaw() {
    if (handle_ == INVALID_SOCKET_HANDLE) return INVALID_SOCKET_HANDLE;
    return ::accept(handle_, nullptr, nullptr);
}

} // namespace mc
