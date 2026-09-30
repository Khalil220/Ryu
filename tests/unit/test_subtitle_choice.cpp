#include "subtitle_choice.hpp"

#include <doctest/doctest.h>

using namespace ryu;

namespace {

const std::vector<Subtitle> megaplayTracks{
    {"a.vtt", "ara", "Arabic", false},
    {"e.vtt", "eng", "English", true},
    {"s.vtt", "spa", "Spanish", false},
    {"l.vtt", "spa", "Spanish (- Spanish(Latin America))", false},
};

SubtitlePreference track(std::string label, std::string language) {
    return {SubtitlePreference::Kind::Track, std::move(label), std::move(language)};
}

}

TEST_CASE("with no choice made, the site's default track is picked") {
    CHECK(pickSubtitle(megaplayTracks, {}) == 1u);
    CHECK_FALSE(pickSubtitle({{"a.vtt", "ara", "Arabic", false}}, {}).has_value());
}

TEST_CASE("Off picks no track at all") {
    CHECK_FALSE(pickSubtitle(megaplayTracks, {SubtitlePreference::Kind::Off}).has_value());
}

TEST_CASE("a chosen track is found by its exact name first") {
    CHECK(pickSubtitle(megaplayTracks, track("Spanish (- Spanish(Latin America))", "spa")) == 3u);
    CHECK(pickSubtitle(megaplayTracks, track("Spanish", "spa")) == 2u);
}

TEST_CASE("a chosen track falls back to the same language, then the same first word") {
    CHECK(pickSubtitle(megaplayTracks, track("Espanol", "spa")) == 2u);
    const std::vector<Subtitle> zoko{{"ai.vtt", "en", "English (AI)", true}, {"cc.vtt", "en", "English (CC)", false}};
    CHECK(pickSubtitle(zoko, track("English", "eng")) == 0u);
    CHECK(pickSubtitle(zoko, track("English (CC)", "en")) == 1u);
}

TEST_CASE("a chosen track that the episode lacks falls back to the site's default") {
    CHECK(pickSubtitle(megaplayTracks, track("German", "ger")) == 1u);
}
