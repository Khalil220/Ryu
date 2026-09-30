#include "update.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <zlib.h>

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

constexpr const char* damagedPackage = "The update package is damaged.";
constexpr std::uint32_t endOfDirectorySignature = 0x06054b50;
constexpr std::uint32_t directoryEntrySignature = 0x02014b50;
constexpr std::uint32_t localHeaderSignature = 0x04034b50;
constexpr size_t endOfDirectorySize = 22;
constexpr size_t maximumCommentSize = 0xffff;
constexpr size_t unpackChunk = 256 * 1024;

struct ZipEntry {
    fs::path path;
    bool directory = false;
    unsigned method = 0;
    std::uint32_t crc = 0;
    std::uint64_t compressedSize = 0;
    std::uint64_t size = 0;
    size_t localOffset = 0;
};

std::uint32_t little(std::string_view data, size_t offset, size_t width) {
    if (offset > data.size() || data.size() - offset < width) {
        throw std::runtime_error(damagedPackage);
    }
    std::uint32_t value = 0;
    for (size_t i = width; i-- > 0;) {
        value = value << 8 | static_cast<unsigned char>(data[offset + i]);
    }
    return value;
}

fs::path entryPath(std::string_view name) {
    const fs::path path =
        fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(name.data()), name.size())).lexically_normal();
    if (path.empty() || path.has_root_name() || path.has_root_directory() || *path.begin() == "..") {
        throw std::runtime_error(damagedPackage);
    }
    return path;
}

std::vector<ZipEntry> zipEntries(std::string_view archive) {
    if (archive.size() < endOfDirectorySize) {
        throw std::runtime_error(damagedPackage);
    }
    const size_t last = archive.size() - endOfDirectorySize;
    const size_t first = last > maximumCommentSize ? last - maximumCommentSize : 0;
    size_t end = last;
    while (little(archive, end, 4) != endOfDirectorySignature) {
        if (end == first) {
            throw std::runtime_error(damagedPackage);
        }
        --end;
    }
    const auto count = little(archive, end + 10, 2);
    size_t offset = little(archive, end + 16, 4);
    std::vector<ZipEntry> entries;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (little(archive, offset, 4) != directoryEntrySignature) {
            throw std::runtime_error(damagedPackage);
        }
        ZipEntry entry;
        entry.method = little(archive, offset + 10, 2);
        entry.crc = little(archive, offset + 16, 4);
        entry.compressedSize = little(archive, offset + 20, 4);
        entry.size = little(archive, offset + 24, 4);
        const size_t nameLength = little(archive, offset + 28, 2);
        const size_t extraLength = little(archive, offset + 30, 2);
        const size_t commentLength = little(archive, offset + 32, 2);
        entry.localOffset = little(archive, offset + 42, 4);
        if (entry.compressedSize == 0xffffffff || entry.size == 0xffffffff || entry.localOffset == 0xffffffff) {
            throw std::runtime_error("The update package is too large for Ryu to unpack.");
        }
        little(archive, offset + 46, nameLength);
        const auto name = archive.substr(offset + 46, nameLength);
        entry.directory = name.ends_with('/');
        entry.path = entryPath(name);
        entries.push_back(std::move(entry));
        offset += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
}

std::string_view entryData(std::string_view archive, const ZipEntry& entry) {
    if (little(archive, entry.localOffset, 4) != localHeaderSignature) {
        throw std::runtime_error(damagedPackage);
    }
    const size_t start = entry.localOffset + 30 + little(archive, entry.localOffset + 26, 2) +
                         little(archive, entry.localOffset + 28, 2);
    if (start > archive.size() || archive.size() - start < entry.compressedSize) {
        throw std::runtime_error(damagedPackage);
    }
    return archive.substr(start, static_cast<size_t>(entry.compressedSize));
}

struct InflateEnder {
    void operator()(z_stream* stream) const { inflateEnd(stream); }
};

void unpackEntry(std::string_view archive, const ZipEntry& entry, const fs::path& path,
                 const std::function<void(std::uint64_t)>& advance) {
    if (entry.method != 0 && entry.method != Z_DEFLATED) {
        throw std::runtime_error("The update package uses a compression Ryu can't unpack.");
    }
    const auto data = entryData(archive, entry);
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Could not write " + path.string());
    }
    std::uint64_t written = 0;
    uLong crc = crc32(0, nullptr, 0);
    const auto write = [&](const char* bytes, size_t length) {
        out.write(bytes, static_cast<std::streamsize>(length));
        crc = crc32(crc, reinterpret_cast<const Bytef*>(bytes), static_cast<uInt>(length));
        written += length;
        advance(length);
    };
    if (entry.method == 0) {
        for (size_t offset = 0; offset < data.size(); offset += unpackChunk) {
            write(data.data() + offset, std::min(unpackChunk, data.size() - offset));
        }
    } else {
        z_stream stream{};
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
            throw std::runtime_error("Could not start unpacking the update");
        }
        std::unique_ptr<z_stream, InflateEnder> ender(&stream);
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
        stream.avail_in = static_cast<uInt>(data.size());
        std::vector<char> buffer(unpackChunk);
        int result = Z_OK;
        while (result != Z_STREAM_END) {
            stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
            stream.avail_out = static_cast<uInt>(buffer.size());
            result = inflate(&stream, Z_NO_FLUSH);
            if (result != Z_OK && result != Z_STREAM_END) {
                throw std::runtime_error(damagedPackage);
            }
            write(buffer.data(), buffer.size() - stream.avail_out);
        }
    }
    out.close();
    if (!out) {
        throw std::runtime_error("Could not write " + path.string());
    }
    if (written != entry.size || crc != entry.crc) {
        throw std::runtime_error(damagedPackage);
    }
}

std::string downloaded(HttpResponse response) {
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("The download returned HTTP " + std::to_string(response.status));
    }
    return std::move(response.body);
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

void extractPackage(std::string_view archive, const fs::path& destination, const Progress& progress) {
    const auto entries = zipEntries(archive);
    std::uint64_t total = 0;
    for (const auto& entry : entries) {
        total += entry.size;
    }
    std::uint64_t done = 0;
    const auto advance = [&](std::uint64_t written) {
        done += written;
        if (progress && !progress(done, total)) {
            throw UpdateCancelled();
        }
    };
    fs::create_directories(destination);
    for (const auto& entry : entries) {
        const auto path = destination / entry.path;
        if (entry.directory) {
            fs::create_directories(path);
            continue;
        }
        fs::create_directories(path.parent_path());
        unpackEntry(archive, entry, path, advance);
    }
    if (progress && !progress(total, total)) {
        throw UpdateCancelled();
    }
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

fs::path installRelease(HttpClient& http, const Release& release, const fs::path& folder,
                        const InstallProgress& progress) {
    if (release.packageUrl.empty() || release.checksumsUrl.empty()) {
        throw std::runtime_error("This release has no Windows package with a checksum.");
    }
    const auto expected = checksumFor(downloaded(http.get(release.checksumsUrl)), release.packageName);
    if (!expected) {
        throw std::runtime_error("The release's checksums don't list its package.");
    }
    std::string package;
    try {
        package = downloaded(http.download(release.packageUrl, [&](std::uint64_t done, std::uint64_t total) {
            return progress(InstallStep::Downloading, done, total);
        }));
    } catch (const HttpCancelled&) {
        throw UpdateCancelled();
    }
    if (sha256Hex(package) != *expected) {
        throw std::runtime_error("The downloaded update doesn't match its checksum.");
    }

    const auto staging = folder / stagingFolderName;
    const auto discardStaging = [&] {
        std::error_code ignored;
        fs::remove_all(staging, ignored);
    };
    try {
        fs::remove_all(staging);
        extractPackage(package, staging, [&](std::uint64_t done, std::uint64_t total) {
            return progress(InstallStep::Extracting, done, total);
        });
        if (!fs::exists(staging / "ryu.exe")) {
            throw std::runtime_error("The update package has no ryu.exe.");
        }
        replaceFiles(staging, folder);
    } catch (const fs::filesystem_error& error) {
        discardStaging();
        if (error.code() == std::errc::permission_denied) {
            throw std::runtime_error("Windows didn't let Ryu change its files in " + folder.string() + ".");
        }
        throw;
    } catch (...) {
        discardStaging();
        throw;
    }
    discardStaging();
    return folder / "ryu.exe";
}

}
