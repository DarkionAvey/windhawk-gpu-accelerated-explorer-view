// Catch accidental process/window activation from an offscreen, tabless host.
// Native Shell selection and context menus remain real. The launch the mod
// asked for (item and verb) is recorded instead of starting a user application.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
namespace TabActivationFixture {
inline thread_local bool enabled=false;
inline thread_local unsigned calls=0;
inline thread_local std::wstring item, verb;
inline thread_local ULONG mask=0;
inline BOOL WINAPI Execute(SHELLEXECUTEINFOW* info) {
    if (!enabled) return ::ShellExecuteExW(info);
    ++calls; mask=info ? info->fMask : 0;
    verb=info && info->lpVerb ? info->lpVerb : L"";
    item.clear();
    wchar_t path[MAX_PATH]{};
    if (info && info->lpIDList && SHGetPathFromIDListW(static_cast<PCIDLIST_ABSOLUTE>(info->lpIDList),path)) item=path;
    else if (info && info->lpFile) item=info->lpFile;
    SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
}
struct Session {
    Session() {enabled=true; calls=0; item.clear(); verb.clear(); mask=0;}
    ~Session() {enabled=false;}
};
}
#define ShellExecuteExW TabActivationFixture::Execute
