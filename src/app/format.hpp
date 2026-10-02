#pragma once

#include "mal.hpp"
#include "provider.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ryu {

struct TrackEntry {
    int64_t id = 0;
    std::string label;
    std::string language;
    bool selected = false;
    std::string source;
};

std::string showLabel(const Show& show);
std::string showDetailsLine(const Show& show);
std::string episodeLabel(const Episode& episode);
std::string formatClock(double seconds);
std::string timeLabel(double position, double duration);
std::string speakableSubtitle(std::string_view raw, const std::vector<std::string>& noise);
std::string trackLabel(std::string_view title, std::string_view language, int64_t id);
std::string preferredAudioLanguages(std::string_view preferred, std::string_view fallback);
std::vector<TrackEntry> distinctTracks(const std::vector<TrackEntry>& tracks);
std::vector<TrackEntry> inRequestedOrder(std::vector<TrackEntry> tracks, const std::vector<std::string>& requested);
bool countsAsWatched(double position, double duration);
std::string malTitle(const MalAnime& anime);
std::string malStatusLabel(MalStatus status);
std::string malEntryLabel(const MalAnime& anime);
std::string malCandidateLabel(const MalAnime& anime);
std::string malProgressMessage(const MalAnime& anime, const MalChanges& changes);
std::string malWatchedText(int watched, int episodes);
int malWatchedFrom(std::string_view text, int episodes, int unchanged);
int leadingNumber(std::string_view text);

enum class MalDatePart { None, Year, Month, Day };
MalDatePart wrongMalDatePart(const MalDate& date, int thisYear);

}
