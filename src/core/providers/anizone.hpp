#pragma once

#include "../provider.hpp"

namespace ryu {

class AniZoneProvider : public Provider {
public:
    static constexpr const char* defaultBaseUrl = "https://anizone.to";

    AniZoneProvider(HttpClient& http, std::string baseUrl = defaultBaseUrl);

    std::vector<Show> search(std::string_view query) override;
    std::vector<Episode> episodes(const Show& show) override;
    std::vector<Stream> streams(std::string_view episodeId, Audio audio) override;
    Show describe(const Show& show) override;

private:
    HttpResponse fetch(const std::string& url);

    HttpClient& http_;
    std::string baseUrl_;
};

}
