#include "anizone.hpp"

#include "../encoding.hpp"
#include "../html.hpp"
#include "../language.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>

namespace ryu {

namespace {

using nlohmann::json;

constexpr std::string_view itemsMarker = "items: JSON.parse('";
constexpr std::string_view playerMarker = "vidstackPlayer(JSON.parse('";
constexpr std::string_view titlesMarker = "epsTitles: JSON.parse('";

std::string stringField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int intField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : 0;
}

bool flagField(const json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

void appendUtf8(std::string& output, uint32_t code) {
    if (code < 0x80) {
        output.push_back(static_cast<char>(code));
    } else if (code < 0x800) {
        output.push_back(static_cast<char>(0xC0 | (code >> 6)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        output.push_back(static_cast<char>(0xE0 | (code >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (code >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code & 0x3F)));
    }
}

std::optional<uint32_t> hexQuad(std::string_view text, size_t at) {
    if (at + 4 > text.size()) {
        return std::nullopt;
    }
    uint32_t value = 0;
    const auto result = std::from_chars(text.data() + at, text.data() + at + 4, value, 16);
    if (result.ec != std::errc() || result.ptr != text.data() + at + 4) {
        return std::nullopt;
    }
    return value;
}

std::string decodeScriptString(std::string_view literal) {
    std::string output;
    output.reserve(literal.size());
    for (size_t i = 0; i < literal.size(); ++i) {
        if (literal[i] != '\\' || i + 1 == literal.size()) {
            output.push_back(literal[i]);
            continue;
        }
        const char escaped = literal[++i];
        if (escaped != 'u') {
            output.push_back(escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped == 'r' ? '\r' : escaped);
            continue;
        }
        auto code = hexQuad(literal, i + 1);
        if (!code) {
            output.push_back(escaped);
            continue;
        }
        i += 4;
        if (*code >= 0xD800 && *code < 0xDC00 && i + 2 < literal.size() && literal[i + 1] == '\\' &&
            literal[i + 2] == 'u') {
            if (const auto low = hexQuad(literal, i + 3); low && *low >= 0xDC00 && *low < 0xE000) {
                code = 0x10000 + ((*code - 0xD800) << 10) + (*low - 0xDC00);
                i += 6;
            }
        }
        appendUtf8(output, *code);
    }
    return output;
}

std::optional<json> embeddedJson(std::string_view text, std::string_view marker) {
    const auto start = text.find(marker);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    const auto from = start + marker.size();
    const auto end = text.find("')", from);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    auto parsed = json::parse(decodeScriptString(text.substr(from, end - from)), nullptr, false);
    if (parsed.is_discarded()) {
        return std::nullopt;
    }
    return parsed;
}

std::string tidy(std::string text) {
    std::ranges::replace(text, '`', '\'');
    return text;
}

std::string englishTitle(const json& item, const char* key, std::string_view fallback) {
    if (const auto titles = item.find(key); titles != item.end() && titles->is_object()) {
        if (auto english = stringField(*titles, "1"); !english.empty()) {
            return tidy(std::move(english));
        }
    }
    return tidy(std::string(fallback));
}

std::string formatOf(std::string_view type) {
    if (type == "TV Series") {
        return "TV";
    }
    if (type == "Web") {
        return "ONA";
    }
    if (type == "TV Special") {
        return "SPECIAL";
    }
    if (type == "Music Video") {
        return "MUSIC";
    }
    if (type == "Unknown") {
        return {};
    }
    std::string upper(type);
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper;
}

std::string subtitleLabel(const std::string& title, const std::string& language) {
    if (!title.empty() && isLanguageName(std::string_view(title).substr(0, title.find_first_of(" (-")))) {
        return title;
    }
    const auto name = languageName(language);
    if (title.empty()) {
        return name.empty() ? language : name;
    }
    return name.empty() ? title : name + " (" + title + ")";
}

std::vector<std::string> audioLanguages(const HtmlDocument& page) {
    std::vector<std::string> languages;
    for (const auto& row : page.select("div.flex.gap-1")) {
        const auto label = row.first("div");
        if (!label || label->text() != "Audio:") {
            continue;
        }
        languages.clear();
        for (const auto& item : row.select("span.rounded-md")) {
            if (auto name = item.text(); !name.empty()) {
                languages.push_back(std::move(name));
            }
        }
    }
    return languages;
}

bool hasEnglish(const std::vector<std::string>& languages) {
    return std::ranges::any_of(languages, [](const std::string& name) { return name.starts_with("English"); });
}

bool containsWord(std::string_view text, std::string_view word) {
    return text.find(word) != std::string_view::npos;
}

void requireSuccess(const HttpResponse& response, const std::string& url) {
    if (response.status < 200 || response.status >= 300) {
        throw ProviderError("AniZone returned HTTP " + std::to_string(response.status) + " for " + url);
    }
}

}

AniZoneProvider::AniZoneProvider(HttpClient& http, std::string baseUrl) : http_(http), baseUrl_(std::move(baseUrl)) {
    while (!baseUrl_.empty() && baseUrl_.back() == '/') {
        baseUrl_.pop_back();
    }
}

std::vector<Show> AniZoneProvider::search(std::string_view query) {
    const auto url = baseUrl_ + "/anime?search=" + urlEncode(query);
    const auto response = fetch(url);
    requireSuccess(response, url);
    std::vector<Show> shows;
    const auto items = embeddedJson(response.body, itemsMarker);
    if (!items || !items->is_array()) {
        return shows;
    }
    for (const auto& item : *items) {
        Show show;
        show.id = stringField(item, "slug");
        if (show.id.empty()) {
            continue;
        }
        const auto main = tidy(stringField(item, "main_title"));
        show.title = englishTitle(item, "title_list", main);
        show.altTitle = show.title == main ? std::string() : main;
        show.format = formatOf(stringField(item, "type"));
        show.year = intField(item, "start_year");
        show.episodeCount = intField(item, "episode_count");
        show.posterUrl = stringField(item, "cover");
        if (const auto tags = item.find("tags"); tags != item.end() && tags->is_array()) {
            for (const auto& tag : *tags) {
                if (auto name = stringField(tag, "name"); !name.empty()) {
                    show.genres.push_back(std::move(name));
                }
            }
        }
        show.pageUrl = baseUrl_ + "/anime/" + show.id;
        shows.push_back(std::move(show));
    }
    return shows;
}

Show AniZoneProvider::describe(const Show& show) {
    if (show.pageUrl.empty()) {
        return show;
    }
    const auto response = fetch(show.pageUrl);
    requireSuccess(response, show.pageUrl);
    Show described = show;
    const auto& body = response.body;
    const auto heading = body.find(">Synopsis</h3>");
    const auto start = heading == std::string::npos ? heading : body.find("<div", heading);
    const auto end = start == std::string::npos ? start : body.find("</div>", start);
    if (end != std::string::npos) {
        const HtmlDocument fragment(std::string_view(body).substr(start, end + 6 - start));
        if (const auto text = fragment.first("div")) {
            auto synopsis = tidy(text->text());
            if (synopsis.starts_with("* ")) {
                synopsis.erase(0, 2);
            }
            if (!synopsis.empty()) {
                described.synopsis = std::move(synopsis);
            }
        }
    }
    return described;
}

std::vector<Episode> AniZoneProvider::episodes(const Show& show) {
    auto url = baseUrl_ + "/anime/" + show.id + "/1";
    auto response = fetch(url);
    if (response.status == 404) {
        const auto showUrl = baseUrl_ + "/anime/" + show.id;
        const auto showPage = fetch(showUrl);
        requireSuccess(showPage, showUrl);
        const auto items = embeddedJson(showPage.body, itemsMarker);
        if (!items || !items->is_array() || items->empty()) {
            return {};
        }
        url = baseUrl_ + "/anime/" + show.id + "/" + stringField(items->front(), "slug");
        response = fetch(url);
    }
    requireSuccess(response, url);

    const HtmlDocument page(response.body);
    std::vector<Episode> result;
    for (const auto& link : page.select("a")) {
        if (!link.attr("wire:key").starts_with("e-")) {
            continue;
        }
        const auto href = link.attr("href");
        const auto slug = href.substr(href.find_last_of('/') + 1);
        if (slug.empty()) {
            continue;
        }
        Episode episode;
        episode.id = show.id + "/" + slug;
        if (const auto order = link.first("div.min-w-10")) {
            episode.number = order->text();
        }
        if (episode.number.empty()) {
            episode.number = slug;
        }
        if (const auto titles = embeddedJson(link.attr("x-data"), titlesMarker); titles && titles->is_object()) {
            episode.title = tidy(stringField(*titles, "1"));
        }
        result.push_back(std::move(episode));
    }
    const auto languages = audioLanguages(page);
    const bool dubbed = languages.empty() || hasEnglish(languages);
    for (auto& episode : result) {
        episode.dubbed = dubbed;
    }
    return result;
}

std::vector<Stream> AniZoneProvider::streams(std::string_view episodeId, Audio audio) {
    const auto url = baseUrl_ + "/anime/" + std::string(episodeId);
    const auto response = fetch(url);
    requireSuccess(response, url);
    const auto player = embeddedJson(response.body, playerMarker);
    if (!player || !player->is_object() || stringField(*player, "src").empty()) {
        throw ProviderError("The AniZone episode page had no video");
    }
    const auto languages = audioLanguages(HtmlDocument(response.body));
    if (audio == Audio::Dub && !languages.empty() && !hasEnglish(languages)) {
        return {};
    }

    Stream stream;
    stream.url = stringField(*player, "src");
    stream.server = "AniZone";
    stream.audio = audio;
    stream.audioLanguage = audio == Audio::Dub ? "eng,en" : "jpn,ja";
    if (const auto tracks = player->find("subtitles"); tracks != player->end() && tracks->is_array()) {
        for (const auto& track : *tracks) {
            const auto language = stringField(track, "language");
            const auto title = stringField(track, "title");
            Subtitle subtitle{stringField(track, "file"), language, subtitleLabel(title, language), false};
            if (!subtitle.url.empty()) {
                subtitle.isDefault = audio == Audio::Sub && flagField(track, "default");
                stream.subtitles.push_back(std::move(subtitle));
            }
        }
    }
    const auto english = [](const Subtitle& subtitle) { return subtitle.language == "en"; };
    if (std::ranges::none_of(stream.subtitles, &Subtitle::isDefault)) {
        const auto chosen =
            audio == Audio::Sub
                ? std::ranges::find_if(stream.subtitles, english)
                : std::ranges::find_if(stream.subtitles, [&](const Subtitle& subtitle) {
                      return english(subtitle) &&
                             (containsWord(subtitle.label, "Forced") || containsWord(subtitle.label, "Signs"));
                  });
        if (chosen != stream.subtitles.end()) {
            chosen->isDefault = true;
        }
    }
    return {stream};
}

HttpResponse AniZoneProvider::fetch(const std::string& url) {
    auto response = http_.get(url);
    if (response.body.find("<title>Just a moment") != std::string::npos) {
        throw ProviderError("AniZone is showing a Cloudflare challenge");
    }
    return response;
}

}
