#include "subtitle_reader.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace ryu;

namespace {

std::string spoken(std::string_view subText) {
    SubtitleReader reader;
    return reader.next(subText);
}

std::vector<std::string> spokenInTurn(const std::vector<std::string>& subTexts) {
    SubtitleReader reader;
    std::vector<std::string> result;
    for (const auto& subText : subTexts) {
        result.push_back(reader.next(subText));
    }
    return result;
}

}

TEST_CASE("subtitle lines are joined and tidied") {
    CHECK(spoken("At the northernmost end\nof the continent,") == "At the northernmost end of the continent,");
    CHECK(spoken("  Frieren.  \r\n ") == "Frieren.");
    CHECK(spoken("\n").empty());
}

TEST_CASE("karaoke layers and syllables are skipped but dialogue is kept") {
    std::string layered;
    for (int i = 0; i < 42; ++i) {
        layered += "o\nshi\n";
    }
    CHECK(spoken(layered).empty());
    CHECK(spoken(layered + "Um, I'm really sorry.") == "Um, I'm really sorry.");
    CHECK(spoken("hi\nro\nOn one spacious blue planet") == "On one spacious blue planet");
    CHECK(spoken("か\nき\nHello") == "Hello");
    CHECK(spoken("Chapter One\nChapter One") == "Chapter One");
    CHECK(spoken("Eh?") == "Eh?");
    CHECK(spoken("e").empty());
    CHECK(spoken("a\nEver since we first met") == "Ever since we first met");
    CHECK(spoken("No.\nThat's not what I mean.") == "No. That's not what I mean.");
}

TEST_CASE("lines that pile up on screen are each read once") {
    CHECK(spokenInTurn({"",
                        "Mom: Where are you?",
                        "Mom: Where are you?\nMe: On my way.",
                        "Mom: Where are you?\nMe: On my way.\nMom: Hurry up!\nDinner is getting cold.",
                        ""}) ==
          std::vector<std::string>{"", "Mom: Where are you?", "Me: On my way.", "Mom: Hurry up! Dinner is getting cold.",
                                   ""});
}

TEST_CASE("a line fading in over the previous one is read once, and the previous one isn't read again") {
    CHECK(spokenInTurn({"First line fading out.", "First line fading out.\nSecond line fading in.",
                        "Second line fading in.", ""}) ==
          std::vector<std::string>{"First line fading out.", "Second line fading in.", "", ""});
}

TEST_CASE("a line is read again after it has left the screen") {
    CHECK(spokenInTurn({"Wait!", "", "Wait!"}) == std::vector<std::string>{"Wait!", "", "Wait!"});
    CHECK(spokenInTurn({"Wait!", "Wait!"}) == std::vector<std::string>{"Wait!", ""});
}

TEST_CASE("dialogue under changing karaoke is read once") {
    CHECK(spokenInTurn({"o\no\no\nUm, I'm really sorry.", "shi\nshi\nshi\nUm, I'm really sorry.", "ka\nka\nka"}) ==
          std::vector<std::string>{"Um, I'm really sorry.", "", ""});
}

TEST_CASE("clearing forgets what was on screen") {
    SubtitleReader reader;
    CHECK(reader.next("Frieren.") == "Frieren.");
    reader.clear();
    CHECK(reader.next("Frieren.") == "Frieren.");
}
