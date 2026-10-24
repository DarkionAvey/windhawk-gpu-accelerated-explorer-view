// Replay physical cursor samples without moving the user's real mouse or
// injecting input. Threads outside this fixture keep using the system APIs.
#include <windows.h>
#include <Unknwn.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
// Projected CoreWindow also has a GetAsyncKeyState member. Parse it before
// substituting the Win32 call used by the mod.
#include <winrt/Windows.UI.Core.h>
namespace PointerFixture {
inline thread_local bool enabled = false, held = true;
inline thread_local POINT screen{};
inline BOOL WINAPI Cursor(LPPOINT output) {
    if (!enabled) return ::GetCursorPos(output);
    *output = screen;
    return TRUE;
}
inline SHORT WINAPI Key(int key) {
    if (enabled && key == VK_LBUTTON) return held ? SHORT(0x8000) : 0;
    return ::GetAsyncKeyState(key);
}
inline void At(HWND hwnd, POINT point) {
    screen = point;
    ClientToScreen(hwnd, &screen);
}
struct Session {
    bool previousEnabled=enabled, previousHeld=held;
    POINT previousScreen=screen;
    Session() { enabled=held=true; }
    ~Session() { enabled=previousEnabled; held=previousHeld; screen=previousScreen; }
};
}
#define GetCursorPos PointerFixture::Cursor
#define GetAsyncKeyState PointerFixture::Key
