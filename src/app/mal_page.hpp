#pragma once

#include "mal.hpp"

#include <wx/panel.h>

#include <functional>
#include <vector>

class wxButton;
class wxChoice;

namespace ryu {

class TextList;

class MalPage : public wxPanel {
public:
    struct Actions {
        std::function<void()> leave;
        std::function<void()> refresh;
        std::function<void(const MalAnime&)> open;
        std::function<void(const MalAnime&)> edit;
        std::function<void(const MalAnime&)> remove;
        std::function<void(const wxString&)> status;
    };

    MalPage(wxWindow* parent, Actions actions);

    void showEntries(const std::vector<MalAnime>& entries, bool loaded);
    void focusList();

private:
    void showList();
    const MalAnime* selected() const;
    void act(const std::function<void(const MalAnime&)>& action);

    Actions actions_;
    std::vector<MalAnime> entries_;
    std::vector<MalAnime> shown_;
    std::vector<wxString> labels_;
    bool loaded_ = false;
    wxChoice* listChoice_ = nullptr;
    TextList* list_ = nullptr;
    wxButton* openButton_ = nullptr;
    wxButton* editButton_ = nullptr;
    wxButton* removeButton_ = nullptr;
};

}
