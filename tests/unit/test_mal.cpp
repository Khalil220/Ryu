#include "fake_http.hpp"
#include "mal.hpp"
#include "mal_login.hpp"
#include "secret.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace ryu;
using ryu::test::FakeHttpClient;
using ryu::test::readFixture;

namespace {

constexpr const char* api = "https://api.myanimelist.net/v2";
constexpr const char* tokenUrl = "https://myanimelist.net/v1/oauth2/token";
constexpr const char* animeFields = "my_list_status,alternative_titles,media_type,num_episodes,start_season";
constexpr const char* listUrl = "https://api.myanimelist.net/v2/users/@me/animelist"
                                "?fields=list_status,alternative_titles,media_type,num_episodes,start_season"
                                "&sort=anime_title&nsfw=true&limit=1000";
constexpr unsigned short testPort = 47814;

std::string searchUrl(const std::string& query) {
    return std::string(api) + "/anime?q=" + query + "&limit=20&nsfw=true&fields=" + animeFields;
}

MalAnime entry(int id, std::string title, std::string english, std::string mediaType, int episodes, int year) {
    MalAnime anime;
    anime.id = id;
    anime.title = std::move(title);
    anime.englishTitle = std::move(english);
    anime.mediaType = std::move(mediaType);
    anime.episodes = episodes;
    anime.year = year;
    return anime;
}

MalAnime watching(int watched, int episodes) {
    auto anime = entry(52991, "Sousou no Frieren", "Frieren: Beyond Journey's End", "tv", episodes, 2023);
    anime.list.status = MalStatus::Watching;
    anime.list.watched = watched;
    anime.list.startDate = "2026-09-01";
    return anime;
}

}

TEST_CASE("MAL status codes map both ways") {
    CHECK(malStatusCode(MalStatus::PlanToWatch) == "plan_to_watch");
    CHECK(malStatusCode(MalStatus::OnHold) == "on_hold");
    CHECK(malStatusCode(MalStatus::None).empty());
    CHECK(malStatusFrom("watching") == MalStatus::Watching);
    CHECK(malStatusFrom("completed") == MalStatus::Completed);
    CHECK(malStatusFrom("something new") == MalStatus::None);
}

TEST_CASE("MAL search reads ids, titles, type, episode count and year") {
    FakeHttpClient http;
    http.serve(searchUrl("frieren"), readFixture("mal/search_frieren.json"));
    MalClient client(http, {}, "");

    const auto results = client.search("frieren");

    REQUIRE(results.size() == 4);
    CHECK(results[1].id == 52991);
    CHECK(results[1].title == "Sousou no Frieren");
    CHECK(results[1].englishTitle == "Frieren: Beyond Journey's End");
    CHECK(results[1].synonyms == std::vector<std::string>{"Frieren at the Funeral", "Frieren The Slayer"});
    CHECK(results[1].mediaType == "tv");
    CHECK(results[1].episodes == 28);
    CHECK(results[1].year == 2023);
    CHECK(results[1].list.status == MalStatus::None);
    CHECK(results[3].mediaType == "ona");
    CHECK(http.header(0, "X-MAL-CLIENT-ID") == MalEndpoints().clientId);
    CHECK(http.header(0, "Authorization").empty());
}

TEST_CASE("MAL search text is kept within the 3 to 64 characters MAL accepts") {
    CHECK(malSearchText("  Frieren ") == "Frieren");
    CHECK(malSearchText("Rez") == "Rez");
    CHECK(malSearchText("K").empty());
    CHECK(malSearchText("   ").empty());
    CHECK(malSearchText("Is It Wrong to Try to Pick Up Girls in a Dungeon? Familia Myth Astrea Record") ==
          "Is It Wrong to Try to Pick Up Girls in a Dungeon? Familia Myth");
    CHECK(malSearchText(std::string(70, 'a')) == std::string(64, 'a'));
    const std::string kana = "\xE3\x81\x82";
    std::string japanese;
    for (int i = 0; i < 30; ++i) {
        japanese += kana;
    }
    const auto clipped = malSearchText(japanese);
    CHECK(clipped.size() == 63);
    CHECK(clipped.size() % 3 == 0);

    FakeHttpClient http;
    MalClient client(http, {}, "");
    CHECK(client.search("K").empty());
    CHECK(http.requests.empty());
}

TEST_CASE("MAL requests carry the login once there is one") {
    FakeHttpClient http;
    http.serve(std::string(api) + "/anime/52991?fields=" + animeFields, readFixture("mal/anime_52991.json"));
    http.serve(std::string(api) + "/users/@me", R"({"id":1,"name":"khalil"})");
    MalClient client(http, {}, "token-1");

    CHECK(client.anime(52991).englishTitle == "Frieren: Beyond Journey's End");
    CHECK(client.userName() == "khalil");
    CHECK(http.header(0, "Authorization") == "Bearer token-1");
    CHECK(http.header(0, "X-MAL-CLIENT-ID").empty());
}

TEST_CASE("the MAL list follows paging and reads each entry's status") {
    FakeHttpClient http;
    const std::string second = "https://api.myanimelist.net/v2/users/@me/animelist?offset=1&status=watching";
    http.serve(std::string(listUrl) + "&status=watching",
               R"({"data":[{"node":{"id":52991,"title":"Sousou no Frieren","alternative_titles":{"synonyms":[],)"
               R"("en":"Frieren: Beyond Journey's End","ja":""},"media_type":"tv","num_episodes":28,)"
               R"("start_season":{"year":2023,"season":"fall"}},"list_status":{"status":"watching","score":9,)"
               R"("num_episodes_watched":5,"is_rewatching":false,"updated_at":"2026-10-01T10:00:00+00:00",)"
               R"("start_date":"2026-09-20"}}],"paging":{"next":")" +
                   second + R"("}})");
    http.serve(second,
               R"({"data":[{"node":{"id":21,"title":"One Piece","media_type":"tv","num_episodes":0},)"
               R"("list_status":{"status":"watching","score":0,"num_watched_episodes":1100,"is_rewatching":false}}],)"
               R"("paging":{}})");
    MalClient client(http, {}, "token-1");

    const auto entries = client.list(MalStatus::Watching);

    REQUIRE(entries.size() == 2);
    CHECK(entries[0].id == 52991);
    CHECK(entries[0].englishTitle == "Frieren: Beyond Journey's End");
    CHECK(entries[0].list == MalListStatus{MalStatus::Watching, 9, 5, false, "2026-09-20", ""});
    CHECK(entries[1].title == "One Piece");
    CHECK(entries[1].episodes == 0);
    CHECK(entries[1].list.watched == 1100);

    http.serve(listUrl, R"({"data":[],"paging":{}})");
    CHECK(client.list(MalStatus::None).empty());
}

TEST_CASE("a MAL update sends only what changed and returns the new status") {
    FakeHttpClient http;
    const auto url = std::string(api) + "/anime/52991/my_list_status";
    http.serve(url, R"({"status":"completed","score":8,"num_episodes_watched":28,"is_rewatching":false,)"
                    R"("updated_at":"2026-10-02T05:00:00+00:00","start_date":"2026-09-20","finish_date":"2026-10-02"})");
    MalClient client(http, {}, "token-1");
    MalChanges changes;
    changes.status = MalStatus::Completed;
    changes.watched = 28;
    changes.finishDate = "2026-10-02";

    const auto status = client.update(52991, changes);

    CHECK(status == MalListStatus{MalStatus::Completed, 8, 28, false, "2026-09-20", "2026-10-02"});
    REQUIRE(http.requests.size() == 1);
    CHECK(http.requests[0].method == "PATCH");
    CHECK(http.requests[0].body == "status=completed&num_watched_episodes=28&finish_date=2026-10-02");
    CHECK(http.header(0, "Content-Type") == "application/x-www-form-urlencoded");
    CHECK(http.header(0, "Authorization") == "Bearer token-1");

    MalChanges score;
    score.score = 10;
    score.rewatching = true;
    score.startDate = "";
    client.update(52991, score);
    CHECK(http.requests[1].body == "score=10&is_rewatching=true&start_date=");
}

TEST_CASE("removing from MAL treats an anime that isn't listed as done") {
    FakeHttpClient http;
    http.serve(std::string(api) + "/anime/1/my_list_status", "[]");
    http.serve(std::string(api) + "/anime/2/my_list_status", "", 404);
    http.serve(std::string(api) + "/anime/3/my_list_status", R"({"message":"down for maintenance"})", 503);
    MalClient client(http, {}, "token-1");

    client.remove(1);
    client.remove(2);
    CHECK(http.requests[0].method == "DELETE");
    CHECK_THROWS_WITH_AS(client.remove(3), "MyAnimeList said: down for maintenance", MalError);
}

TEST_CASE("a rejected MAL token is reported as an expired login, other failures by their message") {
    FakeHttpClient http;
    http.serve(std::string(api) + "/users/@me", R"({"error":"invalid_token"})", 401);
    http.serve(searchUrl("xyz"), "<html>blocked</html>", 403);
    http.serve(std::string(api) + "/anime/5/my_list_status", R"({"error":"invalid_token"})", 401);
    MalClient client(http, {}, "stale");

    CHECK_THROWS_AS(client.userName(), MalLoginExpired);
    CHECK_THROWS_WITH_AS(client.search("xyz"), "MyAnimeList returned HTTP 403", MalError);
    CHECK_THROWS_AS(client.update(5, {}), MalLoginExpired);
    CHECK_THROWS_AS(client.remove(5), MalLoginExpired);
}

TEST_CASE("the MAL login address carries the client, the verifier as a plain challenge and the redirect") {
    const auto verifier = malCodeVerifier();
    CHECK(verifier.size() == 64);
    CHECK(verifier != malCodeVerifier());
    CHECK(verifier.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~") ==
          std::string::npos);

    CHECK(malAuthorizationUrl({}, "verifier-1", "state 1") ==
          "https://myanimelist.net/v1/oauth2/authorize?response_type=code"
          "&client_id=7b530d8dae27b90f2e328b462f335ce0&state=state%201"
          "&redirect_uri=http%3A%2F%2Flocalhost%3A47813%2Fcallback"
          "&code_challenge=verifier-1&code_challenge_method=plain");
    CHECK(MalEndpoints::on("http://127.0.0.1:9000").token == "http://127.0.0.1:9000/v1/oauth2/token");
    CHECK(MalEndpoints::on("http://127.0.0.1:9000").api == "http://127.0.0.1:9000/v2");
}

TEST_CASE("parseMalRedirect reads the code, state and error from the address MAL sends the browser to") {
    const auto approved = parseMalRedirect("/callback?code=def50200a%2Bb&state=abc%20123");
    CHECK(approved.code == "def50200a+b");
    CHECK(approved.state == "abc 123");
    CHECK(approved.error.empty());

    const auto denied = parseMalRedirect("/callback?error=access_denied&state=s#fragment");
    CHECK(denied.code.empty());
    CHECK(denied.error == "access_denied");
    CHECK(denied.state == "s");
    CHECK(parseMalRedirect("/callback").code.empty());
}

TEST_CASE("MAL tokens come from the code and are renewed with the refresh token") {
    FakeHttpClient http;
    http.serve(tokenUrl, R"({"token_type":"Bearer","expires_in":3600,"access_token":"access-1","refresh_token":"refresh-1"})");

    const auto tokens = exchangeMalCode(http, {}, "code 1", "verifier-1", 1000);

    CHECK(tokens.access == "access-1");
    CHECK(tokens.refresh == "refresh-1");
    CHECK(tokens.expiresAt == 4600);
    CHECK(tokens.usable(4000));
    CHECK_FALSE(tokens.usable(4500));
    CHECK_FALSE(MalTokens().usable(0));
    CHECK(http.requests[0].method == "POST");
    CHECK(http.requests[0].body == "client_id=7b530d8dae27b90f2e328b462f335ce0&grant_type=authorization_code"
                                   "&code=code%201&code_verifier=verifier-1"
                                   "&redirect_uri=http%3A%2F%2Flocalhost%3A47813%2Fcallback");

    refreshMalTokens(http, {}, "refresh-1", 5000);
    CHECK(http.requests[1].body ==
          "client_id=7b530d8dae27b90f2e328b462f335ce0&grant_type=refresh_token&refresh_token=refresh-1");
}

TEST_CASE("a refused refresh means logging in again, a refused code says why") {
    FakeHttpClient http;
    http.serve(tokenUrl, readFixture("mal/token_refused.json"), 401);
    CHECK_THROWS_AS(refreshMalTokens(http, {}, "old", 0), MalLoginExpired);
    CHECK_THROWS_WITH_AS(exchangeMalCode(http, {}, "bad", "verifier", 0),
                         "MyAnimeList said: The refresh token is invalid.", MalError);

    FakeHttpClient empty;
    empty.serve(tokenUrl, "{}");
    CHECK_THROWS_AS(exchangeMalCode(empty, {}, "code", "verifier", 0), MalError);
}

TEST_CASE("MalAccess uses a token that is still good without asking for a new one") {
    FakeHttpClient http;
    http.serve(std::string(api) + "/users/@me", R"({"name":"khalil"})");
    MalAccess access(http, {}, {"access-1", "refresh-1", 5000}, [] { return std::int64_t{1000}; });

    CHECK(access.call<std::string>([](MalClient& client) { return client.userName(); }) == "khalil");
    CHECK(http.requests.size() == 1);
    CHECK(http.header(0, "Authorization") == "Bearer access-1");
    CHECK(access.tokens() == MalTokens{"access-1", "refresh-1", 5000});
}

TEST_CASE("MalAccess renews a token that is about to run out before using it") {
    FakeHttpClient http;
    http.serve(tokenUrl, R"({"token_type":"Bearer","expires_in":3600,"access_token":"access-2","refresh_token":"refresh-2"})");
    http.serve(std::string(api) + "/users/@me", R"({"name":"khalil"})");
    MalAccess access(http, {}, {"access-1", "refresh-1", 5000}, [] { return std::int64_t{4950}; });

    CHECK(access.call<std::string>([](MalClient& client) { return client.userName(); }) == "khalil");
    REQUIRE(http.requests.size() == 2);
    CHECK(http.requests[0].url == tokenUrl);
    CHECK(http.requests[0].body.ends_with("grant_type=refresh_token&refresh_token=refresh-1"));
    CHECK(http.header(1, "Authorization") == "Bearer access-2");
    CHECK(access.tokens() == MalTokens{"access-2", "refresh-2", 8550});
}

TEST_CASE("MalAccess renews and retries once when MAL rejects a token that looked good") {
    FakeHttpClient http;
    http.serve(tokenUrl, R"({"token_type":"Bearer","expires_in":3600,"access_token":"access-2","refresh_token":"refresh-2"})");
    MalAccess access(http, {}, {"access-1", "refresh-1", 5000}, [] { return std::int64_t{1000}; });
    int attempts = 0;

    const auto name = access.call<std::string>([&](MalClient&) -> std::string {
        if (++attempts == 1) {
            throw MalLoginExpired();
        }
        return "second try";
    });

    CHECK(name == "second try");
    CHECK(attempts == 2);
    CHECK(access.tokens().access == "access-2");
    CHECK_THROWS_AS(access.call<int>([](MalClient&) -> int { throw MalLoginExpired(); }), MalLoginExpired);
}

TEST_CASE("MalAccess forgets a login MAL won't renew") {
    FakeHttpClient http;
    http.serve(tokenUrl, readFixture("mal/token_refused.json"), 401);
    MalAccess access(http, {}, {"access-1", "refresh-1", 5000}, [] { return std::int64_t{9000}; });

    CHECK_THROWS_AS(access.call<int>([](MalClient&) { return 1; }), MalLoginExpired);
    CHECK(access.tokens() == MalTokens{});
    CHECK_THROWS_AS(access.call<int>([](MalClient&) { return 1; }), MalLoginExpired);
    CHECK(http.requests.size() == 1);

    access.setTokens({"access-3", "refresh-3", 20000});
    CHECK(access.call<int>([](MalClient&) { return 7; }) == 7);
}

TEST_CASE("matchMalAnime uses the provider's MAL id when it has one") {
    const std::vector<MalAnime> list{entry(1, "Other", "", "tv", 12, 2020),
                                     entry(52991, "Sousou no Frieren", "Frieren: Beyond Journey's End", "tv", 28, 2023)};
    Show show{"x", "A title MAL has never heard of"};
    show.malId = 52991;
    CHECK(matchMalAnime(show, list) == 1);
    show.malId = 7;
    CHECK_FALSE(matchMalAnime(show, list).has_value());
}

TEST_CASE("matchMalAnime falls back to titles, in either language, ignoring punctuation and curly quotes") {
    auto frieren = entry(52991, "Sousou no Frieren", "Frieren: Beyond Journey's End", "tv", 28, 2023);
    frieren.synonyms = {"Frieren at the Funeral"};
    const std::vector<MalAnime> list{entry(59978, "Sousou no Frieren 2nd Season", "Frieren: Beyond Journey's End Season 2",
                                           "tv", 10, 2026),
                                     frieren};

    CHECK(matchMalAnime(Show{"1", "Frieren: Beyond Journey’s End", "", "TV"}, list) == 1);
    CHECK(matchMalAnime(Show{"2", "Something else", "SOUSOU NO FRIEREN"}, list) == 1);
    CHECK(matchMalAnime(Show{"3", "Frieren at the Funeral"}, list) == 1);
    CHECK(matchMalAnime(Show{"4", "Frieren: Beyond Journey's End Season 2"}, list) == 0);
    CHECK_FALSE(matchMalAnime(Show{"5", "Frieren"}, list).has_value());
    CHECK_FALSE(matchMalAnime(Show{"6", ""}, list).has_value());
}

TEST_CASE("matchMalAnime settles same-named entries by format and year, and gives up when it can't") {
    const std::vector<MalAnime> list{entry(1, "Hunter x Hunter", "", "tv", 62, 1999),
                                     entry(2, "Hunter x Hunter", "", "tv", 148, 2011),
                                     entry(3, "Hunter x Hunter", "", "ova", 8, 2002)};
    Show show{"h", "Hunter x Hunter", "", "TV"};
    CHECK_FALSE(matchMalAnime(show, list).has_value());
    show.year = 2011;
    CHECK(matchMalAnime(show, list) == 1);
    show.format = "OVA";
    show.year = 0;
    CHECK(matchMalAnime(show, list) == 2);
}

TEST_CASE("matchMalAnime refuses a same-named anime that the show's length or year rules out") {
    const std::vector<MalAnime> onMyList{entry(136, "Hunter x Hunter", "Hunter x Hunter", "tv", 62, 1999)};
    Show remake{"1393", "Hunter x Hunter", "Hunter x Hunter", "TV", 148, 148};
    CHECK_FALSE(matchMalAnime(remake, onMyList).has_value());
    Show original{"1229", "Hunter x Hunter", "Hunter x Hunter", "TV", 62, 62};
    CHECK(matchMalAnime(original, onMyList) == 0);

    Show dated{"x", "Hunter x Hunter", "", "TV"};
    dated.year = 2011;
    CHECK_FALSE(matchMalAnime(dated, onMyList).has_value());
    dated.year = 2000;
    CHECK(matchMalAnime(dated, onMyList) == 0);

    Show airing{"y", "Hunter x Hunter", "", "TV", 40, 0};
    CHECK(matchMalAnime(airing, onMyList) == 0);
}

TEST_CASE("matchMalAnime tells a remake from the original when both are candidates") {
    const std::vector<MalAnime> catalogue{entry(11061, "Hunter x Hunter (2011)", "Hunter x Hunter", "tv", 148, 2011),
                                          entry(136, "Hunter x Hunter", "Hunter x Hunter", "tv", 62, 1999)};
    CHECK(matchMalAnime(Show{"1393", "Hunter x Hunter", "", "TV", 148, 148}, catalogue) == 0);
    CHECK(matchMalAnime(Show{"1229", "Hunter x Hunter", "", "TV", 62, 62}, catalogue) == 1);
    Show byYear{"z", "Hunter x Hunter", "", "TV"};
    byYear.year = 2011;
    CHECK(matchMalAnime(byYear, catalogue) == 0);
    CHECK_FALSE(matchMalAnime(Show{"w", "Hunter x Hunter", "", "TV"}, catalogue).has_value());
}

TEST_CASE("matchShowForMal picks the provider's show that fits a MAL entry best") {
    const std::vector<Show> results{Show{"1393", "Hunter x Hunter", "", "TV", 148, 148},
                                    Show{"1229", "Hunter x Hunter", "", "TV", 62, 62},
                                    Show{"1225", "Hunter x Hunter: Greed Island", "", "OVA", 8, 0}};
    CHECK(matchShowForMal(entry(136, "Hunter x Hunter", "Hunter x Hunter", "tv", 62, 1999), results) == 1);
    CHECK(matchShowForMal(entry(11061, "Hunter x Hunter (2011)", "Hunter x Hunter", "tv", 148, 2011), results) == 0);
    CHECK_FALSE(matchShowForMal(entry(1, "Something else", "", "tv", 12, 2020), results).has_value());

    auto known = results;
    known[1].malId = 11061;
    CHECK(matchShowForMal(entry(11061, "Hunter x Hunter (2011)", "Hunter x Hunter", "tv", 148, 2011), known) == 1);
}

TEST_CASE("findMalCandidates searches MAL by title, then by the other title, or asks for a known id") {
    FakeHttpClient http;
    http.serve(searchUrl("frieren"), readFixture("mal/search_frieren.json"));
    http.serve(searchUrl("Nothing%20like%20it"), R"({"data":[]})");
    http.serve(std::string(api) + "/anime/52991?fields=" + animeFields, readFixture("mal/anime_52991.json"));
    MalClient client(http, {}, "token-1");

    CHECK(findMalCandidates(client, Show{"a", "frieren"}).size() == 4);
    CHECK(http.requests.size() == 1);

    const auto both = findMalCandidates(client, Show{"b", "Nothing like it", "frieren"});
    CHECK(both.size() == 4);
    CHECK(http.requests.size() == 3);

    Show known{"c", "Whatever it is called"};
    known.malId = 52991;
    const auto one = findMalCandidates(client, known);
    REQUIRE(one.size() == 1);
    CHECK(one[0].id == 52991);
}

TEST_CASE("malProgress raises the watched count and never lowers it") {
    const auto changes = malProgress(watching(5, 28), "6", "2026-10-02");
    REQUIRE(changes.has_value());
    MalChanges expected;
    expected.watched = 6;
    CHECK(*changes == expected);

    CHECK_FALSE(malProgress(watching(5, 28), "5", "2026-10-02").has_value());
    CHECK_FALSE(malProgress(watching(5, 28), "2", "2026-10-02").has_value());
    CHECK(malProgress(watching(5, 28), "9", "2026-10-02")->watched == 9);
}

TEST_CASE("malProgress sets the start date on the first episode and completes the show on the last") {
    auto fresh = watching(0, 28);
    fresh.list.startDate.clear();
    const auto first = malProgress(fresh, "1", "2026-10-02");
    REQUIRE(first.has_value());
    CHECK(first->startDate == "2026-10-02");
    CHECK_FALSE(first->status.has_value());

    const auto last = malProgress(watching(27, 28), "28", "2026-10-02");
    REQUIRE(last.has_value());
    CHECK(last->watched == 28);
    CHECK(last->status == MalStatus::Completed);
    CHECK(last->finishDate == "2026-10-02");
    CHECK_FALSE(last->startDate.has_value());

    const auto unknownLength = malProgress(watching(1100, 0), "1101", "2026-10-02");
    REQUIRE(unknownLength.has_value());
    CHECK_FALSE(unknownLength->status.has_value());
}

TEST_CASE("malProgress leaves alone what isn't being watched, odd episode numbers and episodes past the end") {
    auto planned = watching(0, 28);
    planned.list.status = MalStatus::PlanToWatch;
    CHECK_FALSE(malProgress(planned, "1", "2026-10-02").has_value());
    auto unlisted = watching(0, 28);
    unlisted.list.status = MalStatus::None;
    CHECK_FALSE(malProgress(unlisted, "1", "2026-10-02").has_value());
    CHECK_FALSE(malProgress(watching(5, 28), "12.5", "2026-10-02").has_value());
    CHECK_FALSE(malProgress(watching(5, 28), "SP", "2026-10-02").has_value());
    CHECK_FALSE(malProgress(watching(5, 28), "0", "2026-10-02").has_value());
    CHECK_FALSE(malProgress(watching(5, 28), "29", "2026-10-02").has_value());
}

TEST_CASE("malProgress counts a rewatch without touching the status or dates") {
    auto rewatch = watching(3, 28);
    rewatch.list.status = MalStatus::Completed;
    rewatch.list.rewatching = true;
    const auto middle = malProgress(rewatch, "4", "2026-10-02");
    REQUIRE(middle.has_value());
    MalChanges expected;
    expected.watched = 4;
    CHECK(*middle == expected);

    rewatch.list.watched = 27;
    const auto end = malProgress(rewatch, "28", "2026-10-02");
    REQUIRE(end.has_value());
    CHECK(end->rewatching == false);
    CHECK_FALSE(end->status.has_value());
    CHECK_FALSE(end->finishDate.has_value());
}

TEST_CASE("malChangesBetween lists only the fields that differ") {
    const MalListStatus before{MalStatus::Watching, 0, 5, false, "", ""};
    CHECK(malChangesBetween(before, before).empty());

    auto after = before;
    after.status = MalStatus::Completed;
    after.watched = 12;
    after.score = 9;
    after.finishDate = "2026-10-02";
    MalChanges expected;
    expected.status = MalStatus::Completed;
    expected.watched = 12;
    expected.score = 9;
    expected.finishDate = "2026-10-02";
    CHECK(malChangesBetween(before, after) == expected);

    const MalListStatus dated{MalStatus::Completed, 8, 12, false, "2026-01-01", "2026-02-01"};
    auto cleared = dated;
    cleared.startDate.clear();
    MalChanges clearing;
    clearing.startDate = "";
    CHECK(malChangesBetween(dated, cleared) == clearing);

    MalChanges adding;
    adding.status = MalStatus::PlanToWatch;
    CHECK(malChangesBetween({}, MalListStatus{MalStatus::PlanToWatch}) == adding);
}

TEST_CASE("validMalDate accepts real calendar dates and nothing else") {
    CHECK(validMalDate(""));
    CHECK(validMalDate("2026-10-02"));
    CHECK(validMalDate("2024-02-29"));
    CHECK_FALSE(validMalDate("2026-02-29"));
    CHECK_FALSE(validMalDate("2026-13-01"));
    CHECK_FALSE(validMalDate("2026-04-31"));
    CHECK_FALSE(validMalDate("2026-10-2"));
    CHECK_FALSE(validMalDate("02/10/2026"));
    CHECK_FALSE(validMalDate("2026-1x-02"));
    CHECK_FALSE(validMalDate("today"));
}

TEST_CASE("saved secrets are unreadable in the file and come back for the same Windows account") {
    const std::string token = "def50200-access-token-with-\xC3\xA9";
    const auto stored = protectSecret(token);

    CHECK(stored != token);
    CHECK(stored.find("def50200") == std::string::npos);
    CHECK(stored.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=") ==
          std::string::npos);
    CHECK(revealSecret(stored) == token);
    CHECK(protectSecret("").empty());
    CHECK(revealSecret("").empty());
    CHECK(revealSecret("bm90IGEgc2VjcmV0").empty());
    CHECK(revealSecret("not base64 at all !!").empty());
}

TEST_CASE("the login listener answers the browser, ignores other requests and returns the callback") {
    std::atomic<bool> cancelled = false;
    auto waiting = std::async(std::launch::async,
                              [&] { return waitForMalRedirect(testPort, std::chrono::seconds(20), cancelled); });
    CurlHttpClient browser;
    const auto base = "http://127.0.0.1:" + std::to_string(testPort);
    HttpResponse icon;
    for (int attempt = 0; attempt < 50; ++attempt) {
        try {
            icon = browser.get(base + "/favicon.ico");
            break;
        } catch (const HttpError&) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    CHECK(icon.status == 404);

    const auto page = browser.get(base + "/callback?code=abc&state=xyz");

    CHECK(page.status == 200);
    CHECK(page.body.find("go back to Ryu") != std::string::npos);
    CHECK(waiting.get() == "/callback?code=abc&state=xyz");
}

TEST_CASE("the login listener stops when cancelled or when the time is up, and says when its port is taken") {
    std::atomic<bool> cancelled = false;
    auto waiting = std::async(std::launch::async,
                              [&] { return waitForMalRedirect(testPort, std::chrono::seconds(20), cancelled); });
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::atomic<bool> never = false;
    CHECK_THROWS_AS(waitForMalRedirect(testPort, std::chrono::seconds(1), never), MalError);
    cancelled = true;
    CHECK(waiting.get().empty());

    const auto started = std::chrono::steady_clock::now();
    CHECK(waitForMalRedirect(testPort, std::chrono::seconds(1), never).empty());
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
}
