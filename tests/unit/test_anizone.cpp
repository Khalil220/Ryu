#include "fake_http.hpp"
#include "providers/anizone.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ryu;
using ryu::test::FakeHttpClient;
using ryu::test::readFixture;

namespace {

constexpr const char* searchUrl = "https://anizone.to/anime?search=frieren";
constexpr const char* showUrl = "https://anizone.to/anime/mdkytdqp";
constexpr const char* watchUrl = "https://anizone.to/anime/mdkytdqp/1";
constexpr const char* eijiUrl = "https://anizone.to/anime/qtskwpje/1";
constexpr const char* frierenMaster = "https://seiryuu.vid-cdn.xyz/a7305a5d-5bc8-4944-843a-f8675561b940/master.m3u8";

std::string playerPage(const std::string& subtitles, const std::string& audio) {
    return R"(<div class="flex gap-1"><div class="text-sm">Audio:</div><div class="text-xs">)" + audio +
           R"(</div></div><script>vidstackPlayer(JSON.parse('{"src":"https:\/\/cdn.example\/master.m3u8","subtitles":[)" +
           subtitles + R"(]}'))</script>)";
}

std::string track(const std::string& title, const std::string& language, bool isDefault) {
    return R"({"title":")" + title + R"(","language":")" + language +
           R"(","default":)" + (isDefault ? "true" : "false") +
           R"(,"file":"https:\/\/cdn.example\/)" + language + R"(.ass"})";
}

}

TEST_CASE("AniZone search reads the embedded results with English titles, format, year and episode count") {
    FakeHttpClient http;
    http.serve(searchUrl, readFixture("anizone/search_frieren.html"));
    AniZoneProvider provider(http);

    const auto shows = provider.search("frieren");

    REQUIRE(shows.size() == 2);
    CHECK(shows[0].id == "mdkytdqp");
    CHECK(shows[0].title == "Frieren: Beyond Journey's End");
    CHECK(shows[0].altTitle == "Sousou no Frieren");
    CHECK(shows[0].format == "TV");
    CHECK(shows[0].year == 2023);
    CHECK(shows[0].episodeCount == 28);
    CHECK(shows[0].posterUrl == "https://anizone.to/images/anime/38c0dc13-dbdf-4e7d-9590-f079497c2842.jpg");
    CHECK(std::ranges::find(shows[0].genres, "Magic") != shows[0].genres.end());
    CHECK(shows[0].pageUrl == showUrl);
    CHECK(shows[1].title == "Frieren: Beyond Journey's End (2026)");
    CHECK(shows[1].year == 2026);
}

TEST_CASE("AniZone describe reads the synopsis from the show page") {
    FakeHttpClient http;
    http.serve(showUrl, readFixture("anizone/show_frieren.html"));
    AniZoneProvider provider(http);
    Show show{"mdkytdqp", "Frieren"};
    show.pageUrl = showUrl;

    const auto described = provider.describe(show);

    CHECK(described.synopsis.starts_with("Based on an action-adventure fantasy manga"));
    CHECK(described.synopsis.find("Elf mage Frieren and her courageous fellow adventurers") != std::string::npos);
}

TEST_CASE("AniZone episodes come from the first episode's page, with titles and dub availability") {
    FakeHttpClient http;
    http.serve(watchUrl, readFixture("anizone/watch_frieren_1.html"));
    AniZoneProvider provider(http);

    const auto episodes = provider.episodes(Show{"mdkytdqp"});

    REQUIRE(episodes.size() == 28);
    CHECK(episodes[0].id == "mdkytdqp/1");
    CHECK(episodes[0].number == "1");
    CHECK(episodes[0].title == "The Journey's End");
    CHECK(episodes[27].number == "28");
    CHECK(std::ranges::all_of(episodes, [](const Episode& e) { return e.subbed && e.dubbed; }));
}

TEST_CASE("AniZone marks a show without English audio as not dubbed") {
    FakeHttpClient http;
    http.serve(eijiUrl, readFixture("anizone/watch_eiji_1.html"));
    AniZoneProvider provider(http);

    const auto episodes = provider.episodes(Show{"qtskwpje"});

    REQUIRE(episodes.size() == 1);
    CHECK(episodes[0].subbed);
    CHECK_FALSE(episodes[0].dubbed);
}

TEST_CASE("AniZone episodes find the first episode through the show page when it isn't numbered 1") {
    FakeHttpClient http;
    http.serve("https://anizone.to/anime/abc/1", "not found", 404);
    http.serve("https://anizone.to/anime/abc", R"(<div x-data="{ items: JSON.parse('[{"slug":"S1"}]') }">)");
    http.serve("https://anizone.to/anime/abc/S1", readFixture("anizone/watch_frieren_1.html"));
    AniZoneProvider provider(http);

    const auto episodes = provider.episodes(Show{"abc"});

    REQUIRE(episodes.size() == 28);
    CHECK(episodes[0].id == "abc/1");
}

TEST_CASE("AniZone streams play the episode's master playlist with Japanese audio and its subtitles") {
    FakeHttpClient http;
    http.serve(watchUrl, readFixture("anizone/watch_frieren_1.html"));
    AniZoneProvider provider(http);

    const auto streams = provider.streams("mdkytdqp/1", Audio::Sub);

    REQUIRE(streams.size() == 1);
    const auto& stream = streams[0];
    CHECK(stream.url == frierenMaster);
    CHECK(stream.server == "AniZone");
    CHECK(stream.audioLanguage == "jpn,ja");
    CHECK(stream.headers.empty());
    CHECK_FALSE(stream.disguisedSegments);
    REQUIRE(stream.subtitles.size() == 15);
    CHECK(stream.subtitles[0].label == "Arabic (Saudi Arabia)");
    CHECK(stream.subtitles[0].language == "ar");
    REQUIRE(std::ranges::count_if(stream.subtitles, &Subtitle::isDefault) == 1);
    CHECK(std::ranges::find_if(stream.subtitles, &Subtitle::isDefault)->label == "English (US)");
}

TEST_CASE("AniZone dub streams ask for English audio and leave full subtitles off") {
    FakeHttpClient http;
    http.serve(watchUrl, readFixture("anizone/watch_frieren_1.html"));
    AniZoneProvider provider(http);

    const auto streams = provider.streams("mdkytdqp/1", Audio::Dub);

    REQUIRE(streams.size() == 1);
    CHECK(streams[0].audioLanguage == "eng,en");
    CHECK(std::ranges::none_of(streams[0].subtitles, &Subtitle::isDefault));
}

TEST_CASE("AniZone offers no dub for an episode without English audio") {
    FakeHttpClient http;
    http.serve(eijiUrl, readFixture("anizone/watch_eiji_1.html"));
    AniZoneProvider provider(http);

    CHECK(provider.streams("qtskwpje/1", Audio::Dub).empty());
}

TEST_CASE("AniZone names a fansub track after its language and keeps the group's name") {
    FakeHttpClient http;
    http.serve(eijiUrl, readFixture("anizone/watch_eiji_1.html"));
    AniZoneProvider provider(http);

    const auto streams = provider.streams("qtskwpje/1", Audio::Sub);

    REQUIRE(streams.size() == 1);
    REQUIRE(streams[0].subtitles.size() == 1);
    CHECK(streams[0].subtitles[0].label == "English (wowmdildo {+Eternal Blizzard})");
    CHECK(streams[0].subtitles[0].isDefault);
}

TEST_CASE("AniZone dub streams default to the English signs and songs track") {
    FakeHttpClient http;
    http.serve("https://anizone.to/anime/op/1",
               playerPage(track("Spanish - Full Subtitles", "es-419", false) + "," +
                              track("English - Signs/Songs", "en", false) + "," +
                              track("English - Full Subtitles", "en", true),
                          "<span class=\"rounded-md\">English</span><span class=\"rounded-md\">Japanese</span>"));
    AniZoneProvider provider(http);

    const auto dub = provider.streams("op/1", Audio::Dub);
    REQUIRE(dub.size() == 1);
    REQUIRE(std::ranges::count_if(dub[0].subtitles, &Subtitle::isDefault) == 1);
    CHECK(std::ranges::find_if(dub[0].subtitles, &Subtitle::isDefault)->label == "English - Signs/Songs");

    const auto sub = provider.streams("op/1", Audio::Sub);
    REQUIRE(sub.size() == 1);
    CHECK(std::ranges::find_if(sub[0].subtitles, &Subtitle::isDefault)->label == "English - Full Subtitles");
}
