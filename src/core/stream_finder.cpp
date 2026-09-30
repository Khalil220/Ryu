#include "stream_finder.hpp"

#include "host_repair.hpp"
#include "log.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <format>

namespace ryu {

namespace {

std::vector<std::string> titlesOf(const Show& show) {
    std::vector<std::string> titles;
    for (const auto& title : {show.title, show.altTitle}) {
        const auto normalized = normalizeTitle(title);
        if (!normalized.empty()) {
            titles.push_back(normalized);
        }
    }
    return titles;
}

bool sameFormat(const Show& left, const Show& right) {
    return !left.format.empty() && normalizeTitle(left.format) == normalizeTitle(right.format);
}

std::optional<double> parseNumber(std::string_view text) {
    double value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<Show> locate(Provider& provider, const Show& wanted) {
    std::vector<std::string> queries{wanted.title};
    if (!wanted.altTitle.empty() && normalizeTitle(wanted.altTitle) != normalizeTitle(wanted.title)) {
        queries.push_back(wanted.altTitle);
    }
    for (const auto& query : queries) {
        if (auto match = matchShow(wanted, provider.search(query))) {
            return match;
        }
    }
    return std::nullopt;
}

}

std::string normalizeTitle(std::string_view title) {
    std::string output;
    bool pendingSpace = false;
    for (const char ch : title) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80 && !std::isalnum(c)) {
            pendingSpace = !output.empty();
            continue;
        }
        if (pendingSpace) {
            output.push_back(' ');
            pendingSpace = false;
        }
        output.push_back(c < 0x80 ? static_cast<char>(std::tolower(c)) : ch);
    }
    return output;
}

std::optional<Show> matchShow(const Show& wanted, const std::vector<Show>& candidates) {
    const auto wantedTitles = titlesOf(wanted);
    std::optional<Show> fallback;
    for (const auto& candidate : candidates) {
        const auto candidateTitles = titlesOf(candidate);
        const bool matches = std::ranges::any_of(wantedTitles, [&](const std::string& title) {
            return std::ranges::find(candidateTitles, title) != candidateTitles.end();
        });
        if (!matches) {
            continue;
        }
        if (sameFormat(wanted, candidate)) {
            return candidate;
        }
        if (!fallback) {
            fallback = candidate;
        }
    }
    return fallback;
}

bool sameEpisodeNumber(std::string_view left, std::string_view right) {
    const auto a = parseNumber(left);
    const auto b = parseNumber(right);
    return a && b ? *a == *b : left == right;
}

FoundStream findStream(HttpClient& http, PlaylistServer& server, const std::vector<ProviderHandle>& providers,
                       const Show& show, const Episode& episode, Audio audio) {
    std::string lastError;
    bool anyFailure = false;
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    };
    logLine(std::format("Finding a {} stream for {} episode {}", audio == Audio::Dub ? "dubbed" : "subbed", show.title,
                        episode.number));
    for (size_t i = 0; i < providers.size(); ++i) {
        const auto& handle = providers[i];
        try {
            std::vector<Stream> streams;
            if (i == 0) {
                streams = handle.provider->streams(episode.id, audio);
            } else {
                const auto match = locate(*handle.provider, show);
                if (!match) {
                    logLine(handle.name + " has no matching show");
                    continue;
                }
                const auto episodes = handle.provider->episodes(*match);
                const auto same = std::ranges::find_if(
                    episodes, [&](const Episode& candidate) { return sameEpisodeNumber(candidate.number, episode.number); });
                if (same == episodes.end()) {
                    logLine(handle.name + " has no episode " + episode.number);
                    continue;
                }
                streams = handle.provider->streams(same->id, audio);
            }
            if (streams.empty()) {
                logLine(handle.name + " offered no streams");
            }
            for (const auto& stream : streams) {
                try {
                    auto report = repairStreamHosts(http, server, stream);
                    for (const auto& host : report.deadHosts) {
                        logLine("Dead host " + host + ", replaced by " + report.replacementHost);
                    }
                    logLine(std::format("Using {} {} after {} ms: {}", handle.name, stream.server, elapsed(),
                                        report.stream.url));
                    return {std::move(report.stream), handle.name, i > 0, std::move(report.deadHosts)};
                } catch (const std::exception& error) {
                    anyFailure = true;
                    lastError = handle.name + ": " + error.what();
                    logLine("Stream from " + handle.name + " " + stream.server + " failed: " + error.what());
                }
            }
        } catch (const std::exception& error) {
            anyFailure = true;
            lastError = handle.name + ": " + error.what();
            logLine(lastError);
        }
    }
    logLine(std::format("No stream found after {} ms", elapsed()));
    if (!anyFailure) {
        throw ProviderError(audio == Audio::Dub ? "No dubbed stream is available for this episode."
                                                : "No subbed stream is available for this episode.");
    }
    throw ProviderError(lastError);
}

}
