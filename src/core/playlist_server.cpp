#include "playlist_server.hpp"

#include "loopback.hpp"

#include <random>
#include <stdexcept>

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

}

PlaylistServer::~PlaylistServer() {
    const auto listener = listener_;
    if (listener != noSocket) {
        closeSocket(static_cast<SocketHandle>(listener));
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (listener != noSocket) {
        stopNetworking();
    }
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
    if (!startNetworking()) {
        throw std::runtime_error("Could not start networking for the playlist server");
    }
    const SocketHandle listener = listenOnLoopback(AF_INET, 0);
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    if (listener == INVALID_SOCKET ||
        ::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        if (listener != INVALID_SOCKET) {
            closeSocket(listener);
        }
        stopNetworking();
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
        if (client == INVALID_SOCKET) {
            return;
        }
        const auto target = requestTarget(client);
        const auto body = lookup(target.substr(0, target.find('?')));
        std::string response;
        if (body.empty()) {
            response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        } else {
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        }
        sendAll(client, response);
        closeSocket(client);
    }
}

std::string PlaylistServer::lookup(const std::string& path) {
    std::scoped_lock lock(mutex_);
    const auto it = files_.find(path);
    return it == files_.end() ? std::string() : it->second;
}

}
