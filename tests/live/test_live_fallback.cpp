#include "http.hpp"
#include "playlist_server.hpp"
#include "providers/hianime.hpp"
#include "providers/miruro.hpp"
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

TEST_CASE("live: a HiAnime episode that the primary cannot play is found on Miruro") {
    CurlHttpClient http;
    PlaylistServer server;
    HiAnimeProvider hianime(http);
    MiruroProvider miruro(http);
    EmptyProvider nothing;

    const auto shows = hianime.search("frieren");
    REQUIRE_FALSE(shows.empty());
    const auto& show = shows.front();
    const auto episodes = hianime.episodes(show);
    REQUIRE(episodes.size() > 1);

    const auto found = findStream(http, server, {{"Nothing", &nothing}, {"Miruro", &miruro}}, show, episodes[1],
                                  Audio::Dub);

    CHECK(found.providerName == "Miruro");
    CHECK(found.fromFallback);
    CHECK(found.stream.audioLanguage == "eng,en,English");
    MESSAGE("Fallback stream: " << found.stream.server << " " << found.stream.url);
}
