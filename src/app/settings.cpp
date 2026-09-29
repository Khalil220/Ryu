#include "settings.hpp"

#include <wx/confbase.h>

namespace ryu {

namespace {

wxString baseUrlKey(const std::string& providerId) {
    return "/BaseUrls/" + wxString::FromUTF8(providerId);
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

Settings loadSettings(wxConfigBase& config) {
    Settings settings;
    settings.providerId = config.Read("/Provider", wxString::FromUTF8(settings.providerId)).utf8_string();
    settings.providerId = settings.provider().id;
    settings.audio = config.Read("/Audio", "sub") == "dub" ? Audio::Dub : Audio::Sub;
    settings.useFallback = config.ReadBool("/Fallback", true);
    settings.readSubtitlesSubbed = config.ReadBool("/ReadSubtitles/Subbed", true);
    settings.readSubtitlesDubbed = config.ReadBool("/ReadSubtitles/Dubbed", false);
    for (const auto& info : availableProviders()) {
        const auto value = config.Read(baseUrlKey(info.id), wxString()).utf8_string();
        if (!value.empty()) {
            settings.baseUrlOverrides[info.id] = value;
        }
    }
    return settings;
}

void saveSettings(wxConfigBase& config, const Settings& settings) {
    config.Write("/Provider", wxString::FromUTF8(settings.providerId));
    config.Write("/Audio", settings.audio == Audio::Dub ? "dub" : "sub");
    config.Write("/Fallback", settings.useFallback);
    config.Write("/ReadSubtitles/Subbed", settings.readSubtitlesSubbed);
    config.Write("/ReadSubtitles/Dubbed", settings.readSubtitlesDubbed);
    for (const auto& info : availableProviders()) {
        const auto it = settings.baseUrlOverrides.find(info.id);
        if (it != settings.baseUrlOverrides.end() && !it->second.empty() && it->second != info.defaultBaseUrl) {
            config.Write(baseUrlKey(info.id), wxString::FromUTF8(it->second));
        } else {
            config.DeleteEntry(baseUrlKey(info.id), false);
        }
    }
    config.Flush();
}

}
