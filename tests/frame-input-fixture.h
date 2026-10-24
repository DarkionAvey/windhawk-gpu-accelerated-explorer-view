// Model queued hardware input without injecting keys/mouse events into the
// user's desktop. Low word stays zero: input already examined is still pending.
#include <windows.h>
namespace FrameInputFixture {
inline thread_local bool enabled=false;
inline thread_local UINT pending=0;
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

// Explicit test modifiers must survive nested Shell/window message pumps.
// SetKeyboardState changes the thread's live queue snapshot, which subsequent
// messages can replace. Substitute only production calls in a scoped fixture.
namespace ModifierFixture {
inline thread_local BYTE* active=nullptr;
inline SHORT WINAPI Key(int key) {
    if(!active || key<0 || key>=256) return ::GetKeyState(key);
    return SHORT((active[key]&0x80 ? 0x8000 : 0) | (active[key]&1));
}
struct Session {
    BYTE keys[256]{};
    BYTE* previous=active;
    Session() {active=keys;}
    ~Session() {active=previous;}
};
}
#define GetKeyState ModifierFixture::Key
