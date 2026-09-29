#include "main_frame.hpp"
#include "speech.hpp"

#include <wx/app.h>
#include <wx/fileconf.h>
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
        wxConfigBase::Set(new wxFileConfig("Ryu", wxEmptyString, path, wxEmptyString, wxCONFIG_USE_LOCAL_FILE));
        speech_ = std::make_unique<ryu::Speech>();
        ryu::setSpeech(speech_.get());
        (new ryu::MainFrame())->Show();
        return true;
    }

    int OnExit() override {
        ryu::setSpeech(nullptr);
        speech_.reset();
        return wxApp::OnExit();
    }

private:
    std::unique_ptr<ryu::Speech> speech_;
};

}

wxIMPLEMENT_APP(RyuApp);
