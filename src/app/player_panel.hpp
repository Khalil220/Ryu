#pragma once

#include "provider.hpp"
#include "stall_watch.hpp"
#include "subtitle_choice.hpp"

#include <wx/panel.h>
#include <wx/timer.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

struct mpv_handle;
struct mpv_event_property;
struct mpv_node;
class wxButton;
class wxCheckBox;
class wxChoice;
class wxSlider;
class wxTextCtrl;

namespace ryu {

class PlayerPanel : public wxPanel {
public:
    PlayerPanel(wxWindow* parent, std::function<void()> onLeave, std::function<void(int)> onStep,
                std::function<void(bool)> onReadSubtitlesChanged,
                std::function<void(const SubtitlePreference&)> onSubtitlesChosen,
                std::function<void(const std::string&)> onAudioChosen);
    ~PlayerPanel() override;

    void play(const Stream& stream, const std::string& heading, bool readSubtitles,
              const SubtitlePreference& subtitles, const std::string& audioLanguage);
    void stop();
    void focusControls();

private:
    void createControls();
    void startMpv();
    void stopMpv();
    void processEvents();
    void onPropertyChange(uint64_t id, const mpv_event_property& property);
    void refreshSubtitles(const mpv_node& tracks);
    void refreshAudio(const mpv_node& tracks);
    void showAudioChoice(bool show);
    void command(std::initializer_list<std::string> args);
    void commandAsync(std::initializer_list<std::string> args);
    void togglePause();
    void seek(double seconds);
    void changeVolume(double delta);
    void updateTime();
    void resetState(const wxString& timeText);
    void onCharHook(wxKeyEvent& event);
    void checkForStall();
    void skipIntro();
    void toggleReadSubtitles();
    void speakSubtitle(const char* raw);
    void announceIntro();
    bool inIntro() const;
    void updateIntroButton();
    void showVolume(double volume);
    void showPaused(bool paused);
    bool isFullScreen();
    void setFullScreen(bool fullScreen);

    std::function<void()> onLeave_;
    std::function<void(int)> onStep_;
    std::function<void(bool)> onReadSubtitlesChanged_;
    std::function<void(const SubtitlePreference&)> onSubtitlesChosen_;
    std::function<void(const std::string&)> onAudioChosen_;
    mpv_handle* mpv_ = nullptr;
    std::vector<Subtitle> pendingSubtitles_;
    std::vector<int64_t> subtitleTracks_;
    std::vector<std::string> subtitleLanguages_;
    SubtitlePreference subtitlePreference_;
    std::vector<int64_t> audioTracks_;
    std::vector<std::string> audioLanguages_;
    std::string heading_;
    std::string lastError_;
    double position_ = 0;
    double duration_ = 0;
    double volume_ = 100;
    long shownSecond_ = -1;
    int paused_ = -1;
    bool active_ = false;
    bool readSubtitles_ = false;
    std::string lastSubtitle_;
    std::vector<std::string> subtitleNoise_;
    std::optional<TimeRange> intro_;
    bool introAnnounced_ = false;
    bool ended_ = false;
    StallWatch stallWatch_;
    wxTimer stallTimer_;

    wxWindow* video_ = nullptr;
    wxButton* pauseButton_ = nullptr;
    wxSlider* positionSlider_ = nullptr;
    wxTextCtrl* timeText_ = nullptr;
    wxSlider* volumeSlider_ = nullptr;
    wxWindow* audioIcon_ = nullptr;
    wxChoice* audioChoice_ = nullptr;
    wxChoice* subtitleChoice_ = nullptr;
    wxButton* skipIntroButton_ = nullptr;
    wxButton* fullScreenButton_ = nullptr;
    wxCheckBox* readCheck_ = nullptr;
};

}
