#include "playlist_server.hpp"

#include <array>
#include <random>
#include <stdexcept>

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
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#define RYU_CLOSE_SOCKET close
#endif

namespace ryu {

namespace {

constexpr size_t keptPublications = 4;
constexpr std::uintptr_t noSocket = ~std::uintptr_t{0};

std::string randomToken() {
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device device;
    std::uniform_int_distribution<size_t> pick(0, sizeof(alphabet) - 2);
    std::string token;
    for (int i = 0; i < 24; ++i) {
        token.push_back(alphabet[pick(device)]);
    }
    return token;
}

void sendAll(SocketHandle client, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const auto chunk = ::send(client, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (chunk <= 0) {
            return;
        }
        sent += static_cast<size_t>(chunk);
    }
}

std::string requestPath(SocketHandle client) {
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
    auto path = request.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    return path.substr(0, path.find('?'));
}

}

PlaylistServer::~PlaylistServer() {
    const auto listener = listener_;
    if (listener != noSocket) {
        RYU_CLOSE_SOCKET(static_cast<SocketHandle>(listener));
    }
    if (thread_.joinable()) {
        thread_.join();
    }
#ifdef _WIN32
    if (listener != noSocket) {
        WSACleanup();
    }
#endif
}

std::string PlaylistServer::publish(const std::map<std::string, std::string>& files) {
    std::scoped_lock lock(mutex_);
    if (listener_ == noSocket) {
        start();
    }
    const auto token = randomToken();
    for (const auto& [name, body] : files) {
        files_["/" + token + "/" + name] = body;
    }
    tokens_.push_back(token);
    while (tokens_.size() > keptPublications) {
        const auto prefix = "/" + tokens_.front() + "/";
        std::erase_if(files_, [&](const auto& entry) { return entry.first.starts_with(prefix); });
        tokens_.pop_front();
    }
    return "http://127.0.0.1:" + std::to_string(port_) + "/" + token + "/";
}

void PlaylistServer::start() {
#ifdef _WIN32
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("Could not start networking for the playlist server");
    }
#endif
    const SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    socklen_t length = sizeof(address);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 16) != 0 ||
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        RYU_CLOSE_SOCKET(listener);
#ifdef _WIN32
        WSACleanup();
#endif
        throw std::runtime_error("Could not start the local playlist server");
    }
    listener_ = static_cast<std::uintptr_t>(listener);
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { serve(); });
}

void PlaylistServer::serve() {
    const auto listener = static_cast<SocketHandle>(listener_);
    while (true) {
        const SocketHandle client = ::accept(listener, nullptr, nullptr);
#ifdef _WIN32
        if (client == INVALID_SOCKET) {
            return;
        }
#else
        if (client < 0) {
            return;
        }
#endif
        const auto body = lookup(requestPath(client));
        std::string response;
        if (body.empty()) {
            response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        } else {
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        }
        sendAll(client, response);
        RYU_CLOSE_SOCKET(client);
    }
}

std::string PlaylistServer::lookup(const std::string& path) {
    std::scoped_lock lock(mutex_);
    const auto it = files_.find(path);
    return it == files_.end() ? std::string() : it->second;
}

}
