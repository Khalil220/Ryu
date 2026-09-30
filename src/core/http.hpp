#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ryu {

using Headers = std::vector<std::pair<std::string, std::string>>;
using Progress = std::function<bool(std::uint64_t done, std::uint64_t total)>;

struct HttpResponse {
    long status = 0;
    std::string body;
    std::string effectiveUrl;
    std::string location;
};

class HttpError : public std::runtime_error {
public:
    explicit HttpError(const std::string& message) : std::runtime_error(message) {}
};

class HttpCancelled : public HttpError {
public:
    HttpCancelled() : HttpError("The download was cancelled") {}
};

class HttpClient {
public:
    virtual ~HttpClient() = default;
    virtual HttpResponse get(const std::string& url, const Headers& headers = {}) = 0;
    virtual HttpResponse post(const std::string& url, const std::string& body, const Headers& headers = {}) = 0;
    virtual HttpResponse probe(const std::string& url, const Headers& headers = {}) = 0;
    virtual HttpResponse download(const std::string& url, const Progress& progress);
};

class CurlHttpClient : public HttpClient {
public:
    static constexpr const char* defaultUserAgent =
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36";

    explicit CurlHttpClient(std::string userAgent = defaultUserAgent,
                            std::chrono::seconds timeout = std::chrono::seconds(15));

    HttpResponse get(const std::string& url, const Headers& headers = {}) override;
    HttpResponse post(const std::string& url, const std::string& body, const Headers& headers = {}) override;
    HttpResponse probe(const std::string& url, const Headers& headers = {}) override;
    HttpResponse download(const std::string& url, const Progress& progress) override;

private:
    enum class Mode { Get, Post, Probe };
    HttpResponse perform(Mode mode, const std::string& url, const std::string* body, const Headers& headers,
                         const Progress* progress = nullptr);

    std::string userAgent_;
    std::chrono::seconds timeout_;
};

}
