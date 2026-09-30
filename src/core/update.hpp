#pragma once

#include "http.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ryu {

inline constexpr const char* releaseFeedUrl = "https://api.github.com/repos/Khalil220/Ryu/releases/latest";
inline constexpr const char* checksumsName = "SHA256SUMS";
inline constexpr std::chrono::seconds updateDownloadTimeout{900};
inline constexpr const char* stagingFolderName = ".ryu-update";

struct Release {
    std::string version;
    std::string notes;
    std::string pageUrl;
    std::string packageName;
    std::string packageUrl;
    std::string checksumsUrl;
};

class UpdateCancelled : public std::runtime_error {
public:
    UpdateCancelled() : std::runtime_error("The update was cancelled") {}
};

enum class InstallStep { Downloading, Extracting };
using InstallProgress = std::function<bool(InstallStep step, std::uint64_t done, std::uint64_t total)>;

std::optional<Release> parseRelease(std::string_view json);
std::optional<Release> latestRelease(HttpClient& http, const std::string& feedUrl);
bool isNewerVersion(std::string_view candidate, std::string_view current);
std::optional<std::string> checksumFor(std::string_view sums, std::string_view fileName);
std::string sha256Hex(std::string_view data);

void extractPackage(std::string_view archive, const std::filesystem::path& destination,
                    const Progress& progress = {});
void replaceFiles(const std::filesystem::path& staging, const std::filesystem::path& target);
void removeReplacedFiles(const std::filesystem::path& directory);
bool canWriteTo(const std::filesystem::path& folder);
std::filesystem::path installRelease(HttpClient& http, const Release& release, const std::filesystem::path& folder,
                                     const InstallProgress& progress);

}
