#include "hls.hpp"
#include "host_repair.hpp"
#include "http.hpp"
#include "playlist_server.hpp"
#include "providers/anizone.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;

TEST_CASE("live: AniZone finds Frieren and plays episode 1 subbed and dubbed") {
    CurlHttpClient http;
    PlaylistServer server;
    AniZoneProvider provider(http);

    const auto shows = provider.search("frieren");
    const auto show = std::ranges::find_if(shows, [](const Show& s) { return s.altTitle == "Sousou no Frieren"; });
    REQUIRE(show != shows.end());
    CHECK(provider.describe(*show).synopsis.size() > 200);
    const auto poster = http.get(show->posterUrl);
    CHECK(poster.status == 200);
    const auto episodes = provider.episodes(*show);
    REQUIRE(episodes.size() == 28);
    CHECK(episodes.front().dubbed);

    for (const auto audio : {Audio::Sub, Audio::Dub}) {
        const auto streams = provider.streams(episodes.front().id, audio);
        REQUIRE(streams.size() == 1);
        const auto repaired = repairStreamHosts(http, server, streams.front());
        CHECK(repaired.deadHosts.empty());
        const auto master = http.get(streams.front().url);
        REQUIRE(master.status == 200);
        CHECK(master.body.find("LANGUAGE=\"ja\"") != std::string::npos);
        CHECK(master.body.find("LANGUAGE=\"en\"") != std::string::npos);
        const auto media = mediaPlaylistUris(master.body, streams.front().url);
        REQUIRE_FALSE(media.empty());
        const auto playlist = http.get(media.back());
        const auto segments = segmentUris(playlist.body, media.back());
        REQUIRE_FALSE(segments.empty());
        const auto segment = http.probe(segments.front());
        MESSAGE("Segment " << segments.front() << " returned " << segment.status);
        CHECK(segment.status >= 200);
        CHECK(segment.status < 300);
        const auto keyStart = playlist.body.find("URI=\"");
        REQUIRE(keyStart != std::string::npos);
        const auto keyUri = playlist.body.substr(keyStart + 5, playlist.body.find('"', keyStart + 5) - keyStart - 5);
        const auto key = http.get(resolveUrl(media.back(), keyUri));
        CHECK(key.status == 200);
        CHECK(key.body.size() == 16);
        if (audio == Audio::Sub) {
            REQUIRE_FALSE(streams.front().subtitles.empty());
            const auto subtitle = http.get(streams.front().subtitles.front().url);
            CHECK(subtitle.status == 200);
            CHECK(subtitle.body.find("[Script Info]") != std::string::npos);
        }
    }
}

TEST_CASE("live: AniZone lists every episode of a long show") {
    CurlHttpClient http;
    AniZoneProvider provider(http);

    const auto shows = provider.search("one piece");
    const auto show = std::ranges::find_if(shows, [](const Show& s) { return s.title == "One Piece" && s.format == "TV"; });
    REQUIRE(show != shows.end());
    const auto episodes = provider.episodes(*show);
    CHECK(episodes.size() >= 1180);
    CHECK(episodes.front().number == "1");
}
