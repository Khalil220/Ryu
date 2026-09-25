#pragma once

#include "provider.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ryu {

struct ProviderInfo {
    std::string id;
    std::string name;
    std::string defaultBaseUrl;
    std::function<std::unique_ptr<Provider>(HttpClient&, const std::string&)> create;
};

const std::vector<ProviderInfo>& availableProviders();
const ProviderInfo* findProvider(std::string_view id);

}
