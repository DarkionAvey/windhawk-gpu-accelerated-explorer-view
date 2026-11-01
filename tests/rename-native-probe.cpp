// Native-control integration probe on a desktop which is never displayed.
// No Explorer, user input, folders or clipboard are accessed. EM_SETSEL uses
// its documented API; it does not emulate a drag or prove displayed-frame quality.
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cwchar>
#include <initializer_list>

namespace {
unsigned paints=0, erases=0, colors=0;
LRESULT CALLBACK Observe(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_PAINT) ++paints;
    if(msg==WM_ERASEBKGND) ++erases;
    if(msg==WM_CTLCOLOREDIT) {
        ++colors;
        SetTextColor(reinterpret_cast<HDC>(wp),RGB(255,255,255));
        SetBkColor(reinterpret_cast<HDC>(wp),RGB(0,0,0));
        return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}
bool Probe(bool multiline,bool rtl) {
    HWND parent=CreateWindowExW(WS_EX_NOACTIVATE|(rtl ? WS_EX_LAYOUTRTL : 0),L"STATIC",L"private native rename probe",
        WS_POPUP|WS_VISIBLE,0,0,500,300,nullptr,nullptr,nullptr,nullptr);
    if(!parent) return false;
    SetWindowSubclass(parent,Observe,1,0);
    HWND edit=CreateWindowExW(rtl ? WS_EX_RTLREADING : 0,L"EDIT",L"gpu-accelerated-explorer-view.wh.cpp",
        WS_CHILD|WS_VISIBLE|(rtl ? ES_RIGHT : ES_LEFT)|
            (multiline ? ES_MULTILINE|ES_AUTOVSCROLL : ES_AUTOHSCROLL),
        0,0,130,80,parent,nullptr,nullptr,nullptr);
    if(!edit) {DestroyWindow(parent); return false;}
    SetWindowSubclass(edit,Observe,1,0);
    std::printf("native creation style=%llx ex=%llx\n",static_cast<unsigned long long>(GetWindowLongPtrW(edit,GWL_STYLE)),
        static_cast<unsigned long long>(GetWindowLongPtrW(edit,GWL_EXSTYLE)));
    SendMessageW(edit,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),FALSE);
    SendMessageW(edit,EM_SETSEL,30,0);
    SetFocus(edit);
    SendMessageW(edit,EM_SCROLLCARET,0,0);
    UpdateWindow(edit);
    RECT format{}; SendMessageW(edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
    const LRESULT origin=SendMessageW(edit,EM_POSFROMCHAR,0,0);
    DWORD first=0,last=0;
    SendMessageW(edit,EM_GETSEL,reinterpret_cast<WPARAM>(&first),reinterpret_cast<LPARAM>(&last));
    const LONG x=static_cast<short>(LOWORD(origin)),y=static_cast<short>(HIWORD(origin));
    wchar_t module[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(GetClassLongPtrW(edit,GCLP_HMODULE)),module,MAX_PATH);
    std::wprintf(L"class module=%ls\n",module);
    std::printf("multiline=%d rtl=%d native-focus=%d selection=%lu..%lu first-visible=%lld "
        "prefix=(%ld,%ld) format=(%ld,%ld,%ld,%ld) class-style=%llx\n",multiline,rtl,
        GetFocus()==edit,first,last,static_cast<long long>(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0)),
        x,y,format.left,format.top,format.right,format.bottom,
        static_cast<unsigned long long>(GetClassLongPtrW(edit,GCL_STYLE)));
    const bool valid=GetFocus()==edit && first==0 && last==30 && (!multiline ||
        (origin!=-1 && x>=format.left && x<=format.right && y>=format.top && y<format.bottom));
    paints=erases=colors=0;
    for(int n=1;n<25;++n) {
        SendMessageW(edit,EM_SETSEL,0,n);
        MSG message;
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    std::printf("selection API updates: paints=%u erases=%u color-requests=%u\n",paints,erases,colors);
    DestroyWindow(parent);
    return valid;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 2;
    HDESK previous=GetThreadDesktop(GetCurrentThreadId());
    wchar_t name[80]; swprintf(name,80,L"GpuRenameProbe-%lu",GetCurrentProcessId());
    HDESK isolated=CreateDesktopW(name,nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!isolated) {std::printf("CreateDesktop failed: %lu\n",GetLastError()); return 3;}
    if(!SetThreadDesktop(isolated)) {CloseDesktop(isolated); return 4;}
    bool success=true;
    for(bool multiline:{false,true}) for(bool rtl:{false,true}) success=Probe(multiline,rtl) && success;
    ACTCTXW config{sizeof(config)}; config.lpSource=argv[1];
    HANDLE context=CreateActCtxW(&config); ULONG_PTR cookie=0;
    if(context==INVALID_HANDLE_VALUE) success=false;
    else {
        if(!ActivateActCtx(context,&cookie)) success=false;
        else {
            for(bool multiline:{false,true}) for(bool rtl:{false,true}) success=Probe(multiline,rtl) && success;
            DeactivateActCtx(0,cookie);
        }
        ReleaseActCtx(context);
    }
    if(!SetThreadDesktop(previous)) return 5;
    CloseDesktop(isolated);
    return success ? 0 : 1;
}
