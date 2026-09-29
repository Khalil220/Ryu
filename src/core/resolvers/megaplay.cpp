#include "megaplay.hpp"

#include "../encoding.hpp"
#include "../hls.hpp"
#include "../html.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>

namespace ryu {

namespace {

constexpr std::string_view host = "https://megaplay.buzz/";
constexpr std::string_view literalMarker = ".encode(\"";

std::string originOf(std::string_view url) {
    const auto scheme = url.find("://");
    if (scheme == std::string_view::npos) {
        return std::string(url);
    }
    return std::string(url.substr(0, url.find('/', scheme + 3))) + "/";
}

std::optional<std::string> literalAt(std::string_view script, size_t marker) {
    if (marker == std::string_view::npos) {
        return std::nullopt;
    }
    const auto begin = marker + literalMarker.size();
    const auto end = script.find('"', begin);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(script.substr(begin, end - begin));
}

std::string stringField(const nlohmann::json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}


}

std::string MegaplayResolver::languageFromFile(std::string_view url) {
    const auto slash = url.find_last_of('/');
    auto name = slash == std::string_view::npos ? url : url.substr(slash + 1);
    name = name.substr(0, name.find('.'));
    std::string language;
    size_t start = 0;
    while (start <= name.size()) {
        const auto end = std::min(name.find_first_of("-_", start), name.size());
        const auto token = name.substr(start, end - start);
        if (token.size() == 3 && std::ranges::all_of(token, [](char c) { return c >= 'a' && c <= 'z'; })) {
            language = token;
        }
        start = end + 1;
    }
    return language;
}

bool MegaplayResolver::handles(std::string_view embedUrl) {
    return embedUrl.starts_with(std::string(host) + "stream/");
}

Stream MegaplayResolver::resolve(const std::string& embedUrl, Audio audio, const std::string& referer) {
    const auto page = fetch(embedUrl, {{"Referer", referer}});
    const HtmlDocument document(page);
    const auto player = document.first("#megaplay-player");
    const auto id = player ? player->attr("data-id") : std::string();
    if (id.empty()) {
        throw ProviderError("Megaplay has no stream for this episode");
    }

    std::string scriptUrl;
    for (const auto& script : document.select("script[src]")) {
        const auto src = script.attr("src");
        if (src.find("newclient") != std::string::npos) {
            scriptUrl = src.starts_with("http") ? src : originOf(embedUrl) + (src.starts_with("/") ? src.substr(1) : src);
            break;
        }
    }
    if (scriptUrl.empty()) {
        throw ProviderError("The megaplay player page no longer loads its decryption script");
    }

    const auto sources = nlohmann::json::parse(
        fetch(originOf(embedUrl) + "stream/getSources?id=" + urlEncode(id),
              {{"Referer", embedUrl}, {"X-Requested-With", "XMLHttpRequest"}}),
        nullptr, false);
    if (sources.is_discarded() || !sources.is_object()) {
        throw ProviderError("Megaplay sent something other than JSON for its sources");
    }

    std::string file;
    if (const auto encrypted = stringField(sources, "enc"); !encrypted.empty()) {
        const auto cipher = cipherFor(scriptUrl);
        nlohmann::json payload;
        try {
            payload = nlohmann::json::parse(aes256CbcDecrypt(base64Decode(encrypted), cipher.key, cipher.iv));
        } catch (const std::exception&) {
            throw ProviderError("Could not decrypt the megaplay stream address");
        }
        file = payload.is_object() ? stringField(payload, "file") : std::string();
    } else if (const auto list = sources.find("sources"); list != sources.end() && list->is_array() && !list->empty()) {
        file = stringField(list->front(), "file");
    }
    if (file.empty()) {
        throw ProviderError("Megaplay did not return a stream address");
    }

    Stream stream;
    stream.url = file;
    stream.server = "Megaplay";
    stream.audio = audio;
    stream.headers = {{"Referer", originOf(embedUrl)}};
    stream.disguisedSegments = true;
    if (const auto intro = sources.find("intro"); intro != sources.end() && intro->is_object()) {
        const auto start = intro->value("start", 0.0);
        const auto end = intro->value("end", 0.0);
        if (end > start) {
            stream.intro = TimeRange{start, end};
        }
    }
    if (const auto tracks = sources.find("tracks"); tracks != sources.end() && tracks->is_array()) {
        for (const auto& track : *tracks) {
            const auto kind = stringField(track, "kind");
            if (kind != "captions" && kind != "subtitles") {
                continue;
            }
            Subtitle subtitle;
            subtitle.url = stringField(track, "file");
            subtitle.label = stringField(track, "label");
            subtitle.language = languageFromFile(subtitle.url);
            const auto isDefault = track.find("default");
            subtitle.isDefault = isDefault != track.end() && isDefault->is_boolean() && isDefault->get<bool>();
            if (!subtitle.url.empty()) {
                stream.subtitles.push_back(std::move(subtitle));
            }
        }
    }
    stream.alternateHosts.push_back(hostOf(stream.url));
    for (const auto& subtitle : stream.subtitles) {
        const auto subtitleHost = hostOf(subtitle.url);
        if (!subtitleHost.empty() && std::ranges::find(stream.alternateHosts, subtitleHost) == stream.alternateHosts.end()) {
            stream.alternateHosts.push_back(subtitleHost);
        }
    }
    return stream;
}

std::string MegaplayResolver::fetch(const std::string& url, const Headers& headers) {
    auto response = http_.get(url, headers);
    if (response.body.find("<title>Just a moment") != std::string::npos) {
        throw ProviderError("Megaplay is showing a Cloudflare challenge");
    }
    if (response.status == 404 || response.body.find("<title>Error - MegaPlay") != std::string::npos) {
        throw ProviderError("Megaplay has no stream for this episode");
    }
    if (response.status < 200 || response.status >= 300) {
        throw ProviderError("Megaplay returned HTTP " + std::to_string(response.status) + " for " + url);
    }
    return std::move(response.body);
}

MegaplayResolver::Cipher MegaplayResolver::cipherFor(const std::string& scriptUrl) {
    {
        std::scoped_lock lock(mutex_);
        if (cipher_.scriptUrl == scriptUrl) {
            return cipher_;
        }
    }

    const auto script = fetch(scriptUrl, {{"Referer", std::string(host)}});
    const std::string_view view(script);
    const auto importKey = view.find("importKey(\"raw\"");
    const auto decrypt = view.find("decrypt({name:\"AES-CBC\"");
    const auto key = literalAt(view, importKey == std::string_view::npos ? importKey : view.find(literalMarker, importKey));
    const auto iv = literalAt(view, decrypt == std::string_view::npos ? decrypt : view.rfind(literalMarker, decrypt));
    if (!key || !iv || iv->size() != 16) {
        throw ProviderError("Could not find the megaplay decryption key");
    }

    Cipher cipher{scriptUrl, std::string(32, '\0'), *iv};
    cipher.key.replace(0, std::min<size_t>(32, key->size()), key->substr(0, 32));
    std::scoped_lock lock(mutex_);
    cipher_ = cipher;
    return cipher;
}

}
