// Catch accidental process/window activation from an offscreen, tabless host.
// Native Shell selection and context menus remain real.
#include <windows.h>
#include <shellapi.h>
namespace TabActivationFixture {
inline thread_local bool enabled=false;
inline thread_local unsigned calls=0;
inline BOOL WINAPI Execute(SHELLEXECUTEINFOW* info) {
    if (!enabled) return ::ShellExecuteExW(info);
    ++calls; SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
}
struct Session {
    Session() {enabled=true; calls=0;}
    ~Session() {enabled=false;}
};
}
#define ShellExecuteExW TabActivationFixture::Execute
