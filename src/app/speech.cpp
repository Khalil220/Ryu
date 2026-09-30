#include "speech.hpp"

#include <wx/string.h>
#include <wx/utils.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <prism.h>

#include <algorithm>
#include <fstream>
#include <utility>
#include <vector>

namespace ryu {

namespace {

Speech* current = nullptr;

bool isScreenReader(PrismBackendId id) {
    switch (id) {
    case PRISM_BACKEND_SAPI:
    case PRISM_BACKEND_ONE_CORE:
    case PRISM_BACKEND_AV_SPEECH:
    case PRISM_BACKEND_SPEECH_DISPATCHER:
    case PRISM_BACKEND_ANDROID_TTS:
    case PRISM_BACKEND_WEB_SPEECH:
    case PRISM_BACKEND_INVALID:
        return false;
    default:
        return true;
    }
}

}

Speech::Speech() {
    wxString path;
    if (wxGetEnv("RYU_SPEECH_LOG", &path)) {
        logPath_ = path.utf8_string();
    }
    PrismConfig config = prism_config_init();
    context_ = prism_init(&config);
}

Speech::~Speech() {
    releaseBackend();
    if (context_) {
        prism_shutdown(context_);
    }
}

bool Speech::selectBackend() {
    if (!context_) {
        return false;
    }
    std::vector<std::pair<int, PrismBackendId>> candidates;
    const size_t count = prism_registry_count(context_);
    for (size_t i = 0; i < count; ++i) {
        const PrismBackendId id = prism_registry_id_at(context_, i);
        if (isScreenReader(id)) {
            candidates.emplace_back(prism_registry_priority(context_, id), id);
        }
    }
    std::ranges::sort(candidates, std::greater<>{});

    for (const auto& [priority, id] : candidates) {
        PrismBackend* backend = prism_registry_acquire(context_, id);
        if (!backend) {
            continue;
        }
        const PrismError result = prism_backend_initialize(backend);
        if (result == PRISM_OK || result == PRISM_ERROR_ALREADY_INITIALIZED) {
            backend_ = backend;
            if (!logPath_.empty()) {
                std::ofstream(logPath_, std::ios::app) << "[backend] " << prism_backend_name(backend_) << "\n";
            }
            return true;
        }
        prism_backend_free(backend);
    }
    return false;
}

void Speech::releaseBackend() {
    if (backend_) {
        prism_backend_free(backend_);
        backend_ = nullptr;
    }
}

void Speech::say(const wxString& text, bool interrupt) {
    const auto utf8 = text.utf8_string();
    if (!logPath_.empty()) {
        std::ofstream(logPath_, std::ios::app) << utf8 << "\n";
    }
    if (!backend_ && !selectBackend()) {
        return;
    }
    if (prism_backend_output(backend_, utf8.c_str(), interrupt) != PRISM_OK) {
        releaseBackend();
        if (selectBackend()) {
            (void)prism_backend_output(backend_, utf8.c_str(), interrupt);
        }
    }
}

void setSpeech(Speech* speech) {
    current = speech;
}

void announce(const wxString& text, bool interrupt) {
    if (current) {
        current->say(text, interrupt);
    }
}

}
