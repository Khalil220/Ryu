#pragma once

#include "provider.hpp"

#include <wx/panel.h>
#include <wx/timer.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

struct mpv_handle;
struct mpv_event_property;
struct mpv_node;
class wxButton;
class wxChoice;
class wxSlider;
class wxTextCtrl;

namespace ryu {

class PlayerPanel : public wxPanel {
public:
    PlayerPanel(wxWindow* parent, std::function<void()> onLeave, std::function<void(int)> onStep);
    ~PlayerPanel() override;

    void play(const Stream& stream);
    void stop();
    void focusControls();

private:
    void createControls();
    void startMpv();
    void stopMpv();
    void processEvents();
    void onPropertyChange(uint64_t id, const mpv_event_property& property);
    void refreshSubtitles(const mpv_node& tracks);
    void command(std::initializer_list<std::string> args);
    void commandAsync(std::initializer_list<std::string> args);
    void togglePause();
    void seek(double seconds);
    void changeVolume(double delta);
    void updateTime();
    void resetState(const wxString& timeText);
    void onCharHook(wxKeyEvent& event);
    void checkForStall();

    std::function<void()> onLeave_;
    std::function<void(int)> onStep_;
    mpv_handle* mpv_ = nullptr;
    std::vector<Subtitle> pendingSubtitles_;
    std::vector<int64_t> subtitleTracks_;
    std::string lastError_;
    double position_ = 0;
    double duration_ = 0;
    double volume_ = 100;
    long shownSecond_ = -1;
    int paused_ = -1;
    bool active_ = false;
    bool ended_ = false;
    double lastProgress_ = -1;
    int stalledSeconds_ = 0;
    bool stallReported_ = false;
    wxTimer stallTimer_;

    wxWindow* video_ = nullptr;
    wxButton* pauseButton_ = nullptr;
    wxSlider* positionSlider_ = nullptr;
    wxTextCtrl* timeText_ = nullptr;
    wxSlider* volumeSlider_ = nullptr;
    wxChoice* subtitleChoice_ = nullptr;
};

}
