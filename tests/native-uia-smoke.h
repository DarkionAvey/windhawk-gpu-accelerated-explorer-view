class UiaBatchProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot {
    ULONG refs=1;
    ComPtr<UiaBatchProvider> parent;
public:
    HWND hwnd;
    unsigned windowReads=0;
    unsigned rootReads=0;
    std::function<void()> onWindowRead;
    ComPtr<IRawElementProviderSimple> windowHost;
    explicit UiaBatchProvider(HWND target,UiaBatchProvider* root=nullptr) : parent(root),hwnd(target) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr;
        if(iid==IID_IUnknown || iid==__uuidof(IRawElementProviderSimple)) *out=static_cast<IRawElementProviderSimple*>(this);
        else if(iid==__uuidof(IRawElementProviderFragment)) *out=static_cast<IRawElementProviderFragment*>(this);
        else if(iid==__uuidof(IRawElementProviderFragmentRoot) && !parent) *out=static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override { *out=ProviderOptions_ServerSideProvider; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID,IUnknown** out) override { *out=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property,VARIANT* out) override {
        VariantInit(out);
        if(property==UIA_NativeWindowHandlePropertyId) {
            ++windowReads;
            if(onWindowRead) onWindowRead();
            if(hwnd) {out->vt=VT_I4; out->lVal=static_cast<LONG>(reinterpret_cast<UINT_PTR>(hwnd));}
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override { return windowHost.CopyTo(out); }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection,IRawElementProviderFragment** out) override { *out=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override { *out=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override { *out={0,0,0,0}; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override { *out=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        ++rootReads;
        *out=static_cast<IRawElementProviderFragmentRoot*>(parent ? parent.Get() : this); (*out)->AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double,double,IRawElementProviderFragment** out) override { *out=nullptr; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override { *out=nullptr; return S_OK; }
};
struct CapturedUiaStructure {
    IRawElementProviderSimple* provider;
    StructureChangeType kind;
    std::vector<int> runtime;
};
static std::vector<CapturedUiaStructure>* capturedUiaStructure=nullptr;
static HRESULT WINAPI CaptureUiaStructure(IRawElementProviderSimple* provider,StructureChangeType kind,int* runtime,int count) {
    capturedUiaStructure->push_back({provider,kind,count ? std::vector<int>(runtime,runtime+count) : std::vector<int>{}});
    return S_FALSE;
}
static void TestNativeUiaBatch() {
    ViewState state;
    state.frameHwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"UIA batch fixture",
        WS_POPUP|WS_VISIBLE,-32000,-32000,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND host=CreateWindowExW(0,L"CtrlNotifySink",L"",WS_CHILD|WS_VISIBLE,0,0,640,480,state.frameHwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.nativeHwnd=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,0,0,640,480,host,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND list=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE,0,0,640,480,state.nativeHwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.hwnd=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,640,480,state.frameHwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.hwnd && state.nativeHwnd && list,"UIA batch windows");
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    state.visible=state.inScope=state.detailsViewActive=true;
    Check(SuppressNativeContent(state),"UIA batch suppression");
    ComPtr<UiaBatchProvider> root,child,foreign,windowHost;
    windowHost.Attach(new UiaBatchProvider(list));
    root.Attach(new UiaBatchProvider(nullptr)); root->windowHost=windowHost;
    child.Attach(new UiaBatchProvider(nullptr,root.Get()));
    foreign.Attach(new UiaBatchProvider(state.frameHwnd));
    std::vector<CapturedUiaStructure> observed;
    const auto previous=g_raiseStructureEventOriginal;
    g_raiseStructureEventOriginal=CaptureUiaStructure; capturedUiaStructure=&observed;
    int runtime[]{UiaAppendRuntimeId,47};
    Check(RaiseStructureEventHook(child.Get(),StructureChangeType_ChildRemoved,runtime,2)==S_FALSE && observed.size()==1,
        "unscoped UIA event was changed");
    observed.clear();
    {
        NativeStructureBatch batch(list);
        Check(RaiseStructureEventHook(child.Get(),StructureChangeType_ChildRemoved,runtime,2)==S_OK && observed.empty(),
            "native UIA event was not deferred to actual dispatch completion");
    }
    Check(observed.size()==1 && observed[0].provider==child.Get() && observed[0].kind==StructureChangeType_ChildRemoved &&
        observed[0].runtime==std::vector<int>(runtime,runtime+2),"single UIA event changed semantics");
    observed.clear(); root->windowReads=0;
    {
        NativeStructureBatch batch(list);
        for(int i=0;i<818;++i) {
            NativeStructureBatch nested(list);
            Check(RaiseStructureEventHook(child.Get(),StructureChangeType_ChildAdded,nullptr,0)==S_OK,"native UIA batch record");
        }
        Check(observed.empty() && root->windowReads==1,"nested native batches flushed early or reread the root HWND");
        Check(RaiseStructureEventHook(foreign.Get(),StructureChangeType_ChildrenInvalidated,nullptr,0)==S_FALSE && observed.size()==1,
            "native batch swallowed a foreign chrome event");
        { NativeStructureBatch other(state.frameHwnd); RaiseStructureEventHook(child.Get(),StructureChangeType_ChildAdded,nullptr,0); }
        Check(observed.size()==2,"nested foreign dispatch inherited native batching");
    }
    Check(observed.size()==3 && observed.back().provider==root.Get() && observed.back().kind==StructureChangeType_ChildrenInvalidated &&
        observed.back().runtime.empty(),"native row burst did not publish one bulk invalidation");
    observed.clear(); root->rootReads=root->windowReads=0;
    const bool savedTrace=g_pipelineTracing.exchange(true);
    state.background->pipeline=std::make_unique<PipelineTrace>();
    state.background->pipelinePublished=state.background->pipeline.get();
    {
        NativeStructureBatch batch(list);
        for(int i=0;i<600;++i)
            Check(RaiseStructureEventHook(root.Get(),StructureChangeType_ChildRemoved,runtime,2)==S_OK,"native root burst");
        Check(root->rootReads==1 && root->windowReads==1,
            "repeated verified native roots re-entered their provider for every row");
        // The fast path retains exact-root identity only; other providers must
        // still prove their fragment/HWND ownership on every event.
        const auto childReads=child->rootReads;
        for(int i=0;i<3;++i) RaiseStructureEventHook(child.Get(),StructureChangeType_ChildAdded,nullptr,0);
        Check(child->rootReads==childReads+3,"child providers inherited cached root ownership");
        Check(RaiseStructureEventHook(foreign.Get(),StructureChangeType_ChildAdded,nullptr,0)==S_FALSE,
            "verified root fast path swallowed a foreign provider");
    }
    Check(observed.size()==2 && observed.back().provider==root.Get() &&
        observed.back().kind==StructureChangeType_ChildrenInvalidated,"verified-root burst lost final bulk notification");
    std::array<PipelineRecord,8> diagnostics;
    bool resolved=false;
    const auto records=state.background->pipeline->Drain(diagnostics.data(),diagnostics.size());
    for(size_t i=0;i<records;++i) if(wcscmp(diagnostics[i].operation,L"native.uia.resolve[queries,rootHits,lookupMs,scopeMs]")==0) {
        const auto& event=diagnostics[i];
        Check(event.v[0]==5 && event.v[1]==599 && event.v[2]>=0 && event.v[3]>=event.v[2],
            "native UIA diagnostics lost lookup/reuse/scope correlation");
        resolved=true;
    }
    Check(resolved,"native UIA lookup diagnostics missing");
    g_pipelineTracing=savedTrace;
    state.background->pipelinePublished=nullptr; state.background->pipeline.reset();
    observed.clear();
    {
        NativeStructureBatch batch(list);
        RaiseStructureEventHook(child.Get(),StructureChangeType_ChildAdded,nullptr,0);
        ++state.token;
    }
    Check(observed.empty(),"cancelled native generation emitted stale UIA structure");
    {
        NativeStructureBatch batch(list);
        root->onWindowRead=[&] { SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); };
        Check(RaiseStructureEventHook(child.Get(),StructureChangeType_ChildAdded,nullptr,0)==S_FALSE && observed.size()==1,
            "provider reentrancy borrowed a destroyed/replaced ViewState");
        root->onWindowRead={};
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    }
    observed.clear();
    {
        NativeStructureBatch batch(list);
        g_unloading=true;
        Check(RaiseStructureEventHook(child.Get(),StructureChangeType_ChildAdded,nullptr,0)==S_FALSE,"unload did not bypass UIA batching");
        g_unloading=false;
    }
    Check(observed.size()==1 && !t_nativeStructureBatch,"UIA batch retained TLS or failed unload passthrough");
    observed.clear();
    {
        NativeStructureBatch ownerBatch(list);
        HANDLE foreignThread=CreateThread(nullptr,0,[](void* target)->DWORD {
            NativeStructureBatch other(static_cast<HWND>(target));
            return RaiseStructureEventHook(nullptr,StructureChangeType_ChildrenInvalidated,nullptr,0)==S_FALSE &&
                !t_nativeStructureBatch ? 0 : 1;
        },list,0,nullptr);
        Check(foreignThread && WaitForSingleObject(foreignThread,5000)==WAIT_OBJECT_0,"UIA foreign-thread fixture");
        DWORD result=1; GetExitCodeThread(foreignThread,&result); CloseHandle(foreignThread);
        Check(result==0 && observed.size()==1,"a foreign thread borrowed native UIA ownership/TLS");
    }
    g_raiseStructureEventOriginal=previous; capturedUiaStructure=nullptr;
    RestoreNativeContent(state);
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.frameHwnd);
    std::cout<<"PASS: native UIA bursts coalesce at real dispatch completion; exact singleton/runtime IDs, root-only verification, nested/foreign scope, cancellation and unload passthrough\n";
}

// Substitute this import only in the standalone test process. The actual
// DirectUI provider and UI Automation client/core remain Windows components.
class PrivateUiaImport {
    std::vector<std::pair<void**,void*>> slots;
    void Patch(void** slot,void* replacement) {
        DWORD old=0; Check(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)!=FALSE,"private UIA import protection");
        slots.emplace_back(slot,InterlockedExchangePointer(slot,replacement));
        DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
    }
public:
    explicit PrivateUiaImport(void* replacement) {
        auto* image=reinterpret_cast<unsigned char*>(GetModuleHandleW(L"DUI70.dll"));
        Check(image!=nullptr,"real DirectUI module");
        auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
        const auto imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if(imports.VirtualAddress) for(auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+imports.VirtualAddress);
            descriptor->Name;++descriptor) {
            if(!descriptor->OriginalFirstThunk) continue;
            auto* names=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+descriptor->OriginalFirstThunk);
            auto* functions=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+descriptor->FirstThunk);
            for(;names->u1.AddressOfData;++names,++functions) if(!IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                const auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+names->u1.AddressOfData);
                if(!strcmp(reinterpret_cast<const char*>(name->Name),"UiaRaiseStructureChangedEvent"))
                    Patch(reinterpret_cast<void**>(&functions->u1.Function),replacement);
            }
        }
        struct Delay { DWORD attributes,name,module,iat,names,bound,unload,time; };
        const auto delayed=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if(delayed.VirtualAddress) for(auto* descriptor=reinterpret_cast<Delay*>(image+delayed.VirtualAddress);
            descriptor->name;++descriptor) {
            if(!(descriptor->attributes&1) || !descriptor->names) continue;
            auto* names=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+descriptor->names);
            auto* functions=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+descriptor->iat);
            for(;names->u1.AddressOfData;++names,++functions) if(!IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                const auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+names->u1.AddressOfData);
                if(!strcmp(reinterpret_cast<const char*>(name->Name),"UiaRaiseStructureChangedEvent"))
                    Patch(reinterpret_cast<void**>(&functions->u1.Function),replacement);
            }
        }
        // Current DirectUI builds resolve UIA lazily and cache API pointers in
        // writable image data rather than the PE import table. Match the exact
        // resolved export, not instructions, signatures or private symbols.
        if(slots.empty()) {
            const auto original=GetProcAddress(GetModuleHandleW(L"UIAutomationCore.dll"),"UiaRaiseStructureChangedEvent");
            const auto* section=IMAGE_FIRST_SECTION(nt);
            for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++section) {
                if(!(section->Characteristics&IMAGE_SCN_MEM_WRITE) || !(section->Characteristics&IMAGE_SCN_CNT_INITIALIZED_DATA)) continue;
                auto** begin=reinterpret_cast<void**>(image+section->VirtualAddress);
                const size_t count=section->Misc.VirtualSize/sizeof(void*);
                for(size_t slot=0;slot<count;++slot)
                    if(begin[slot]==reinterpret_cast<void*>(original)) Patch(begin+slot,replacement);
            }
        }
        Check(!slots.empty(),"real DirectUI structure-event import/cache not found");
    }
    ~PrivateUiaImport() {
        for(const auto& [slot,original] : slots) {
            DWORD old=0; VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old);
            InterlockedExchangePointer(slot,original);
            DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
        }
    }
};
class NativeUiaListener final : public IUIAutomationStructureChangedEventHandler {
    std::atomic<ULONG> refs{1};
public:
    std::atomic<unsigned> received{0};
    std::atomic<unsigned> bulkReceived{0},bulkCached{0};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr;
        if(iid!=IID_IUnknown && iid!=__uuidof(IUIAutomationStructureChangedEventHandler)) return E_NOINTERFACE;
        *out=static_cast<IUIAutomationStructureChangedEventHandler*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement* sender,StructureChangeType kind,SAFEARRAY*) override {
        ++received;
        if(kind==StructureChangeType_ChildrenInvalidated) {
            BSTR name=nullptr;
            if(SUCCEEDED(sender->get_CachedName(&name))) ++bulkCached;
            SysFreeString(name);
            ++bulkReceived;
        }
        return S_OK;
    }
};
// UIA's legacy WinEvent bridge otherwise subscribes to the whole desktop even
// for an element-scoped client. Unregistration then waits behind MSAA probes of
// unrelated (possibly hung) apps. This private fixture only owns this process:
// filter the real bridge's OS subscriptions, without substituting its events,
// providers, cache requests, delivery or removal.
class PrivateUiaWinEvents {
    std::vector<std::pair<void**,void*>> slots;
    static HWINEVENTHOOK WINAPI Subscribe(DWORD first,DWORD last,HMODULE module,WINEVENTPROC callback,
        DWORD process,DWORD thread,DWORD flags) {
        return SetWinEventHook(first,last,module,callback,process ? process : GetCurrentProcessId(),thread,flags);
    }
public:
    explicit PrivateUiaWinEvents(HMODULE module) {
        auto* image=reinterpret_cast<unsigned char*>(module);
        auto* nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
        const auto imports=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        const auto patch=[&](DWORD namesRva,DWORD functionsRva) {
            if(!namesRva || !functionsRva) return;
            auto* names=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+namesRva);
            auto* functions=reinterpret_cast<IMAGE_THUNK_DATA64*>(image+functionsRva);
            for(;names->u1.AddressOfData;++names,++functions) if(!IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                const auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+names->u1.AddressOfData);
                if(strcmp(reinterpret_cast<const char*>(name->Name),"SetWinEventHook")) continue;
                auto** slot=reinterpret_cast<void**>(&functions->u1.Function);
                DWORD old=0; Check(VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old)!=FALSE,"UIA WinEvent protection");
                slots.emplace_back(slot,InterlockedExchangePointer(slot,reinterpret_cast<void*>(Subscribe)));
                DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
            }
        };
        if(imports.VirtualAddress) for(auto* descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+imports.VirtualAddress);
            descriptor->Name;++descriptor) patch(descriptor->OriginalFirstThunk,descriptor->FirstThunk);
        struct Delay { DWORD attributes,name,module,iat,names,bound,unload,time; };
        const auto delayed=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if(delayed.VirtualAddress) for(auto* descriptor=reinterpret_cast<Delay*>(image+delayed.VirtualAddress);
            descriptor->name;++descriptor) {
            if(descriptor->attributes&1) patch(descriptor->names,descriptor->iat);
        }
        Check(!slots.empty(),"UIA WinEvent import not found");
    }
    ~PrivateUiaWinEvents() {
        for(const auto& [slot,original] : slots) {
            DWORD old=0; VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old);
            InterlockedExchangePointer(slot,original);
            DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
        }
    }
};
static RaiseStructureEvent_t actualUiaRaise=nullptr;
static bool realUiaBatchEnabled=false;
static unsigned realUiaInput=0,realUiaOutput=0;
static std::unordered_map<IRawElementProviderSimple*,unsigned> realUiaProviders;
static LONGLONG realUiaHookTicks=0;
static HRESULT WINAPI ObserveRealUia(IRawElementProviderSimple* p,StructureChangeType k,int* r,int n) {
    ++realUiaOutput; return actualUiaRaise(p,k,r,n);
}
static HRESULT WINAPI RouteRealUia(IRawElementProviderSimple* p,StructureChangeType k,int* r,int n) {
    ++realUiaInput;
    ++realUiaProviders[p];
    LARGE_INTEGER start{},end{}; QueryPerformanceCounter(&start);
    const HRESULT result=realUiaBatchEnabled ? RaiseStructureEventHook(p,k,r,n) : ObserveRealUia(p,k,r,n);
    QueryPerformanceCounter(&end); realUiaHookTicks+=end.QuadPart-start.QuadPart;
    return result;
}
static void TestRealNativeUiaPressure(const std::wstring& directory) {
    struct ModuleReference {
        HMODULE value=LoadLibraryExW(L"UIAutomationCore.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        ~ModuleReference() { if(value) FreeLibrary(value); }
    } module;
    HMODULE uia=module.value;
    Check(uia!=nullptr,"UIA pressure module");
    PrivateUiaWinEvents privateEvents(uia);
    struct Browser {
        ViewState state;
        ComPtr<IExplorerBrowser> browser;
        NativeViewportEvents* sink=nullptr;
        DWORD cookie=0;
    };
    std::array<Browser,3> tabs;
    const auto pump=[] {
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&msg);
            NativeStructureBatch batch(msg.hwnd);
            DispatchMessageW(&msg);
        }
    };
    OwnedPidl folder; PIDLIST_ABSOLUTE parsed=nullptr;
    Check(SUCCEEDED(SHParseDisplayName(directory.c_str(),nullptr,&parsed,0,nullptr)),"UIA pressure directory"); folder.reset(parsed);
    std::vector<HWND> handles;
    for(auto& tab : tabs) {
        auto& s=tab.state;
        s.frameHwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"UIA native pressure",
            WS_POPUP|WS_VISIBLE,-32000,-32000,1000,800,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        s.hwnd=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,1000,800,s.frameHwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
        s.visible=s.inScope=s.detailsViewActive=true;
        SetWindowLongPtrW(s.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        Check(SUCCEEDED(CoCreateInstance(CLSID_ExplorerBrowser,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&tab.browser))),"UIA native browser");
        RECT bounds{0,0,1000,800}; FOLDERSETTINGS settings{FVM_DETAILS,FWF_NOBROWSERVIEWSTATE};
        Check(SUCCEEDED(tab.browser->Initialize(s.frameHwnd,&bounds,&settings)),"UIA native initialize");
        tab.browser->SetOptions(EBO_NOBORDER|EBO_NOTRAVELLOG);
        tab.sink=new NativeViewportEvents(s,false);
        tab.browser->Advise(tab.sink,&tab.cookie);
        Check(SUCCEEDED(tab.browser->BrowseToIDList(folder.get(),SBSP_ABSOLUTE)),"UIA native browse");
        Check(Eventually([&] {pump(); int n=0; return tab.sink->view && SUCCEEDED(tab.sink->view->ItemCount(SVGIO_ALLVIEW,&n)) && n==512;}),"UIA native names");
        HWND child=FindWindowExW(s.nativeHwnd,nullptr,L"DirectUIHWND",nullptr);
        Check(child!=nullptr,"UIA real native child"); handles.push_back(child);
        Check(SuppressNativeContent(s),"UIA pressure native suppression");
    }
    struct Client {
        std::vector<HWND>* handles;
        HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr),stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        HRESULT result=E_PENDING;
        std::atomic<unsigned> cleanup{0};
        NativeUiaListener* listener=nullptr;
    } client{&handles};
    HANDLE worker=CreateThread(nullptr,0,[](void* context)->DWORD {
        auto& c=*static_cast<Client*>(context);
        const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        // Construct, register, unregister and release the handler on its MTA.
        // The owning STA only observes its atomics before requesting shutdown.
        c.listener=new NativeUiaListener;
        ComPtr<IUIAutomation> automation;
        c.result=CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation));
        ComPtr<IUIAutomationCacheRequest> cache;
        std::vector<ComPtr<IUIAutomationElement>> elements;
        if(SUCCEEDED(c.result)) c.result=automation->CreateCacheRequest(&cache);
        if(cache) {
            cache->AddProperty(UIA_NamePropertyId); cache->AddProperty(UIA_IsOffscreenPropertyId);
            cache->put_TreeScope(TreeScope_Subtree);
            cache->put_AutomationElementMode(AutomationElementMode_None);
        }
        for(HWND hwnd : *c.handles) {
            if(FAILED(c.result)) break;
            ComPtr<IUIAutomationElement> element;
            c.result=automation->ElementFromHandle(hwnd,&element);
            if(SUCCEEDED(c.result)) c.result=automation->AddStructureChangedEventHandler(element.Get(),TreeScope_Subtree,cache.Get(),c.listener);
            elements.push_back(std::move(element));
        }
        SetEvent(c.ready); WaitForSingleObject(c.stop,INFINITE);
        c.cleanup=1;
        if(automation) for(const auto& element : elements) {
            const HRESULT removed=automation->RemoveStructureChangedEventHandler(element.Get(),c.listener);
            if(FAILED(removed)) c.result=removed;
            ++c.cleanup;
        }
        c.cleanup=5; elements.clear(); c.cleanup=6; cache.Reset(); c.cleanup=7; automation.Reset();
        c.listener->Release();
        c.cleanup=8; if(SUCCEEDED(initialized)) CoUninitialize(); c.cleanup=9;
        return 0;
    },&client,0,nullptr);
    Check(worker && Eventually([&] {pump(); return WaitForSingleObject(client.ready,0)==WAIT_OBJECT_0;}),"UIA listener setup timeout");
    Check(SUCCEEDED(client.result),"real UIA listener registration");
    // Make the native provider resolve its optional UIA entry points before
    // substituting that pointer in this private process.
    for(auto& tab : tabs) {
        tab.sink->view->SetViewModeAndIconSize(FVM_ICON,48); pump();
        tab.sink->view->SetViewModeAndIconSize(FVM_DETAILS,48); pump();
    }
    actualUiaRaise=reinterpret_cast<RaiseStructureEvent_t>(GetProcAddress(uia,"UiaRaiseStructureChangedEvent"));
    Check(actualUiaRaise!=nullptr,"real UIA structure API");
    const auto saved=g_raiseStructureEventOriginal;
    g_raiseStructureEventOriginal=ObserveRealUia;
    const bool savedTrace=g_pipelineTracing.exchange(true);
    for(auto& tab : tabs) {
        tab.state.background->pipeline=std::make_unique<PipelineTrace>();
        tab.state.background->pipelinePublished=tab.state.background->pipeline.get();
    }
    {
        PrivateUiaImport imports(reinterpret_cast<void*>(RouteRealUia));
        for(bool batching : {true,false}) {
            realUiaBatchEnabled=batching; realUiaInput=realUiaOutput=0;
            realUiaProviders.clear(); realUiaHookTicks=0;
            const auto cachedBefore=client.listener->bulkCached.load();
            LARGE_INTEGER start{},end{},frequency{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&start);
            for(size_t i=0;i<tabs.size();++i) {
                auto& tab=tabs[i];
                for(UINT mode : {FVM_ICON,FVM_DETAILS,FVM_LIST,FVM_DETAILS}) {
                    NativeStructureBatch batch(handles[i]);
                    Check(SUCCEEDED(tab.sink->view->SetViewModeAndIconSize(static_cast<FOLDERVIEWMODE>(mode),48)),"UIA native mode change");
                    pump();
                }
            }
            pump(); QueryPerformanceCounter(&end);
            std::cout<<"NATIVE UIA pressure batching="<<batching<<" input-events="<<realUiaInput<<" output-events="<<realUiaOutput
                <<" elapsed-ms="<<1000.*(end.QuadPart-start.QuadPart)/frequency.QuadPart<<" client-events="<<client.listener->received.load()<<std::endl;
            std::cout<<"NATIVE UIA providers="<<realUiaProviders.size()<<" hook-ms="
                <<1000.*realUiaHookTicks/frequency.QuadPart<<std::endl;
            double queries=0,rootHits=0,lookupMs=0;
            for(auto& tab : tabs) {
                std::array<PipelineRecord,64> records;
                for(size_t n;(n=tab.state.background->pipeline->Drain(records.data(),records.size()))!=0;)
                    for(size_t j=0;j<n;++j) if(wcscmp(records[j].operation,L"native.uia.resolve[queries,rootHits,lookupMs,scopeMs]")==0) {
                        queries+=records[j].v[0]; rootHits+=records[j].v[1]; lookupMs+=records[j].v[2];
                    }
            }
            std::cout<<"NATIVE UIA queries="<<queries<<" root-hits="<<rootHits<<" lookup-ms="<<lookupMs<<std::endl;
            Check(realUiaInput>0,"real UIA pressure generated no structure events");
            if(batching) {
                Check(realUiaOutput<realUiaInput,"real UIA providers did not coalesce their native structure burst");
                Check(Eventually([&] {pump(); return client.listener->bulkCached.load()>cachedBefore;}),
                    "coalesced native UIA changes did not deliver cached provider data to the real client");
            }
        }
    }
    g_pipelineTracing=savedTrace;
    g_raiseStructureEventOriginal=saved;
    SetEvent(client.stop);
    const bool cleaned=Eventually([&] {pump(); return WaitForSingleObject(worker,0)==WAIT_OBJECT_0;});
    if(!cleaned) std::cout<<"UIA cleanup stage="<<client.cleanup.load()<<" pid="<<GetCurrentProcessId()
        <<" worker="<<GetThreadId(worker)<<std::endl;
    Check(cleaned,"UIA listener cleanup timeout");
    Check(SUCCEEDED(client.result),"UIA listener removal failed");
    CloseHandle(worker); CloseHandle(client.ready); CloseHandle(client.stop);
    for(auto& tab : tabs) {
        RestoreNativeContent(tab.state); tab.browser->Unadvise(tab.cookie); tab.browser->Destroy(); tab.browser.Reset(); tab.sink->Release();
        SetWindowLongPtrW(tab.state.hwnd,GWLP_USERDATA,0); DestroyWindow(tab.state.frameHwnd);
    }
    std::cout<<"PASS: three real native views with an MTA UIA subtree-cache listener publish coalesced structure changes; no desktop file/focus changes\n";
}
