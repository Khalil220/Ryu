#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ryu {

class SubtitleReader {
public:
    void setNoise(std::vector<std::string> noise);
    void clear();
    std::string next(std::string_view subText);

private:
    std::vector<std::string> noise_;
    std::vector<std::string> onScreen_;
};

}
