// Actual OverlayProc/renderer and worker queues; no Explorer injection or file
// mutations. Count paints to detect synchronous provider-message rendering.
inline unsigned coldScrollPaints = 0;
static LRESULT CALLBACK ColdScrollProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_PAINT) ++coldScrollPaints;
    if (msg == WM_PAINT || msg == kMsgFrame) return OverlayProc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void TestColdScrollDelivery() {
    // This checks immediate paced delivery with no queued input. Real mouse
    // activity must not invoke the separately tested input-deferral path or
    // overwrite the synthetic thumb positions sampled by a captured frame.
    FrameInputFixture::Session frameInput;
    PointerFixture::Session pointer;
    ViewState state;
    state.hwnd = IconTestWindow(); state.frameHwnd = state.activeTabHost = state.hwnd;
    HWND host = CreateWindowExW(0, L"CtrlNotifySink", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 420, 800, state.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    state.nativeHwnd = CreateWindowExW(0, L"SHELLDLL_DefView", L"", WS_CHILD | WS_VISIBLE,
        0, 0, 420, 800, host, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(state.nativeHwnd && SuppressNativeContent(state), "cold scroll native host");
    state.background->hwnd = state.hwnd;
    state.background->directoryGeneration = state.directoryToken = 845;
    state.visible = state.inScope = state.detailsViewActive = state.loaded = state.columnsView = true;
    state.rowPx = 34.f; state.fontPx = 16.f; state.iconPx = 20.f; state.headerPx = 26;
    state.firstContentSubmittedAt = GetTickCount64(); state.featureInitializationStep = 5;
    Check(EnsureTarget(state), "cold scroll target");
    UpdateRefreshRate(state);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    auto original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(ColdScrollProc)));
    ExplorerColumn name; name.key = PKEY_FileName; name.title = L"Name"; name.width = 380;
    state.columns.push_back(name); UpdateColumns(state);
    for (int i = 0; i < 600; ++i) {
        Entry entry; entry.name = L"row" + std::to_wstring(i) + L".txt";
        entry.path = L"cold-scroll:" + entry.name; state.entries.push_back(std::move(entry));
    }
    state.total = static_cast<int>(state.entries.size());
    auto ready = [&] { return std::count_if(state.entries.begin(), state.entries.end(),
        [](const auto& entry) { return entry.detailsReady; }); };
    auto publish = [&](int row) {
        DirectoryResult result; result.kind = DirectoryResult::Details; result.token = state.directoryToken;
        DetailValue value; value.index = row; value.columnEpoch = state.columnEpoch;
        value.cells.push_back({PKEY_Size, L"123 KB"}); result.details.push_back(std::move(value));
        PublishDirectoryResult(*state.background, std::move(result));
    };
    // No animation is active: direct thumb scrubbing used to let a metadata
    // fit run beneath the grab, and every completion could force WM_PAINT.
    state.draggingScrollbar = true;
    state.columnFitPendingEpoch = state.columnEpoch;
    for (int i = 0; i < 160; ++i) publish(i);
    MSG message{};
    Check(PeekMessageW(&message, state.hwnd, kMsgDetailsReady, kMsgDetailsReady, PM_REMOVE), "metadata notification");
    const auto beforePaint = coldScrollPaints;
    const float beforeScroll = state.scrollNow, beforeTarget = state.scrollTarget;
    OverlayProc(state.hwnd, message.message, message.wParam, message.lParam);
    Check(ready() == 0 && coldScrollPaints == beforePaint,
          "metadata notification committed/refitted/painted synchronously before queued pointer input");
    Check(state.scrollNow == beforeScroll && state.scrollTarget == beforeTarget &&
          state.columnFitPendingEpoch == state.columnEpoch, "metadata changed the active grab");
    for (int i = 160; i < 180; ++i) publish(i);
    // Frame/style/initialization messages can legitimately share this window's
    // queue. Check the provider's actual notification, not their numeric range.
    Check(!PeekMessageW(&message, state.hwnd, kMsgDetailsReady, kMsgDetailsReady, PM_REMOVE),
          "provider wake latch reopened before its paced delivery");
    // Structural data has an independent wake even while metadata is held.
    DirectoryResult preview; preview.kind = DirectoryResult::Preview; preview.token = state.directoryToken;
    PublishDirectoryResult(*state.background, std::move(preview));
    Check(PeekMessageW(&message, state.hwnd, kMsgDirectoryReady, kMsgDirectoryReady, PM_REMOVE),
          "held metadata postponed a names notification");
    {
        ExclusiveSrwLock lock(state.background->directoryLock);
        std::erase_if(state.background->directoryResults, [](const auto& r) { return r.kind == DirectoryResult::Preview; });
        state.background->directoryNotificationQueued = false;
    }
    // The icon message must likewise leave ownership transfer for the frame.
    auto& icon = state.icons.try_emplace(L"type:.txt").first->second;
    icon.pending = icon.association = true; icon.pendingToken = state.iconToken; icon.pendingRequestId = 101;
    IconResult image; image.key = L"type:.txt"; image.token = state.iconToken; image.requestId = 101;
    image.pixels.width = image.pixels.height = 8; image.pixels.stride = 32; image.pixels.data.assign(256, 255);
    {
        ExclusiveSrwLock lock(state.background->iconLock);
        state.background->iconResults.push_back(std::move(image)); state.background->iconNotificationQueued = true;
    }
    OverlayProc(state.hwnd, kMsgIconReady, 0, 0);
    Check(icon.pending && !icon.pixels.Ready(), "icon notification restarted viewport work before input");
    const bool instant = g_instantScrub.exchange(true);
    const auto bar = Scrollbar(state);
    state.scrollbarGrabY = bar.thumbLength * .5f;
    const int x = static_cast<int>((bar.left + bar.right) * .5f);
    PointerFixture::At(state.hwnd,{x,300});
    OverlayProc(state.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, 300));
    const float requested = state.scrollNow;
    AdvanceAnimationFrame(state, true);
    Check(ready() > 0 && ready() <= 32 && icon.pixels.Ready(), "paced delivery did not bound/progress provider results");
    Check(state.scrollNow == requested && state.columnFitPendingEpoch == state.columnEpoch,
          "paced data delivery reset scroll or refitted beneath the thumb");
    for (int frame = 0; ready() < 180 && frame < 190; ++frame) AdvanceAnimationFrame(state, true);
    Check(ready() == 180, "metadata stopped progressing while thumb stayed held");
    // Freeze actual property dispatch, then jump past queued prefetch and back.
    // The bounded queue must contain current visible demand and unpin cancelled rows.
    providerGate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    Check(FreezeDetailProviders(state), "cold scroll property gate");
    ExplorerColumn author; author.key = PKEY_Author; state.columns = {author};
    InvalidateDetailJobs(state); UpdateColumns(state);
    QueueVisibleDetails(state);
    int oldRow = -1;
    {
        ExclusiveSrwLock lock(state.background->directoryLock);
        Check(!state.background->detailJobs.empty(), "initial cold scroll demand");
        oldRow = state.background->detailJobs.front().requests.front().index;
        const auto visible = VisibleItemRows(state, 0.f);
        Check(std::find(visible.begin(), visible.end(), oldRow) != visible.end(), "metadata prefetched behind/above visible rows first");
    }
    SetScrollImmediate(state, 400 * state.rowPx); QueueVisibleDetails(state);
    Check(!state.entries[oldRow].detailsPending, "departed metadata stayed pinned after viewport jump");
    {
        const auto current = VisibleItemRows(state, 12.f);
        ExclusiveSrwLock lock(state.background->directoryLock);
        for (const auto& job : state.background->detailJobs) for (const auto& request : job.requests)
            Check(std::find(current.begin(), current.end(), request.index) != current.end(), "old viewport work survived reprioritization");
    }
    SetScrollImmediate(state, requested); QueueVisibleDetails(state);
    Check(state.entries[oldRow].detailsPending, "cancelled metadata could not be requested on return");
    std::weak_ptr<TabBackground> lifetime = state.background;
    Stop(state.background); SetEvent(providerGate);
    Check(Eventually([&] { return lifetime.expired(); }), "cold scroll workers did not retire");
    CloseHandle(providerGate); providerGate = nullptr;
    // Read-only profile of the user's example through real enumeration,
    // metadata/icon workers, scrollbar input and GPU rendering. OS caches and
    // composition latency are uncontrolled; no FPS claim comes from this.
    wchar_t windows[MAX_PATH]{};
    GetWindowsDirectoryW(windows, ARRAYSIZE(windows));
    const auto syswow64 = JoinPath(windows, L"SysWOW64");
    if (std::filesystem::is_directory(syswow64)) {
        state.background = std::make_shared<TabBackground>();
        state.folderPath = syswow64; state.loaded = false; state.directoryPending = false;
        state.columnFitPendingEpoch = 0; state.columns.clear();
        for (const auto& key : {PKEY_FileName, PKEY_DateModified, PKEY_ItemTypeText, PKEY_Size}) {
            ExplorerColumn column; column.key = key; column.title = L"Profile";
            column.width = SameKey(key, PKEY_FileName) ? 180 : 70; state.columns.push_back(column);
        }
        UpdateColumns(state); InvalidateDetailJobs(state);
        QueueDirectoryRefresh(state);
        auto snapshot = WaitForComplete(*state.background, state.directoryToken);
        state.entries = std::move(snapshot.entries); state.entryPathIndex = std::move(snapshot.pathIndex);
        state.total = static_cast<int>(state.entries.size()); state.loaded = true; state.directoryPending = false;
        state.scrollNow = state.scrollTarget = 0; state.iconsResolved = 0;
        SetCapture(state.hwnd); state.draggingScrollbar = true;
        const auto actualBar = Scrollbar(state);
        state.scrollbarGrabY = actualBar.thumbLength * .5f;
        auto pumpProviders = [&] {
            for (UINT id : {kMsgIconReady, kMsgDirectoryReady, UINT(WM_APP+64)})
                while (PeekMessageW(&message, state.hwnd, id, id, PM_REMOVE))
                    OverlayProc(state.hwnd, message.message, message.wParam, message.lParam);
        };
        std::vector<double> times;
        LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
        for (int pass = 0; pass < 2; ++pass) {
            g_instantScrub = pass == 0;
            for (int tick = 0; tick < 32; ++tick) {
                for (int sample = 0; sample < 4; ++sample) {
                    const int step = tick * 4 + sample;
                    const float fraction = (step < 64 ? step : 127-step) / 64.f;
                    const int y = static_cast<int>(actualBar.top + state.scrollbarGrabY + actualBar.travel * fraction);
                    if (state.animationActive) state.smoothStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.003);
                    PointerFixture::At(state.hwnd,{x,y});
                    OverlayProc(state.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(x, y));
                }
                const float target = state.scrollTarget;
                pumpProviders();
                LARGE_INTEGER began{}, ended{}; QueryPerformanceCounter(&began);
                SendMessageW(state.hwnd, kMsgFrame, 0, 0);
                const auto painted=coldScrollPaints;
                UpdateWindow(state.hwnd);
                Check(coldScrollPaints==painted,"enrichment inside a paced paint left a duplicate paint behind");
                QueryPerformanceCounter(&ended);
                times.push_back(1000. * (ended.QuadPart-began.QuadPart) / frequency.QuadPart);
                Check(state.draggingScrollbar && state.scrollTarget == target,
                      "real cold SysWOW64 result/frame changed thumb ownership or its destination");
                Check(!(GetWindowLongPtrW(host, GWL_STYLE) & WS_VISIBLE), "cold scroll exposed native content");
            }
        }
        pumpProviders(); AdvanceAnimationFrame(state, true);
        Check(ready() > 0, "real metadata did not progress during held scrollbar");
        std::sort(times.begin(), times.end());
        std::cout << "PROFILE: read-only SysWOW64 " << state.total << " rows, 256 thumb input samples, cold provider/frame CPU median=" <<
            times[times.size()/2] << " ms p95=" << times[times.size()*95/100] << " ms (offscreen; OS caches uncontrolled)\n";
        state.draggingScrollbar = false; StopScrollAnimation(state); ReleaseCapture();
        lifetime = state.background; Stop(state.background);
        Check(Eventually([&] { return lifetime.expired(); }, 30000), "real SysWOW64 workers did not retire");
    }
    state.draggingScrollbar = false;
    UnregisterAnimationWindow(state.hwnd); DropTarget(state); RestoreNativeContent(state);
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
    SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
    DestroyWindow(state.hwnd); g_instantScrub = instant;
    std::cout << "PASS: cold provider wakes defer paint/refit/refill to bounded frames; metadata progresses during thumb drag, names stay independent, viewport jumps cancel/retry old demand\n";
}
