#include "settings.hpp"

#include <doctest/doctest.h>

#include <wx/fileconf.h>
#include <wx/init.h>
#include <wx/sstream.h>

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
}

TEST_CASE("saved settings load back unchanged") {
    wxInitializer init;
    auto config = configFrom("");
    Settings settings;
    settings.audio = Audio::Dub;
    settings.baseUrlOverrides["hianime"] = "https://mirror.example";
    saveSettings(config, settings);

    const auto loaded = loadSettings(config);
    CHECK(loaded.audio == Audio::Dub);
    CHECK(loaded.baseUrl() == "https://mirror.example");
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

TEST_CASE("an unknown provider in the file falls back to the first registered one") {
    wxInitializer init;
    auto config = configFrom("Provider=wcostream\nAudio=dub\n");
    const auto settings = loadSettings(config);
    CHECK(settings.providerId == "hianime");
    CHECK(settings.audio == Audio::Dub);
}
