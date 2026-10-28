// Model per-item Recycle Bin pressure without modifying the real namespace.
static void TestFrameBudgets() {
    IconFixture::Session fixture; IconFixture::blockAll=true;
    {
        ViewState cancelled; cancelled.hwnd=IconTestWindow();
        cancelled.background->hwnd=cancelled.hwnd; cancelled.iconToken=17;
        const auto add=[&](const std::wstring& key,bool association) {
            IconResult result; result.hwnd=cancelled.hwnd; result.token=17;
            result.key=key; result.useAttributes=association;
            result.pixels.width=result.pixels.height=256; result.pixels.stride=1024;
            result.pixels.data.resize(256*256*4);
            auto* pixels=result.pixels.data.data();
            cancelled.background->iconResults.push_back(std::move(result));
            return pixels;
        };
        auto* departed=add(L"@256|departed",false);
        add(L"type:.png",true);
        CancelDepartedIconJobs(cancelled,18);
        Check(cancelled.retiredIconPixels.size()==1 &&
            cancelled.retiredIconPixels.front().data.data()==departed &&
            cancelled.background->iconResults.size()==1,
            "navigation destroyed thumbnail buffers on the UI thread or cancelled associations");
        auto* obsolete=add(L"@512|changed",false);
        auto& record=cancelled.icons[L"@256|changed"];
        record.pixels.width=record.pixels.height=256; record.pixels.data.resize(256*256*4);
        auto* cached=record.pixels.data.data();
        AccountIconCache(cancelled,L"@256|changed",record);
        auto& unrelated=cancelled.icons[L"unrelated"];
        unrelated.pending=true; unrelated.pendingToken=18; unrelated.pendingRequestId=42;
        InvalidateShellIconKey(cancelled,L"changed");
        Check(cancelled.background->iconGeneration==18 && unrelated.pending &&
            unrelated.pendingRequestId==42 && record.needsRefresh && record.pixels.data.data()==cached &&
            cancelled.background->iconResults.size()==1 &&
            std::any_of(cancelled.retiredIconPixels.begin(),cancelled.retiredIconPixels.end(),
                [&](const auto& image) { return image.data.data()==obsolete; }) &&
            std::none_of(cancelled.retiredIconPixels.begin(),cancelled.retiredIconPixels.end(),
                [&](const auto& image) { return image.data.data()==cached; }),
            "item invalidation discarded cached artwork or changed unrelated request ownership");
        InvalidateShellIcons(cancelled);
        Check(cancelled.background->iconResults.empty() && cancelled.icons.size()==2 &&
            record.needsRefresh && !unrelated.pending && record.pixels.data.data()==cached &&
            cancelled.retiredIconPixels.size()==3 && cancelled.iconResultsPending,
            "global icon invalidation discarded cached artwork or bypassed obsolete-result reclamation");
        ReceiveIcons(cancelled,false);
        Check(cancelled.retiredIconPixels.empty(),"cancelled pixels did not reach background reclamation");
        Stop(cancelled.background); UnregisterAnimationWindow(cancelled.hwnd); DestroyWindow(cancelled.hwnd);
    }
    ViewState state; state.hwnd=IconTestWindow(); state.background->hwnd=state.hwnd;
    state.visible=false; state.total=790; state.folderPath=kRecycleBinLocation;
    // Standalone textures now charge allocation overhead as well as CPU
    // pixels. Keep this count-cliff check below its explicit byte budget.
    state.folderCache.budgetBytes=64*1024*1024;
    std::wstring first;
    for (int i=0;i<790;++i) {
        const auto key=L"wh-icon-test:"+std::wstring(256,L'a')+std::to_wstring(i)+L".exe";
        if (!i) first=key;
        auto& record=state.icons[key]; record.resolved=true; record.used=i;
        record.pixels.width=record.pixels.height=32; record.pixels.stride=128;
        record.pixels.data.assign(4096,255); AccountIconCache(state,key,record);
    }
    Entry next; next.name=L"new.exe"; next.path=L"wh-icon-test:slow-new.exe";
    std::unordered_map<std::wstring,unsigned> wanted{{next.path,0}};
    Check(RequestEntryIcon(state,next,false,wanted) && state.icons.size()==791 && state.icons.contains(first),
        "790 small per-item icons hit the old fixed-count eviction cliff");
    Check(state.iconCacheBytes<state.folderCache.budgetBytes,"small icons exceeded their explicit allocation estimate budget");
    // A provider paused while holding a queue lock must never stall input.
    AcquireSRWLockExclusive(&state.background->iconLock);
    ReceiveIcons(state,false);
    const bool iconsDeferred=state.iconResultsPending;
    ReleaseSRWLockExclusive(&state.background->iconLock);
    Check(iconsDeferred,"contended icon delivery was not left pending");
    AcquireSRWLockExclusive(&state.background->directoryLock);
    ReceiveDirectoryResults(state,DirectoryDelivery::Details);
    const bool detailsDeferred=state.detailResultsPending;
    DirectoryResult saved; saved.kind=DirectoryResult::Details; saved.token=999;
    state.deferredDetails.push_back(std::move(saved));
    ReceiveDirectoryResults(state,DirectoryDelivery::Details);
    const bool backlogProgressed=state.deferredDetails.empty();
    ReleaseSRWLockExclusive(&state.background->directoryLock);
    Check(detailsDeferred && backlogProgressed,"metadata delivery waited for a provider or stranded its UI remainder");
    {
        ExclusiveSrwLock lock(state.background->iconLock);
        for (int i=0;i<24;++i) {
            IconResult stale; stale.key=L"obsolete"; stale.pixels.width=stale.pixels.height=256;
            stale.pixels.stride=1024; stale.pixels.data.assign(256*256*4,255);
            state.background->iconResults.push_back(std::move(stale));
        }
    }
    AcquireSRWLockExclusive(&state.background->directoryLock);
    // iconLock belongs to live provider lanes. A nonblocking delivery may
    // correctly defer before consuming any result if that other lock is busy.
    // Wait for actual consumption before asserting cleanup ownership; keep
    // directoryLock held so synchronous reclamation is still caught.
    const bool consumed=Eventually([&] {
        ReceiveIcons(state,false); return !state.retiredIconPixels.empty();
    });
    const bool retiredLater=consumed && state.iconResultsPending;
    ReleaseSRWLockExclusive(&state.background->directoryLock);
    Check(retiredLater,"stale thumbnail memory was freed on input or waited for the cleanup lock");
    Check(Eventually([&] { ReceiveIcons(state,false); return !state.iconResultsPending; }),
        "bounded icon deliveries did not finish/reclaim stale pixels");
    state.detailResultsPending=state.viewportDemandPending=false;
    g_frameTimeLogging=false; BeginFrameProfile(state);
    Check(!state.frameProfile.recording && !state.frameProfile.frequency,"disabled frame logger sampled frames");
    const bool perf=g_perf.exchange(false); g_frameTimeLogging=true; state.draggingScrollbar=true;
    state.lastInteractionTick=GetTickCount64();
    BeginFrameProfile(state);
    state.hoverAnimationActive=true;
    { FrameStageScope phase(state,FrameStage::Demand); Sleep(2); }
    EndFrameProfile(state);
    state.hoverAnimationActive=false;
    Check(state.frameProfile.frames==1 && state.frameProfile.maxima[1]>0. &&
        (state.frameProfile.activity&(1u<<16)),
        "frame logger depended on general performance logging or missed CPU phases");
    state.frameProfile.previous-=static_cast<LONGLONG>(state.frameProfile.frequency*.100);
    state.lastInteractionTick=GetTickCount64();
    BeginFrameProfile(state); EndFrameProfile(state);
    Check(state.frameProfile.samples==1 && state.frameProfile.intervals[0]>=100. && state.frameProfile.late==1,
        "frame logger lost a UI stall between frames");
    state.frameProfile.continuing=false;
    state.frameProfile.previous-=static_cast<LONGLONG>(state.frameProfile.frequency*10.);
    BeginFrameProfile(state); EndFrameProfile(state);
    Check(state.frameProfile.samples==1,"idle time was counted as dropped frames");
    state.frameProfile.continuing=true; state.lastInteractionTick=GetTickCount64()-2000;
    state.frameProfile.previous-=static_cast<LONGLONG>(state.frameProfile.frequency*10.);
    state.iconResultsPending=true;
    BeginFrameProfile(state); EndFrameProfile(state);
    Check(state.frameProfile.samples==1,"a thumbnail arriving during a paused grab was counted as a frame stall");
    g_frameTimeLogging=false; BeginFrameProfile(state); g_perf=perf;
    Check(!state.frameProfile.frequency,"disabling frame logging retained an obsolete cadence window");
    SetEvent(IconFixture::gate); Stop(state.background);
    UnregisterAnimationWindow(state.hwnd); DestroyWindow(state.hwnd);
    std::cout << "PASS: 790 per-item icon cache, nonblocking enrichment/cleanup, bounded stale thumbnails and independent frame cadence/stall profiling\n";
}
