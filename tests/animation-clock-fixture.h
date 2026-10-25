// Hold one animation instant while checking retarget continuity. GPU timing,
// waits and worker threads continue to use real time. Only explicitly scoped
// calls on the test thread substitute production QPC reads.
#include <windows.h>
namespace AnimationClockFixture {
inline thread_local bool held=false;
inline thread_local LARGE_INTEGER instant{};
inline BOOL WINAPI Counter(LARGE_INTEGER* value) {
    if (!held) return ::QueryPerformanceCounter(value);
    *value=instant; return TRUE;
}
struct Freeze {
    bool previous=held;
    LARGE_INTEGER saved=instant;
    Freeze() {if (!held) ::QueryPerformanceCounter(&instant); held=true;}
    ~Freeze() {held=previous; instant=saved;}
};
}
#define QueryPerformanceCounter AnimationClockFixture::Counter
