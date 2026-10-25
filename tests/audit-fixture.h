// Faults are restricted to explicitly enabled test threads/calls. The real
// desktop, other processes and the production build use the system APIs.
#include <windows.h>
#include <dcomp.h>
#include <dwmapi.h>
#include <commctrl.h>
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
inline std::atomic<HANDLE> watchedWait{nullptr};
inline std::atomic<unsigned> infiniteWaits{0},secondPolls{0};
inline thread_local bool failDispatchWait=false,watchEventRemoval=false;
inline thread_local HANDLE busyFrame=nullptr;
inline thread_local unsigned busyFrameProbes=0;
inline std::atomic<DWORD> busyPresentationThread{0};
inline std::atomic<unsigned> busySharedProbes{0};
inline thread_local HWND rejectSubclass=nullptr;
inline thread_local unsigned eventsRemoved=0,eventRemovalFailures=0;
inline DWORD WINAPI Wait(HANDLE handle,DWORD delay) {
    if (handle==busyFrame && busyFrame && delay==0) {++busyFrameProbes; return WAIT_TIMEOUT;}
    if (delay==0 && handle && GetCurrentThreadId()==busyPresentationThread.load(std::memory_order_relaxed)) {
        ++busySharedProbes; return WAIT_TIMEOUT;
    }
    const HANDLE watched=watchedWait.load(std::memory_order_relaxed);
    if (watched && handle==watched) {
        if (delay==INFINITE) ++infiniteWaits;
        if (delay==1000) ++secondPolls;
    }
    return ::WaitForSingleObject(handle,delay);
}
inline BOOL WINAPI RegisterWait(PHANDLE wait,HANDLE object,WAITORTIMERCALLBACK callback,
        void* context,ULONG delay,ULONG flags) {
    if (failDispatchWait) {SetLastError(ERROR_NOT_ENOUGH_MEMORY); return FALSE;}
    return ::RegisterWaitForSingleObject(wait,object,callback,context,delay,flags);
}
inline BOOL WINAPI Subclass(HWND hwnd,SUBCLASSPROC procedure,UINT_PTR id,DWORD_PTR data) {
    return hwnd==rejectSubclass ? FALSE : ::SetWindowSubclass(hwnd,procedure,id,data);
}
inline BOOL WINAPI UnhookEvent(HWINEVENTHOOK hook) {
    const BOOL result=::UnhookWinEvent(hook);
    if (watchEventRemoval) {++eventsRemoved; if (!result) ++eventRemovalFailures;}
    return result;
}
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
#define WaitForSingleObject AuditFixture::Wait
#define RegisterWaitForSingleObject AuditFixture::RegisterWait
#define SetWindowSubclass AuditFixture::Subclass
#define UnhookWinEvent AuditFixture::UnhookEvent
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
