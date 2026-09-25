#include "http.hpp"

#include <curl/curl.h>

#include <memory>
#include <mutex>

namespace ryu {

namespace {

void ensureGlobalInit() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t appendBody(char* data, size_t size, size_t count, void* userData) {
    static_cast<std::string*>(userData)->append(data, size * count);
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
    CURL* easy = handle.get();
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_USERAGENT, userAgent_.c_str());
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, headerList.get());
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy, CURLOPT_TIMEOUT, static_cast<long>(timeout_.count()));
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &response.body);

    const CURLcode code = curl_easy_perform(easy);
    if (code != CURLE_OK) {
        throw HttpError("Request to " + url + " failed: " + curl_easy_strerror(code));
    }

    curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &response.status);
    char* effective = nullptr;
    curl_easy_getinfo(easy, CURLINFO_EFFECTIVE_URL, &effective);
    response.effectiveUrl = effective ? effective : url;
    return response;
}

}
