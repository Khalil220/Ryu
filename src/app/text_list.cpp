#include "text_list.hpp"

#include <algorithm>

namespace ryu {

TextList::TextList(wxWindow* parent)
    : wxListView(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | wxLC_SINGLE_SEL | wxLC_NO_HEADER) {
    AppendColumn(wxEmptyString);
    Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
        event.Skip();
        CallAfter([this] { fitColumn(); });
    });
}

void TextList::setItems(std::vector<wxString> items) {
    items_ = std::move(items);
    SetItemCount(static_cast<long>(items_.size()));
    fitColumn();
    CallAfter([this] { fitColumn(); });
    Refresh();
}

void TextList::selectItem(long index) {
    if (index < 0 || index >= GetItemCount()) {
        return;
    }
    Select(index);
    Focus(index);
}

size_t TextList::selectedIndex() const {
    const long index = GetFirstSelected();
    return index < 0 ? static_cast<size_t>(-1) : static_cast<size_t>(index);
}

wxString TextList::OnGetItemText(long item, long) const {
    return item >= 0 && static_cast<size_t>(item) < items_.size() ? items_[static_cast<size_t>(item)] : wxString();
}

void TextList::fitColumn() {
    SetColumnWidth(0, std::max(1, GetClientSize().GetWidth()));
}

}
