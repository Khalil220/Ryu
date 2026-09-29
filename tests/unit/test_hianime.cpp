#include "fake_http.hpp"
#include "providers/hianime.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;
using ryu::test::FakeHttpClient;
using ryu::test::readFixture;

namespace {

constexpr const char* searchUrl = "https://hianime.at/search?keyword=frieren";
constexpr const char* episodesUrl = "https://hianime.at/api/theme/episode/list/481";
constexpr const char* serversUrl = "https://hianime.at/api/theme/episode/servers?episodeId=9227";
constexpr const char* subEmbedUrl = "https://zokoanime.video/stream/mal/52991/1/sub";
constexpr const char* dubEmbedUrl = "https://zokoanime.video/stream/mal/52991/1/dub";
constexpr const char* masterUrl =
    "https://hls.1embed.buzz/v/scxqicy/55ejtphmn9/e38a115ezb/dnfinmtr3wdbax/master.m3u8";

std::string serversJson(const std::string& items) {
    std::string escaped;
    for (const char ch : items) {
        if (ch == '"') {
            escaped += "\\\"";
        } else {
            escaped += ch;
        }
    }
    return "{\"status\":true,\"html\":\"" + escaped + "\"}";
}

}

TEST_CASE("search parses every result in the main list") {
    FakeHttpClient http;
    http.serve(searchUrl, readFixture("hianime/search_frieren.html"));
    HiAnimeProvider provider(http);

    const auto shows = provider.search("frieren");

    REQUIRE(shows.size() == 4);
    CHECK(shows[0].id == "481");
    CHECK(shows[0].title == "Frieren: Beyond Journey's End");
    CHECK(shows[0].altTitle == "Sousou no Frieren");
    CHECK(shows[0].format == "TV");
    CHECK(shows[0].subEpisodes == 28);
    CHECK(shows[0].dubEpisodes == 28);
    CHECK(shows[1].id == "808");
    CHECK(shows[1].subEpisodes == 10);
    CHECK(shows[2].format == "ONA");
    CHECK(shows[3].id == "7220");
    CHECK(shows[3].title == "Sousou no Frieren 3rd Season");
    CHECK(shows[3].subEpisodes == 0);
    CHECK(shows[3].dubEpisodes == 0);
}

TEST_CASE("search encodes the query and returns nothing for an empty result page") {
    FakeHttpClient http;
    http.serve("https://hianime.at/search?keyword=one%20piece%3F", "<div id=\"main-content\"></div>");
    HiAnimeProvider provider(http);

    CHECK(provider.search("one piece?").empty());
    CHECK(http.requests.size() == 1);
}

TEST_CASE("a base URL override is used and its trailing slash ignored") {
    FakeHttpClient http;
    http.serve("https://mirror.example/search?keyword=x", "<div id=\"main-content\"></div>");
    HiAnimeProvider provider(http, "https://mirror.example/");

    CHECK(provider.search("x").empty());
    CHECK(http.requests.at(0).url == "https://mirror.example/search?keyword=x");
}

TEST_CASE("episodes parses ids, numbers and titles from the list API") {
    FakeHttpClient http;
    http.serve(episodesUrl, readFixture("hianime/episodes_481.json"));
    HiAnimeProvider provider(http);

    const auto episodes = provider.episodes(Show{"481", "Frieren: Beyond Journey's End", "", "TV", 28, 28});

    REQUIRE(episodes.size() == 28);
    CHECK(episodes.front().subbed);
    CHECK(episodes.back().dubbed);
    CHECK(episodes.front().id == "9227");
    CHECK(episodes.front().number == "1");
    CHECK(episodes.front().title == "The Journey's End");
    CHECK(episodes.back().id == "9254");
    CHECK(episodes.back().number == "28");
    CHECK(episodes.back().title == "It Would Be Embarrassing When We Met Again");
}

TEST_CASE("episodes marks only the first dubbed-count episodes as dubbed") {
    FakeHttpClient http;
    http.serve(episodesUrl, readFixture("hianime/episodes_481.json"));
    HiAnimeProvider provider(http);

    const auto episodes = provider.episodes(Show{"481", "Frieren", "", "TV", 27, 25});

    REQUIRE(episodes.size() == 28);
    CHECK(episodes[24].dubbed);
    CHECK_FALSE(episodes[25].dubbed);
    CHECK(episodes[26].subbed);
    CHECK_FALSE(episodes[27].subbed);
    CHECK(episodes[27].availableIn(Audio::Sub) == false);
}

TEST_CASE("episodes treats every episode as available when the show has no counts") {
    FakeHttpClient http;
    http.serve(episodesUrl, readFixture("hianime/episodes_481.json"));
    HiAnimeProvider provider(http);

    const auto episodes = provider.episodes(Show{"481"});

    REQUIRE(episodes.size() == 28);
    CHECK(std::ranges::all_of(episodes, [](const Episode& e) { return e.subbed && e.dubbed; }));
}

TEST_CASE("streams resolves the ZokoAnime embed into a master playlist with headers and subtitles") {
    FakeHttpClient http;
    http.serve(serversUrl, readFixture("hianime/servers_9227.json"));
    http.serve(subEmbedUrl, readFixture("hianime/embed_zoko.html"));
    HiAnimeProvider provider(http);

    const auto streams = provider.streams("9227", Audio::Sub);

    REQUIRE(streams.size() == 1);
    const auto& stream = streams.front();
    CHECK(stream.url == masterUrl);
    CHECK(stream.server == "ZokoAnime");
    CHECK(stream.audio == Audio::Sub);
    REQUIRE(stream.headers.size() == 1);
    CHECK(stream.headers[0].first == "Referer");
    CHECK(stream.headers[0].second == "https://zokoanime.video/");
    REQUIRE(stream.subtitles.size() == 1);
    CHECK(stream.subtitles[0].language == "en");
    CHECK(stream.subtitles[0].label == "English");
    CHECK(stream.subtitles[0].isDefault);
    CHECK(stream.subtitles[0].url ==
          "https://hls.1embed.buzz/v/scxqicy/55ejtphmn9/e38a115ezb/dnfinmtr3wdbax/subs/nzo9j4s07a98qkl1.vtt");

    REQUIRE(http.requests.size() == 2);
    CHECK(http.requests[1].url == subEmbedUrl);
    CHECK(http.header(1, "Referer") == "https://hianime.at/");
}

TEST_CASE("streams picks the dub embed when dub audio is requested") {
    FakeHttpClient http;
    http.serve(serversUrl, readFixture("hianime/servers_9227.json"));
    http.serve(dubEmbedUrl, readFixture("hianime/embed_zoko.html"));
    HiAnimeProvider provider(http);

    const auto streams = provider.streams("9227", Audio::Dub);

    REQUIRE(streams.size() == 1);
    CHECK(streams[0].audio == Audio::Dub);
    CHECK(http.requests.at(1).url == dubEmbedUrl);
}

TEST_CASE("streams skips servers it cannot resolve") {
    FakeHttpClient http;
    http.serve(serversUrl,
               serversJson(R"(<div class="item server-item" data-type="sub" data-server-name="HD-1" )"
                           R"(data-hash="aHR0cHM6Ly9leGFtcGxlLmNvbQ=="></div>)"));
    HiAnimeProvider provider(http);

    CHECK(provider.streams("9227", Audio::Sub).empty());
    CHECK(http.requests.size() == 1);
}

TEST_CASE("streams reports an embed page without a player config") {
    FakeHttpClient http;
    http.serve(serversUrl, readFixture("hianime/servers_9227.json"));
    http.serve(subEmbedUrl, "<html><body>No player here</body></html>");
    HiAnimeProvider provider(http);

    CHECK_THROWS_AS(provider.streams("9227", Audio::Sub), ProviderError);
}

TEST_CASE("a Cloudflare challenge page is reported as a provider error") {
    FakeHttpClient http;
    http.serve(searchUrl, "<html><head><title>Just a moment...</title></head></html>", 403);
    HiAnimeProvider provider(http);

    CHECK_THROWS_WITH_AS(provider.search("frieren"), doctest::Contains("Cloudflare"), ProviderError);
}

TEST_CASE("HTTP errors and failed API responses are provider errors") {
    FakeHttpClient http;
    http.serve(searchUrl, "gone", 404);
    http.serve(episodesUrl, "{\"status\":false}");
    http.serve(serversUrl, "not json");
    HiAnimeProvider provider(http);

    CHECK_THROWS_WITH_AS(provider.search("frieren"), doctest::Contains("HTTP 404"), ProviderError);
    CHECK_THROWS_AS(provider.episodes(Show{"481"}), ProviderError);
    CHECK_THROWS_AS(provider.streams("9227", Audio::Sub), ProviderError);
}

TEST_CASE("streams resolves megaplay servers when ZokoAnime is not offered") {
    FakeHttpClient http;
    http.serve(serversUrl, readFixture("hianime/servers_9227_megaplay.json"));
    http.serve("https://megaplay.buzz/stream/s-2/107257/sub", readFixture("megaplay/stream_107257_sub.html"));
    http.serve("https://megaplay.buzz/stream/getSources?id=13461", readFixture("megaplay/get_sources_13461.json"));
    http.serve("https://megaplay.buzz/lib/newclient.min.js?v=4.20", readFixture("megaplay/newclient.min.js"));
    HiAnimeProvider provider(http);

    const auto streams = provider.streams("9227", Audio::Sub);

    REQUIRE(streams.size() == 1);
    CHECK(streams[0].server == "Vidstream-2");
    CHECK(streams[0].url.ends_with("/master.m3u8"));
    CHECK(streams[0].disguisedSegments);
    CHECK(http.header(1, "Referer") == "https://hianime.at/");
}

TEST_CASE("streams keeps working servers when another one fails") {
    FakeHttpClient http;
    http.serve(serversUrl,
               serversJson(R"(<div class="item server-item" data-type="sub" data-server-name="Vidstream-2" )"
                           R"(data-hash="aHR0cHM6Ly9tZWdhcGxheS5idXp6L3N0cmVhbS9zLTIvMS9zdWI="></div>)"
                           R"(<div class="item server-item" data-type="sub" data-server-name="ZokoAnime" )"
                           R"(data-hash="aHR0cHM6Ly96b2tvYW5pbWUudmlkZW8vc3RyZWFtL21hbC81Mjk5MS8xL3N1Yg=="></div>)"));
    http.serve("https://megaplay.buzz/stream/s-2/1/sub", "gone", 404);
    http.serve(subEmbedUrl, readFixture("hianime/embed_zoko.html"));
    HiAnimeProvider provider(http);

    const auto streams = provider.streams("9227", Audio::Sub);

    REQUIRE(streams.size() == 1);
    CHECK(streams[0].server == "ZokoAnime");
}

TEST_CASE("streams reports the failure when every server fails") {
    FakeHttpClient http;
    http.serve(serversUrl,
               serversJson(R"(<div class="item server-item" data-type="sub" data-server-name="Vidstream-2" )"
                           R"(data-hash="aHR0cHM6Ly9tZWdhcGxheS5idXp6L3N0cmVhbS9zLTIvMS9zdWI="></div>)"));
    http.serve("https://megaplay.buzz/stream/s-2/1/sub", "gone", 404);
    HiAnimeProvider provider(http);

    CHECK_THROWS_WITH_AS(provider.streams("9227", Audio::Sub), doctest::Contains("no stream"), ProviderError);
}
