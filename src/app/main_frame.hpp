#pragma once

#include "provider.hpp"
#include "settings.hpp"

#include <wx/frame.h>
#include <wx/weakref.h>

#include <memory>
#include <string>
#include <vector>

class wxButton;
class wxChoice;
class wxListBox;
class wxTextCtrl;

namespace ryu {

class PlayerFrame;
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
    void playSelectedEpisode();
    void showPreferences();
    void onCharHook(wxKeyEvent& event);
    void setStatus(const wxString& text);
    void showError(const wxString& title, const std::string& message);
    Audio selectedAudio() const;

    Settings settings_;
    std::shared_ptr<ProviderSession> session_;
    std::shared_ptr<int> alive_;
    std::vector<ryu::Show> shows_;
    std::vector<Episode> episodes_;
    ryu::Show currentShow_;
    unsigned searchGeneration_ = 0;
    unsigned episodeGeneration_ = 0;
    unsigned streamGeneration_ = 0;
    wxWeakRef<PlayerFrame> player_;

    wxTextCtrl* searchBox_ = nullptr;
    wxButton* searchButton_ = nullptr;
    wxListBox* results_ = nullptr;
    wxListBox* episodeList_ = nullptr;
    wxChoice* audioChoice_ = nullptr;
    wxButton* playButton_ = nullptr;
};

}
