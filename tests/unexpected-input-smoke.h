// Fault-injected boundary checks use private folders, offscreen windows and
// fake Shell destinations. No desktop input, clipboard or file command runs.
namespace UnexpectedInputFixture {
class Enumerator final : public IEnumIDList {
    ULONG refs=1;
    OwnedPidl child;
    int scenario;
public:
    Enumerator(PCUITEMID_CHILD value,int test) : child(ILClone(value)),scenario(test) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr; if(iid!=IID_IUnknown && iid!=IID_IEnumIDList) return E_NOINTERFACE;
        *out=static_cast<IEnumIDList*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=--refs; if(!n) delete this; return n;}
    HRESULT STDMETHODCALLTYPE Next(ULONG count,PITEMID_CHILD* out,ULONG* fetched) override {
        if(scenario==0 || scenario==4) {
            // No valid outputs are promised for an empty/error result.
            out[0]=reinterpret_cast<PITEMID_CHILD>(1);
            *fetched=0; return scenario==0 ? S_OK : E_FAIL;
        }
        const ULONG actual=scenario==1 ? count : 1;
        for(ULONG i=0;i<actual;++i) out[i]=ILClone(child.get());
        if(scenario==5 && count>1) out[1]=reinterpret_cast<PITEMID_CHILD>(1);
        *fetched=scenario==0 ? 0 : scenario==1 ? ULONG_MAX : scenario==2 ? 2 : 1;
        return scenario==4 ? E_FAIL : scenario==2 || scenario==5 ? S_FALSE : S_OK;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Reset() override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE Clone(IEnumIDList**) override {return E_NOTIMPL;}
};
class FaultDropTarget final : public IDropTarget {
    ULONG refs=1;
public:
    HRESULT enter=S_OK,over=S_OK;
    DWORD effect=DROPEFFECT_COPY;
    unsigned enters=0,drops=0;
    std::function<void()> duringOver,duringLeave;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr; if(iid!=IID_IUnknown && iid!=IID_IDropTarget) return E_NOINTERFACE;
        *out=static_cast<IDropTarget*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=--refs; if(!n) delete this; return n;}
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*,DWORD,POINTL,DWORD* out) override {++enters; *out=effect; return enter;}
    HRESULT STDMETHODCALLTYPE DragOver(DWORD,POINTL,DWORD* out) override {if(duringOver) duringOver(); *out=effect; return over;}
    HRESULT STDMETHODCALLTYPE DragLeave() override {auto callback=std::exchange(duringLeave,{}); if(callback) callback(); return S_OK;}
    HRESULT STDMETHODCALLTYPE Drop(IDataObject*,DWORD,POINTL,DWORD* out) override {++drops; *out=effect; return S_OK;}
};
inline ComPtr<IShellFolder> BindFolder(const std::wstring& path) {
    ComPtr<IShellItem> item; ComPtr<IShellFolder> folder;
    Check(SUCCEEDED(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&folder))),"unexpected input private Shell folder");
    return folder;
}
}

static void TestUnexpectedGeometry() {
    PresentationFixture::Tab tab(L"unexpected geometry",20);
    auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
    SetScrollImmediate(state,80); SetHorizontalScroll(state,0);
    const float scroll=state.scrollNow,target=state.scrollTarget,horizontal=state.horizontalScroll;
    for(float value:{NAN,INFINITY,-INFINITY}) {
        SetScrollImmediate(state,value); MoveScroll(state,value); SetHorizontalScroll(state,value);
        AnimateHorizontalTrackTo(state,value); ScrubScrollbar(state,value);
        Check(state.scrollNow==scroll && state.scrollTarget==target && state.horizontalScroll==horizontal &&
            std::isfinite(state.horizontalTarget),"non-finite input poisoned shared scroll/animation state");
    }
    for(float value:{NAN,INFINITY,-INFINITY,-1.f,0.f}) {
        auto bar=MakeScrollbarGeometry(ScrollAxis::Vertical,10,20,0,300,1000,value,0,20);
        Check(!bar.usable,"invalid viewport produced a usable scrollbar");
    }
    const float maximum=std::numeric_limits<float>::max();
    auto overflow=MakeScrollbarGeometry(ScrollAxis::Horizontal,-maximum,maximum,0,20,1000,200,0,20);
    Check(!overflow.usable,"overflowing finite track extent entered scrollbar geometry");
    const auto bar=MakeScrollbarGeometry(ScrollAxis::Vertical,10,20,0,300,1000,200,0,20);
    Check(bar.usable && std::isfinite(bar.thumbStart) && ScrollFractionAt(bar,NAN,0)==0 &&
        ScrollFractionAt(bar,0,INFINITY)==0,"invalid scrollbar coordinate produced a non-finite fraction");
    // Deterministic mixed input, including extremes, ordinary values and tiny
    // viewports. Every usable result must have finite ordered track geometry.
    unsigned random=0x91e10da5;
    for(int i=0;i<2000;++i) {
        random=random*1664525u+1013904223u;
        const float viewport=float(random%20000)/10.f;
        random=random*1664525u+1013904223u;
        const float content=float(random%1000000)/10.f;
        const auto sample=MakeScrollbarGeometry(ScrollAxis::Vertical,10,20,0,float(i%400),content,viewport,float(i*900),20);
        Check(!sample.usable || (std::isfinite(sample.thumbStart) && std::isfinite(sample.thumbLength) &&
            sample.thumbLength>=0 && sample.thumbLength<=float(i%400) && std::isfinite(sample.travel)),
            "scrollbar mixed-input invariant");
    }
    std::cout<<"PASS: non-finite scrolling is ignored; invalid/overflowing and 2000 mixed scrollbar inputs stay bounded\n";
}

static void TestUnexpectedPixels() {
    IconPixels pixels; pixels.width=pixels.height=2; pixels.stride=8; pixels.data.assign(16,255);
    Check(pixels.Ready(),"valid pixel buffer rejected");
    pixels.data.resize(15); Check(!pixels.Ready(),"short pixel buffer accepted for GPU upload/thumbnail memcpy");
    pixels.data.resize(16); pixels.stride=4; Check(!pixels.Ready(),"short pixel stride accepted");
    pixels.width=UINT_MAX; pixels.stride=UINT_MAX; Check(!pixels.Ready(),"pixel row-byte multiplication overflow accepted");
    ComPtr<IWICImagingFactory> factory;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))),"boundary WIC factory");
    ComPtr<IWICBitmap> bitmap;
    Check(SUCCEEDED(factory->CreateBitmap(2,2,GUID_WICPixelFormat32bppPBGRA,WICBitmapCacheOnLoad,&bitmap)),"boundary WIC source");
    HRESULT hr=S_OK;
    Check(!DecodeIconPixels(factory.Get(),bitmap.Get(),&hr,0).Ready() && hr==E_INVALIDARG,"zero decode budget was not rejected");
    hr=E_FAIL;
    Check(DecodeIconPixels(factory.Get(),bitmap.Get(),&hr,UINT_MAX).Ready() && hr==S_OK,"valid decode left a stale failure status");
    std::cout<<"PASS: truncated/invalid-stride/overflow pixels, zero decode limits and successful decode status\n";
}

static void TestUnexpectedNavigation() {
    const std::wstring ordinary=L"Q:\\valid\\folder";
    VARIANT value{}; value.vt=VT_BSTR; value.bstrVal=SysAllocString(ordinary.c_str());
    Check(AutomationNavigationUrlToPath(value)==ordinary,"ordinary navigation rejected"); VariantClear(&value);
    for(const auto& path:{ordinary+std::wstring(1,L'\0')+L"hidden",std::wstring(L"Q:\\")+std::wstring(32768,L'x')}) {
        value.vt=VT_BSTR; value.bstrVal=SysAllocStringLen(path.data(),UINT(path.size()));
        Check(AutomationNavigationUrlToPath(value).empty() && !IsTargetPath(path),"ambiguous or oversized navigation path accepted");
        VariantClear(&value);
    }
    value.vt=VT_VARIANT|VT_BYREF; value.pvarVal=&value;
    Check(AutomationNavigationUrlToPath(value).empty(),"cyclic automation variant did not short circuit");
    PresentationFixture::Tab tab(L"empty incremental search",0);
    auto& state=tab.state; state.typeSearch.assign(32767,L'x'); state.lastTyped=GetTickCount64();
    TypeToSelect(state,L'y'); Check(state.typeSearch.size()==32767,"incremental search grew beyond the supported filename bound");
    TypeToSelect(state,L'\b'); Check(state.typeSearch.size()==32766,"bounded incremental search no longer supports Backspace");
    std::cout<<"PASS: embedded-NUL/oversized navigation, cyclic variants and bounded incremental search\n";
}

static void TestUnexpectedColumns() {
    PresentationFixture::Tab tab(L"unexpected Shell columns",2);
    auto& state=tab.state; SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    FocusFixture::Session focus(tab.frame,state.hwnd);
    ComPtr<ColumnBridgeFixture::FolderView> view; view.Attach(new ColumnBridgeFixture::FolderView);
    state.folderView=view; state.columnManager=view->columns; auto& provider=*view->columns.Get();
    provider.available=true; state.deferColumnProviders=false; state.featureInitializationStep=5;
    const auto oldColumns=state.columns;
    for(UINT count:{4097u,UINT_MAX}) {
        provider.reportedCount=count; state.nativeColumnsDirty=true;
        RefreshExplorerColumns(state,true);
        Check(state.columns.size()==oldColumns.size() && state.columns[0].title==oldColumns[0].title && !provider.snapshots,
            "invalid provider column count changed existing columns or requested a huge snapshot");
        state.columnCatalogReady=state.columnCatalogPending=false;
        Check(!RequestColumnCatalog(state) && !state.columnCatalogPending && !provider.snapshots,
            "invalid provider catalog count allocated or left its pending latch set");
    }
    provider.reportedCount=0; provider.unterminatedLabel=true; state.columns.clear();
    state.columnCatalogReady=true; // No cold catalog work is needed by this label test.
    RefreshExplorerColumns(state,true);
    Check(state.columns.size()==provider.keys.size() && state.columns[0].title==std::wstring(80,L'Q'),
        "non-terminated fixed Shell column label read past its buffer");
    std::cout<<"PASS: corrupt visible/catalog column counts retain layout, release latches and bound fixed labels\n";
}

static void TestUnexpectedSelection(const std::wstring& directory) {
    using namespace UnexpectedInputFixture;
    for(int scenario=0;scenario<6;++scenario) {
        PresentationFixture::Tab tab(L"unexpected selection enumeration",1);
        auto& state=tab.state; SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        FocusFixture::Session focus(tab.frame,state.hwnd);
        state.folder=BindFolder(directory); state.folderPath=directory;
        state.entries[0].name=L"file0.txt"; state.entries[0].path=(std::filesystem::path(directory)/L"file0.txt").wstring();
        state.selected=state.anchor=0;
        PITEMID_CHILD raw=nullptr; Check(SUCCEEDED(EntryChildPidl(state,state.entries[0],&raw)) && raw,"selection boundary identity"); OwnedPidl child(raw);
        auto* view=new SelectionFolderView; view->folder=state.folder; view->state=&state; state.folderView.Attach(view);
        const int expected=scenario==1 ? 16 : 1;
        for(int i=0;i<expected;++i) view->selected.emplace_back(ILClone(child.get()));
        auto job=std::make_shared<PreparedNativeSelection>(); job->generation=g_nextToken.fetch_add(1);
        job->viewToken=state.token; job->directoryToken=state.directoryToken; job->native=state.nativeHwnd;
        job->expectedCount=expected; job->imported=true;
        state.nativeSelectionCapture.Attach(new Enumerator(child.get(),scenario)); state.nativeSelectionCaptureJob=job;
        state.pendingNativeSelection=job->generation; state.nativeSelectionUnsafe=true; state.nativeSelectionDirty=false;
        AdvanceNativeSelectionCapture(state);
        if(scenario<5) Check(state.nativeSelectionDirty && state.nativeSelectionUnsafe && !state.pendingNativeSelection &&
            state.selected==0 && state.selectedFlags[0],"malformed selection batch was accepted or erased the GPU identity");
        else Check(!state.nativeSelectionCaptureJob && !state.nativeSelectionDirty && state.background->selectionJob==job,
            "valid partial final selection batch was rejected");
        CancelNativeSelectionPreparation(state);
    }
    std::cout<<"PASS: inconsistent/overflowing/null/error selection batches retry safely; valid S_FALSE partial batch is retained\n";
}

static void TestUnexpectedDrops(const std::wstring& directory) {
    using namespace UnexpectedInputFixture;
    for(int scenario=0;scenario<7;++scenario) {
        PresentationFixture::Tab tab(L"unexpected drop target",0);
        auto& state=tab.state; SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        FocusFixture::Session focus(tab.frame,state.hwnd); state.folderPath=directory;
        auto* folder=new RenameHandoffFixture::Folder; folder->inner=BindFolder(directory); state.folder.Attach(folder);
        ComPtr<FaultDropTarget> target; target.Attach(new FaultDropTarget); folder->dropTarget=target;
        PIDLIST_ABSOLUTE raw=nullptr; Check(SUCCEEDED(SHGetIDListFromObject(folder->inner.Get(),&raw)) && raw,"drop boundary private identity"); OwnedPidl parent(raw);
        ComPtr<IDataObject> data; Check(SUCCEEDED(SHCreateDataObject(parent.get(),0,nullptr,nullptr,IID_PPV_ARGS(&data))),"drop boundary payload");
        ComPtr<IDropTarget> bridge; bridge.Attach(new ShellDropBridge(state.hwnd));
        const POINTL point{-30000,-30000}; DWORD effect=DROPEFFECT_COPY;
        if(scenario==0) target->enter=E_ACCESSDENIED;
        if(scenario==1) target->over=E_FAIL;
        if(scenario==2) target->effect=DROPEFFECT_MOVE;
        if(scenario==3) target->duringOver=[&] {++state.mouseFolderEpoch;};
        if(scenario==5) folder->beforeDropResolve=[&] {++state.mouseFolderEpoch;};
        bridge->DragEnter(data.Get(),0,point,&effect);
        if(scenario==0 || scenario==2 || scenario==5) Check(effect==DROPEFFECT_NONE,"rejected/stale/unauthorized drag advertised a drop effect");
        if(scenario==6) {
            target->duringLeave=[&] {DWORD next=DROPEFFECT_COPY; bridge->DragEnter(data.Get(),0,point,&next);};
            bridge->DragLeave(); effect=DROPEFFECT_COPY; bridge->DragOver(0,point,&effect);
            Check(effect==DROPEFFECT_COPY,"old DragLeave erased a reentrant new drag session");
        }
        effect=DROPEFFECT_COPY; bridge->Drop(data.Get(),0,point,&effect);
        Check(target->drops==((scenario==4 || scenario==6) ? 1u : 0u) &&
            effect==((scenario==4 || scenario==6) ? DROPEFFECT_COPY : DROPEFFECT_NONE),
            "invalid/stale drop reached a file destination or valid copy was lost");
        bridge->DragLeave();
    }
    std::cout<<"PASS: failed enter/over, disallowed effects, reentrant navigation/resolve/leave and valid drop controls; no file commands\n";
}

static void TestUnexpectedInputs(const std::wstring& directory,const wchar_t* area=L"all") {
    const auto run=[&](const wchar_t* name) {return !wcscmp(area,L"all") || !wcscmp(area,name);};
    if(run(L"geometry")) TestUnexpectedGeometry();
    if(run(L"pixels")) TestUnexpectedPixels();
    if(run(L"navigation")) TestUnexpectedNavigation();
    if(run(L"columns")) TestUnexpectedColumns();
    if(run(L"selection")) TestUnexpectedSelection(directory);
    if(run(L"drops")) TestUnexpectedDrops(directory);
}
