// Completion ordering tests use private windows/files, substituted focus and
// clipboard APIs, and real owner-thread messages. No live Explorer is modified.
class TimingShellBrowser final : public IShellBrowser {
    ULONG refs=1;
public:
    HWND window=nullptr;
    ComPtr<IShellView> view;
    HRESULT result=S_OK;
    HRESULT destinationReadiness=E_NOINTERFACE;
    void (*duringQuery)(void*)=nullptr;
    void* queryContext=nullptr;
    unsigned queries=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr;
        if(iid==IID_IExplorerBrowser) return destinationReadiness;
        if(iid!=IID_IUnknown && iid!=IID_IOleWindow && iid!=IID_IShellBrowser) return E_NOINTERFACE;
        *out=static_cast<IShellBrowser*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* out) override { *out=window; return S_OK; }
    HRESULT STDMETHODCALLTYPE QueryActiveShellView(IShellView** out) override {
        ++queries; *out=nullptr; if(FAILED(result)) return result;
        if(duringQuery) duringQuery(queryContext);
        return view ? view.CopyTo(out) : E_PENDING;
    }
#define UNUSED_BROWSER(name,...) HRESULT STDMETHODCALLTYPE name(__VA_ARGS__) override { return E_NOTIMPL; }
    UNUSED_BROWSER(ContextSensitiveHelp,BOOL)
    UNUSED_BROWSER(InsertMenusSB,HMENU,LPOLEMENUGROUPWIDTHS)
    UNUSED_BROWSER(SetMenuSB,HMENU,HOLEMENU,HWND)
    UNUSED_BROWSER(RemoveMenusSB,HMENU)
    UNUSED_BROWSER(SetStatusTextSB,LPCWSTR)
    UNUSED_BROWSER(EnableModelessSB,BOOL)
    UNUSED_BROWSER(TranslateAcceleratorSB,MSG*,WORD)
    UNUSED_BROWSER(BrowseObject,PCUIDLIST_RELATIVE,UINT)
    UNUSED_BROWSER(GetViewStateStream,DWORD,IStream**)
    UNUSED_BROWSER(GetControlWindow,UINT,HWND*)
    UNUSED_BROWSER(SendControlMsg,UINT,UINT,WPARAM,LPARAM,LRESULT*)
    UNUSED_BROWSER(OnViewWindowActive,IShellView*)
    UNUSED_BROWSER(SetToolbarItems,LPTBBUTTONSB,UINT,UINT)
#undef UNUSED_BROWSER
};

class TimingAutomation final : public IDispatch {
    ULONG refs=1;
public:
    HRESULT readiness=E_PENDING;
    HRESULT connectionReadiness=E_NOINTERFACE;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr;
        if(iid==IID_IWebBrowser2) return readiness;
        if(iid==IID_IConnectionPointContainer) return connectionReadiness;
        if(iid!=IID_IUnknown && iid!=IID_IDispatch) return E_NOINTERFACE;
        *out=static_cast<IDispatch*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR*,UINT,LCID,DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID,REFIID,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,UINT*) override { return E_NOTIMPL; }
};

class TimingManipulationUpdates final : public IDirectManipulationUpdateManager {
    ULONG refs=1;
public:
    unsigned updates=0;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr;
        if(iid!=IID_IUnknown && iid!=__uuidof(IDirectManipulationUpdateManager)) return E_NOINTERFACE;
        *out=static_cast<IDirectManipulationUpdateManager*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE RegisterWaitHandleCallback(HANDLE,IDirectManipulationUpdateHandler*,DWORD*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE UnregisterWaitHandleCallback(DWORD) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Update(IDirectManipulationFrameInfoProvider*) override { ++updates; return S_OK; }
};

static void TestTimingReadiness(const std::wstring& directory) {
    const bool autoFit=g_autoRefitColumns.exchange(true);
    auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"timing DirectWrite");
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    cls.lpszClassName=L"CabinetWClass"; RegisterClassW(&cls);
    HWND frame=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"CabinetWClass",L"timing",
        WS_POPUP|WS_VISIBLE,-32000,-32000,640,480,nullptr,nullptr,cls.hInstance,nullptr);
    ViewState state;
    state.hwnd=CreateWindowExW(0,L"STATIC",L"timing overlay",WS_CHILD|WS_VISIBLE,
        0,0,640,480,frame,nullptr,cls.hInstance,nullptr);
    Check(frame && state.hwnd,"timing windows");
    state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
    state.background->hwnd=state.hwnd; state.folderPath=directory;
    state.visible=state.inScope=state.detailsViewActive=state.loaded=state.columnsView=true;
    state.directoryToken=601; state.background->directoryGeneration=601;
    state.featureInitializationStep=kOverlayFeatureSteps; state.firstContentSubmittedAt=1;
    state.nativeColumnsDirty=false; state.lastColumnsProbe=1; state.deferColumnProviders=false;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    const auto original=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.hwnd,GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(SampledPointerProc)));
    auto* native=new SelectionFolderView;
    state.folderView.Attach(native); native->state=&state; native->reenterCapture=false;
    ComPtr<IShellItem> folderItem;
    Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(),nullptr,IID_PPV_ARGS(&folderItem))) &&
        SUCCEEDED(folderItem->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&state.folder))),"timing private folder");
    native->folder=state.folder;
    state.columnManager.Attach(new CatalogColumnManager);
    ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=55;
    ExplorerColumn author; author.key=PKEY_Author; author.title=L"Author"; author.width=55;
    state.columns={name,author};
    const auto resetRows=[&](int count) {
        state.total=count; state.entries.assign(count,{}); state.selectedFlags.assign(count,0);
        state.grouping.enabled=false;
        for(int i=0;i<count;++i) {
            auto& e=state.entries[i]; e.name=L"file"+std::to_wstring(i)+L".txt";
            e.path=JoinPath(directory,e.name); e.propertyEpoch=state.propertyEpoch;
            e.columnEpoch=state.columnEpoch; e.detailsReady=true;
            e.cells={{PKEY_Author,L"A completed property value much wider than its initial column"}};
        }
        state.entryPathIndex.clear(); RebuildGroupSections(state,false); UpdateColumns(state);
    };
    resetRows(3); UpdateMetrics(state); UpdateRefreshRate(state);
    Check(EnsureTarget(state),"timing target");
    const HWND actualFocus=GetFocus(),actualForeground=GetForegroundWindow();
    {
        FocusFixture::Session focus(frame,state.hwnd);
        const auto pumpFit=[&] {
            MSG message{}; unsigned count=0;
            while(PeekMessageW(&message,state.hwnd,kMsgColumnFitReady,kMsgColumnFitReady,PM_REMOVE)) {
                Check(++count<4,"column fit spun its own continuation");
                OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
            }
            return count;
        };
        const auto settle=[&] { FinishColumnTransition(state); Paint(state); pumpFit(); };
        for(auto& e:state.entries) {
            e.detailsReady=false; e.cells.clear(); e.detailsPending=true;
            e.detailsPendingToken=state.directoryToken; e.detailsPendingEpoch=state.columnEpoch;
        }
        state.directoryPending=true; state.namesPreview=true;
        ArmAutoRefit(state);
        Check(state.autoRefitPending && !pumpFit(),"auto fit started before final names");
        state.namesPreview=false; state.directoryPending=false; state.nativeColumnsDirty=true;
        QueueColumnFit(state); Check(!pumpFit(),"auto fit started before native columns");
        state.nativeColumnsDirty=false; state.deferColumnProviders=true;
        QueueColumnFit(state); Check(!pumpFit(),"auto fit ignored first-content setup");
        state.deferColumnProviders=false;
        for(int turn=0;turn<12;++turn) {
            MaybeRunAutoRefit(state); QueueColumnFit(state);
            Check(state.autoRefitPending && !pumpFit(),"slow metadata lost its pending automatic fit");
        }
        // Complete all but the last required property through real delivery.
        state.mousePressActive=true;
        const auto complete=[&](int first,int last,unsigned long long epoch) {
            DirectoryResult result; result.kind=DirectoryResult::Details; result.token=state.directoryToken;
            for(int i=first;i<last;++i) {
                DetailValue value; value.index=i; value.columnEpoch=epoch; value.path=state.entries[i].path;
                value.cells={{PKEY_Author,L"A completed property value much wider than its initial column"}};
                result.details.push_back(std::move(value));
            }
            PublishDirectoryResult(*state.background,std::move(result)); ReceiveDirectoryResults(state);
        };
        complete(0,2,state.columnEpoch);
        QueueColumnFit(state); Check(!pumpFit() && state.autoRefitPending,"partial properties licensed auto fit");
        complete(2,3,state.columnEpoch+1);
        Check(!state.entries[2].detailsReady,"stale property generation licensed auto fit");
        complete(2,3,state.columnEpoch);
        QueueColumnFit(state); Check(!pumpFit(),"auto fit ran while a real press still owned input");
        state.lastInteractionTick=GetTickCount64();
        OverlayProc(state.hwnd,WM_CAPTURECHANGED,0,0);
        Check(pumpFit()==1 && !state.autoRefitPending && state.columnPresentationPending,
            "input completion failed to wake the ready fit, or depended on a quiet-time estimate");
        Paint(state);
        Check(state.modeComposition.published && state.modeComposition.widths && state.columnPresentationPending,
            "completion-driven automatic fit failed to publish independent motion before settlement");
        settle();
        // Drive the real zoom handlers. A stale fit/timer, completed visual
        // transition, or long pause in a held pinch cannot bypass the debounce
        // and actual gesture completion. No wall-clock sleeps are required.
        const bool smooth=g_smooth.load();
        const auto propertyEpoch=state.propertyEpoch, columnEpoch=state.columnEpoch;
        for(bool animated:{false,true}) {
            g_smooth=animated;
            state.columns={name,author}; UpdateColumns(state);
            ArmAutoRefit(state); // Deliberately leave the old fit message queued.
            OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
            Check(state.zoomAutoRefitNotBefore,"wheel zoom did not schedule automatic fitting");
            state.zoomAutoRefitNotBefore=GetTickCount64()+60000;
            Check(pumpFit()==1 && state.autoRefitPending && !state.columnPresentationPending,
                "an already queued fit bypassed the new zoom burst");
            for(int sample=0;sample<24;++sample) {
                const auto sampledAt=GetTickCount64();
                state.zoomAutoRefitNotBefore=sampledAt+20;
                OverlayProc(state.hwnd,WM_MOUSEWHEEL,
                    MAKEWPARAM(MK_CONTROL,sample%2 ? WHEEL_DELTA : -WHEEL_DELTA),0);
                Check(state.zoomAutoRefitNotBefore>=sampledAt+kZoomAutoRefitDebounceMs,
                    "continuous wheel zoom failed to postpone fitting");
                state.zoomAutoRefitNotBefore=GetTickCount64()+60000;
                Check(!pumpFit(),"continuous wheel zoom queued intermediate fits");
            }
            const auto wheelSampledAt=GetTickCount64();
            state.wheelRemainder=0; state.zoomAutoRefitNotBefore=wheelSampledAt+20;
            OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,1),0);
            Check(state.zoomAutoRefitNotBefore>=wheelSampledAt+kZoomAutoRefitDebounceMs,
                "fractional wheel sample ended the burst");
            const auto latestDeadline=state.zoomAutoRefitNotBefore=GetTickCount64()+60000;
            OverlayProc(state.hwnd,WM_TIMER,kZoomAutoRefitTimerId,0);
            AdvanceRowHeight(state,true); QueueColumnFit(state);
            Check(state.zoomAutoRefitNotBefore==latestDeadline && state.autoRefitPending && !pumpFit(),
                "stale timer or completed animation bypassed the newest zoom deadline");
            state.zoomAutoRefitNotBefore=1;
            OverlayProc(state.hwnd,WM_TIMER,kZoomAutoRefitTimerId,0);
            Check(pumpFit()==1 && !state.autoRefitPending && !state.zoomAutoRefitNotBefore &&
                (state.columns[0].width>name.width || state.columns[1].width>author.width),
                "settled wheel burst failed to run the existing column fitter exactly once");
            settle();
            OverlayProc(state.hwnd,WM_TIMER,kZoomAutoRefitTimerId,0);
            Check(!pumpFit(),"obsolete zoom timer repeated an already completed fit");
        }
        state.columns={name,author}; UpdateColumns(state);
        state.manipulationEnabled=true;
        OverlayProc(state.hwnd,kMsgManipulationStatus,1,0);
        OverlayProc(state.hwnd,kMsgManipulationPinch,0,static_cast<LPARAM>(std::lround(std::log(1.08)*65536)));
        Check(state.autoRefitPending,"pinch zoom did not request automatic fitting");
        const auto pinchSampledAt=GetTickCount64();
        state.zoomAutoRefitNotBefore=pinchSampledAt+20;
        OverlayProc(state.hwnd,kMsgManipulationPinch,0,1); // Valid sub-pixel scale sample.
        Check(state.zoomAutoRefitNotBefore>=pinchSampledAt+kZoomAutoRefitDebounceMs,
            "fractional pinch sample ended the burst");
        AdvanceRowHeight(state,true); state.zoomAutoRefitNotBefore=1;
        OverlayProc(state.hwnd,WM_TIMER,kZoomAutoRefitTimerId,0);
        Check(!pumpFit() && state.autoRefitPending,"quiet time impersonated pinch completion");
        OverlayProc(state.hwnd,kMsgManipulationStatus,2,0);
        Check(!pumpFit() && state.autoRefitPending,"pinch inertia licensed fitting before READY");
        // Keep a required property incomplete well beyond the quiet period.
        auto& delayed=state.entries[0]; delayed.detailsReady=false; delayed.cells.clear();
        delayed.detailsPending=true; delayed.detailsPendingToken=state.directoryToken;
        delayed.detailsPendingEpoch=state.columnEpoch;
        OverlayProc(state.hwnd,kMsgManipulationStatus,0,0);
        for(int turn=0;turn<12;++turn) {
            MaybeRunAutoRefit(state); QueueColumnFit(state);
            Check(!pumpFit() && state.autoRefitPending,"post-zoom fit expired or ignored slow properties");
        }
        DirectoryResult zoomDetails; zoomDetails.kind=DirectoryResult::Details; zoomDetails.token=state.directoryToken;
        DetailValue zoomValue; zoomValue.index=0; zoomValue.path=delayed.path; zoomValue.columnEpoch=state.columnEpoch;
        zoomValue.cells={{PKEY_Author,L"A completed property value much wider than its initial column"}};
        zoomDetails.details.push_back(std::move(zoomValue));
        PublishDirectoryResult(*state.background,std::move(zoomDetails)); ReceiveDirectoryResults(state);
        Check(pumpFit()==1 && !state.autoRefitPending,"late metadata never resumed the post-pinch fit");
        state.manipulationEnabled=state.touchpadPinching=false; settle();
        Check(state.propertyEpoch==propertyEpoch && state.columnEpoch==columnEpoch,
            "post-zoom fitting discarded populated metadata");
        state.wheelRemainder=0;
        g_autoRefitColumns=false;
        OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
        AdvanceRowHeight(state,true);
        Check(!state.autoRefitPending && !state.zoomAutoRefitNotBefore && !pumpFit(),
            "disabled automatic fitting still scheduled a zoom fit");
        g_autoRefitColumns=true;
        ApplyRowHeight(state,96); AdvanceRowHeight(state,true); CancelAutoRefit(state);
        OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
        Check(!state.autoRefitPending,"clamped zoom initiated a redundant fit");
        OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,-WHEEL_DELTA),0);
        Check(state.autoRefitPending,"new zoom failed to arm after a clamped sample");
        CancelAutoRefit(state); AdvanceRowHeight(state,true);
        OverlayProc(state.hwnd,WM_TIMER,kZoomAutoRefitTimerId,0);
        Check(!state.autoRefitPending && !pumpFit(),"stale timer revived a cancelled zoom fit");
        ApplyRowHeight(state,32); CancelAutoRefit(state); AdvanceRowHeight(state,true);
        state.wheelRemainder=0; g_smooth=smooth; settle();
        // Hidden groups are not metadata dependencies: their rows are never
        // requested by the viewport's property lane.
        resetRows(1100); state.grouping.enabled=true;
        for(int i=0;i<state.total;++i) {
            auto& e=state.entries[i]; e.groupKey=i<3 ? L"A" : i<903 ? L"B" : L"C";
            e.groupTitle=e.groupKey;
            if(i>=3 && i<903) { e.detailsReady=false; e.cells.clear(); }
        }
        RebuildGroupSections(state,false); state.sections[1].collapsed=true;
        state.sections[1].fraction=0; LayoutGroupSections(state); SetScrollImmediate(state,0);
        const auto visible=VisibleItemRows(state,0.f); const auto sample=SampleColumnRows(state);
        for(size_t i=0;i<sample.count;++i)
            Check(std::find(visible.begin(),visible.end(),sample.At(i))!=visible.end(),"fitter sampled unrequested collapsed rows");
        Check(!HasPendingVisibleColumnDetails(state),"collapsed metadata could strand a pending fit");
        state.columns={name,author}; UpdateColumns(state); ArmAutoRefit(state);
        Check(pumpFit()==1 && !state.autoRefitPending,"collapsed groups stranded automatic fitting");
        settle();
        // Interactive resizing has an actual end, not a quiet-period timer.
        RegisterTabOverlay(frame,frame,state.hwnd);
        SetWindowSubclass(frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass),0);
        SendMessageW(frame,WM_ENTERSIZEMOVE,0,0);
        state.lastAutoFitViewport={640,480}; state.lastResizeObserved={640,480};
        SetWindowPos(state.hwnd,nullptr,0,0,600,480,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        state.columns={name,author}; UpdateColumns(state);
        ScheduleResizeAutoRefit(state,600,480);
        Check(state.autoRefitPending && !pumpFit(),"resize fit fired before WM_EXITSIZEMOVE");
        SendMessageW(frame,WM_EXITSIZEMOVE,0,0);
        Check(pumpFit()==1 && !state.autoRefitPending,"resize completion did not wake a fit");
        RemoveWindowSubclass(frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass));
        UnregisterTabOverlay(frame,frame,state.hwnd); settle();
        ArmAutoRefit(state); g_autoRefitColumns=false; ArmAutoRefit(state); pumpFit();
        Check(!state.autoRefitPending,"disabled boolean kept an old automatic request");
        resetRows(3); state.columns={name}; UpdateColumns(state); settle();
        // A stalled/missing viewport status cannot make elapsed quiet time
        // impersonate READY. Only an actual terminal status ends the gesture.
        auto* updates=new TimingManipulationUpdates;
        state.manipulationUpdateManager.Attach(updates);
        state.manipulationEnabled=state.manipulationFrameActive=true;
        state.lastManipulationActivity=1; state.framePaintPending=false;
        OverlayProc(state.hwnd,kMsgFrame,0,0);
        Check(updates->updates && state.manipulationFrameActive,
            "quiet or delayed touchpad gesture was ended by elapsed time");
        OverlayProc(state.hwnd,kMsgManipulationStatus,0,0);
        Check(!state.manipulationFrameActive,"actual READY did not end the touchpad gesture");
        state.manipulationEnabled=false; state.manipulationUpdateManager.Reset();
        state.framePaintPending=false; settle();
        // A delayed native model must still receive the return selection after
        // more than eight attempts. Only a real compositor acknowledgement
        // licenses any attempt after a cached GPU submission.
        auto* browser=new TimingShellBrowser; state.shellBrowser.Attach(browser); browser->window=frame;
        auto* shellView=new SelectionCommandView(nullptr); shellView->window=state.nativeHwnd;
        browser->view.Attach(shellView);
        state.lastFolderNativePending=L"file0.txt"; state.lastFolderNativeAfterPaint=true;
        TrySyncLastFolderSelection(state); Check(!native->commits,"selection ran before an accepted paint");
        state.lastFolderNativeAfterPaint=false; state.lastFolderNativePresentAfter=100;
        const auto completed=g_compositionCompleted.load();
        TrySyncLastFolderSelection(state); Check(!native->commits,"selection guessed compositor completion");
        g_compositionCompleted=100; OverlayProc(state.hwnd,kMsgFrame,0,0);
        Check(!state.lastFolderNativePresentAfter,"real compositor completion failed to release selection");
        g_compositionCompleted=completed;
        native->rejectSelection=true;
        for(int attempt=0;attempt<12;++attempt) {
            state.lastFolderNativeNextAttempt=0; TrySyncLastFolderSelection(state);
            Check(!state.lastFolderNativePending.empty(),"slow native selection expired after a retry count");
        }
        native->rejectSelection=false; state.lastFolderNativeNextAttempt=0; TrySyncLastFolderSelection(state);
        Check(state.lastFolderNativePending.empty() && native->commits>=13,"native selection never resumed when its model completed");
        const auto beforeRebind=native->commits;
        state.lastFolderNativePending=L"file0.txt"; state.lastFolderNativeNextAttempt=0;
        browser->queryContext=&state;
        browser->duringQuery=+[](void* context) {
            auto& live=*static_cast<ViewState*>(context);
            ++live.token; live.lastFolderNativePending=L"file1.txt";
        };
        TrySyncLastFolderSelection(state);
        Check(native->commits==beforeRebind && state.lastFolderNativePending==L"file1.txt",
            "late Shell query licensed an obsolete selection or cleared a newer request");
        browser->duringQuery=nullptr; state.lastFolderNativePending.clear();
        // An expired fast-retry window changes pacing, never view identity.
        HWND candidate=CreateWindowExW(0,L"STATIC",L"pending view",WS_CHILD|WS_VISIBLE,0,0,1,1,frame,nullptr,cls.hInstance,nullptr);
        state.pendingShellView=candidate; state.navProbeUntil=1; browser->result=E_PENDING;
        OverlayProc(state.hwnd,WM_TIMER,kTimerId,0);
        Check(state.pendingShellView==candidate && state.inScope,"expired navigation window abandoned a live pending view");
        ClearNavigationProbe(state); DestroyWindow(candidate); browser->result=S_OK;
        ComPtr<TimingAutomation> automation; automation.Attach(new TimingAutomation);
        for(int attempt=0;attempt<7;++attempt) {
            Check(!AttachShellSelectionEvents(state,automation.Get()) && state.selectionEventBindAttempts &&
                state.selectionEventBindNext,"slow automation exhausted its subscription attempts");
        }
        automation->readiness=E_NOINTERFACE;
        Check(AttachShellSelectionEvents(state,automation.Get()) && !state.selectionEventBindAttempts,
            "explicit unsupported automation did not settle retry work");
        // Selection must not depend on navigation-event Advise succeeding.
        automation->readiness=E_PENDING; automation->connectionReadiness=E_PENDING;
        browser->destinationReadiness=E_PENDING;
        BindShellAutomation(state,automation.Get());
        AttachShellDestinationEvents(state,browser);
        Check(!state.navigationEventProbed && !state.explorerNavigationProbed &&
            state.selectionEventBindAttempts,"transient subscription failure became permanent unsupported status");
        automation->readiness=E_NOINTERFACE; automation->connectionReadiness=E_NOINTERFACE;
        browser->destinationReadiness=E_NOINTERFACE;
        state.shellSubscriptionNextProbe=0; state.selectionEventBindNext=0;
        RetryShellSubscriptions(state,GetTickCount64());
        Check(state.navigationEventProbed && state.explorerNavigationProbed && !state.selectionEventBindAttempts,
            "independent subscription completions failed to resume on idle");
        // Exact created identity and context, rather than 3/4-second deadlines,
        // survive late enumeration. No real file creation/rename is invoked.
        FocusFixture::focus=state.hwnd;
        state.pendingCreatedItemPath=state.entries[0].path; state.pendingCreatedItemAt=1;
        state.pendingCreatedItemNative=state.nativeHwnd; state.pendingCreatedItemToken=state.token;
        state.pendingCreatedItemRefreshAttempts=8; state.directoryPending=true; state.loaded=false;
        TryStartCreatedItemRename(state);
        Check(!state.pendingCreatedItemPath.empty(),"delayed created item expired before names completed");
        state.directoryPending=false; state.loaded=true; TryStartCreatedItemRename(state);
        Check(state.rename.editor && state.pendingCreatedItemPath.empty(),"late names never started the licensed created-item editor");
        CancelRename(state);
        ArmInteractiveCreation(state,3,L"timing fixture"); state.creationCommandAt=1;
        WatchDirectory(state);
        Check(state.creationCommandMask,"watch setup erased same-generation creation intent");
        OverlayProc(state.hwnd,WM_KEYDOWN,VK_ESCAPE,0);
        Check(!state.creationCommandMask,"new explicit input failed to cancel old creation intent");
        state.shellBrowser.Reset();
        state.shellAutomationBrowser.Reset(); state.navigationEventSource.Reset();
        state.explorerNavigationSource.Reset();
    }
    Check(GetFocus()==actualFocus && GetForegroundWindow()==actualForeground,"timing tests changed user focus");
    CancelRename(state); CancelAutoRefit(state); Stop(state.background); DropTarget(state);
    UnregisterAnimationWindow(state.hwnd);
    SetWindowLongPtrW(state.hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original));
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(frame);
    g_autoRefitColumns=autoFit; g_writeFactory=factory;
    std::cout<<"PASS: completion-driven auto fit, debounced wheel/pinch zoom with animation/gesture/metadata completion and cancellation, partial/stale/delayed metadata, input/resize completion, collapsed samples, compositor barrier, persistent native selection/navigation/subscription retries and delayed creation with cancellation\n";
}
