#include "main_frame.hpp"

#include "accessibility.hpp"
#include "background.hpp"
#include "format.hpp"
#include "log.hpp"
#include "mal_session.hpp"
#include "stream_finder.hpp"
#include "playlist_server.hpp"
#include "player_panel.hpp"
#include "preferences_dialog.hpp"
#include "shortcuts_dialog.hpp"
#include "show_details_panel.hpp"
#include "speech.hpp"
#include "text_list.hpp"
#include "updater.hpp"

#include <algorithm>

#include <wx/aboutdlg.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/display.h>
#include <wx/iconbndl.h>
#include <wx/image.h>
#include <wx/log.h>
#include <wx/mstream.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

namespace ryu {

namespace {

constexpr int slowLoadMilliseconds = 700;
constexpr int detailsDelayMilliseconds = 100;

}

struct ProviderSession {
    std::shared_ptr<HttpClient> http;
    std::vector<std::unique_ptr<Provider>> providers;
    std::vector<ProviderHandle> handles;

    Provider& primary() const { return *providers.front(); }
};

MainFrame::MainFrame(SettingsStore& store)
    : wxFrame(nullptr, wxID_ANY, "Ryu"), store_(store), playlistServer_(std::make_shared<PlaylistServer>()),
      alive_(std::make_shared<int>(0)) {
    settings_ = store_.load();
    SetIcons(wxIconBundle("appicon", nullptr));
    createMenu();
    createControls();
    CreateStatusBar();
    SetMinSize(FromDIP(wxSize(760, 480)));
    const auto& saved = settings_.window;
    const wxRect savedRect(saved.x, saved.y, saved.width, saved.height);
    int display = wxNOT_FOUND;
    long largestOverlap = 0;
    for (unsigned i = 0; saved.width > 0 && saved.height > 0 && i < wxDisplay::GetCount(); ++i) {
        const auto overlap = wxDisplay(i).GetClientArea().Intersect(savedRect);
        const long size = static_cast<long>(std::max(0, overlap.width)) * std::max(0, overlap.height);
        if (size > largestOverlap) {
            largestOverlap = size;
            display = static_cast<int>(i);
        }
    }
    if (display != wxNOT_FOUND) {
        const auto area = wxDisplay(static_cast<unsigned>(display)).GetClientArea();
        const auto minimum = GetMinSize();
        const auto fitted = fitToArea(saved, {area.x, area.y, area.width, area.height}, minimum.x, minimum.y);
        SetSize(wxRect(fitted.x, fitted.y, fitted.width, fitted.height));
    } else {
        const auto area = wxDisplay().GetClientArea();
        auto size = FromDIP(wxSize(1000, 680));
        size.DecTo(area.GetSize());
        SetSize(wxRect(area.GetTopLeft() + (area.GetSize() - size) / 2, size));
    }
    if (saved.width > 0 && saved.maximized) {
        Maximize();
    }
    normalRect_ = GetRect();
    detailsTimer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { showSelectedDetails(); }, detailsTimer_.GetId());
    loadingTimer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { announce(loadingMessage_); }, loadingTimer_.GetId());
    const auto trackNormalRect = [this](wxEvent& event) {
        if (!IsMaximized() && !IsFullScreen() && !IsIconized()) {
            normalRect_ = GetRect();
        }
        event.Skip();
    };
    Bind(wxEVT_SIZE, trackNormalRect);
    Bind(wxEVT_MOVE, trackNormalRect);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        settings_.window = {normalRect_.x, normalRect_.y, normalRect_.width, normalRect_.height, IsMaximized()};
        store_.save(settings_);
        event.Skip();
    });
    applySettings();
    setStatus("Ready");
    searchBox_->SetFocus();
    updater_ = std::make_unique<Updater>(this, alive_, [this](const wxString& text) { setStatus(text); });
    mal_ = std::make_unique<MalSession>(settings_.mal, alive_, [this](const MalAccount& account) {
        settings_.mal = account;
        store_.save(settings_);
    });
    Updater::cleanUp();
    if (settings_.checkForUpdates) {
        CallAfter([this] { updater_->check(false); });
    }
}

MainFrame::~MainFrame() = default;

void MainFrame::createMenu() {
    auto* file = new wxMenu;
    file->Append(wxID_PREFERENCES, "&Preferences...\tCtrl+P");
    file->AppendSeparator();
    file->Append(wxID_EXIT, "E&xit");
    auto* help = new wxMenu;
    const int shortcutsId = wxWindow::NewControlId();
    help->Append(shortcutsId, "&Keyboard shortcuts\tF1");
    const int updatesId = wxWindow::NewControlId();
    help->Append(updatesId, "Check for &updates");
    help->Append(wxID_ABOUT, "&About Ryu");
    auto* bar = new wxMenuBar;
    bar->Append(file, "&File");
    bar->Append(help, "&Help");
    SetMenuBar(bar);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { showPreferences(); }, wxID_PREFERENCES);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { Close(); }, wxID_EXIT);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { ShortcutsDialog(this).ShowModal(); }, shortcutsId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { updater_->check(true); }, updatesId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        wxAboutDialogInfo about;
        about.SetName("Ryu");
        about.SetVersion(RYU_VERSION);
        about.SetDescription("An accessible anime player for Windows.");
        about.SetWebSite("https://github.com/Khalil220/Ryu");
        about.SetLicence("Ryu is licensed under the GNU General Public License v3.0.");
        about.SetIcon(GetIcon());
        wxAboutBox(about, this);
    }, wxID_ABOUT);
}

void MainFrame::createControls() {
    book_ = new wxSimplebook(this);
    browsePage_ = new wxPanel(book_);
    auto* panel = browsePage_;
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    const int gap = FromDIP(12);
    auto* searchRow = new wxBoxSizer(wxHORIZONTAL);
    searchRow->Add(new wxStaticText(panel, wxID_ANY, "&Search:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    searchBox_ = new wxTextCtrl(panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    searchRow->Add(searchBox_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    searchButton_ = new wxButton(panel, wxID_ANY, "Search");
    searchRow->Add(searchButton_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(searchRow, 0, wxEXPAND | wxALL, gap);

    auto* showsRow = new wxBoxSizer(wxHORIZONTAL);
    auto* resultsColumn = new wxBoxSizer(wxVERTICAL);
    resultsColumn->Add(new wxStaticText(panel, wxID_ANY, "&Results:"), 0, wxBOTTOM, FromDIP(4));
    results_ = new TextList(panel);
    results_->SetMinSize(FromDIP(wxSize(-1, 120)));
    resultsColumn->Add(results_, 1, wxEXPAND);
    showsRow->Add(resultsColumn, 1, wxEXPAND | wxRIGHT, gap);
    details_ = new ShowDetailsPanel(panel);
    showsRow->Add(details_, 1, wxEXPAND | wxTOP, FromDIP(4));
    sizer->Add(showsRow, 1, wxEXPAND | wxLEFT | wxRIGHT, gap);

    sizer->Add(new wxStaticText(panel, wxID_ANY, "&Episodes:"), 0, wxLEFT | wxRIGHT | wxTOP, gap);
    episodeList_ = new TextList(panel);
    episodeList_->SetMinSize(FromDIP(wxSize(-1, 120)));
    sizer->Add(episodeList_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, gap);

    auto* playRow = new wxBoxSizer(wxHORIZONTAL);
    playRow->Add(new wxStaticText(panel, wxID_ANY, "&Audio:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    audioChoice_ = new wxChoice(panel, wxID_ANY);
    audioChoice_->Append("Subbed");
    audioChoice_->Append("Dubbed");
    playRow->Add(audioChoice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, gap);
    playButton_ = new wxButton(panel, wxID_ANY, "&Play");
    playRow->Add(playButton_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(playRow, 0, wxALL, gap);


    panel->SetSizer(sizer);

    player_ = new PlayerPanel(
        book_, [this] { showBrowser(); }, [this](int delta) { stepEpisode(delta); },
        [this](bool read) {
            (playingAudio_ == Audio::Dub ? settings_.readSubtitlesDubbed : settings_.readSubtitlesSubbed) = read;
            store_.save(settings_);
        },
        [this](const SubtitlePreference& subtitles) {
            settings_.subtitlesFor(playingAudio_) = subtitles;
            store_.save(settings_);
        },
        [this](const std::string& language) {
            settings_.audioLanguageFor(playingAudio_) = language;
            store_.save(settings_);
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
    results_->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent&) {
        ++posterGeneration_;
        detailsTimer_.StartOnce(detailsDelayMilliseconds);
    });
    results_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == 'D' && event.GetModifiers() == wxMOD_CONTROL) {
            speakSynopsis();
            return;
        }
        event.Skip();
    });
    const int speakSynopsisId = wxWindow::NewControlId();
    results_->Bind(wxEVT_CONTEXT_MENU, [this, speakSynopsisId](wxContextMenuEvent& event) {
        const long row = results_->GetFirstSelected();
        if (row < 0 || static_cast<size_t>(row) >= shows_.size()) {
            return;
        }
        wxMenu menu;
        menu.Append(speakSynopsisId, "&Speak synopsis\tCtrl+D");
        auto position = event.GetPosition();
        if (position == wxDefaultPosition) {
            wxRect item;
            results_->GetItemRect(row, item);
            position = results_->ClientToScreen(item.GetBottomLeft());
        }
        results_->PopupMenu(&menu, results_->ScreenToClient(position));
    });
    results_->Bind(wxEVT_MENU, [this](wxCommandEvent&) { speakSynopsis(); }, speakSynopsisId);
    episodeList_->Bind(wxEVT_LIST_ITEM_ACTIVATED,
                       [this](wxListEvent& event) { playEpisode(static_cast<size_t>(event.GetIndex())); });
    playButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { playEpisode(episodeList_->selectedIndex()); });
    audioChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        settings_.audio = selectedAudio();
        store_.save(settings_);
        if (episodes_.empty()) {
            return;
        }
        const size_t row = episodeList_->selectedIndex();
        showEpisodes(row < visibleEpisodes_.size() ? visibleEpisodes_[row] : 0);
        setStatus(episodeCountLabel(true) + " of " + wxString::FromUTF8(currentShow_.title));
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
    loadFinished();
    shows_.clear();
    episodes_.clear();
    visibleEpisodes_.clear();
    ++posterGeneration_;
    detailsTimer_.Stop();
    described_.clear();
    details_->clear();
    results_->setItems({});
    episodeList_->setItems({});
    setStatus("Searching for " + query + "...");

    auto session = session_;
    runInBackground<std::vector<ryu::Show>>(
        alive_, [session, text = query.utf8_string()] { return session->primary().search(text); },
        [this, generation, query](std::vector<ryu::Show> shows) {
            if (generation != searchGeneration_) {
                return;
            }
            shows_ = std::move(shows);
            std::vector<wxString> labels;
            if (shows_.empty()) {
                labels.push_back("No results for " + query);
                setStatus("No results for " + query);
            } else {
                for (const auto& show : shows_) {
                    labels.push_back(wxString::FromUTF8(showLabel(show)));
                }
                setStatus(wxString::Format("%zu results for %s", shows_.size(), query));
            }
            results_->setItems(std::move(labels));
            results_->selectItem(0);
            if (!browsing()) {
                return;
            }
            results_->SetFocus();
        },
        [this, generation](const std::string& message) {
            if (generation == searchGeneration_) {
                setStatus("Search failed");
                showError("Search failed", message);
            }
        });
}

void MainFrame::loadEpisodes() {
    const size_t index = results_->selectedIndex();
    if (index >= shows_.size()) {
        return;
    }
    currentShow_ = shows_[index];
    const unsigned generation = ++episodeGeneration_;
    episodes_.clear();
    visibleEpisodes_.clear();
    episodeList_->setItems({});
    const auto title = wxString::FromUTF8(currentShow_.title);
    setStatus("Loading episodes of " + title + "...");
    announceIfSlow("Loading episodes");

    auto session = session_;
    runInBackground<std::vector<Episode>>(
        alive_, [session, show = currentShow_] { return session->primary().episodes(show); },
        [this, generation, title](std::vector<Episode> episodes) {
            if (generation != episodeGeneration_) {
                return;
            }
            loadFinished();
            episodes_ = std::move(episodes);
            showEpisodes(0);
            setStatus(episodes_.empty() ? "No episodes available for " + title
                                        : episodeCountLabel(false) + " of " + title);
            if (!browsing()) {
                return;
            }
            episodeList_->SetFocus();
        },
        [this, generation](const std::string& message) {
            if (generation == episodeGeneration_) {
                loadFinished();
                setStatus("Could not load episodes");
                showError("Could not load episodes", message);
            }
        });
}

void MainFrame::showEpisodes(size_t preferred) {
    const auto audio = selectedAudio();
    visibleEpisodes_.clear();
    std::vector<wxString> labels;
    for (size_t i = 0; i < episodes_.size(); ++i) {
        if (episodes_[i].availableIn(audio)) {
            visibleEpisodes_.push_back(i);
            labels.push_back(wxString::FromUTF8(episodeLabel(episodes_[i])));
        }
    }
    if (labels.empty()) {
        labels.push_back(episodes_.empty()      ? wxString("No episodes available")
                         : audio == Audio::Dub ? wxString("No dubbed episodes")
                                               : wxString("No subbed episodes"));
    }
    episodeList_->setItems(std::move(labels));
    long row = 0;
    for (size_t r = 0; r < visibleEpisodes_.size() && visibleEpisodes_[r] <= preferred; ++r) {
        row = static_cast<long>(r);
    }
    episodeList_->selectItem(row);
}

wxString MainFrame::episodeCountLabel(bool mentionAudio) const {
    const auto count = visibleEpisodes_.size();
    const wxString kind = !mentionAudio                    ? wxString()
                          : selectedAudio() == Audio::Dub ? wxString("dubbed ")
                                                          : wxString("subbed ");
    return count == 1 ? "1 " + kind + "episode" : wxString::Format("%zu %sepisodes", count, kind);
}

void MainFrame::speakSynopsis() {
    const auto row = results_->selectedIndex();
    if (row >= shows_.size()) {
        return;
    }
    const auto& synopsis = shows_[row].synopsis;
    announce(synopsis.empty() ? wxString("No synopsis available") : wxString::FromUTF8(synopsis));
}

void MainFrame::showSelectedDetails() {
    const auto index = results_->selectedIndex();
    if (index >= shows_.size()) {
        details_->clear();
        return;
    }
    const auto& show = shows_[index];
    details_->showDetails(show);
    const auto* cached = posterCache_.find(show.posterUrl);
    if (cached) {
        details_->setPoster(*cached);
    } else {
        details_->clearPoster();
    }
    if (!described_.contains(show.id) || (!cached && !show.posterUrl.empty())) {
        loadDetails();
    }
}

void MainFrame::loadDetails() {
    const auto index = results_->selectedIndex();
    if (index >= shows_.size()) {
        return;
    }
    const unsigned generation = posterGeneration_;
    if (described_.contains(shows_[index].id)) {
        loadPoster(shows_[index].posterUrl, generation);
        return;
    }
    auto session = session_;
    const unsigned search = searchGeneration_;
    runInBackground<ryu::Show>(
        alive_, [session, show = shows_[index]] { return session->primary().describe(show); },
        [this, generation, search, index](ryu::Show described) {
            if (search != searchGeneration_ || index >= shows_.size()) {
                return;
            }
            described_.insert(described.id);
            shows_[index] = std::move(described);
            if (generation == posterGeneration_) {
                details_->showDetails(shows_[index]);
                loadPoster(shows_[index].posterUrl, generation);
            }
        },
        [this, generation, search, index](const std::string& message) {
            logLine("Could not load show details: " + message);
            if (search == searchGeneration_ && index < shows_.size() && generation == posterGeneration_) {
                loadPoster(shows_[index].posterUrl, generation);
            }
        });
}

void MainFrame::loadPoster(const std::string& url, unsigned generation) {
    if (url.empty()) {
        return;
    }
    if (const auto* cached = posterCache_.find(url)) {
        details_->setPoster(*cached);
        return;
    }
    auto session = session_;
    runInBackground<wxImage>(
        alive_,
        [session, url, box = details_->posterSize()] {
            auto response = session->http->get(url);
            if (response.status < 200 || response.status >= 300) {
                throw HttpError("The poster returned HTTP " + std::to_string(response.status));
            }
            wxLogNull quiet;
            wxMemoryInputStream stream(response.body.data(), response.body.size());
            const wxImage image(stream, wxBITMAP_TYPE_ANY);
            if (!image.IsOk()) {
                throw HttpError("The poster could not be decoded");
            }
            return fitPoster(image, box);
        },
        [this, generation, url](wxImage poster) {
            const auto bundle = details_->posterFrom(poster);
            posterCache_.put(url, bundle);
            if (generation == posterGeneration_) {
                details_->setPoster(bundle);
            }
        },
        [](const std::string&) {});
}

void MainFrame::playEpisode(size_t row, bool announceNow) {
    if (row >= visibleEpisodes_.size()) {
        return;
    }
    const size_t index = visibleEpisodes_[row];
    const auto episode = episodes_[index];
    const auto audio = selectedAudio();
    const auto label = wxString::FromUTF8(episodeLabel(episode));
    const auto title = label + " - " + wxString::FromUTF8(currentShow_.title);
    const unsigned generation = ++streamGeneration_;
    pendingEpisode_ = index;
    setStatus("Loading " + title + "...");
    if (announceNow) {
        loadFinished();
        announce("Loading " + label);
    } else {
        announceIfSlow("Loading " + label);
    }

    auto session = session_;
    auto server = playlistServer_;
    runInBackground<FoundStream>(
        alive_,
        [session, server, show = currentShow_, episode, audio] {
            return findStream(*session->http, *server, session->handles, show, episode, audio);
        },
        [this, generation, index, title, label](FoundStream found) {
            if (generation != streamGeneration_) {
                logLine("Dropped the stream for " + title.utf8_string() + " because a newer request replaced it");
                return;
            }
            pendingEpisode_.reset();
            loadFinished();
            try {
                playingAudio_ = found.stream.audio;
                player_->play(found.stream, label.utf8_string(), settings_.readSubtitlesFor(found.stream.audio),
                              settings_.subtitlesFor(found.stream.audio),
                              settings_.audioLanguageFor(found.stream.audio));
                currentEpisode_ = index;
                showPlayer(title);
                if (found.fromFallback) {
                    const auto source = wxString::FromUTF8(found.providerName);
                    setStatus("Playing " + title + " from " + source);
                    announce("From " + source, false);
                }
            } catch (const std::exception& error) {
                logLine(std::string("Playback failed: ") + error.what());
                setStatus("Playback failed");
                showError("Playback failed", error.what());
            }
        },
        [this, generation](const std::string& message) {
            logLine("Could not load the episode: " + message);
            if (generation == streamGeneration_) {
                pendingEpisode_.reset();
                loadFinished();
                setStatus("Could not load the episode");
                showError("Could not load the episode", message);
            }
        });
}

void MainFrame::stepEpisode(int delta) {
    if (browsing()) {
        return;
    }
    const auto current = std::ranges::find(visibleEpisodes_, pendingEpisode_.value_or(currentEpisode_));
    if (current == visibleEpisodes_.end()) {
        return;
    }
    const auto target = (current - visibleEpisodes_.begin()) + delta;
    if (target < 0) {
        announce("This is the first episode");
        return;
    }
    if (static_cast<size_t>(target) >= visibleEpisodes_.size()) {
        announce("This is the last episode");
        return;
    }
    playEpisode(static_cast<size_t>(target), true);
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
    pendingEpisode_.reset();
    loadFinished();
    player_->stop();
    book_->ChangeSelection(0);
    SetTitle("Ryu - " + wxString::FromUTF8(settings_.provider().name));
    setStatus("Stopped");
    if (const auto row = std::ranges::find(visibleEpisodes_, currentEpisode_); row != visibleEpisodes_.end()) {
        episodeList_->selectItem(static_cast<long>(row - visibleEpisodes_.begin()));
        episodeList_->SetFocus();
    } else {
        searchBox_->SetFocus();
    }
}

bool MainFrame::browsing() const {
    return book_->GetSelection() == 0;
}

void MainFrame::showPreferences() {
    PreferencesDialog dialog(this, settings_, *mal_);
    if (dialog.ShowModal() != wxID_OK) {
        return;
    }
    const bool providerChanged =
        dialog.settings().providerId != settings_.providerId || dialog.settings().baseUrl() != settings_.baseUrl();
    if (providerChanged && !browsing()) {
        showBrowser();
    }
    settings_ = dialog.settings();
    settings_.mal = mal_->account();
    store_.save(settings_);
    applySettings();
    if (providerChanged) {
        ++searchGeneration_;
        ++episodeGeneration_;
        loadFinished();
        shows_.clear();
        episodes_.clear();
        visibleEpisodes_.clear();
        ++posterGeneration_;
        detailsTimer_.Stop();
        described_.clear();
        details_->clear();
        results_->setItems({});
        episodeList_->setItems({});
        setStatus("Using " + wxString::FromUTF8(settings_.provider().name));
    }
}

void MainFrame::announceIfSlow(const wxString& message) {
    loadingMessage_ = message;
    loadingTimer_.StartOnce(slowLoadMilliseconds);
}

void MainFrame::loadFinished() {
    loadingTimer_.Stop();
    loadingMessage_.clear();
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
