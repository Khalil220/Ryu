#include "subtitle_choice.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace ryu {

namespace {

std::string firstWord(std::string_view label) {
    std::string word;
    for (const char ch : label) {
        if (std::isalpha(static_cast<unsigned char>(ch))) {
            word += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        } else if (!word.empty()) {
            break;
        }
    }
    return word;
}

template <typename Match>
std::optional<size_t> findTrack(const std::vector<Subtitle>& tracks, Match match) {
    const auto it = std::ranges::find_if(tracks, match);
    return it == tracks.end() ? std::nullopt : std::optional<size_t>(static_cast<size_t>(it - tracks.begin()));
}

}

std::optional<size_t> pickSubtitle(const std::vector<Subtitle>& tracks, const SubtitlePreference& preference) {
    if (preference.kind == SubtitlePreference::Kind::Off) {
        return std::nullopt;
    }
    if (preference.kind == SubtitlePreference::Kind::Track) {
        if (const auto index = findTrack(tracks, [&](const Subtitle& track) { return track.label == preference.label; })) {
            return index;
        }
        if (!preference.language.empty()) {
            if (const auto index =
                    findTrack(tracks, [&](const Subtitle& track) { return track.language == preference.language; })) {
                return index;
            }
        }
        if (const auto word = firstWord(preference.label); !word.empty()) {
            if (const auto index =
                    findTrack(tracks, [&](const Subtitle& track) { return firstWord(track.label) == word; })) {
                return index;
            }
        }
    }
    return findTrack(tracks, [](const Subtitle& track) { return track.isDefault; });
}

}
