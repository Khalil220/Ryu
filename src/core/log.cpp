#include "log.hpp"

#include <chrono>
#include <format>
#include <fstream>
#include <mutex>

namespace ryu {

namespace {

struct Log {
    std::mutex mutex;
    std::ofstream file;
    std::chrono::steady_clock::time_point opened;
};

Log& state() {
    static auto* log = new Log;
    return *log;
}

std::string startedAt() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    try {
        return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::zoned_time(std::chrono::current_zone(), now));
    } catch (const std::exception&) {
        return std::format("{:%Y-%m-%d %H:%M:%S} UTC", now);
    }
}

}

void openLog(const std::filesystem::path& path) {
    auto& log = state();
    std::scoped_lock lock(log.mutex);
    log.file.close();
    std::error_code ignored;
    if (std::filesystem::exists(path, ignored)) {
        auto previous = path;
        std::filesystem::rename(path, previous.replace_extension(".old.log"), ignored);
    }
    log.file.open(path, std::ios::out | std::ios::trunc);
    log.opened = std::chrono::steady_clock::now();
    if (log.file) {
        log.file << "Ryu log started " << startedAt() << '\n' << std::flush;
    }
}

void closeLog() {
    auto& log = state();
    std::scoped_lock lock(log.mutex);
    log.file.close();
}

void logLine(std::string_view message) {
    auto& log = state();
    std::scoped_lock lock(log.mutex);
    if (!log.file.is_open()) {
        return;
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - log.opened;
    log.file << std::format("[{:9.3f}] ", elapsed.count()) << message << '\n' << std::flush;
}

}
