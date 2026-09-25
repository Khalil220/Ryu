#pragma once

#include "http.hpp"

#include <fstream>
#include <map>
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
        std::string url;
        Headers headers;
    };

    void serve(const std::string& url, std::string body, long status = 200) {
        routes_[url] = HttpResponse{status, std::move(body), url};
    }

    HttpResponse get(const std::string& url, const Headers& headers) override {
        requests.push_back({url, headers});
        const auto it = routes_.find(url);
        if (it == routes_.end()) {
            throw HttpError("No fake route for " + url);
        }
        return it->second;
    }

    std::string header(size_t requestIndex, const std::string& name) const {
        for (const auto& [key, value] : requests.at(requestIndex).headers) {
            if (key == name) {
                return value;
            }
        }
        return {};
    }

    std::vector<Request> requests;

private:
    std::map<std::string, HttpResponse> routes_;
};

}
