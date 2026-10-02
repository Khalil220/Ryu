#include "shortcuts_dialog.hpp"

#include "accessibility.hpp"

#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>

#include <array>
#include <utility>

namespace ryu {

namespace {

constexpr std::array<std::pair<const char*, const char*>, 15> shortcuts{{
    {"Ctrl+D in Results", "Speak the synopsis"},
    {"Ctrl+M in Results", "Add to, edit on or remove from MyAnimeList, when logged in"},
    {"Ctrl+L", "My anime lists, when logged in to MyAnimeList"},
    {"Ctrl+M, Delete, F5 in my lists", "Edit, remove, refresh"},
    {"Ctrl+P", "Preferences"},
    {"Space", "Play or pause"},
    {"Left, Right", "Seek 10 seconds"},
    {"Shift+Left, Shift+Right", "Seek 60 seconds"},
    {"Up, Down", "Volume"},
    {"T", "Speak the current time"},
    {"I", "Skip the intro"},
    {"R", "Turn subtitle reading on or off"},
    {"N, P", "Next and previous episode"},
    {"F11", "Full screen"},
    {"Escape", "Leave full screen, then close the player"},
}};

}

ShortcutsDialog::ShortcutsDialog(wxWindow* parent)
    : wxDialog(parent, wxID_ANY, "Keyboard shortcuts", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
    auto* list = new wxListView(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(560, 420)),
                                wxLC_REPORT | wxLC_SINGLE_SEL);
    list->AppendColumn("Key");
    list->AppendColumn("Action");
    for (size_t i = 0; i < shortcuts.size(); ++i) {
        const auto row = list->InsertItem(static_cast<long>(i), shortcuts[i].first);
        list->SetItem(row, 1, shortcuts[i].second);
    }
    list->SetColumnWidth(0, wxLIST_AUTOSIZE);
    list->SetColumnWidth(1, wxLIST_AUTOSIZE_USEHEADER);
    list->Select(0);
    list->Focus(0);
    setAccessibleName(list, "Keyboard shortcuts");

    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(list, 1, wxEXPAND | wxALL, FromDIP(12));
    sizer->Add(CreateStdDialogButtonSizer(wxCLOSE), 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    SetSizerAndFit(sizer);
    SetEscapeId(wxID_CLOSE);
    Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); }, wxID_CLOSE);
    list->SetFocus();
}

}
