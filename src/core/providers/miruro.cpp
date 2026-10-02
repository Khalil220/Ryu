#include "miruro.hpp"

#include "../encoding.hpp"
#include "../hls.hpp"
#include "../language.hpp"

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>

namespace ryu {

namespace {

using nlohmann::json;

constexpr std::string_view responseKey = "miruro/catalog";
constexpr std::array<std::string_view, 4> preferredProviders{"icarus", "aniwaves", "anikoto", "kickassanime"};

std::string stringField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int intField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : 0;
}

std::string gunzip(std::string_view data) {
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        throw ProviderError("Could not start decompressing Miruro's response");
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    stream.avail_in = static_cast<uInt>(data.size());
    std::string output;
    std::array<char, 65536> buffer;
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer.data());
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        output.append(buffer.data(), buffer.size() - stream.avail_out);
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END) {
        throw ProviderError("Miruro sent a response Ryu could not decompress");
    }
    return output;
}

std::string tidy(std::string text) {
    for (const std::string_view quote : {"‘", "’"}) {
        for (auto at = text.find(quote); at != std::string::npos; at = text.find(quote, at + 1)) {
            text.replace(at, quote.size(), "'");
        }
    }
    return text;
}

int malIdOf(const json& item) {
    const auto ids = item.find("external_ids");
    if (ids == item.end() || !ids->is_object()) {
        return 0;
    }
    const auto mal = ids->find("mal");
    if (mal == ids->end() || !mal->is_array() || mal->size() != 1 || !mal->front().is_string()) {
        return 0;
    }
    const auto text = mal->front().get<std::string>();
    int id = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
    return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() ? id : 0;
}

std::string formatOf(std::string_view format) {
    if (format == "TV_SHORT") {
        return "TV";
    }
    return std::string(format);
}

std::string numberText(const json& value) {
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number()) {
        const auto number = value.get<double>();
        return number == std::floor(number) ? std::to_string(static_cast<long long>(number)) : value.dump();
    }
    return value.is_string() ? value.get<std::string>() : std::string();
}

std::string subtitleLabel(std::string label, const std::string& language) {
    for (const std::string_view extension : {".vtt", ".srt", ".ass"}) {
        if (label.ends_with(extension)) {
            label.erase(label.size() - extension.size());
        }
    }
    const bool shortCode = label.size() <= 3 && std::ranges::all_of(label, [](unsigned char c) { return std::isupper(c); });
    if (label.empty() || shortCode) {
        if (auto name = languageName(language); !name.empty()) {
            return name;
        }
    }
    return label;
}

size_t rankOf(std::string_view provider) {
    const auto it = std::ranges::find(preferredProviders, provider);
    return static_cast<size_t>(std::distance(preferredProviders.begin(), it));
}

}

std::string decodeMiruroResponse(std::string_view body) {
    const auto start = body.find_first_not_of(" \t\r\n");
    if (start != std::string_view::npos && (body[start] == '{' || body[start] == '[')) {
        return std::string(body);
    }
    std::string decoded(body);
    for (size_t i = 0; i < decoded.size(); ++i) {
        decoded[i] = static_cast<char>(decoded[i] ^ responseKey[i % responseKey.size()]);
    }
    return gunzip(decoded);
}

MiruroProvider::MiruroProvider(HttpClient& http, std::string baseUrl) : http_(http), baseUrl_(std::move(baseUrl)) {
    while (!baseUrl_.empty() && baseUrl_.back() == '/') {
        baseUrl_.pop_back();
    }
}

std::vector<Show> MiruroProvider::search(std::string_view query) {
    const auto parsed = json::parse(fetch("/anime?q=" + urlEncode(query) + "&limit=15&sort=-popularity"), nullptr, false);
    std::vector<Show> shows;
    const auto results = parsed.is_object() ? parsed.find("data") : parsed.end();
    if (results == parsed.end() || !results->is_array()) {
        return shows;
    }
    for (const auto& item : *results) {
        Show show;
        show.id = stringField(item, "id");
        if (show.id.empty()) {
            continue;
        }
        if (const auto titles = item.find("title"); titles != item.end() && titles->is_object()) {
            const auto english = tidy(stringField(*titles, "english"));
            const auto romaji = tidy(stringField(*titles, "romaji"));
            show.title = english.empty() ? romaji : english;
            show.altTitle = romaji == show.title ? std::string() : romaji;
        }
        show.format = formatOf(stringField(item, "format"));
        show.year = intField(item, "season_year");
        if (const auto counts = item.find("episode_counts"); counts != item.end() && counts->is_object()) {
            show.subEpisodes = intField(*counts, "sub");
            show.dubEpisodes = intField(*counts, "dub");
        }
        show.posterUrl = stringField(item, "cover_url");
        show.synopsis = stringField(item, "description");
        show.malId = malIdOf(item);
        shows.push_back(std::move(show));
    }
    return shows;
}

std::vector<Episode> MiruroProvider::episodes(const Show& show) {
    std::vector<Episode> result;
    for (const std::string_view kind : {"regular", "film"}) {
        const auto parsed = json::parse(fetch("/anime/" + show.id + "/episodes?kind=" + std::string(kind) + "&limit=10000"),
                                        nullptr, false);
        const auto list = parsed.is_object() ? parsed.find("data") : parsed.end();
        if (list == parsed.end() || !list->is_array()) {
            continue;
        }
        for (const auto& item : *list) {
            const auto number = item.find("episode_number");
            if (number == item.end()) {
                continue;
            }
            Episode episode;
            episode.number = numberText(*number);
            episode.id = show.id + "|" + episode.number;
            episode.title = tidy(stringField(item, "title"));
            result.push_back(std::move(episode));
        }
        if (!result.empty()) {
            break;
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

std::vector<Stream> MiruroProvider::streams(std::string_view episodeId, Audio audio) {
    const auto separator = episodeId.find('|');
    if (separator == std::string_view::npos) {
        throw ProviderError("Not a Miruro episode: " + std::string(episodeId));
    }
    const auto parsed = json::parse(fetch("/anime/" + std::string(episodeId.substr(0, separator)) + "/episodes/" +
                                          std::string(episodeId.substr(separator + 1)) + "/play"),
                                    nullptr, false);
    const auto tracks = parsed.is_object() ? parsed.find("tracks") : parsed.end();
    if (tracks == parsed.end() || !tracks->is_array()) {
        throw ProviderError("Miruro sent no tracks for this episode");
    }
    const std::string wanted = audio == Audio::Dub ? "dub" : "ssub";
    std::vector<std::pair<size_t, Stream>> found;
    for (const auto& track : *tracks) {
        if (stringField(track, "track") != wanted || !track.contains("providers")) {
            continue;
        }
        for (const auto& provider : track["providers"]) {
            const auto name = stringField(provider, "provider");
            const auto rank = rankOf(name);
            if (rank == preferredProviders.size() || !provider.contains("servers")) {
                continue;
            }
            std::vector<Subtitle> subtitles;
            if (const auto list = provider.find("subtitles"); list != provider.end() && list->is_array()) {
                for (const auto& entry : *list) {
                    const auto language = stringField(entry, "language");
                    Subtitle subtitle{stringField(entry, "file"), language, subtitleLabel(stringField(entry, "label"), language),
                                      false};
                    if (!subtitle.url.empty()) {
                        subtitles.push_back(std::move(subtitle));
                    }
                }
            }
            if (audio == Audio::Sub) {
                if (const auto english = std::ranges::find_if(
                        subtitles, [](const Subtitle& s) { return s.language == "en" || s.language == "eng"; });
                    english != subtitles.end()) {
                    english->isDefault = true;
                }
            }
            for (const auto& server : provider["servers"]) {
                const auto list = server.find("streams");
                if (list == server.end() || !list->is_array()) {
                    continue;
                }
                const auto hls = std::ranges::find_if(*list, [](const json& s) { return stringField(s, "format") == "hls"; });
                if (hls == list->end() || stringField(*hls, "url").empty()) {
                    continue;
                }
                Stream stream;
                stream.url = stringField(*hls, "url");
                stream.server = name + " " + stringField(server, "server");
                stream.audio = audio;
                if (const auto headers = server.find("headers"); headers != server.end() && headers->is_object()) {
                    for (const auto& [header, value] : headers->items()) {
                        if (value.is_string()) {
                            stream.headers.emplace_back(header, value.get<std::string>());
                        }
                    }
                }
                stream.disguisedSegments = true;
                stream.audioLanguage = audio == Audio::Dub ? "eng,en,English" : "jpn,ja,Japanese";
                stream.subtitles = subtitles;
                if (name == "kickassanime") {
                    stream.subtitleNoise = {"kaa.lt"};
                }
                if (name == "anikoto") {
                    stream.alternateHosts.push_back(hostOf(stream.url));
                    for (const auto& subtitle : subtitles) {
                        const auto host = hostOf(subtitle.url);
                        if (!host.empty() && std::ranges::find(stream.alternateHosts, host) == stream.alternateHosts.end()) {
                            stream.alternateHosts.push_back(host);
                        }
                    }
                }
                found.emplace_back(rank, std::move(stream));
            }
        }
    }
    std::ranges::stable_sort(found, {}, &std::pair<size_t, Stream>::first);
    std::vector<Stream> result;
    for (auto& [rank, stream] : found) {
        result.push_back(std::move(stream));
    }
    return result;
}

std::string MiruroProvider::fetch(const std::string& path) {
    const auto url = baseUrl_ + "/api/v1" + path;
    const auto response = http_.get(url, {{"Referer", baseUrl_ + "/"}, {"Accept", "application/json"}});
    if (response.body.find("<title>Just a moment") != std::string::npos) {
        throw ProviderError("Miruro is showing a Cloudflare challenge");
    }
    if (response.status < 200 || response.status >= 300) {
        throw ProviderError("Miruro returned HTTP " + std::to_string(response.status) + " for " + url);
    }
    return decodeMiruroResponse(response.body);
}

}
