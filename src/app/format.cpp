#include "format.hpp"

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

std::string timeLabel(double position, double duration) {
    if (!std::isfinite(duration) || duration <= 0) {
        return formatClock(position);
    }
    return formatClock(position) + " of " + formatClock(duration);
}

}
