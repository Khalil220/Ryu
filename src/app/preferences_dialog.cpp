#include "preferences_dialog.hpp"

#include "accessibility.hpp"
#include "mal_session.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <memory>

namespace ryu {

namespace {

struct LoginWait {
    wxDialog* dialog = nullptr;
    bool finished = false;
    std::string error;
};

}

PreferencesDialog::PreferencesDialog(wxWindow* parent, const Settings& settings, MalSession& mal)
    : wxDialog(parent, wxID_ANY, "Preferences"), settings_(settings), mal_(mal) {
    const auto& providers = availableProviders();

    auto* grid = new wxFlexGridSizer(2, 6, 8);
    grid->AddGrowableCol(1);

    grid->Add(new wxStaticText(this, wxID_ANY, "&Provider:"), 0, wxALIGN_CENTER_VERTICAL);
    providerChoice_ = new wxChoice(this, wxID_ANY);
    for (const auto& info : providers) {
        providerChoice_->Append(wxString::FromUTF8(info.name));
        if (info.id == settings_.provider().id) {
            shownProvider_ = static_cast<int>(providerChoice_->GetCount()) - 1;
        }
    }
    grid->Add(providerChoice_, 1, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, "&Base URL:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* urlRow = new wxBoxSizer(wxHORIZONTAL);
    baseUrl_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(320, -1));
    urlRow->Add(baseUrl_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    auto* reset = new wxButton(this, wxID_ANY, "&Reset to default");
    urlRow->Add(reset, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(urlRow, 1, wxEXPAND);

    grid->Add(new wxStaticText(this, wxID_ANY, "Preferred &audio:"), 0, wxALIGN_CENTER_VERTICAL);
    audioChoice_ = new wxChoice(this, wxID_ANY);
    audioChoice_->Append("Subbed");
    audioChoice_->Append("Dubbed");
    audioChoice_->SetSelection(settings_.audio == Audio::Dub ? 1 : 0);
    grid->Add(audioChoice_, 0);

    grid->Add(new wxStaticText(this, wxID_ANY, "Th&eme:"), 0, wxALIGN_CENTER_VERTICAL);
    themeChoice_ = new wxChoice(this, wxID_ANY);
    themeChoice_->Append("Dark");
    themeChoice_->Append("Light");
    themeChoice_->Append("Match Windows");
    themeChoice_->SetSelection(static_cast<int>(settings_.theme));
    grid->Add(themeChoice_, 0);
    grid->AddSpacer(0);
    grid->Add(new wxStaticText(this, wxID_ANY, "A new theme applies the next time Ryu starts."), 0);

    grid->Add(new wxStaticText(this, wxID_ANY, "MyAnimeList:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* malRow = new wxBoxSizer(wxHORIZONTAL);
    malButton_ = new wxButton(this, wxID_ANY, "Log in to &MyAnimeList");
    malRow->Add(malButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    malStatus_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    malRow->Add(malStatus_, 1, wxALIGN_CENTER_VERTICAL);
    grid->Add(malRow, 1, wxEXPAND);

    fallbackCheck_ = new wxCheckBox(this, wxID_ANY, "&Try other providers when an episode won't play");
    fallbackCheck_->SetValue(settings_.useFallback);
    updatesCheck_ = new wxCheckBox(this, wxID_ANY, "Check for &updates when Ryu starts");
    updatesCheck_->SetValue(settings_.checkForUpdates);

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(grid, 1, wxEXPAND | wxALL, 12);
    sizer->Add(fallbackCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    sizer->Add(updatesCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
    sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
    SetSizerAndFit(sizer);
    setAccessibleName(providerChoice_, "Provider");
    setAccessibleName(baseUrl_, "Base URL");
    setAccessibleName(audioChoice_, "Preferred audio");
    setAccessibleName(themeChoice_, "Theme");

    providerChoice_->SetSelection(shownProvider_);
    showProvider(shownProvider_);

    providerChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!storeBaseUrl()) {
            providerChoice_->SetSelection(shownProvider_);
            return;
        }
        showProvider(providerChoice_->GetSelection());
    });
    reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        baseUrl_->SetValue(wxString::FromUTF8(availableProviders()[shownProvider_].defaultBaseUrl));
        baseUrl_->SetFocus();
    });
    Bind(wxEVT_BUTTON, &PreferencesDialog::onOk, this, wxID_OK);
    malButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        if (mal_.loggedIn()) {
            mal_.logOut();
            showMalAccount();
        } else {
            logInToMal();
        }
    });
    showMalAccount();

    providerChoice_->SetFocus();
}

void PreferencesDialog::showMalAccount() {
    const auto user = wxString::FromUTF8(mal_.account().userName);
    if (mal_.loggedIn()) {
        setAccessibleName(malButton_, "Log out of MyAnimeList, logged in as " + user);
        malButton_->SetLabel("Log out of &MyAnimeList");
        malStatus_->SetLabel("Logged in as " + user);
    } else {
        setAccessibleName(malButton_, "Log in to MyAnimeList");
        malButton_->SetLabel("Log in to &MyAnimeList");
        malStatus_->SetLabel("Not logged in");
    }
    GetSizer()->SetSizeHints(this);
}

void PreferencesDialog::logInToMal() {
    wxDialog waiting(this, wxID_ANY, "MyAnimeList login");
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(&waiting, wxID_ANY,
                                "Approve Ryu in your browser, then come back here.\n"
                                "Ryu is waiting for MyAnimeList."),
               0, wxALL, 16);
    auto* cancel = new wxButton(&waiting, wxID_CANCEL, "Cancel");
    sizer->Add(cancel, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, 16);
    waiting.SetSizerAndFit(sizer);
    waiting.CentreOnParent();
    cancel->SetFocus();

    const auto wait = std::make_shared<LoginWait>();
    wait->dialog = &waiting;
    mal_.logIn([wait](bool, const std::string& error) {
        wait->finished = true;
        wait->error = error;
        if (wait->dialog) {
            wait->dialog->EndModal(wxID_OK);
        }
    });
    waiting.ShowModal();
    wait->dialog = nullptr;
    if (!wait->finished) {
        mal_.cancelLogin();
        return;
    }
    showMalAccount();
    if (!wait->error.empty()) {
        wxMessageBox(wxString::FromUTF8(wait->error), "Could not log in", wxOK | wxICON_ERROR, this);
    }
}

void PreferencesDialog::showProvider(int index) {
    shownProvider_ = index;
    const auto& info = availableProviders()[index];
    const auto it = settings_.baseUrlOverrides.find(info.id);
    const auto& url = it != settings_.baseUrlOverrides.end() && !it->second.empty() ? it->second : info.defaultBaseUrl;
    baseUrl_->ChangeValue(wxString::FromUTF8(url));
}

bool PreferencesDialog::storeBaseUrl() {
    wxString url = baseUrl_->GetValue();
    url.Trim().Trim(false);
    const auto& info = availableProviders()[shownProvider_];
    if (url.empty()) {
        settings_.baseUrlOverrides.erase(info.id);
        return true;
    }
    if (!url.StartsWith("https://") && !url.StartsWith("http://")) {
        wxMessageBox("The base URL has to start with http:// or https://.", "Preferences", wxOK | wxICON_WARNING,
                     this);
        baseUrl_->SetFocus();
        return false;
    }
    const auto value = url.utf8_string();
    if (value == info.defaultBaseUrl) {
        settings_.baseUrlOverrides.erase(info.id);
    } else {
        settings_.baseUrlOverrides[info.id] = value;
    }
    return true;
}

void PreferencesDialog::onOk(wxCommandEvent& event) {
    if (!storeBaseUrl()) {
        return;
    }
    settings_.providerId = availableProviders()[shownProvider_].id;
    settings_.audio = audioChoice_->GetSelection() == 1 ? Audio::Dub : Audio::Sub;
    settings_.useFallback = fallbackCheck_->GetValue();
    settings_.checkForUpdates = updatesCheck_->GetValue();
    settings_.theme = static_cast<Theme>(themeChoice_->GetSelection());
    event.Skip();
}

}
