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
