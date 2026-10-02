#pragma once

#include "lru_cache.hpp"
#include "provider.hpp"
#include "settings.hpp"

#include <wx/bmpbndl.h>
#include <wx/frame.h>
#include <wx/timer.h>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

class wxButton;
class wxChoice;
class wxMenu;
class wxMenuItem;
class wxPanel;
class wxSimplebook;
class wxTextCtrl;

namespace ryu {

inline constexpr size_t posterCacheSize = 60;

class ShowDetailsPanel;

class PlayerPanel;
class TextList;
class PlaylistServer;
struct ProviderSession;
class MalPage;
class MalSession;
class Updater;

class MainFrame : public wxFrame {
public:
    explicit MainFrame(SettingsStore& store);
    ~MainFrame() override;

private:
    void createMenu();
    void createControls();
    void applySettings();
    void startSearch(std::optional<MalAnime> sought = std::nullopt, bool retried = false);
    void loadEpisodes(const std::string& preferredNumber = {}, bool play = false);
    void refreshRecentMenu();
    void playRecent(size_t index);
    void showRecent(const ryu::Show& show, const std::string& episode);
    void showMalLists();
    void leaveMalLists();
    void refreshMalLists();
    void openMalEntry(const MalAnime& anime);
    void editShowOnMal();
    void editOnMal(std::vector<MalAnime> candidates, size_t selected, const std::string& showId);
    void removeFromMal(const MalAnime& anime, bool confirm);
    void trackProgress();
    void showEpisodes(size_t preferred);
    void showSelectedDetails();
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
    std::unique_ptr<MalSession> mal_;
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
    LruCache<std::string, wxBitmapBundle> posterCache_{posterCacheSize};
    wxTimer detailsTimer_;
    wxTimer loadingTimer_;
    wxString loadingMessage_;
    std::set<std::string> described_;

    int malListsId_ = 0;
    wxMenu* recentMenu_ = nullptr;
    wxMenuItem* recentItem_ = nullptr;
    wxWindow* focusBeforeLists_ = nullptr;
    MalPage* malPage_ = nullptr;
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
