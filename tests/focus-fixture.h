// Modal tests never activate windows or move the user's keyboard focus.
// Parse projected UI members before substituting the Win32 functions.
#include <windows.h>
#include <commctrl.h>
#include <uiautomation.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Content.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
namespace FocusFixture {
inline thread_local bool enabled=false;
inline thread_local HWND focus=nullptr, foreground=nullptr, routed=nullptr;
inline thread_local HWND owner=nullptr;
inline thread_local HWND rejected=nullptr;
inline thread_local bool ownerEnabled=true;
inline thread_local unsigned sets=0;
inline HWND WINAPI Focus() { return enabled ? focus : ::GetFocus(); }
inline HWND WINAPI Foreground() { return enabled ? foreground : ::GetForegroundWindow(); }
inline BOOL WINAPI Visible(HWND hwnd) { return enabled && hwnd==owner ? TRUE : ::IsWindowVisible(hwnd); }
inline BOOL WINAPI IsEnabled(HWND hwnd) {
    return enabled && hwnd==owner ? ownerEnabled : ::IsWindowEnabled(hwnd);
}
inline void EnableOwner(HWND hwnd,bool value) {
    if(ownerEnabled==value) return;
    if(!value) SendMessageW(hwnd,WM_CANCELMODE,0,0);
    ownerEnabled=value;
    SendMessageW(hwnd,WM_ENABLE,value,0);
}
inline HWND WINAPI Set(HWND target) {
    if(!enabled) return ::SetFocus(target);
    if(target==rejected) return nullptr;
    const HWND previous=focus; focus=target; ++sets; return previous;
}
inline LRESULT WINAPI Subclass(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(enabled && (msg==WM_ACTIVATE || msg==WM_ENABLE || msg==WM_CANCELMODE)) return 0;
    return ::DefSubclassProc(hwnd,msg,wp,lp);
}
struct Session {
    bool previous=enabled;
    HWND previousFocus=focus, previousForeground=foreground;
    HWND previousOwner=owner;
    HWND previousRejected=rejected;
    bool previousOwnerEnabled=ownerEnabled;
    Session(HWND frame,HWND list) { enabled=true; owner=foreground=frame; ownerEnabled=true; focus=list; sets=0; rejected=nullptr; }
    ~Session() { enabled=previous; focus=previousFocus; foreground=previousForeground;
        owner=previousOwner; ownerEnabled=previousOwnerEnabled; rejected=previousRejected; }
};
}
#define GetFocus FocusFixture::Focus
#define GetForegroundWindow FocusFixture::Foreground
#define IsWindowVisible FocusFixture::Visible
#define SetFocus FocusFixture::Set
#define DefSubclassProc FocusFixture::Subclass
#define IsWindowEnabled FocusFixture::IsEnabled
