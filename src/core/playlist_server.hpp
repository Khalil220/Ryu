#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace ryu {

class PlaylistServer {
public:
    PlaylistServer() = default;
    ~PlaylistServer();
    PlaylistServer(const PlaylistServer&) = delete;
    PlaylistServer& operator=(const PlaylistServer&) = delete;

    std::string publish(const std::map<std::string, std::string>& files);

private:
    void start();
    void serve();
    std::string lookup(const std::string& path);

    std::mutex mutex_;
    std::map<std::string, std::string> files_;
    std::deque<std::string> tokens_;
    std::uintptr_t listener_ = ~std::uintptr_t{0};
    unsigned short port_ = 0;
    std::thread thread_;
};

}
