#include "stall_watch.hpp"

#include <cmath>

namespace ryu {

void StallWatch::reset() {
    lastPosition_ = -1;
    stalledSeconds_ = 0;
    reported_ = false;
}

bool StallWatch::tick(double position, bool playing) {
    if (!playing) {
        stalledSeconds_ = 0;
        return false;
    }
    if (std::abs(position - lastPosition_) > 0.2) {
        lastPosition_ = position;
        stalledSeconds_ = 0;
        reported_ = false;
        return false;
    }
    if (++stalledSeconds_ < limit_ || reported_) {
        return false;
    }
    reported_ = true;
    return true;
}

}
