// Model queued hardware input without injecting keys/mouse events into the
// user's desktop. Low word stays zero: input already examined is still pending.
#include <windows.h>
#include <atomic>
#include <stdexcept>
namespace FrameInputFixture {
inline thread_local bool enabled=false;
inline thread_local UINT pending=0;
inline std::atomic<HWND> heldWindow=nullptr;
inline std::atomic<UINT> heldMessage=0;
inline BOOL WINAPI Post(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if (hwnd && hwnd==heldWindow.load(std::memory_order_acquire) &&
        message==heldMessage.load(std::memory_order_relaxed)) return FALSE;
    return ::PostMessageW(hwnd,message,wp,lp);
}
struct HoldFramePosts {
    HoldFramePosts(HWND hwnd,UINT message) {
        heldMessage.store(message,std::memory_order_relaxed);
        heldWindow.store(hwnd,std::memory_order_release);
    }
    ~HoldFramePosts() { heldWindow.store(nullptr,std::memory_order_release); }
};
inline DWORD WINAPI QueueStatus(UINT flags) {
    return enabled ? MAKELONG(0,pending & flags) : ::GetQueueStatus(flags);
}
struct Session {
    bool previous=enabled;
    UINT previousPending=pending;
    Session() { enabled=true; pending=0; }
    ~Session() { enabled=previous; pending=previousPending; }
};
}
#define GetQueueStatus FrameInputFixture::QueueStatus
#define PostMessageW FrameInputFixture::Post

// Explicit test modifiers must survive nested Shell/window message pumps.
// SetKeyboardState changes the thread's live queue snapshot, which subsequent
// messages can replace. Substitute only production calls in a scoped fixture.
namespace ModifierFixture {
inline thread_local BYTE* active=nullptr;
inline SHORT WINAPI Key(int key) {
    if(!active || key<0 || key>=256) return ::GetKeyState(key);
    return SHORT((active[key]&0x80 ? 0x8000 : 0) | (active[key]&1));
}
inline BOOL WINAPI Translate(const MSG* message) {
    if(!active) return ::TranslateMessage(message);
    BYTE saved[256]{};
    if(!GetKeyboardState(saved) || !SetKeyboardState(active))
        throw std::runtime_error("private keyboard translation state");
    const BOOL result=::TranslateMessage(message);
    if(!SetKeyboardState(saved)) throw std::runtime_error("restore keyboard translation state");
    return result;
}
struct Session {
    BYTE keys[256]{};
    BYTE* previous=active;
    Session() {active=keys;}
    ~Session() {active=previous;}
};
}
#define GetKeyState ModifierFixture::Key
#define TranslateMessage ModifierFixture::Translate
