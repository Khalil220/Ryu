#include "log.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace ryu;

namespace {

std::string contents(const std::filesystem::path& path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

}

TEST_CASE("the log keeps the previous session and writes timestamped lines") {
    const auto directory = std::filesystem::temp_directory_path() / "ryu-log-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const auto path = directory / "ryu.log";

    openLog(path);
    logLine("first session");
    openLog(path);
    logLine("second session");
    closeLog();
    logLine("after closing");

    const auto current = contents(path);
    const auto previous = contents(directory / "ryu.old.log");
    CHECK(current.starts_with("Ryu log started "));
    CHECK(current.find("] second session\n") != std::string::npos);
    CHECK(current.find("first session") == std::string::npos);
    CHECK(current.find("after closing") == std::string::npos);
    CHECK(previous.find("] first session\n") != std::string::npos);
    std::filesystem::remove_all(directory);
}
