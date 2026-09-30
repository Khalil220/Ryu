#pragma once

#include "provider.hpp"
#include "settings.hpp"

#include <wx/bitmap.h>
#include <wx/frame.h>
#include <wx/timer.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class wxButton;
class wxChoice;
class wxPanel;
class wxSimplebook;
class wxTextCtrl;

namespace ryu {

class ShowDetailsPanel;

class PlayerPanel;
class TextList;
class PlaylistServer;
struct ProviderSession;
class Updater;

class MainFrame : public wxFrame {
public:
    explicit MainFrame(SettingsStore& store);
    ~MainFrame() override;

private:
    void createMenu();
    void createControls();
    void applySettings();
    void startSearch();
    void loadEpisodes();
    void showEpisodes(size_t preferred);
    void showDetailsFor(long row);
    void speakSynopsis();
    void loadDetails();
    void loadPoster(const std::string& url, unsigned generation);
    wxString episodeCountLabel(bool mentionAudio) const;
    void playEpisode(size_t index, bool announceNow = false);
    void announceIfSlow(const wxString& message);
    void loadFinished();
    void stepEpisode(int delta);
    void showPlayer(const wxString& title);
    void showBrowser();
    bool browsing() const;
    void showPreferences();
    void setStatus(const wxString& text);
    void showError(const wxString& title, const std::string& message);
    Audio selectedAudio() const;

    SettingsStore& store_;
    Settings settings_;
    std::shared_ptr<ProviderSession> session_;
    std::shared_ptr<PlaylistServer> playlistServer_;
    std::shared_ptr<int> alive_;
    std::unique_ptr<Updater> updater_;
    std::vector<ryu::Show> shows_;
    std::vector<Episode> episodes_;
    std::vector<size_t> visibleEpisodes_;
    ryu::Show currentShow_;
    size_t currentEpisode_ = 0;
    std::optional<size_t> pendingEpisode_;
    wxRect normalRect_;
    Audio playingAudio_ = Audio::Sub;
    unsigned searchGeneration_ = 0;
    unsigned episodeGeneration_ = 0;
    unsigned streamGeneration_ = 0;
    unsigned posterGeneration_ = 0;
    std::map<std::string, wxBitmap> posterCache_;
    wxTimer detailsTimer_;
    wxTimer loadingTimer_;
    wxString loadingMessage_;
    std::set<std::string> described_;

    wxSimplebook* book_ = nullptr;
    wxPanel* browsePage_ = nullptr;
    PlayerPanel* player_ = nullptr;
    wxTextCtrl* searchBox_ = nullptr;
    wxButton* searchButton_ = nullptr;
    TextList* results_ = nullptr;
    ShowDetailsPanel* details_ = nullptr;
    TextList* episodeList_ = nullptr;
    wxChoice* audioChoice_ = nullptr;
    wxButton* playButton_ = nullptr;
};

}
