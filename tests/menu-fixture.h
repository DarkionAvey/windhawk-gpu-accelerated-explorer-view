// Observe the real menu preparation without showing a popup or invoking verbs.
#include <windows.h>
namespace MenuFixture {
inline thread_local HWND owner = nullptr;
inline thread_local unsigned calls = 0;
inline thread_local int items = 0;
inline BOOL WINAPI Track(HMENU menu, UINT flags, int x, int y, HWND window, LPTPMPARAMS parameters) {
    if (owner && owner == window) { ++calls; items = GetMenuItemCount(menu); return 0; }
    return ::TrackPopupMenuEx(menu, flags, x, y, window, parameters);
}
struct Session {
    HWND previous = owner;
    explicit Session(HWND window) { owner = window; calls = 0; items = 0; }
    ~Session() { owner = previous; }
};
}
#define TrackPopupMenuEx MenuFixture::Track
