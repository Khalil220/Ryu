#pragma once

#include <wx/listctrl.h>

#include <vector>

namespace ryu {

class TextList : public wxListView {
public:
    explicit TextList(wxWindow* parent);

    void setItems(std::vector<wxString> items);
    void selectItem(long index);
    size_t selectedIndex() const;

protected:
    wxString OnGetItemText(long item, long column) const override;

private:
    void fitColumn();

    std::vector<wxString> items_;
};

}
