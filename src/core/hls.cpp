#include "hls.hpp"

#include <sstream>

namespace ryu {

namespace {

constexpr std::string_view uriAttribute = "URI=\"";

std::vector<std::string_view> lines(std::string_view text) {
    std::vector<std::string_view> result;
    size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        auto line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        result.push_back(line);
        start = end + 1;
    }
    return result;
}

bool isUriLine(std::string_view line) {
    return !line.empty() && line.front() != '#';
}

bool startsWithTag(std::string_view line, std::string_view tag) {
    return line.starts_with(tag);
}

std::string_view attributeUri(std::string_view line) {
    const auto start = line.find(uriAttribute);
    if (start == std::string_view::npos) {
        return {};
    }
    const auto begin = start + uriAttribute.size();
    const auto end = line.find('"', begin);
    return end == std::string_view::npos ? std::string_view{} : line.substr(begin, end - begin);
}

std::string_view schemeOf(std::string_view url) {
    const auto colon = url.find("://");
    return colon == std::string_view::npos ? std::string_view{} : url.substr(0, colon);
}

}

std::string resolveUrl(std::string_view base, std::string_view reference) {
    if (reference.find("://") != std::string_view::npos) {
        return std::string(reference);
    }
    const auto scheme = schemeOf(base);
    if (reference.starts_with("//")) {
        return std::string(scheme.empty() ? "https" : scheme) + ":" + std::string(reference);
    }
    const auto authority = base.find("://");
    if (authority == std::string_view::npos) {
        return std::string(reference);
    }
    const auto pathStart = base.find('/', authority + 3);
    const auto origin = base.substr(0, pathStart);
    if (reference.starts_with("/")) {
        return std::string(origin) + std::string(reference);
    }
    auto path = pathStart == std::string_view::npos ? std::string_view("/") : base.substr(pathStart);
    path = path.substr(0, path.find_first_of("?#"));
    const auto directory = path.substr(0, path.find_last_of('/') + 1);
    return std::string(origin) + std::string(directory) + std::string(reference);
}

std::string hostOf(std::string_view url) {
    const auto authority = url.find("://");
    if (authority == std::string_view::npos) {
        return {};
    }
    const auto start = authority + 3;
    const auto end = url.find_first_of("/?#", start);
    return std::string(url.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
}

std::string withHost(std::string_view url, std::string_view host) {
    const auto authority = url.find("://");
    if (authority == std::string_view::npos) {
        return std::string(url);
    }
    const auto start = authority + 3;
    const auto end = url.find_first_of("/?#", start);
    return std::string(url.substr(0, start)) + std::string(host) +
           std::string(end == std::string_view::npos ? std::string_view{} : url.substr(end));
}

bool isMasterPlaylist(std::string_view playlist) {
    return playlist.find("#EXT-X-STREAM-INF") != std::string_view::npos;
}

std::vector<std::string> mediaPlaylistUris(std::string_view master, std::string_view baseUrl) {
    std::vector<std::string> uris;
    bool nextIsVariant = false;
    for (const auto line : lines(master)) {
        if (startsWithTag(line, "#EXT-X-STREAM-INF")) {
            nextIsVariant = true;
        } else if (startsWithTag(line, "#EXT-X-MEDIA:")) {
            if (const auto uri = attributeUri(line); !uri.empty()) {
                uris.push_back(resolveUrl(baseUrl, uri));
            }
        } else if (isUriLine(line) && nextIsVariant) {
            uris.push_back(resolveUrl(baseUrl, line));
            nextIsVariant = false;
        }
    }
    return uris;
}

std::vector<std::string> segmentUris(std::string_view media, std::string_view baseUrl) {
    std::vector<std::string> uris;
    for (const auto line : lines(media)) {
        if (isUriLine(line)) {
            uris.push_back(resolveUrl(baseUrl, line));
        }
    }
    return uris;
}

std::string rewritePlaylist(std::string_view playlist, std::string_view baseUrl,
                            const std::function<std::string(const std::string&)>& map) {
    std::ostringstream output;
    const auto all = lines(playlist);
    for (size_t i = 0; i < all.size(); ++i) {
        const auto line = all[i];
        if (isUriLine(line)) {
            output << map(resolveUrl(baseUrl, line));
        } else if (const auto uri = attributeUri(line); !uri.empty() && line.front() == '#') {
            const auto start = line.find(uriAttribute) + uriAttribute.size();
            output << line.substr(0, start) << map(resolveUrl(baseUrl, uri)) << line.substr(start + uri.size());
        } else {
            output << line;
        }
        if (i + 1 < all.size()) {
            output << '\n';
        }
    }
    return output.str();
}

}
