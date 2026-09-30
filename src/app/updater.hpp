#pragma once

#include "update.hpp"

#include <wx/string.h>

#include <functional>
#include <memory>

class wxWindow;

namespace ryu {

class Updater {
public:
    Updater(wxWindow* parent, std::weak_ptr<void> alive, std::function<void(const wxString&)> setStatus);

    void check(bool userAsked);
    static void cleanUp();

private:
    void offer(const Release& release);
    void install(const Release& release);

    wxWindow* parent_;
    std::weak_ptr<void> alive_;
    std::function<void(const wxString&)> setStatus_;
    bool busy_ = false;
};

}
