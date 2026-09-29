#include "fake_http.hpp"
#include "playlist_server.hpp"
#include "stream_finder.hpp"

#include <doctest/doctest.h>

#include <functional>
#include <map>

using namespace ryu;
using ryu::test::FakeHttpClient;

namespace {

class ScriptedProvider : public Provider {
public:
    std::map<std::string, std::vector<Show>> searchResults;
    std::map<std::string, std::vector<Episode>> episodeLists;
    std::function<std::vector<Stream>(std::string_view, Audio)> onStreams = [](std::string_view, Audio) {
        return std::vector<Stream>{};
    };
    std::vector<std::string> searches;

    std::vector<Show> search(std::string_view query) override {
        searches.emplace_back(query);
        const auto it = searchResults.find(std::string(query));
        return it == searchResults.end() ? std::vector<Show>{} : it->second;
    }
    std::vector<Episode> episodes(const Show& show) override { return episodeLists[show.id]; }
    std::vector<Stream> streams(std::string_view episodeId, Audio audio) override { return onStreams(episodeId, audio); }
};

Stream streamAt(const std::string& url) {
    Stream stream;
    stream.url = url;
    return stream;
}

const Show frieren{"481", "Frieren: Beyond Journey's End", "Sousou no Frieren", "TV", 28, 28};
const Episode episodeTwo{"9228", "2", "It Didn't Have to Be Magic..."};

}

TEST_CASE("normalizeTitle ignores case, punctuation and spacing") {
    CHECK(normalizeTitle("Frieren: Beyond Journey's End") == "frieren beyond journey s end");
    CHECK(normalizeTitle("  Sousou  no FRIEREN!! ") == "sousou no frieren");
    CHECK(normalizeTitle("Re:Zero") == "re zero");
}

TEST_CASE("matchShow matches on either title and prefers the same format") {
    const Show movie{"m", "Sousou no Frieren", "", "MOVIE"};
    const Show tv{"t", "Frieren - Beyond Journey's End", "Sousou no Frieren", "tv"};
    const Show other{"o", "Frieren Mini Anime", "", "ONA"};

    const auto match = matchShow(frieren, {movie, other, tv});
    REQUIRE(match);
    CHECK(match->id == "t");
    CHECK(matchShow(frieren, {movie})->id == "m");
    CHECK_FALSE(matchShow(frieren, {other}).has_value());
}

TEST_CASE("sameEpisodeNumber compares numerically when it can") {
    CHECK(sameEpisodeNumber("1", "01"));
    CHECK(sameEpisodeNumber("12.5", "12.5"));
    CHECK_FALSE(sameEpisodeNumber("12", "12.5"));
    CHECK(sameEpisodeNumber("SP", "SP"));
}

TEST_CASE("the primary provider's stream is used when it works") {
    FakeHttpClient http;
    PlaylistServer server;
    ScriptedProvider primary;
    ScriptedProvider backup;
    primary.onStreams = [](std::string_view id, Audio) {
        CHECK(id == "9228");
        return std::vector<Stream>{streamAt("https://primary.example/master.m3u8")};
    };

    const auto found = findStream(http, server, {{"HiAnime", &primary}, {"KickAssAnime", &backup}}, frieren,
                                  episodeTwo, Audio::Sub);

    CHECK(found.providerName == "HiAnime");
    CHECK_FALSE(found.fromFallback);
    CHECK(found.stream.url == "https://primary.example/master.m3u8");
    CHECK(backup.searches.empty());
}

TEST_CASE("an episode the primary cannot play is found on the fallback by title and number") {
    FakeHttpClient http;
    PlaylistServer server;
    ScriptedProvider primary;
    ScriptedProvider backup;
    backup.searchResults["Frieren: Beyond Journey's End"] = {
        {"mini", "Frieren: Beyond Journey's End Mini Anime", "", "ONA"},
        {"sousou-no-frieren-2d15", "Frieren: Beyond Journey's End", "Sousou no Frieren", "TV"}};
    backup.episodeLists["sousou-no-frieren-2d15"] = {{"s|1", "1", "One"}, {"s|2", "2", "Two"}};
    backup.onStreams = [](std::string_view id, Audio audio) {
        CHECK(id == "s|2");
        CHECK(audio == Audio::Dub);
        return std::vector<Stream>{streamAt("https://backup.example/master.m3u8")};
    };

    const auto found = findStream(http, server, {{"HiAnime", &primary}, {"KickAssAnime", &backup}}, frieren,
                                  episodeTwo, Audio::Dub);

    CHECK(found.providerName == "KickAssAnime");
    CHECK(found.fromFallback);
    CHECK(found.stream.url == "https://backup.example/master.m3u8");
}

TEST_CASE("the fallback also searches the other title when the first finds nothing") {
    FakeHttpClient http;
    PlaylistServer server;
    ScriptedProvider primary;
    ScriptedProvider backup;
    backup.searchResults["Sousou no Frieren"] = {{"x", "Sousou no Frieren", "", "TV"}};
    backup.episodeLists["x"] = {{"x|2", "02", "Two"}};
    backup.onStreams = [](std::string_view, Audio) {
        return std::vector<Stream>{streamAt("https://backup.example/x.m3u8")};
    };

    const auto found =
        findStream(http, server, {{"HiAnime", &primary}, {"Backup", &backup}}, frieren, episodeTwo, Audio::Sub);

    CHECK(found.providerName == "Backup");
    CHECK(backup.searches == std::vector<std::string>{"Frieren: Beyond Journey's End", "Sousou no Frieren"});
}

TEST_CASE("a primary that throws or serves only dead hosts hands over to the fallback") {
    FakeHttpClient http;
    http.serve("https://dead.example/master.m3u8", "#EXTM3U\n#EXTINF:10,\nhttps://gone.example/seg-1.jpg\n");
    http.serveRedirect("https://gone.example/seg-1.jpg", "https://www.cloudflare-terms-of-service-abuse.com/x");
    PlaylistServer server;
    ScriptedProvider dead;
    dead.onStreams = [](std::string_view, Audio) {
        return std::vector<Stream>{streamAt("https://dead.example/master.m3u8")};
    };
    ScriptedProvider broken;
    broken.onStreams = [](std::string_view, Audio) -> std::vector<Stream> { throw ProviderError("site is down"); };
    ScriptedProvider backup;
    backup.searchResults["Frieren: Beyond Journey's End"] = {frieren};
    backup.episodeLists["481"] = {episodeTwo};
    backup.onStreams = [](std::string_view, Audio) {
        return std::vector<Stream>{streamAt("https://backup.example/master.m3u8")};
    };

    CHECK(findStream(http, server, {{"Dead", &dead}, {"Backup", &backup}}, frieren, episodeTwo, Audio::Sub)
              .providerName == "Backup");
    CHECK(findStream(http, server, {{"Broken", &broken}, {"Backup", &backup}}, frieren, episodeTwo, Audio::Sub)
              .providerName == "Backup");
}

TEST_CASE("when nobody has the episode the error says what is missing, or why it failed") {
    FakeHttpClient http;
    PlaylistServer server;
    ScriptedProvider primary;
    ScriptedProvider backup;

    CHECK_THROWS_WITH_AS(
        findStream(http, server, {{"HiAnime", &primary}, {"Backup", &backup}}, frieren, episodeTwo, Audio::Dub),
        "No dubbed stream is available for this episode.", ProviderError);

    ScriptedProvider broken;
    broken.onStreams = [](std::string_view, Audio) -> std::vector<Stream> { throw ProviderError("site is down"); };
    CHECK_THROWS_WITH_AS(
        findStream(http, server, {{"HiAnime", &broken}, {"Backup", &backup}}, frieren, episodeTwo, Audio::Sub),
        "HiAnime: site is down", ProviderError);
}
