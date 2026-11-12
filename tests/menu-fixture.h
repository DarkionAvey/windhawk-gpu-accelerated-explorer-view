// Narrow routing observation for offscreen selection/gesture tests. This does
// not prove that a menu is displayed: native-menu-smoke.h observes real popups.
#include <windows.h>
#include <functional>
namespace MenuFixture {
inline thread_local HWND owner = nullptr;
inline thread_local unsigned calls = 0;
inline thread_local int items = 0;
inline thread_local HWND target = nullptr;
inline thread_local WPARAM source = 0;
inline thread_local LPARAM position = 0;
inline thread_local POINT menuPoint{};
inline thread_local std::function<UINT(HMENU)> choose;
inline LRESULT WINAPI Send(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (owner && message == WM_CONTEXTMENU) {
        ++calls; target = window; source = wp; position = lp;
        menuPoint = POINT{static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))};
        return 0;
    }
    return ::SendMessageW(window, message, wp, lp);
}
inline BOOL WINAPI Track(HMENU menu, UINT flags, int x, int y, HWND window, LPTPMPARAMS parameters) {
    if (owner && owner == window) { ++calls; items = GetMenuItemCount(menu); return choose ? choose(menu) : 0; }
    return ::TrackPopupMenuEx(menu, flags, x, y, window, parameters);
}
struct Session {
    HWND previous = owner;
    std::function<UINT(HMENU)> previousChoose = std::move(choose);
    explicit Session(HWND window) {
        owner = window; calls = 0; items = 0; target = nullptr; source = 0; position = 0;
    }
    ~Session() { owner = previous; choose = std::move(previousChoose); }
};
}
#define TrackPopupMenuEx MenuFixture::Track
#define SendMessageW MenuFixture::Send
