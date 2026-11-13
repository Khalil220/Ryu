#include "http.hpp"

#include "log.hpp"

#include <curl/curl.h>

#include <array>
#include <atomic>
#include <chrono>
#include <format>
#include <memory>
#include <mutex>
#include <vector>

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

struct ListDeleter {
    void operator()(curl_slist* list) const { curl_slist_free_all(list); }
};

using ShareLocks = std::array<std::mutex, CURL_LOCK_DATA_LAST>;

void lockShare(CURL*, curl_lock_data data, curl_lock_access, void* locks) {
    (*static_cast<ShareLocks*>(locks))[data].lock();
}

void unlockShare(CURL*, curl_lock_data data, void* locks) {
    (*static_cast<ShareLocks*>(locks))[data].unlock();
}

std::string originOf(const std::string& url) {
    const auto scheme = url.find("://");
    return scheme == std::string::npos ? std::string() : url.substr(0, url.find_first_of("/?#", scheme + 3));
}

}

struct CurlHttpClient::Pool {
    static constexpr size_t maxIdle = 16;

    Pool() {
        ensureGlobalInit();
        share = curl_share_init();
        curl_share_setopt(share, CURLSHOPT_LOCKFUNC, lockShare);
        curl_share_setopt(share, CURLSHOPT_UNLOCKFUNC, unlockShare);
        curl_share_setopt(share, CURLSHOPT_USERDATA, &locks);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
        curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    }

    ~Pool() {
        for (const auto& entry : idle) {
            curl_easy_cleanup(entry.second);
        }
        curl_share_cleanup(share);
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    CURL* take(const std::string& origin) {
        CURL* handle = nullptr;
        {
            std::lock_guard lock(mutex);
            auto chosen = idle.rbegin();
            for (auto it = idle.rbegin(); it != idle.rend(); ++it) {
                if (it->first == origin) {
                    chosen = it;
                    break;
                }
            }
            if (chosen != idle.rend()) {
                handle = chosen->second;
                idle.erase(std::next(chosen).base());
            }
        }
        if (handle) {
            curl_easy_reset(handle);
            return handle;
        }
        handle = curl_easy_init();
        if (handle) {
            curl_easy_setopt(handle, CURLOPT_SHARE, share);
        }
        return handle;
    }

    void give(const std::string& origin, CURL* handle) {
        CURL* surplus = nullptr;
        {
            std::lock_guard lock(mutex);
            idle.emplace_back(origin, handle);
            if (idle.size() > maxIdle) {
                surplus = idle.front().second;
                idle.erase(idle.begin());
            }
        }
        if (surplus) {
            curl_easy_cleanup(surplus);
        }
    }

    CURLSH* share = nullptr;
    ShareLocks locks;
    std::mutex mutex;
    std::vector<std::pair<std::string, CURL*>> idle;
    std::atomic<std::uint64_t> connections{0};
};

HttpResponse HttpClient::download(const std::string& url, const Progress& progress) {
    auto response = get(url);
    if (progress && !progress(response.body.size(), response.body.size())) {
        throw HttpCancelled();
    }
    return response;
}

CurlHttpClient::CurlHttpClient(std::string userAgent, std::chrono::seconds timeout)
    : userAgent_(std::move(userAgent)), timeout_(timeout), pool_(std::make_unique<Pool>()) {}

CurlHttpClient::~CurlHttpClient() = default;

std::uint64_t CurlHttpClient::connectionsOpened() const {
    return pool_->connections;
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
    const auto origin = originOf(url);
    const auto release = [this, &origin](CURL* used) { pool_->give(origin, used); };
    std::unique_ptr<CURL, decltype(release)> handle(pool_->take(origin), release);
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
    long connects = 0;
    curl_easy_getinfo(easy, CURLINFO_NUM_CONNECTS, &connects);
    pool_->connections += static_cast<std::uint64_t>(connects);
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
