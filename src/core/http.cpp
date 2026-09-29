#include "http.hpp"

#include <curl/curl.h>

#include <memory>
#include <mutex>

namespace ryu {

namespace {

constexpr size_t probeBytes = 16;

void ensureGlobalInit() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct Sink {
    std::string* body;
    size_t limit;
};

size_t appendBody(char* data, size_t size, size_t count, void* userData) {
    auto* sink = static_cast<Sink*>(userData);
    sink->body->append(data, size * count);
    if (sink->limit && sink->body->size() >= sink->limit) {
        return 0;
    }
    return size * count;
}

struct EasyDeleter {
    void operator()(CURL* handle) const { curl_easy_cleanup(handle); }
};

struct ListDeleter {
    void operator()(curl_slist* list) const { curl_slist_free_all(list); }
};

}

CurlHttpClient::CurlHttpClient(std::string userAgent, std::chrono::seconds timeout)
    : userAgent_(std::move(userAgent)), timeout_(timeout) {
    ensureGlobalInit();
}

HttpResponse CurlHttpClient::get(const std::string& url, const Headers& headers) {
    return perform(Mode::Get, url, nullptr, headers);
}

HttpResponse CurlHttpClient::post(const std::string& url, const std::string& body, const Headers& headers) {
    return perform(Mode::Post, url, &body, headers);
}

HttpResponse CurlHttpClient::probe(const std::string& url, const Headers& headers) {
    return perform(Mode::Probe, url, nullptr, headers);
}

HttpResponse CurlHttpClient::perform(Mode mode, const std::string& url, const std::string* body,
                                     const Headers& headers) {
    std::unique_ptr<CURL, EasyDeleter> handle(curl_easy_init());
    if (!handle) {
        throw HttpError("Could not create an HTTP handle");
    }

    std::unique_ptr<curl_slist, ListDeleter> headerList;
    for (const auto& [name, value] : headers) {
        const auto line = name + ": " + value;
        headerList.reset(curl_slist_append(headerList.release(), line.c_str()));
    }

    HttpResponse response;
    Sink sink{&response.body, mode == Mode::Probe ? probeBytes : 0};
    CURL* easy = handle.get();
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_USERAGENT, userAgent_.c_str());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headerList.get());
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, mode == Mode::Probe ? 0L : 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, static_cast<long>(timeout_.count()));
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &sink);
    if (mode == Mode::Post) {
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body->c_str());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
    }

    const CURLcode code = curl_easy_perform(easy);
    const bool stoppedEarly = code == CURLE_WRITE_ERROR && sink.limit && response.body.size() >= sink.limit;
    if (code != CURLE_OK && !stoppedEarly) {
        throw HttpError("Request to " + url + " failed: " + curl_easy_strerror(code));
    }

    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response.status);
    char* effective = nullptr;
    curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective);
    response.effectiveUrl = effective ? effective : url;
    char* location = nullptr;
    curl_easy_getinfo(easy, CURLINFO_REDIRECT_URL, &location);
    response.location = location ? location : "";
    return response;
}

}
