#pragma once

#include "settings.hpp"

#include <wx/dialog.h>

class wxCheckBox;
class wxChoice;
class wxTextCtrl;

namespace ryu {

class PreferencesDialog : public wxDialog {
public:
    PreferencesDialog(wxWindow* parent, const Settings& settings);

    const Settings& settings() const { return settings_; }

private:
    void showProvider(int index);
    bool storeBaseUrl();
    void onOk(wxCommandEvent& event);

    Settings settings_;
    int shownProvider_ = 0;
    wxChoice* providerChoice_ = nullptr;
    wxTextCtrl* baseUrl_ = nullptr;
    wxChoice* audioChoice_ = nullptr;
    wxChoice* themeChoice_ = nullptr;
    wxCheckBox* fallbackCheck_ = nullptr;
};

}
