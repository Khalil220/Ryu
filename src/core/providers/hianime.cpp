#include "hianime.hpp"

#include "../encoding.hpp"
#include "../html.hpp"
#include "../log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>

namespace ryu {

namespace {

constexpr std::string_view embedKey = "otaku-embed-v1";
constexpr std::string_view zokoServer = "ZokoAnime";
constexpr std::string_view configMarker = "window.__P=\"";

int parseCount(std::string_view text) {
    const auto begin = text.find_first_of("0123456789");
    if (begin == std::string_view::npos) {
        return 0;
    }
    int value = 0;
    std::from_chars(text.data() + begin, text.data() + text.size(), value);
    return value;
}

std::string trailingId(std::string_view url) {
    const auto dash = url.find_last_of('-');
    if (dash == std::string_view::npos) {
        return {};
    }
    const auto id = url.substr(dash + 1);
    if (id.empty() || id.find_first_not_of("0123456789") != std::string_view::npos) {
        return {};
    }
    return std::string(id);
}

std::string originOf(std::string_view url) {
    const auto scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return std::string(url);
    }
    const auto path = url.find('/', scheme + 3);
    return std::string(url.substr(0, path)) + "/";
}

std::string stringField(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool boolField(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

std::string trimTrailingSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

}

HiAnimeProvider::HiAnimeProvider(HttpClient& http, std::string baseUrl)
    : http_(http), baseUrl_(trimTrailingSlash(std::move(baseUrl))), megaplay_(http) {}

std::vector<Show> HiAnimeProvider::search(std::string_view query) {
    const HtmlDocument page(fetch(baseUrl_ + "/search?keyword=" + urlEncode(query)));
    std::vector<Show> shows;
    for (const auto& item : page.select("#main-content .flw-item")) {
        const auto link = item.first(".film-name a");
        if (!link) {
            continue;
        }
        Show show;
        const auto poster = item.first(".film-poster-ahref");
        show.id = poster ? poster->attr("data-id") : std::string();
        if (show.id.empty()) {
            show.id = trailingId(link->attr("href"));
        }
        if (show.id.empty()) {
            continue;
        }
        show.title = link->attr("title");
        if (show.title.empty()) {
            show.title = link->text();
        }
        show.altTitle = link->attr("data-jname");
        if (const auto format = item.first(".fd-infor .fdi-item")) {
            show.format = format->text();
        }
        if (const auto sub = item.first(".tick-sub")) {
            show.subEpisodes = parseCount(sub->text());
        }
        if (const auto dub = item.first(".tick-dub")) {
            show.dubEpisodes = parseCount(dub->text());
        }
        shows.push_back(std::move(show));
    }
    return shows;
}

std::vector<Episode> HiAnimeProvider::episodes(const Show& show) {
    const HtmlDocument list(fetchHtmlFragment(baseUrl_ + "/api/theme/episode/list/" + urlEncode(show.id)));
    std::vector<Episode> result;
    for (const auto& item : list.select(".ep-item")) {
        Episode episode{item.attr("data-id"), item.attr("data-number"), item.attr("title")};
        if (!episode.id.empty()) {
            result.push_back(std::move(episode));
        }
    }
    if (show.subEpisodes > 0 || show.dubEpisodes > 0) {
        for (size_t i = 0; i < result.size(); ++i) {
            result[i].subbed = i < static_cast<size_t>(show.subEpisodes);
            result[i].dubbed = i < static_cast<size_t>(show.dubEpisodes);
        }
    }
    return result;
}

std::vector<Stream> HiAnimeProvider::streams(std::string_view episodeId, Audio audio) {
    const HtmlDocument servers(
        fetchHtmlFragment(baseUrl_ + "/api/theme/episode/servers?episodeId=" + urlEncode(episodeId)));
    const std::string_view type = audio == Audio::Dub ? "dub" : "sub";
    std::vector<Stream> result;
    std::string lastError;
    auto candidates = servers.select(".server-item");
    std::ranges::stable_partition(
        candidates, [](const HtmlNode& server) { return server.attr("data-server-name") != zokoServer; });
    for (const auto& server : candidates) {
        if (server.attr("data-type") != type) {
            continue;
        }
        const auto name = server.attr("data-server-name");
        std::string embedUrl;
        try {
            embedUrl = base64Decode(server.attr("data-hash"));
        } catch (const std::invalid_argument&) {
            continue;
        }
        try {
            if (name == zokoServer) {
                result.push_back(resolveZoko(embedUrl, audio));
            } else if (MegaplayResolver::handles(embedUrl)) {
                auto stream = megaplay_.resolve(embedUrl, audio, baseUrl_ + "/");
                stream.server = name;
                result.push_back(std::move(stream));
            }
        } catch (const std::exception& error) {
            lastError = error.what();
            logLine("HiAnime server " + name + " failed: " + lastError);
        }
        if (!result.empty()) {
            logLine("HiAnime server " + name + " resolved");
            break;
        }
    }
    if (result.empty() && !lastError.empty()) {
        throw ProviderError(lastError);
    }
    return result;
}

std::string HiAnimeProvider::fetch(const std::string& url, const Headers& headers) {
    auto response = http_.get(url, headers);
    if (response.body.find("<title>Just a moment") != std::string::npos) {
        throw ProviderError("HiAnime is showing a Cloudflare challenge for " + url);
    }
    if (response.status < 200 || response.status >= 300) {
        throw ProviderError("HiAnime returned HTTP " + std::to_string(response.status) + " for " + url);
    }
    return std::move(response.body);
}

std::string HiAnimeProvider::fetchHtmlFragment(const std::string& url) {
    const auto json = nlohmann::json::parse(fetch(url), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        throw ProviderError("HiAnime sent something other than JSON for " + url);
    }
    if (!boolField(json, "status")) {
        throw ProviderError("HiAnime reported a failure for " + url);
    }
    return stringField(json, "html");
}

Stream HiAnimeProvider::resolveZoko(const std::string& embedUrl, Audio audio) {
    const auto page = fetch(embedUrl, {{"Referer", baseUrl_ + "/"}});
    const auto start = page.find(configMarker);
    const auto end = start == std::string::npos ? start : page.find('"', start + configMarker.size());
    if (end == std::string::npos) {
        throw ProviderError("The ZokoAnime player page had no player config");
    }

    const auto config = nlohmann::json::parse(
        xorCipher(base64Decode(std::string_view(page).substr(start + configMarker.size(), end - start - configMarker.size())),
                  embedKey),
        nullptr, false);
    if (config.is_discarded() || !config.is_object()) {
        throw ProviderError("Could not decode the ZokoAnime player config");
    }

    Stream stream;
    stream.url = stringField(config, "src");
    if (stream.url.empty()) {
        throw ProviderError("The ZokoAnime player config had no stream URL");
    }
    stream.server = std::string(zokoServer);
    stream.audio = audio;
    stream.headers = {{"Referer", originOf(embedUrl)}};

    if (const auto tracks = config.find("subtitles"); tracks != config.end() && tracks->is_array()) {
        for (const auto& track : *tracks) {
            Subtitle subtitle{stringField(track, "src"), stringField(track, "lang"), stringField(track, "label"),
                              boolField(track, "default")};
            if (!subtitle.url.empty()) {
                stream.subtitles.push_back(std::move(subtitle));
            }
        }
    }
    return stream;
}

}
