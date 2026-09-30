#pragma once

#include "http.hpp"

#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ryu::test {

inline std::string readFixture(const std::string& relativePath) {
    std::ifstream in(std::string(RYU_FIXTURE_DIR) + "/" + relativePath, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Missing fixture " + relativePath);
    }
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

class FakeHttpClient : public HttpClient {
public:
    struct Request {
        std::string method;
        std::string url;
        Headers headers;
        std::string body;
    };

    void serve(const std::string& url, std::string body, long status = 200) {
        routes_[url] = HttpResponse{status, std::move(body)};
    }

    void serveRedirect(const std::string& url) {
        routes_[url] = HttpResponse{302, "<html>moved</html>"};
    }

    HttpResponse get(const std::string& url, const Headers& headers) override {
        return respond("GET", url, headers, {});
    }

    HttpResponse post(const std::string& url, const std::string& body, const Headers& headers) override {
        return respond("POST", url, headers, body);
    }

    HttpResponse probe(const std::string& url, const Headers& headers) override {
        auto response = respond("PROBE", url, headers, {});
        response.body = response.body.substr(0, 16);
        return response;
    }

    std::string header(size_t requestIndex, const std::string& name) const {
        for (const auto& [key, value] : requests.at(requestIndex).headers) {
            if (key == name) {
                return value;
            }
        }
        return {};
    }

    size_t count(const std::string& method, const std::string& url) const {
        size_t total = 0;
        for (const auto& request : requests) {
            total += request.method == method && request.url == url ? 1 : 0;
        }
        return total;
    }

    std::vector<Request> requests;

private:
    HttpResponse respond(const std::string& method, const std::string& url, const Headers& headers,
                         const std::string& body) {
        std::scoped_lock lock(mutex_);
        requests.push_back({method, url, headers, body});
        const auto it = routes_.find(url);
        if (it == routes_.end()) {
            throw HttpError("No fake route for " + url);
        }
        return it->second;
    }

    std::map<std::string, HttpResponse> routes_;
    std::mutex mutex_;
};

}
