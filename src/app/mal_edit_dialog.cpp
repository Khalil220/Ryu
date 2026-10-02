#include "mal_edit_dialog.hpp"

#include "accessibility.hpp"
#include "format.hpp"

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/datetime.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <array>
#include <functional>

namespace ryu {

namespace {

constexpr int mostEpisodes = 99999;
constexpr int earliestYear = 1900;
constexpr std::array<const char*, 11> scoreLabels{
    "No score",    "1, Appalling", "2, Horrible", "3, Very bad",  "4, Bad",          "5, Average",
    "6, Fine",     "7, Good",      "8, Very good", "9, Great",    "10, Masterpiece",
};

void numberKeys(wxTextCtrl* field, std::function<void(int)> step) {
    field->Bind(wxEVT_CHAR, [](wxKeyEvent& event) {
        const auto typed = event.GetUnicodeKey();
        const bool printable = typed != WXK_NONE && typed >= WXK_SPACE && typed != WXK_DELETE;
        if (!printable || (typed >= '0' && typed <= '9')) {
            event.Skip();
        }
    });
    field->Bind(wxEVT_KEY_DOWN, [step = std::move(step)](wxKeyEvent& event) {
        const int key = event.GetKeyCode();
        if ((key == WXK_UP || key == WXK_DOWN) && !event.HasAnyModifiers()) {
            step(key == WXK_UP ? 1 : -1);
        } else {
            event.Skip();
        }
    });
}

int numberIn(const wxTextCtrl* field) {
    return leadingNumber(field->GetValue().utf8_string());
}

void showNumber(wxTextCtrl* field, int value) {
    field->ChangeValue(value > 0 ? wxString::Format("%d", value) : wxString());
    field->SetInsertionPointEnd();
}

}

MalEditDialog::MalEditDialog(wxWindow* parent, std::vector<MalAnime> candidates, size_t selected)
    : wxDialog(parent, wxID_ANY, "MyAnimeList"), candidates_(std::move(candidates)),
      selected_(std::min(selected, candidates_.size() - 1)) {
    const int gap = FromDIP(8);
    const int margin = FromDIP(12);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    auto* grid = new wxFlexGridSizer(2, gap, gap);
    grid->AddGrowableCol(1);
    const auto label = [&](const wxString& text) {
        grid->Add(new wxStaticText(this, wxID_ANY, text), 0, wxALIGN_CENTER_VERTICAL);
    };

    if (candidates_.size() > 1) {
        label("A&nime:");
        animeChoice_ = new wxChoice(this, wxID_ANY);
        for (const auto& candidate : candidates_) {
            animeChoice_->Append(wxString::FromUTF8(malCandidateLabel(candidate)));
        }
        animeChoice_->SetSelection(static_cast<int>(selected_));
        grid->Add(animeChoice_, 1, wxEXPAND);
        setAccessibleName(animeChoice_, "Anime");
    } else {
        sizer->Add(new wxStaticText(this, wxID_ANY, wxString::FromUTF8(malCandidateLabel(candidates_.front()))), 0,
                   wxLEFT | wxRIGHT | wxTOP, margin);
    }

    label("&Status:");
    statusChoice_ = new wxChoice(this, wxID_ANY);
    for (const auto status : malLists) {
        statusChoice_->Append(wxString::FromUTF8(malStatusLabel(status)));
    }
    grid->Add(statusChoice_, 0);

    label("&Episodes watched:");
    watched_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(110, -1)));
    grid->Add(watched_, 0);

    label("S&core:");
    scoreChoice_ = new wxChoice(this, wxID_ANY);
    for (const auto* text : scoreLabels) {
        scoreChoice_->Append(text);
    }
    grid->Add(scoreChoice_, 0);
    sizer->Add(grid, 0, wxEXPAND | wxALL, margin);

    start_ = addDateGroup(sizer, "Start date");
    finish_ = addDateGroup(sizer, "Finish date");

    rewatching_ = new wxCheckBox(this, wxID_ANY, "Re&watching");
    sizer->Add(rewatching_, 0, wxALL, margin);

    auto* buttons = new wxBoxSizer(wxHORIZONTAL);
    removeButton_ = new wxButton(this, wxID_DELETE, "&Remove from my list");
    buttons->Add(removeButton_, 0, wxRIGHT, gap);
    buttons->AddStretchSpacer();
    auto* save = new wxButton(this, wxID_OK, "Save");
    buttons->Add(save, 0, wxRIGHT, gap);
    buttons->Add(new wxButton(this, wxID_CANCEL, "Cancel"), 0);
    sizer->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, margin);
    save->SetDefault();
    SetSizer(sizer);

    setAccessibleName(statusChoice_, "Status");
    setAccessibleName(watched_, "Episodes watched");
    setAccessibleName(scoreChoice_, "Score");
    numberKeys(watched_, [this](int delta) {
        const int episodes = anime().episodes;
        showWatched(std::clamp(watched() + delta, 0, episodes > 0 ? episodes : mostEpisodes));
    });

    showCandidate(selected_);
    if (animeChoice_) {
        animeChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
            showCandidate(static_cast<size_t>(animeChoice_->GetSelection()));
        });
    }
    statusChoice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
        const int episodes = anime().episodes;
        if (malLists[statusChoice_->GetSelection()] == MalStatus::Completed && episodes > 0) {
            showWatched(episodes);
        }
    });
    watched_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& event) {
        event.Skip();
        showWatched(watched());
    });
    Bind(wxEVT_BUTTON, &MalEditDialog::onSave, this, wxID_OK);
    removeButton_->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        const auto title = wxString::FromUTF8(malTitle(anime()));
        const auto list = wxString::FromUTF8(malStatusLabel(anime().list.status));
        if (wxMessageBox("Remove " + title + " from your " + list + " list?", "MyAnimeList",
                         wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) == wxYES) {
            EndModal(wxID_DELETE);
        }
    });
    if (animeChoice_) {
        animeChoice_->SetFocus();
    } else {
        statusChoice_->SetFocus();
    }
}

MalEditDialog::DateFields MalEditDialog::addDateGroup(wxSizer* sizer, const wxString& title) {
    const int gap = FromDIP(8);
    auto* group = new wxStaticBoxSizer(wxHORIZONTAL, this, title);
    auto* box = group->GetStaticBox();
    const auto field = [&](const wxString& text, const wxString& name, int width, int first, int last, int fresh) {
        group->Add(new wxStaticText(box, wxID_ANY, text), 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxTOP | wxBOTTOM, gap);
        auto* edit = new wxTextCtrl(box, wxID_ANY, wxEmptyString, wxDefaultPosition, FromDIP(wxSize(width, -1)));
        group->Add(edit, 0, wxALIGN_CENTER_VERTICAL | wxALL, gap);
        setAccessibleName(edit, name);
        numberKeys(edit, [edit, first, last, fresh](int delta) {
            const int current = numberIn(edit);
            showNumber(edit, current == 0 ? fresh : std::clamp(current + delta, first, last));
        });
        return edit;
    };
    DateFields fields;
    const int thisYear = wxDateTime::Now().GetYear();
    fields.year = field("&Year:", "Year", 70, earliestYear, thisYear, thisYear);
    fields.month = field("&Month:", "Month", 50, 1, 12, 1);
    fields.day = field("&Day:", "Day", 50, 1, 31, 1);
    sizer->Add(group, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(12));
    return fields;
}

void MalEditDialog::showCandidate(size_t index) {
    selected_ = index;
    const auto& candidate = anime();
    const auto& list = candidate.list;
    const bool listed = list.status != MalStatus::None;
    const auto status = listed ? list.status : MalStatus::Watching;
    statusChoice_->SetSelection(static_cast<int>(std::ranges::find(malLists, status) - std::begin(malLists)));
    showWatched(list.watched);
    scoreChoice_->SetSelection(std::clamp(list.score, 0, 10));
    showDate(start_, list.startDate);
    showDate(finish_, list.finishDate);
    rewatching_->SetValue(list.rewatching);
    removeButton_->Show(listed);
    SetTitle(listed ? "Edit on MyAnimeList" : "Add to MyAnimeList");
    GetSizer()->SetSizeHints(this);
    CentreOnParent();
}

int MalEditDialog::watched() const {
    return malWatchedFrom(watched_->GetValue().utf8_string(), anime().episodes, anime().list.watched);
}

void MalEditDialog::showWatched(int watched) {
    watched_->ChangeValue(wxString::FromUTF8(malWatchedText(watched, anime().episodes)));
    watched_->SetInsertionPoint(static_cast<long>(std::to_string(watched).size()));
}

void MalEditDialog::showDate(const DateFields& fields, const std::string& date) {
    const auto parts = malDateFrom(date);
    showNumber(fields.year, parts.year);
    showNumber(fields.month, parts.month);
    showNumber(fields.day, parts.day);
}

MalDate MalEditDialog::dateIn(const DateFields& fields) {
    return {numberIn(fields.year), numberIn(fields.month), numberIn(fields.day)};
}

std::string MalEditDialog::editedDate(const DateFields& fields, const std::string& original) {
    const auto parts = dateIn(fields);
    return parts == malDateFrom(original) ? original : malDateText(parts);
}

bool MalEditDialog::checkDate(const DateFields& fields, const std::string& original) {
    const auto parts = dateIn(fields);
    const int thisYear = wxDateTime::Now().GetYear();
    const auto wrong = parts == malDateFrom(original) ? MalDatePart::None : wrongMalDatePart(parts, thisYear);
    if (wrong == MalDatePart::None) {
        return true;
    }
    wxMessageBox(parts.year > thisYear ? "The year can't be later than this one."
                                       : "Fill in a real year, month and day, or leave all three empty.",
                 "MyAnimeList", wxOK | wxICON_WARNING, this);
    (wrong == MalDatePart::Year ? fields.year : wrong == MalDatePart::Month ? fields.month : fields.day)->SetFocus();
    return false;
}

MalListStatus MalEditDialog::edited() const {
    const auto& original = anime().list;
    MalListStatus list;
    list.status = malLists[statusChoice_->GetSelection()];
    list.watched = watched();
    list.score = scoreChoice_->GetSelection();
    list.startDate = editedDate(start_, original.startDate);
    list.finishDate = editedDate(finish_, original.finishDate);
    list.rewatching = rewatching_->GetValue();
    return list;
}

void MalEditDialog::onSave(wxCommandEvent& event) {
    const auto& original = anime().list;
    if (checkDate(start_, original.startDate) && checkDate(finish_, original.finishDate)) {
        event.Skip();
    }
}

}
