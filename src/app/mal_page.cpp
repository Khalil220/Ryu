#include "mal_page.hpp"

#include "accessibility.hpp"
#include "format.hpp"
#include "text_list.hpp"

#include <wx/button.h>
#include <wx/choice.h>
#include <wx/menu.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include <algorithm>

namespace ryu {

MalPage::MalPage(wxWindow* parent, Actions actions) : wxPanel(parent), actions_(std::move(actions)) {
    const int gap = FromDIP(12);
    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto* listRow = new wxBoxSizer(wxHORIZONTAL);
    listRow->Add(new wxStaticText(this, wxID_ANY, "&List:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(8));
    listChoice_ = new wxChoice(this, wxID_ANY);
    for (const auto status : malLists) {
        listChoice_->Append(wxString::FromUTF8(malStatusLabel(status)));
    }
    listChoice_->SetSelection(0);
    listRow->Add(listChoice_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(listRow, 0, wxALL, gap);

    sizer->Add(new wxStaticText(this, wxID_ANY, "&Anime:"), 0, wxLEFT | wxRIGHT, gap);
    list_ = new TextList(this);
    list_->SetMinSize(FromDIP(wxSize(-1, 160)));
    sizer->Add(list_, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(4));

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    openButton_ = new wxButton(this, wxID_ANY, "&Open");
    editButton_ = new wxButton(this, wxID_ANY, "&Edit");
    removeButton_ = new wxButton(this, wxID_ANY, "&Remove");
    auto* close = new wxButton(this, wxID_ANY, "&Close");
    for (auto* button : {openButton_, editButton_, removeButton_}) {
        buttons->Add(button, 0, wxRIGHT, FromDIP(8));
    }
    buttons->AddStretchSpacer();
    buttons->Add(close, 0);
    sizer->Add(buttons, 0, wxEXPAND | wxALL, gap);
    SetSizer(sizer);

    setAccessibleName(listChoice_, "List");
    setAccessibleName(list_, "Anime");
    setAccessibleDescription(openButton_, "Find its episodes with the current provider");
    openButton_->SetToolTip("Find its episodes with the current provider (Enter)");
    editButton_->SetToolTip("Edit it on MyAnimeList (Ctrl+M)");
    removeButton_->SetToolTip("Remove it from your lists (Delete)");
    close->SetToolTip("Back to searching (Escape)");

    const int openId = wxWindow::NewControlId();
    const int editId = wxWindow::NewControlId();
    const int removeId = wxWindow::NewControlId();
    const int refreshId = wxWindow::NewControlId();
    listChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { showList(); });
    list_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { act(actions_.open); });
    list_->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_DELETE && !event.HasAnyModifiers()) {
            act(actions_.remove);
        } else if (event.GetKeyCode() == 'M' && event.GetModifiers() == wxMOD_CONTROL) {
            act(actions_.edit);
        } else {
            event.Skip();
        }
    });
    list_->Bind(wxEVT_CONTEXT_MENU, [this, openId, editId, removeId, refreshId](wxContextMenuEvent& event) {
        wxMenu menu;
        if (selected()) {
            menu.Append(openId, "&Open\tEnter");
            menu.Append(editId, "&Edit on MyAnimeList...\tCtrl+M");
            menu.Append(removeId, "&Remove from my list\tDelete");
            menu.AppendSeparator();
        }
        menu.Append(refreshId, "Re&fresh\tF5");
        auto position = event.GetPosition();
        if (position == wxDefaultPosition) {
            wxRect item;
            const long row = list_->GetFirstSelected();
            if (row >= 0) {
                list_->GetItemRect(row, item);
            }
            position = list_->ClientToScreen(item.GetBottomLeft());
        }
        list_->PopupMenu(&menu, list_->ScreenToClient(position));
    });
    list_->Bind(wxEVT_MENU, [this](wxCommandEvent&) { act(actions_.open); }, openId);
    list_->Bind(wxEVT_MENU, [this](wxCommandEvent&) { act(actions_.edit); }, editId);
    list_->Bind(wxEVT_MENU, [this](wxCommandEvent&) { act(actions_.remove); }, removeId);
    list_->Bind(wxEVT_MENU, [this](wxCommandEvent&) { actions_.refresh(); }, refreshId);
    openButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { act(actions_.open); });
    editButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { act(actions_.edit); });
    removeButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { act(actions_.remove); });
    close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { actions_.leave(); });
    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& event) {
        if (event.GetKeyCode() == WXK_ESCAPE && !event.HasAnyModifiers()) {
            actions_.leave();
        } else if (event.GetKeyCode() == WXK_F5 && !event.HasAnyModifiers()) {
            actions_.refresh();
        } else {
            event.Skip();
        }
    });
}

void MalPage::showEntries(const std::vector<MalAnime>& entries, bool loaded) {
    entries_ = entries;
    loaded_ = loaded;
    showList();
}

void MalPage::focusList() {
    list_->SetFocus();
}

void MalPage::showList() {
    const auto* previous = selected();
    const int keptId = previous ? previous->id : 0;
    const auto status = malLists[listChoice_->GetSelection()];
    shown_.clear();
    std::ranges::copy_if(entries_, std::back_inserter(shown_),
                         [status](const MalAnime& anime) { return anime.list.status == status; });
    std::ranges::sort(shown_, [](const MalAnime& left, const MalAnime& right) {
        return wxString::FromUTF8(malTitle(left)).CmpNoCase(wxString::FromUTF8(malTitle(right))) < 0;
    });
    std::vector<wxString> labels;
    for (const auto& anime : shown_) {
        labels.push_back(wxString::FromUTF8(malEntryLabel(anime)));
    }
    const auto name = wxString::FromUTF8(malStatusLabel(status));
    if (labels.empty()) {
        labels.push_back(loaded_ ? "Nothing on your " + name + " list" : wxString("Loading your lists"));
    }
    if (labels != labels_) {
        labels_ = labels;
        list_->setItems(std::move(labels));
        const auto kept = std::ranges::find(shown_, keptId, &MalAnime::id);
        list_->selectItem(kept == shown_.end() ? 0 : static_cast<long>(kept - shown_.begin()));
    }
    for (auto* button : {openButton_, editButton_, removeButton_}) {
        button->Show(!shown_.empty());
    }
    Layout();
    if (loaded_) {
        actions_.status(wxString::Format("%zu anime on your %s list", shown_.size(), name));
    }
}

const MalAnime* MalPage::selected() const {
    const auto row = list_->selectedIndex();
    return row < shown_.size() ? &shown_[row] : nullptr;
}

void MalPage::act(const std::function<void(const MalAnime&)>& action) {
    if (const auto* anime = selected()) {
        action(MalAnime(*anime));
    }
}

}
