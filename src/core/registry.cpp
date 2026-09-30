#include "registry.hpp"

#include "providers/anizone.hpp"
#include "providers/hianime.hpp"
#include "providers/kickassanime.hpp"

#include <algorithm>

namespace ryu {

const std::vector<ProviderInfo>& availableProviders() {
    static const std::vector<ProviderInfo> providers{
        {"hianime", "HiAnime", HiAnimeProvider::defaultBaseUrl,
         [](HttpClient& http, const std::string& baseUrl) { return std::make_unique<HiAnimeProvider>(http, baseUrl); }},
        {"kickassanime", "KickAssAnime", KickAssAnimeProvider::defaultBaseUrl,
         [](HttpClient& http, const std::string& baseUrl) {
             return std::make_unique<KickAssAnimeProvider>(http, baseUrl);
         }},
        {"anizone", "AniZone", AniZoneProvider::defaultBaseUrl,
         [](HttpClient& http, const std::string& baseUrl) { return std::make_unique<AniZoneProvider>(http, baseUrl); }},
    };
    return providers;
}

const ProviderInfo* findProvider(std::string_view id) {
    const auto& providers = availableProviders();
    const auto it = std::ranges::find(providers, id, &ProviderInfo::id);
    return it == providers.end() ? nullptr : &*it;
}

}
