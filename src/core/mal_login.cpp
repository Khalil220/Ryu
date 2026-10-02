#include "mal_login.hpp"

#include "mal.hpp"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#define RYU_CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
constexpr SocketHandle INVALID_SOCKET = -1;
#define RYU_CLOSE_SOCKET close
#endif

namespace ryu {

namespace {

constexpr std::string_view callbackPath = "/callback";
constexpr auto pollInterval = std::chrono::milliseconds(200);

class Networking {
public:
    Networking() {
#ifdef _WIN32
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw MalError("Could not start networking to receive the MyAnimeList login.");
        }
#endif
    }
    ~Networking() {
        for (const auto socket : sockets) {
            RYU_CLOSE_SOCKET(socket);
        }
#ifdef _WIN32
        WSACleanup();
#endif
    }

    std::vector<SocketHandle> sockets;
};

SocketHandle listenOn(int family, unsigned short port) {
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
    if (bound != 0 || ::listen(listener, 8) != 0) {
        RYU_CLOSE_SOCKET(listener);
        return INVALID_SOCKET;
    }
    return listener;
}

std::string requestTarget(SocketHandle client) {
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

void reply(SocketHandle client, std::string_view status, std::string_view heading, std::string_view text) {
    const std::string page = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>Ryu</title></head>"
                             "<body><h1>" +
                             std::string(heading) + "</h1><p>" + std::string(text) + "</p></body></html>";
    const std::string response = "HTTP/1.1 " + std::string(status) +
                                 "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                                 std::to_string(page.size()) + "\r\nConnection: close\r\n\r\n" + page;
    size_t sent = 0;
    while (sent < response.size()) {
        const auto chunk = ::send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
        if (chunk <= 0) {
            return;
        }
        sent += static_cast<size_t>(chunk);
    }
}

}

std::string waitForMalRedirect(unsigned short port, std::chrono::seconds timeout, const std::atomic<bool>& cancelled) {
    Networking networking;
    for (const int family : {AF_INET, AF_INET6}) {
        if (const auto listener = listenOn(family, port); listener != INVALID_SOCKET) {
            networking.sockets.push_back(listener);
        }
    }
    if (networking.sockets.empty()) {
        throw MalError("Ryu could not listen on port " + std::to_string(port) +
                       " for the MyAnimeList login. Another program may be using it.");
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!cancelled && std::chrono::steady_clock::now() < deadline) {
        fd_set readable;
        FD_ZERO(&readable);
        SocketHandle highest = 0;
        for (const auto socket : networking.sockets) {
            FD_SET(socket, &readable);
            highest = std::max(highest, socket);
        }
        timeval wait{};
        wait.tv_usec = static_cast<long>(std::chrono::duration_cast<std::chrono::microseconds>(pollInterval).count());
        if (::select(static_cast<int>(highest) + 1, &readable, nullptr, nullptr, &wait) <= 0) {
            continue;
        }
        for (const auto socket : networking.sockets) {
            if (!FD_ISSET(socket, &readable)) {
                continue;
            }
            const SocketHandle client = ::accept(socket, nullptr, nullptr);
            if (client == INVALID_SOCKET) {
                continue;
            }
            const auto target = requestTarget(client);
            const bool callback = std::string_view(target).substr(0, target.find('?')) == callbackPath;
            if (!callback) {
                reply(client, "404 Not Found", "Nothing here", "This address only receives the MyAnimeList login.");
            } else if (parseMalRedirect(target).code.empty()) {
                reply(client, "200 OK", "Ryu was not logged in",
                      "MyAnimeList did not approve the login. You can close this tab and try again from Ryu.");
            } else {
                reply(client, "200 OK", "Ryu is logging in", "You can close this tab and go back to Ryu.");
            }
            RYU_CLOSE_SOCKET(client);
            if (callback) {
                return target;
            }
        }
    }
    return {};
}

}
