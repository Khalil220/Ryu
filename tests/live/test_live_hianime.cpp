#include "http.hpp"
#include "providers/hianime.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;

namespace {

Stream firstEpisodeStream(HiAnimeProvider& provider) {
    const auto shows = provider.search("frieren");
    REQUIRE_FALSE(shows.empty());
    const auto show = std::ranges::find_if(shows, [](const Show& s) { return s.subEpisodes > 0; });
    REQUIRE(show != shows.end());
    MESSAGE("Show: " << show->title << " (" << show->id << ")");

    const auto episodes = provider.episodes(show->id);
    REQUIRE_FALSE(episodes.empty());
    MESSAGE("Episodes: " << episodes.size());

    const auto streams = provider.streams(episodes.front().id, Audio::Sub);
    REQUIRE_FALSE(streams.empty());
    MESSAGE("Stream: " << streams.front().url);
    return streams.front();
}

}

TEST_CASE("live: HiAnime search, episodes and stream resolve to a playable playlist") {
    CurlHttpClient http;
    HiAnimeProvider provider(http);
    const auto stream = firstEpisodeStream(provider);

    const auto playlist = http.get(stream.url, stream.headers);
    CHECK(playlist.status == 200);
    CHECK(playlist.body.starts_with("#EXTM3U"));

    if (!stream.subtitles.empty()) {
        const auto subtitle = http.get(stream.subtitles.front().url, stream.headers);
        CHECK(subtitle.status == 200);
        CHECK(subtitle.body.find("WEBVTT") != std::string::npos);
    }
}
