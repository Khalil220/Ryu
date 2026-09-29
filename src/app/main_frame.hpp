#pragma once

#include "provider.hpp"
#include "settings.hpp"

#include <wx/frame.h>

#include <memory>
#include <string>
#include <vector>

class wxButton;
class wxChoice;
class wxPanel;
class wxSimplebook;
class wxTextCtrl;

namespace ryu {

class PlayerPanel;
class TextList;
class PlaylistServer;
struct ProviderSession;

class MainFrame : public wxFrame {
public:
    MainFrame();

private:
    void createMenu();
    void createControls();
    void applySettings();
    void startSearch();
    void loadEpisodes();
    void playEpisode(size_t index);
    void stepEpisode(int delta);
    void showPlayer(const wxString& title);
    void showBrowser();
    bool browsing() const;
    void showPreferences();
    void setStatus(const wxString& text);
    void showError(const wxString& title, const std::string& message);
    Audio selectedAudio() const;

    Settings settings_;
    std::shared_ptr<ProviderSession> session_;
    std::shared_ptr<PlaylistServer> playlistServer_;
    std::shared_ptr<int> alive_;
    std::vector<ryu::Show> shows_;
    std::vector<Episode> episodes_;
    ryu::Show currentShow_;
    size_t currentEpisode_ = 0;
    Audio playingAudio_ = Audio::Sub;
    unsigned searchGeneration_ = 0;
    unsigned episodeGeneration_ = 0;
    unsigned streamGeneration_ = 0;

    wxSimplebook* book_ = nullptr;
    wxPanel* browsePage_ = nullptr;
    PlayerPanel* player_ = nullptr;
    wxTextCtrl* searchBox_ = nullptr;
    wxButton* searchButton_ = nullptr;
    TextList* results_ = nullptr;
    TextList* episodeList_ = nullptr;
    wxChoice* audioChoice_ = nullptr;
    wxButton* playButton_ = nullptr;
};

}
