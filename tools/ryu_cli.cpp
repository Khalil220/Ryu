#include "host_repair.hpp"
#include "http.hpp"
#include "log.hpp"
#include "playlist_server.hpp"
#include "registry.hpp"

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

int usage() {
    std::cerr << "Usage:\n"
                 "  ryu-cli providers\n"
                 "  ryu-cli search <query> [--provider id] [--base-url url]\n"
                 "  ryu-cli episodes <show id> [--provider id] [--base-url url]\n"
                 "  ryu-cli streams <episode id> [sub|dub] [--provider id] [--base-url url]\n"
                 "  ryu-cli start <episode id> [sub|dub] [--wait] [--provider id] [--base-url url]\n"
                 "--wait keeps the stream's local playlists served until Enter is pressed.\n"
                 "Set RYU_LOG to a file to log every request.\n";
    return 2;
}

void printShows(const std::vector<ryu::Show>& shows) {
    if (shows.empty()) {
        std::cout << "No results.\n";
    }
    for (const auto& show : shows) {
        std::cout << show.id << ": " << show.title;
        if (!show.format.empty()) {
            std::cout << ", " << show.format;
        }
        if (show.year > 0) {
            std::cout << ", " << show.year;
        }
        if (show.subEpisodes > 0 || show.dubEpisodes > 0) {
            std::cout << ", " << show.subEpisodes << " sub, " << show.dubEpisodes << " dub";
        }
        std::cout << "\n";
    }
}

void printEpisodes(const std::vector<ryu::Episode>& episodes) {
    if (episodes.empty()) {
        std::cout << "No episodes.\n";
    }
    for (const auto& episode : episodes) {
        std::cout << "Episode " << episode.number;
        if (!episode.title.empty()) {
            std::cout << ", " << episode.title;
        }
        std::cout << (episode.subbed ? ", sub" : "") << (episode.dubbed ? ", dub" : "") << ", id " << episode.id
                  << "\n";
    }
}

long long millisecondsSince(std::chrono::steady_clock::time_point started) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
}

void timeStart(ryu::HttpClient& http, ryu::Provider& provider, const std::string& episodeId, ryu::Audio audio,
               bool wait) {
    const auto started = std::chrono::steady_clock::now();
    const auto streams = provider.streams(episodeId, audio);
    std::cout << "Streams resolved after " << millisecondsSince(started) << " ms\n";
    if (streams.empty()) {
        std::cout << "No playable streams.\n";
        return;
    }
    ryu::PlaylistServer server;
    const auto report = ryu::repairStreamHosts(http, server, streams.front());
    std::cout << "Hosts checked after " << millisecondsSince(started) << " ms\n";
    for (const auto& host : report.deadHosts) {
        std::cout << "Dead host " << host << ", replaced by " << report.replacementHost << "\n";
    }
    std::cout << streams.front().server << ": " << report.stream.url << std::endl;
    if (wait) {
        std::cin.get();
    }
}

void printStreams(const std::vector<ryu::Stream>& streams) {
    if (streams.empty()) {
        std::cout << "No playable streams.\n";
    }
    for (const auto& stream : streams) {
        std::cout << stream.server << " " << (stream.audio == ryu::Audio::Dub ? "dub" : "sub") << ": " << stream.url
                  << "\n";
        if (stream.intro) {
            std::cout << "Intro from " << stream.intro->start << " to " << stream.intro->end << " seconds\n";
        }
        for (const auto& [name, value] : stream.headers) {
            std::cout << "Header " << name << ": " << value << "\n";
        }
        for (const auto& subtitle : stream.subtitles) {
            std::cout << "Subtitle " << subtitle.label << " (" << subtitle.language
                      << (subtitle.isDefault ? ", default" : "") << "): " << subtitle.url << "\n";
        }
    }
}

}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    wchar_t* logPath = nullptr;
    size_t length = 0;
    if (_wdupenv_s(&logPath, &length, L"RYU_LOG") == 0 && logPath) {
        ryu::openLog(logPath);
        std::free(logPath);
    }
#endif
    std::vector<std::string> positional;
    std::string providerId = "hianime";
    std::string baseUrl;
    bool wait = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--provider" && i + 1 < argc) {
            providerId = argv[++i];
        } else if (arg == "--base-url" && i + 1 < argc) {
            baseUrl = argv[++i];
        } else if (arg == "--wait") {
            wait = true;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.empty()) {
        return usage();
    }

    const auto& command = positional[0];
    if (command == "providers") {
        for (const auto& info : ryu::availableProviders()) {
            std::cout << info.id << ": " << info.name << ", " << info.defaultBaseUrl << "\n";
        }
        return 0;
    }
    if (positional.size() < 2) {
        return usage();
    }

    const auto* info = ryu::findProvider(providerId);
    if (!info) {
        std::cerr << "Unknown provider " << providerId << "\n";
        return 2;
    }

    try {
        ryu::CurlHttpClient http;
        const auto provider = info->create(http, baseUrl.empty() ? info->defaultBaseUrl : baseUrl);
        if (command == "search") {
            printShows(provider->search(positional[1]));
        } else if (command == "episodes") {
            ryu::Show show;
            show.id = positional[1];
            printEpisodes(provider->episodes(show));
        } else if (command == "streams") {
            const bool dub = positional.size() > 2 && positional[2] == "dub";
            printStreams(provider->streams(positional[1], dub ? ryu::Audio::Dub : ryu::Audio::Sub));
        } else if (command == "start") {
            const bool dub = positional.size() > 2 && positional[2] == "dub";
            timeStart(http, *provider, positional[1], dub ? ryu::Audio::Dub : ryu::Audio::Sub, wait);
        } else {
            return usage();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
    return 0;
}
