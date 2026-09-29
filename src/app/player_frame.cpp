#include "player_frame.hpp"

#include "accessibility.hpp"
#include "format.hpp"
#include "http.hpp"
#include "speech.hpp"

#include <mpv/client.h>

#include <wx/accel.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>
#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace ryu {

namespace {

class VideoSurface : public wxWindow {
public:
    explicit VideoSurface(wxWindow* parent) : wxWindow(parent, wxID_ANY) {}
    bool AcceptsFocus() const override { return false; }
    bool AcceptsFocusFromKeyboard() const override { return false; }
};

enum PropertyId : uint64_t { TimePos = 1, Duration, Pause, Volume, TrackList, EndReached };

const mpv_node* field(const mpv_node& map, const char* key) {
    if (map.format != MPV_FORMAT_NODE_MAP) {
        return nullptr;
    }
    for (int i = 0; i < map.u.list->num; ++i) {
        if (std::string_view(map.u.list->keys[i]) == key) {
            return &map.u.list->values[i];
        }
    }
    return nullptr;
}

std::string text(const mpv_node* node) {
    return node && node->format == MPV_FORMAT_STRING ? node->u.string : "";
}

}

PlayerFrame::PlayerFrame(wxWindow* parent) : wxFrame(parent, wxID_ANY, "Ryu player") {
    createControls();
    startMpv();
    Bind(wxEVT_CHAR_HOOK, &PlayerFrame::onCharHook, this);
    const int speakTimeId = wxWindow::NewControlId();
    wxAcceleratorEntry speakTime(wxACCEL_NORMAL, 'T', speakTimeId);
    SetAcceleratorTable(wxAcceleratorTable(1, &speakTime));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { announce(wxString::FromUTF8(timeLabel(position_, duration_))); },
         speakTimeId);
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        stopMpv();
        event.Skip();
    });
}

PlayerFrame::~PlayerFrame() {
    stopMpv();
}

void PlayerFrame::createControls() {
    auto* root = new wxPanel(this);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    video_ = new VideoSurface(root);
    video_->SetBackgroundColour(*wxBLACK);
    video_->SetMinSize(FromDIP(wxSize(640, 360)));
    sizer->Add(video_, 1, wxEXPAND);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    pauseButton_ = new wxButton(root, wxID_ANY, "&Pause");
    auto* back = new wxButton(root, wxID_ANY, "&Back 10 seconds");
    auto* forward = new wxButton(root, wxID_ANY, "&Forward 10 seconds");
    auto* close = new wxButton(root, wxID_ANY, "&Close");
    for (auto* button : {pauseButton_, back, forward, close}) {
        buttons->Add(button, 0, wxRIGHT, 6);
    }
    sizer->Add(buttons, 0, wxALL, 8);

    auto* info = new wxBoxSizer(wxHORIZONTAL);
    info->Add(new wxStaticText(root, wxID_ANY, "Posi&tion:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    positionSlider_ = new wxSlider(root, wxID_ANY, 0, 0, 1);
    positionSlider_->SetLineSize(10);
    positionSlider_->SetPageSize(60);
    info->Add(positionSlider_, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    info->Add(new wxStaticText(root, wxID_ANY, "Ti&me:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    timeText_ = new wxTextCtrl(root, wxID_ANY, "Loading", wxDefaultPosition, FromDIP(wxSize(140, -1)), wxTE_READONLY);
    info->Add(timeText_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    info->Add(new wxStaticText(root, wxID_ANY, "&Volume:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    volumeSlider_ = new wxSlider(root, wxID_ANY, 100, 0, 100);
    volumeSlider_->SetLineSize(5);
    volumeSlider_->SetPageSize(20);
    info->Add(volumeSlider_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    info->Add(new wxStaticText(root, wxID_ANY, "&Subtitles:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    subtitleChoice_ = new wxChoice(root, wxID_ANY);
    subtitleChoice_->Append("Off");
    subtitleChoice_->SetSelection(0);
    info->Add(subtitleChoice_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(info, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

    root->SetSizer(sizer);
    setAccessibleName(positionSlider_, "Position");
    setAccessibleName(timeText_, "Time");
    setAccessibleName(volumeSlider_, "Volume");
    setAccessibleName(subtitleChoice_, "Subtitles");
    auto* frameSizer = new wxBoxSizer(wxVERTICAL);
    frameSizer->Add(root, 1, wxEXPAND);
    SetSizerAndFit(frameSizer);

    pauseButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { togglePause(); });
    back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { seek(-10); });
    forward->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { seek(10); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Close(); });
    positionSlider_->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        command({"seek", std::to_string(positionSlider_->GetValue()), "absolute"});
    });
    volumeSlider_->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        double volume = volumeSlider_->GetValue();
        mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &volume);
    });
    subtitleChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int index = subtitleChoice_->GetSelection();
        if (index <= 0 || static_cast<size_t>(index) > subtitleTracks_.size()) {
            mpv_set_property_string(mpv_, "sid", "no");
        } else {
            int64_t track = subtitleTracks_[index - 1];
            mpv_set_property(mpv_, "sid", MPV_FORMAT_INT64, &track);
        }
    });
}

void PlayerFrame::startMpv() {
    mpv_ = mpv_create();
    if (!mpv_) {
        throw std::runtime_error("Could not create the mpv player");
    }

    int64_t window = reinterpret_cast<intptr_t>(video_->GetHandle());
    mpv_set_option(mpv_, "wid", MPV_FORMAT_INT64, &window);
    mpv_set_option_string(mpv_, "input-vo-keyboard", "no");
    mpv_set_option_string(mpv_, "keep-open", "yes");
    mpv_set_option_string(mpv_, "force-window", "yes");
    mpv_set_option_string(mpv_, "hwdec", "auto-safe");
    mpv_set_option_string(mpv_, "osc", "yes");
    mpv_set_option_string(mpv_, "ytdl", "no");
    mpv_set_option_string(mpv_, "user-agent", CurlHttpClient::defaultUserAgent);

    wxString extra;
    if (wxGetEnv("RYU_MPV_OPTIONS", &extra)) {
        wxStringTokenizer options(extra, ",");
        while (options.HasMoreTokens()) {
            const auto option = options.GetNextToken();
            mpv_set_option_string(mpv_, option.BeforeFirst('=').utf8_str(), option.AfterFirst('=').utf8_str());
        }
    }

    if (mpv_initialize(mpv_) < 0) {
        mpv_terminate_destroy(mpv_);
        mpv_ = nullptr;
        throw std::runtime_error("Could not start the mpv player");
    }

    mpv_observe_property(mpv_, TimePos, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, Duration, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, Pause, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, Volume, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, TrackList, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(mpv_, EndReached, "eof-reached", MPV_FORMAT_FLAG);
    mpv_request_log_messages(mpv_, "error");
    mpv_set_wakeup_callback(
        mpv_, [](void* frame) { static_cast<PlayerFrame*>(frame)->CallAfter(&PlayerFrame::processEvents); }, this);
}

void PlayerFrame::stopMpv() {
    if (!mpv_) {
        return;
    }
    mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    mpv_terminate_destroy(mpv_);
    mpv_ = nullptr;
}

void PlayerFrame::play(const Stream& stream, const wxString& title) {
    std::vector<std::string> lines;
    for (const auto& [name, value] : stream.headers) {
        lines.push_back(name + ": " + value);
    }
    std::vector<mpv_node> items(lines.size());
    for (size_t i = 0; i < lines.size(); ++i) {
        items[i].format = MPV_FORMAT_STRING;
        items[i].u.string = lines[i].data();
    }
    mpv_node_list list{};
    list.num = static_cast<int>(items.size());
    list.values = items.data();
    mpv_node node{};
    node.format = MPV_FORMAT_NODE_ARRAY;
    node.u.list = &list;
    mpv_set_property(mpv_, "http-header-fields", MPV_FORMAT_NODE, &node);
    mpv_set_property_string(mpv_, "demuxer-lavf-o", stream.disguisedSegments ? "extension_picky=0" : "");

    pendingSubtitles_ = stream.subtitles;
    subtitleTracks_.clear();
    subtitleChoice_->Clear();
    subtitleChoice_->Append("Off");
    subtitleChoice_->SetSelection(0);
    lastError_.clear();
    position_ = 0;
    duration_ = 0;
    shownSecond_ = -1;
    paused_ = -1;
    timeText_->ChangeValue("Loading");
    SetTitle(title + " - Ryu");
    command({"loadfile", stream.url, "replace"});
    pauseButton_->SetFocus();
}

void PlayerFrame::processEvents() {
    while (mpv_) {
        const mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            return;
        }
        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE:
            onPropertyChange(event->reply_userdata, *static_cast<const mpv_event_property*>(event->data));
            break;
        case MPV_EVENT_FILE_LOADED:
            for (const auto& subtitle : pendingSubtitles_) {
                commandAsync({"sub-add", subtitle.url, subtitle.isDefault ? "select" : "auto", subtitle.label,
                              subtitle.language});
            }
            pendingSubtitles_.clear();
            announce("Playing");
            break;
        case MPV_EVENT_LOG_MESSAGE: {
            wxString line = wxString::FromUTF8(static_cast<const mpv_event_log_message*>(event->data)->text);
            lastError_ = line.Trim().utf8_string();
            break;
        }
        case MPV_EVENT_END_FILE: {
            const auto* end = static_cast<const mpv_event_end_file*>(event->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                wxString message = "Playback failed: " + wxString::FromUTF8(mpv_error_string(end->error));
                if (!lastError_.empty()) {
                    message += "\n" + wxString::FromUTF8(lastError_);
                }
                timeText_->ChangeValue("Failed");
                CallAfter([this, message] { wxMessageBox(message, "Ryu", wxOK | wxICON_ERROR, this); });
            }
            break;
        }
        default:
            break;
        }
    }
}

void PlayerFrame::onPropertyChange(uint64_t id, const mpv_event_property& property) {
    const bool isDouble = property.format == MPV_FORMAT_DOUBLE;
    const bool isFlag = property.format == MPV_FORMAT_FLAG;
    switch (id) {
    case TimePos:
        position_ = isDouble ? *static_cast<double*>(property.data) : 0;
        updateTime();
        break;
    case Duration:
        duration_ = isDouble ? *static_cast<double*>(property.data) : 0;
        positionSlider_->SetRange(0, std::max(1, static_cast<int>(duration_)));
        shownSecond_ = -1;
        updateTime();
        break;
    case Pause:
        if (isFlag) {
            const int paused = *static_cast<int*>(property.data) ? 1 : 0;
            pauseButton_->SetLabel(paused ? "&Play" : "&Pause");
            if (paused_ != -1 && paused != paused_) {
                announce(paused ? "Paused" : "Playing");
            }
            paused_ = paused;
        }
        break;
    case Volume:
        if (isDouble) {
            volume_ = *static_cast<double*>(property.data);
            if (FindFocus() != volumeSlider_) {
                volumeSlider_->SetValue(static_cast<int>(std::lround(volume_)));
            }
        }
        break;
    case TrackList:
        if (property.format == MPV_FORMAT_NODE) {
            refreshSubtitles(*static_cast<const mpv_node*>(property.data));
        }
        break;
    case EndReached:
        if (isFlag && *static_cast<int*>(property.data)) {
            announce("End of episode");
        }
        break;
    default:
        break;
    }
}

void PlayerFrame::refreshSubtitles(const mpv_node& tracks) {
    if (tracks.format != MPV_FORMAT_NODE_ARRAY) {
        return;
    }
    std::vector<int64_t> ids;
    wxArrayString labels;
    int selected = 0;
    for (int i = 0; i < tracks.u.list->num; ++i) {
        const auto& track = tracks.u.list->values[i];
        if (text(field(track, "type")) != "sub") {
            continue;
        }
        const auto* id = field(track, "id");
        if (!id || id->format != MPV_FORMAT_INT64) {
            continue;
        }
        auto label = text(field(track, "title"));
        if (label.empty()) {
            label = text(field(track, "lang"));
        }
        if (label.empty()) {
            label = "Track " + std::to_string(id->u.int64);
        }
        ids.push_back(id->u.int64);
        labels.Add(wxString::FromUTF8(label));
        const auto* isSelected = field(track, "selected");
        if (isSelected && isSelected->format == MPV_FORMAT_FLAG && isSelected->u.flag) {
            selected = static_cast<int>(ids.size());
        }
    }
    if (ids == subtitleTracks_ && subtitleChoice_->GetSelection() == selected) {
        return;
    }
    subtitleTracks_ = std::move(ids);
    subtitleChoice_->Clear();
    subtitleChoice_->Append("Off");
    subtitleChoice_->Append(labels);
    subtitleChoice_->SetSelection(selected);
}

void PlayerFrame::updateTime() {
    const long second = static_cast<long>(position_);
    if (second == shownSecond_) {
        return;
    }
    shownSecond_ = second;
    timeText_->ChangeValue(wxString::FromUTF8(timeLabel(position_, duration_)));
    if (FindFocus() != positionSlider_) {
        positionSlider_->SetValue(std::min(static_cast<int>(second), positionSlider_->GetMax()));
    }
}

void PlayerFrame::command(std::initializer_list<std::string> args) {
    if (!mpv_) {
        return;
    }
    std::vector<const char*> argv;
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    mpv_command(mpv_, argv.data());
}

void PlayerFrame::commandAsync(std::initializer_list<std::string> args) {
    if (!mpv_) {
        return;
    }
    std::vector<const char*> argv;
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);
    mpv_command_async(mpv_, 0, argv.data());
}

void PlayerFrame::togglePause() {
    command({"cycle", "pause"});
}

void PlayerFrame::seek(double seconds) {
    const double end = duration_ > 0 ? duration_ : std::numeric_limits<double>::max();
    announce(wxString::FromUTF8(formatClock(std::clamp(position_ + seconds, 0.0, end))));
    command({"seek", std::to_string(seconds), "relative"});
}

void PlayerFrame::changeVolume(double delta) {
    double volume = std::clamp(volume_ + delta, 0.0, 100.0);
    announce(wxString::Format("Volume %d", static_cast<int>(std::lround(volume))));
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &volume);
}

void PlayerFrame::onCharHook(wxKeyEvent& event) {
    const int key = event.GetKeyCode();
    const bool plain = !event.HasAnyModifiers();
    auto* focus = FindFocus();

    if (key == WXK_ESCAPE && plain) {
        Close();
        return;
    }
    if (key == WXK_SPACE && plain && !dynamic_cast<wxButton*>(focus)) {
        togglePause();
        return;
    }
    const bool focusUsesArrows =
        focus == positionSlider_ || focus == volumeSlider_ || focus == timeText_ || focus == subtitleChoice_;
    const bool shifted = event.GetModifiers() == wxMOD_SHIFT;
    if (!focusUsesArrows && (plain || shifted)) {
        const double step = shifted ? 60 : 10;
        switch (key) {
        case WXK_LEFT:
            seek(-step);
            return;
        case WXK_RIGHT:
            seek(step);
            return;
        case WXK_UP:
            if (plain) {
                changeVolume(5);
                return;
            }
            break;
        case WXK_DOWN:
            if (plain) {
                changeVolume(-5);
                return;
            }
            break;
        default:
            break;
        }
    }
    event.Skip();
}

}
