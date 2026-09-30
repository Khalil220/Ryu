#include "update.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <bcrypt.h>
#endif

namespace ryu {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

constexpr std::string_view packageSuffix = "-win64.zip";
constexpr std::string_view replacedSuffix = ".ryu-old";

std::string stringField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::optional<std::array<int, 3>> versionParts(std::string_view text) {
    if (text.starts_with('v') || text.starts_with('V')) {
        text.remove_prefix(1);
    }
    std::array<int, 3> parts{};
    for (size_t i = 0; i < parts.size(); ++i) {
        const auto end = text.find('.');
        const auto piece = text.substr(0, end);
        const auto result = std::from_chars(piece.data(), piece.data() + piece.size(), parts[i]);
        if (piece.empty() || result.ec != std::errc() || result.ptr != piece.data() + piece.size()) {
            return std::nullopt;
        }
        if (end == std::string_view::npos) {
            return parts;
        }
        text.remove_prefix(end + 1);
    }
    return text.empty() ? std::optional(parts) : std::nullopt;
}

fs::path replacedName(const fs::path& file) {
    return fs::path(file).concat(replacedSuffix);
}

}

std::optional<Release> parseRelease(std::string_view text) {
    const auto parsed = json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        return std::nullopt;
    }
    Release release;
    release.version = stringField(parsed, "tag_name");
    if (release.version.starts_with('v')) {
        release.version.erase(0, 1);
    }
    if (release.version.empty()) {
        return std::nullopt;
    }
    release.notes = stringField(parsed, "body");
    release.pageUrl = stringField(parsed, "html_url");
    if (const auto assets = parsed.find("assets"); assets != parsed.end() && assets->is_array()) {
        for (const auto& asset : *assets) {
            const auto name = stringField(asset, "name");
            const auto url = stringField(asset, "browser_download_url");
            if (name.ends_with(packageSuffix) && release.packageUrl.empty()) {
                release.packageName = name;
                release.packageUrl = url;
            } else if (name == checksumsName) {
                release.checksumsUrl = url;
            }
        }
    }
    return release;
}

std::optional<Release> latestRelease(HttpClient& http, const std::string& feedUrl) {
    const auto response = http.get(feedUrl, {{"Accept", "application/vnd.github+json"}});
    if (response.status == 404) {
        return std::nullopt;
    }
    if (response.status < 200 || response.status >= 300) {
        throw HttpError("GitHub returned HTTP " + std::to_string(response.status) + " for the latest release");
    }
    auto release = parseRelease(response.body);
    if (!release) {
        throw HttpError("GitHub sent a release Ryu could not read");
    }
    return release;
}

bool isNewerVersion(std::string_view candidate, std::string_view current) {
    const auto next = versionParts(candidate);
    const auto now = versionParts(current);
    return next && now && *next > *now;
}

std::optional<std::string> checksumFor(std::string_view sums, std::string_view fileName) {
    while (!sums.empty()) {
        const auto end = sums.find('\n');
        auto line = sums.substr(0, end);
        sums = end == std::string_view::npos ? std::string_view() : sums.substr(end + 1);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        const auto space = line.find(' ');
        const auto start = space == std::string_view::npos ? space : line.find_first_not_of(" *", space);
        if (start == std::string_view::npos) {
            continue;
        }
        if (line.substr(start) == fileName) {
            std::string hash(line.substr(0, space));
            std::ranges::transform(hash, hash.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return hash;
        }
    }
    return std::nullopt;
}

std::string sha256Hex(std::string_view data) {
#ifdef _WIN32
    std::array<unsigned char, 32> digest{};
    if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                   reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                                   static_cast<ULONG>(data.size()), digest.data(),
                                   static_cast<ULONG>(digest.size())))) {
        throw std::runtime_error("Could not compute a SHA-256 hash");
    }
    std::string hex;
    for (const auto byte : digest) {
        char pair[3];
        std::snprintf(pair, sizeof(pair), "%02x", byte);
        hex += pair;
    }
    return hex;
#else
    throw std::runtime_error("SHA-256 is only available on Windows");
#endif
}

void extractPackage(const fs::path& package, const fs::path& destination) {
#ifdef _WIN32
    fs::create_directories(destination);
    wchar_t system[MAX_PATH];
    const auto length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        throw std::runtime_error("Could not find the Windows system folder");
    }
    std::wstring commandLine = L"\"" + (fs::path(system) / L"tar.exe").wstring() + L"\" -xf \"" + package.wstring() +
                               L"\" -C \"" + destination.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process)) {
        throw std::runtime_error("Could not start tar to unpack the update");
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (code != 0) {
        throw std::runtime_error("tar could not unpack the update (exit code " + std::to_string(code) + ")");
    }
#else
    (void)package;
    (void)destination;
    throw std::runtime_error("Unpacking updates is only available on Windows");
#endif
}

void replaceFiles(const fs::path& staging, const fs::path& target) {
    std::vector<fs::path> names;
    for (const auto& entry : fs::directory_iterator(staging)) {
        if (entry.is_regular_file()) {
            names.push_back(entry.path().filename());
        }
    }
    std::ranges::sort(names);
    std::vector<std::pair<fs::path, bool>> done;
    try {
        for (const auto& name : names) {
            const auto destination = target / name;
            const auto replaced = replacedName(destination);
            const bool existed = fs::exists(destination);
            if (existed) {
                fs::remove(replaced);
                fs::rename(destination, replaced);
            }
            done.emplace_back(destination, existed);
            fs::rename(staging / name, destination);
        }
    } catch (...) {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            std::error_code ignored;
            const auto& [destination, existed] = *it;
            if (existed) {
                if (fs::exists(replacedName(destination), ignored)) {
                    fs::remove(destination, ignored);
                    fs::rename(replacedName(destination), destination, ignored);
                }
            } else {
                fs::remove(destination, ignored);
            }
        }
        throw;
    }
}

void removeReplacedFiles(const fs::path& directory) {
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (entry.path().extension() == replacedSuffix) {
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
        }
    }
}

bool canWriteTo(const fs::path& folder) {
    const auto probe = folder / ".ryu-write-check";
    {
        std::ofstream out(probe, std::ios::binary);
        if (!out) {
            return false;
        }
    }
    std::error_code ignored;
    fs::remove(probe, ignored);
    return true;
}

}
