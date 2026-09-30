#include "show_details_panel.hpp"

#include "accessibility.hpp"
#include "format.hpp"

#include <wx/image.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include <algorithm>

namespace ryu {

ShowDetailsPanel::ShowDetailsPanel(wxWindow* parent) : wxPanel(parent), posterSize_(FromDIP(wxSize(160, 240))) {
    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    poster_ = new wxStaticBitmap(this, wxID_ANY, wxBitmapBundle(), wxDefaultPosition, posterSize_);
    poster_->SetMinSize(posterSize_);
    sizer->Add(poster_, 0, wxALIGN_TOP | wxRIGHT, FromDIP(14));

    auto* text = new wxBoxSizer(wxVERTICAL);
    title_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    title_->SetFont(GetFont().Scaled(1.6f).Bold());
    text->Add(title_, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    info_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    info_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    text->Add(info_, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    synopsis_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                               wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE);
    synopsis_->SetBackgroundColour(GetBackgroundColour());
    text->Add(synopsis_, 1, wxEXPAND);
    sizer->Add(text, 1, wxEXPAND);

    SetSizer(sizer);
    setAccessibleName(synopsis_, "Synopsis");
    setAccessibleShortcut(synopsis_, "Alt+Y");
}

void ShowDetailsPanel::showDetails(const ryu::Show& show) {
    title_->SetLabelText(wxString::FromUTF8(show.title));
    info_->SetLabelText(wxString::FromUTF8(showDetailsLine(show)));
    synopsis_->ChangeValue(show.synopsis.empty() ? wxString("No synopsis available.") : wxString::FromUTF8(show.synopsis));
    poster_->SetBitmap(wxBitmapBundle());
    Layout();
}

void ShowDetailsPanel::setPoster(const wxImage& image) {
    if (!image.IsOk() || image.GetWidth() <= 0 || image.GetHeight() <= 0) {
        return;
    }
    const double scale = std::min(static_cast<double>(posterSize_.x) / image.GetWidth(),
                                  static_cast<double>(posterSize_.y) / image.GetHeight());
    const int width = std::max(1, static_cast<int>(image.GetWidth() * scale));
    const int height = std::max(1, static_cast<int>(image.GetHeight() * scale));
    poster_->SetBitmap(wxBitmap(image.Scale(width, height, wxIMAGE_QUALITY_HIGH)));
    Layout();
}

void ShowDetailsPanel::clear() {
    title_->SetLabelText(wxEmptyString);
    info_->SetLabelText(wxEmptyString);
    synopsis_->ChangeValue(wxEmptyString);
    poster_->SetBitmap(wxBitmapBundle());
}

}
