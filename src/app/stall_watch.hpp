#pragma once

namespace ryu {

class StallWatch {
public:
    explicit StallWatch(int limitSeconds = 15) : limit_(limitSeconds) {}

    void reset();
    bool tick(double position, bool playing);

private:
    int limit_;
    double lastPosition_ = -1;
    int stalledSeconds_ = 0;
    bool reported_ = false;
};

}
