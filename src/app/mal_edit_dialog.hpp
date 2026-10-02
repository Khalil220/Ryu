#pragma once

#include "mal.hpp"

#include <wx/dialog.h>

#include <string>
#include <vector>

class wxCheckBox;
class wxChoice;
class wxTextCtrl;

namespace ryu {

class MalEditDialog : public wxDialog {
public:
    MalEditDialog(wxWindow* parent, std::vector<MalAnime> candidates, size_t selected);

    const MalAnime& anime() const { return candidates_[selected_]; }
    MalListStatus edited() const;

private:
    struct DateFields {
        wxTextCtrl* year = nullptr;
        wxTextCtrl* month = nullptr;
        wxTextCtrl* day = nullptr;
    };

    DateFields addDateGroup(wxSizer* sizer, const wxString& title);
    void showCandidate(size_t index);
    int watched() const;
    void showWatched(int watched);
    static void showDate(const DateFields& fields, const std::string& date);
    static MalDate dateIn(const DateFields& fields);
    static std::string editedDate(const DateFields& fields, const std::string& original);
    bool checkDate(const DateFields& fields, const std::string& original);
    void onSave(wxCommandEvent& event);

    std::vector<MalAnime> candidates_;
    size_t selected_ = 0;
    wxChoice* animeChoice_ = nullptr;
    wxChoice* statusChoice_ = nullptr;
    wxTextCtrl* watched_ = nullptr;
    wxChoice* scoreChoice_ = nullptr;
    DateFields start_;
    DateFields finish_;
    wxCheckBox* rewatching_ = nullptr;
    wxButton* removeButton_ = nullptr;
};

}
