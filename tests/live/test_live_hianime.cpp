#include "http.hpp"
#include "providers/hianime.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <sstream>

using namespace ryu;

namespace {

std::string resolveAgainst(const std::string& base, const std::string& line) {
    return line.starts_with("http") ? line : base.substr(0, base.find_last_of('/') + 1) + line;
}

std::string firstUri(const std::string& playlist) {
    std::istringstream lines(playlist);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty() && line.front() != '#') {
            return line;
        }
    }
    return {};
}

bool firstSegmentIsVideo(HttpClient& http, const Stream& stream) {
    const auto master = http.get(stream.url, stream.headers);
    if (master.status != 200 || !master.body.starts_with("#EXTM3U")) {
        return false;
    }
    const auto variantUrl = resolveAgainst(stream.url, firstUri(master.body));
    const auto variant = http.get(variantUrl, stream.headers);
    const auto segmentUrl = resolveAgainst(variantUrl, firstUri(variant.body));
    const auto segment = http.get(segmentUrl, stream.headers);
    MESSAGE("Segment " << segmentUrl << " returned " << segment.status);
    return segment.status == 200 && !segment.body.empty() && segment.body.front() == 0x47;
}

}

TEST_CASE("live: a recent HiAnime episode resolves to video Ryu can play") {
    CurlHttpClient http;
    HiAnimeProvider provider(http);

    const auto shows = provider.search("one piece");
    const auto show = std::ranges::find_if(shows, [](const Show& s) { return s.title == "One Piece"; });
    REQUIRE(show != shows.end());
    const auto episodes = provider.episodes(show->id);
    REQUIRE(episodes.size() > 3);

    bool played = false;
    for (auto it = episodes.rbegin(); it != episodes.rbegin() + 3 && !played; ++it) {
        const auto streams = provider.streams(it->id, Audio::Sub);
        REQUIRE_FALSE(streams.empty());
        MESSAGE("Episode " << it->number << " via " << streams.front().server << ": " << streams.front().url);
        played = firstSegmentIsVideo(http, streams.front());
        if (played && !streams.front().subtitles.empty()) {
            const auto subtitle = http.get(streams.front().subtitles.front().url, streams.front().headers);
            CHECK(subtitle.status == 200);
            CHECK(subtitle.body.find("WEBVTT") != std::string::npos);
        }
    }
    CHECK(played);
}

TEST_CASE("live: Frieren still lists episodes with a stream for episode 1") {
    CurlHttpClient http;
    HiAnimeProvider provider(http);

    const auto shows = provider.search("frieren");
    REQUIRE_FALSE(shows.empty());
    CHECK(shows.front().id == "481");
    const auto episodes = provider.episodes(shows.front().id);
    REQUIRE(episodes.size() == 28);
    CHECK_FALSE(provider.streams(episodes.front().id, Audio::Sub).empty());
}
