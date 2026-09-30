#pragma once

#include "provider.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ryu {

struct SubtitlePreference {
    enum class Kind { SiteDefault, Off, Track };

    Kind kind = Kind::SiteDefault;
    std::string label;
    std::string language;

    bool operator==(const SubtitlePreference&) const = default;
};

std::optional<size_t> pickSubtitle(const std::vector<Subtitle>& tracks, const SubtitlePreference& preference);

}
