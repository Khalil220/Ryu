#include "updater.hpp"

#include "background.hpp"
#include "log.hpp"
#include "speech.hpp"

#include <wx/msgdlg.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/window.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace ryu {

namespace {

namespace fs = std::filesystem;

constexpr const char* stagingName = ".ryu-update";
constexpr int notesLineLimit = 15;

fs::path appFolder() {
    return fs::path(wxStandardPaths::Get().GetExecutablePath().ToStdWstring()).parent_path();
}

std::string feedUrl() {
    wxString feed;
    return wxGetEnv("RYU_UPDATE_FEED", &feed) ? feed.utf8_string() : std::string(releaseFeedUrl);
}

std::string download(HttpClient& http, const std::string& url) {
    auto response = http.get(url);
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error("The download returned HTTP " + std::to_string(response.status));
    }
    return std::move(response.body);
}

fs::path installPackage(const Release& release) {
    if (release.packageUrl.empty() || release.checksumsUrl.empty()) {
        throw std::runtime_error("This release has no Windows package with a checksum.");
    }
    CurlHttpClient http(CurlHttpClient::defaultUserAgent, updateDownloadTimeout);
    const auto expected = checksumFor(download(http, release.checksumsUrl), release.packageName);
    if (!expected) {
        throw std::runtime_error("The release's checksums don't list its package.");
    }
    const auto package = download(http, release.packageUrl);
    if (sha256Hex(package) != *expected) {
        throw std::runtime_error("The downloaded update doesn't match its checksum.");
    }

    const auto saved = fs::temp_directory_path() / release.packageName;
    {
        std::ofstream out(saved, std::ios::binary);
        out.write(package.data(), static_cast<std::streamsize>(package.size()));
        if (!out) {
            throw std::runtime_error("Could not save the update to " + saved.string());
        }
    }
    const auto folder = appFolder();
    const auto staging = folder / stagingName;
    fs::remove_all(staging);
    extractPackage(saved, staging);
    fs::remove(saved);
    if (!fs::exists(staging / "ryu.exe")) {
        fs::remove_all(staging);
        throw std::runtime_error("The update package has no ryu.exe.");
    }
    replaceFiles(staging, folder);
    fs::remove_all(staging);
    return folder / "ryu.exe";
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
    : parent_(parent), alive_(std::move(alive)), setStatus_(std::move(setStatus)) {}

void Updater::cleanUp() {
    const auto folder = appFolder();
    std::error_code ignored;
    fs::remove_all(folder / stagingName, ignored);
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
    const auto version = wxString::FromUTF8(release.version);
    setStatus_("Downloading Ryu " + version);
    announce("Downloading the update");
    logLine("Installing Ryu " + release.version + " from " + release.packageUrl);
    runInBackground<fs::path>(
        alive_, [release] { return installPackage(release); },
        [this, version](fs::path program) {
            busy_ = false;
            logLine("Installed the update to " + program.string());
            setStatus_("Ryu " + version + " is installed");
            wxMessageDialog dialog(parent_, "Ryu " + version + " is installed. Restart Ryu now to use it?",
                                   "Update installed", wxYES_NO | wxYES_DEFAULT | wxICON_INFORMATION);
            if (dialog.ShowModal() != wxID_YES) {
                return;
            }
            const wxString path = program.wstring();
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

}
