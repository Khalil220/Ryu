#pragma once

#include <string>

class wxString;
struct PrismBackend;
struct PrismContext;

namespace ryu {

class Speech {
public:
    Speech();
    ~Speech();
    Speech(const Speech&) = delete;
    Speech& operator=(const Speech&) = delete;

    void say(const wxString& text, bool interrupt);
    std::string backendName() const;

private:
    bool selectBackend();
    void releaseBackend();

    PrismContext* context_ = nullptr;
    PrismBackend* backend_ = nullptr;
    std::string logPath_;
};

void setSpeech(Speech* speech);
void announce(const wxString& text, bool interrupt = true);

}
