#pragma once

#include "provider.hpp"
#include "registry.hpp"
#include "subtitle_choice.hpp"

#include <condition_variable>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class wxConfigBase;
class wxFileConfig;

namespace ryu {

enum class Theme { Dark, Light, System };

struct WindowPlacement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;

    bool operator==(const WindowPlacement&) const = default;
};

struct Settings {
    std::string providerId = "hianime";
    Theme theme = Theme::Dark;
    std::map<std::string, std::string> baseUrlOverrides;
    Audio audio = Audio::Sub;
    bool useFallback = true;
    bool checkForUpdates = true;
    bool readSubtitlesSubbed = true;
    bool readSubtitlesDubbed = false;
    SubtitlePreference subtitlesSubbed;
    SubtitlePreference subtitlesDubbed;
    std::string audioLanguageSubbed;
    std::string audioLanguageDubbed;
    WindowPlacement window;

    bool readSubtitlesFor(Audio kind) const { return kind == Audio::Dub ? readSubtitlesDubbed : readSubtitlesSubbed; }
    const SubtitlePreference& subtitlesFor(Audio kind) const {
        return kind == Audio::Dub ? subtitlesDubbed : subtitlesSubbed;
    }
    SubtitlePreference& subtitlesFor(Audio kind) { return kind == Audio::Dub ? subtitlesDubbed : subtitlesSubbed; }
    const std::string& audioLanguageFor(Audio kind) const {
        return kind == Audio::Dub ? audioLanguageDubbed : audioLanguageSubbed;
    }
    std::string& audioLanguageFor(Audio kind) { return kind == Audio::Dub ? audioLanguageDubbed : audioLanguageSubbed; }

    const ProviderInfo& provider() const;
    std::string baseUrl() const;
    std::string baseUrlFor(const ProviderInfo& info) const;
};

Settings loadSettings(wxConfigBase& config);
void saveSettings(wxConfigBase& config, const Settings& settings);

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path path);
    ~SettingsStore();
    SettingsStore(const SettingsStore&) = delete;
    SettingsStore& operator=(const SettingsStore&) = delete;

    Settings load() const;
    void save(const Settings& settings);
    void flush();

private:
    void writeLoop();

    std::filesystem::path path_;
    std::unique_ptr<wxFileConfig> config_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::optional<std::string> pending_;
    bool writing_ = false;
    bool stopping_ = false;
    std::thread writer_;
};

}
