#pragma once

#include <filesystem>
#include <string_view>

namespace ryu {

void openLog(const std::filesystem::path& path);
void closeLog();
void logLine(std::string_view message);

}
