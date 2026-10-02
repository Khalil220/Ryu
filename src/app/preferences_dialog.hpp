#pragma once

#include "settings.hpp"

#include <wx/dialog.h>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxStaticText;
class wxTextCtrl;

namespace ryu {

class MalSession;

class PreferencesDialog : public wxDialog {
public:
    PreferencesDialog(wxWindow* parent, const Settings& settings, MalSession& mal);

    const Settings& settings() const { return settings_; }

private:
    void showProvider(int index);
    bool storeBaseUrl();
    void onOk(wxCommandEvent& event);
    void showMalAccount();
    void logInToMal();

    Settings settings_;
    MalSession& mal_;
    wxButton* malButton_ = nullptr;
    wxStaticText* malStatus_ = nullptr;
    int shownProvider_ = 0;
    wxChoice* providerChoice_ = nullptr;
    wxTextCtrl* baseUrl_ = nullptr;
    wxChoice* audioChoice_ = nullptr;
    wxChoice* themeChoice_ = nullptr;
    wxCheckBox* fallbackCheck_ = nullptr;
    wxCheckBox* updatesCheck_ = nullptr;
};

}
