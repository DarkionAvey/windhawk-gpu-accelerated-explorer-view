// Faults are restricted to explicitly enabled test threads/calls. The real
// desktop, other processes and the production build use the system APIs.
#include <windows.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <atomic>
#include <functional>
#include <utility>
namespace AuditFixture {
inline thread_local bool holdThreadCreation=false, failComposition=false;
inline thread_local std::function<void()> compositionProbe;
inline HANDLE threadEntered=nullptr, threadRelease=nullptr;
inline LPTHREAD_START_ROUTINE heldProcedure=nullptr;
using ThreadProbe=void(*)(LPTHREAD_START_ROUTINE,LPVOID);
inline std::atomic<ThreadProbe> threadProbe{nullptr};
inline std::atomic<void(*)()> framePulseProbe{nullptr};
struct ProbeStart {LPTHREAD_START_ROUTINE procedure; LPVOID context; ThreadProbe probe;};
inline DWORD WINAPI ProbeThread(LPVOID value) {
    auto* start=static_cast<ProbeStart*>(value); const auto captured=*start; delete start;
    captured.probe(captured.procedure,captured.context);
    return captured.procedure(captured.context);
}
inline HANDLE WINAPI Thread(LPSECURITY_ATTRIBUTES attributes,SIZE_T stack,
    LPTHREAD_START_ROUTINE procedure,LPVOID context,DWORD flags,LPDWORD id) {
    if (holdThreadCreation && procedure==heldProcedure) {
        SetEvent(threadEntered);
        WaitForSingleObject(threadRelease,INFINITE);
    }
    if(const auto probe=threadProbe.load(std::memory_order_acquire)) {
        auto* start=new ProbeStart{procedure,context,probe};
        HANDLE result=::CreateThread(attributes,stack,ProbeThread,start,flags,id);
        if(!result) delete start;
        return result;
    }
    return ::CreateThread(attributes,stack,procedure,context,flags,id);
}
inline HRESULT WINAPI Composition(IDXGIDevice* device,REFIID iid,void** output) {
    auto probe=std::exchange(compositionProbe,{});
    if (probe) probe();
    if (failComposition) { *output=nullptr; return E_OUTOFMEMORY; }
    return ::DCompositionCreateDevice(device,iid,output);
}
inline HRESULT WINAPI FramePulse() {
    const auto result=::DwmFlush();
    if(const auto probe=framePulseProbe.exchange(nullptr,std::memory_order_acq_rel)) probe();
    return result;
}
}
#define CreateThread AuditFixture::Thread
#define DCompositionCreateDevice AuditFixture::Composition
#define DwmFlush AuditFixture::FramePulse
// Only explicitly enabled private discovery/failure scenarios are substituted.
#include <exdisp.h>
namespace ReportApiFixture {
inline thread_local bool countShellWindows=false, failEvents=false;
inline thread_local unsigned shellWindowsCalls=0;
inline thread_local bool suppressRenameError=false;
inline thread_local unsigned renameErrors=0;
inline int WINAPI Message(HWND window,LPCWSTR text,LPCWSTR title,UINT flags) {
    if (suppressRenameError && !wcscmp(title,L"Rename")) {++renameErrors; return IDOK;}
    return ::MessageBoxW(window,text,title,flags);
}
inline HRESULT WINAPI Create(REFCLSID cls,IUnknown* outer,DWORD flags,REFIID iid,void** result) {
    if (countShellWindows && cls==CLSID_ShellWindows) {++shellWindowsCalls; *result=nullptr; return E_FAIL;}
    return ::CoCreateInstance(cls,outer,flags,iid,result);
}
inline HWINEVENTHOOK WINAPI Event(DWORD first,DWORD last,HMODULE module,WINEVENTPROC callback,
        DWORD process,DWORD thread,DWORD flags) {
    if (failEvents) return nullptr;
    return ::SetWinEventHook(first,last,module,callback,process,thread,flags);
}
}
#define CoCreateInstance ReportApiFixture::Create
#define SetWinEventHook ReportApiFixture::Event
#define MessageBoxW ReportApiFixture::Message
