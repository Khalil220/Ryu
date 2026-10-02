#pragma once

#include <string>
#include <string_view>

namespace ryu {

std::string protectSecret(std::string_view secret);
std::string revealSecret(std::string_view stored);

}
