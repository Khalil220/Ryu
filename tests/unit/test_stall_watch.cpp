#include "stall_watch.hpp"

#include <doctest/doctest.h>

using namespace ryu;

namespace {

int ticksUntilReport(StallWatch& watch, double position, int limit) {
    for (int second = 1; second <= limit; ++second) {
        if (watch.tick(position, true)) {
            return second;
        }
    }
    return 0;
}

}

TEST_CASE("a clock that stops moving is reported once after the limit") {
    StallWatch watch(15);
    CHECK_FALSE(watch.tick(0, true));
    CHECK(ticksUntilReport(watch, 0, 30) == 15);
    CHECK(ticksUntilReport(watch, 0, 30) == 0);
}

TEST_CASE("steady playback is never reported") {
    StallWatch watch(15);
    for (int second = 0; second < 60; ++second) {
        CHECK_FALSE(watch.tick(second, true));
    }
}

TEST_CASE("playing again after seeking back counts as progress") {
    StallWatch watch(15);
    watch.tick(600, true);
    for (int second = 300; second < 360; ++second) {
        CHECK_FALSE(watch.tick(second, true));
    }
}

TEST_CASE("pausing restarts the count") {
    StallWatch watch(15);
    watch.tick(10, true);
    ticksUntilReport(watch, 10, 10);
    CHECK_FALSE(watch.tick(10, false));
    CHECK(ticksUntilReport(watch, 10, 30) == 15);
}

TEST_CASE("progress after a report allows the next stall to be reported") {
    StallWatch watch(15);
    watch.tick(10, true);
    REQUIRE(ticksUntilReport(watch, 10, 15) == 15);
    CHECK_FALSE(watch.tick(11, true));
    CHECK(ticksUntilReport(watch, 11, 15) == 15);
}

TEST_CASE("reset forgets the last position") {
    StallWatch watch(15);
    watch.tick(0, true);
    ticksUntilReport(watch, 0, 14);
    watch.reset();
    CHECK_FALSE(watch.tick(0, true));
    CHECK(ticksUntilReport(watch, 0, 30) == 15);
}
