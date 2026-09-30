#include "accessibility.hpp"

#include <wx/window.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>

#include <initguid.h>
#include <oleacc.h>

#include <iterator>
#include <set>
#endif

namespace ryu {

#ifdef __WXMSW__
namespace {

void clearProperties(HWND hwnd) {
    IAccPropServices* services = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&services)))) {
        MSAAPROPID properties[] = {PROPID_ACC_NAME, PROPID_ACC_KEYBOARDSHORTCUT};
        services->ClearHwndProps(hwnd, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, properties,
                                 static_cast<int>(std::size(properties)));
        services->Release();
    }
}

void setProperty(wxWindow* window, MSAAPROPID property, const wxString& value) {
    IAccPropServices* services = nullptr;
    if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&services)))) {
        return;
    }
    const auto hwnd = static_cast<HWND>(window->GetHWND());
    services->SetHwndPropStr(hwnd, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, property, value.wc_str());
    services->Release();

    static std::set<HWND> annotated;
    if (!annotated.insert(hwnd).second) {
        return;
    }
    window->Bind(wxEVT_DESTROY, [hwnd, window](wxWindowDestroyEvent& event) {
        if (event.GetEventObject() == window) {
            annotated.erase(hwnd);
            clearProperties(hwnd);
        }
        event.Skip();
    });
}

}
#endif

void setAccessibleName(wxWindow* window, const wxString& name) {
#ifdef __WXMSW__
    setProperty(window, PROPID_ACC_NAME, name);
#else
    window->SetName(name);
#endif
}

void setAccessibleShortcut(wxWindow* window, const wxString& shortcut) {
#ifdef __WXMSW__
    setProperty(window, PROPID_ACC_KEYBOARDSHORTCUT, shortcut);
#endif
}

}
