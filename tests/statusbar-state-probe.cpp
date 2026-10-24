// Intentional read-only integration probe: calls the production measurement
// and placement functions against visible Explorer chrome. No input, focus,
// selection, process-memory writes or window positioning.
#include <windhawk_api.h>
#ifndef GPU_OVERLAY_SOURCE
#define GPU_OVERLAY_SOURCE "../latest.cpp"
#endif
#include GPU_OVERLAY_SOURCE
#include <iostream>

int wmain() {
    const auto previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WorkerComApartment apartment;
    ComPtr<IUIAutomation> automation;
    unsigned measured=0;
    struct Context {ComPtr<IUIAutomation>& automation; unsigned& measured;} context{automation,measured};
    EnumWindows(+[](HWND frame,LPARAM parameter)->BOOL {
        if(!IsExplorerFrame(frame) || !IsWindowVisible(frame) || IsIconic(frame)) return TRUE;
        auto& context=*reinterpret_cast<Context*>(parameter);
        struct Viewport {HWND overlay=nullptr,native=nullptr;} viewport;
        EnumChildWindows(frame,+[](HWND child,LPARAM parameter)->BOOL {
            auto& viewport=*reinterpret_cast<Viewport*>(parameter);
            wchar_t cls[128]{}; GetClassNameW(child,cls,ARRAYSIZE(cls));
            if(!_wcsicmp(cls,kOverlayClass) && IsWindowVisible(child)) viewport.overlay=child;
            else if(!_wcsicmp(cls,L"SHELLDLL_DefView")) {
                RECT rect{}; GetWindowRect(FolderContentHost(child),&rect);
                if(rect.right>rect.left && rect.bottom>rect.top) viewport.native=child;
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(&viewport));
        const auto query=CaptureStatusButtonQuery(frame,viewport.overlay ? viewport.overlay : viewport.native,
            (GetWindowLongPtrW(frame,GWL_EXSTYLE)&(WS_EX_LAYOUTRTL|WS_EX_RTLREADING))!=0,DpiOf(frame));
        auto snapshot=DiscoverStatusButtonWindows(query);
        if(snapshot.buttons.empty()) ReadVirtualStatusButtons(snapshot,context.automation,[] {return true;});
        RECT placed{}; HWND neighbor=nullptr; int gap=0;
        const bool native=StatusTogglePlacement(snapshot,placed,neighbor,gap);
        std::wcout<<L"STATUS frame="<<frame<<L" dpi="<<query.dpi<<L" source="<<
            (native ? (snapshot.accessible ? L"accessibility" : L"HWND") : L"fallback")<<
            L" host="<<snapshot.providerHost<<L" buttons="<<snapshot.buttons.size()<<L" gap="<<gap<<
            L" result=0x"<<std::hex<<snapshot.result<<std::dec<<L"\n";
        for(const auto& button:snapshot.buttons)
            std::wcout<<L"  NATIVE "<<button.bounds.left<<L","<<button.bounds.top<<L","<<button.bounds.right<<L","<<button.bounds.bottom<<L" hwnd="<<button.hwnd<<L"\n";
        if(native) {
            ++context.measured;
            std::wcout<<L"  PROPOSED "<<placed.left<<L","<<placed.top<<L","<<placed.right<<L","<<placed.bottom<<L" size="<<
                placed.right-placed.left<<L"x"<<placed.bottom-placed.top<<L"\n";
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&context));
    SetThreadDpiAwarenessContext(previous);
    return measured ? 0 : 1;
}
