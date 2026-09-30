#include "http.hpp"
#include "update.hpp"

#include <doctest/doctest.h>

using namespace ryu;

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
    const auto release = latestRelease(http, "https://api.github.com/repos/ethindp/prism/releases/latest");
    REQUIRE(release.has_value());
    CHECK_FALSE(release->version.empty());
    CHECK(release->pageUrl.starts_with("https://github.com/ethindp/prism/releases/"));
}
