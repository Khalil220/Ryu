#include "updater.hpp"

#include "background.hpp"
#include "log.hpp"

#include <wx/msgdlg.h>
#include <wx/progdlg.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/window.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <mutex>

namespace ryu {

struct InstallState {
    std::mutex mutex;
    InstallStep step = InstallStep::Downloading;
    std::uint64_t done = 0;
    std::uint64_t total = 0;
    std::atomic<bool> cancelled = false;
};

namespace {

namespace fs = std::filesystem;

constexpr int notesLineLimit = 15;
constexpr int progressRange = 1000;
constexpr int progressInterval = 100;

fs::path appFolder() {
    return fs::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring()).parent_path();
}

std::string feedUrl() {
    wxString feed;
    return wxGetEnv("RYU_UPDATE_FEED", &feed) ? feed.utf8_string() : std::string(releaseFeedUrl);
}

wxString shortNotes(const std::string& notes) {
    wxString text;
    int lines = 0;
    for (const auto& line : wxSplit(wxString::FromUTF8(notes), '\n')) {
        if (++lines > notesLineLimit) {
            text += "And more on the release page.";
            break;
        }
        text += line.Strip(wxString::both) + "\n";
    }
    return text.Trim();
}

}

Updater::Updater(wxWindow* parent, std::weak_ptr<void> alive, std::function<void(const wxString&)> setStatus)
    : parent_(parent), alive_(std::move(alive)), setStatus_(std::move(setStatus)) {
    progressTimer_.Bind(wxEVT_TIMER, [this](wxTimerEvent&) { showProgress(); });
}

Updater::~Updater() {
    if (state_) {
        state_->cancelled = true;
    }
    progressTimer_.Stop();
    delete progress_.get();
}

void Updater::cleanUp() {
    const auto folder = appFolder();
    std::error_code ignored;
    fs::remove_all(folder / stagingFolderName, ignored);
    removeReplacedFiles(folder);
}

void Updater::check(bool userAsked) {
    if (busy_) {
        return;
    }
    busy_ = true;
    if (userAsked) {
        setStatus_("Checking for updates");
    }
    runInBackground<std::optional<Release>>(
        alive_,
        [feed = feedUrl()] {
            CurlHttpClient http;
            return latestRelease(http, feed);
        },
        [this, userAsked](std::optional<Release> release) {
            busy_ = false;
            cleanUp();
            if (release && isNewerVersion(release->version, RYU_VERSION)) {
                offer(*release);
                return;
            }
            if (userAsked) {
                setStatus_("Ready");
                wxMessageBox("You have the latest version of Ryu, " RYU_VERSION ".", "Check for updates",
                             wxOK | wxICON_INFORMATION, parent_);
            }
        },
        [this, userAsked](const std::string& message) {
            busy_ = false;
            logLine("Update check failed: " + message);
            if (userAsked) {
                setStatus_("Ready");
                wxMessageBox("Ryu could not check for updates.\n" + wxString::FromUTF8(message), "Check for updates",
                             wxOK | wxICON_ERROR, parent_);
            }
        });
}

void Updater::offer(const Release& release) {
    const auto version = wxString::FromUTF8(release.version);
    setStatus_("Ryu " + version + " is available");
    if (!canWriteTo(appFolder())) {
        wxMessageDialog dialog(parent_,
                               "Ryu " + version + " is available, but Ryu can't update itself because its folder, " +
                                   wxString(appFolder().wstring()) +
                                   ", needs administrator rights to change. Move Ryu to a folder of your own, or "
                                   "download the update by hand.\nOpen the release page?",
                               "Update available", wxYES_NO | wxYES_DEFAULT | wxICON_INFORMATION);
        if (dialog.ShowModal() == wxID_YES && !release.pageUrl.empty()) {
            wxLaunchDefaultBrowser(wxString::FromUTF8(release.pageUrl));
        }
        setStatus_("Ready");
        return;
    }
    wxMessageDialog dialog(parent_,
                           "Ryu " + version + " is available. You have " RYU_VERSION ".\nDownload and install it now?",
                           "Update available", wxYES_NO | wxYES_DEFAULT | wxICON_INFORMATION);
    if (!release.notes.empty()) {
        dialog.SetExtendedMessage(shortNotes(release.notes));
    }
    if (dialog.ShowModal() == wxID_YES) {
        install(release);
    } else {
        setStatus_("Ready");
    }
}

void Updater::install(const Release& release) {
    busy_ = true;
    version_ = wxString::FromUTF8(release.version);
    setStatus_("Downloading Ryu " + version_);
    logLine("Installing Ryu " + release.version + " from " + release.packageUrl);
    state_ = std::make_shared<InstallState>();
    shownStep_ = InstallStep::Downloading;
    progress_ = new wxProgressDialog("Updating Ryu", "Downloading Ryu " + version_, progressRange, parent_,
                                     wxPD_CAN_ABORT | wxPD_APP_MODAL);
    progressTimer_.Start(progressInterval);
    runInBackground<std::optional<fs::path>>(
        alive_,
        [release, folder = appFolder(), state = state_]() -> std::optional<fs::path> {
            CurlHttpClient http(CurlHttpClient::defaultUserAgent, updateDownloadTimeout);
            try {
                return installRelease(http, release, folder,
                                      [state](InstallStep step, std::uint64_t done, std::uint64_t total) {
                                          std::scoped_lock lock(state->mutex);
                                          state->step = step;
                                          state->done = done;
                                          state->total = total;
                                          return !state->cancelled;
                                      });
            } catch (const UpdateCancelled&) {
                return std::nullopt;
            }
        },
        [this](std::optional<fs::path> program) {
            busy_ = false;
            closeProgress();
            if (!program) {
                logLine("The update was cancelled");
                setStatus_("Update cancelled");
                return;
            }
            logLine("Installed the update to " + program->string());
            setStatus_("Ryu " + version_ + " is installed");
            wxMessageDialog dialog(parent_, "Ryu " + version_ + " is installed. Restart Ryu now to use it?",
                                   "Update installed", wxYES_NO | wxYES_DEFAULT | wxICON_INFORMATION);
            if (dialog.ShowModal() != wxID_YES) {
                return;
            }
            const wxString path = program->wstring();
            const wchar_t* command[] = {path.wc_str(), nullptr};
            if (wxExecute(command, wxEXEC_ASYNC) == 0) {
                wxMessageBox("Ryu could not restart itself. Start it again to use the new version.", "Update installed",
                             wxOK | wxICON_WARNING, parent_);
                return;
            }
            parent_->Close();
        },
        [this, page = release.pageUrl](const std::string& message) {
            busy_ = false;
            closeProgress();
            logLine("Update failed: " + message);
            setStatus_("Update failed");
            wxMessageDialog dialog(parent_,
                                   "Ryu could not install the update.\n" + wxString::FromUTF8(message) +
                                       "\nOpen the release page to download it yourself?",
                                   "Update failed", wxYES_NO | wxICON_ERROR);
            if (dialog.ShowModal() == wxID_YES && !page.empty()) {
                wxLaunchDefaultBrowser(wxString::FromUTF8(page));
            }
        });
}

void Updater::showProgress() {
    if (!progress_ || !state_) {
        return;
    }
    InstallStep step;
    std::uint64_t done = 0;
    std::uint64_t total = 0;
    {
        std::scoped_lock lock(state_->mutex);
        step = state_->step;
        done = state_->done;
        total = state_->total;
    }
    const int value =
        total ? static_cast<int>(std::min<std::uint64_t>(done * progressRange / total, progressRange - 1)) : 0;
    const bool carryOn =
        step == shownStep_
            ? progress_->Update(value)
            : progress_->Update(value, step == InstallStep::Downloading ? "Downloading Ryu " + version_
                                                                        : wxString("Extracting package"));
    shownStep_ = step;
    if (!carryOn) {
        state_->cancelled = true;
    }
}

void Updater::closeProgress() {
    progressTimer_.Stop();
    delete progress_.get();
    state_.reset();
}

}
