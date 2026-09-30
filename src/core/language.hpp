#pragma once

#include <string>
#include <string_view>

namespace ryu {

std::string languageName(std::string_view code);
bool isLanguageName(std::string_view name);

}
