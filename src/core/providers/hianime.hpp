#pragma once

#include "../provider.hpp"
#include "../resolvers/megaplay.hpp"

namespace ryu {

class HiAnimeProvider : public Provider {
public:
    static constexpr const char* defaultBaseUrl = "https://hianime.at";

    HiAnimeProvider(HttpClient& http, std::string baseUrl = defaultBaseUrl);

    std::vector<Show> search(std::string_view query) override;
    std::vector<Episode> episodes(const Show& show) override;
    std::vector<Stream> streams(std::string_view episodeId, Audio audio) override;
    Show describe(const Show& show) override;

private:
    std::string fetch(const std::string& url, const Headers& headers = {});
    std::string fetchHtmlFragment(const std::string& url);
    Stream resolveZoko(const std::string& embedUrl, Audio audio);

    HttpClient& http_;
    std::string baseUrl_;
    MegaplayResolver megaplay_;
};

}
