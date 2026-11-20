#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <vector>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
static void check(HRESULT hr, const char* step) {
    if (FAILED(hr)) { fprintf(stderr,"%s: %08lx\n",step,hr); throw std::runtime_error(step); }
}
static bool pumpUntil(auto ready) {
    auto end=GetTickCount64()+15000;
    for (;;) {
        MSG msg;
        while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg); DispatchMessage(&msg);}
        if(ready()) return true;
        auto now=GetTickCount64();
        if(now>=end) return false;
        MsgWaitForMultipleObjectsEx(0,nullptr,DWORD(end-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
}
static bool capture(HWND hwnd, const fs::path& path) {
    RECT r{}; GetClientRect(hwnd,&r);
    BITMAPINFO info{}; auto& h=info.bmiHeader;
    h.biSize=sizeof(h); h.biWidth=r.right; h.biHeight=-r.bottom; h.biPlanes=1; h.biBitCount=32;
    HDC dc=CreateCompatibleDC(nullptr);
    void* pixels=nullptr;
    HBITMAP bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bmp) throw std::runtime_error("bitmap");
    auto old=SelectObject(dc,bmp);
    if(!PrintWindow(hwnd,dc,PW_CLIENTONLY|2)) throw std::runtime_error("PrintWindow");
    GdiFlush();
    BITMAPFILEHEADER file{0x4d42}; file.bfOffBits=sizeof(file)+sizeof(h);
    size_t bytes=size_t(r.right)*r.bottom*4; file.bfSize=file.bfOffBits+bytes;
    const auto* colors=static_cast<const DWORD*>(pixels);
    bool magenta=false; for(size_t i=0;i<bytes/4;i++) if((colors[i]&0xffffff)==0xff00ff) {magenta=true;break;}
    std::ofstream out(path,std::ios::binary);
    out.write((char*)&file,sizeof(file)).write((char*)&h,sizeof(h)).write((char*)pixels,bytes);
    if(!out) throw std::runtime_error("write image");
    SelectObject(dc,old); DeleteObject(bmp); DeleteDC(dc); return magenta;
}
static void probe(const wchar_t* folder,const wchar_t* output) {
    RECT rect{0,0,1250,1800};
    HWND host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"",WS_VISIBLE|WS_POPUP,
        -32000,-32000,rect.right,rect.bottom,nullptr,nullptr,nullptr,nullptr);
    ComPtr<IExplorerBrowser> browser;
    check(CoCreateInstance(CLSID_ExplorerBrowser,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&browser)),"ExplorerBrowser");
    FOLDERSETTINGS settings{FVM_ICON,FWF_NOBROWSERVIEWSTATE|FWF_AUTOARRANGE|FWF_NOGROUPING};
    check(browser->SetOptions(EBO_NOPERSISTVIEWSTATE|EBO_NOBORDER),"options");
    check(browser->Initialize(host,&rect,&settings),"initialize");
    ComPtr<IShellItem> item;
    check(SHCreateItemFromParsingName(folder,nullptr,IID_PPV_ARGS(&item)),"fixture");
    check(browser->BrowseToObject(item.Get(),SBSP_ABSOLUTE),"navigate");
    ComPtr<IFolderView2> view;
    if(!pumpUntil([&]{int n=0;return SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(view.ReleaseAndGetAddressOf()))) &&
        SUCCEEDED(view->ItemCount(SVGIO_ALLVIEW,&n)) && n==12;})) throw std::runtime_error("enumeration");
    ComPtr<IShellView> shell; check(view.As(&shell),"shell view");
    HWND hwnd=nullptr; check(shell->GetWindow(&hwnd),"view window");
    ICONMETRICSW metrics{sizeof(metrics)};
    if(!SystemParametersInfoForDpi(SPI_GETICONMETRICS,sizeof(metrics),&metrics,0,GetDpiForWindow(hwnd))) throw std::runtime_error("icon metrics");
    wprintf(L"DPI %u, icon title %ls height %ld\n",GetDpiForWindow(hwnd),metrics.lfFont.lfFaceName,metrics.lfFont.lfHeight);
    ComPtr<IContextMenu> menu;
    check(shell->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&menu)),"background menu");
    HMENU popup=CreatePopupMenu();
    HRESULT menuResult=menu->QueryContextMenu(popup,0,1,0x7fff,CMF_NORMAL); check(menuResult,"menu");
    for(const char* preset:{"small","medium","large","extralarge"}) {
        std::string verb=std::string("viewlogicalicons")+preset;
        CMINVOKECOMMANDINFO command{sizeof(command)};
        command.hwnd=host; UINT id=0;
        for(;id<HRESULT_CODE(menuResult);++id) {
            wchar_t name[128]{};
            if(SUCCEEDED(menu->GetCommandString(id,GCS_VERBW,nullptr,(char*)name,128)) && std::wstring(verb.begin(),verb.end())==name) break;
        }
        if(id==HRESULT_CODE(menuResult)) throw std::runtime_error("missing icon preset");
        command.lpVerb=MAKEINTRESOURCEA(id); command.nShow=SW_SHOWNOACTIVATE;
        check(menu->InvokeCommand(&command),"native view command");
        int actual=0; FOLDERVIEWMODE mode{};
        const auto path=fs::path(output)/(std::string(preset)+".bmp");
        const bool ready=pumpUntil([&]{
            check(view->GetViewModeAndIconSize(&mode,&actual),"view mode");
            RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
            bool photo=capture(hwnd,path);
            return preset==std::string("small") || photo;
        });
        wprintf(L"%hs mode %d shell size %d thumbnail visible %d\n",preset,mode,actual,ready && mode!=FVM_SMALLICON);
    }
    DestroyMenu(popup);
    browser->Destroy(); DestroyWindow(host);
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3) return 2;
    _wfreopen((fs::path(argv[2])/L"metrics.txt").c_str(),L"w, ccs=UTF-8",stdout); setvbuf(stdout,nullptr,_IONBF,0);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(FAILED(OleInitialize(nullptr))) return 1;
    try { probe(argv[1],argv[2]); } catch(const std::exception& e) {fprintf(stderr,"%s\n",e.what());return 1;}
    OleUninitialize();
}






