#pragma once

#include "http.hpp"
#include "playlist_server.hpp"
#include "provider.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ryu {

struct ProviderHandle {
    std::string name;
    Provider* provider = nullptr;
};

struct FoundStream {
    Stream stream;
    std::string providerName;
    bool fromFallback = false;
};

std::string normalizeTitle(std::string_view title);
std::optional<Show> matchShow(const Show& wanted, const std::vector<Show>& candidates);
bool sameEpisodeNumber(std::string_view left, std::string_view right);

FoundStream findStream(HttpClient& http, PlaylistServer& server, const std::vector<ProviderHandle>& providers,
                       const Show& show, const Episode& episode, Audio audio);

}
