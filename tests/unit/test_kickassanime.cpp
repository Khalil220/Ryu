#include "fake_http.hpp"
#include "providers/kickassanime.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;
using ryu::test::FakeHttpClient;
using ryu::test::readFixture;

namespace {

constexpr const char* searchUrl = "https://kaa.lt/api/fsearch";
constexpr const char* jaListUrl = "https://kaa.lt/api/show/sousou-no-frieren-2d15/episodes?ep=1&lang=ja-JP";
constexpr const char* enListUrl = "https://kaa.lt/api/show/sousou-no-frieren-2d15/episodes?ep=1&lang=en-US";
constexpr const char* jaEpisodeUrl = "https://kaa.lt/api/show/sousou-no-frieren-2d15/episode/ep-1-f897b3";
constexpr const char* enEpisodeUrl = "https://kaa.lt/api/show/sousou-no-frieren-2d15/episode/ep-1-aa83b7";
constexpr const char* jaPlayerUrl =
    "https://krussdomi.com/cat-player/player?id=67d0c079169c31976b8d7970&source=vidstream&ln=ja-JP";
constexpr const char* enPlayerUrl =
    "https://krussdomi.com/cat-player/player?id=67d0c079169c31976b8d7970&source=vidstream&ln=en-US";
constexpr const char* manifest = "https://hls.krussdomi.com/manifest/67d0c079169c31976b8d7970/master.m3u8";

}

TEST_CASE("search posts the query and reads titles, format, year and audio availability") {
    FakeHttpClient http;
    http.serve(searchUrl, readFixture("kickassanime/search_frieren.json"));
    KickAssAnimeProvider provider(http);

    const auto shows = provider.search("frieren");

    REQUIRE(shows.size() == 3);
    CHECK(shows[0].id == "sousou-no-frieren-2d15");
    CHECK(shows[0].title == "Frieren: Beyond Journey's End");
    CHECK(shows[0].altTitle == "Sousou no Frieren");
    CHECK(shows[0].format == "TV");
    CHECK(shows[0].year == 2023);
    CHECK(shows[0].offersSub);
    CHECK(shows[0].offersDub);
    REQUIRE(http.requests.size() == 1);
    CHECK(http.requests[0].method == "POST");
    CHECK(http.requests[0].body == R"({"query":"frieren"})");
    CHECK(http.header(0, "Content-Type") == "application/json");
}

TEST_CASE("episodes lists the Japanese-audio episodes with ids that carry the show and number") {
    FakeHttpClient http;
    http.serve(jaListUrl, readFixture("kickassanime/episodes_frieren_ja.json"));
    KickAssAnimeProvider provider(http);

    const auto episodes = provider.episodes("sousou-no-frieren-2d15");

    REQUIRE(episodes.size() == 28);
    CHECK(episodes.front().id == "sousou-no-frieren-2d15|1");
    CHECK(episodes.front().number == "1");
    CHECK(episodes.front().title == "The Journey's End");
    CHECK(episodes.back().number == "28");
}

TEST_CASE("episodes follows every page of a long show") {
    FakeHttpClient http;
    http.serve("https://kaa.lt/api/show/long/episodes?ep=1&lang=ja-JP",
               R"({"current_page":1,"pages":[{"number":1},{"number":2}],)"
               R"("result":[{"slug":"a","title":"One","episode_number":1,"episode_string":"1"}]})");
    http.serve("https://kaa.lt/api/show/long/episodes?page=2&lang=ja-JP",
               R"({"current_page":2,"pages":[{"number":1},{"number":2}],)"
               R"("result":[{"slug":"b","title":"Two","episode_number":101,"episode_string":"101"}]})");
    KickAssAnimeProvider provider(http);

    const auto episodes = provider.episodes("long");

    REQUIRE(episodes.size() == 2);
    CHECK(episodes[1].id == "long|101");
    CHECK(episodes[1].title == "Two");
}

TEST_CASE("episodes falls back to the English list for dub-only shows") {
    FakeHttpClient http;
    http.serve("https://kaa.lt/api/show/dubonly/episodes?ep=1&lang=ja-JP", R"({"current_page":1,"pages":[],"result":[]})");
    http.serve("https://kaa.lt/api/show/dubonly/episodes?ep=1&lang=en-US",
               R"({"current_page":1,"pages":[{"number":1}],)"
               R"("result":[{"slug":"x","title":"Pilot","episode_number":1,"episode_string":"1"}]})");
    KickAssAnimeProvider provider(http);

    const auto episodes = provider.episodes("dubonly");

    REQUIRE(episodes.size() == 1);
    CHECK(episodes[0].title == "Pilot");
}

TEST_CASE("streams resolves the VidStreaming player into a manifest with headers, audio and subtitles") {
    FakeHttpClient http;
    http.serve(jaListUrl, readFixture("kickassanime/episodes_frieren_ja.json"));
    http.serve(jaEpisodeUrl, readFixture("kickassanime/episode_1_ja.json"));
    http.serve(jaPlayerUrl, readFixture("kickassanime/player_ja.html"));
    KickAssAnimeProvider provider(http);

    const auto streams = provider.streams("sousou-no-frieren-2d15|1", Audio::Sub);

    REQUIRE(streams.size() == 1);
    const auto& stream = streams[0];
    CHECK(stream.url == manifest);
    CHECK(stream.server == "VidStreaming");
    CHECK(stream.disguisedSegments);
    CHECK(stream.audioLanguage == "jpn,ja");
    REQUIRE(stream.headers.size() == 2);
    CHECK(stream.headers[0] == std::pair<std::string, std::string>{"Origin", "https://krussdomi.com"});
    CHECK(stream.headers[1] == std::pair<std::string, std::string>{"Referer", "https://krussdomi.com/"});
    REQUIRE(stream.subtitles.size() == 9);
    CHECK(stream.subtitles[0].language == "en");
    CHECK(stream.subtitles[0].label == "English");
    CHECK(stream.subtitles[0].isDefault);
    CHECK_FALSE(stream.subtitles[1].isDefault);
}

TEST_CASE("dub streams use the English episode list and ask for English audio without forcing subtitles") {
    FakeHttpClient http;
    http.serve(enListUrl, readFixture("kickassanime/episodes_frieren_en.json"));
    http.serve(enEpisodeUrl, readFixture("kickassanime/episode_1_en.json"));
    http.serve(enPlayerUrl, readFixture("kickassanime/player_ja.html"));
    KickAssAnimeProvider provider(http);

    const auto streams = provider.streams("sousou-no-frieren-2d15|1", Audio::Dub);

    REQUIRE(streams.size() == 1);
    CHECK(streams[0].audio == Audio::Dub);
    CHECK(streams[0].audioLanguage == "eng,en");
    CHECK(std::ranges::none_of(streams[0].subtitles, [](const Subtitle& s) { return s.isDefault; }));
}

TEST_CASE("an episode missing from the requested audio's list has no stream") {
    FakeHttpClient http;
    http.serve("https://kaa.lt/api/show/sousou-no-frieren-2d15/episodes?ep=29&lang=en-US",
               readFixture("kickassanime/episodes_frieren_en.json"));
    KickAssAnimeProvider provider(http);

    CHECK(provider.streams("sousou-no-frieren-2d15|29", Audio::Dub).empty());
}

TEST_CASE("Cloudflare challenges and a player page without props are provider errors") {
    FakeHttpClient http;
    http.serve(searchUrl, "<html><head><title>Just a moment...</title></head></html>", 403);
    http.serve(jaListUrl, readFixture("kickassanime/episodes_frieren_ja.json"));
    http.serve(jaEpisodeUrl, readFixture("kickassanime/episode_1_ja.json"));
    http.serve(jaPlayerUrl, "<html><body>nothing</body></html>");
    KickAssAnimeProvider provider(http);

    CHECK_THROWS_WITH_AS(provider.search("frieren"), doctest::Contains("Cloudflare"), ProviderError);
    CHECK_THROWS_WITH_AS(provider.streams("sousou-no-frieren-2d15|1", Audio::Sub),
                         doctest::Contains("no stream address"), ProviderError);
}
