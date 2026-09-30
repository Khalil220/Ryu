#pragma once

#include "http.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ryu {

enum class Audio { Sub, Dub };

struct Show {
    std::string id;
    std::string title;
    std::string altTitle;
    std::string format;
    int subEpisodes = 0;
    int dubEpisodes = 0;
    int year = 0;
    bool offersSub = false;
    bool offersDub = false;
    std::string posterUrl;
    std::string synopsis;
    std::vector<std::string> genres;
    std::string pageUrl;
};

struct Episode {
    std::string id;
    std::string number;
    std::string title;
    bool subbed = true;
    bool dubbed = true;

    bool availableIn(Audio audio) const { return audio == Audio::Dub ? dubbed : subbed; }
};

struct Subtitle {
    std::string url;
    std::string language;
    std::string label;
    bool isDefault = false;
};

struct TimeRange {
    double start = 0;
    double end = 0;
};

struct Stream {
    std::string url;
    std::string server;
    Audio audio = Audio::Sub;
    Headers headers;
    std::vector<Subtitle> subtitles;
    bool disguisedSegments = false;
    std::vector<std::string> alternateHosts;
    std::string audioLanguage;
    std::optional<TimeRange> intro;
    std::vector<std::string> subtitleNoise;
};

class ProviderError : public std::runtime_error {
public:
    explicit ProviderError(const std::string& message) : std::runtime_error(message) {}
};

class Provider {
public:
    virtual ~Provider() = default;
    virtual std::vector<Show> search(std::string_view query) = 0;
    virtual std::vector<Episode> episodes(const Show& show) = 0;
    virtual std::vector<Stream> streams(std::string_view episodeId, Audio audio) = 0;
    virtual Show describe(const Show& show) { return show; }
};

}
