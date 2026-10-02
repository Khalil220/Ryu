#pragma once

class wxString;
class wxWindow;

namespace ryu {

void setAccessibleName(wxWindow* window, const wxString& name);
void setAccessibleShortcut(wxWindow* window, const wxString& shortcut);
void setAccessibleDescription(wxWindow* window, const wxString& description);

}
