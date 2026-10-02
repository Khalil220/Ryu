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

TEST_CASE("showLabel falls back to the total episode count when audio is unknown") {
    Show show{"mdkytdqp", "Frieren: Beyond Journey's End", "Sousou no Frieren", "TV"};
    show.year = 2023;
    show.episodeCount = 28;
    CHECK(showLabel(show) == "Frieren: Beyond Journey's End, TV, 2023, 28 episodes");
    CHECK(showDetailsLine(show) == "TV  ·  2023  ·  28 episodes");
    show.episodeCount = 1;
    CHECK(showLabel(show) == "Frieren: Beyond Journey's End, TV, 2023, 1 episode");
}

TEST_CASE("showDetailsLine joins format, year, audio and genres") {
    Show frieren{"481", "Frieren", "", "TV", 28, 28, 2023};
    CHECK(showDetailsLine(frieren) == "TV  \u00b7  2023  \u00b7  28 subbed, 28 dubbed");
    frieren.genres = {"Adventure", "Fantasy"};
    CHECK(showDetailsLine(frieren) == "TV  \u00b7  2023  \u00b7  28 subbed, 28 dubbed  \u00b7  Adventure, Fantasy");
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
    CHECK(speakableSubtitle("KAA.lt\nHello there.", {"kaa.lt"}) == "Hello there.");
}

TEST_CASE("speakableSubtitle skips karaoke layers and syllables but keeps dialogue") {
    std::string layered;
    for (int i = 0; i < 42; ++i) {
        layered += "o\nshi\n";
    }
    CHECK(speakableSubtitle(layered, {}).empty());
    CHECK(speakableSubtitle(layered + "Um, I'm really sorry.", {}) == "Um, I'm really sorry.");
    CHECK(speakableSubtitle("hi\nro\nOn one spacious blue planet", {}) == "On one spacious blue planet");
    CHECK(speakableSubtitle("か\nき\nHello", {}) == "Hello");
    CHECK(speakableSubtitle("Chapter One\nChapter One", {}) == "Chapter One");
    CHECK(speakableSubtitle("Eh?", {}) == "Eh?");
    CHECK(speakableSubtitle("e", {}).empty());
    CHECK(speakableSubtitle("a\nEver since we first met", {}) == "Ever since we first met");
    CHECK(speakableSubtitle("No.\nThat's not what I mean.", {}) == "No. That's not what I mean.");
}

TEST_CASE("timeLabel includes the duration once it is known") {
    CHECK(timeLabel(201, 1450) == "3:21 of 24:10");
    CHECK(timeLabel(201, 0) == "3:21");
}

TEST_CASE("trackLabel prefers the title, then the language's name, then its code") {
    CHECK(trackLabel("English (US)", "en", 1) == "English (US)");
    CHECK(trackLabel("", "fil", 2) == "Filipino");
    CHECK(trackLabel("", "hin", 3) == "hin");
    CHECK(trackLabel("", "", 4) == "Track 4");
}

TEST_CASE("preferredAudioLanguages puts the chosen language ahead of the stream's own") {
    CHECK(preferredAudioLanguages("", "jpn,ja") == "jpn,ja");
    CHECK(preferredAudioLanguages("de", "eng,en") == "de,eng,en");
    CHECK(preferredAudioLanguages("hin", "") == "hin");
}

TEST_CASE("distinctTracks merges tracks with the same name and language, keeping the playing copy") {
    const std::vector<TrackEntry> tracks{{1, "Hindi", "hin", false},   {2, "Japanese", "jpn", false},
                                         {3, "English", "eng", false}, {4, "Hindi", "hin", false},
                                         {5, "Japanese", "jpn", true}, {6, "English", "eng", false}};

    const auto distinct = distinctTracks(tracks);

    REQUIRE(distinct.size() == 3);
    CHECK(distinct[0].id == 1);
    CHECK(distinct[1].id == 5);
    CHECK(distinct[1].selected);
    CHECK(distinct[2].id == 3);
}

TEST_CASE("distinctTracks keeps tracks that share a language but not a name") {
    const std::vector<TrackEntry> tracks{{1, "English", "eng", true}, {2, "English (CC)", "eng", false}};
    CHECK(distinctTracks(tracks).size() == 2);
}

TEST_CASE("inRequestedOrder puts added tracks back in the order they were requested") {
    const std::vector<std::string> requested{"https://x/ar.vtt", "https://x/en.vtt", "https://x/fr.vtt"};
    const std::vector<TrackEntry> tracks{{1, "Embedded", "ja", false, ""},
                                         {2, "French", "fr", false, "https://x/fr.vtt"},
                                         {3, "Arabic", "ar", false, "https://x/ar.vtt"},
                                         {4, "English", "en", true, "https://x/en.vtt"}};

    const auto ordered = inRequestedOrder(tracks, requested);

    REQUIRE(ordered.size() == 4);
    CHECK(ordered[0].label == "Embedded");
    CHECK(ordered[1].label == "Arabic");
    CHECK(ordered[2].label == "English");
    CHECK(ordered[3].label == "French");
}

TEST_CASE("an episode counts as watched once 85 percent of it has played") {
    CHECK_FALSE(countsAsWatched(0, 1440));
    CHECK_FALSE(countsAsWatched(1223, 1440));
    CHECK(countsAsWatched(1224, 1440));
    CHECK(countsAsWatched(1440, 1440));
    CHECK_FALSE(countsAsWatched(10, 0));
}

namespace {

MalAnime malEntry(std::string title, std::string english, int episodes, MalStatus status, int watched, int score) {
    MalAnime anime;
    anime.id = 1;
    anime.title = std::move(title);
    anime.englishTitle = std::move(english);
    anime.mediaType = "tv";
    anime.episodes = episodes;
    anime.year = 2013;
    anime.list.status = status;
    anime.list.watched = watched;
    anime.list.score = score;
    return anime;
}

}

TEST_CASE("MAL entries read with their English title, progress and score") {
    CHECK(malEntryLabel(malEntry("Shingeki no Kyojin", "Attack on Titan", 25, MalStatus::Watching, 11, 0)) ==
          "Attack on Titan, 11 of 25 episodes");
    CHECK(malEntryLabel(malEntry("Karakai Jouzu no Takagi-san", "", 12, MalStatus::Completed, 12, 10)) ==
          "Karakai Jouzu no Takagi-san, 12 episodes, score 10");
    CHECK(malEntryLabel(malEntry("One Piece", "One Piece", 0, MalStatus::OnHold, 751, 10)) ==
          "One Piece, 751 episodes watched, score 10");
    CHECK(malEntryLabel(malEntry("X", "", 12, MalStatus::PlanToWatch, 0, 0)) == "X, 12 episodes");
    CHECK(malEntryLabel(malEntry("X", "", 0, MalStatus::PlanToWatch, 0, 0)) == "X");
    CHECK(malEntryLabel(malEntry("A Movie", "", 1, MalStatus::Completed, 1, 8)) == "A Movie, 1 episode, score 8");
    auto rewatch = malEntry("X", "", 12, MalStatus::Completed, 3, 9);
    rewatch.list.rewatching = true;
    CHECK(malEntryLabel(rewatch) == "X, 3 of 12 episodes, rewatching, score 9");
}

TEST_CASE("MAL candidates read with type, year, length and the list they're on") {
    CHECK(malCandidateLabel(malEntry("Shingeki no Kyojin", "Attack on Titan", 25, MalStatus::Watching, 11, 0)) ==
          "Attack on Titan, TV, 2013, 25 episodes, on your Watching list");
    auto movie = malEntry("Kimi no Na wa.", "Your Name.", 1, MalStatus::None, 0, 0);
    movie.mediaType = "movie";
    movie.year = 2016;
    CHECK(malCandidateLabel(movie) == "Your Name., Movie, 2016, 1 episode");
    auto special = malEntry("X", "", 0, MalStatus::PlanToWatch, 0, 0);
    special.mediaType = "tv_special";
    special.year = 0;
    CHECK(malCandidateLabel(special) == "X, Tv special, on your Plan to watch list");
    CHECK(malStatusLabel(MalStatus::OnHold) == "On hold");
    CHECK(malStatusLabel(MalStatus::None) == "Not on my list");
}

TEST_CASE("the progress message says which episode was marked, or that the show is finished") {
    const auto anime = malEntry("Shingeki no Kyojin", "Attack on Titan", 25, MalStatus::Watching, 11, 0);
    MalChanges next;
    next.watched = 12;
    CHECK(malProgressMessage(anime, next) == "Episode 12 marked as watched on MyAnimeList");
    MalChanges last;
    last.watched = 25;
    last.status = MalStatus::Completed;
    CHECK(malProgressMessage(anime, last) == "Finished Attack on Titan, moved to Completed on MyAnimeList");
}
