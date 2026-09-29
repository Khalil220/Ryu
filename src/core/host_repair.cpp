#include "host_repair.hpp"

#include "hls.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace ryu {

namespace {

bool alive(const HttpResponse& response) {
    return response.status >= 200 && response.status < 300 && !response.body.empty() &&
           response.body.front() != '<';
}

bool probeAlive(HttpClient& http, const std::string& url, const Headers& headers) {
    try {
        return alive(http.probe(url, headers));
    } catch (const HttpError&) {
        return false;
    }
}

std::string fetchPlaylist(HttpClient& http, const std::string& url, const Headers& headers) {
    auto response = http.get(url, headers);
    if (response.status < 200 || response.status >= 300 || !response.body.starts_with("#EXTM3U")) {
        return {};
    }
    return std::move(response.body);
}

}

RepairReport repairStreamHosts(HttpClient& http, PlaylistServer& server, const Stream& stream) {
    RepairReport report{stream, {}, {}};
    const auto master = fetchPlaylist(http, stream.url, stream.headers);
    if (master.empty()) {
        return report;
    }

    const bool hasMaster = isMasterPlaylist(master);
    const auto mediaUrls = hasMaster ? mediaPlaylistUris(master, stream.url) : std::vector<std::string>{stream.url};
    if (mediaUrls.empty()) {
        return report;
    }
    std::map<std::string, std::string> mediaBodies;
    mediaBodies[mediaUrls.front()] = hasMaster ? fetchPlaylist(http, mediaUrls.front(), stream.headers) : master;
    const auto& firstMedia = mediaBodies[mediaUrls.front()];
    if (firstMedia.empty()) {
        return report;
    }

    std::vector<std::string> hosts;
    std::map<std::string, std::string> sampleFor;
    for (const auto& segment : segmentUris(firstMedia, mediaUrls.front())) {
        const auto host = hostOf(segment);
        if (!host.empty() && sampleFor.emplace(host, segment).second) {
            hosts.push_back(host);
        }
    }

    std::vector<std::string> living;
    for (const auto& host : hosts) {
        if (probeAlive(http, sampleFor[host], stream.headers)) {
            living.push_back(host);
        } else {
            report.deadHosts.push_back(host);
        }
    }
    if (report.deadHosts.empty()) {
        return report;
    }

    if (living.empty()) {
        const auto& sample = sampleFor[report.deadHosts.front()];
        for (const auto& candidate : stream.alternateHosts) {
            if (std::ranges::find(report.deadHosts, candidate) != report.deadHosts.end()) {
                continue;
            }
            if (probeAlive(http, withHost(sample, candidate), stream.headers)) {
                living.push_back(candidate);
                break;
            }
        }
    }
    if (living.empty()) {
        throw ProviderError("The video for this episode is not reachable on any of its hosts");
    }
    report.replacementHost = living.front();

    const std::set<std::string> dead(report.deadHosts.begin(), report.deadHosts.end());
    const auto swap = [&](const std::string& url) {
        return dead.contains(hostOf(url)) ? withHost(url, report.replacementHost) : url;
    };

    std::map<std::string, std::string> localNames;
    std::map<std::string, std::string> files;
    for (size_t i = 0; i < mediaUrls.size(); ++i) {
        const auto& url = mediaUrls[i];
        if (!mediaBodies.contains(url)) {
            mediaBodies[url] = fetchPlaylist(http, url, stream.headers);
        }
        if (mediaBodies[url].empty()) {
            continue;
        }
        const auto name = "media-" + std::to_string(i) + ".m3u8";
        localNames[url] = name;
        files[name] = rewritePlaylist(mediaBodies[url], url, swap);
    }

    std::string entry;
    if (hasMaster) {
        files["master.m3u8"] = rewritePlaylist(master, stream.url, [&](const std::string& url) {
            const auto it = localNames.find(url);
            return it != localNames.end() ? it->second : swap(url);
        });
        entry = "master.m3u8";
    } else {
        entry = localNames.begin()->second;
    }

    report.stream.url = server.publish(files) + entry;
    for (auto& subtitle : report.stream.subtitles) {
        subtitle.url = swap(subtitle.url);
    }
    return report;
}

}
