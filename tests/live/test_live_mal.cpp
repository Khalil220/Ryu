#include "mal.hpp"
#include "providers/hianime.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;

TEST_CASE("live: MyAnimeList answers a search and a details request for Ryu's client ID") {
    CurlHttpClient http;
    MalClient client(http, {}, "");

    const auto results = client.search("frieren");
    const auto frieren = std::ranges::find(results, 52991, &MalAnime::id);
    REQUIRE(frieren != results.end());
    CHECK(frieren->title == "Sousou no Frieren");
    CHECK(frieren->englishTitle == "Frieren: Beyond Journey's End");
    CHECK(frieren->mediaType == "tv");
    CHECK(frieren->episodes == 28);
    CHECK(frieren->year == 2023);

    Show show{"481", "Frieren: Beyond Journey's End", "Sousou no Frieren", "TV"};
    const auto match = matchMalAnime(show, results);
    REQUIRE(match.has_value());
    CHECK(results[*match].id == 52991);

    const auto details = client.anime(52991);
    CHECK(details.episodes == 28);
    CHECK(details.list.status == MalStatus::None);
}

TEST_CASE("live: both Hunter x Hunter series on HiAnime find their own MyAnimeList entries") {
    CurlHttpClient http;
    HiAnimeProvider provider(http);
    MalClient client(http, {}, "");

    const auto shows = provider.search("hunter x hunter");
    const auto series = [&](int episodes) {
        const auto it = std::ranges::find_if(shows, [&](const Show& show) {
            return show.title == "Hunter x Hunter" && show.format == "TV" && show.subEpisodes == episodes;
        });
        REQUIRE(it != shows.end());
        return *it;
    };
    const auto remake = series(148);
    const auto original = series(62);

    const auto forRemake = findMalCandidates(client, remake);
    const auto remakeMatch = matchMalAnime(remake, forRemake);
    REQUIRE(remakeMatch.has_value());
    CHECK(forRemake[*remakeMatch].id == 11061);

    const auto forOriginal = findMalCandidates(client, original);
    const auto originalMatch = matchMalAnime(original, forOriginal);
    REQUIRE(originalMatch.has_value());
    CHECK(forOriginal[*originalMatch].id == 136);

    const auto listed = std::ranges::find(forOriginal, 136, &MalAnime::id);
    REQUIRE(listed != forOriginal.end());
    CHECK_FALSE(matchMalAnime(remake, {*listed}).has_value());
    CHECK(matchShowForMal(*listed, shows) == static_cast<size_t>(std::ranges::find(shows, original.id, &Show::id) - shows.begin()));
}

TEST_CASE("live: MyAnimeList refuses a made-up login") {
    CurlHttpClient http;
    CHECK_THROWS_AS(refreshMalTokens(http, {}, "not a refresh token", 0), MalLoginExpired);
    MalClient client(http, {}, "not an access token");
    CHECK_THROWS_AS(client.userName(), MalLoginExpired);
    CHECK_THROWS_AS(client.list(MalStatus::Watching), MalLoginExpired);
}
