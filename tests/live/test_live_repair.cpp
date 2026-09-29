#include "hls.hpp"
#include "host_repair.hpp"
#include "http.hpp"
#include "playlist_server.hpp"
#include "providers/hianime.hpp"

#include <doctest/doctest.h>

using namespace ryu;

TEST_CASE("live: a megaplay episode repaired onto a working host plays from the first segment") {
    CurlHttpClient http;
    PlaylistServer server;
    HiAnimeProvider provider(http);

    const auto streams = provider.streams("9227", Audio::Sub);
    REQUIRE_FALSE(streams.empty());
    const auto report = repairStreamHosts(http, server, streams.front());
    for (const auto& host : report.deadHosts) {
        MESSAGE("Dead host: " << host);
    }
    if (!report.deadHosts.empty()) {
        MESSAGE("Replacement: " << report.replacementHost);
    }

    const auto& stream = report.stream;
    const auto master = http.get(stream.url, stream.headers);
    REQUIRE(master.status == 200);
    const auto mediaUrl = isMasterPlaylist(master.body) ? mediaPlaylistUris(master.body, stream.url).front()
                                                        : stream.url;
    const auto media = http.get(mediaUrl, stream.headers);
    REQUIRE(media.status == 200);
    const auto segments = segmentUris(media.body, mediaUrl);
    REQUIRE_FALSE(segments.empty());
    const auto first = http.probe(segments.front(), stream.headers);
    CHECK(first.status == 200);
    REQUIRE_FALSE(first.body.empty());
    CHECK(first.body.front() == 0x47);
}
