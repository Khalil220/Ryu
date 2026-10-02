#include "settings.hpp"

#include "secret.hpp"

#include <algorithm>

#include <wx/fileconf.h>
#include <wx/sstream.h>
#include <wx/wfstream.h>

#include <chrono>
#include <fstream>

namespace ryu {

namespace {

SubtitlePreference readSubtitlePreference(wxConfigBase& config, const wxString& audio) {
    SubtitlePreference preference;
    const auto kind = config.Read("/Subtitles/" + audio, "default");
    if (kind == "off") {
        preference.kind = SubtitlePreference::Kind::Off;
    } else if (kind == "track") {
        preference.kind = SubtitlePreference::Kind::Track;
        preference.label = config.Read("/Subtitles/" + audio + "Label", wxString()).utf8_string();
        preference.language = config.Read("/Subtitles/" + audio + "Language", wxString()).utf8_string();
    }
    return preference;
}

void writeSubtitlePreference(wxConfigBase& config, const wxString& audio, const SubtitlePreference& preference) {
    const auto kind = preference.kind == SubtitlePreference::Kind::Off     ? "off"
                      : preference.kind == SubtitlePreference::Kind::Track ? "track"
                                                                            : "default";
    config.Write("/Subtitles/" + audio, kind);
    config.Write("/Subtitles/" + audio + "Label", wxString::FromUTF8(preference.label));
    config.Write("/Subtitles/" + audio + "Language", wxString::FromUTF8(preference.language));
}

wxString baseUrlKey(const std::string& providerId) {
    return "/BaseUrls/" + wxString::FromUTF8(providerId);
}

void writeAtomically(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    auto temporary = path;
    temporary += L".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            return;
        }
    }
    for (int attempt = 0; attempt < 20; ++attempt) {
        std::filesystem::rename(temporary, path, error);
        if (!error) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    std::filesystem::remove(temporary, error);
}

}

const ProviderInfo& Settings::provider() const {
    const auto* info = findProvider(providerId);
    return info ? *info : availableProviders().front();
}

std::string Settings::baseUrl() const {
    return baseUrlFor(provider());
}

std::string Settings::baseUrlFor(const ProviderInfo& info) const {
    const auto it = baseUrlOverrides.find(info.id);
    return it != baseUrlOverrides.end() && !it->second.empty() ? it->second : info.defaultBaseUrl;
}

WindowPlacement fitToArea(const WindowPlacement& saved, const ScreenArea& area, int minWidth, int minHeight) {
    WindowPlacement fitted = saved;
    fitted.width = std::clamp(saved.width, std::min(minWidth, area.width), area.width);
    fitted.height = std::clamp(saved.height, std::min(minHeight, area.height), area.height);
    fitted.x = std::clamp(saved.x, area.x, area.x + area.width - fitted.width);
    fitted.y = std::clamp(saved.y, area.y, area.y + area.height - fitted.height);
    return fitted;
}

Settings loadSettings(wxConfigBase& config) {
    Settings settings;
    settings.providerId = config.Read("/Provider", wxString::FromUTF8(settings.providerId)).utf8_string();
    settings.providerId = settings.provider().id;
    settings.audio = config.Read("/Audio", "sub") == "dub" ? Audio::Dub : Audio::Sub;
    settings.useFallback = config.ReadBool("/Fallback", true);
    settings.checkForUpdates = config.ReadBool("/Updates/CheckAtStartup", true);
    const auto theme = config.Read("/Theme", "dark");
    settings.theme = theme == "light" ? Theme::Light : theme == "system" ? Theme::System : Theme::Dark;
    settings.readSubtitlesSubbed = config.ReadBool("/ReadSubtitles/Subbed", true);
    settings.readSubtitlesDubbed = config.ReadBool("/ReadSubtitles/Dubbed", false);
    settings.subtitlesSubbed = readSubtitlePreference(config, "Subbed");
    settings.subtitlesDubbed = readSubtitlePreference(config, "Dubbed");
    settings.audioLanguageSubbed = config.Read("/AudioLanguage/Subbed", wxString()).utf8_string();
    settings.audioLanguageDubbed = config.Read("/AudioLanguage/Dubbed", wxString()).utf8_string();
    settings.window.x = config.ReadLong("/Window/X", 0);
    settings.window.y = config.ReadLong("/Window/Y", 0);
    settings.window.width = config.ReadLong("/Window/Width", 0);
    settings.window.height = config.ReadLong("/Window/Height", 0);
    settings.window.maximized = config.ReadBool("/Window/Maximized", false);
    for (const auto& info : availableProviders()) {
        const auto value = config.Read(baseUrlKey(info.id), wxString()).utf8_string();
        if (!value.empty()) {
            settings.baseUrlOverrides[info.id] = value;
        }
    }
    settings.mal.userName = config.Read("/MyAnimeList/User", wxString()).utf8_string();
    settings.mal.tokens.access = revealSecret(config.Read("/MyAnimeList/AccessToken", wxString()).utf8_string());
    settings.mal.tokens.refresh = revealSecret(config.Read("/MyAnimeList/RefreshToken", wxString()).utf8_string());
    long long expiresAt = 0;
    config.Read("/MyAnimeList/ExpiresAt", wxString()).ToLongLong(&expiresAt);
    settings.mal.tokens.expiresAt = expiresAt;
    if (!settings.mal.loggedIn()) {
        settings.mal = {};
    }
    return settings;
}

void saveSettings(wxConfigBase& config, const Settings& settings) {
    config.Write("/Provider", wxString::FromUTF8(settings.providerId));
    config.Write("/Audio", settings.audio == Audio::Dub ? "dub" : "sub");
    config.Write("/Fallback", settings.useFallback);
    config.Write("/Updates/CheckAtStartup", settings.checkForUpdates);
    config.Write("/Theme", settings.theme == Theme::Light    ? "light"
                           : settings.theme == Theme::System ? "system"
                                                             : "dark");
    config.Write("/ReadSubtitles/Subbed", settings.readSubtitlesSubbed);
    config.Write("/ReadSubtitles/Dubbed", settings.readSubtitlesDubbed);
    writeSubtitlePreference(config, "Subbed", settings.subtitlesSubbed);
    writeSubtitlePreference(config, "Dubbed", settings.subtitlesDubbed);
    config.Write("/AudioLanguage/Subbed", wxString::FromUTF8(settings.audioLanguageSubbed));
    config.Write("/AudioLanguage/Dubbed", wxString::FromUTF8(settings.audioLanguageDubbed));
    if (settings.window.width > 0 && settings.window.height > 0) {
        config.Write("/Window/X", settings.window.x);
        config.Write("/Window/Y", settings.window.y);
        config.Write("/Window/Width", settings.window.width);
        config.Write("/Window/Height", settings.window.height);
        config.Write("/Window/Maximized", settings.window.maximized);
    }
    for (const auto& info : availableProviders()) {
        const auto it = settings.baseUrlOverrides.find(info.id);
        if (it != settings.baseUrlOverrides.end() && !it->second.empty() && it->second != info.defaultBaseUrl) {
            config.Write(baseUrlKey(info.id), wxString::FromUTF8(it->second));
        } else {
            config.DeleteEntry(baseUrlKey(info.id), false);
        }
    }
    if (settings.mal.loggedIn()) {
        config.Write("/MyAnimeList/User", wxString::FromUTF8(settings.mal.userName));
        config.Write("/MyAnimeList/AccessToken", wxString::FromUTF8(protectSecret(settings.mal.tokens.access)));
        config.Write("/MyAnimeList/RefreshToken", wxString::FromUTF8(protectSecret(settings.mal.tokens.refresh)));
        config.Write("/MyAnimeList/ExpiresAt", wxString::FromUTF8(std::to_string(settings.mal.tokens.expiresAt)));
    } else {
        config.DeleteGroup("/MyAnimeList");
    }
}

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {
    std::error_code error;
    if (std::filesystem::exists(path_, error)) {
        wxFileInputStream in(path_.wstring());
        if (in.IsOk()) {
            config_ = std::make_unique<wxFileConfig>(in);
        }
    }
    if (!config_) {
        wxStringInputStream empty(wxEmptyString);
        config_ = std::make_unique<wxFileConfig>(empty);
    }
    writer_ = std::thread([this] { writeLoop(); });
}

SettingsStore::~SettingsStore() {
    {
        std::scoped_lock lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_one();
    writer_.join();
}

Settings SettingsStore::load() const {
    return loadSettings(*config_);
}

void SettingsStore::save(const Settings& settings) {
    saveSettings(*config_, settings);
    wxStringOutputStream out;
    config_->Save(out, wxConvUTF8);
    {
        std::scoped_lock lock(mutex_);
        pending_ = out.GetString().utf8_string();
    }
    wake_.notify_one();
}

void SettingsStore::flush() {
    std::unique_lock lock(mutex_);
    idle_.wait(lock, [this] { return !pending_ && !writing_; });
}

void SettingsStore::writeLoop() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return pending_ || stopping_; });
        if (!pending_) {
            return;
        }
        const auto text = std::move(*pending_);
        pending_.reset();
        writing_ = true;
        lock.unlock();
        writeAtomically(path_, text);
        lock.lock();
        writing_ = false;
        idle_.notify_all();
    }
}

}
