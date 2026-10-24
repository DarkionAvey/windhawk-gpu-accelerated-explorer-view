// Production AttachView/OverlayProc/host suppression/presentation endpoints.
// Only the Shell folder-view response is substituted; no preloaded columns,
// provider-ready flags, fit requests or synthetic overlay WM_SIZE are supplied.
namespace ColumnBridgeFixture {
template<class Receipt> bool Content(const Receipt& receipt) {
    if constexpr(requires {receipt.content;}) return receipt.content;
    else return receipt.accepted; // Historical source negative controls.
}
class Columns final : public IColumnManager {
    ULONG refs=1;
public:
    DWORD owner=GetCurrentThreadId();
    unsigned snapshots=0;
    bool available=false;
    UINT reportedCount=0;
    bool unterminatedLabel=false;
    std::vector<PROPERTYKEY> keys{PKEY_FileName,PKEY_DateModified,PKEY_ItemTypeText,PKEY_Size,PKEY_Author,PKEY_DateCreated};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr; if(iid!=IID_IUnknown && iid!=IID_IColumnManager) return E_NOINTERFACE;
        *out=static_cast<IColumnManager*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {const auto n=--refs; if(!n) delete this; return n;}
    HRESULT STDMETHODCALLTYPE GetColumnCount(CM_ENUM_FLAGS,UINT* out) override {
        Check(GetCurrentThreadId()==owner,"bridge column count crossed the native STA");
        if(!available) {*out=0; return E_PENDING;}
        *out=reportedCount ? reportedCount : UINT(keys.size()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetColumns(CM_ENUM_FLAGS,PROPERTYKEY* out,UINT count) override {
        Check(GetCurrentThreadId()==owner && count==keys.size(),"bridge column snapshot boundary");
        ++snapshots; std::copy(keys.begin(),keys.end(),out); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetColumnInfo(REFPROPERTYKEY key,CM_COLUMNINFO* out) override {
        Check(GetCurrentThreadId()==owner,"bridge column descriptor crossed the native STA");
        const wchar_t* label=SameKey(key,PKEY_FileName) ? L"Name" : SameKey(key,PKEY_DateModified) ? L"Date modified" :
            SameKey(key,PKEY_ItemTypeText) ? L"Type" : SameKey(key,PKEY_Size) ? L"Size" : SameKey(key,PKEY_Author) ? L"Authors" : L"Date created";
        if(unterminatedLabel) std::fill(std::begin(out->wszName),std::end(out->wszName),L'Q');
        else wcscpy_s(out->wszName,label);
        out->uWidth=out->uDefaultWidth=240; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetColumnInfo(REFPROPERTYKEY,const CM_COLUMNINFO*) override {return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetColumns(const PROPERTYKEY*,UINT) override {return E_NOTIMPL;}
};
class FolderView final : public AttachmentFolderView {
public:
    ComPtr<Columns> columns;
    FolderView() {columns.Attach(new Columns);}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(iid==IID_IColumnManager) return columns.CopyTo(reinterpret_cast<IColumnManager**>(out));
        return AttachmentFolderView::QueryInterface(iid,out);
    }
};
inline HWND contentHost=nullptr;
LRESULT CALLBACK FrameProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    const auto result=DefWindowProcW(hwnd,msg,wp,lp);
    if(msg==WM_SIZE && contentHost && GetParent(contentHost)==hwnd) {
        RECT r{}; GetClientRect(hwnd,&r);
        SetWindowPos(contentHost,nullptr,0,0,r.right,r.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    return result;
}
inline void Pump() {
    MSG msg{};
    while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg); DispatchMessageW(&msg);}
}
inline void Diagnose(ViewState& state,const char* stage) {
    std::cout<<"COLUMN BRIDGE "<<stage<<" columns="<<state.columns.size()<<" mode="<<state.columnsView
        <<" header="<<state.headerPx<<" step="<<state.featureInitializationStep<<" queued="<<state.featureInitializationQueued
        <<" first="<<state.firstContentSubmittedAt<<" dirty="<<state.nativeColumnsDirty<<" deferred="<<state.deferColumnProviders
        <<" pending="<<state.columnPresentationPending<<" motion="<<state.columnTransitionActive
        <<" row-motion="<<state.rowTransitionActive<<" hover="<<state.hoverAnimationActive
        <<" auto="<<state.autoRefitPending<<" loaded="<<state.loaded<<" names="<<state.namesPreview
        <<" directory="<<state.directoryPending<<" probe="<<state.lastColumnsProbe
        <<" widths="<<state.columnContentWidth<<" viewport="<<ListViewportWidth(state)<<std::endl;
}
}

static void TestExplorerColumnBridge(const std::wstring& directory) {
    const bool automatic=g_autoRefitColumns.load(); const auto factory=g_writeFactory;
    const auto empty=std::filesystem::path(directory)/L"column-bridge-empty";
    std::filesystem::create_directory(empty);
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"column bridge fonts");
    {
        PresentationFixture::Tab tab(L"column-content-ack",0);
        auto& state=tab.state;
        state.loaded=false; state.deferColumnProviders=true;
        state.firstContentSubmittedAt=123; state.directoryModePinned=false;
        tab.Start();
        PresentationFixture::Await(state,[](const auto& r) {return r.accepted;}); ConsumePresentation(state);
        Check(state.deferColumnProviders && !state.directoryModePinned,
            "loading-only acceptance released providers or pinned the native mode");
        state.loaded=true; SubmitPresentation(state);
        const auto emptyContent=PresentationFixture::Await(state,[](const auto& r) {return r.accepted;});
        ConsumePresentation(state);
        if(state.deferColumnProviders || !state.directoryModePinned || state.firstContentSubmittedAt!=123)
            std::cout<<"CONTENT ACK sequence="<<emptyContent->sequence<<" expected="<<state.presentationSequence
                <<" content="<<ColumnBridgeFixture::Content(*emptyContent)<<" accepted="<<emptyContent->accepted
                <<" deferred="<<state.deferColumnProviders<<" pinned="<<state.directoryModePinned
                <<" first="<<state.firstContentSubmittedAt<<std::endl;
        Check(!state.deferColumnProviders && state.directoryModePinned && state.firstContentSubmittedAt==123,
            "accepted empty content did not release this directory's providers independently of the lifetime timestamp");
        ++state.token; ++state.directoryToken; state.deferColumnProviders=true;
        auto stale=std::make_shared<PresentationReceipt>(*emptyContent);
        {std::lock_guard lock(state.presenter->mutex); state.presenter->receipt=stale;}
        ConsumePresentation(state);
        Check(state.deferColumnProviders,"departing-directory content released the new directory's providers");
        SubmitPresentation(state); PresentationFixture::Await(state,[](const auto& r) {return r.accepted;}); ConsumePresentation(state);
        Check(!state.deferColumnProviders,"current-directory content failed to release providers after rejecting the stale receipt");
        const auto current=state.presentationSequence;
        ++state.directoryToken; state.deferColumnProviders=true;
        stale->token=state.token; stale->sequence=current;
        {std::lock_guard lock(state.presenter->mutex); state.presenter->receipt=stale;}
        ConsumePresentation(state);
        Check(state.deferColumnProviders,"same-view stale enumeration released the newer generation's providers");
        SubmitPresentation(state); PresentationFixture::Await(state,[](const auto& r) {return r.accepted;}); ConsumePresentation(state);
        Check(!state.deferColumnProviders,"current enumeration content did not recover after stale cancellation");
    }
    for(bool enabled:{false,true}) {
        g_autoRefitColumns=enabled;
        WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
        cls.lpszClassName=L"CabinetWClass"; RegisterClassW(&cls);
        cls.lpfnWndProc=OverlayProc; cls.lpszClassName=kOverlayClass; RegisterClassW(&cls);
        HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CabinetWClass",L"cold column bridge",
            WS_POPUP|WS_VISIBLE,-32000,-32000,700,500,nullptr,nullptr,cls.hInstance,nullptr);
        SetWindowLongPtrW(frame,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(ColumnBridgeFixture::FrameProc));
        HWND host=CreateWindowExW(0,L"CtrlNotifySink",L"",WS_CHILD|WS_VISIBLE,0,0,700,500,frame,nullptr,cls.hInstance,nullptr);
        HWND native=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,0,0,700,500,host,nullptr,cls.hInstance,nullptr);
        Check(frame && host && native,"column bridge native hierarchy");
        ColumnBridgeFixture::contentHost=host;
        FocusFixture::Session focus(frame,nullptr);
        struct Cleanup {
            HWND frame; std::shared_ptr<TabBackground> work;
            ~Cleanup() {
                DestroyWindow(frame); ColumnBridgeFixture::contentHost=nullptr;
                if(work && work->presentation.thread) WaitForSingleObject(work->presentation.thread,10000);
            }
        } cleanup{frame};
        ComPtr<ColumnBridgeFixture::FolderView> response; response.Attach(new ColumnBridgeFixture::FolderView);
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&response->folder))),"column bridge private folder");
        ComPtr<IFolderView2> view; response.As(&view); ComPtr<IShellBrowser> browser;
        AttachView(native,view,response->folder,browser,directory);
        const HWND overlay=OverlayForTab(frame); auto* state=State(overlay);
        Check(state && state->presenter,"cold column bridge attachment"); cleanup.work=state->background;
        std::shared_ptr<const PresentationReceipt> lastReceipt;
        const auto wait=[&](auto predicate,const char* phase) {
            const bool done=Eventually([&] {
                {std::lock_guard lock(state->presenter->mutex); if(state->presenter->receipt) lastReceipt=state->presenter->receipt;}
                ColumnBridgeFixture::Pump(); return predicate();
            });
            if(!done) {
                ColumnBridgeFixture::Diagnose(*state,phase);
                if(lastReceipt) std::cout<<"COLUMN RECEIPT seq="<<lastReceipt->sequence<<" expected="<<state->presentationSequence
                    <<" accepted="<<lastReceipt->accepted<<" content="<<ColumnBridgeFixture::Content(*lastReceipt)<<" control="<<lastReceipt->control
                    <<" ready="<<lastReceipt->ready<<" failed="<<lastReceipt->failed
                    <<" token="<<lastReceipt->token<<"/"<<state->token<<" directory="<<lastReceipt->directoryToken<<"/"<<state->directoryToken
                    <<" rows="<<lastReceipt->modelRows<<std::endl;
            }
            Check(done,phase);
        };
        wait([&] {return state->loaded && state->total>0 && state->firstContentSubmittedAt;},"cold bridge never accepted its first names");
        Check(state->columns.empty(),"unavailable native provider published columns");
        const auto firstContent=state->firstContentSubmittedAt;
        // Explorer can bind the final location after a tab's first image.
        // Exercise the real directory reset and delayed native readiness, not
        // a synthetic write to the provider-deferral/startup flags.
        response->columns->available=true;
        Check(SUCCEEDED(SHCreateItemFromParsingName(empty.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&response->folder))),"column bridge empty destination");
        AttachView(native,view,response->folder,browser,empty.wstring());
        wait([&] {return state->loaded && !state->total && state->columns.size()==6 && state->headerPx>0 &&
            !state->nativeColumnsDirty && !state->columnPresentationPending && !state->columnTransitionActive;},
            "post-first-content navigation stranded native columns behind provider deferral");
        Check(state->firstContentSubmittedAt==firstContent && !state->deferColumnProviders,
            "directory completion reset the lifetime first-content clock or retained provider deferral");
        // Unchanged results still advance the scan cancellation generation.
        // They must publish a matching model/receipt without changing widths
        // or allocating another GPU domain.
        Paint(*state);
        const auto accepted=PresentationFixture::Await(*state,[](const auto& r) {return r.accepted;});
        ConsumePresentation(*state);
        for(unsigned scan=0;scan<3;++scan) {
            QueueDirectoryRefresh(*state);
            wait([&] {return !state->directoryPending && lastReceipt && lastReceipt->accepted &&
                lastReceipt->directoryToken==state->directoryToken &&
                state->presentationAcceptedSequence>=lastReceipt->sequence;},
                "unchanged revalidation left only obsolete presentation generations");
            Check(lastReceipt->device==accepted->device && lastReceipt->swapchain==accepted->swapchain,
                "unchanged readiness publication replaced the warm GPU domain");
        }
        Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&response->folder))),"column bridge return destination");
        AttachView(native,view,response->folder,browser,directory);
        wait([&] {return state->loaded && state->total>0 && !state->namesPreview && !state->directoryPending &&
            !state->deferColumnProviders && !state->nativeColumnsDirty;},"populated return never released native column readiness");
        Check(response->columns->snapshots && !(GetWindowLongPtrW(host,GWL_STYLE)&WS_VISIBLE),"cold columns bypassed native snapshot/suppression");
        // Five metadata columns, then maximize/restore the native frame. Its
        // host relayout is the only resize source; never resize the overlay.
        for(int width:{1600,480,1200}) {
            const auto before=state->columns;
            SetWindowPos(frame,nullptr,0,0,width,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            if(enabled) wait([&] {
                RECT r{}; GetClientRect(overlay,&r);
                const float viewport=ListViewportWidth(*state);
                const bool readableOverflow=viewport<Scale96(180+5*112,ViewDpi(*state));
                return r.right==width && !state->autoRefitPending && !state->columnFitMessageQueued &&
                    !state->columnTransitionActive && !state->columnPresentationPending &&
                    (readableOverflow ? state->columnContentWidth>viewport+1.f && HorizontalBar(*state).usable :
                        std::abs(state->columnContentWidth-viewport)<=1.f);
            },"native frame/host resizing never reached automatic column fitting");
            else {
                wait([&] {RECT r{}; GetClientRect(overlay,&r); return r.right==width;},"native frame resize did not update overlay bounds");
                for(size_t i=0;i<before.size();++i) Check(state->columns[i].width==before[i].width,"disabled fitting changed native widths");
            }
        }
    }
    std::filesystem::remove(empty);
    ShutDownFrames(); g_frameStopping=false; g_autoRefitColumns=automatic; g_writeFactory=factory;
    std::cout<<"PASS: current-directory content acknowledgements release provider deferral; loading/stale frames cannot. Cold/delayed native columns, empty/populated navigation and six-column native frame/host resizing preserve readable overflow or fill wide viewports only when enabled\n";
}
