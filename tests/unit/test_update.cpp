#include "fake_http.hpp"
#include "update.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace ryu;
using ryu::test::FakeHttpClient;
using ryu::test::readFixture;
namespace fs = std::filesystem;

namespace {

class TempFolder {
public:
    TempFolder()
        : path_(fs::temp_directory_path() /
                ("ryu-update-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        fs::create_directories(path_);
    }
    ~TempFolder() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

void writeFile(const fs::path& path, const std::string& contents) {
    std::ofstream(path, std::ios::binary) << contents;
}

std::string readFile(const fs::path& path) {
    std::ostringstream contents;
    contents << std::ifstream(path, std::ios::binary).rdbuf();
    return contents.str();
}

}

TEST_CASE("parseRelease reads the version, notes, page, package and checksums from GitHub's release") {
    const auto release = parseRelease(readFixture("update/release.json"));

    REQUIRE(release.has_value());
    CHECK(release->version == "0.2.0");
    CHECK(release->notes == "- Add an audio language box to the player\n- Add AniZone as a third provider");
    CHECK(release->pageUrl == "https://github.com/Khalil220/Ryu/releases/tag/v0.2.0");
    CHECK(release->packageName == "Ryu-0.2.0-win64.zip");
    CHECK(release->packageUrl == "https://github.com/Khalil220/Ryu/releases/download/v0.2.0/Ryu-0.2.0-win64.zip");
    CHECK(release->checksumsUrl == "https://github.com/Khalil220/Ryu/releases/download/v0.2.0/SHA256SUMS");
}

TEST_CASE("parseRelease rejects anything without a tag") {
    CHECK_FALSE(parseRelease("not json").has_value());
    CHECK_FALSE(parseRelease(R"({"name":"Ryu"})").has_value());
    const auto bare = parseRelease(R"({"tag_name":"v0.3.0"})");
    REQUIRE(bare.has_value());
    CHECK(bare->packageUrl.empty());
}

TEST_CASE("latestRelease asks GitHub's API and treats a missing release as no update") {
    FakeHttpClient http;
    http.serve(releaseFeedUrl, readFixture("update/release.json"));
    const auto release = latestRelease(http, releaseFeedUrl);
    REQUIRE(release.has_value());
    CHECK(release->version == "0.2.0");
    CHECK(http.header(0, "Accept") == "application/vnd.github+json");

    FakeHttpClient none;
    none.serve(releaseFeedUrl, R"({"message":"Not Found"})", 404);
    CHECK_FALSE(latestRelease(none, releaseFeedUrl).has_value());

    FakeHttpClient limited;
    limited.serve(releaseFeedUrl, R"({"message":"API rate limit exceeded"})", 403);
    CHECK_THROWS_AS(latestRelease(limited, releaseFeedUrl), HttpError);
}

TEST_CASE("isNewerVersion compares version numbers part by part") {
    CHECK(isNewerVersion("0.2.0", "0.1.0"));
    CHECK(isNewerVersion("v0.1.10", "0.1.9"));
    CHECK(isNewerVersion("1.0", "0.9.9"));
    CHECK_FALSE(isNewerVersion("0.1.0", "0.1.0"));
    CHECK_FALSE(isNewerVersion("v0.1.0", "0.2.0"));
    CHECK_FALSE(isNewerVersion("latest", "0.1.0"));
    CHECK_FALSE(isNewerVersion("1.2.3.4", "0.1.0"));
}

TEST_CASE("checksumFor finds the package's hash in a SHA256SUMS file") {
    const std::string sums = "ABCDEF01  Ryu-0.2.0-win64.zip\r\n1234 *other.zip\n";
    CHECK(checksumFor(sums, "Ryu-0.2.0-win64.zip") == "abcdef01");
    CHECK(checksumFor(sums, "other.zip") == "1234");
    CHECK_FALSE(checksumFor(sums, "missing.zip").has_value());
    CHECK_FALSE(checksumFor("deadbeef \n", "missing.zip").has_value());
}

TEST_CASE("sha256Hex hashes like sha256sum") {
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("an unpacked update replaces the installed files and keeps the old ones until cleanup") {
    TempFolder folder;
    const auto& app = folder.path();
    writeFile(app / "ryu.exe", "old exe");
    writeFile(app / "prism.dll", "old dll");
    writeFile(app / "libmpv-2.dll", "untouched");
    writeFile(app / "notes.old", "someone else's");

    extractPackage(fs::path(RYU_FIXTURE_DIR) / "update" / "Ryu-0.2.0-win64.zip", app / ".ryu-update");
    CHECK(readFile(app / ".ryu-update" / "ryu.exe") == "new exe");
    replaceFiles(app / ".ryu-update", app);

    CHECK(readFile(app / "ryu.exe") == "new exe");
    CHECK(readFile(app / "prism.dll") == "new dll");
    CHECK(readFile(app / "ryu.exe.ryu-old") == "old exe");
    CHECK(readFile(app / "libmpv-2.dll") == "untouched");
    CHECK(fs::is_empty(app / ".ryu-update"));

    removeReplacedFiles(app);
    CHECK_FALSE(fs::exists(app / "ryu.exe.ryu-old"));
    CHECK_FALSE(fs::exists(app / "prism.dll.ryu-old"));
    CHECK(fs::exists(app / "ryu.exe"));
    CHECK(readFile(app / "notes.old") == "someone else's");
}

TEST_CASE("a failed replacement puts every file back") {
    TempFolder folder;
    const auto app = folder.path() / "app";
    const auto staging = folder.path() / "staging";
    fs::create_directories(app / "b.txt.ryu-old" / "stuck");
    fs::create_directories(staging);
    writeFile(app / "a.txt", "old a");
    writeFile(app / "b.txt", "old b");
    writeFile(app / "b.txt.ryu-old" / "stuck" / "file", "in the way");
    writeFile(staging / "a.txt", "new a");
    writeFile(staging / "b.txt", "new b");

    CHECK_THROWS(replaceFiles(staging, app));

    CHECK(readFile(app / "a.txt") == "old a");
    CHECK(readFile(app / "b.txt") == "old b");
    CHECK_FALSE(fs::exists(app / "a.txt.ryu-old"));
}

TEST_CASE("extractPackage reports a package tar can't read") {
    TempFolder folder;
    writeFile(folder.path() / "broken.zip", "not a zip");
    CHECK_THROWS(extractPackage(folder.path() / "broken.zip", folder.path() / "out"));
}
