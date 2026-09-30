#include "http.hpp"
#include "update.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <chrono>

using namespace ryu;

namespace {

constexpr const char* prismFeed = "https://api.github.com/repos/ethindp/prism/releases/latest";

void downloadsLikeTheUpdater(const std::string& name) {
    CurlHttpClient api;
    const auto feed = api.get(prismFeed, {{"Accept", "application/vnd.github+json"}});
    REQUIRE(feed.status == 200);
    const auto release = nlohmann::json::parse(feed.body);
    std::string url;
    std::string digest;
    size_t size = 0;
    for (const auto& asset : release.at("assets")) {
        if (asset.value("name", "") == name) {
            url = asset.value("browser_download_url", "");
            digest = asset.value("digest", "");
            size = asset.value("size", size_t{0});
        }
    }
    REQUIRE_FALSE(url.empty());

    CurlHttpClient http(CurlHttpClient::defaultUserAgent, updateDownloadTimeout);
    const auto started = std::chrono::steady_clock::now();
    const auto download = http.get(url);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    MESSAGE(name << ": " << download.body.size() << " bytes in " << seconds << " s from " << download.effectiveUrl);
    CHECK(download.status == 200);
    CHECK(download.effectiveUrl != url);
    CHECK(download.body.size() == size);
    if (digest.empty()) {
        MESSAGE("GitHub gave no digest for " << name);
    } else {
        CHECK(digest == "sha256:" + sha256Hex(download.body));
    }
}

}

TEST_CASE("live: GitHub's latest release feed for Ryu answers") {
    CurlHttpClient http;
    const auto release = latestRelease(http, releaseFeedUrl);
    if (release) {
        CHECK_FALSE(release->version.empty());
        CHECK_FALSE(release->packageUrl.empty());
        CHECK_FALSE(release->checksumsUrl.empty());
    } else {
        MESSAGE("Ryu has no published release yet");
    }
}

TEST_CASE("live: a real GitHub release parses") {
    CurlHttpClient http;
    const auto release = latestRelease(http, prismFeed);
    REQUIRE(release.has_value());
    CHECK_FALSE(release->version.empty());
    CHECK(release->pageUrl.starts_with("https://github.com/ethindp/prism/releases/"));
}

TEST_CASE("live: a release asset downloads through GitHub's redirect and matches GitHub's digest") {
    downloadsLikeTheUpdater("prism-android.zip");
}

TEST_CASE("live: a release asset the size of Ryu's downloads within the updater's timeout" * doctest::skip()) {
    downloadsLikeTheUpdater("prismatoid-gdextension-v0.18.2.zip");
}
