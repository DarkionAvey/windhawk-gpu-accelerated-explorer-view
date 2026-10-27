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
