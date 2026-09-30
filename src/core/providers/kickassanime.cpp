#include "kickassanime.hpp"

#include "../encoding.hpp"
#include "../hls.hpp"
#include "../html.hpp"
#include "../log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <set>

namespace ryu {

namespace {

using nlohmann::json;

std::string stringField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string episodeString(const json& episode) {
    auto text = stringField(episode, "episode_string");
    if (text.empty()) {
        const auto number = episode.find("episode_number");
        if (number != episode.end() && number->is_number()) {
            text = number->dump();
        }
    }
    return text;
}

bool hasLocale(const json& show, const char* locale) {
    const auto locales = show.find("locales");
    return locales != show.end() && locales->is_array() &&
           std::ranges::any_of(*locales, [&](const json& value) { return value.is_string() && value == locale; });
}

std::string upper(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return text;
}

json unwrap(const json& value) {
    if (value.is_array() && value.size() == 2 && value[0].is_number_integer()) {
        const auto& inner = value[1];
        if (value[0].get<int>() == 1 && inner.is_array()) {
            json list = json::array();
            for (const auto& item : inner) {
                list.push_back(unwrap(item));
            }
            return list;
        }
        return unwrap(inner);
    }
    if (value.is_object()) {
        json object = json::object();
        for (const auto& [key, item] : value.items()) {
            object[key] = unwrap(item);
        }
        return object;
    }
    return value;
}

std::string originOf(std::string_view url) {
    const auto scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return std::string(url);
    }
    return std::string(url.substr(0, url.find('/', scheme + 3)));
}

std::string trimTrailingSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::optional<double> numberOf(std::string_view text) {
    double value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::set<double> listedNumbers(const json& response) {
    std::set<double> numbers;
    if (const auto pages = response.find("pages"); pages != response.end() && pages->is_array()) {
        for (const auto& page : *pages) {
            if (const auto eps = page.find("eps"); eps != page.end() && eps->is_array()) {
                for (const auto& number : *eps) {
                    if (number.is_number()) {
                        numbers.insert(number.get<double>());
                    }
                }
            }
        }
    }
    return numbers;
}

json parseJson(const std::string& body, const std::string& url) {
    auto parsed = json::parse(body, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw ProviderError("KickAssAnime sent something other than JSON for " + url);
    }
    return parsed;
}

}

KickAssAnimeProvider::KickAssAnimeProvider(HttpClient& http, std::string baseUrl)
    : http_(http), baseUrl_(trimTrailingSlash(std::move(baseUrl))) {}

std::vector<Show> KickAssAnimeProvider::search(std::string_view query) {
    const auto url = baseUrl_ + "/api/fsearch";
    auto response = http_.post(url, json{{"query", std::string(query)}}.dump(), {{"Content-Type", "application/json"}});
    if (response.body.find("<title>Just a moment") != std::string::npos) {
        throw ProviderError("KickAssAnime is showing a Cloudflare challenge");
    }
    if (response.status < 200 || response.status >= 300) {
        throw ProviderError("KickAssAnime returned HTTP " + std::to_string(response.status) + " for its search");
    }
    const auto parsed = parseJson(response.body, url);
    const auto results = parsed.find("result");
    std::vector<Show> shows;
    if (results == parsed.end() || !results->is_array()) {
        return shows;
    }
    for (const auto& item : *results) {
        Show show;
        show.id = stringField(item, "slug");
        if (show.id.empty()) {
            continue;
        }
        const auto japanese = stringField(item, "title");
        const auto english = stringField(item, "title_en");
        show.title = english.empty() ? japanese : english;
        show.altTitle = english.empty() || english == japanese ? std::string() : japanese;
        show.format = upper(stringField(item, "type"));
        if (const auto year = item.find("year"); year != item.end() && year->is_number_integer()) {
            show.year = year->get<int>();
        }
        show.offersSub = hasLocale(item, "ja-JP");
        show.offersDub = hasLocale(item, "en-US");
        shows.push_back(std::move(show));
    }
    return shows;
}

std::vector<Episode> KickAssAnimeProvider::episodes(const Show& show) {
    const std::string_view showId = show.id;
    const auto base = baseUrl_ + "/api/show/" + urlEncode(showId) + "/episodes";
    std::string language = "ja-JP";
    auto first = parseJson(fetch(base + "?ep=1&lang=" + language), base);
    if (!first.contains("result") || !first["result"].is_array() || first["result"].empty()) {
        language = "en-US";
        first = parseJson(fetch(base + "?ep=1&lang=" + language), base);
    }

    std::vector<json> pages{first};
    const auto current = first.value("current_page", 1);
    if (const auto list = first.find("pages"); list != first.end() && list->is_array()) {
        for (const auto& page : *list) {
            const auto number = page.value("number", 0);
            if (number > 0 && number != current) {
                pages.push_back(parseJson(fetch(base + "?page=" + std::to_string(number) + "&lang=" + language), base));
            }
        }
    }

    const bool japanese = language == "ja-JP";
    std::optional<std::set<double>> dubbed;
    if (japanese) {
        try {
            dubbed = listedNumbers(parseJson(fetch(base + "?ep=1&lang=en-US"), base));
        } catch (const std::exception&) {
        }
    }

    std::vector<Episode> result;
    for (const auto& page : pages) {
        const auto items = page.find("result");
        if (items == page.end() || !items->is_array()) {
            continue;
        }
        for (const auto& item : *items) {
            const auto number = episodeString(item);
            if (number.empty()) {
                continue;
            }
            Episode episode{std::string(showId) + "|" + number, number, stringField(item, "title")};
            episode.subbed = japanese;
            const auto value = numberOf(number);
            episode.dubbed = !japanese || !dubbed || (value && dubbed->contains(*value));
            result.push_back(std::move(episode));
        }
    }
    return result;
}

std::vector<Stream> KickAssAnimeProvider::streams(std::string_view episodeId, Audio audio) {
    const auto separator = episodeId.rfind('|');
    if (separator == std::string_view::npos) {
        throw ProviderError("Not a KickAssAnime episode id: " + std::string(episodeId));
    }
    const auto show = urlEncode(episodeId.substr(0, separator));
    const auto number = std::string(episodeId.substr(separator + 1));
    const std::string language = audio == Audio::Dub ? "en-US" : "ja-JP";

    const auto listUrl = baseUrl_ + "/api/show/" + show + "/episodes?ep=" + urlEncode(number) + "&lang=" + language;
    const auto list = parseJson(fetch(listUrl), listUrl);
    std::string slug;
    if (const auto items = list.find("result"); items != list.end() && items->is_array()) {
        for (const auto& item : *items) {
            if (episodeString(item) == number) {
                slug = stringField(item, "slug");
                break;
            }
        }
    }
    if (slug.empty()) {
        return {};
    }

    const auto episodeUrl = baseUrl_ + "/api/show/" + show + "/episode/ep-" + urlEncode(number) + "-" + urlEncode(slug);
    const auto episode = parseJson(fetch(episodeUrl), episodeUrl);
    std::vector<std::pair<std::string, std::string>> players;
    if (const auto servers = episode.find("servers"); servers != episode.end() && servers->is_array()) {
        for (const auto& server : *servers) {
            if (auto src = stringField(server, "src"); !src.empty()) {
                players.emplace_back(stringField(server, "name"), std::move(src));
            }
        }
    }
    if (players.empty()) {
        throw ProviderError("KickAssAnime has no servers for this episode");
    }
    std::ranges::stable_partition(players, [](const auto& player) {
        return player.first == "VidStreaming" || player.second.find("source=vidstream") != std::string::npos;
    });
    std::string firstError;
    for (const auto& [name, src] : players) {
        try {
            return {resolvePlayer(src, name, audio)};
        } catch (const std::exception& error) {
            logLine("KickAssAnime server " + name + " failed: " + error.what());
            if (firstError.empty()) {
                firstError = error.what();
            }
        }
    }
    throw ProviderError(firstError);
}

std::string KickAssAnimeProvider::fetch(const std::string& url) {
    auto response = http_.get(url);
    if (response.body.find("<title>Just a moment") != std::string::npos) {
        throw ProviderError("KickAssAnime is showing a Cloudflare challenge for " + url);
    }
    if (response.status < 200 || response.status >= 300) {
        throw ProviderError("KickAssAnime returned HTTP " + std::to_string(response.status) + " for " + url);
    }
    return std::move(response.body);
}

Stream KickAssAnimeProvider::resolvePlayer(const std::string& playerUrl, const std::string& server, Audio audio) {
    const HtmlDocument page(fetch(playerUrl));
    json props;
    for (const auto& island : page.select("astro-island[props]")) {
        auto candidate = json::parse(island.attr("props"), nullptr, false);
        if (!candidate.is_discarded() && candidate.contains("manifest")) {
            props = unwrap(candidate);
            break;
        }
    }
    const auto manifest = props.is_object() ? stringField(props, "manifest") : std::string();
    if (manifest.empty()) {
        throw ProviderError("The KickAssAnime player page had no stream address");
    }

    const auto origin = originOf(playerUrl);
    Stream stream;
    stream.url = resolveUrl(playerUrl, manifest);
    stream.server = server.empty() ? "KickAssAnime" : server;
    stream.audio = audio;
    stream.headers = {{"Origin", origin}, {"Referer", origin + "/"}};
    stream.disguisedSegments = true;
    stream.audioLanguage = audio == Audio::Dub ? "eng,en" : "jpn,ja";
    stream.subtitleNoise = {"kaa.lt"};

    bool haveDefault = false;
    if (const auto tracks = props.find("subtitles"); tracks != props.end() && tracks->is_array()) {
        for (const auto& track : *tracks) {
            Subtitle subtitle{stringField(track, "src"), stringField(track, "language"), stringField(track, "name"),
                              false};
            if (subtitle.url.empty()) {
                continue;
            }
            if (audio == Audio::Sub && !haveDefault && subtitle.language == "en") {
                subtitle.isDefault = true;
                haveDefault = true;
            }
            stream.subtitles.push_back(std::move(subtitle));
        }
    }
    return stream;
}

}
