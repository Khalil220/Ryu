#include "mal_login.hpp"

#include "loopback.hpp"
#include "mal.hpp"

#include <algorithm>
#include <string_view>
#include <vector>

namespace ryu {

namespace {

constexpr std::string_view callbackPath = "/callback";
constexpr auto pollInterval = std::chrono::milliseconds(200);

class Networking {
public:
    Networking() {
        if (!startNetworking()) {
            throw MalError("Could not start networking to receive the MyAnimeList login.");
        }
    }
    ~Networking() {
        for (const auto socket : sockets) {
            closeSocket(socket);
        }
        stopNetworking();
    }

    std::vector<SocketHandle> sockets;
};

void reply(SocketHandle client, std::string_view status, std::string_view heading, std::string_view text) {
    const std::string page = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><title>Ryu</title></head>"
                             "<body><h1>" +
                             std::string(heading) + "</h1><p>" + std::string(text) + "</p></body></html>";
    sendAll(client, "HTTP/1.1 " + std::string(status) + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                        std::to_string(page.size()) + "\r\nConnection: close\r\n\r\n" + page);
}

}

std::string waitForMalRedirect(unsigned short port, std::chrono::seconds timeout, const std::atomic<bool>& cancelled) {
    Networking networking;
    for (const int family : {AF_INET, AF_INET6}) {
        if (const auto listener = listenOnLoopback(family, port); listener != INVALID_SOCKET) {
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
            closeSocket(client);
            if (callback) {
                return target;
            }
        }
    }
    return {};
}

}
