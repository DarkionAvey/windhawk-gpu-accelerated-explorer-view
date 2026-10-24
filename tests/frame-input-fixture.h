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
