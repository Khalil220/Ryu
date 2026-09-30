#include "format.hpp"

#include <doctest/doctest.h>

#include <limits>

using namespace ryu;

TEST_CASE("showLabel lists format and episode counts") {
    CHECK(showLabel({"481", "Frieren: Beyond Journey's End", "Sousou no Frieren", "TV", 28, 28}) ==
          "Frieren: Beyond Journey's End, TV, 28 subbed, 28 dubbed");
    CHECK(showLabel({"4164", "One Piece Movie 1", "", "MOVIE", 1, 0}) == "One Piece Movie 1, MOVIE, 1 subbed");
    CHECK(showLabel({"1", "Dub Only", "", "", 0, 3}) == "Dub Only, 3 dubbed");
    CHECK(showLabel({"7220", "Sousou no Frieren 3rd Season", "", "TV (? eps)", 0, 0}) ==
          "Sousou no Frieren 3rd Season, TV (? eps), no episodes yet");
}

TEST_CASE("showDetailsLine joins format, year, audio and genres") {
    Show frieren{"481", "Frieren", "", "TV", 28, 28, 2023};
    CHECK(showDetailsLine(frieren) == "TV  \u00b7  2023  \u00b7  28 subbed, 28 dubbed");
    frieren.genres = {"Adventure", "Fantasy"};
    CHECK(showDetailsLine(frieren) == "TV  \u00b7  2023  \u00b7  28 subbed, 28 dubbed  \u00b7  Adventure, Fantasy");
    Show kaa{"x", "X", "", "TV", 0, 0, 0, true, true};
    CHECK(showDetailsLine(kaa) == "TV  \u00b7  Subbed and dubbed");
    CHECK(showDetailsLine(Show{"y", "Y", "", "", 0, 4}) == "4 dubbed");
    CHECK(showDetailsLine(Show{"z"}).empty());
}

TEST_CASE("episodeLabel combines number and title") {
    CHECK(episodeLabel({"9227", "1", "The Journey's End"}) == "Episode 1: The Journey's End");
    CHECK(episodeLabel({"9", "12.5", ""}) == "Episode 12.5");
    CHECK(episodeLabel({"9", "", "Special"}) == "Special");
}

TEST_CASE("episodeLabel leaves out a title that only repeats the number") {
    CHECK(episodeLabel({"1", "1", "Episode 1"}) == "Episode 1");
    CHECK(episodeLabel({"1", "1180", "episode 1180"}) == "Episode 1180");
    CHECK(episodeLabel({"1", "7", "Episode 07"}) == "Episode 7");
    CHECK(episodeLabel({"1", "12.5", "Episode 12.5"}) == "Episode 12.5");
    CHECK(episodeLabel({"1", "5", "Episode 5: The Return"}) == "Episode 5: Episode 5: The Return");
    CHECK(episodeLabel({"1", "2", "Episode 1"}) == "Episode 2: Episode 1");
    CHECK(episodeLabel({"1", "3", "Episodes"}) == "Episode 3: Episodes");
}

TEST_CASE("formatClock switches to hours only when needed") {
    CHECK(formatClock(0) == "0:00");
    CHECK(formatClock(5.9) == "0:05");
    CHECK(formatClock(201) == "3:21");
    CHECK(formatClock(3723) == "1:02:03");
    CHECK(formatClock(-4) == "0:00");
    CHECK(formatClock(std::numeric_limits<double>::quiet_NaN()) == "0:00");
}

TEST_CASE("speakableSubtitle joins lines, tidies spacing and drops watermark lines") {
    CHECK(speakableSubtitle("At the northernmost end\nof the continent,", {}) ==
          "At the northernmost end of the continent,");
    CHECK(speakableSubtitle("  Frieren.  \r\n ", {}) == "Frieren.");
    CHECK(speakableSubtitle("KAA.lt", {"kaa.lt"}).empty());
    CHECK(speakableSubtitle("Visit kaa.lt later", {"kaa.lt"}) == "Visit kaa.lt later");
    CHECK(speakableSubtitle("\n", {}).empty());
}

TEST_CASE("timeLabel includes the duration once it is known") {
    CHECK(timeLabel(201, 1450) == "3:21 of 24:10");
    CHECK(timeLabel(201, 0) == "3:21");
}
