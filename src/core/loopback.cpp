#include "loopback.hpp"

#include <array>

namespace ryu {

namespace {

constexpr int receiveTimeoutMilliseconds = 2000;

}

bool startNetworking() {
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void stopNetworking() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void closeSocket(SocketHandle socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

SocketHandle listenOnLoopback(int family, unsigned short port) {
    const SocketHandle listener = ::socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }
    int bound = -1;
    if (family == AF_INET6) {
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_loopback;
        address.sin6_port = htons(port);
        bound = ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    } else {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        bound = ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    }
    if (bound != 0 || ::listen(listener, 16) != 0) {
        closeSocket(listener);
        return INVALID_SOCKET;
    }
    return listener;
}

std::string requestTarget(SocketHandle client) {
#ifdef _WIN32
    const DWORD timeout = receiveTimeoutMilliseconds;
#else
    const timeval timeout{receiveTimeoutMilliseconds / 1000, receiveTimeoutMilliseconds % 1000 * 1000};
#endif
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::string request;
    std::array<char, 2048> buffer{};
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 16384) {
        const auto received = ::recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) {
            break;
        }
        request.append(buffer.data(), static_cast<size_t>(received));
    }
    const auto firstSpace = request.find(' ');
    const auto secondSpace = request.find(' ', firstSpace + 1);
    if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
        return {};
    }
    return request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
}

void sendAll(SocketHandle client, std::string_view data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const auto chunk = ::send(client, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (chunk <= 0) {
            return;
        }
        sent += static_cast<size_t>(chunk);
    }
}

}
