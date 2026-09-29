#pragma once

#include "provider.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ryu {

std::string showLabel(const Show& show);
std::string episodeLabel(const Episode& episode);
std::string formatClock(double seconds);
std::string timeLabel(double position, double duration);
std::string speakableSubtitle(std::string_view raw, const std::vector<std::string>& noise);

}
