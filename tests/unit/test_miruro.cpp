#include "fake_http.hpp"
#include "providers/miruro.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;
using ryu::test::FakeHttpClient;
using ryu::test::readFixture;

namespace {

constexpr const char* frierenId = "o2Eqmv4w0JYgQJWQFi9CDDdH-PgffNpm";
constexpr const char* searchUrl = "https://www.miruro.tv/api/v1/anime?q=frieren&limit=15&sort=-popularity";
constexpr const char* regularUrl =
    "https://www.miruro.tv/api/v1/anime/o2Eqmv4w0JYgQJWQFi9CDDdH-PgffNpm/episodes?kind=regular&limit=10000";
constexpr const char* filmUrl =
    "https://www.miruro.tv/api/v1/anime/o2Eqmv4w0JYgQJWQFi9CDDdH-PgffNpm/episodes?kind=film&limit=10000";
constexpr const char* playUrl = "https://www.miruro.tv/api/v1/anime/o2Eqmv4w0JYgQJWQFi9CDDdH-PgffNpm/episodes/1/play";

std::string header(const Stream& stream, const std::string& name) {
    const auto it = std::ranges::find(stream.headers, name, &std::pair<std::string, std::string>::first);
    return it == stream.headers.end() ? std::string() : it->second;
}

}

TEST_CASE("decodeMiruroResponse undoes Miruro's XOR and gzip") {
    const auto decoded = decodeMiruroResponse(readFixture("miruro/search_frieren.bin"));
    CHECK(decoded.starts_with("{"));
    CHECK(decoded.find(frierenId) != std::string::npos);
    CHECK(decodeMiruroResponse(R"({"data":[]})") == R"({"data":[]})");
    CHECK_THROWS_AS(decodeMiruroResponse("not a response"), ProviderError);
}

TEST_CASE("Miruro search reads titles, format, year, audio counts and cover") {
    FakeHttpClient http;
    http.serve(searchUrl, readFixture("miruro/search_frieren.bin"));
    MiruroProvider provider(http);

    const auto shows = provider.search("frieren");

    REQUIRE(shows.size() >= 2);
    CHECK(shows[0].id == frierenId);
    CHECK(shows[0].title == "Frieren: Beyond Journey's End");
    CHECK(shows[0].altTitle == "Sousou no Frieren");
    CHECK(shows[0].format == "TV");
    CHECK(shows[0].year == 2023);
    CHECK(shows[0].subEpisodes == 28);
    CHECK(shows[0].dubEpisodes == 28);
    CHECK(shows[0].posterUrl.starts_with("https://s4.anilist.co/"));
    CHECK_FALSE(shows[0].synopsis.empty());
    CHECK(http.header(0, "Referer") == "https://www.miruro.tv/");
}

TEST_CASE("Miruro episodes come from the regular list with ids that carry the show and number") {
    FakeHttpClient http;
    http.serve(regularUrl, readFixture("miruro/episodes_frieren.json"));
    MiruroProvider provider(http);
    Show show{frierenId};
    show.subEpisodes = 28;
    show.dubEpisodes = 28;

    const auto episodes = provider.episodes(show);

    REQUIRE(episodes.size() == 28);
    CHECK(episodes[0].id == std::string(frierenId) + "|1");
    CHECK(episodes[0].number == "1");
    CHECK(episodes[0].title == "The Journey's End");
    CHECK(std::ranges::all_of(episodes, [](const Episode& e) { return e.subbed && e.dubbed; }));
}

TEST_CASE("Miruro episodes fall back to the film list for movies") {
    FakeHttpClient http;
    http.serve(regularUrl, R"({"data":[],"next_cursor":null,"has_more":false})");
    http.serve(filmUrl, R"({"data":[{"episode_number":1,"kind":"film","title":"Your Name."}],"has_more":false})");
    MiruroProvider provider(http);

    const auto episodes = provider.episodes(Show{frierenId});

    REQUIRE(episodes.size() == 1);
    CHECK(episodes[0].number == "1");
    CHECK(episodes[0].title == "Your Name.");
}

TEST_CASE("Miruro subbed streams use only soft-subtitle tracks, own mirror first") {
    FakeHttpClient http;
    http.serve(playUrl, readFixture("miruro/play_frieren_1.json"));
    MiruroProvider provider(http);

    const auto streams = provider.streams(std::string(frierenId) + "|1", Audio::Sub);

    REQUIRE(streams.size() >= 3);
    CHECK(streams[0].server.starts_with("icarus "));
    CHECK(header(streams[0], "Origin") == "https://ultracloud.cc");
    CHECK(header(streams[0], "Referer") == "https://ultracloud.cc/");
    CHECK(streams[0].disguisedSegments);
    CHECK(streams[0].audioLanguage == "jpn,ja,Japanese");
    CHECK(std::ranges::none_of(streams, [](const Stream& s) {
        return s.server.starts_with("animepahe") || s.server.starts_with("aniwaves Vidplay");
    }));
    const auto& subtitles = streams[0].subtitles;
    REQUIRE_FALSE(subtitles.empty());
    const auto english = std::ranges::find(subtitles, "English", &Subtitle::label);
    REQUIRE(english != subtitles.end());
    CHECK(english->isDefault);
    CHECK(std::ranges::none_of(subtitles, [](const Subtitle& s) { return s.label.ends_with(".vtt"); }));
    CHECK(streams.back().server.starts_with("kickassanime "));
    CHECK(streams.back().subtitleNoise == std::vector<std::string>{"kaa.lt"});
}

TEST_CASE("Miruro dubbed streams rank its mirror and aniwaves ahead of megaplay and leave subtitles off") {
    FakeHttpClient http;
    http.serve(playUrl, readFixture("miruro/play_frieren_1.json"));
    MiruroProvider provider(http);

    const auto streams = provider.streams(std::string(frierenId) + "|1", Audio::Dub);

    REQUIRE(streams.size() >= 4);
    CHECK(streams[0].server.starts_with("icarus "));
    const auto aniwaves = std::ranges::find_if(streams, [](const Stream& s) { return s.server.starts_with("aniwaves "); });
    const auto anikoto = std::ranges::find_if(streams, [](const Stream& s) { return s.server.starts_with("anikoto "); });
    REQUIRE(aniwaves != streams.end());
    REQUIRE(anikoto != streams.end());
    CHECK(aniwaves < anikoto);
    CHECK_FALSE(anikoto->alternateHosts.empty());
    CHECK(streams[0].audioLanguage == "eng,en,English");
    CHECK(std::ranges::none_of(streams, [](const Stream& s) {
        return std::ranges::any_of(s.subtitles, &Subtitle::isDefault);
    }));
    CHECK(std::ranges::none_of(streams, [](const Stream& s) { return s.server.starts_with("animepahe"); }));
}

TEST_CASE("Miruro rejects an episode id it didn't make") {
    FakeHttpClient http;
    MiruroProvider provider(http);
    CHECK_THROWS_AS(provider.streams("9227", Audio::Sub), ProviderError);
}
