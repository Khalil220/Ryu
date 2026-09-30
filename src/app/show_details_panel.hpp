#pragma once

#include "provider.hpp"

#include <wx/panel.h>

class wxBitmap;
class wxImage;
class wxStaticBitmap;
class wxStaticText;

namespace ryu {

class SynopsisView;

wxImage fitPoster(const wxImage& image, wxSize box);

class ShowDetailsPanel : public wxPanel {
public:
    explicit ShowDetailsPanel(wxWindow* parent);

    void showDetails(const ryu::Show& show);
    void setPoster(const wxBitmap& poster);
    void clearPoster();
    void clear();
    wxSize posterSize() const { return posterSize_; }

private:
    wxSize posterSize_;
    wxStaticBitmap* poster_ = nullptr;
    wxStaticText* title_ = nullptr;
    wxStaticText* info_ = nullptr;
    SynopsisView* synopsis_ = nullptr;
};

}
