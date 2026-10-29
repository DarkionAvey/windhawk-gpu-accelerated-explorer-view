// Real read-only ExplorerBrowser controls, owned by this test process. This
// exercises native DirectUI message dispatch, not an IFolderView2 substitute.
class NativeViewportEvents final : public IExplorerBrowserEvents {
    ULONG refs=1;
public:
    ViewState& state;
    bool suppress;
    bool repairChildGap;
    bool navigationComplete=false;
    ComPtr<IFolderView2> view;
    NativeViewportEvents(ViewState& owner,bool enabled,bool repair=false) : state(owner),suppress(enabled),repairChildGap(repair) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr;
        if(iid!=IID_IUnknown && iid!=IID_IExplorerBrowserEvents) return E_NOINTERFACE;
        *out=static_cast<IExplorerBrowserEvents*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE OnNavigationPending(PCIDLIST_ABSOLUTE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnNavigationComplete(PCIDLIST_ABSOLUTE) override { navigationComplete=true; return S_OK; }
    HRESULT STDMETHODCALLTYPE OnNavigationFailed(PCIDLIST_ABSOLUTE) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnViewCreated(IShellView* shell) override {
        shell->QueryInterface(IID_PPV_ARGS(&view)); shell->GetWindow(&state.nativeHwnd);
        if(suppress && !SuppressNativeContent(state)) return E_FAIL;
        if(suppress) {
            // Reproduce an acquired host with a presentation child that was
            // missed by the lifecycle event, using this real private browser.
            auto children=std::move(state.suppressedNativeViewports);
            state.suppressedNativeViewports.clear();
            for(const auto& child : children) {
                RemovePropW(child->hwnd,kNativeViewportProperty);
                RemoveWindowSubclass(child->hwnd,NativeHostSubclass,reinterpret_cast<UINT_PTR>(state.hwnd));
                const auto& r=child->bounds;
                SetWindowPos(child->hwnd,nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,
                    SWP_NOZORDER|SWP_NOACTIVATE);
            }
            if(repairChildGap) Check(SuppressNativeContent(state),"repair real native child gap");
        }
        return S_OK;
    }
};

static void TestRealNativeViewport(const std::wstring& directory) {
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    for(const bool recycle : {false,true}) {
        OwnedPidl root;
        PIDLIST_ABSOLUTE raw=nullptr;
        Check(SUCCEEDED(recycle ? SHGetKnownFolderIDList(FOLDERID_RecycleBinFolder,0,nullptr,&raw) :
            SHParseDisplayName(directory.c_str(),nullptr,&raw,0,nullptr)),"native probe root");
        root.reset(raw);
        // Obtain a complete read-only namespace enumeration. A cold native
        // view can still be adding rows after two seconds; that deadline was
        // accidentally captured as the expected count for the next view.
        ComPtr<IShellItem> rootItem;
        ComPtr<IShellFolder> rootFolder;
        Check(SUCCEEDED(SHCreateItemFromIDList(root.get(),IID_PPV_ARGS(&rootItem))) &&
            SUCCEEDED(rootItem->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&rootFolder))),"native probe namespace binding");
        const auto visibility=ReadShellVisibilityPolicy();
        SHCONTF flags=SHCONTF_FOLDERS|SHCONTF_NONFOLDERS;
        if(visibility.showHidden) flags|=SHCONTF_INCLUDEHIDDEN;
        if(visibility.showProtected) flags|=SHCONTF_INCLUDESUPERHIDDEN;
        ComPtr<IEnumIDList> enumeration;
        Check(SUCCEEDED(rootFolder->EnumObjects(nullptr,flags,&enumeration)),"native probe complete enumeration");
        int baselineCount=0;
        while(enumeration) {
            PITEMID_CHILD child=nullptr;
            const auto hr=enumeration->Next(1,&child,nullptr);
            CoTaskMemFree(child);
            Check(SUCCEEDED(hr),"native probe enumeration failed");
            if(hr==S_FALSE) break;
            ++baselineCount;
        }
        for(const int presentation : {0,1,2}) {
            const bool suppressed=presentation!=0, repaired=presentation==2;
            ViewState state;
            const HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"native viewport test",
                WS_POPUP|WS_VISIBLE,-32000,-32000,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
            state.frameHwnd=frame;
            state.hwnd=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,1000,800,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
            Check(frame && state.hwnd,"native probe windows");
            state.visible=state.inScope=state.detailsViewActive=true;
            SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
            ComPtr<IExplorerBrowser> browser;
            Check(SUCCEEDED(CoCreateInstance(CLSID_ExplorerBrowser,nullptr,CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&browser))),"real ExplorerBrowser");
            RECT bounds{0,0,1000,800}; FOLDERSETTINGS settings{FVM_DETAILS,FWF_NOBROWSERVIEWSTATE};
            Check(SUCCEEDED(browser->Initialize(frame,&bounds,&settings)),"native browser initialize");
            browser->SetOptions(EBO_NOBORDER|EBO_NOTRAVELLOG);
            auto* sink=new NativeViewportEvents(state,suppressed,repaired);
            DWORD cookie=0; Check(SUCCEEDED(browser->Advise(sink,&cookie)),"native browser events");
            Check(SUCCEEDED(browser->BrowseToIDList(root.get(),SBSP_ABSOLUTE)),"native browser navigate");
            double total=0,maximum=0; unsigned calls=0;
            const auto pump=[&] {
                MSG message{};
                while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                    LARGE_INTEGER start{},end{}; QueryPerformanceCounter(&start);
                    if(repaired && message.message>=WM_USER) SuppressDispatchedNativeViewport(message.hwnd);
                    TranslateMessage(&message); DispatchMessageW(&message);
                    QueryPerformanceCounter(&end);
                    if(message.message==0x4c8 && IsChild(state.nativeHwnd,message.hwnd)) {
                        const auto ms=1000.*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
                        maximum=std::max(maximum,ms); total+=ms; ++calls;
                    }
                }
            };
            int count=-1;
            const bool ready=Eventually([&] {
                pump();
                return sink->navigationComplete && sink->view &&
                    SUCCEEDED(sink->view->ItemCount(SVGIO_ALLVIEW,&count)) && count==baselineCount;
            },30000);
            if(!ready) std::cout<<"NATIVE MODEL recycle="<<recycle<<" presentation="<<presentation
                <<" expected="<<baselineCount<<" actual="<<count<<std::endl;
            Check(ready,"hidden native viewport changed the item model or failed to finish enumeration");
            const auto retainedViewports=[&] {
                Check(!state.suppressedNativeViewports.empty(),"native viewports absent after repair");
                for(const auto& child : state.suppressedNativeViewports) {
                    if(!child->hwnd) continue;
                    RECT r{}; GetClientRect(child->hwnd,&r);
                    Check(GetPropW(child->hwnd,kNativeViewportProperty)==child.get() && r.right>0 && r.bottom>0 && !IsWindowVisible(child->hwnd),
                        "hidden native child lost its input layout");
                }
            };
            if(repaired) retainedViewports();
            PROPERTYKEY deleted{}; PSGetPropertyKeyFromName(L"System.Recycle.DateDeleted",&deleted);
            double sortTotal=0,sortMax=0;
            for(const auto key : {PKEY_Size,PKEY_ItemNameDisplay,deleted,PKEY_ItemTypeText}) {
                for(const auto direction : {SORT_ASCENDING,SORT_DESCENDING}) {
                    SORTCOLUMN sort{key,direction};
                    LARGE_INTEGER start{},end{}; QueryPerformanceCounter(&start);
                    const auto hr=sink->view->SetSortColumns(&sort,1);
                    QueryPerformanceCounter(&end);
                    const auto callMs=1000.*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
                    Check(SUCCEEDED(hr),"real native sort failed");
                    sortTotal+=callMs; sortMax=std::max(sortMax,callMs);
                    pump();
                    if(repaired) retainedViewports();
                }
            }
            std::cout<<"NATIVE WORK recycle="<<recycle<<" presentation="<<presentation<<" rows="<<count
                <<" sort-total-ms="<<sortTotal<<" sort-max-ms="<<sortMax
                <<" callbacks="<<calls<<" callback-total-ms="<<total<<" callback-max-ms="<<maximum<<std::endl;
            if(suppressed) {
                // Repair the intentionally unhealthy comparison before checking
                // the existing mode/selection/unload contracts.
                Check(SuppressNativeContent(state),"late native model reacquisition");
                retainedViewports();
                Check(!state.suppressedNativeViewports.empty(),"real DirectUI viewport was not acquired");
                {
                    std::vector<HWND> children;
                    for(const auto& viewport : state.suppressedNativeViewports)
                        if(viewport->hwnd) children.push_back(viewport->hwnd);
                    RestoreNativeContent(state);
                    for(HWND child : children) {
                        RECT restored{}; GetClientRect(child,&restored);
                        Check(restored.right>0 && restored.bottom>0,
                            "natural native restoration left a visible file list at zero size");
                    }
                    Check(SuppressNativeContent(state),"native viewport reacquisition after natural restoration");
                }
                for(const auto mode : {FVM_ICON,FVM_LIST,FVM_DETAILS}) {
                    Check(SUCCEEDED(sink->view->SetViewModeAndIconSize(mode,48)),"native view command failed");
                    pump();
                    FOLDERVIEWMODE actual=FVM_AUTO; int size=0;
                    Check(SUCCEEDED(sink->view->GetViewModeAndIconSize(&actual,&size)) && actual==mode,
                        "suppression changed native view preferences");
                }
                if(count) {
                    const int chosen=count-1;
                    int selected=-1;
                    HRESULT selectionHr=E_PENDING;
                    const bool selectedReady=Eventually([&] {
                        pump();
                        selectionHr=sink->view->SelectItem(chosen,SVSI_DESELECTOTHERS|SVSI_SELECT|SVSI_FOCUSED|SVSI_ENSUREVISIBLE);
                        return SUCCEEDED(selectionHr) && SUCCEEDED(sink->view->GetSelectedItem(-1,&selected)) && selected==chosen;
                    },30000);
                    if(!selectedReady) std::cout<<"NATIVE SELECTION hr=0x"<<std::hex<<static_cast<unsigned>(selectionHr)
                        <<std::dec<<" expected="<<chosen<<" selected="<<selected<<std::endl;
                    Check(selectedReady,"hidden native viewport failed to accept/retain selection after native view changes");
                }
                ComPtr<IShellView> shell;
                Check(SUCCEEDED(sink->view.As(&shell)) && SUCCEEDED(shell->Refresh()),
                    "native model refresh failed while presentation was suppressed");
                Check(Eventually([&] {
                    pump();
                    int refreshed=-1;
                    return SUCCEEDED(sink->view->ItemCount(SVGIO_ALLVIEW,&refreshed)) && refreshed==baselineCount;
                }),"native model did not repopulate after refresh");
                Check(SuppressNativeContent(state),"refresh native child repair");
                retainedViewports();
                for(const auto& viewport : state.suppressedNativeViewports) {
                    if(!viewport->hwnd) continue;
                    NativeSetWindowPosHook(viewport->hwnd,nullptr,7,9,840,620,
                        SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
                    HDWP batch=BeginDeferWindowPos(1);
                    batch=NativeDeferWindowPosHook(batch,viewport->hwnd,nullptr,11,13,860,640,
                        SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
                    Check(batch && EndDeferWindowPos(batch),"native viewport deferred bounds");
                    RECT rect{}; GetClientRect(viewport->hwnd,&rect);
                    Check(rect.right==860 && rect.bottom==640 && !IsWindowVisible(viewport->hwnd),"native layout was altered while hidden");
                }
                std::vector<HWND> children;
                for(const auto& viewport : state.suppressedNativeViewports) if(viewport->hwnd) children.push_back(viewport->hwnd);
                RestoreNativeContent(state);
                for(HWND child : children) {
                    RECT rect{}; GetWindowRect(child,&rect);
                    Check(!GetPropW(child,kNativeViewportProperty) && rect.right-rect.left==860 && rect.bottom-rect.top==640,
                        "native fallback did not restore the latest intended bounds");
                }
                Check(SUCCEEDED(sink->view->ItemCount(SVGIO_ALLVIEW,&count)) && count==baselineCount,
                    "native restoration changed model count");
            }
            std::cout<<"NATIVE VIEWPORT recycle="<<recycle<<" suppressed="<<suppressed<<" rows="<<count
                <<" repaired="<<repaired<<" native-callbacks="<<calls<<" total-ms="<<total<<" max-ms="<<maximum<<std::endl;
            browser->Unadvise(cookie); browser->Destroy(); browser.Reset(); sink->Release(); DestroyWindow(frame);
        }
    }
    std::cout<<"PASS: real native namespace/filesystem models, selection, Details/List/grid commands, constrained geometry and restored viewport\n";
}
