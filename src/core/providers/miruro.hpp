#pragma once

#include "../provider.hpp"

namespace ryu {

std::string decodeMiruroResponse(std::string_view body);

class MiruroProvider : public Provider {
public:
    static constexpr const char* defaultBaseUrl = "https://www.miruro.tv";

    MiruroProvider(HttpClient& http, std::string baseUrl = defaultBaseUrl);

    std::vector<Show> search(std::string_view query) override;
    std::vector<Episode> episodes(const Show& show) override;
    std::vector<Stream> streams(std::string_view episodeId, Audio audio) override;

private:
    std::string fetch(const std::string& path);

    HttpClient& http_;
    std::string baseUrl_;
};

}
