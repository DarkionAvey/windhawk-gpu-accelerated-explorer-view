// Private ExplorerBrowser controls and menus; no live Explorer or commands.
// Capture USER popup calls before display, and substitute Shell focus imports.
#include <tlhelp32.h>
namespace ItemMenuFixture {
struct Capture {
    HWND owner=nullptr;
    HMENU menu=nullptr;
    UINT flags=0;
    int x=0,y=0,calls=0;
    LPTPMPARAMS parameters=nullptr;
    std::wstring choose;
    UINT chosen=0;
    std::vector<std::wstring> labels;
    static inline thread_local Capture* active=nullptr;
    static void Read(HMENU menu,std::vector<std::wstring>& labels,int depth=0) {
        if(depth>8) return;
        for(int i=0;i<GetMenuItemCount(menu);++i) {
            wchar_t text[1024]{}; GetMenuStringW(menu,i,text,ARRAYSIZE(text),MF_BYPOSITION);
            MENUITEMINFOW item{sizeof(item)}; item.fMask=MIIM_FTYPE|MIIM_SUBMENU;
            GetMenuItemInfoW(menu,i,TRUE,&item);
            labels.push_back(std::to_wstring(depth)+L":"+std::to_wstring(item.fType)+L":"+text);
            if(item.hSubMenu) labels.back()+=L":submenu";
        }
    }
    static BOOL WINAPI Popup(HMENU menu,UINT flags,int x,int y,HWND owner,LPTPMPARAMS parameters) {
        if(!active) return 0;
        auto& c=*active; ++c.calls; c.owner=owner; c.menu=menu; c.flags=flags;
        c.x=x; c.y=y; c.parameters=parameters; c.labels.clear(); Read(menu,c.labels);
        if(!c.choose.empty()) for(int i=0;i<GetMenuItemCount(menu);++i) {
            wchar_t text[1024]{}; GetMenuStringW(menu,i,text,ARRAYSIZE(text),MF_BYPOSITION);
            if(c.choose==text) {c.chosen=GetMenuItemID(menu,i); return c.chosen;}
        }
        return 0; // No real popup. The optional Rename test cancels its editor.
    }
    static BOOL WINAPI OldPopup(HMENU m,UINT f,int x,int y,int,HWND w,const RECT*) { return Popup(m,f,x,y,w,nullptr); }
};
class Imports {
    std::vector<std::pair<void**,void*>> slots;
    static BOOL WINAPI Foreground(HWND) { return FALSE; }
public:
    Imports() {
        const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());
        Check(snapshot!=INVALID_HANDLE_VALUE,"private menu module snapshot");
        MODULEENTRY32W module{sizeof(module)};
        for(BOOL more=Module32FirstW(snapshot,&module);more;more=Module32NextW(snapshot,&module)) {
            if(module.hModule==GetModuleHandleW(nullptr)) continue;
            auto* image=module.modBaseAddr;
            auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
            const auto patch=[&](DWORD namesRva,DWORD functionsRva) {
                if(!namesRva || !functionsRva) return;
                auto* names=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+namesRva);
                auto* functions=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+functionsRva);
                for(;names->u1.AddressOfData;++names,++functions) if(!IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                    const auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+names->u1.AddressOfData);
                    void* replacement=nullptr;
                    if(!strcmp(reinterpret_cast<const char*>(name->Name),"TrackPopupMenu")) replacement=reinterpret_cast<void*>(NativeTrackPopupMenuHook);
                    if(!strcmp(reinterpret_cast<const char*>(name->Name),"TrackPopupMenuEx")) replacement=reinterpret_cast<void*>(NativeTrackPopupMenuExHook);
                    if(!strcmp(reinterpret_cast<const char*>(name->Name),"SetFocus")) replacement=reinterpret_cast<void*>(FocusFixture::Set);
                    if(!strcmp(reinterpret_cast<const char*>(name->Name),"GetFocus")) replacement=reinterpret_cast<void*>(FocusFixture::Focus);
                    if(!strcmp(reinterpret_cast<const char*>(name->Name),"SetForegroundWindow")) replacement=reinterpret_cast<void*>(Foreground);
                    if(!replacement) continue;
                    auto** slot=reinterpret_cast<void**>(&functions->u1.Function);
                    DWORD old=0; Check(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)!=FALSE,"private menu import protection");
                    slots.emplace_back(slot,InterlockedExchangePointer(slot,replacement));
                    DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
                }
            };
            const auto imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
            if(imports.VirtualAddress) for(auto* d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+imports.VirtualAddress);d->Name;++d)
                patch(d->OriginalFirstThunk,d->FirstThunk);
            struct Delay { DWORD attributes,name,module,iat,names,bound,unload,time; };
            const auto delayed=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
            if(delayed.VirtualAddress) for(auto* d=reinterpret_cast<Delay*>(image+delayed.VirtualAddress);d->name;++d)
                if(d->attributes&1) patch(d->names,d->iat);
        }
        CloseHandle(snapshot);
        Check(!slots.empty(),"private menu popup imports absent");
    }
    ~Imports() {
        for(const auto& [slot,original]:slots) {
            DWORD old=0,ignored=0; VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old);
            InterlockedExchangePointer(slot,original); VirtualProtect(slot,sizeof(void*),old,&ignored);
        }
    }
};
struct PopupSession {
    decltype(g_trackPopupMenuOriginal) old=g_trackPopupMenuOriginal;
    decltype(g_trackPopupMenuExOriginal) oldEx=g_trackPopupMenuExOriginal;
    Capture* previous=Capture::active;
    explicit PopupSession(Capture& capture) {
        Capture::active=&capture; g_trackPopupMenuOriginal=Capture::OldPopup; g_trackPopupMenuExOriginal=Capture::Popup;
    }
    ~PopupSession() {g_trackPopupMenuOriginal=old; g_trackPopupMenuExOriginal=oldEx; Capture::active=previous;}
};
}

static void TestNativeItemMenus(const std::wstring& temporary) {
    using namespace ItemMenuFixture;
    const HWND desktopFocus=GetFocus(),desktopForeground=GetForegroundWindow();
    const auto directory=std::filesystem::path(temporary)/L"context-menu-fixture";
    std::filesystem::create_directory(directory);
    ViewState state;
    state.frameHwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private native menus",
        WS_POPUP,-32000,-32000,800,600,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.hwnd=CreateWindowExW(0,L"STATIC",L"private GPU menu owner",WS_CHILD,0,0,800,600,
        state.frameHwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.frameHwnd && state.hwnd,"private menu windows");
    state.background->hwnd=state.hwnd;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    state.presentationOnly=state.presentationActive=state.visible=state.inScope=state.loaded=state.detailsViewActive=true;
    state.folderPath=directory.wstring(); state.token=420;
    for(int i=0;i<128;++i) {
        Entry e; e.folder=i==0; e.name=e.folder ? L"menu-folder" : L"menu-file-"+std::to_wstring(i)+L".txt"; e.path=(directory/e.name).wstring();
        if(e.folder) std::filesystem::create_directory(e.path);
        else {
        HANDLE file=CreateFileW(e.path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"private menu files"); CloseHandle(file);
        }
        state.entryPathIndex.emplace_back(SelectionPathHash(e.path),i);
        state.entries.push_back(std::move(e));
    }
    std::sort(state.entryPathIndex.begin(),state.entryPathIndex.end());
    state.total=static_cast<int>(state.entries.size()); state.selectedFlags.resize(state.total);
    ComPtr<IExplorerBrowser> browser;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ExplorerBrowser,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&browser))),"private menu ExplorerBrowser");
    RECT bounds{0,0,800,600}; FOLDERSETTINGS settings{FVM_DETAILS,FWF_NOBROWSERVIEWSTATE};
    Check(SUCCEEDED(browser->Initialize(state.frameHwnd,&bounds,&settings)),"private menu browser initialize");
    browser->SetOptions(EBO_NOBORDER|EBO_NOTRAVELLOG);
    PIDLIST_ABSOLUTE raw=nullptr;
    Check(SUCCEEDED(SHParseDisplayName(directory.c_str(),nullptr,&raw,0,nullptr)),"private menu folder PIDL");
    OwnedPidl root(raw);
    Check(SUCCEEDED(browser->BrowseToIDList(root.get(),SBSP_ABSOLUTE)),"private menu navigation");
    ComPtr<IShellView> shell;
    const auto pump=[] {MSG m{}; while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&m);};
    Check(Eventually([&] {
        pump(); int count=-1;
        return SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&shell))) && shell &&
            SUCCEEDED(shell.As(&state.folderView)) && SUCCEEDED(state.folderView->ItemCount(SVGIO_ALLVIEW,&count)) && count==state.total;
    },30000),"private menu native enumeration");
    Check(SUCCEEDED(shell->GetWindow(&state.nativeHwnd)) &&
        SUCCEEDED(state.folderView->GetFolder(IID_PPV_ARGS(&state.folder))),"private menu native identity");
    auto* activeBrowser=new TimingShellBrowser; activeBrowser->view=shell; activeBrowser->window=state.frameHwnd;
    state.shellBrowser.Attach(activeBrowser);
    FocusFixture::Session focus(state.frameHwnd,state.hwnd);
    ModifierFixture::Session modifiers;
    Capture capture;
    PopupSession popups(capture);
    // Prime providers before capturing native USER imports; no popup is shown.
    Check(SUCCEEDED(state.folderView->SelectItem(0,SVSI_SELECT|SVSI_DESELECTOTHERS|SVSI_FOCUSED)),"private menu initial native selection");
    ComPtr<IContextMenu> prime;
    Check(SUCCEEDED(shell->GetItemObject(SVGIO_SELECTION,IID_PPV_ARGS(&prime))),"private menu provider warmup");
    HMENU warm=CreatePopupMenu();
    const HRESULT primed=prime->QueryContextMenu(warm,0,1,0x7fff,CMF_NORMAL|CMF_CANRENAME);
    std::wstring renameTitle;
    for(UINT offset=0;SUCCEEDED(primed) && offset<HRESULT_CODE(primed);++offset) {
        wchar_t verb[128]{},label[256]{};
        if(SUCCEEDED(prime->GetCommandString(offset,GCS_VERBW,nullptr,reinterpret_cast<LPSTR>(verb),ARRAYSIZE(verb))) &&
            !_wcsicmp(verb,L"rename") && GetMenuStringW(warm,offset+1,label,ARRAYSIZE(label),MF_BYCOMMAND)) renameTitle=label;
    }
    DestroyMenu(warm); prime.Reset();
    Check(!renameTitle.empty(),"private file provider did not offer native Rename");
    {
        Imports imports;
        for(bool folderFirst : {false,true}) for(int selected : {1,3,128}) for(bool extended : {false,true}) {
            const int focusRow=folderFirst ? 0 : 1;
            state.selectedFlags.Fill(0); for(int i=0;i<selected;++i) state.selectedFlags[size_t((focusRow+i)%state.total)]=1;
            state.selected=state.anchor=focusRow;
            PidlSelection identities;
            Check(CollectSelectedPidls(state,identities),"private menu selected identities");
            auto prepared=std::make_shared<PreparedNativeSelection>();
            prepared->native=state.nativeHwnd; prepared->viewToken=state.token;
            for(int row=0;row<state.total;++row) if(state.selectedFlags[size_t(row)]) {
                if(row==state.selected) prepared->focused=static_cast<int>(prepared->entries.size());
                prepared->entries.push_back(state.entries[size_t(row)]);
            }
            for(auto child:identities.children) prepared->children.emplace_back(ILClone(child));
            // Native controls can acknowledge enumeration before accepting
            // selection. Use the production verified commit once they are ready.
            RestoreNativeContent(state);
            const bool committed=Eventually([&] {pump(); return SyncNativeSelection(state,prepared) && VerifyNativeSelection(state,identities);});
            if(!committed) {
                int actual=-1; state.folderView->ItemCount(SVGIO_SELECTION,&actual);
                std::cout<<"MENU initial selection selected="<<selected<<" folderFirst="<<folderFirst<<" actual="<<actual<<std::endl;
            }
            Check(committed,"private native menu group mismatch");
            BYTE saved[256]{},keys[256]{}; GetKeyboardState(saved); if(extended) keys[VK_SHIFT]=0x80; SetKeyboardState(keys);
            RestoreNativeContent(state);
            capture={};
            SendMessageW(state.nativeHwnd,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(state.nativeHwnd),-1);
            Check(capture.calls==1 && capture.owner==state.nativeHwnd && !capture.labels.empty(),"native selection did not own its popup");
            const auto baseline=capture.labels; const UINT flags=capture.flags;
            Check(SuppressNativeContent(state),"menu native suppression");
            const auto hidden=[&] {
                for(const auto& host:state.suppressedNativeHosts) Check(!(GetWindowLongPtrW(host.host,GWL_STYLE)&WS_VISIBLE),"menu exposed native host");
                for(const auto& viewport:state.suppressedNativeViewports) if(viewport->hwnd) {
                    RECT r{}; GetClientRect(viewport->hwnd,&r); Check(!r.right && !r.bottom,"menu realized native viewport");
                }
            };
            hidden(); capture={};
            const POINT click{-31763,-31829};
            ContextMenu(state,focusRow,click);
            if(capture.calls!=1 || capture.owner!=state.nativeHwnd || capture.labels!=baseline || capture.flags!=flags) {
                std::cout<<"MENU mismatch selected="<<selected<<" extended="<<extended<<" calls="<<capture.calls
                    <<" owner="<<capture.owner<<" expected="<<state.nativeHwnd<<" flags="<<capture.flags<<" expectedFlags="<<flags
                    <<" labels="<<capture.labels.size()<<" expectedLabels="<<baseline.size()<<" unsafe="<<state.nativeSelectionUnsafe
                    <<" pending="<<state.pendingNativeSelection<<" import="<<state.nativeSelectionImportPending
                    <<" loaded="<<state.loaded<<" visible="<<state.visible<<" preview="<<state.namesPreview<<std::endl;
                for(size_t i=0;i<std::max(baseline.size(),capture.labels.size());++i) {
                    std::wcout<<L" expected: "<<(i<baseline.size() ? baseline[i] : L"<none>")
                        <<L" actual: "<<(i<capture.labels.size() ? capture.labels[i] : L"<none>")<<std::endl;
                }
            }
            Check(capture.calls==1 && capture.owner==state.nativeHwnd && capture.labels==baseline && capture.flags==flags,
                "GPU item menu differs from the native Shell view menu");
            Check(capture.x==click.x && capture.y==click.y,"native menu lost the GPU screen anchor");
            Check(VerifyNativeSelection(state,identities),"item menu changed the selected group"); hidden();
            SetKeyboardState(saved);
        }
        // Invoke the real native menu's provider-supplied Rename command, then
        // cancel. Its label is discovered from the canonical verb, not a locale.
        state.selectedFlags.Fill(0); state.selectedFlags[0]=1; state.selected=state.anchor=0;
        state.presentationClient=RECT{0,0,800,600};
        const auto factory=g_writeFactory;
        if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"native menu rename fonts");
        capture={}; capture.choose=renameTitle;
        ContextMenu(state,0,POINT{-31763,-31829});
        Check(capture.chosen!=0,"native menu lost its provider's Rename command");
        const bool renamed=Eventually([&] {
            pump();
            if(NativeRenameEditor(state,FocusFixture::Focus())) AdoptNativeRenameEditor(state,FocusFixture::Focus());
            return state.rename.editor!=nullptr;
        },3000);
        if(!renamed) {
            std::cout<<"MENU Rename focus="<<FocusFixture::Focus()<<" native="<<state.nativeHwnd<<" sets="<<FocusFixture::sets<<std::endl;
            EnumChildWindows(state.nativeHwnd,[](HWND hwnd,LPARAM)->BOOL {
                wchar_t name[128]{}; GetClassNameW(hwnd,name,ARRAYSIZE(name));
                std::wcout<<L" child="<<hwnd<<L" class="<<name<<std::endl; return TRUE;
            },0);
        }
        Check(renamed,"native menu Rename did not reach the shared visible editor");
        FinishRename(state,false); g_writeFactory=factory;
        Check(std::filesystem::exists(state.entries[0].path),"cancelled native menu Rename changed a file");
        capture={}; state.presentationActive=false; ContextMenu(state,0,POINT{10,20});
        Check(!capture.calls,"inactive tab opened a native menu"); state.presentationActive=true;
        activeBrowser->duringQuery=+[](void* p){++static_cast<ViewState*>(p)->token;}; activeBrowser->queryContext=&state;
        ContextMenu(state,0,POINT{10,20}); Check(!capture.calls,"rebound view opened a stale menu");
        activeBrowser->duringQuery=nullptr;
        {
            NativeItemMenuAnchor anchor(state,POINT{-123,456});
            TPMPARAMS parameters{sizeof(parameters)};
            NativeTrackPopupMenuExHook(nullptr,TPM_LAYOUTRTL,5,6,state.hwnd,&parameters);
            Check(capture.x==5 && capture.y==6 && !anchor.tracked,"foreign popup borrowed native anchor");
            NativeTrackPopupMenuExHook(nullptr,TPM_LAYOUTRTL,5,6,state.nativeHwnd,&parameters);
            Check(capture.x==-123 && capture.y==456 && capture.flags==TPM_LAYOUTRTL && capture.parameters==&parameters,"popup hook changed native flags/parameters");
            NativeTrackPopupMenuHook(nullptr,TPM_RIGHTALIGN,7,8,0,state.nativeHwnd,nullptr);
            Check(capture.x==7 && capture.y==8,"later extension popup was repositioned");
        }
        capture={};
        { NativeItemMenuAnchor anchor(state,POINT{1,2}); ++state.token;
          NativeTrackPopupMenuHook(nullptr,0,3,4,0,state.nativeHwnd,nullptr); Check(!capture.calls,"stale menu was tracked"); }
    }
    RestoreNativeContent(state);
    state.shellBrowser.Reset(); state.folderView.Reset(); shell.Reset(); browser->Destroy(); browser.Reset();
    Stop(state.background); UnregisterAnimationWindow(state.hwnd);
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.hwnd); DestroyWindow(state.frameHwnd);
    for(const auto& e:state.entries) std::filesystem::remove(e.path); std::filesystem::remove(directory);
    Check(GetFocus()==desktopFocus && GetForegroundWindow()==desktopForeground,"item menu tests changed desktop focus");
    std::cout<<"PASS: real native/GPU item-menu parity for files/folders, single/multi/128-item selections and native Rename/cancel, Shift menus, native owner/flags, screen anchors, hidden zero-size viewports and stale-tab cancellation\n";
}