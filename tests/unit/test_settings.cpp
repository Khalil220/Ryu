#include "settings.hpp"

#include <doctest/doctest.h>

#include <wx/fileconf.h>
#include <wx/init.h>
#include <wx/sstream.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace ryu;

namespace {

wxFileConfig configFrom(const wxString& text) {
    wxStringInputStream stream(text);
    return wxFileConfig(stream);
}

}

TEST_CASE("empty settings fall back to HiAnime, subs and the default base URL") {
    wxInitializer init;
    auto config = configFrom("");
    const auto settings = loadSettings(config);
    CHECK(settings.providerId == "hianime");
    CHECK(settings.audio == Audio::Sub);
    CHECK(settings.baseUrl() == "https://hianime.at");
    CHECK(settings.useFallback);
    CHECK(settings.checkForUpdates);
    CHECK(settings.theme == Theme::Dark);
    CHECK(settings.window.width == 0);
    CHECK(settings.readSubtitlesFor(Audio::Sub));
    CHECK_FALSE(settings.readSubtitlesFor(Audio::Dub));
    CHECK(settings.subtitlesFor(Audio::Sub) == SubtitlePreference{});
    CHECK(settings.subtitlesFor(Audio::Dub) == SubtitlePreference{});
    CHECK(settings.audioLanguageFor(Audio::Sub).empty());
    CHECK(settings.audioLanguageFor(Audio::Dub).empty());
}

TEST_CASE("saved settings load back unchanged") {
    wxInitializer init;
    auto config = configFrom("");
    Settings settings;
    settings.audio = Audio::Dub;
    settings.useFallback = false;
    settings.checkForUpdates = false;
    settings.theme = Theme::System;
    settings.window = {-8, 40, 1600, 900, true};
    settings.readSubtitlesSubbed = false;
    settings.readSubtitlesDubbed = true;
    settings.providerId = "anizone";
    settings.baseUrlOverrides["hianime"] = "https://mirror.example";
    settings.subtitlesSubbed = {SubtitlePreference::Kind::Track, "Spanish (- Spanish(Latin America))", "spa"};
    settings.subtitlesDubbed = {SubtitlePreference::Kind::Off, "", ""};
    settings.audioLanguageDubbed = "de";
    saveSettings(config, settings);

    const auto loaded = loadSettings(config);
    CHECK(loaded.audio == Audio::Dub);
    CHECK_FALSE(loaded.useFallback);
    CHECK_FALSE(loaded.checkForUpdates);
    CHECK(loaded.theme == Theme::System);
    CHECK(loaded.window == settings.window);
    CHECK_FALSE(loaded.readSubtitlesFor(Audio::Sub));
    CHECK(loaded.readSubtitlesFor(Audio::Dub));
    CHECK(loaded.providerId == "anizone");
    CHECK(loaded.baseUrl() == "https://anizone.to");
    CHECK(loaded.baseUrlFor(*findProvider("hianime")) == "https://mirror.example");
    CHECK(loaded.subtitlesFor(Audio::Sub) == settings.subtitlesSubbed);
    CHECK(loaded.subtitlesFor(Audio::Dub).kind == SubtitlePreference::Kind::Off);
    CHECK(loaded.audioLanguageFor(Audio::Sub).empty());
    CHECK(loaded.audioLanguageFor(Audio::Dub) == "de");
}

TEST_CASE("an override equal to the default is not stored") {
    wxInitializer init;
    auto config = configFrom("");
    Settings settings;
    settings.baseUrlOverrides["hianime"] = "https://hianime.at";
    saveSettings(config, settings);
    CHECK_FALSE(config.HasEntry("/BaseUrls/hianime"));

    settings.baseUrlOverrides["hianime"] = "https://mirror.example";
    saveSettings(config, settings);
    CHECK(config.HasEntry("/BaseUrls/hianime"));

    settings.baseUrlOverrides.clear();
    saveSettings(config, settings);
    CHECK_FALSE(config.HasEntry("/BaseUrls/hianime"));
}

TEST_CASE("the MyAnimeList login is saved encrypted and removed on logging out") {
    wxInitializer init;
    auto config = configFrom("");
    CHECK_FALSE(loadSettings(config).mal.loggedIn());

    Settings settings;
    settings.mal = {"khalil", {"access-token-1", "refresh-token-1", 1790000000}};
    saveSettings(config, settings);

    CHECK(config.Read("/MyAnimeList/User", "") == "khalil");
    const auto stored = config.Read("/MyAnimeList/RefreshToken", "").utf8_string();
    CHECK_FALSE(stored.empty());
    CHECK(stored.find("refresh-token-1") == std::string::npos);
    CHECK(config.Read("/MyAnimeList/AccessToken", "").utf8_string().find("access-token-1") == std::string::npos);
    CHECK(loadSettings(config).mal == settings.mal);

    config.Write("/MyAnimeList/RefreshToken", "tampered");
    CHECK(loadSettings(config).mal == MalAccount{});

    settings.mal = {};
    saveSettings(config, settings);
    CHECK_FALSE(config.HasGroup("/MyAnimeList"));
}

namespace {

RecentEntry watched(const std::string& provider, const std::string& id, const std::string& title,
                    const std::string& episode) {
    RecentEntry entry;
    entry.providerId = provider;
    entry.show.id = id;
    entry.show.title = title;
    entry.episode = episode;
    return entry;
}

}

TEST_CASE("rememberWatched keeps one entry per show, newest first") {
    std::vector<RecentEntry> recent;
    rememberWatched(recent, watched("hianime", "100", "One Piece", "750"));
    rememberWatched(recent, watched("hianime", "20", "Naruto", "25"));
    rememberWatched(recent, watched("hianime", "100", "One Piece", "751"));

    REQUIRE(recent.size() == 2);
    CHECK(recentLabel(recent[0]) == "One Piece episode 751");
    CHECK(recentLabel(recent[1]) == "Naruto episode 25");

    rememberWatched(recent, watched("miruro", "100", "Bleach", "3"));
    CHECK(recent.size() == 3);
    CHECK(recent[0].providerId == "miruro");
}

TEST_CASE("rememberWatched treats the same anime on another provider as the same entry") {
    std::vector<RecentEntry> recent;
    auto first = watched("hianime", "481", "Frieren: Beyond Journey's End", "3");
    first.show.altTitle = "Sousou no Frieren";
    rememberWatched(recent, first);
    rememberWatched(recent, watched("hianime", "20", "Naruto", "25"));

    rememberWatched(recent, watched("anizone", "mdkytdqp", "Sousou no Frieren", "4"));
    REQUIRE(recent.size() == 2);
    CHECK(recent[0].providerId == "anizone");
    CHECK(recent[0].episode == "4");
    CHECK(recent[1].show.title == "Naruto");

    auto byId = watched("miruro", "abc", "A different spelling", "5");
    byId.show.malId = 52991;
    recent[0].show.malId = 52991;
    rememberWatched(recent, byId);
    REQUIRE(recent.size() == 2);
    CHECK(recent[0].providerId == "miruro");

    rememberWatched(recent, watched("miruro", "def", "Frieren: Beyond Journey's End Season 2", "1"));
    CHECK(recent.size() == 3);
}

TEST_CASE("rememberWatched stops at fifty shows and drops the oldest") {
    std::vector<RecentEntry> recent;
    for (int i = 1; i <= 60; ++i) {
        rememberWatched(recent, watched("hianime", std::to_string(i), "Show " + std::to_string(i), "1"));
    }
    REQUIRE(recent.size() == recentLimit);
    CHECK(recent.front().show.title == "Show 60");
    CHECK(recent.back().show.title == "Show 11");
}

TEST_CASE("recently watched shows are saved with what's needed to load them again, and cleared when emptied") {
    wxInitializer init;
    auto config = configFrom("");
    CHECK(loadSettings(config).recent.empty());

    Settings settings;
    auto piece = watched("hianime", "100", "One Piece", "750");
    piece.show.altTitle = "ONE PIECE";
    piece.show.format = "TV";
    piece.show.year = 1999;
    piece.show.subEpisodes = 1180;
    piece.show.dubEpisodes = 1155;
    piece.show.malId = 21;
    piece.show.posterUrl = "https://example.test/one-piece.webp";
    piece.show.synopsis = "Not saved.";
    piece.audio = Audio::Dub;
    auto frieren = watched("anizone", "mdkytdqp", "Sousou no Frieren", "12.5");
    frieren.show.episodeCount = 28;
    frieren.show.pageUrl = "https://anizone.to/anime/mdkytdqp";
    settings.recent = {piece, frieren};
    saveSettings(config, settings);

    const auto loaded = loadSettings(config).recent;
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].providerId == "hianime");
    CHECK(loaded[0].show.id == "100");
    CHECK(loaded[0].show.title == "One Piece");
    CHECK(loaded[0].show.altTitle == "ONE PIECE");
    CHECK(loaded[0].show.format == "TV");
    CHECK(loaded[0].show.year == 1999);
    CHECK(loaded[0].show.subEpisodes == 1180);
    CHECK(loaded[0].show.dubEpisodes == 1155);
    CHECK(loaded[0].show.malId == 21);
    CHECK(loaded[0].show.posterUrl == "https://example.test/one-piece.webp");
    CHECK(loaded[0].show.synopsis.empty());
    CHECK(loaded[0].episode == "750");
    CHECK(loaded[0].audio == Audio::Dub);
    CHECK(loaded[1].providerId == "anizone");
    CHECK(loaded[1].show.episodeCount == 28);
    CHECK(loaded[1].show.pageUrl == "https://anizone.to/anime/mdkytdqp");
    CHECK(loaded[1].episode == "12.5");
    CHECK(loaded[1].audio == Audio::Sub);

    settings.recent.erase(settings.recent.begin());
    saveSettings(config, settings);
    const auto fewer = loadSettings(config).recent;
    REQUIRE(fewer.size() == 1);
    CHECK(fewer[0].show.title == "Sousou no Frieren");

    settings.recent.clear();
    saveSettings(config, settings);
    CHECK_FALSE(config.HasGroup("/Recent"));
}

TEST_CASE("an unknown provider in the file falls back to the first registered one") {
    wxInitializer init;
    auto config = configFrom("Provider=wcostream\nAudio=dub\n");
    const auto settings = loadSettings(config);
    CHECK(settings.providerId == "hianime");
    CHECK(settings.audio == Audio::Dub);
}

namespace {

std::filesystem::path scratchDirectory() {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("ryu-store-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    return directory;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

}

TEST_CASE("the settings store writes in the background and reloads what it saved") {
    wxInitializer init;
    const auto directory = scratchDirectory();
    const auto path = directory / "nested" / "settings.ini";
    {
        SettingsStore store(path);
        auto settings = store.load();
        CHECK(settings.audio == Audio::Sub);
        settings.audio = Audio::Dub;
        settings.readSubtitlesDubbed = true;
        store.save(settings);
        store.flush();
        CHECK(readText(path).find("Audio=dub") != std::string::npos);
    }
    SettingsStore reopened(path);
    CHECK(reopened.load().audio == Audio::Dub);
    CHECK(reopened.load().readSubtitlesFor(Audio::Dub));
    std::filesystem::remove_all(directory);
}

TEST_CASE("rapid saves end with the last one on disk, and closing the store writes what is pending") {
    wxInitializer init;
    const auto directory = scratchDirectory();
    const auto path = directory / "settings.ini";
    {
        SettingsStore store(path);
        Settings settings;
        for (int i = 0; i < 50; ++i) {
            settings.audio = i % 2 == 0 ? Audio::Dub : Audio::Sub;
            store.save(settings);
        }
        settings.providerId = "anizone";
        store.save(settings);
    }
    const auto text = readText(path);
    CHECK(text.find("Provider=anizone") != std::string::npos);
    CHECK(text.find("Audio=sub") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(directory / "settings.ini.tmp"));
    std::filesystem::remove_all(directory);
}

TEST_CASE("fitToArea leaves a window that already fits alone") {
    const WindowPlacement saved{420, 0, 1500, 1000, true};
    CHECK(fitToArea(saved, {0, 0, 3840, 2080}, 1140, 720) == saved);
}

TEST_CASE("fitToArea shrinks a window that's larger than its screen") {
    const auto fitted = fitToArea({420, 0, 3000, 2016, false}, {0, 0, 1920, 1040}, 1140, 720);
    CHECK(fitted == WindowPlacement{0, 0, 1920, 1040, false});
}

TEST_CASE("fitToArea moves a window hanging off an edge back onto its screen") {
    CHECK(fitToArea({1500, 700, 1000, 680, false}, {0, 0, 1920, 1040}, 760, 480) ==
          WindowPlacement{920, 360, 1000, 680, false});
    CHECK(fitToArea({-300, -40, 1000, 680, false}, {0, 0, 1920, 1040}, 760, 480) ==
          WindowPlacement{0, 0, 1000, 680, false});
    CHECK(fitToArea({-1500, 100, 1000, 680, false}, {-1920, 0, 1920, 1040}, 760, 480) ==
          WindowPlacement{-1500, 100, 1000, 680, false});
}

TEST_CASE("fitToArea never makes a window bigger than a screen smaller than the minimum size") {
    CHECK(fitToArea({0, 0, 1000, 680, false}, {0, 0, 640, 440}, 760, 480) == WindowPlacement{0, 0, 640, 440, false});
}

TEST_CASE("fitToArea grows a window saved below the minimum size") {
    CHECK(fitToArea({100, 100, 300, 200, false}, {0, 0, 1920, 1040}, 760, 480) ==
          WindowPlacement{100, 100, 760, 480, false});
}
