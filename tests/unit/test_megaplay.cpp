#include "fake_http.hpp"
#include "resolvers/megaplay.hpp"

#include <doctest/doctest.h>

using namespace ryu;
using ryu::test::FakeHttpClient;
using ryu::test::readFixture;

namespace {

constexpr const char* embedUrl = "https://megaplay.buzz/stream/s-2/107257/sub";
constexpr const char* sourcesUrl = "https://megaplay.buzz/stream/getSources?id=13461";
constexpr const char* scriptUrl = "https://megaplay.buzz/lib/newclient.min.js?v=4.20";
constexpr const char* masterUrl =
    "https://fetch.nexabloom.top/anime/bb6d2babd7797d94d8f4a8600bc9b44e/b7d51fb7e838ee9b60dcdb34b953bc07/master.m3u8";

void serveEpisode(FakeHttpClient& http) {
    http.serve(embedUrl, readFixture("megaplay/stream_107257_sub.html"));
    http.serve(sourcesUrl, readFixture("megaplay/get_sources_13461.json"));
    http.serve(scriptUrl, readFixture("megaplay/newclient.min.js"));
}

size_t countRequests(const FakeHttpClient& http, const std::string& url) {
    size_t count = 0;
    for (const auto& request : http.requests) {
        count += request.url == url ? 1 : 0;
    }
    return count;
}

}

TEST_CASE("handles only megaplay stream embeds") {
    CHECK(MegaplayResolver::handles("https://megaplay.buzz/stream/s-2/107257/sub"));
    CHECK_FALSE(MegaplayResolver::handles("https://megaplay.buzz/videojs/stream/s-2/233066/sub"));
    CHECK_FALSE(MegaplayResolver::handles("https://zokoanime.video/stream/mal/52991/1/sub"));
}

TEST_CASE("subtitle languages come from the three-letter code in the file name") {
    CHECK(MegaplayResolver::languageFromFile("https://x.example/a/subtitles/eng-2.vtt") == "eng");
    CHECK(MegaplayResolver::languageFromFile("https://x.example/a/subtitles/track_0_CR_English_eng.vtt") == "eng");
    CHECK(MegaplayResolver::languageFromFile("https://x.example/a/subtitles/spa-4.vtt") == "spa");
    CHECK(MegaplayResolver::languageFromFile("https://x.example/a/subtitles/English.vtt").empty());
}

TEST_CASE("resolve decrypts the stream address with the key from megaplay's own script") {
    FakeHttpClient http;
    serveEpisode(http);
    MegaplayResolver resolver(http);

    const auto stream = resolver.resolve(embedUrl, Audio::Sub, "https://hianime.at/");

    CHECK(stream.url == masterUrl);
    CHECK(stream.audio == Audio::Sub);
    CHECK(stream.disguisedSegments);
    REQUIRE(stream.headers.size() == 1);
    CHECK(stream.headers[0].first == "Referer");
    CHECK(stream.headers[0].second == "https://megaplay.buzz/");
    CHECK(http.header(0, "Referer") == "https://hianime.at/");
}

TEST_CASE("resolve asks for sources the way megaplay's player does") {
    FakeHttpClient http;
    serveEpisode(http);
    MegaplayResolver resolver(http);
    resolver.resolve(embedUrl, Audio::Sub, "https://hianime.at/");

    size_t index = 0;
    while (http.requests.at(index).url != sourcesUrl) {
        ++index;
    }
    CHECK(http.header(index, "X-Requested-With") == "XMLHttpRequest");
    CHECK(http.header(index, "Referer") == embedUrl);
}

TEST_CASE("resolve lists every subtitle track with its language and the default") {
    FakeHttpClient http;
    serveEpisode(http);
    MegaplayResolver resolver(http);

    const auto stream = resolver.resolve(embedUrl, Audio::Sub, "https://hianime.at/");

    REQUIRE(stream.subtitles.size() == 9);
    CHECK(stream.subtitles[0].label == "Arabic");
    CHECK(stream.subtitles[0].language == "ara");
    CHECK_FALSE(stream.subtitles[0].isDefault);
    CHECK(stream.subtitles[1].label == "English");
    CHECK(stream.subtitles[1].language == "eng");
    CHECK(stream.subtitles[1].isDefault);
    CHECK(stream.subtitles[1].url ==
          "https://4driv.onehpanddreaming.site/anime/bb6d2babd7797d94d8f4a8600bc9b44e/"
          "b7d51fb7e838ee9b60dcdb34b953bc07/subtitles/eng-2.vtt");
}

TEST_CASE("the decryption script is fetched once and reused") {
    FakeHttpClient http;
    serveEpisode(http);
    MegaplayResolver resolver(http);

    resolver.resolve(embedUrl, Audio::Sub, "https://hianime.at/");
    resolver.resolve(embedUrl, Audio::Sub, "https://hianime.at/");

    CHECK(countRequests(http, scriptUrl) == 1);
    CHECK(countRequests(http, sourcesUrl) == 2);
}

TEST_CASE("a missing episode page is reported as having no stream") {
    FakeHttpClient http;
    http.serve(embedUrl, "<html><head><title>Error - MegaPlay</title></head><body>Error Code 404</body></html>");
    MegaplayResolver resolver(http);

    CHECK_THROWS_WITH_AS(resolver.resolve(embedUrl, Audio::Sub, "https://hianime.at/"),
                         doctest::Contains("no stream"), ProviderError);
}

TEST_CASE("a script without the key is reported clearly") {
    FakeHttpClient http;
    serveEpisode(http);
    http.serve(scriptUrl, "console.log('nothing here');");
    MegaplayResolver resolver(http);

    CHECK_THROWS_WITH_AS(resolver.resolve(embedUrl, Audio::Sub, "https://hianime.at/"),
                         doctest::Contains("decryption key"), ProviderError);
}

TEST_CASE("a rotated key that no longer decrypts is reported clearly") {
    FakeHttpClient http;
    serveEpisode(http);
    auto script = readFixture("megaplay/newclient.min.js");
    const auto at = script.find("i?LMTAx0Q6,:}50U");
    REQUIRE(at != std::string::npos);
    script.replace(at, 16, "0000000000000000");
    http.serve(scriptUrl, script);
    MegaplayResolver resolver(http);

    CHECK_THROWS_WITH_AS(resolver.resolve(embedUrl, Audio::Sub, "https://hianime.at/"),
                         doctest::Contains("decrypt"), ProviderError);
}
