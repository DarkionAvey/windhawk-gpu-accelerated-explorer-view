// Count live-view queries while running actual timer, input, projection and
// startup paths. All windows stay off screen and file access is read-only.
static void TestTypeSearchPrefix() {
    Check(MatchesTypeSearchPrefix(L"Flower in the park.jpg", L"f") &&
          MatchesTypeSearchPrefix(L"Flower in the park.jpg", L"flower") &&
          MatchesTypeSearchPrefix(L"folder", L"FOL"),
          "type-search prefix matching is not case-insensitive/anchored");
    Check(!MatchesTypeSearchPrefix(L"A flower.jpg", L"flower") &&
          !MatchesTypeSearchPrefix(L"file.txt", L"files") &&
          !MatchesTypeSearchPrefix(L"file.txt", L""),
          "type-search highlighted a non-prefix or empty query");
    std::cout << "PASS: visible quick-search highlights share the type-to-select prefix matcher\n";
}

class FeedbackFolderView final : public AttachmentFolderView {
public:
    unsigned queries = 0;
    HRESULT STDMETHODCALLTYPE GetCurrentViewMode(UINT* out) override { ++queries; *out = mode; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetGroupBy(PROPERTYKEY*, BOOL*) override { ++queries; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetFolder(REFIID iid, void** out) override { ++queries; return folder->QueryInterface(iid, out); }
    HRESULT STDMETHODCALLTYPE ItemCount(UINT, int* out) override { ++queries; *out = 0; return S_OK; }
};

static void TestUiFeedbackAudit(const std::wstring& directory) {
    WorkerComApartment apartment;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))), "feedback DirectWrite");
    ViewState state;
    state.hwnd = IconTestWindow(); state.frameHwnd = state.activeTabHost = state.hwnd;
    HWND host = CreateWindowExW(0, L"CtrlNotifySink", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 420, 800, state.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    state.nativeHwnd = CreateWindowExW(0, L"SHELLDLL_DefView", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 420, 800, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(state.nativeHwnd && SuppressNativeContent(state), "feedback native host");
    auto* view = new FeedbackFolderView;
    state.folderView.Attach(view); view->mode = FVM_LIST;
    ComPtr<IShellItem> item;
    Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr, BHID_SFObject, IID_PPV_ARGS(&state.folder))), "feedback folder");
    view->folder = state.folder;
    state.background->hwnd = state.hwnd;
    state.background->directoryGeneration = state.directoryToken = 911;
    state.visible = state.inScope = state.detailsViewActive = state.loaded = true;
    state.columnsView = false; state.rowPx = 34.f; state.fontPx = 16.f; state.iconPx = 20.f;
    state.folderPath = directory; state.firstContentSubmittedAt = state.attachedAt = GetTickCount64();
    state.featureInitializationStep = 5; state.lastActiveViewProbe = GetTickCount64();
    Check(EnsureTarget(state), "feedback GPU target"); UpdateRefreshRate(state);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    auto original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ColdScrollProc)));
    state.displayedOrderHash = state.displayedMetadataHash = 1;
    state.displayedSort = state.sort; state.displayedGrouping = state.grouping;
    DirectoryResult unchanged; unchanged.kind = DirectoryResult::Complete;
    unchanged.token = state.directoryToken; unchanged.orderHash = unchanged.metadataHash = 1;
    PublishDirectoryResult(*state.background, std::move(unchanged));
    view->queries = 0; const auto quietPaints = coldScrollPaints;
    OverlayProc(state.hwnd, kMsgDirectoryReady, 0, 0);
    Check(view->queries == 0 && coldScrollPaints == quietPaints,
          "unchanged directory revalidation forced native presentation or paint");
    DirectoryResult ignored; ignored.kind = DirectoryResult::Preview; ignored.token = state.directoryToken;
    ignored.entries.emplace_back(); PublishDirectoryResult(*state.background, std::move(ignored));
    Check(!ReceiveDirectoryResults(state), "ignored loaded-view preview was reported as changed content");
    // These phases were absent from the old timer's drag/collapse-only guard.
    for (bool* active : {&state.animationActive, &state.zoomAnimating, &state.keyboardSelectionAnimating,
                        &state.columnTransitionActive, &state.hoverAnimationActive, &state.manipulationFrameActive, &state.marqueePossible,
                        &state.columnDragArmed}) {
        *active = true; state.lastNativeModeProbe = 0; view->queries = 0;
        OverlayProc(state.hwnd, WM_TIMER, kTimerId, 0);
        Check(view->queries == 0, "idle timer queried live Shell during wheel/zoom/keyboard/direct input");
        *active = false;
    }
    state.lastInteractionTick=GetTickCount64(); view->queries=0;
    OverlayProc(state.hwnd,WM_TIMER,kTimerId,0);
    Check(view->queries==0,"native maintenance ran between pointer samples after hover settled");
    state.lastInteractionTick=0;
    // Liveness: the guard must not disable ordinary idle maintenance.
    state.lastNativeModeProbe = 0; OverlayProc(state.hwnd, WM_TIMER, kTimerId, 0);
    Check(view->queries != 0, "idle Shell maintenance never resumed");
    state.featureInitializationStep = 3; state.featureInitializationQueued = true;
    state.animationActive = true; view->queries = 0;
    InitializeOverlayFeatures(state);
    Check(state.featureInitializationStep == 3 && !state.featureInitializationQueued && view->queries == 0,
          "optional startup continued its provider chain during animation");
    state.animationActive = false;
    AdvanceAnimationFrame(state, false);
    MSG message{};
    Check(state.featureInitializationQueued && PeekMessageW(&message, state.hwnd,
        kMsgInitializeFeatures, kMsgInitializeFeatures, PM_REMOVE), "deferred optional startup did not resume on settlement");
    // A deferred provider step must not keep the input-service step behind it.
    state.featureInitializationStep = 0; state.animationActive = true;
    const bool touchpad = g_touchpadDirectManipulation.exchange(false);
    InitializeOverlayFeatures(state);
    Check(state.featureInitializationStep == 1 && view->queries == 0,
          "active input prevented input-service initialization");
    g_touchpadDirectManipulation = touchpad; state.animationActive = false;
    while (PeekMessageW(&message, state.hwnd, kMsgInitializeFeatures, kMsgInitializeFeatures, PM_REMOVE)) {}
    state.featureInitializationStep = 5; state.featureInitializationQueued = false;
    providerGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(FreezeDetailProviders(state), "feedback property gate");
    state.directoryPending = false; state.columnsView = true;
    ExplorerColumn author; author.key = PKEY_Author; author.title = L"Authors"; author.width = 350;
    state.columns = {author}; UpdateColumns(state);
    for (int i = 0; i < 800; ++i) {
        Entry entry; entry.name = L"file" + std::to_wstring(i) + L".txt";
        entry.path = JoinPath(directory, entry.name); state.entries.push_back(std::move(entry));
    }
    state.total = static_cast<int>(state.entries.size());
    for (int i=0;i<state.total;++i)
        state.entryPathIndex.emplace_back(SelectionPathHash(state.entries[i].path),i);
    std::sort(state.entryPathIndex.begin(),state.entryPathIndex.end());
    auto& icon = state.icons.try_emplace(L"type:.txt").first->second;
    icon.resolved = icon.association = true;
    icon.pixels.width = icon.pixels.height = 8; icon.pixels.stride = 32; icon.pixels.data.assign(256, 255);
    BYTE savedKeys[256]{}, keys[256]{}; GetKeyboardState(savedKeys); SetKeyboardState(keys);
    const bool smooth = g_smooth.exchange(true), swipe = g_touchpadSwipeNavigation.exchange(false);
    auto queuedRequests = [&] {
        ExclusiveSrwLock lock(state.background->directoryLock);
        size_t count = 0;
        for (const auto& batch : state.background->detailJobs) count += batch.requests.size() - batch.nextRequest;
        return count;
    };
    for (int i = 0; i < 200; ++i)
        OverlayProc(state.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0);
    Check(queuedRequests() == 0, "wheel samples submitted provider demand before their paint");
    AdvanceAnimationFrame(state, true);
    Check(queuedRequests() > 0 && queuedRequests() <= 8, "wheel frame did not submit one metadata follow-up");
    StopScrollAnimation(state); InvalidateDetailJobs(state);
    state.manipulationEnabled = state.manipulationFrameActive = true;
    state.swipeReleased = state.touchpadPinching = false;
    for (int i = 0; i < 200; ++i) OverlayProc(state.hwnd, kMsgManipulationPan, 0, -256);
    Check(queuedRequests() == 0, "touchpad samples repeatedly refilled visible data before a frame");
    AdvanceAnimationFrame(state, true);
    Check(queuedRequests() > 0 && queuedRequests() <= 8, "touchpad frame lost its data follow-up");
    InvalidateDetailJobs(state);
    ManipulationHandler gesture(state.hwnd);
    int historyActions=0;
    auto status = [&](DIRECTMANIPULATION_STATUS current, DIRECTMANIPULATION_STATUS previous) {
        gesture.OnViewportStatusChanged(nullptr,current,previous);
        MSG input{};
        while (PeekMessageW(&input,state.hwnd,kMsgManipulationPan,kMsgManipulationNavigate,PM_REMOVE)) {
            if (input.message==kMsgManipulationNavigate) ++historyActions;
            else OverlayProc(state.hwnd,input.message,input.wParam,input.lParam);
        }
    };
    auto pan = [&](int x, int y) {
        OverlayProc(state.hwnd,kMsgManipulationPan,
            static_cast<WPARAM>(static_cast<INT_PTR>(x*256)),static_cast<LPARAM>(y*256));
    };
    status(DIRECTMANIPULATION_RUNNING,DIRECTMANIPULATION_READY);
    pan(0,-40);
    const float contactDistance=state.touchpadPanY, releasePosition=state.scrollNow;
    status(DIRECTMANIPULATION_INERTIA,DIRECTMANIPULATION_RUNNING);
    for (int delta : {-32,-16,-8,-4}) pan(0,delta);
    Check(state.swipeReleased && state.manipulationFrameActive &&
        fabsf(state.scrollNow-releasePosition-60.f)<.01f && !state.animationActive &&
        state.scrollTarget==state.scrollNow && state.touchpadPanY==contactDistance &&
        queuedRequests()==0,
        "OS inertia was dropped, eased twice, changed contact distance or ran provider demand per sample");
    status(DIRECTMANIPULATION_RUNNING,DIRECTMANIPULATION_INERTIA);
    const float interrupted=state.scrollNow;
    pan(0,20);
    Check(!state.swipeReleased && state.touchpadPanY==20.f &&
        state.scrollNow==interrupted-20.f,"new touchpad contact did not interrupt/reverse inertia");
    status(DIRECTMANIPULATION_READY,DIRECTMANIPULATION_RUNNING);
    const float settled=state.scrollNow;
    pan(0,-80);
    Check(!state.manipulationFrameActive && state.scrollNow==settled,
        "queued pan after READY restarted scrolling");
    g_touchpadSwipeNavigation=true;
    for (int distance : {100,1000}) {
        status(DIRECTMANIPULATION_RUNNING,DIRECTMANIPULATION_READY);
        pan(distance,0);
        status(DIRECTMANIPULATION_INERTIA,DIRECTMANIPULATION_RUNNING);
        pan(1000,-100); // Cannot complete a short swipe or move/revive its fading arrow.
        status(DIRECTMANIPULATION_READY,DIRECTMANIPULATION_INERTIA);
        Check(historyActions==(distance==1000 ? 1 : 0) && state.scrollNow==settled &&
            !state.swipeFeedbackActive,"inertia extended/repeated a history swipe or scrolled its list");
    }
    g_touchpadSwipeNavigation=false;
    status(DIRECTMANIPULATION_RUNNING,DIRECTMANIPULATION_READY);
    state.touchpadPinching=true;
    pan(0,-100);
    status(DIRECTMANIPULATION_INERTIA,DIRECTMANIPULATION_RUNNING);
    pan(0,-100);
    Check(state.scrollNow==settled,"pinch translation leaked into contact or inertial scrolling");
    status(DIRECTMANIPULATION_READY,DIRECTMANIPULATION_INERTIA);
    status(DIRECTMANIPULATION_RUNNING,DIRECTMANIPULATION_READY);
    status(DIRECTMANIPULATION_INERTIA,DIRECTMANIPULATION_RUNNING);
    PostMessageW(state.hwnd,kMsgManipulationPan,0,-256);
    OverlayProc(state.hwnd,WM_KEYDOWN,VK_SHIFT,0);
    Check(!state.manipulationFrameActive && state.manipulationEnabled &&
        !PeekMessageW(&message,state.hwnd,kMsgManipulationPan,kMsgManipulationNavigate,PM_NOREMOVE),
        "new input did not stop inertia/drain old samples or discarded the reusable viewport");
    std::cout << "PASS: DirectManipulation RUNNING/INERTIA/READY routing, unchanged OS deltas, interruption, no repeated history/pinch scroll and paced provider demand\n";
    state.manipulationEnabled = state.manipulationFrameActive = false;
    InvalidateDetailJobs(state);
    // A selection observation remains dirty through motion and is woken at
    // settlement. The explicit native commit/identity tests run separately.
    state.animationActive = true; state.nativeSelectionDirty = state.nativeSelectionMessageQueued = false;
    QueueNativeSelectionHint(state);
    Check(state.nativeSelectionDirty && !state.nativeSelectionMessageQueued,
          "selection observation posted a Shell read during input");
    state.animationActive = false; AdvanceAnimationFrame(state, false);
    Check(state.nativeSelectionMessageQueued, "selection observation was stranded after motion");
    while (PeekMessageW(&message, state.hwnd, kMsgNativeSelectionChanged, kMsgNativeSelectionChanged, PM_REMOVE)) {}
    state.nativeSelectionDirty = state.nativeSelectionMessageQueued = false;
    // A scroll after selection owns the viewport even when the native view
    // emits a late echo as cold thumbnails/properties settle. Exercise the
    // shared import in both a filesystem folder and Recycle Bin.
    for (const auto& path : {directory,std::wstring(kRecycleBinLocation)}) {
        state.folderPath=path;
        state.selectedFlags.assign(state.total,0); state.selectedFlags[7]=1;
        state.selected=state.anchor=7;
        SetScrollImmediate(state,0.f);
        SetScrollImmediate(state,MaxScrollPx(state));
        const float viewport=state.scrollNow;
        state.nativeSelectionPaths={state.entries[7].path};
        state.nativeFocusedPath=state.entries[7].path;
        state.nativeSelectionImportPending=true;
        ApplyImportedNativeSelection(state,true);
        Check(state.scrollNow==viewport && state.scrollTarget==viewport && state.selected==7,
            "late unchanged native selection jumped from cold rows 600-800 to item 7");
    }
    // A genuine NEW external selection still reveals its item.
    state.nativeSelectionPaths={state.entries[11].path}; state.nativeFocusedPath=state.entries[11].path;
    state.nativeSelectionImportPending=true;
    ApplyImportedNativeSelection(state,false);
    Check(state.selected==11 && state.scrollNow==MaxScrollPx(state) &&
        state.animationActive && state.scrollTarget<state.scrollNow,
        "viewport ownership prevented a genuine external selection reveal");
    QueryPerformanceCounter(&state.smoothStartedQpc);
    state.smoothStartedQpc.QuadPart-=static_cast<LONGLONG>(state.qpcFrequency*.181);
    AdvanceScroll(state);
    state.directoryModePinned=true;
    state.hoverAnimationActive=true; state.lastInteractionTick=0; view->queries=0;
    for (int i=0;i<120;++i) {
        PostShellVisibilityProbe(state,true); PostShellVisibilityProbe(state);
    }
    // Include notifications already queued before the next input sample.
    OverlayProc(state.hwnd,kMsgShellVisibilityChanged,2,0);
    OverlayProc(state.hwnd,kMsgShellVisibilityChanged,0,0);
    OverlayProc(state.hwnd,WM_TIMER,kTimerId,0);
    Check(view->queries==0 && !state.nativeModeHintPending && state.nativeSettingsHintPending,
        "native provider reorder burst queried settings between hover/scroll frames");
    state.hoverAnimationActive=false;
    AdvanceAnimationFrame(state,false);
    while (PeekMessageW(&message,state.hwnd,kMsgShellVisibilityChanged,kMsgShellVisibilityChanged,PM_REMOVE))
        OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
    Check(view->queries==0 && !state.nativeModeHintPending && !state.nativeSettingsHintPending,
        "namespace background reorders repolled live mode/grouping after motion");
    FinishColumnTransition(state);
    state.cachedNativeMode=state.layoutNativeMode=view->mode=FVM_DETAILS;
    state.columnsView=true; UpdateMetrics(state); UpdateColumns(state);
    state.nativeColumnsDirty=false; state.lastColumnsProbe=1;
    state.lastNativeModeProbe=1; state.lastActiveViewProbe=0;
    state.nativeSelectionDirty=state.nativeSelectionMessageQueued=state.nativeSelectionImportPending=false;
    state.nativeSelectionLastProbe=0; state.nativeSelectionObservedCount=1;
    state.lastThemeCheck=state.lastVisibilityProbe=state.lastRefresh=GetTickCount64();
    state.nativeSelectionWarmUntil=GetTickCount64()+5000;
    for (int i=0;i<12;++i) {
        state.nativeSelectionLastProbe=0;
        OverlayProc(state.hwnd,WM_TIMER,kTimerId,0);
    }
    Check(view->queries==0,"Recycle Bin idle timer polled live selection/mode/group/column providers");
    state.hoverAnimationActive=true;
    state.layoutNativeMode=state.cachedNativeMode=FVM_DETAILS; view->mode=FVM_DETAILS;
    { NativeViewInput input(&state); }
    Check(PeekMessageW(&message,state.hwnd,kMsgShellVisibilityChanged,kMsgShellVisibilityChanged,PM_REMOVE),
        "explicit view command waited behind hover instead of posting immediately");
    OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
    Check(view->queries>0 && !state.nativeModeSignalQueued,
        "namespace native view commands stopped observing changed settings");
    state.hoverAnimationActive=false; state.lastInteractionTick=0;
    state.folderPath=directory;
    // A late capacity value changes pixels but must not duplicate pending
    // extra-property jobs or take the native projection/forced-paint handler.
    state.thisPcGpu = true; state.folderPath = kThisPcLocation;
    state.nativeRowsNeedCapture = false; state.nativeRowsComplete = true;
    Entry drive; drive.folder = true; drive.path = L"C:\\"; drive.name = L"Drive";
    state.entries = state.nativeRows = {drive}; state.total = 1;
    state.thisPcSpaceRequestedPaths = {drive.path};
    SetScrollImmediate(state, 0.f);
    QueueVisibleDetails(state);
    Check(queuedRequests() == 1 && state.entries[0].detailsPending, "This PC property demand");
    DriveCapacity capacity; capacity.root = drive.path; capacity.valid = capacity.hasFreeBytes = true;
    capacity.total = 100; capacity.used = 25; capacity.totalText = L"100 B"; capacity.freeText = L"75 B";
    state.background->capacityGeneration = state.capacityToken = state.directoryToken;
    DirectoryResult result; result.kind = DirectoryResult::DriveSpace; result.token = state.capacityToken;
    result.driveSpace.push_back(capacity); PublishDirectoryResult(*state.background, std::move(result));
    OverlayProc(state.hwnd, WM_APP+64, 0, 0);
    view->queries = 0; const unsigned paints = coldScrollPaints;
    AdvanceAnimationFrame(state, true);
    Check(state.entries[0].driveUsed == 25 && state.entries[0].detailsPending && queuedRequests() == 1,
          "This PC capacity projection reset pending metadata or duplicated provider work");
    Check(!PeekMessageW(&message, state.hwnd, kMsgThisPcRows, kMsgThisPcRows, PM_REMOVE) &&
        coldScrollPaints == paints && view->queries == 0, "This PC enrichment reentered native maintenance or synchronous paint");
    QueueNativeRows(state, true);
    Check(PeekMessageW(&message, state.hwnd, kMsgThisPcRows, kMsgThisPcRows, PM_REMOVE),
          "real namespace identity refresh lost its immediate wake");
    OverlayProc(state.hwnd, message.message, message.wParam, message.lParam);
    Check(state.nativeRowsPending && view->queries == 0 && coldScrollPaints == paints,
          "namespace capture hint repainted or polled the live view");
    // Queued work from a departed tab must not spend another UI turn binding
    // providers or fitting text. Pending intent is retained for activation.
    const HWND activeTab=state.activeTabHost;
    state.activeTabHost=nullptr; state.featureInitializationStep=3;
    state.firstContentSubmittedAt=1; view->queries=0;
    state.nativeModeHintPending=state.nativeSettingsHintPending=true;
    state.nativeModeSignalQueued=state.visibilitySignalQueued=true;
    InitializeOverlayFeatures(state);
    OverlayProc(state.hwnd,kMsgShellVisibilityChanged,2,0);
    OverlayProc(state.hwnd,kMsgShellVisibilityChanged,0,0);
    Check(state.featureInitializationStep==3 && state.nativeModeHintPending &&
        state.nativeSettingsHintPending && view->queries==0,
        "inactive tab ran queued provider maintenance");
    state.activeTabHost=activeTab;
    std::weak_ptr<TabBackground> lifetime = state.background;
    Stop(state.background); SetEvent(providerGate);
    Check(Eventually([&] { return lifetime.expired(); }), "feedback workers did not retire");
    CloseHandle(providerGate); providerGate = nullptr;
    g_smooth = smooth; g_touchpadSwipeNavigation = swipe; SetKeyboardState(savedKeys);
    KillTimer(state.hwnd, kTimerId); UnregisterAnimationWindow(state.hwnd);
    DropTarget(state); RestoreNativeContent(state);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
    SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
    DestroyWindow(state.hwnd); g_writeFactory.Reset();
    std::cout << "PASS: hover/pointer/scroll-safe native hints, stable viewport after late selection echoes, event-driven Recycle Bin settings, immediate view commands, batched demand and This PC enrichment\n";
}
