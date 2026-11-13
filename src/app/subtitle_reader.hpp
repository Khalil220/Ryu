#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ryu {

class SubtitleReader {
public:
    void clear();
    std::string next(std::string_view subText);

private:
    std::vector<std::string> onScreen_;
};

}
