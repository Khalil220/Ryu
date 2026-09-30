#include "format.hpp"

#include "language.hpp"
#include "stream_finder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace ryu {

namespace {

bool titleOnlyRepeatsNumber(const Episode& episode) {
    constexpr std::string_view prefix = "episode ";
    const std::string_view title = episode.title;
    return title.size() > prefix.size() &&
           std::ranges::equal(title.substr(0, prefix.size()), prefix,
                              [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == b; }) &&
           sameEpisodeNumber(title.substr(prefix.size()), episode.number);
}

}

std::string showLabel(const Show& show) {
    std::string label = show.title;
    if (!show.format.empty()) {
        label += ", " + show.format;
    }
    if (show.year > 0) {
        label += ", " + std::to_string(show.year);
    }
    if (show.subEpisodes == 0 && show.dubEpisodes == 0) {
        if (show.offersSub && show.offersDub) {
            return label + ", subbed and dubbed";
        }
        if (show.offersSub || show.offersDub) {
            return label + (show.offersSub ? ", subbed" : ", dubbed");
        }
        if (show.episodeCount > 0) {
            return label + ", " + std::to_string(show.episodeCount) + (show.episodeCount == 1 ? " episode" : " episodes");
        }
        return label + ", no episodes yet";
    }
    if (show.subEpisodes > 0) {
        label += ", " + std::to_string(show.subEpisodes) + " subbed";
    }
    if (show.dubEpisodes > 0) {
        label += ", " + std::to_string(show.dubEpisodes) + " dubbed";
    }
    return label;
}

std::string showDetailsLine(const Show& show) {
    std::vector<std::string> parts;
    if (!show.format.empty()) {
        parts.push_back(show.format);
    }
    if (show.year > 0) {
        parts.push_back(std::to_string(show.year));
    }
    std::string audio;
    if (show.subEpisodes > 0) {
        audio = std::to_string(show.subEpisodes) + " subbed";
    }
    if (show.dubEpisodes > 0) {
        audio += (audio.empty() ? "" : ", ") + std::to_string(show.dubEpisodes) + " dubbed";
    }
    if (audio.empty() && (show.offersSub || show.offersDub)) {
        audio = show.offersSub && show.offersDub ? "Subbed and dubbed" : show.offersSub ? "Subbed" : "Dubbed";
    }
    if (audio.empty() && show.episodeCount > 0) {
        audio = std::to_string(show.episodeCount) + (show.episodeCount == 1 ? " episode" : " episodes");
    }
    if (!audio.empty()) {
        parts.push_back(audio);
    }
    std::string genres;
    for (const auto& genre : show.genres) {
        genres += (genres.empty() ? "" : ", ") + genre;
    }
    if (!genres.empty()) {
        parts.push_back(genres);
    }
    std::string line;
    for (const auto& part : parts) {
        line += (line.empty() ? "" : "  \u00b7  ") + part;
    }
    return line;
}

std::string episodeLabel(const Episode& episode) {
    if (episode.number.empty()) {
        return episode.title;
    }
    std::string label = "Episode " + episode.number;
    if (!episode.title.empty() && !titleOnlyRepeatsNumber(episode)) {
        label += ": " + episode.title;
    }
    return label;
}

std::string formatClock(double seconds) {
    const long long total = std::isfinite(seconds) && seconds > 0 ? static_cast<long long>(seconds) : 0;
    const long long hours = total / 3600;
    const long long minutes = total / 60 % 60;
    const long long secs = total % 60;
    char buffer[32];
    if (hours > 0) {
        std::snprintf(buffer, sizeof buffer, "%lld:%02lld:%02lld", hours, minutes, secs);
    } else {
        std::snprintf(buffer, sizeof buffer, "%lld:%02lld", minutes, secs);
    }
    return buffer;
}

std::string speakableSubtitle(std::string_view raw, const std::vector<std::string>& noise) {
    const auto collapse = [](std::string_view line) {
        std::string text;
        bool pendingSpace = false;
        for (const char ch : line) {
            if (std::isspace(static_cast<unsigned char>(ch))) {
                pendingSpace = !text.empty();
                continue;
            }
            if (pendingSpace) {
                text.push_back(' ');
                pendingSpace = false;
            }
            text.push_back(ch);
        }
        return text;
    };
    const auto lower = [](std::string value) {
        std::ranges::transform(value, value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    const auto characters = [](const std::string& line) {
        return std::ranges::count_if(line, [](unsigned char c) { return (c & 0xC0) != 0x80; });
    };
    const auto isShort = [&](const std::string& line) { return characters(line) <= 3; };

    std::vector<std::string> lines;
    for (size_t start = 0; start <= raw.size();) {
        const auto end = std::min(raw.find('\n', start), raw.size());
        if (auto line = collapse(raw.substr(start, end - start)); !line.empty()) {
            lines.push_back(std::move(line));
        }
        start = end + 1;
    }
    std::vector<std::string> distinct;
    for (const auto& line : lines) {
        if (std::ranges::find(distinct, line) == distinct.end()) {
            distinct.push_back(line);
        }
    }
    const bool karaoke = std::ranges::count_if(distinct, isShort) >= 2;
    std::string text;
    for (const auto& line : distinct) {
        if (characters(line) == 1 || std::ranges::count(lines, line) >= 3 || (karaoke && isShort(line)) ||
            std::ranges::any_of(noise, [&](const std::string& entry) { return lower(entry) == lower(line); })) {
            continue;
        }
        text += (text.empty() ? "" : " ") + line;
    }
    return text;
}

std::string timeLabel(double position, double duration) {
    if (!std::isfinite(duration) || duration <= 0) {
        return formatClock(position);
    }
    return formatClock(position) + " of " + formatClock(duration);
}

std::string trackLabel(std::string_view title, std::string_view language, int64_t id) {
    if (!title.empty()) {
        return std::string(title);
    }
    if (auto name = languageName(language); !name.empty()) {
        return name;
    }
    return language.empty() ? "Track " + std::to_string(id) : std::string(language);
}

std::string preferredAudioLanguages(std::string_view preferred, std::string_view fallback) {
    if (preferred.empty()) {
        return std::string(fallback);
    }
    if (fallback.empty()) {
        return std::string(preferred);
    }
    return std::string(preferred) + "," + std::string(fallback);
}

std::vector<TrackEntry> distinctTracks(const std::vector<TrackEntry>& tracks) {
    std::vector<TrackEntry> result;
    for (const auto& track : tracks) {
        const auto same = std::ranges::find_if(result, [&](const TrackEntry& kept) {
            return kept.label == track.label && kept.language == track.language;
        });
        if (same == result.end()) {
            result.push_back(track);
        } else if (track.selected) {
            *same = track;
        }
    }
    return result;
}

std::vector<TrackEntry> inRequestedOrder(std::vector<TrackEntry> tracks, const std::vector<std::string>& requested) {
    const auto rank = [&](const TrackEntry& track) {
        const auto it = std::ranges::find(requested, track.source);
        return it == requested.end() ? 0 : std::distance(requested.begin(), it) + 1;
    };
    std::ranges::stable_sort(tracks, {}, rank);
    return tracks;
}

}
