// Real kernel notifications and private files; no Explorer input or user data.
static void TestBurstRowContinuity(const std::wstring& fixture) {
    for (bool grid : {false,true}) for (bool rtl : {false,true}) {
        const auto directory=JoinPath(fixture,L"burst-rows-"+std::to_wstring(grid)+std::to_wstring(rtl));
        std::filesystem::create_directory(directory);
        std::vector<std::wstring> files, restored;
        const auto create=[&](const std::wstring& name) {
            const auto path=JoinPath(directory,name);
            HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,
                FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,CREATE_NEW,0,nullptr);
            Check(file!=INVALID_HANDLE_VALUE,"burst animation fixture"); CloseHandle(file);
            files.push_back(path); return path;
        };
        for(int i=0;i<24;++i) create(L"M-anchor-"+std::to_wstring(i)+L".txt");
        ViewState state;
        state.hwnd=CreateWindowExW(0,L"STATIC",L"private burst geometry",WS_POPUP,
            0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"burst geometry window");
        state.frameHwnd=state.nativeHwnd=state.activeTabHost=state.hwnd;
        state.background->hwnd=state.hwnd; state.folderPath=directory;
        state.visible=state.inScope=state.detailsViewActive=true;
        state.rtlLayout=rtl; state.layoutNativeMode=grid ? FVM_ICON : FVM_DETAILS;
        state.layoutIconSize=48;
        state.sort.explicitOrder=true; state.sort.kind=SortKind::Name;
        state.sort.key=PKEY_FileName; state.sort.direction=SORT_ASCENDING;
        UpdateMetrics(state); UpdateColumns(state); UpdateRefreshRate(state);
        const auto refresh=[&] {
            QueueDirectoryRefresh(state,true);
            auto result=WaitForComplete(*state.background,state.directoryToken);
            PublishDirectoryResult(*state.background,std::move(result));
            ReceiveDirectoryResults(state);
        };
        refresh(); StopRowTransition(state);
        float previousOpacity=0.f;
        for(int i=0;i<10;++i) {
            if(i) state.rowTransitionStartedQpc.QuadPart-=static_cast<LONGLONG>(state.qpcFrequency*.016);
            restored.push_back(create(L"M-anchor-1-restored-"+std::to_wstring(i)+L".txt"));
            refresh();
            Check(state.rowTransitionActive,"insertion did not animate");
            if(!i) {
                // Model the first accepted p=0 frame; the real-GPU test checks
                // first-submission clock ownership. No frame occurs between
                // any of the following nine directory commits.
                QueryPerformanceCounter(&state.rowTransitionStartedQpc);
            } else {
                Check(state.rowTransitionStartedQpc.QuadPart!=0,"insert burst reset its started animation to unpainted");
                const int first=GpuRowForNativePath(state,restored[0]);
                const float opacity=LayoutRowVisual(state,first,0.f).opacity;
                Check(opacity>.001f && opacity>=previousOpacity-.0001f &&
                    (previousOpacity>=.999f || opacity>previousOpacity+.00001f),
                    "insert burst froze a visible row until notifications stopped");
                previousOpacity=opacity;
            }
        }
        StopRowTransition(state);
        float previousGhost=1.f;
        for(int i=0;i<10;++i) {
            if(i) state.rowTransitionStartedQpc.QuadPart-=static_cast<LONGLONG>(state.qpcFrequency*.016);
            Check(DeleteFileW(restored[i].c_str()),"burst deletion"); refresh();
            if(!state.rowTransitionActive) std::cerr << "BURST no deletion animation grid=" << grid << " rtl=" << rtl <<
                " step=" << i << " rows=" << state.total << " scroll=" << state.scrollNow <<
                " row=" << state.rowPx << " gridActual=" << state.gridView << " motion=" << state.rowMotion.size() <<
                " ghosts=" << state.disappearingRows.size() << " empty=" << state.emptyNative <<
                " shown=" << state.visible << " viewport=" << ViewHeight(state) << "\n";
            Check(state.rowTransitionActive,"deletion did not animate");
            const auto ghost=std::find_if(state.disappearingRows.begin(),state.disappearingRows.end(),
                [&](const CapturedRow& row) { return row.entry.path==restored[0]; });
            Check(ghost!=state.disappearingRows.end() || previousGhost<.01f,
                "later deletion dropped a preceding grid/list ghost");
            if(!i) QueryPerformanceCounter(&state.rowTransitionStartedQpc);
            else {
                Check(state.rowTransitionStartedQpc.QuadPart!=0,"delete burst restarted its clock");
                if(ghost!=state.disappearingRows.end()) {
                    Check(ghost->oldOpacity<previousGhost && ghost->oldOpacity>0.f,
                        "delete burst froze or restarted the outgoing fade");
                    previousGhost=ghost->oldOpacity;
                }
            }
        }
        Check(state.total==24,"visual ghosts changed logical membership");
        StopRowTransition(state); Stop(state.background); DestroyWindow(state.hwnd);
        for(const auto& path:files) std::filesystem::remove(path);
        Check(std::filesystem::remove(directory),"burst fixture cleanup");
    }
    std::cout << "PASS: ten streamed inserts/deletes advance between paints in LTR/RTL list/grid; no zero-clock restart or disappearing-ghost loss\n";
}

static void TestFilesystemMutationDelivery(const std::wstring& fixture) {
    const auto directory = JoinPath(fixture, L"mutation-delivery");
    std::filesystem::create_directory(directory);
    const auto first = JoinPath(directory, L"first.txt");
    const auto second = JoinPath(directory, L"second.txt");
    const auto create = [](const std::wstring& path) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE, "mutation fixture create");
        CloseHandle(file);
    };
    create(first); create(second);
    ViewState state;
    state.hwnd = MessageWindow(); state.background->hwnd = state.hwnd;
    state.folderPath = directory; state.inScope = true;
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    UpdateMetrics(state); UpdateColumns(state);
    unsigned filesystemWakes = 0;
    const auto pump = [&] {
        MSG message{};
        while (PeekMessageW(&message, state.hwnd, 0, 0, PM_REMOVE)) {
            if (message.message == kMsgFilesystemChanged) ++filesystemWakes;
            if (message.message == kMsgFilesystemChanged ||
                message.message == kMsgFlushDirectoryChanges)
                OverlayProc(state.hwnd, message.message, message.wParam, message.lParam);
        }
        ReceiveDirectoryResults(state);
    };
    const auto settled = [&] {
        pump();
        return state.loaded && !state.directoryPending && !state.refreshAgain &&
            !state.watchedChangePending && state.directoryWatch &&
            state.validatedFolderStamp == state.directoryWatch->stamp;
    };
    WatchDirectory(state); QueueDirectoryRefresh(state);
    Check(Eventually(settled), "initial watched names did not validate");
    Check(state.total == 2, "initial mutation names");
    CacheDepartingFolder(state);
    Check(RestoreCachedFolder(state) && state.total == 2,
        "unchanged watched snapshot lost immediate restore");
    state.validatedFolderStamp.reset(); // Names finished before watch installation.
    CacheDepartingFolder(state); state.loaded = false;
    Check(!RestoreCachedFolder(state) && state.entries.empty(),
        "unverified initial names were allowed to flash on a cached return");
    QueueDirectoryRefresh(state);
    Check(Eventually(settled) && state.total == 2, "unverified snapshot did not revalidate");

    // No WM_TIMER is dispatched anywhere in this fixture. The existing watch
    // lane must wake the owner, and the first hint must start a names scan now.
    const auto before = state.directoryToken;
    const auto wakesBefore = filesystemWakes;
    Check(DeleteFileW(first.c_str()), "private deletion");
    Check(Eventually([&] { pump(); return filesystemWakes > wakesBefore; }),
        "filesystem mutation needed polling instead of a worker wake");
    Check(state.directoryToken != before, "first filesystem hint waited for a quiet timer");
    Check(Eventually(settled) && state.total == 1 && state.entries.front().path == second,
        "deletion did not reach GPU model without a timer");
    Check(state.presentObserverId && state.presentObserverQpc && state.observerCommitQpc,
        "filesystem-to-model timing lost its accepted-frame correlation");

    // A second change during a scan is latched, without cancelling that scan.
    QueueDirectoryRefresh(state);
    const auto inFlight = state.directoryToken;
    create(first);
    ObserveDirectoryWatch(state, GetTickCount64(), true);
    UpdateWatchedDirectory(state, GetTickCount64());
    Check(state.directoryPending && state.directoryToken == inFlight && state.watchedChangePending,
        "notification cancelled a live names scan instead of awaiting completion");
    Check(Eventually(settled) && state.total == 2 && state.directoryToken != inFlight,
        "names completion did not release the coalesced follow-up scan");

    // History retains only a weak stamp. Once the watch lane moves on, cached
    // folders release their handles and returns require fresh names.
    CacheDepartingFolder(state);
    auto retained = state.folderCache.folders.at(directory).changeStamp;
    CloseDirectoryWatch(state);
    Check(Eventually([&]{return retained.expired();}),"cached history kept a directory watch alive after navigation");
    const auto moved = JoinPath(fixture, L"mutation-moved.txt");
    Check(MoveFileW(first.c_str(), moved.c_str()) && DeleteFileW(second.c_str()),
        "private move/delete away from cached folder");
    state.loaded = false;
    Check(!RestoreCachedFolder(state) && state.entries.empty() && !state.loaded,
        "changed cached folder flashed stale names");
    WatchDirectory(state); QueueDirectoryRefresh(state);
    Check(Eventually(settled) && state.total == 0 && state.entries.empty(),
        "changed cached folder did not publish its actual empty result");

    // Already-consumed signals are protected by the names request's revision.
    state.graphicsStandby = true;
    const auto sleepingGeneration = state.directoryToken;
    const auto sleepingWakes = filesystemWakes;
    create(first);
    Check(Eventually([&] { pump(); return filesystemWakes > sleepingWakes; }) &&
        state.watchedChangePending && !state.directoryPending &&
        state.directoryToken == sleepingGeneration,
        "hidden warm tab rescanned instead of latching its notification");
    state.graphicsStandby = false;
    QueueDirectoryChangeFlush(state);
    Check(Eventually([&] { return settled() && state.total == 1; }), "watch rearm lost a later creation");
    const auto oldRevision = state.validatedFolderRevision;
    Check(DeleteFileW(first.c_str()), "revision fixture deletion");
    Check(WaitForSingleObject(state.dirChange, 10000) == WAIT_OBJECT_0, "revision signal");
    ObserveDirectoryWatch(state, GetTickCount64());
    Check(state.directoryWatch->stamp->revision != oldRevision, "watch revision did not advance");
    CacheDepartingFolder(state); state.loaded = false;
    Check(!RestoreCachedFolder(state), "consumed notification allowed stale snapshot restore");
    const auto generation = state.directoryToken;
    const auto oldWatchToken = state.watchToken;
    CloseDirectoryWatch(state);
    OverlayProc(state.hwnd, kMsgFilesystemChanged, oldWatchToken, 0);
    Check(state.directoryToken == generation, "obsolete watch message refreshed a different folder");
    ClearFolderSnapshots(state); state.validatedFolderStamp.reset(); retained.reset();
    RetireCachedEntries(state, state.entries);
    Stop(state.background);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0); DestroyWindow(state.hwnd);
    Check(DeleteFileW(moved.c_str()), "private moved fixture cleanup");
    Check(Eventually([&] { return RemoveDirectoryW(directory.c_str()) != FALSE; }),
        "private mutation directory cleanup");
    std::cout << "PASS: direct filesystem wake, completion-coalesced mutations, stale cached move/delete rejection, revision guard and watcher teardown\n";
}
