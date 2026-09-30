#pragma once

#include "provider.hpp"

#include <wx/panel.h>

class wxImage;
class wxSimplebook;
class wxStaticBitmap;
class wxStaticText;

namespace ryu {

class SynopsisView;

class ShowDetailsPanel : public wxPanel {
public:
    explicit ShowDetailsPanel(wxWindow* parent);

    void showDetails(const ryu::Show& show);
    void setPoster(const wxImage& image);
    void clear();

private:
    wxSize posterSize_;
    wxSimplebook* book_ = nullptr;
    wxStaticBitmap* poster_ = nullptr;
    wxStaticText* title_ = nullptr;
    wxStaticText* info_ = nullptr;
    SynopsisView* synopsis_ = nullptr;
};

}
