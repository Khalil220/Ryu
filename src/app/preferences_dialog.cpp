#include "preferences_dialog.hpp"

#include "accessibility.hpp"
#include "mal_session.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
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
    const int gap = FromDIP(8);
    const int margin = FromDIP(12);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    const auto group = [&](const wxString& title) {
        auto* box = new wxStaticBoxSizer(wxVERTICAL, this, title);
        sizer->Add(box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
        return box;
    };

    auto* sources = group("Sources");
    auto* sourcesBox = sources->GetStaticBox();
    auto* grid = new wxFlexGridSizer(2, gap, gap);
    grid->AddGrowableCol(1);
    grid->Add(new wxStaticText(sourcesBox, wxID_ANY, "&Provider:"), 0, wxALIGN_CENTER_VERTICAL);
    providerChoice_ = new wxChoice(sourcesBox, wxID_ANY);
    for (const auto& info : providers) {
        providerChoice_->Append(wxString::FromUTF8(info.name));
        if (info.id == settings_.provider().id) {
            shownProvider_ = static_cast<int>(providerChoice_->GetCount()) - 1;
        }
    }
    grid->Add(providerChoice_, 1, wxEXPAND);
    grid->Add(new wxStaticText(sourcesBox, wxID_ANY, "&Base URL:"), 0, wxALIGN_CENTER_VERTICAL);
    auto* urlRow = new wxBoxSizer(wxHORIZONTAL);
    baseUrl_ = new wxTextCtrl(sourcesBox, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(320, -1)));
    urlRow->Add(baseUrl_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    auto* reset = new wxButton(sourcesBox, wxID_ANY, "&Reset to default");
    urlRow->Add(reset, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(urlRow, 1, wxEXPAND);
    sources->Add(grid, 0, wxEXPAND | wxALL, gap);
    fallbackCheck_ = new wxCheckBox(sourcesBox, wxID_ANY, "&Try other providers when an episode won't play");
    fallbackCheck_->SetValue(settings_.useFallback);
    sources->Add(fallbackCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, gap);

    auto* playback = group("Playback");
    auto* audioRow = new wxBoxSizer(wxHORIZONTAL);
    audioRow->Add(new wxStaticText(playback->GetStaticBox(), wxID_ANY, "Preferred &audio:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    audioChoice_ = new wxChoice(playback->GetStaticBox(), wxID_ANY);
    audioChoice_->Append("Subbed");
    audioChoice_->Append("Dubbed");
    audioChoice_->SetSelection(settings_.audio == Audio::Dub ? 1 : 0);
    audioRow->Add(audioChoice_, 0, wxALIGN_CENTER_VERTICAL);
    playback->Add(audioRow, 0, wxALL, gap);

    auto* appearance = group("Appearance");
    auto* themeRow = new wxBoxSizer(wxHORIZONTAL);
    themeRow->Add(new wxStaticText(appearance->GetStaticBox(), wxID_ANY, "Th&eme:"), 0,
                  wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    themeChoice_ = new wxChoice(appearance->GetStaticBox(), wxID_ANY);
    themeChoice_->Append("Dark");
    themeChoice_->Append("Light");
    themeChoice_->Append("Match Windows");
    themeChoice_->SetSelection(static_cast<int>(settings_.theme));
    themeRow->Add(themeChoice_, 0, wxALIGN_CENTER_VERTICAL);
    appearance->Add(themeRow, 0, wxLEFT | wxRIGHT | wxTOP, gap);
    appearance->Add(new wxStaticText(appearance->GetStaticBox(), wxID_ANY,
                                     "A new theme applies the next time Ryu starts."),
                    0, wxALL, gap);

    auto* updates = group("Updates");
    updatesCheck_ = new wxCheckBox(updates->GetStaticBox(), wxID_ANY, "Check for &updates when Ryu starts");
    updatesCheck_->SetValue(settings_.checkForUpdates);
    updates->Add(updatesCheck_, 0, wxALL, gap);

    auto* account = group("MyAnimeList");
    auto* malRow = new wxBoxSizer(wxHORIZONTAL);
    malStatus_ = new wxStaticText(account->GetStaticBox(), wxID_ANY, wxEmptyString);
    malRow->Add(malStatus_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    malButton_ = new wxButton(account->GetStaticBox(), wxID_ANY, "&Log in");
    malRow->Add(malButton_, 0, wxALIGN_CENTER_VERTICAL);
    account->Add(malRow, 0, wxEXPAND | wxALL, gap);

    sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, margin);
    SetSizer(sizer);
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
    const wxString status =
        mal_.loggedIn() ? "Logged in as " + wxString::FromUTF8(mal_.account().userName) : wxString("Not logged in");
    malStatus_->SetLabel(status);
    setAccessibleDescription(malButton_, status);
    malButton_->SetLabel(mal_.loggedIn() ? "&Log out" : "&Log in");
    GetSizer()->SetSizeHints(this);
}

void PreferencesDialog::logInToMal() {
    wxDialog waiting(this, wxID_ANY, "MyAnimeList login");
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(new wxStaticText(&waiting, wxID_ANY,
                                "Approve Ryu in your browser, then come back here.\n"
                                "Ryu is waiting for MyAnimeList."),
               0, wxALL, FromDIP(16));
    auto* cancel = new wxButton(&waiting, wxID_CANCEL, "Cancel");
    sizer->Add(cancel, 0, wxALIGN_RIGHT | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(16));
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
