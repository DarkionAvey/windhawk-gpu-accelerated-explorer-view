// Isolate cold metadata drawing from Shell/worker time. Every cell is already
// available, but the text/glyph caches start empty, as during a first traversal.
static void TestColdTextDrawing(HWND frame) {
    ViewState state;
    state.hwnd=CreateWindowExW(0,L"STATIC",L"cold metadata",WS_CHILD,0,0,1800,1200,
        frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
    state.visible=state.inScope=state.loaded=state.columnsView=true;
    state.fontPx=16; state.rowPx=34; state.headerPx=38;
    state.firstContentSubmittedAt=GetTickCount64(); state.deferColumnProviders=false;
    Check(EnsureTarget(state),"cold text target");
    for(const auto& key : {PKEY_FileName,PKEY_Comment,PKEY_DateModified,PKEY_ItemTypeText,PKEY_Size,PKEY_Author}) {
        ExplorerColumn c; c.key=key; c.title=L"Column"; c.width=295; state.columns.push_back(c);
    }
    UpdateColumns(state);
    state.entries.resize(793); state.total=793;
    for(int i=0;i<state.total;++i) {
        auto& e=state.entries[i];
        const auto n=std::to_wstring(i);
        e.name=L"Photo of the flowers in the park "+n+L".jpg";
        e.path=L"windhawk:pidl:"+n;
        e.date=L"20/10/2026 01:"+n; e.type=L"JPEG image "+std::to_wstring(i%17);
        e.size=n+L" KB"; e.propertyEpoch=state.propertyEpoch; e.detailsReady=true;
        e.cells={{PKEY_Comment,L"D:\\Photos\\Albums\\Flowers in the park "+n+L"\\Original photographs"},
                 {PKEY_Author,L"Photographer "+n}};
    }
    RebuildGroupSections(state);
    // Measure CPU rendering without composition pacing. The target still uses
    // the real per-tab Direct2D device; no missing labels are accepted.
    auto swap=std::move(state.swapChain);
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    // An unpaced paint (for example WM_SIZE) has no pre-existing frame wake.
    // Deferred text must register its own continuation, without mouse input.
    UnregisterAnimationWindow(state.hwnd);
    Paint(state);
    Check(state.textRasterPending,"cold text continuation fixture did not exhaust its budget");
    AcquireSRWLockShared(&g_frameLock);
    const bool registered=std::any_of(g_activeFrames.begin(),g_activeFrames.end(),[&](const auto& f) { return f.hwnd==state.hwnd; });
    ReleaseSRWLockShared(&g_frameLock);
    Check(registered,"unpaced text deferral had no frame wake to finish missing metadata");
    for(int n=0;n<100 && state.textRasterPending;++n) Paint(state);
    Check(!state.textRasterPending,"idle text continuation did not finish without input/provider results");
    state.draggingScrollbar=true;
    for(bool warm : {false,true}) {
        std::vector<double> times;
        size_t hits=0,misses=0;
        for(int tick=0;tick<110;++tick) {
            state.scrollNow=state.scrollTarget=(warm ? 700 : tick*6)*state.rowPx;
            state.frameProfile.recording=true;
            state.frameProfile.textHits=state.frameProfile.textMisses=0;
            LARGE_INTEGER start{},end{}; QueryPerformanceCounter(&start);
            Paint(state);
            QueryPerformanceCounter(&end);
            Check(state.graphicsReady,"cold text rendering lost the device");
            times.push_back(1000.*(end.QuadPart-start.QuadPart)/frequency.QuadPart);
            hits+=state.frameProfile.textHits; misses+=state.frameProfile.textMisses;
        }
        std::sort(times.begin(),times.end());
        std::cout<<"PROFILE: 793 rows/six populated columns warm="<<warm
            <<" CPU p50="<<times[times.size()/2]<<" p95="<<times[times.size()*95/100]
            <<" max="<<times.back()<<" ms text="<<hits<<"/"<<misses<<std::endl;
        if(warm) Check(!state.textRasterPending,"metadata raster preparation did not settle while the thumb remained held");
    }
    state.frameProfile.recording=false;
    state.swapChain=std::move(swap);
    state.draggingScrollbar=false;
    UnregisterAnimationWindow(state.hwnd); DropTarget(state); DestroyWindow(state.hwnd);
}
