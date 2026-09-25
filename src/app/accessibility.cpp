#include "accessibility.hpp"

#include <wx/window.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>

#include <initguid.h>
#include <oleacc.h>
#endif

namespace ryu {

void setAccessibleName(wxWindow* window, const wxString& name) {
#ifdef __WXMSW__
    IAccPropServices* services = nullptr;
    if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&services)))) {
        return;
    }
    const auto hwnd = static_cast<HWND>(window->GetHWND());
    services->SetHwndPropStr(hwnd, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME, name.wc_str());
    services->Release();

    window->Bind(wxEVT_DESTROY, [hwnd, window](wxWindowDestroyEvent& event) {
        if (event.GetEventObject() == window) {
            IAccPropServices* cleanup = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&cleanup)))) {
                MSAAPROPID property = PROPID_ACC_NAME;
                cleanup->ClearHwndProps(hwnd, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, &property, 1);
                cleanup->Release();
            }
        }
        event.Skip();
    });
#else
    window->SetName(name);
#endif
}

}
