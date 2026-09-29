#include "format.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace ryu {

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

std::string episodeLabel(const Episode& episode) {
    if (episode.number.empty()) {
        return episode.title;
    }
    std::string label = "Episode " + episode.number;
    if (!episode.title.empty()) {
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
    std::string text;
    bool pendingSpace = false;
    for (const char ch : raw) {
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
    const auto lower = [](std::string value) {
        std::ranges::transform(value, value.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    };
    const auto folded = lower(text);
    if (std::ranges::any_of(noise, [&](const std::string& line) { return lower(line) == folded; })) {
        return {};
    }
    return text;
}

std::string timeLabel(double position, double duration) {
    if (!std::isfinite(duration) || duration <= 0) {
        return formatClock(position);
    }
    return formatClock(position) + " of " + formatClock(duration);
}

}
