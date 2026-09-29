#include "main_frame.hpp"
#include "settings.hpp"
#include "speech.hpp"

#include <wx/app.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include <memory>

namespace {

class RyuApp : public wxApp {
public:
    bool OnInit() override {
        if (!wxApp::OnInit()) {
            return false;
        }
        SetAppName("Ryu");
        wxString path;
        if (!wxGetEnv("RYU_CONFIG_FILE", &path)) {
            const wxFileName file(wxStandardPaths::Get().GetUserDataDir(), "settings.ini");
            file.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            path = file.GetFullPath();
        }
        settings_ = std::make_unique<ryu::SettingsStore>(path.ToStdWstring());
        speech_ = std::make_unique<ryu::Speech>();
        ryu::setSpeech(speech_.get());
        (new ryu::MainFrame(*settings_))->Show();
        return true;
    }

    int OnExit() override {
        ryu::setSpeech(nullptr);
        speech_.reset();
        settings_.reset();
        return wxApp::OnExit();
    }

private:
    std::unique_ptr<ryu::SettingsStore> settings_;
    std::unique_ptr<ryu::Speech> speech_;
};

}

wxIMPLEMENT_APP(RyuApp);
