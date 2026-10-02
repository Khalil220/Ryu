#include "http.hpp"

#include "log.hpp"

#include <curl/curl.h>

#include <chrono>
#include <format>
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

int reportProgress(void* userData, curl_off_t total, curl_off_t done, curl_off_t, curl_off_t) {
    const auto& progress = *static_cast<const Progress*>(userData);
    return progress(static_cast<std::uint64_t>(done), static_cast<std::uint64_t>(total)) ? 0 : 1;
}

struct EasyDeleter {
    void operator()(CURL* handle) const { curl_easy_cleanup(handle); }
};

struct ListDeleter {
    void operator()(curl_slist* list) const { curl_slist_free_all(list); }
};

}

HttpResponse HttpClient::download(const std::string& url, const Progress& progress) {
    auto response = get(url);
    if (progress && !progress(response.body.size(), response.body.size())) {
        throw HttpCancelled();
    }
    return response;
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

HttpResponse CurlHttpClient::send(const std::string& method, const std::string& url, const std::string& body,
                                  const Headers& headers) {
    return perform(Mode::Post, url, &body, headers, nullptr, method.c_str());
}

HttpResponse CurlHttpClient::download(const std::string& url, const Progress& progress) {
    return perform(Mode::Get, url, nullptr, {}, &progress);
}

HttpResponse CurlHttpClient::perform(Mode mode, const std::string& url, const std::string* body,
                                     const Headers& headers, const Progress* progress, const char* method) {
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
    if (progress && *progress) {
        curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, reportProgress);
        curl_easy_setopt(easy, CURLOPT_XFERINFODATA, progress);
    }
    if (mode == Mode::Post) {
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, body->c_str());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
    }
    if (method) {
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method);
    }

    const char* verb = method ? method : mode == Mode::Post ? "POST" : mode == Mode::Probe ? "PROBE" : "GET";
    const auto started = std::chrono::steady_clock::now();
    const CURLcode code = curl_easy_perform(easy);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    const bool stoppedEarly = code == CURLE_WRITE_ERROR && sink.limit && response.body.size() >= sink.limit;
    if (code == CURLE_ABORTED_BY_CALLBACK) {
        logLine(std::format("{} {} was cancelled after {} ms", verb, url, elapsed));
        throw HttpCancelled();
    }
    if (code != CURLE_OK && !stoppedEarly) {
        logLine(std::format("{} {} failed after {} ms: {}", verb, url, elapsed, curl_easy_strerror(code)));
        throw HttpError("Request to " + url + " failed: " + curl_easy_strerror(code));
    }

    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response.status);
    logLine(std::format("{} {} returned {} in {} ms", verb, url, response.status, elapsed));
    return response;
}

}
