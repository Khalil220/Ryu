#pragma once

#include "../provider.hpp"

namespace ryu {

class KickAssAnimeProvider : public Provider {
public:
    static constexpr const char* defaultBaseUrl = "https://kaa.lt";

    KickAssAnimeProvider(HttpClient& http, std::string baseUrl = defaultBaseUrl);

    std::vector<Show> search(std::string_view query) override;
    std::vector<Episode> episodes(std::string_view showId) override;
    std::vector<Stream> streams(std::string_view episodeId, Audio audio) override;

private:
    std::string fetch(const std::string& url);
    Stream resolvePlayer(const std::string& playerUrl, Audio audio);

    HttpClient& http_;
    std::string baseUrl_;
};

}
