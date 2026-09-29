#pragma once

#include "../provider.hpp"

#include <mutex>
#include <string>
#include <string_view>

namespace ryu {

class MegaplayResolver {
public:
    explicit MegaplayResolver(HttpClient& http) : http_(http) {}

    static bool handles(std::string_view embedUrl);
    static std::string languageFromFile(std::string_view url);
    Stream resolve(const std::string& embedUrl, Audio audio, const std::string& referer);

private:
    struct Cipher {
        std::string scriptUrl;
        std::string key;
        std::string iv;
    };

    std::string fetch(const std::string& url, const Headers& headers);
    Cipher cipherFor(const std::string& scriptUrl);

    HttpClient& http_;
    std::mutex mutex_;
    Cipher cipher_;
};

}
