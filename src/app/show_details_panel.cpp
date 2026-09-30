#include "show_details_panel.hpp"

#include "format.hpp"

#include <wx/image.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textwrapper.h>

#include <algorithm>

namespace ryu {

namespace {

class LineCounter : public wxTextWrapper {
public:
    int lines = 1;

protected:
    void OnOutputLine(const wxString&) override {}
    void OnNewLine() override { ++lines; }
};

}

class SynopsisView : public wxScrolled<wxWindow> {
public:
    explicit SynopsisView(wxWindow* parent) : wxScrolled<wxWindow>(parent) {
        text_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
                                 wxST_NO_AUTORESIZE);
        SetScrollRate(0, FromDIP(8));
        ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            event.Skip();
            CallAfter([this] { fit(); });
        });
    }

    bool AcceptsFocus() const override { return false; }
    bool AcceptsFocusFromKeyboard() const override { return false; }

    void setText(const wxString& text) {
        text_->SetLabelText(text);
        Scroll(0, 0);
        fit();
    }

private:
    void fit() {
        const int width = GetClientSize().GetWidth();
        if (width <= 0) {
            return;
        }
        LineCounter counter;
        counter.Wrap(text_, text_->GetLabelText(), width);
        const int height = (counter.lines + 1) * text_->GetCharHeight();
        text_->SetSize(0, 0, width, height);
        SetVirtualSize(width, height);
    }

    wxStaticText* text_ = nullptr;
};

ShowDetailsPanel::ShowDetailsPanel(wxWindow* parent) : wxPanel(parent), posterSize_(FromDIP(wxSize(160, 240))) {
    book_ = new wxSimplebook(this);

    auto* hintPage = new wxPanel(book_);
    auto* hint = new wxStaticText(hintPage, wxID_ANY, "Pick a search result to see its details here.");
    hint->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    auto* hintSizer = new wxBoxSizer(wxVERTICAL);
    hintSizer->AddStretchSpacer();
    hintSizer->Add(hint, 0, wxALIGN_CENTER_HORIZONTAL);
    hintSizer->AddStretchSpacer();
    hintPage->SetSizer(hintSizer);

    auto* page = new wxPanel(book_);
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    poster_ = new wxStaticBitmap(page, wxID_ANY, wxBitmapBundle(), wxDefaultPosition, posterSize_);
    poster_->SetMinSize(posterSize_);
    row->Add(poster_, 0, wxALIGN_TOP | wxRIGHT, FromDIP(14));
    auto* text = new wxBoxSizer(wxVERTICAL);
    title_ = new wxStaticText(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    title_->SetFont(GetFont().Scaled(1.6f).Bold());
    text->Add(title_, 0, wxEXPAND | wxBOTTOM, FromDIP(4));
    info_ = new wxStaticText(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    info_->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    text->Add(info_, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    synopsis_ = new SynopsisView(page);
    text->Add(synopsis_, 1, wxEXPAND);
    row->Add(text, 1, wxEXPAND);
    page->SetSizer(row);

    book_->AddPage(hintPage, wxEmptyString, true);
    book_->AddPage(page, wxEmptyString, false);
    auto* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(book_, 1, wxEXPAND);
    SetSizer(sizer);
}

void ShowDetailsPanel::showDetails(const ryu::Show& show) {
    book_->ChangeSelection(1);
    title_->SetLabelText(wxString::FromUTF8(show.title));
    info_->SetLabelText(wxString::FromUTF8(showDetailsLine(show)));
    synopsis_->setText(show.synopsis.empty() ? wxString("No synopsis available.") : wxString::FromUTF8(show.synopsis));
    poster_->SetBitmap(wxBitmapBundle());
    book_->GetPage(1)->Layout();
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
    book_->GetPage(1)->Layout();
}

void ShowDetailsPanel::clear() {
    title_->SetLabelText(wxEmptyString);
    info_->SetLabelText(wxEmptyString);
    synopsis_->setText(wxEmptyString);
    poster_->SetBitmap(wxBitmapBundle());
    book_->ChangeSelection(0);
}

}
