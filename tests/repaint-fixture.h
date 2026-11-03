// The code under test's own repaint requests, independent of the desktop.
// Windows and other applications can invalidate every top-level window at any
// time (theme, display or DWM changes, RedrawWindow(NULL)), so an empty update
// region is machine state, not test-owned state. Tests assert on what the mod
// requested since a mark; the real calls still run unchanged.
#include <windows.h>
#include <map>
#include <mutex>
namespace RepaintFixture {
inline std::mutex lock;
inline std::map<HWND,unsigned long long> requests;
inline void Record(HWND hwnd) {
    if (!hwnd) return;
    std::lock_guard guard(lock); ++requests[hwnd];
}
inline unsigned long long Count(HWND hwnd) {
    std::lock_guard guard(lock);
    const auto found=requests.find(hwnd);
    return found==requests.end() ? 0 : found->second;
}
inline BOOL WINAPI Invalidate(HWND hwnd,const RECT* rect,BOOL erase) {
    Record(hwnd); return ::InvalidateRect(hwnd,rect,erase);
}
inline BOOL WINAPI Redraw(HWND hwnd,const RECT* rect,HRGN region,UINT flags) {
    if (flags & RDW_INVALIDATE) Record(hwnd);
    return ::RedrawWindow(hwnd,rect,region,flags);
}
// Repaint requests for one window since construction.
struct Mark {
    HWND hwnd;
    unsigned long long start;
    explicit Mark(HWND window):hwnd(window),start(Count(window)) {}
    bool Requested() const { return Count(hwnd)!=start; }
};
}
#define InvalidateRect RepaintFixture::Invalidate
#define RedrawWindow RepaintFixture::Redraw
