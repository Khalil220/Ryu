#include "main_frame.hpp"

#include "accessibility.hpp"
#include "background.hpp"
#include "format.hpp"
#include "stream_finder.hpp"
#include "playlist_server.hpp"
#include "player_panel.hpp"
#include "preferences_dialog.hpp"
#include "speech.hpp"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/confbase.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace ryu {

struct ProviderSession {
    std::shared_ptr<HttpClient> http;
    std::vector<std::unique_ptr<Provider>> providers;
    std::vector<ProviderHandle> handles;

    Provider& primary() const { return *providers.front(); }
};

namespace {

void fitColumn(wxListView* list) {
    list->SetColumnWidth(0, wxLIST_AUTOSIZE_USEHEADER);
}

wxListView* createList(wxWindow* parent) {
    auto* list = new wxListView(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
    list->AppendColumn(wxEmptyString);
    list->Bind(wxEVT_SIZE, [list](wxSizeEvent& event) {
        event.Skip();
        list->CallAfter([list] { fitColumn(list); });
    });
    return list;
}

void appendItem(wxListView* list, const wxString& text) {
    list->InsertItem(list->GetItemCount(), text);
}

void selectItem(wxListView* list, long index) {
    if (index < 0 || index >= list->GetItemCount()) {
        return;
    }
    fitColumn(list);
    list->Select(index);
    list->Focus(index);
}

size_t selectedItem(const wxListView* list) {
    const long index = list->GetFirstSelected();
    return index < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(index);
}

}

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Ryu"), playlistServer_(std::make_shared<PlaylistServer>()),
      alive_(std::make_shared<int>(0)) {
    settings_ = loadSettings(*wxConfigBase::Get());
    createMenu();
    createControls();
    CreateStatusBar();
    SetMinSize(FromDIP(wxSize(480, 420)));
    SetSize(FromDIP(wxSize(800, 640)));
    applySettings();
    setStatus("Ready");
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
    book_ = new wxSimplebook(this);
    browsePage_ = new wxPanel(book_);
    auto* panel = browsePage_;
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* searchRow = new wxBoxSizer(wxHORIZONTAL);
    searchRow->Add(new wxStaticText(panel, wxID_ANY, "&Search:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    searchBox_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    searchRow->Add(searchBox_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    searchButton_ = new wxButton(panel, wxID_ANY, "Search");
    searchRow->Add(searchButton_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(searchRow, 0, wxEXPAND | wxALL, 8);

    sizer->Add(new wxStaticText(panel, wxID_ANY, "&Results:"), 0, wxLEFT | wxRIGHT, 8);
    results_ = createList(panel);
    results_->SetMinSize(FromDIP(wxSize(-1, 120)));
    sizer->Add(results_, 1, wxEXPAND | wxALL, 8);

    sizer->Add(new wxStaticText(panel, wxID_ANY, "&Episodes:"), 0, wxLEFT | wxRIGHT, 8);
    episodeList_ = createList(panel);
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

    player_ = new PlayerPanel(
        book_, [this] { showBrowser(); }, [this](int delta) { stepEpisode(delta); },
        [this](bool read) {
            (playingAudio_ == Audio::Dub ? settings_.readSubtitlesDubbed : settings_.readSubtitlesSubbed) = read;
            saveSettings(*wxConfigBase::Get(), settings_);
        });
    book_->AddPage(browsePage_, "Browse", true);
    book_->AddPage(player_, "Player");

    setAccessibleName(searchBox_, "Search");
    setAccessibleName(results_, "Results");
    setAccessibleName(episodeList_, "Episodes");
    setAccessibleName(audioChoice_, "Audio");

    searchBox_->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { startSearch(); });
    searchButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { startSearch(); });
    results_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { loadEpisodes(); });
    episodeList_->Bind(wxEVT_LIST_ITEM_ACTIVATED,
                       [this](wxListEvent& event) { playEpisode(static_cast<size_t>(event.GetIndex())); });
    playButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { playEpisode(selectedItem(episodeList_)); });
    audioChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        settings_.audio = selectedAudio();
        saveSettings(*wxConfigBase::Get(), settings_);
    });
}

void MainFrame::applySettings() {
    auto session = std::make_shared<ProviderSession>();
    session->http = std::make_shared<CurlHttpClient>();
    const auto add = [&](const ProviderInfo& info) {
        session->providers.push_back(info.create(*session->http, settings_.baseUrlFor(info)));
        session->handles.push_back({info.name, session->providers.back().get()});
    };
    add(settings_.provider());
    if (settings_.useFallback) {
        for (const auto& info : availableProviders()) {
            if (info.id != settings_.provider().id) {
                add(info);
            }
        }
    }
    session_ = std::move(session);
    audioChoice_->SetSelection(settings_.audio == Audio::Dub ? 1 : 0);
    SetTitle("Ryu - " + wxString::FromUTF8(settings_.provider().name));
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
    results_->DeleteAllItems();
    episodeList_->DeleteAllItems();
    setStatus("Searching for " + query + "...");
    announce("Searching for " + query);

    auto session = session_;
    runInBackground<std::vector<ryu::Show>>(
        alive_, [session, text = query.utf8_string()] { return session->primary().search(text); },
        [this, generation, query](std::vector<ryu::Show> shows) {
            if (generation != searchGeneration_) {
                return;
            }
            shows_ = std::move(shows);
            if (shows_.empty()) {
                appendItem(results_, "No results for " + query);
                setStatus("No results for " + query);
            } else {
                for (const auto& show : shows_) {
                    appendItem(results_, wxString::FromUTF8(showLabel(show)));
                }
                setStatus(wxString::Format("%zu results for %s", shows_.size(), query));
            }
            selectItem(results_, 0);
            if (!browsing()) {
                return;
            }
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
    const size_t index = selectedItem(results_);
    if (index >= shows_.size()) {
        return;
    }
    currentShow_ = shows_[index];
    const unsigned generation = ++episodeGeneration_;
    episodes_.clear();
    episodeList_->DeleteAllItems();
    const auto title = wxString::FromUTF8(currentShow_.title);
    setStatus("Loading episodes of " + title + "...");
    announce("Loading episodes");

    auto session = session_;
    runInBackground<std::vector<Episode>>(
        alive_, [session, id = currentShow_.id] { return session->primary().episodes(id); },
        [this, generation, title](std::vector<Episode> episodes) {
            if (generation != episodeGeneration_) {
                return;
            }
            episodes_ = std::move(episodes);
            if (episodes_.empty()) {
                appendItem(episodeList_, "No episodes available");
                setStatus("No episodes available for " + title);
            } else {
                for (const auto& episode : episodes_) {
                    appendItem(episodeList_, wxString::FromUTF8(episodeLabel(episode)));
                }
                setStatus(wxString::Format("%zu episodes of %s", episodes_.size(), title));
            }
            selectItem(episodeList_, 0);
            if (!browsing()) {
                return;
            }
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

void MainFrame::playEpisode(size_t index) {
    if (index >= episodes_.size()) {
        return;
    }
    const auto episode = episodes_[index];
    const auto audio = selectedAudio();
    const auto label = wxString::FromUTF8(episodeLabel(episode));
    const auto title = label + " - " + wxString::FromUTF8(currentShow_.title);
    const unsigned generation = ++streamGeneration_;
    setStatus("Loading " + title + "...");
    announce("Loading " + label);

    auto session = session_;
    auto server = playlistServer_;
    runInBackground<FoundStream>(
        alive_,
        [session, server, show = currentShow_, episode, audio] {
            return findStream(*session->http, *server, session->handles, show, episode, audio);
        },
        [this, generation, index, title](FoundStream found) {
            if (generation != streamGeneration_) {
                return;
            }
            try {
                playingAudio_ = found.stream.audio;
                player_->play(found.stream, settings_.readSubtitlesFor(found.stream.audio));
                currentEpisode_ = index;
                showPlayer(title);
                if (found.fromFallback) {
                    const auto source = wxString::FromUTF8(found.providerName);
                    setStatus("Playing " + title + " from " + source);
                    announce("From " + source, false);
                }
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

void MainFrame::stepEpisode(int delta) {
    if (browsing()) {
        return;
    }
    const auto target = static_cast<long long>(currentEpisode_) + delta;
    if (target < 0) {
        announce("This is the first episode");
        return;
    }
    if (static_cast<size_t>(target) >= episodes_.size()) {
        announce("This is the last episode");
        return;
    }
    playEpisode(static_cast<size_t>(target));
}

void MainFrame::showPlayer(const wxString& title) {
    if (browsing()) {
        book_->ChangeSelection(1);
    }
    SetTitle(title + " - Ryu");
    setStatus("Playing " + title);
    player_->focusControls();
}

void MainFrame::showBrowser() {
    ++streamGeneration_;
    player_->stop();
    book_->ChangeSelection(0);
    SetTitle("Ryu - " + wxString::FromUTF8(settings_.provider().name));
    setStatus("Stopped");
    if (currentEpisode_ < episodes_.size()) {
        selectItem(episodeList_, static_cast<long>(currentEpisode_));
        episodeList_->SetFocus();
    } else {
        searchBox_->SetFocus();
    }
}

bool MainFrame::browsing() const {
    return book_->GetSelection() == 0;
}

void MainFrame::showPreferences() {
    PreferencesDialog dialog(this, settings_);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const bool providerChanged =
        dialog.settings().providerId != settings_.providerId || dialog.settings().baseUrl() != settings_.baseUrl();
    if (providerChanged && !browsing()) {
        showBrowser();
    }
    settings_ = dialog.settings();
    saveSettings(*wxConfigBase::Get(), settings_);
    applySettings();
    if (providerChanged) {
        ++searchGeneration_;
        ++episodeGeneration_;
        shows_.clear();
        episodes_.clear();
        results_->DeleteAllItems();
        episodeList_->DeleteAllItems();
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
