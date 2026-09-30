#include "hls.hpp"
#include "host_repair.hpp"
#include "http.hpp"
#include "playlist_server.hpp"
#include "providers/miruro.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;

namespace {

bool firstSegmentLoads(HttpClient& http, const Stream& stream) {
    const auto master = http.get(stream.url, stream.headers);
    if (master.status != 200 || master.body.find("#EXTM3U") == std::string::npos) {
        return false;
    }
    auto media = stream.url;
    auto playlist = master.body;
    if (isMasterPlaylist(master.body)) {
        const auto uris = mediaPlaylistUris(master.body, stream.url);
        if (uris.empty()) {
            return false;
        }
        media = uris.back();
        playlist = http.get(media, stream.headers).body;
    }
    const auto segments = segmentUris(playlist, media);
    if (segments.empty()) {
        return false;
    }
    const auto segment = http.probe(segments.front(), stream.headers);
    MESSAGE(stream.server << " segment " << segments.front() << " returned " << segment.status);
    return segment.status >= 200 && segment.status < 300 && !segment.body.empty();
}

}

TEST_CASE("live: Miruro finds Frieren and serves episode 1 with soft subtitles and a dub") {
    CurlHttpClient http;
    PlaylistServer server;
    MiruroProvider provider(http);

    const auto shows = provider.search("frieren");
    const auto show = std::ranges::find_if(shows, [](const Show& s) { return s.altTitle == "Sousou no Frieren"; });
    REQUIRE(show != shows.end());
    CHECK(show->subEpisodes >= 28);
    const auto episodes = provider.episodes(*show);
    REQUIRE(episodes.size() >= 28);

    for (const auto audio : {Audio::Sub, Audio::Dub}) {
        const auto streams = provider.streams(episodes.front().id, audio);
        REQUIRE_FALSE(streams.empty());
        const auto playable = std::ranges::any_of(streams, [&](const Stream& stream) {
            try {
                return firstSegmentLoads(http, repairStreamHosts(http, server, stream).stream);
            } catch (const std::exception& error) {
                MESSAGE(stream.server << " failed: " << error.what());
                return false;
            }
        });
        CHECK(playable);
        if (audio == Audio::Sub) {
            CHECK(std::ranges::any_of(streams.front().subtitles, &Subtitle::isDefault));
        }
    }
}

TEST_CASE("live: Miruro lists a movie's single episode") {
    CurlHttpClient http;
    MiruroProvider provider(http);

    const auto shows = provider.search("your name");
    const auto movie = std::ranges::find_if(shows, [](const Show& s) { return s.format == "MOVIE"; });
    REQUIRE(movie != shows.end());
    CHECK(provider.episodes(*movie).size() == 1);
}
