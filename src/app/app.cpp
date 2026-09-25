#include "main_frame.hpp"

#include <wx/app.h>
#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

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
        (new ryu::MainFrame())->Show();
        return true;
    }
};

}

wxIMPLEMENT_APP(RyuApp);
