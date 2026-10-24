// Intentional read-only OS integration probe. Never injects, suspends, writes
// process memory, sends window messages, changes focus or accesses file names.
#include <windhawk_api.h>
#ifndef GPU_OVERLAY_SOURCE
#define GPU_OVERLAY_SOURCE "../latest.cpp"
#endif
#include GPU_OVERLAY_SOURCE
#include <psapi.h>
#include <iostream>
#include <sstream>

int wmain() {
    struct Target {HWND hwnd; DWORD pid;}; std::vector<Target> targets;
    EnumWindows(+[](HWND frame,LPARAM data)->BOOL {
        if(!IsExplorerFrame(frame)) return TRUE;
        DWORD pid=0; GetWindowThreadProcessId(frame,&pid);
        std::wcout<<L"EXPLORER_FRAME hwnd="<<frame<<L" pid="<<pid<<L"\n";
        EnumChildWindows(frame,+[](HWND hwnd,LPARAM data)->BOOL {
            wchar_t cls[128]{}; GetClassNameW(hwnd,cls,ARRAYSIZE(cls));
            if(std::wstring_view(cls).find(L"Windhawk")!=std::wstring_view::npos)
                std::wcout<<L"OVERLAY_CLASS hwnd="<<hwnd<<L" class="<<cls<<L"\n";
            if(wcscmp(cls,kOverlayClass)) return TRUE;
            DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
            reinterpret_cast<std::vector<Target>*>(data)->push_back({hwnd,pid}); return TRUE;
        },data); return TRUE;
    },reinterpret_cast<LPARAM>(&targets));
    ViewState layout;
    const auto offset=[&](auto member) {return reinterpret_cast<const BYTE*>(&(layout.*member))-reinterpret_cast<const BYTE*>(&layout);};
    std::wcout<<L"READ_ONLY_COLUMN_STATE version="<<GPU_COLUMN_PROBE_VERSION<<L" windows="<<targets.size()<<L"\n";
    unsigned matched=0;
    for(const auto& target:targets) {
        HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_INFORMATION,FALSE,target.pid);
        if(!process) continue;
        HMODULE modules[2048]{}; DWORD bytes=0; bool version=false;
        const std::wstring expected=std::wstring(L"local@gpu-accelerated-explorer-view_")+GPU_COLUMN_PROBE_VERSION+L"_";
        if(EnumProcessModules(process,modules,sizeof(modules),&bytes))
            for(size_t i=0;i<std::min<size_t>(bytes/sizeof(HMODULE),ARRAYSIZE(modules));++i) {
                wchar_t path[32768]{};
                if(GetModuleFileNameExW(process,modules[i],path,ARRAYSIZE(path)) && std::wstring_view(path).find(expected)!=std::wstring::npos) version=true;
            }
        const auto base=ULONG_PTR(GetWindowLongPtrW(target.hwnd,GWLP_USERDATA));
        const auto read=[&](auto member,auto& value) {
            SIZE_T bytes=0; return ReadProcessMemory(process,reinterpret_cast<void*>(base+offset(member)),&value,sizeof(value),&bytes) && bytes==sizeof(value);
        };
        HWND self=nullptr;
        if(!version || !base || !read(&ViewState::hwnd,self) || self!=target.hwnd) {CloseHandle(process); continue;}
        ++matched;
        std::wostringstream out; out<<L"pid="<<target.pid<<L" hwnd="<<target.hwnd<<L" shown="<<IsWindowVisible(target.hwnd);
#define FIELD(name) {decltype(layout.name) value{}; if(read(&ViewState::name,value)) out<<L" "<<#name<<L"="<<value;}
        FIELD(visible) FIELD(inScope) FIELD(columnsView) FIELD(gridView) FIELD(headerPx)
        FIELD(total) FIELD(loaded) FIELD(namesPreview) FIELD(directoryPending) FIELD(firstContentSubmittedAt)
        FIELD(featureInitializationStep) FIELD(featureInitializationQueued) FIELD(deferColumnProviders)
        FIELD(nativeColumnsDirty) FIELD(lastColumnsProbe) FIELD(columnModeTransition) FIELD(columnTransitionActive)
        FIELD(columnPresentationPending) FIELD(rowTransitionActive) FIELD(hoverAnimationActive)
        FIELD(animationActive) FIELD(horizontalAnimating) FIELD(keyboardSelectionAnimating) FIELD(zoomAnimating)
        FIELD(autoRefitPending) FIELD(autoRefitForResize) FIELD(pendingNavigationPaint)
        FIELD(nativeViewCommandPending) FIELD(nativeModeApplying) FIELD(viewControlDispatchDepth)
        FIELD(mousePressActive) FIELD(columnWiggleAwaitRelease) FIELD(manipulationFrameActive)
        FIELD(columnContentWidth) FIELD(presentationSequence) FIELD(presentationAcceptedSequence)
#undef FIELD
        struct Range {ULONG_PTR begin=0,end=0,capacity=0;} columns;
        SIZE_T got=0;
        if(ReadProcessMemory(process,reinterpret_cast<void*>(base+offset(&ViewState::columns)),&columns,sizeof(columns),&got) && got==sizeof(columns))
            out<<L" columns="<<(columns.end-columns.begin)/sizeof(ExplorerColumn);
        RECT bounds{}; GetClientRect(target.hwnd,&bounds); out<<L" viewport="<<bounds.right<<L","<<bounds.bottom;
        std::wcout<<out.str()<<L"\n"; CloseHandle(process);
    }
    std::wcout<<L"MATCHED_SOURCE_LAYOUTS="<<matched<<L"\n";
    return matched ? 0 : 1;
}
