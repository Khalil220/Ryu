#pragma once

#include "provider.hpp"

#include <wx/frame.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

struct mpv_handle;
struct mpv_event_property;
class wxButton;
class wxSlider;
class wxTextCtrl;

namespace ryu {

class PlayerFrame : public wxFrame {
public:
    explicit PlayerFrame(wxWindow* parent);
    ~PlayerFrame() override;

    void play(const Stream& stream, const wxString& title);

private:
    void createControls();
    void startMpv();
    void stopMpv();
    void processEvents();
    void onPropertyChange(uint64_t id, const mpv_event_property& property);
    void command(std::initializer_list<std::string> args);
    void togglePause();
    void seek(double seconds);
    void changeVolume(double delta);
    void updateTime();
    void onCharHook(wxKeyEvent& event);

    mpv_handle* mpv_ = nullptr;
    std::vector<Subtitle> pendingSubtitles_;
    std::string lastError_;
    double position_ = 0;
    double duration_ = 0;
    double volume_ = 100;
    long shownSecond_ = -1;

    wxWindow* video_ = nullptr;
    wxButton* pauseButton_ = nullptr;
    wxSlider* positionSlider_ = nullptr;
    wxTextCtrl* timeText_ = nullptr;
    wxSlider* volumeSlider_ = nullptr;
};

}
