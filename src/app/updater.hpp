#pragma once

#include "update.hpp"

#include <wx/string.h>
#include <wx/timer.h>
#include <wx/weakref.h>

#include <functional>
#include <memory>

class wxProgressDialog;
class wxWindow;

namespace ryu {

struct InstallState;

class Updater {
public:
    Updater(wxWindow* parent, std::weak_ptr<void> alive, std::function<void(const wxString&)> setStatus);
    ~Updater();

    void check(bool userAsked);
    static void cleanUp();

private:
    void offer(const Release& release);
    void install(const Release& release);
    void showProgress();
    void closeProgress();

    wxWindow* parent_;
    std::weak_ptr<void> alive_;
    std::function<void(const wxString&)> setStatus_;
    bool busy_ = false;
    wxString version_;
    std::shared_ptr<InstallState> state_;
    wxWeakRef<wxProgressDialog> progress_;
    wxTimer progressTimer_;
    InstallStep shownStep_ = InstallStep::Downloading;
};

}
