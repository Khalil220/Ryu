#include "hls.hpp"
#include "host_repair.hpp"
#include "http.hpp"
#include "playlist_server.hpp"
#include "providers/kickassanime.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;

namespace {

bool firstSegmentIsVideo(HttpClient& http, const Stream& stream) {
    const auto master = http.get(stream.url, stream.headers);
    if (master.status != 200 || !isMasterPlaylist(master.body)) {
        return false;
    }
    const auto uris = mediaPlaylistUris(master.body, stream.url);
    if (uris.empty()) {
        return false;
    }
    const auto media = http.get(uris.back(), stream.headers);
    const auto segments = segmentUris(media.body, uris.back());
    if (segments.empty()) {
        return false;
    }
    const auto segment = http.probe(segments.front(), stream.headers);
    MESSAGE("Segment " << segments.front() << " returned " << segment.status);
    return segment.status >= 200 && segment.status < 300 && !segment.body.empty() && segment.body.front() == 0x47;
}

}

TEST_CASE("live: KickAssAnime finds Frieren and plays episode 1 subbed and dubbed") {
    CurlHttpClient http;
    PlaylistServer server;
    KickAssAnimeProvider provider(http);

    const auto shows = provider.search("frieren");
    const auto show = std::ranges::find_if(shows, [](const Show& s) { return s.altTitle == "Sousou no Frieren"; });
    REQUIRE(show != shows.end());
    const auto episodes = provider.episodes(*show);
    REQUIRE(episodes.size() == 28);

    for (const auto audio : {Audio::Sub, Audio::Dub}) {
        const auto streams = provider.streams(episodes.front().id, audio);
        REQUIRE_FALSE(streams.empty());
        const auto repaired = repairStreamHosts(http, server, streams.front());
        CHECK(firstSegmentIsVideo(http, repaired.stream));
        if (audio == Audio::Sub) {
            REQUIRE_FALSE(streams.front().subtitles.empty());
            const auto subtitle = http.get(streams.front().subtitles.front().url, streams.front().headers);
            CHECK(subtitle.status == 200);
            CHECK(subtitle.body.find("WEBVTT") != std::string::npos);
        }
    }
}
