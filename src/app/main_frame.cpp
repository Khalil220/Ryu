#include "main_frame.hpp"

#include "accessibility.hpp"
#include "background.hpp"
#include "format.hpp"
#include "player_frame.hpp"
#include "preferences_dialog.hpp"
#include "speech.hpp"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/confbase.h>
#include <wx/listbox.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace ryu {

struct ProviderSession {
    std::shared_ptr<HttpClient> http;
    std::unique_ptr<Provider> provider;
};

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Ryu"), alive_(std::make_shared<int>(0)) {
    settings_ = loadSettings(*wxConfigBase::Get());
    createMenu();
    createControls();
    CreateStatusBar();
    SetMinSize(FromDIP(wxSize(480, 420)));
    SetSize(FromDIP(wxSize(800, 640)));
    applySettings();
    setStatus("Ready");
    Bind(wxEVT_CHAR_HOOK, &MainFrame::onCharHook, this);
    searchBox_->SetFocus();
}

void MainFrame::createMenu() {
    auto* file = new wxMenu;
    file->Append(wxID_PREFERENCES, "&Preferences...\tCtrl+,");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");
    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    SetMenuBar(bar);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { showPreferences(); }, wxID_PREFERENCES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
}

void MainFrame::createControls() {
    auto* panel = new wxPanel(this);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* searchRow = new wxBoxSizer(wxHORIZONTAL);
    searchRow->Add(new wxStaticText(panel, wxID_ANY, "&Search:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    searchBox_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    searchRow->Add(searchBox_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    searchButton_ = new wxButton(panel, wxID_ANY, "Search");
    searchRow->Add(searchButton_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(searchRow, 0, wxEXPAND | wxALL, 8);

    sizer->Add(new wxStaticText(panel, wxID_ANY, "&Results:"), 0, wxLEFT | wxRIGHT, 8);
    results_ = new wxListBox(panel, wxID_ANY);
    results_->SetMinSize(FromDIP(wxSize(-1, 120)));
    sizer->Add(results_, 1, wxEXPAND | wxALL, 8);

    sizer->Add(new wxStaticText(panel, wxID_ANY, "&Episodes:"), 0, wxLEFT | wxRIGHT, 8);
    episodeList_ = new wxListBox(panel, wxID_ANY);
    episodeList_->SetMinSize(FromDIP(wxSize(-1, 120)));
    sizer->Add(episodeList_, 1, wxEXPAND | wxALL, 8);

    auto* playRow = new wxBoxSizer(wxHORIZONTAL);
    playRow->Add(new wxStaticText(panel, wxID_ANY, "&Audio:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    audioChoice_ = new wxChoice(panel, wxID_ANY);
    audioChoice_->Append("Subbed");
    audioChoice_->Append("Dubbed");
    playRow->Add(audioChoice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    playButton_ = new wxButton(panel, wxID_ANY, "&Play");
    playRow->Add(playButton_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(playRow, 0, wxALL, 8);

    panel->SetSizer(sizer);

    setAccessibleName(searchBox_, "Search");
    setAccessibleName(results_, "Results");
    setAccessibleName(episodeList_, "Episodes");
    setAccessibleName(audioChoice_, "Audio");

    searchBox_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { startSearch(); });
    searchButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { startSearch(); });
    results_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { loadEpisodes(); });
    episodeList_->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { playSelectedEpisode(); });
    playButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { playSelectedEpisode(); });
    audioChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        settings_.audio = selectedAudio();
        saveSettings(*wxConfigBase::Get(), settings_);
    });
}

void MainFrame::applySettings() {
    auto session = std::make_shared<ProviderSession>();
    session->http = std::make_shared<CurlHttpClient>();
    session->provider = settings_.provider().create(*session->http, settings_.baseUrl());
    session_ = std::move(session);
    audioChoice_->SetSelection(settings_.audio == Audio::Dub ? 1 : 0);
    SetTitle("Ryu - " + wxString::FromUTF8(settings_.provider().name));
}

void MainFrame::onCharHook(wxKeyEvent& event) {
    const int key = event.GetKeyCode();
    if ((key == WXK_RETURN || key == WXK_NUMPAD_ENTER) && !event.HasAnyModifiers()) {
        if (FindFocus() == results_) {
            loadEpisodes();
            return;
        }
        if (FindFocus() == episodeList_) {
            playSelectedEpisode();
            return;
        }
    }
    event.Skip();
}

void MainFrame::startSearch() {
    wxString query = searchBox_->GetValue();
    query.Trim().Trim(false);
    if (query.empty()) {
        setStatus("Type a title to search for");
        wxBell();
        return;
    }

    const unsigned generation = ++searchGeneration_;
    ++episodeGeneration_;
    shows_.clear();
    episodes_.clear();
    results_->Clear();
    episodeList_->Clear();
    setStatus("Searching for " + query + "...");
    announce("Searching for " + query);

    auto session = session_;
    runInBackground<std::vector<ryu::Show>>(
        alive_, [session, text = query.utf8_string()] { return session->provider->search(text); },
        [this, generation, query](std::vector<ryu::Show> shows) {
            if (generation != searchGeneration_) {
                return;
            }
            shows_ = std::move(shows);
            if (shows_.empty()) {
                results_->Append("No results for " + query);
                setStatus("No results for " + query);
            } else {
                for (const auto& show : shows_) {
                    results_->Append(wxString::FromUTF8(showLabel(show)));
                }
                setStatus(wxString::Format("%zu results for %s", shows_.size(), query));
            }
            results_->SetSelection(0);
            results_->SetFocus();
            if (!shows_.empty()) {
                announce(shows_.size() == 1 ? wxString("1 result") : wxString::Format("%zu results", shows_.size()),
                         false);
            }
        },
        [this, generation](const std::string& message) {
            if (generation == searchGeneration_) {
                setStatus("Search failed");
                showError("Search failed", message);
            }
        });
}

void MainFrame::loadEpisodes() {
    const int index = results_->GetSelection();
    if (index == wxNOT_FOUND || static_cast<size_t>(index) >= shows_.size()) {
        return;
    }
    currentShow_ = shows_[index];
    const unsigned generation = ++episodeGeneration_;
    episodes_.clear();
    episodeList_->Clear();
    const auto title = wxString::FromUTF8(currentShow_.title);
    setStatus("Loading episodes of " + title + "...");
    announce("Loading episodes");

    auto session = session_;
    runInBackground<std::vector<Episode>>(
        alive_, [session, id = currentShow_.id] { return session->provider->episodes(id); },
        [this, generation, title](std::vector<Episode> episodes) {
            if (generation != episodeGeneration_) {
                return;
            }
            episodes_ = std::move(episodes);
            if (episodes_.empty()) {
                episodeList_->Append("No episodes available");
                setStatus("No episodes available for " + title);
            } else {
                for (const auto& episode : episodes_) {
                    episodeList_->Append(wxString::FromUTF8(episodeLabel(episode)));
                }
                setStatus(wxString::Format("%zu episodes of %s", episodes_.size(), title));
            }
            episodeList_->SetSelection(0);
            episodeList_->SetFocus();
            if (!episodes_.empty()) {
                announce(episodes_.size() == 1 ? wxString("1 episode")
                                               : wxString::Format("%zu episodes", episodes_.size()),
                         false);
            }
        },
        [this, generation](const std::string& message) {
            if (generation == episodeGeneration_) {
                setStatus("Could not load episodes");
                showError("Could not load episodes", message);
            }
        });
}

void MainFrame::playSelectedEpisode() {
    const int index = episodeList_->GetSelection();
    if (index == wxNOT_FOUND || static_cast<size_t>(index) >= episodes_.size()) {
        return;
    }
    const auto episode = episodes_[index];
    const auto audio = selectedAudio();
    const auto title = wxString::FromUTF8(episodeLabel(episode) + " - " + currentShow_.title);
    const unsigned generation = ++streamGeneration_;
    setStatus("Loading " + title + "...");
    announce("Loading episode");

    auto session = session_;
    runInBackground<std::vector<Stream>>(
        alive_, [session, id = episode.id, audio] { return session->provider->streams(id, audio); },
        [this, generation, title, audio](std::vector<Stream> streams) {
            if (generation != streamGeneration_) {
                return;
            }
            if (streams.empty()) {
                const wxString message = audio == Audio::Dub ? "No dubbed stream is available for this episode."
                                                             : "No subbed stream is available for this episode.";
                setStatus(message);
                wxMessageBox(message, "Ryu", wxOK | wxICON_INFORMATION, this);
                return;
            }
            try {
                if (!player_) {
                    player_ = new PlayerFrame(this);
                }
                player_->play(streams.front(), title);
                player_->Show();
                player_->Raise();
                setStatus("Playing " + title);
            } catch (const std::exception& error) {
                setStatus("Playback failed");
                showError("Playback failed", error.what());
            }
        },
        [this, generation](const std::string& message) {
            if (generation == streamGeneration_) {
                setStatus("Could not load the episode");
                showError("Could not load the episode", message);
            }
        });
}

void MainFrame::showPreferences() {
    PreferencesDialog dialog(this, settings_);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const bool providerChanged =
        dialog.settings().providerId != settings_.providerId || dialog.settings().baseUrl() != settings_.baseUrl();
    settings_ = dialog.settings();
    saveSettings(*wxConfigBase::Get(), settings_);
    applySettings();
    if (providerChanged) {
        ++searchGeneration_;
        ++episodeGeneration_;
        shows_.clear();
        episodes_.clear();
        results_->Clear();
        episodeList_->Clear();
        setStatus("Using " + wxString::FromUTF8(settings_.provider().name));
    }
}

void MainFrame::setStatus(const wxString& text) {
    SetStatusText(text);
}

void MainFrame::showError(const wxString& title, const std::string& message) {
    wxMessageBox(wxString::FromUTF8(message), title, wxOK | wxICON_ERROR, this);
}

Audio MainFrame::selectedAudio() const {
    return audioChoice_->GetSelection() == 1 ? Audio::Dub : Audio::Sub;
}

}
