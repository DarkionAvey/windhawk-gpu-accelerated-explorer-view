// Cold column catalogs use real worker-side folder/schema calls. Only the live
// view's key snapshot is substituted, with thread/call-count assertions.
static std::atomic<unsigned> shellApartmentState{0};
static DWORD WINAPI PumpingShellWorker(LPVOID context) {
    auto& worker=*static_cast<TabBackground::Worker*>(context);
    WorkerComApartment apartment;
    APTTYPE type{}; APTTYPEQUALIFIER qualifier{};
    if (FAILED(apartment.result) || FAILED(CoGetApartmentType(&type,&qualifier)) || type!=APTTYPE_STA) return 0;
    HWND window=MessageWindow();
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(worker.wake));
    SetTimer(window,1,1,[](HWND hwnd,UINT,UINT_PTR,DWORD) {
        shellApartmentState.fetch_or(2);
        SetEvent(reinterpret_cast<HANDLE>(GetWindowLongPtrW(hwnd,GWLP_USERDATA)));
    });
    shellApartmentState.fetch_or(1);
    if (apartment.Wait(worker.wake)==WAIT_OBJECT_0) shellApartmentState.fetch_or(4);
    KillTimer(window,1); DestroyWindow(window);
    return 0;
}
static void TestWorkerApartment() {
    auto work=std::make_shared<TabBackground>(); shellApartmentState=0;
    Check(StartTabWorker(work,work->directory,PumpingShellWorker,THREAD_PRIORITY_NORMAL),"Shell apartment worker");
    const bool pumped=Eventually([] { return shellApartmentState.load()==7; },2000);
    Stop(work);
    Check(pumped,"Shell worker was not an independent STA or failed to pump its idle messages");
    std::cout << "PASS: Shell worker owns its STA and dispatches provider messages while idle\n";
}

static void TestSnapshotSelectionRestoration(const std::wstring& directory) {
    for (int count : {0, 1, 64, 65, 1024}) {
        ViewState state;
        state.hwnd = MessageWindow();
        state.background->hwnd = state.hwnd;
        state.folderPath = directory;
        state.visible = state.inScope = state.columnsView = false;
        state.loaded = true; state.rowPx = 34.f;
        state.total = 1024;
        state.selectedFlags.assign(state.total, 0);
        for (int i = 0; i < state.total; ++i) {
            Entry row;
            row.name = L"selection-row-" + std::to_wstring(i);
            row.path = JoinPath(directory, row.name);
            row.byteSize = static_cast<ULONGLONG>(i); // Fixture ID, independent of row position.
            state.entries.push_back(std::move(row));
            if (count && (i < count-1 || i == 1023)) state.selectedFlags[i] = 1;
        }
        state.selected = count ? 1023 : -1;
        state.directoryToken = 300;
        state.background->directoryGeneration = state.directoryToken;
        DirectoryResult result;
        result.kind = DirectoryResult::Complete;
        result.token = state.directoryToken;
        result.hwnd = state.hwnd;
        for (int i = 1023; i >= 0; --i)
            if (i != 17) result.entries.push_back(state.entries[i]);
        Entry added;
        added.name = L"inserted"; added.path = JoinPath(directory, added.name); added.byteSize = 1024;
        result.entries.push_back(std::move(added));
        for (size_t i = 0; i < result.entries.size(); ++i)
            result.pathIndex.emplace_back(SelectionPathHash(result.entries[i].path), static_cast<int>(i));
        std::sort(result.pathIndex.begin(), result.pathIndex.end());
        PublishDirectoryResult(*state.background, std::move(result));
        ReceiveDirectoryResults(state);
        Check(state.loaded && state.total == 1024 && state.selected == (count ? 0 : -1),
              "snapshot refresh lost focus across reorder/delete/insert");
        for (size_t i = 0; i < state.entries.size(); ++i) {
            const auto id = state.entries[i].byteSize;
            const bool selected = count && id < 1024 &&
                (id < static_cast<ULONGLONG>(count-1) || id == 1023);
            Check((state.selectedFlags[i] != 0) == selected,
                  "snapshot refresh lost sparse/dense selection identities or selected an inserted row");
        }
        Stop(state.background);
        DestroyWindow(state.hwnd);
    }
    std::cout << "PASS: empty, sparse, threshold and Select All selections retain identities/focus across reorder, deletion and insertion\n";
}

class CatalogColumnManager final : public IColumnManager {
    ULONG refs = 1;
public:
    DWORD owner = GetCurrentThreadId();
    unsigned descriptors = 0, snapshots = 0, visibleSnapshots = 0;
    unsigned unavailableReads = 0;
    void (*duringCapture)(void*) = nullptr;
    void* captureContext = nullptr;
    std::vector<PROPERTYKEY> keys{PKEY_FileName, PKEY_Author, PKEY_DateCreated};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        *out = nullptr;
        if (iid != IID_IUnknown && iid != IID_IColumnManager) return E_NOINTERFACE;
        *out = static_cast<IColumnManager*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG n = --refs; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetColumnCount(CM_ENUM_FLAGS, UINT* out) override {
        Check(GetCurrentThreadId() == owner, "column view crossed apartments");
        if (unavailableReads) { --unavailableReads; *out=0; return E_PENDING; }
        *out = static_cast<UINT>(keys.size()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetColumns(CM_ENUM_FLAGS flags, PROPERTYKEY* out, UINT count) override {
        Check(GetCurrentThreadId() == owner && count == keys.size(), "column key capture");
        ++snapshots;
        if(flags==CM_ENUM_VISIBLE) ++visibleSnapshots;
        if (duringCapture) duringCapture(captureContext);
        std::copy(keys.begin(), keys.end(), out); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetColumnInfo(REFPROPERTYKEY, CM_COLUMNINFO*) override {
        ++descriptors; return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetColumnInfo(REFPROPERTYKEY, const CM_COLUMNINFO*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetColumns(const PROPERTYKEY*, UINT) override { return E_NOTIMPL; }
};

static void TestDeferredColumnCapture(const std::wstring& directory) {
    const bool autoFit=g_autoRefitColumns.exchange(false);
    auto previousFactory=g_writeFactory;
    if (!g_writeFactory)
        Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"deferred columns DirectWrite");
    ViewState state;
    state.hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"deferred columns",
        WS_POPUP|WS_VISIBLE,-32000,-32000,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.hwnd!=nullptr,"deferred column window");
    state.frameHwnd=state.nativeHwnd=state.activeTabHost=state.hwnd;
    state.background->hwnd=state.hwnd;
    state.visible=state.inScope=state.detailsViewActive=state.loaded=state.columnsView=true;
    state.folderPath=directory; state.directoryToken=501; state.background->directoryGeneration=501;
    state.featureInitializationStep=3; // Input ready; optional automation still deferred.
    state.firstContentSubmittedAt=GetTickCount64(); // This PC has already painted in this tab.
    state.folderView.Attach(new AttachmentFolderView);
    auto* manager=new CatalogColumnManager;
    manager->keys={PKEY_FileName,PKEY_DateModified,PKEY_ItemTypeText,PKEY_Size};
    state.columnManager.Attach(manager);
    ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=220;
    state.columns={name}; state.entries.resize(1); state.total=1;
    state.entries[0].path=directory+L"\\file0.txt"; state.entries[0].name=L"file0.txt";
    state.selectedFlags={0};
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    UpdateMetrics(state); UpdateColumns(state); UpdateRefreshRate(state);
    Check(EnsureTarget(state),"deferred columns target");
    const auto hover=[&] {
        state.hoverAnimationActive=true; QueryPerformanceCounter(&state.hoverStartedQpc);
        state.lastInteractionTick=GetTickCount64();
    };
    const auto capture=[&] {
        MSG message{}; bool ran=false;
        while(PeekMessageW(&message,state.hwnd,kMsgInitializeFeatures,kMsgInitializeFeatures,PM_REMOVE)) {
            ran=true; hover(); OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
        }
        return ran;
    };
    RefreshExplorerColumns(state,true);
    Check(manager->snapshots==0,"native columns ran before the destination's first paint");
    Check(Eventually([&] { hover(); Paint(state); return !state.deferColumnProviders; }),
        "destination names never released the column dependency");
    Check(capture() && state.columns.size()==4 && !state.nativeColumnsDirty &&
        state.featureInitializationStep==3 && manager->visibleSnapshots==1 && !state.autoRefitPending,
        "required headings waited for optional startup/idle/input or depended on auto-fit");
    state.nativeColumnsDirty=true; state.lastColumnsProbe=0;
    for(bool* eligible : {&state.visible,&state.inScope,&state.detailsViewActive}) {
        *eligible=false;
        Check(!NativeColumnSnapshotReady(state),"native-only/out-of-scope presentation scheduled required GPU columns");
        *eligible=true;
    }
    for(bool finishByInput : {false,true}) {
        state.featureInitializationStep=kOverlayFeatureSteps;
        state.columns={name}; state.nativeColumnsDirty=true; state.lastColumnsProbe=0;
        state.columnsView=false; state.headerPx=0; UpdateColumns(state);
        SetColumnViewMode(state,true);
        QueueFeatureInitialization(state);
        Check(state.columnModeTransition && !state.featureInitializationQueued,
            "column snapshot ignored the active header transition");
        hover();
        if (finishByInput) FinishColumnTransition(state);
        else {
            state.columnTransitionStartedQpc.QuadPart-=static_cast<LONGLONG>(state.qpcFrequency*.2);
            AdvanceColumnTransition(state);
        }
        Check(state.columnPresentationPending && !NativeColumnSnapshotReady(state),
            "elapsed header duration licensed providers before a settled submission");
        Paint(state);
        Check(capture() && state.columns.size()==4 && !state.nativeColumnsDirty,
            "header settlement dropped the pending column snapshot after startup finished");
    }
    FinishColumnTransition(state); Paint(state);
    state.columns={name}; UpdateColumns(state);
    state.nativeColumnsDirty=true; state.lastColumnsProbe=0; manager->unavailableReads=1;
    QueueFeatureInitialization(state); capture();
    Check(state.nativeColumnsDirty && state.lastColumnsProbe && !state.featureInitializationQueued &&
        !NativeColumnSnapshotReady(state),"unready native columns entered a posted-message retry loop");
    state.lastColumnsProbe=GetTickCount64()-351; hover();
    OverlayProc(state.hwnd,WM_TIMER,kTimerId,0);
    Check(capture() && state.columns.size()==4 && !state.nativeColumnsDirty,
        "column retry depended on idle maintenance despite recent pointer activity");
    FinishColumnTransition(state); Paint(state);
    state.columns={name}; UpdateColumns(state); state.nativeColumnsDirty=true; state.lastColumnsProbe=0;
    manager->captureContext=&state;
    manager->duringCapture=+[](void* context) {
        auto& view=*static_cast<ViewState*>(context); ++view.token;
        view.nativeColumnsDirty=true; view.lastColumnsProbe=0;
    };
    QueueFeatureInitialization(state); capture();
    Check(state.columns.size()==1 && state.nativeColumnsDirty,
        "navigation during native column capture committed stale headings");
    manager->duringCapture=nullptr; QueueFeatureInitialization(state); capture();
    Check(state.columns.size()==4 && !state.nativeColumnsDirty,"current binding did not recover after stale capture");
    Check(Eventually([&] {
        hover(); MSG message{};
        while(PeekMessageW(&message,state.hwnd,kMsgIconReady,kMsgIconReady,PM_REMOVE))
            OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
        while(PeekMessageW(&message,state.hwnd,kMsgDetailsReady,kMsgDetailsReady,PM_REMOVE))
            OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
        AdvanceAnimationFrame(state,true); Paint(state);
        return state.entries[0].detailsReady && HasCachedColumnCell(state.entries[0],PKEY_DateModified) &&
            HasCachedColumnCell(state.entries[0],PKEY_ItemTypeText) && HasCachedColumnCell(state.entries[0],PKEY_Size) &&
            !state.textRasterPending;
    }),"required column snapshot failed to start/present background metadata without list input");
    Stop(state.background); UnregisterAnimationWindow(state.hwnd); DropTarget(state);
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.hwnd);
    g_writeFactory=std::move(previousFactory); g_autoRefitColumns=autoFit;
    std::cout<<"PASS: deferred Details headers and metadata resume after first paint/header settlement without idle/input/auto-fit; bounded retry and stale-capture rejection\n";
}

template<class State = ViewState>
static void TestColdColumnCatalog(const std::wstring& directory) {
    State state;
    if constexpr (requires { state.columnCatalogToken; }) {
        WorkerComApartment apartment;
        state.hwnd = MessageWindow();
        state.background->hwnd = state.hwnd;
        state.folderPath = directory;
        state.columnsView = true;
        auto manager = new CatalogColumnManager;
        manager->captureContext = &state;
        manager->duringCapture = +[](void* context) {
            Check(RequestColumnCatalog(*static_cast<State*>(context)), "reentrant column request");
        };
        state.columnManager.Attach(manager);
        ExplorerColumn name;
        name.key = PKEY_FileName; name.title = L"Tab-specific name";
        state.columns.push_back(name);
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
        providerGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        State other;
        other.hwnd = MessageWindow();
        other.background->hwnd = other.hwnd;
        other.folderPath = directory; other.columnsView = true;
        other.columnManager.Attach(new CatalogColumnManager);
        SetWindowLongPtrW(other.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&other));
        Check(StartTabWorker(other.background, other.background->columns, BlockedProvider,
                             THREAD_PRIORITY_BELOW_NORMAL) && RequestColumnCatalog(other),
              "second tab blocked catalog fixture");
        Check(StartTabWorker(state.background, state.background->properties, BlockedProvider,
                             THREAD_PRIORITY_BELOW_NORMAL), "blocked properties fixture");
        Check(RequestColumnCatalog(state), "cold column request");
        const auto token = state.columnCatalogToken;
        Check(RequestColumnCatalog(state) && state.columnCatalogToken == token && manager->snapshots == 1 &&
              manager->descriptors == 0, "column request duplicated work or queried descriptors on the STA");
        Check(Eventually([&] { ReceiveDirectoryResults(state); return state.columnCatalogReady; }),
              "column labels waited for a blocked property lane");
        Check(state.columnMenuChoices.size() == manager->keys.size() &&
              state.columnMenuChoices[0].second == name.title,
              "column catalog lost native key order or an already-known localized title");
        for (size_t i = 0; i < manager->keys.size(); ++i)
            Check(SameKey(state.columnMenuChoices[i].first, manager->keys[i]) &&
                  !state.columnMenuChoices[i].second.empty(), "cold column label resolution");
        Check(!state.loaded && state.entries.empty() && !state.background->directory.thread,
              "column result modified directory content or started enumeration");
        state.columnMenuPending = true;
        OverlayProc(state.hwnd, WM_CANCELMODE, 0, 0);
        Check(!state.columnMenuPending && state.columnCatalogReady, "new input failed to cancel a deferred menu");
        InvalidateColumnCatalog(state);
        using Result = typename std::remove_reference_t<decltype(state.background->directoryResults)>::value_type;
        Result obsolete;
        obsolete.kind = Result::ColumnCatalog;
        obsolete.token = token;
        obsolete.columnChoices.push_back({PKEY_FileName, L"Obsolete folder"});
        PublishDirectoryResult(*state.background, std::move(obsolete));
        ReceiveDirectoryResults(state);
        Check(!state.columnCatalogReady && state.columnMenuChoices.empty(), "obsolete folder catalog was accepted");
        manager->duringCapture=nullptr;
        manager->keys.clear(); state.columns.clear();
        for(unsigned i=0;i<1100;++i) {
            PROPERTYKEY key=PKEY_Author; key.pid+=1000+i;
            manager->keys.push_back(key);
            ExplorerColumn column; column.key=key; column.title=L"Provider field "+std::to_wstring(i); column.width=120;
            state.columns.push_back(std::move(column));
        }
        Check(RequestColumnCatalog(state),"column catalog stopped at 1024 available provider fields");
        Check(Eventually([&] { ReceiveDirectoryResults(state); return state.columnCatalogReady; }),"wide provider catalog did not complete");
        Check(state.columnMenuChoices.size()==1100,"wide provider catalog lost choices");
        state.folderView.Attach(new AttachmentFolderView);
        state.frameHwnd=state.nativeHwnd=state.activeTabHost=state.hwnd;
        state.featureInitializationStep=5; state.deferColumnProviders=false;
        manager->keys.resize(200);
        const bool smooth=g_smooth.exchange(false);
        RefreshExplorerColumns(state,true);
        g_smooth=smooth;
        Check(state.columns.size()==200,"visible native columns stopped at 128 fields");
        InvalidateColumnCatalog(state);
        manager->duringCapture = +[](void* context) {
            auto& view = *static_cast<State*>(context);
            InvalidateColumnCatalog(view);
            ++view.token; // Navigation during the provider's nested dispatch.
        };
        Check(!RequestColumnCatalog(state) && !state.columnCatalogPending &&
              state.columnMenuChoices.empty(), "reentrant navigation queued a stale column capture");
        std::weak_ptr<TabBackground> lifetime = state.background;
        std::weak_ptr<TabBackground> otherLifetime = other.background;
        Stop(state.background);
        Stop(other.background);
        Check(!lifetime.expired() && !otherLifetime.expired(), "blocked column/provider context was freed early");
        SetEvent(providerGate);
        Check(Eventually([&] { return lifetime.expired() && otherLifetime.expired(); }), "column worker was not reclaimed");
        CloseHandle(providerGate); providerGate = nullptr;
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
        DestroyWindow(state.hwnd);
        SetWindowLongPtrW(other.hwnd, GWLP_USERDATA, 0);
        DestroyWindow(other.hwnd);
        std::cout << "PASS: cold column catalog uses its own lane, keeps STA descriptors out, cancels stale menus/results and retires safely\n";
    }
}
