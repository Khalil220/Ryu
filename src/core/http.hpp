#pragma once

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ryu {

using Headers = std::vector<std::pair<std::string, std::string>>;

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string effectiveUrl;
};

class HttpError : public std::runtime_error {
public:
    explicit HttpError(const std::string& message) : std::runtime_error(message) {}
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(const std::string& url, const Headers& headers = {}) = 0;
};

class CurlHttpClient : public HttpClient {
public:
    static constexpr const char* defaultUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

    explicit CurlHttpClient(std::string userAgent = defaultUserAgent,
                            std::chrono::seconds timeout = std::chrono::seconds(15));

    HttpResponse get(const std::string& url, const Headers& headers = {}) override;

private:
    std::string userAgent_;
    std::chrono::seconds timeout_;
};

}
