#include "http.hpp"
#include "playlist_server.hpp"
#include "providers/hianime.hpp"
#include "providers/kickassanime.hpp"
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

TEST_CASE("live: a HiAnime episode that the primary cannot play is found on KickAssAnime") {
    CurlHttpClient http;
    PlaylistServer server;
    HiAnimeProvider hianime(http);
    KickAssAnimeProvider kickass(http);
    EmptyProvider nothing;

    const auto shows = hianime.search("frieren");
    REQUIRE_FALSE(shows.empty());
    const auto& show = shows.front();
    const auto episodes = hianime.episodes(show);
    REQUIRE(episodes.size() > 1);

    const auto found = findStream(http, server, {{"Nothing", &nothing}, {"KickAssAnime", &kickass}}, show,
                                  episodes[1], Audio::Dub);

    CHECK(found.providerName == "KickAssAnime");
    CHECK(found.fromFallback);
    const bool direct = found.stream.url.find("krussdomi") != std::string::npos;
    const bool repaired = found.stream.url.starts_with("http://127.0.0.1:") && !found.repairedHosts.empty();
    CHECK((direct || repaired));
    CHECK(found.stream.audioLanguage == "eng,en");
    MESSAGE("Fallback stream: " << found.stream.url);
    for (const auto& host : found.repairedHosts) {
        MESSAGE("Routed around dead host: " << host);
    }
}
