static void TestTabResumeRefresh(const std::wstring& directory) {
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    for(const wchar_t* name:{L"CabinetWClass",L"ShellTabWindowClass",L"SHELLDLL_DefView"}) {
        cls.lpszClassName=name; RegisterClassW(&cls);
    }
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CabinetWClass",L"private resume fixture",
        WS_POPUP|WS_VISIBLE,-32000,-32000,640,480,nullptr,nullptr,cls.hInstance,nullptr);
    Check(frame!=nullptr,"resume frame");
    FocusFixture::Session focus(frame,nullptr);
    const bool toggle=g_showNativeViewToggleButton.exchange(false);
    std::array<std::unique_ptr<ViewState>,3> tabs;
    std::vector<HWND> overlays;
    for(auto& tab:tabs) {
        tab=std::make_unique<ViewState>(); auto& s=*tab;
        s.frameHwnd=frame; s.token=g_nextToken.fetch_add(1);
        s.activeTabHost=CreateWindowExW(0,L"ShellTabWindowClass",L"",WS_CHILD,0,0,640,480,
            frame,nullptr,cls.hInstance,nullptr);
        s.nativeHwnd=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,0,0,640,480,
            s.activeTabHost,nullptr,cls.hInstance,nullptr);
        s.hwnd=CreateWindowExW(0,L"STATIC",L"private resume overlay",WS_CHILD,0,0,640,480,
            frame,nullptr,cls.hInstance,nullptr);
        Check(s.activeTabHost && s.nativeHwnd && s.hwnd,"resume tab windows");
        SetWindowLongPtrW(s.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        s.background->hwnd=s.hwnd; s.folderPath=directory;
        s.visible=s.inScope=s.detailsViewActive=s.graphicsReady=true;
        overlays.push_back(s.hwnd);
    }
    tabs[2]->thisPcGpu=true; tabs[2]->folderPath=kThisPcLocation;
    {ExclusiveSrwLock lock(g_tabRegistryLock); g_tabOverlaysByFrame[frame]=overlays;}
    const auto activate=[&](size_t index) {
        for(size_t i=0;i<tabs.size();++i) ShowWindow(tabs[i]->activeTabHost,i==index ? SW_SHOWNOACTIVATE : SW_HIDE);
        SynchronizeTabSurfaces(frame,true);
    };
    const auto take=[&](ViewState& s) {
        MSG message{};
        Check(PeekMessageW(&message,s.hwnd,kMsgTabResumeRefresh,kMsgTabResumeRefresh,PM_REMOVE),"resume refresh was not posted");
        MSG duplicate{};
        Check(!PeekMessageW(&duplicate,s.hwnd,kMsgTabResumeRefresh,kMsgTabResumeRefresh,PM_NOREMOVE),"activation hints duplicated refresh");
        return message;
    };
    const auto dispatch=[](const MSG& message) { OverlayProc(message.hwnd,message.message,message.wParam,message.lParam); };
    activate(0);
    Check(!tabs[0]->tabResumeRefreshQueued && !tabs[0]->detailsRefreshEpoch,"first presentation forced a refresh");
    activate(1); SynchronizeTabSurfaces(frame,true);
    auto message=take(*tabs[1]);
    Check(IsWindowVisible(tabs[1]->hwnd) && !tabs[1]->detailsRefreshEpoch && !tabs[1]->directoryPending,
        "resume waited for refresh before presenting cached content");
    ValidateRect(tabs[1]->hwnd,nullptr); dispatch(message);
    Check(tabs[1]->detailsRefreshEpoch==1 && tabs[1]->directoryPending &&
        !GetUpdateRect(tabs[1]->hwnd,nullptr,FALSE) && !tabs[0]->detailsRefreshEpoch && !tabs[2]->detailsRefreshEpoch,
        "resume did not use the nonpainting, tab-owned forced refresh path");
    activate(0); activate(1); message=take(*tabs[1]); activate(2);
    dispatch(message);
    Check(tabs[1]->detailsRefreshEpoch==1,"departed tab admitted a stale resume command");
    dispatch(take(*tabs[2]));
    Check(tabs[2]->detailsRefreshEpoch==1 && tabs[2]->nativeRowsNeedCapture && tabs[2]->nativeRowsMessageQueued,
        "This PC resume did not recheck native identities through forced refresh");
    activate(1); dispatch(take(*tabs[1]));
    Check(tabs[1]->detailsRefreshEpoch==2,"stale wake consumed the next genuine resume");
    activate(0); activate(1); message=take(*tabs[1]); ++tabs[1]->token; dispatch(message);
    PresentCachedTabSurface(*tabs[1]);
    Check(tabs[1]->detailsRefreshEpoch==2 && !tabs[1]->tabResumeRefreshQueued,
        "resume refreshed a different navigation generation");
    {ExclusiveSrwLock lock(g_tabRegistryLock); g_tabOverlaysByFrame.erase(frame);}
    for(auto& tab:tabs) {
        RestoreNativeContent(*tab); UnregisterAnimationWindow(tab->hwnd); KillTimer(tab->hwnd,kTimerId);
        Stop(tab->background); SetWindowLongPtrW(tab->hwnd,GWLP_USERDATA,0);
    }
    DestroyWindow(frame); tabs={}; g_showNativeViewToggleButton=toggle;
    std::cout<<"PASS: three private tabs resume once after cached presentation, including This PC; stale wakes/navigation and duplicate hints are rejected\n";
}

static void TestWindowResumeRefresh() {
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    cls.lpszClassName=L"CabinetWClass"; RegisterClassW(&cls);
    CapacityFixture::Session volume;
    const bool toggle=g_showNativeViewToggleButton.exchange(false);
    std::array<std::unique_ptr<PresentationFixture::Tab>,3> windows;
    for(auto& window:windows) {
        window=std::make_unique<PresentationFixture::Tab>(L"window resume",1,false,false,false,800,480,L"CabinetWClass");
        auto& s=window->state;
        s.token=g_nextToken.fetch_add(1); s.thisPcGpu=true; s.folderPath=kThisPcLocation;
        s.nativeRowsComplete=true; s.nativeRowsNeedCapture=false; s.nativeRowsViewToken=s.token;
        Entry drive; drive.path=L"Z:\\"; drive.name=L"Synthetic volume"; drive.folder=true;
        auto empty=static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(sizeof(USHORT)));
        Check(empty!=nullptr,"window resume synthetic volume identity"); empty->mkid.cb=0;
        drive.shellIdentity=SharedPidl(empty,TaskMemDeleter{});
        s.entries=s.nativeRows={drive}; s.selected=0; s.selectedFlags={1};
        SetWindowLongPtrW(s.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&s));
        {ExclusiveSrwLock lock(g_tabRegistryLock);
            g_tabOverlaysByFrame[window->frame]={s.hwnd}; g_tabOverlayByHost[window->frame]=s.hwnd;}
        Check(SetWindowSubclass(window->frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass),0),
            "window resume frame subscription");
        QueueDirectoryRefresh(s);
        Check(Eventually([&]{ReceiveDirectoryResults(s); return !s.directoryPending;}),"initial window capacity");
        RefreshThisPcRows(s,false);
    }
    FocusFixture::Session focus(windows[0]->frame,windows[0]->state.hwnd);
    const auto activate=[&](size_t index) {
        const HWND previous=FocusFixture::foreground;
        FocusFixture::foreground=windows[index]->frame; FocusFixture::focus=windows[index]->state.hwnd;
        if(previous!=FocusFixture::foreground) SendMessageW(previous,WM_ACTIVATE,WA_INACTIVE,LPARAM(FocusFixture::foreground));
        SendMessageW(windows[index]->frame,WM_ACTIVATE,WA_CLICKACTIVE,LPARAM(previous));
    };
    const auto take=[](ViewState& s) {
        MSG message{},duplicate{};
        Check(PeekMessageW(&message,s.hwnd,kMsgTabResumeRefresh,kMsgTabResumeRefresh,PM_REMOVE),"window activation omitted refresh");
        Check(!PeekMessageW(&duplicate,s.hwnd,kMsgTabResumeRefresh,kMsgTabResumeRefresh,PM_NOREMOVE),"window/tab activation duplicated refresh");
        return message;
    };
    const auto dispatch=[](const MSG& m){OverlayProc(m.hwnd,m.message,m.wParam,m.lParam);};
    auto& second=windows[1]->state;
    CapacityFixture::freeBytes=900; // A deletion frees space while all windows remain visible.
    activate(1); SendMessageW(windows[1]->frame,WM_ACTIVATE,WA_ACTIVE,0);
    second.tabResumeRefreshToken=second.token; QueueResumeRefresh(second);
    const auto message=take(second);
    Check(!second.detailsRefreshEpoch && second.entries[0].driveUsed==400,"activation synchronously queried the volume");
    ValidateRect(second.hwnd,nullptr); dispatch(message);
    Check(second.detailsRefreshEpoch==1 && !GetUpdateRect(second.hwnd,nullptr,FALSE),"window refresh repainted retained values at invocation");
    Check(Eventually([&]{ReceiveDirectoryResults(second); return !second.directoryPending;}),"resumed window capacity did not arrive");
    RefreshThisPcRows(second,false);
    Check(second.entries[0].driveUsed==100 && second.nativeRowsNeedCapture &&
        !windows[0]->state.detailsRefreshEpoch && !windows[2]->state.detailsRefreshEpoch &&
        CapacityFixture::uiCalls==0 && !FocusFixture::sets,
        "window activation missed capacity/identity revalidation or refreshed a peer");
    for(UINT msg:{WM_SETFOCUS,WM_KILLFOCUS}) SendMessageW(windows[1]->frame,msg,0,0);
    OverlayProc(second.hwnd,WM_SETFOCUS,0,0);
    MSG focusMessage{};
    while(PeekMessageW(&focusMessage,second.hwnd,kMsgInputFocusChanged,kMsgInputFocusChanged,PM_REMOVE)) dispatch(focusMessage);
    Check(second.detailsRefreshEpoch==1 && !second.tabResumeRefreshQueued,"control focus restarted a window refresh");
    const auto focusSets=FocusFixture::sets; // The existing list-focus handler may return focus to its native host.
    activate(2); auto departed=take(windows[2]->state); activate(0); dispatch(departed);
    Check(!windows[2]->state.detailsRefreshEpoch,"stale window activation refreshed after foreground departure");
    auto navigated=take(windows[0]->state); ++windows[0]->state.token; dispatch(navigated);
    Check(!windows[0]->state.detailsRefreshEpoch,"window activation refreshed a newer navigation");
    activate(2); auto native=take(windows[2]->state); windows[2]->state.visible=false; dispatch(native);
    Check(!windows[2]->state.detailsRefreshEpoch,"native fallback admitted an automatic GPU refresh");
    windows[2]->state.visible=true; activate(1); auto unloading=take(second);
    g_unloading=true; dispatch(unloading); g_unloading=false;
    Check(second.detailsRefreshEpoch==1 && FocusFixture::sets==focusSets,"window refresh changed focus or ran during unload");
    for(auto& window:windows) {
        auto& s=window->state;
        RemoveWindowSubclass(window->frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass));
        {ExclusiveSrwLock lock(g_tabRegistryLock); g_tabOverlaysByFrame.erase(window->frame); g_tabOverlayByHost.erase(window->frame);}
        SetWindowLongPtrW(s.hwnd,GWLP_USERDATA,0);
    }
    windows={}; g_showNativeViewToggleButton=toggle;
    std::cout<<"PASS: three visible private Explorer frames refresh only the activated window; volume/identity updates, tab coalescing, control focus, departed/navigation/native/unload guards and zero UI volume calls\n";
}

static void TestUnchangedRefreshPresentation() {
    const auto factory=g_writeFactory;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"refresh presentation fonts");
    {
        PresentationFixture::Tab tab(L"unchanged refresh",3);
        auto& s=tab.state;
        tab.Start(); tab.Await([](const auto& r){return r.accepted;});
        // Drain the initial composition handoff through an owner-lane barrier.
        // The first receipt precedes the worker's initial animation decision.
        ++s.directoryToken; SynchronizePresentationGeneration(s);
        tab.Await([&](const auto& r){return r.directoryToken==s.directoryToken;});
        Check(Eventually([&]{return !s.presenter->animating.load();}),"refresh presenter did not settle");
        const auto before=tab.Receipt();
        ++s.directoryToken; ValidateRect(s.hwnd,nullptr);
        SynchronizePresentationGeneration(s);
        auto after=tab.Await([&](const auto& r){return r.directoryToken==s.directoryToken;});
        if(!(after->accepted && after->sequence==before->sequence && after->frames==before->frames &&
            after->device==before->device && after->backbuffer==before->backbuffer && !GetUpdateRect(s.hwnd,nullptr,FALSE)))
            std::cout<<"REFRESH GENERATION accepted="<<after->accepted<<" sequence="<<before->sequence<<"->"<<after->sequence
                <<" frames="<<before->frames<<"->"<<after->frames<<" device="<<before->device<<"->"<<after->device
                <<" buffer="<<before->backbuffer<<"->"<<after->backbuffer<<" update="<<GetUpdateRect(s.hwnd,nullptr,FALSE)<<std::endl;
        Check(after->accepted && after->sequence==before->sequence && after->frames==before->frames &&
            after->device==before->device && after->backbuffer==before->backbuffer && !GetUpdateRect(s.hwnd,nullptr,FALSE),
            "identical refresh repainted instead of acknowledging the existing model");
        s.thisPcGpu=true; s.nativeRowsNeedCapture=false; s.nativeRowsComplete=true;
        s.nativeRowsViewToken=s.token; s.nativeRows=s.entries;
        RefreshThisPcRows(s,false); StopRowTransition(s);
        auto* entries=s.entries.data(); ValidateRect(s.hwnd,nullptr);
        DirectoryResult rows; rows.kind=DirectoryResult::NativeRows; rows.token=s.nativeRowsToken;
        rows.entries=s.nativeRows; rows.nativeRowsComplete=true;
        PublishDirectoryResult(*s.background,std::move(rows));
        Check(!ReceiveDirectoryResults(s) && s.entries.data()==entries && !GetUpdateRect(s.hwnd,nullptr,FALSE),
            "identical This PC identities reported a content change or repainted");
        const auto capacity=[&](const wchar_t* caption=L"free space") {
            DirectoryResult result; result.kind=DirectoryResult::DriveSpace; result.token=s.capacityToken;
            result.driveSpaceComplete=true; DriveCapacity value;
            value.root=s.entries.front().path; value.valid=true; value.total=1000; value.used=250;
            value.hasFreeBytes=true; value.freeText=caption;
            result.driveSpace.push_back(value); PublishDirectoryResult(*s.background,std::move(result));
            Check(!ReceiveDirectoryResults(s),"capacity reply reported content before comparing projected values");
            return RefreshThisPcRows(s,false);
        };
        Check(capacity(),"changed capacity was not projected");
        ValidateRect(s.hwnd,nullptr); entries=s.entries.data();
        Check(!capacity() && s.entries.data()==entries && !GetUpdateRect(s.hwnd,nullptr,FALSE),
            "identical capacity replaced or repainted unchanged rows");
        Check(capacity(L"updated formatted space"),"changed formatted capacity was hidden by identical raw bytes");
    }
    {
        PresentationFixture::Tab tab(L"refresh navigation race",3);
        auto& s=tab.state;
        PresentationFixture::Hold hold(s.hwnd);
        tab.Start();
        Check(WaitForSingleObject(PresentationFixture::entered,5000)==WAIT_OBJECT_0,"refresh race worker gate");
        ++s.directoryToken; SynchronizePresentationGeneration(s);
        ++s.directoryToken; s.selected=1; s.selectedFlags={0,1,0}; SubmitPresentation(s);
        hold.Release();
        tab.Await([&](const auto& r){return r.accepted && r.sequence==s.presentationSequence && r.directoryToken==s.directoryToken;});
    }
    g_writeFactory=factory;
    std::cout<<"PASS: unchanged refresh advances presenter generation without a GPU paint; identical This PC identities and capacities preserve rows/pixels\n";
}

static void TestPeriodicCapacityRefresh() {
    CapacityFixture::Session volume;
    std::weak_ptr<TabBackground> lifetime;
    {
    PresentationFixture::Tab tab(L"capacity maintenance",1);
    auto& state=tab.state;
    lifetime=state.background;
    FocusFixture::Session focus(tab.frame,state.hwnd);
    state.nativeHwnd=CreateWindowExW(0,L"STATIC",L"private native capacity view",WS_CHILD|WS_VISIBLE,
        0,0,640,480,tab.frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.nativeHwnd!=nullptr,"capacity maintenance native view");
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    state.folderPath=kThisPcLocation; state.thisPcGpu=true;
    state.directoryModePinned=true;
    ApplyViewLayout(state,{FVM_LIST,48},false);
    // Names already exist; icons/presentation need not be ready for capacity.
    state.loaded=false;
    state.nativeRowsComplete=true; state.nativeRowsNeedCapture=false;
    // A subscribed view takes the event path; no OS hook is installed here.
    state.viewEvents=reinterpret_cast<HWINEVENTHOOK>(UINT_PTR(1));
    state.nativeRowsViewToken=state.token;
    auto& drive=state.entries.front(); drive.path=L"Z:\\"; drive.name=L"Synthetic volume";
    auto empty=static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(sizeof(USHORT)));
    Check(empty!=nullptr,"capacity maintenance synthetic identity"); empty->mkid.cb=0;
    drive.shellIdentity=SharedPidl(empty,TaskMemDeleter{});
    state.nativeRows=state.entries;
    state.lastActiveViewProbe=GetTickCount64();
    state.driveCapacityRequestedAt=GetTickCount64()-kDriveSpaceRefreshMs;
    ResetEvent(CapacityFixture::gate);
    OverlayProc(state.hwnd,WM_TIMER,kTimerId,0);
    Check(Eventually([&]{return CapacityFixture::calls==1;}),"due capacity maintenance did not start its worker");
    Check(state.directoryPending && CapacityFixture::uiCalls==0 && !state.nativeRowsNeedCapture &&
        !state.nativeRowsMessageQueued && !state.nativeRowsPending && !state.thisPcRowsDirty &&
        state.background->nativeRowsJobs.empty(),"capacity maintenance also enumerated devices or blocked the UI");
    // A real membership hint still takes the identity lane, independently of
    // the blocked capacity request. No actual device or volume is touched.
    QueueSystemShellChange(state,SHCNE_DRIVEADD);
    FlushShellChangeHints(state,GetTickCount64()+kShellVisualCoalesceMs);
    Check(state.nativeRowsNeedCapture && state.nativeRowsMessageQueued,
        "drive membership event no longer requested replacement identities");
    SetEvent(CapacityFixture::gate);
    Check(Eventually([&]{ReceiveDirectoryResults(state); return !state.directoryPending;}),
        "periodic capacity result did not settle");
    // Only a missing native subscription admits the old discovery recovery.
    state.viewEvents=nullptr; state.loaded=false; state.thisPcRowsDirty=false;
    state.nativeRowsNeedCapture=state.nativeRowsMessageQueued=false;
    state.nativeSettingsHintPending=state.visibilitySignalQueued=false;
    state.driveCapacityRequestedAt=GetTickCount64()-kDriveSpaceRefreshMs;
    state.lastActiveViewProbe=GetTickCount64();
    OverlayProc(state.hwnd,WM_TIMER,kTimerId,0);
    Check(state.nativeSettingsHintPending && state.visibilitySignalQueued,
        "missing native subscription lost portable-device discovery recovery");
    MSG hint{};
    Check(PeekMessageW(&hint,state.hwnd,kMsgShellVisibilityChanged,kMsgShellVisibilityChanged,PM_REMOVE),
        "missing-subscription recovery did not queue its owner hint");
    OverlayProc(state.hwnd,hint.message,hint.wParam,hint.lParam);
    Check(state.nativeRowsNeedCapture && state.nativeRowsMessageQueued,
        "missing-subscription owner hint did not request identities");
    Check(Eventually([&]{ReceiveDirectoryResults(state); return !state.directoryPending;}),
        "missing-subscription capacity result did not settle");
    }
    Check(Eventually([&]{return lifetime.expired();}),"capacity maintenance workers did not retire");
    std::cout<<"PASS: subscribed capacity timer uses only the background volume lane; drive events and missing-subscription recovery still request identities\n";
}

static void TestDriveSpaceRefresh() {
    CapacityFixture::Session volume;
    Check(CapacityFixture::gate!=nullptr,"synthetic volume gate");
    ViewState state, other;
    state.hwnd=MessageWindow(); state.background->hwnd=state.hwnd;
    state.folderPath=kThisPcLocation; state.thisPcGpu=true;
    state.loaded=state.inScope=state.nativeRowsComplete=true;
    state.nativeRowsNeedCapture=false;
    state.directoryToken=991; state.background->directoryGeneration=state.directoryToken;
    Entry drive; drive.path=L"Z:\\"; drive.name=L"Synthetic volume"; drive.folder=true;
    auto empty=static_cast<PIDLIST_ABSOLUTE>(CoTaskMemAlloc(sizeof(USHORT)));
    Check(empty!=nullptr,"synthetic volume identity"); empty->mkid.cb=0;
    drive.shellIdentity=SharedPidl(empty,TaskMemDeleter{});
    state.nativeRows=state.entries={drive}; state.total=1;
    ExplorerColumn name; name.key=PKEY_FileName;
    ExplorerColumn extra; extra.key=PKEY_Author;
    state.columns={name,extra};
    DetailJob details; details.token=state.directoryToken; details.columnEpoch=state.columnEpoch;
    details.requests.emplace_back(); details.requests.back().path=drive.path;
    state.background->detailJobs.push_back(details);
    for(int i=0;i<1000;++i) QueueSystemShellChange(state,SHCNE_FREESPACE);
    Check(state.driveSpaceRefreshPending && !state.shellDriveDirty && !state.directoryPending &&
        CapacityFixture::calls==0 && state.background->nativeRowsJobs.empty(),
        "free-space notification synchronously probed a volume or refreshed native identities");
    QueueSystemShellChange(other,SHCNE_FREESPACE);
    Check(!other.driveSpaceRefreshPending,"filesystem tab admitted free-space work");
    state.driveSpaceChangedAt=GetTickCount64()-kShellVisualCoalesceMs;
    const auto generation=state.directoryToken, epoch=state.columnEpoch;
    {
        ExclusiveSrwLock lock(state.background->directoryLock);
        QueuePendingDriveSpaceRefresh(state,GetTickCount64());
        Check(state.driveSpaceRefreshPending && !state.directoryPending,
            "contended capacity submission lost its pending retry");
    }
    ResetEvent(CapacityFixture::gate);
    QueuePendingDriveSpaceRefresh(state,GetTickCount64());
    Check(Eventually([&]{return CapacityFixture::calls==1;}),"coalesced capacity worker did not start");
    Check(state.directoryPending && state.directoryToken==generation && state.columnEpoch==epoch &&
        state.background->detailJobs.size()==1 && state.background->nativeRowsJobs.empty() &&
        CapacityFixture::uiCalls==0,"capacity update blocked UI or invalidated unrelated metadata/names");
    const auto oldCapacity=state.capacityToken;
    QueueSystemShellChange(state,SHCNE_FREESPACE);
    QueuePendingDriveSpaceRefresh(state,GetTickCount64()+2000);
    Check(state.capacityToken==oldCapacity && state.driveSpaceRefreshPending,
        "disk hint restarted an in-flight probe or lost the follow-up");
    SetEvent(CapacityFixture::gate);
    Check(Eventually([&]{ ReceiveDirectoryResults(state); return !state.directoryPending; }),"volume result did not arrive");
    RefreshThisPcRows(state,false);
    Check(state.entries[0].driveUsed==400,"capacity refresh trusted the stale Shell free-space value");
    const auto* caption=ColumnCellText(state.entries[0],PKEY_Computer_DecoratedFreeSpace,
        state.columnEpoch,state.propertyEpoch);
    Check(caption && !caption->empty(),"capacity worker did not publish the localized tile caption");
    const auto previousCaption=*caption;
    CapacityFixture::freeBytes=250;
    QueuePendingDriveSpaceRefresh(state,GetTickCount64()+2000);
    Check(state.capacityToken!=oldCapacity,"disk change during the probe was not followed up");
    DirectoryResult stale; stale.kind=DirectoryResult::DriveSpace; stale.token=oldCapacity;
    stale.driveSpaceComplete=true;
    PublishDirectoryResult(*state.background,std::move(stale));
    Check(Eventually([&]{ ReceiveDirectoryResults(state); return !state.directoryPending; }),"follow-up volume result did not arrive");
    RefreshThisPcRows(state,false);
    Check(state.entries[0].driveUsed==750 && CapacityFixture::calls==2 && CapacityFixture::uiCalls==0 &&
        state.directoryToken==generation && state.background->detailJobs.size()==1,
        "fresh space did not replace stale space independently of property work");
    caption=ColumnCellText(state.entries[0],PKEY_Computer_DecoratedFreeSpace,
        state.columnEpoch,state.propertyEpoch);
    Check(caption && !caption->empty() && *caption!=previousCaption,
        "tile capacity caption retained old bytes after a background update");
    state.entries[0].cells.push_back({PKEY_Author,L"old provider value"});
    state.entries[0].detailsReady=true;
    const auto propertyEpoch=state.propertyEpoch;
    const auto refreshEpoch=state.detailsRefreshEpoch;
    CapacityFixture::freeBytes=125;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    {
        // Scoped simulated input remains stable through nested message pumps
        // and never depends on or changes the user's keyboard state.
        ModifierFixture::Session modifiers;
        modifiers.keys[VK_MENU]=0x80;
        OverlayProc(state.hwnd,WM_KEYDOWN,VK_F5,0);
        Check(state.detailsRefreshEpoch==refreshEpoch,"Alt+F5 unexpectedly forced a volume refresh");
        modifiers.keys[VK_MENU]=0;
        OverlayProc(state.hwnd,WM_KEYDOWN,VK_F5,0);
    }
    Check(state.detailsRefreshEpoch!=refreshEpoch && state.propertyEpoch==propertyEpoch,
        "This PC F5 did not revalidate while retaining displayed provider values");
    Check(Eventually([&]{
        QueuePendingDriveSpaceRefresh(state,GetTickCount64()+2000);
        ReceiveDirectoryResults(state);
        return !state.directoryPending && !state.driveSpaceRefreshPending;
    }),"forced volume result did not arrive");
    RefreshThisPcRows(state,false);
    const auto* author=ColumnCellText(state.entries[0],PKEY_Author,state.columnEpoch,state.propertyEpoch);
    Check(state.entries[0].driveUsed==875 &&
        author && *author==L"old provider value" &&
        ColumnCellText(state.entries[0],PKEY_FreeSpace,state.columnEpoch,state.propertyEpoch),
        "This PC projection blanked retained extras or hid freshly probed seeded properties");
    std::weak_ptr<TabBackground> lifetime=state.background;
    UnregisterAnimationWindow(state.hwnd); Stop(state.background);
    Check(Eventually([&]{return lifetime.expired();}),"capacity refresh workers did not retire");
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.hwnd);
    std::cout<<"PASS: coalesced disk changes, nonblocking queue retry, independent capacity generations, stale Shell/late results, This PC F5 and zero UI volume/provider calls\n";
}
