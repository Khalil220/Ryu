#pragma once

#include "provider.hpp"
#include "registry.hpp"

#include <map>
#include <string>

class wxConfigBase;

namespace ryu {

struct Settings {
    std::string providerId = "hianime";
    std::map<std::string, std::string> baseUrlOverrides;
    Audio audio = Audio::Sub;

    const ProviderInfo& provider() const;
    std::string baseUrl() const;
};

Settings loadSettings(wxConfigBase& config);
void saveSettings(wxConfigBase& config, const Settings& settings);

}
