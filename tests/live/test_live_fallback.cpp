#include "http.hpp"
#include "playlist_server.hpp"
#include "providers/anizone.hpp"
#include "providers/hianime.hpp"
#include "stream_finder.hpp"

#include <doctest/doctest.h>

using namespace ryu;

namespace {

class EmptyProvider : public Provider {
public:
    std::vector<Show> search(std::string_view) override { return {}; }
    std::vector<Episode> episodes(const Show&) override { return {}; }
    std::vector<Stream> streams(std::string_view, Audio) override { return {}; }
};

}

TEST_CASE("live: a HiAnime episode that the primary cannot play is found on AniZone") {
    CurlHttpClient http;
    PlaylistServer server;
    HiAnimeProvider hianime(http);
    AniZoneProvider anizone(http);
    EmptyProvider nothing;

    const auto shows = hianime.search("frieren");
    REQUIRE_FALSE(shows.empty());
    const auto& show = shows.front();
    const auto episodes = hianime.episodes(show);
    REQUIRE(episodes.size() > 1);

    const auto found = findStream(http, server, {{"Nothing", &nothing}, {"AniZone", &anizone}}, show, episodes[1],
                                  Audio::Dub);

    CHECK(found.providerName == "AniZone");
    CHECK(found.fromFallback);
    CHECK(found.stream.audioLanguage == "eng,en");
    MESSAGE("Fallback stream: " << found.stream.server << " " << found.stream.url);
}
