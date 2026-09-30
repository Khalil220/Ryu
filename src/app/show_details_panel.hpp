#pragma once

#include "provider.hpp"

#include <wx/panel.h>

class wxImage;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

namespace ryu {

class ShowDetailsPanel : public wxPanel {
public:
    explicit ShowDetailsPanel(wxWindow* parent);

    void showDetails(const ryu::Show& show);
    void setPoster(const wxImage& image);
    void clear();
    wxTextCtrl* synopsis() const { return synopsis_; }

private:
    wxSize posterSize_;
    wxStaticBitmap* poster_ = nullptr;
    wxStaticText* title_ = nullptr;
    wxStaticText* info_ = nullptr;
    wxTextCtrl* synopsis_ = nullptr;
};

}
