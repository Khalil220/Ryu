#pragma once

#include "provider.hpp"

#include <string>

namespace ryu {

std::string showLabel(const Show& show);
std::string episodeLabel(const Episode& episode);
std::string formatClock(double seconds);
std::string timeLabel(double position, double duration);

}
