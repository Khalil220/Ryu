#include "player_panel.hpp"

#include "accessibility.hpp"
#include "format.hpp"
#include "log.hpp"
#include "http.hpp"
#include "speech.hpp"

#include <mpv/client.h>

#include <wx/accel.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/font.h>
#include <wx/fontenum.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/tokenzr.h>
#include <wx/toplevel.h>
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

constexpr wchar_t playGlyph = 0xE768;
constexpr wchar_t pauseGlyph = 0xE769;
constexpr wchar_t previousGlyph = 0xE892;
constexpr wchar_t nextGlyph = 0xE893;
constexpr wchar_t rewindGlyph = 0xEB9E;
constexpr wchar_t forwardGlyph = 0xEB9D;
constexpr wchar_t fullScreenGlyph = 0xE740;
constexpr wchar_t windowGlyph = 0xE73F;
constexpr wchar_t closeGlyph = 0xE711;
constexpr wchar_t volumeGlyph = 0xE767;
constexpr wchar_t captionsGlyph = 0xE7F0;

wxFont iconFont(double points) {
    static const wxString face = wxFontEnumerator::IsValidFacename("Segoe Fluent Icons") ? "Segoe Fluent Icons"
                                                                                         : "Segoe MDL2 Assets";
    return wxFont(wxFontInfo(points).FaceName(face));
}

wxButton* iconButton(wxWindow* parent, wchar_t glyph, const wxString& name, const wxString& shortcut,
                     const wxString& tip, double points = 13) {
    auto* button = new wxButton(parent, wxID_ANY, wxString(glyph), wxDefaultPosition, parent->FromDIP(wxSize(46, 38)));
    button->SetFont(iconFont(points));
    button->SetToolTip(tip);
    setAccessibleName(button, name);
    setAccessibleShortcut(button, shortcut);
    return button;
}

wxStaticText* iconLabel(wxWindow* parent, wchar_t glyph) {
    auto* label = new wxStaticText(parent, wxID_ANY, wxString(glyph));
    label->SetFont(iconFont(13));
    return label;
}

enum PropertyId : uint64_t { TimePos = 1, Duration, Pause, Volume, TrackList, EndReached, SubText };

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

std::string endReason(const mpv_event_end_file& end) {
    switch (end.reason) {
    case MPV_END_FILE_REASON_EOF:
        return "it reached the end";
    case MPV_END_FILE_REASON_STOP:
        return "it was stopped or replaced";
    case MPV_END_FILE_REASON_QUIT:
        return "the player shut down";
    case MPV_END_FILE_REASON_ERROR:
        return std::string("of an error: ") + mpv_error_string(end.error);
    case MPV_END_FILE_REASON_REDIRECT:
        return "it redirected to another file";
    default:
        return "of reason " + std::to_string(end.reason);
    }
}

}

PlayerPanel::PlayerPanel(wxWindow* parent, std::function<void()> onLeave, std::function<void(int)> onStep,
                         std::function<void(bool)> onReadSubtitlesChanged,
                         std::function<void(const SubtitlePreference&)> onSubtitlesChosen)
    : wxPanel(parent), onLeave_(std::move(onLeave)), onStep_(std::move(onStep)),
      onReadSubtitlesChanged_(std::move(onReadSubtitlesChanged)), onSubtitlesChosen_(std::move(onSubtitlesChosen)) {
    createControls();
    Bind(wxEVT_CHAR_HOOK, &PlayerPanel::onCharHook, this);
    stallTimer_.SetOwner(this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&) { checkForStall(); });

    const int speakTimeId = wxWindow::NewControlId();
    const int nextId = wxWindow::NewControlId();
    const int previousId = wxWindow::NewControlId();
    const int skipIntroId = wxWindow::NewControlId();
    const int readId = wxWindow::NewControlId();
    const int pauseId = wxWindow::NewControlId();
    const int backId = wxWindow::NewControlId();
    const int forwardId = wxWindow::NewControlId();
    const int closeId = wxWindow::NewControlId();
    const int fullScreenId = wxWindow::NewControlId();
    const int positionId = wxWindow::NewControlId();
    const int timeId = wxWindow::NewControlId();
    const int volumeId = wxWindow::NewControlId();
    const int subtitlesId = wxWindow::NewControlId();
    wxAcceleratorEntry keys[] = {
        {wxACCEL_NORMAL, 'T', speakTimeId},
        {wxACCEL_NORMAL, 'N', nextId},
        {wxACCEL_NORMAL, 'P', previousId},
        {wxACCEL_NORMAL, 'I', skipIntroId},
        {wxACCEL_NORMAL, 'R', readId},
        {wxACCEL_ALT, 'P', pauseId},
        {wxACCEL_ALT, 'B', backId},
        {wxACCEL_ALT, 'F', forwardId},
        {wxACCEL_ALT, 'R', previousId},
        {wxACCEL_ALT, 'N', nextId},
        {wxACCEL_ALT, 'C', closeId},
        {wxACCEL_ALT, 'T', positionId},
        {wxACCEL_ALT, 'M', timeId},
        {wxACCEL_ALT, 'V', volumeId},
        {wxACCEL_ALT, 'S', subtitlesId},
        {wxACCEL_NORMAL, WXK_F11, fullScreenId},
    };
    SetAcceleratorTable(wxAcceleratorTable(static_cast<int>(std::size(keys)), keys));
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { announce(wxString::FromUTF8(timeLabel(position_, duration_))); },
         speakTimeId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onStep_(1); }, nextId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onStep_(-1); }, previousId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { skipIntro(); }, skipIntroId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { toggleReadSubtitles(); }, readId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { togglePause(); }, pauseId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { seek(-10); }, backId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { seek(10); }, forwardId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { onLeave_(); }, closeId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { setFullScreen(!isFullScreen()); }, fullScreenId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { positionSlider_->SetFocus(); }, positionId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { timeText_->SetFocus(); }, timeId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { volumeSlider_->SetFocus(); }, volumeId);
    Bind(wxEVT_MENU, [this](wxCommandEvent&) { subtitleChoice_->SetFocus(); }, subtitlesId);
}

PlayerPanel::~PlayerPanel() {
    stallTimer_.Stop();
    stopMpv();
}

void PlayerPanel::createControls() {
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    video_ = new VideoSurface(this);
    video_->SetBackgroundColour(*wxBLACK);
    video_->SetMinSize(FromDIP(wxSize(320, 180)));
    sizer->Add(video_, 1, wxEXPAND);
    video_->Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent&) { setFullScreen(!isFullScreen()); });

    auto* seekRow = new wxBoxSizer(wxHORIZONTAL);
    positionSlider_ = new wxSlider(this, wxID_ANY, 0, 0, 1);
    positionSlider_->SetLineSize(10);
    positionSlider_->SetPageSize(60);
    seekRow->Add(positionSlider_, 1, wxALIGN_CENTER_VERTICAL);
    timeText_ = new wxTextCtrl(this, wxID_ANY, "Stopped", wxDefaultPosition, FromDIP(wxSize(130, -1)),
                               wxTE_READONLY | wxTE_RIGHT | wxBORDER_NONE);
    timeText_->SetBackgroundColour(GetBackgroundColour());
    seekRow->Add(timeText_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(8));
    sizer->Add(seekRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));

    auto* controls = new wxBoxSizer(wxHORIZONTAL);
    auto* previous = iconButton(this, previousGlyph, "Previous episode", "Alt+R", "Previous episode (P)");
    auto* back = iconButton(this, rewindGlyph, "Back 10 seconds", "Alt+B", "Back 10 seconds (Left)");
    pauseButton_ = iconButton(this, pauseGlyph, "Pause", "Alt+P", "Play or pause (Space)", 16);
    auto* forward = iconButton(this, forwardGlyph, "Forward 10 seconds", "Alt+F", "Forward 10 seconds (Right)");
    auto* next = iconButton(this, nextGlyph, "Next episode", "Alt+N", "Next episode (N)");
    for (auto* button : {previous, back, pauseButton_, forward, next}) {
        controls->Add(button, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    }
    skipIntroButton_ = new wxButton(this, wxID_ANY, "Skip &intro");
    skipIntroButton_->SetToolTip("Skip the intro (I)");
    controls->Add(skipIntroButton_, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));
    controls->AddStretchSpacer();

    controls->Add(iconLabel(this, volumeGlyph), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    volumeSlider_ = new wxSlider(this, wxID_ANY, 100, 0, 100, wxDefaultPosition, FromDIP(wxSize(100, -1)));
    volumeSlider_->SetLineSize(5);
    volumeSlider_->SetPageSize(20);
    controls->Add(volumeSlider_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    controls->Add(iconLabel(this, captionsGlyph), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
    subtitleChoice_ = new wxChoice(this, wxID_ANY);
    subtitleChoice_->SetMinSize(FromDIP(wxSize(150, -1)));
    subtitleChoice_->Append("Off");
    subtitleChoice_->SetSelection(0);
    controls->Add(subtitleChoice_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    readCheck_ = new wxCheckBox(this, wxID_ANY, "Read alou&d");
    readCheck_->SetToolTip("Read subtitles aloud (R)");
    controls->Add(readCheck_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
    fullScreenButton_ = iconButton(this, fullScreenGlyph, "Full screen", "F11", "Full screen (F11)");
    auto* close = iconButton(this, closeGlyph, "Close", "Alt+C", "Close the player (Escape)");
    controls->Add(fullScreenButton_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(4));
    controls->Add(close, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(controls, 0, wxEXPAND | wxALL, FromDIP(10));

    SetSizer(sizer);
    setAccessibleName(positionSlider_, "Position");
    setAccessibleShortcut(positionSlider_, "Alt+T");
    setAccessibleName(timeText_, "Time");
    setAccessibleShortcut(timeText_, "Alt+M");
    setAccessibleName(volumeSlider_, "Volume");
    setAccessibleShortcut(volumeSlider_, "Alt+V");
    setAccessibleName(subtitleChoice_, "Subtitles");
    setAccessibleShortcut(subtitleChoice_, "Alt+S");
    setAccessibleName(readCheck_, "Read subtitles aloud");

    pauseButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { togglePause(); });
    back->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { seek(-10); });
    forward->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { seek(10); });
    skipIntroButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { skipIntro(); });
    readCheck_->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        readSubtitles_ = readCheck_->GetValue();
        onReadSubtitlesChanged_(readSubtitles_);
    });
    previous->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { onStep_(-1); });
    next->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { onStep_(1); });
    fullScreenButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { setFullScreen(!isFullScreen()); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { onLeave_(); });
    positionSlider_->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        command({"seek", std::to_string(positionSlider_->GetValue()), "absolute"});
    });
    volumeSlider_->Bind(wxEVT_SLIDER, [this](wxCommandEvent&) {
        if (mpv_) {
            double volume = volumeSlider_->GetValue();
            mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &volume);
            showVolume(volume);
        }
    });
    subtitleChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        if (!mpv_) {
            return;
        }
        const int index = subtitleChoice_->GetSelection();
        if (index <= 0 || static_cast<size_t>(index) > subtitleTracks_.size()) {
            mpv_set_property_string(mpv_, "sid", "no");
            subtitlePreference_ = {SubtitlePreference::Kind::Off, "", ""};
        } else {
            int64_t track = subtitleTracks_[index - 1];
            mpv_set_property(mpv_, "sid", MPV_FORMAT_INT64, &track);
            subtitlePreference_ = {SubtitlePreference::Kind::Track, subtitleChoice_->GetString(index).utf8_string(),
                                   subtitleLanguages_[index - 1]};
        }
        onSubtitlesChosen_(subtitlePreference_);
    });
}

void PlayerPanel::startMpv() {
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
    mpv_set_option_string(mpv_, "cache-secs", "60");
    mpv_set_option_string(mpv_, "osc", "no");
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
    mpv_observe_property(mpv_, SubText, "sub-text", MPV_FORMAT_STRING);
    mpv_request_log_messages(mpv_, "info");
    mpv_set_wakeup_callback(
        mpv_, [](void* panel) { static_cast<PlayerPanel*>(panel)->CallAfter(&PlayerPanel::processEvents); }, this);
}

void PlayerPanel::stopMpv() {
    if (!mpv_) {
        return;
    }
    mpv_set_wakeup_callback(mpv_, nullptr, nullptr);
    mpv_terminate_destroy(mpv_);
    mpv_ = nullptr;
}

void PlayerPanel::play(const Stream& stream, bool readSubtitles, const SubtitlePreference& subtitles) {
    if (!mpv_) {
        startMpv();
    }

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
    mpv_set_property_string(mpv_, "alang", stream.audioLanguage.c_str());
    mpv_set_property_string(mpv_, "pause", "no");

    resetState("Loading");
    pendingSubtitles_ = stream.subtitles;
    subtitlePreference_ = subtitles;
    subtitleNoise_ = stream.subtitleNoise;
    intro_ = stream.intro;
    readSubtitles_ = readSubtitles;
    readCheck_->SetValue(readSubtitles);
    updateIntroButton();
    active_ = true;
    logLine("Loading " + stream.server + " stream " + stream.url);
    command({"loadfile", stream.url, "replace"});
    stallTimer_.Start(1000);
}

void PlayerPanel::stop() {
    if (!active_) {
        return;
    }
    active_ = false;
    stallTimer_.Stop();
    pendingSubtitles_.clear();
    command({"stop"});
    resetState("Stopped");
    setFullScreen(false);
}

void PlayerPanel::focusControls() {
    pauseButton_->SetFocus();
}

void PlayerPanel::resetState(const wxString& timeText) {
    subtitleTracks_.clear();
    subtitleLanguages_.clear();
    subtitleChoice_->Clear();
    subtitleChoice_->Append("Off");
    subtitleChoice_->SetSelection(0);
    lastError_.clear();
    position_ = 0;
    duration_ = 0;
    intro_.reset();
    updateIntroButton();
    shownSecond_ = -1;
    paused_ = -1;
    ended_ = false;
    introAnnounced_ = false;
    lastSubtitle_.clear();
    stallWatch_.reset();
    positionSlider_->SetValue(0);
    showPaused(false);
    timeText_->ChangeValue(timeText);
}

void PlayerPanel::processEvents() {
    while (mpv_) {
        const mpv_event* event = mpv_wait_event(mpv_, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            return;
        }
        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE:
            if (active_) {
                onPropertyChange(event->reply_userdata, *static_cast<const mpv_event_property*>(event->data));
            }
            break;
        case MPV_EVENT_FILE_LOADED:
            if (const auto chosen = pickSubtitle(pendingSubtitles_, subtitlePreference_)) {
                for (size_t i = 0; i < pendingSubtitles_.size(); ++i) {
                    const auto& subtitle = pendingSubtitles_[i];
                    commandAsync({"sub-add", subtitle.url, i == *chosen ? "select" : "auto", subtitle.label,
                                  subtitle.language});
                }
            } else {
                for (const auto& subtitle : pendingSubtitles_) {
                    commandAsync({"sub-add", subtitle.url, "auto", subtitle.label, subtitle.language});
                }
                commandAsync({"set", "sid", "no"});
            }
            pendingSubtitles_.clear();
            logLine("mpv loaded the file");
            break;
        case MPV_EVENT_LOG_MESSAGE: {
            const auto* message = static_cast<const mpv_event_log_message*>(event->data);
            const std::string prefix = message->prefix;
            const auto line = wxString::FromUTF8(message->text).Trim().utf8_string();
            const bool listing = prefix == "cplayer" && message->text[0] != ' ' && line != "Track added:";
            if (message->log_level <= MPV_LOG_LEVEL_WARN || listing) {
                logLine("mpv " + prefix + ": " + line);
            }
            if (message->log_level <= MPV_LOG_LEVEL_ERROR) {
                lastError_ = line;
            }
            break;
        }
        case MPV_EVENT_END_FILE: {
            const auto* end = static_cast<const mpv_event_end_file*>(event->data);
            logLine("mpv stopped the file because " + endReason(*end));
            if (active_ && end->reason == MPV_END_FILE_REASON_ERROR) {
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

void PlayerPanel::onPropertyChange(uint64_t id, const mpv_event_property& property) {
    const bool isDouble = property.format == MPV_FORMAT_DOUBLE;
    const bool isFlag = property.format == MPV_FORMAT_FLAG;
    switch (id) {
    case TimePos:
        position_ = isDouble ? *static_cast<double*>(property.data) : 0;
        updateTime();
        announceIntro();
        updateIntroButton();
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
            showPaused(paused);
            if (paused_ != -1 && paused != paused_ && FindFocus() != pauseButton_) {
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
    case SubText:
        speakSubtitle(property.format == MPV_FORMAT_STRING ? *static_cast<char**>(property.data) : "");
        break;
    case EndReached:
        ended_ = isFlag && *static_cast<int*>(property.data);
        if (ended_) {
            announce("End of episode");
        }
        break;
    default:
        break;
    }
}

void PlayerPanel::refreshSubtitles(const mpv_node& tracks) {
    if (tracks.format != MPV_FORMAT_NODE_ARRAY) {
        return;
    }
    std::vector<int64_t> ids;
    std::vector<std::string> languages;
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
        languages.push_back(text(field(track, "lang")));
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
    subtitleLanguages_ = std::move(languages);
    subtitleChoice_->Clear();
    subtitleChoice_->Append("Off");
    subtitleChoice_->Append(labels);
    subtitleChoice_->SetSelection(selected);
}

void PlayerPanel::speakSubtitle(const char* raw) {
    const auto text = speakableSubtitle(raw ? raw : "", subtitleNoise_);
    if (text.empty()) {
        lastSubtitle_.clear();
        return;
    }
    if (text == lastSubtitle_) {
        return;
    }
    lastSubtitle_ = text;
    if (readSubtitles_) {
        announce(wxString::FromUTF8(text), false);
    }
}

void PlayerPanel::announceIntro() {
    if (introAnnounced_ || !inIntro()) {
        return;
    }
    introAnnounced_ = true;
    announce("Intro", false);
}

bool PlayerPanel::inIntro() const {
    return intro_ && position_ >= intro_->start && position_ < intro_->end;
}

void PlayerPanel::updateIntroButton() {
    const bool show = inIntro();
    if (skipIntroButton_->IsShown() == show) {
        return;
    }
    if (!show && FindFocus() == skipIntroButton_) {
        pauseButton_->SetFocus();
    }
    skipIntroButton_->Show(show);
    Layout();
}

void PlayerPanel::skipIntro() {
    if (!active_ || !inIntro()) {
        return;
    }
    introAnnounced_ = true;
    announce("Skipped intro");
    command({"seek", std::to_string(intro_->end), "absolute"});
}

void PlayerPanel::toggleReadSubtitles() {
    readSubtitles_ = !readSubtitles_;
    readCheck_->SetValue(readSubtitles_);
    announce(readSubtitles_ ? "Reading subtitles" : "Not reading subtitles");
    onReadSubtitlesChanged_(readSubtitles_);
}

void PlayerPanel::checkForStall() {
    if (stallWatch_.tick(position_, active_ && !ended_ && paused_ != 1)) {
        logLine("Playback stalled at " + formatClock(position_));
        timeText_->ChangeValue("Not loading");
        announce("The video is not loading. Its host may be down.");
    }
}

void PlayerPanel::updateTime() {
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

void PlayerPanel::command(std::initializer_list<std::string> args) {
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

void PlayerPanel::commandAsync(std::initializer_list<std::string> args) {
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

void PlayerPanel::togglePause() {
    command({"cycle", "pause"});
}

void PlayerPanel::seek(double seconds) {
    const double end = duration_ > 0 ? duration_ : std::numeric_limits<double>::max();
    announce(wxString::FromUTF8(formatClock(std::clamp(position_ + seconds, 0.0, end))));
    command({"seek", std::to_string(seconds), "relative"});
}

void PlayerPanel::changeVolume(double delta) {
    if (!mpv_) {
        return;
    }
    double volume = std::clamp(volume_ + delta, 0.0, 100.0);
    announce(wxString::Format("Volume %d", static_cast<int>(std::lround(volume))));
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &volume);
    showVolume(volume);
}

void PlayerPanel::showVolume(double volume) {
    commandAsync({"show-text", "Volume " + std::to_string(std::lround(volume)) + "%"});
}

void PlayerPanel::showPaused(bool paused) {
    setAccessibleName(pauseButton_, paused ? "Play" : "Pause");
    pauseButton_->SetLabel(wxString(paused ? playGlyph : pauseGlyph));
}

bool PlayerPanel::isFullScreen() {
    const auto* frame = wxDynamicCast(wxGetTopLevelParent(this), wxTopLevelWindow);
    return frame && frame->IsFullScreen();
}

void PlayerPanel::setFullScreen(bool fullScreen) {
    auto* frame = wxDynamicCast(wxGetTopLevelParent(this), wxTopLevelWindow);
    if (!frame || frame->IsFullScreen() == fullScreen) {
        return;
    }
    frame->ShowFullScreen(fullScreen, wxFULLSCREEN_ALL);
    setAccessibleName(fullScreenButton_, fullScreen ? "Exit full screen" : "Full screen");
    fullScreenButton_->SetLabel(wxString(fullScreen ? windowGlyph : fullScreenGlyph));
    fullScreenButton_->SetToolTip(fullScreen ? "Exit full screen (F11 or Escape)" : "Full screen (F11)");
}

void PlayerPanel::onCharHook(wxKeyEvent& event) {
    const int key = event.GetKeyCode();
    const bool plain = !event.HasAnyModifiers();
    auto* focus = FindFocus();

    if (key == WXK_ESCAPE && plain) {
        if (isFullScreen()) {
            setFullScreen(false);
        } else {
            onLeave_();
        }
        return;
    }
    if (key == WXK_SPACE && plain && !dynamic_cast<wxButton*>(focus) && !dynamic_cast<wxCheckBox*>(focus)) {
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
