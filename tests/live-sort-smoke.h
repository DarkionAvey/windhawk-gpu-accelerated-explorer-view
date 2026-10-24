static LRESULT CALLBACK LiveSortNotifications(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam,
                                             UINT_PTR,DWORD_PTR) {
    if(message==kMsgShellItemCreated) return OverlayProc(hwnd,message,wParam,lParam);
    return DefSubclassProc(hwnd,message,wParam,lParam);
}

static void TestLiveSorting(HWND frame, const std::wstring& fixture) {
    const auto directory=JoinPath(fixture,L"live-sort");
    std::filesystem::create_directory(directory);
    const ULONGLONG base=135000000000000000ULL;
    const auto stamp=[](ULONGLONG value) {
        return FILETIME{static_cast<DWORD>(value),static_cast<DWORD>(value>>32)};
    };
    for(int i=0;i<6;++i) {
        const auto path=JoinPath(directory,L"Item-"+std::to_wstring(i)+L".txt");
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE|FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ,
            nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"live sort fixture");
        DWORD written=0; const char bytes[]="abcdef";
        Check(WriteFile(file,bytes,i+1,&written,nullptr),"live sort sizes");
        const auto time=stamp(base+i*10000000ULL);
        Check(SetFileTime(file,&time,&time,&time),"live sort timestamps"); CloseHandle(file);
    }
    ViewState state;
    state.hwnd=CreateWindowExW(0,L"STATIC",L"live-sort",WS_CHILD,
        0,0,640,360,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.hwnd!=nullptr,"live sort window");
    state.frameHwnd=frame; state.nativeHwnd=state.activeTabHost=state.hwnd;
    state.background->hwnd=state.hwnd; state.folderPath=directory;
    state.visible=state.inScope=true;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    Check(SetWindowSubclass(state.hwnd,LiveSortNotifications,1,0),"live Shell notification receiver");
    UpdateRefreshRate(state); UpdateMetrics(state); UpdateColumns(state);
    const auto settle=[&] {
        Check(Eventually([&] { ReceiveDirectoryResults(state); return state.loaded && !state.directoryPending; }),
            "live sort directory publication");
    };
    const auto choose=[&](const PROPERTYKEY& key) {
        state.sort.key=key; state.sort.kind=ClassifySort(key);
        state.sort.explicitOrder=state.sortInitialized=true;
        state.sort.direction=SORT_DESCENDING; state.sort.placement=FolderPlacement::Mixed;
        QueueDirectoryRefresh(state,true,true); settle(); StopRowTransition(state);
    };
    const auto hint=[&](const std::wstring& path,bool changed) {
        const auto token=state.directoryToken;
        for(int i=0;i<100;++i) QueueShellItemUpdate(state,path);
        FlushShellChangeHints(state,GetTickCount64()+kShellVisualCoalesceMs+1);
        Check(state.sortCheckPending && !state.directoryPending && state.directoryToken==token,
            "item hint synchronously refreshed rows instead of observing the sort key");
        Check(Eventually([&] { ReceiveDirectoryResults(state); return !state.sortCheckPending; }),
            "sort-key observation did not finish");
        Check(state.watchedChangePending==changed,"sort key change detection did not match actual file data");
        if(changed) {
            UpdateWatchedDirectory(state,GetTickCount64());
            settle();
            Check(state.directoryToken!=token,"changed active sort key did not revalidate the snapshot");
        } else Check(state.directoryToken==token && !state.directoryPending,
            "unchanged metadata hints restarted directory enumeration");
    };
    int revision=10;
    for(const PROPERTYKEY key : {PKEY_DateModified,PKEY_Size,PKEY_DateCreated,PKEY_DateAccessed,PKEY_FileAttributes}) {
        choose(key);
        SetWindowPos(state.hwnd,nullptr,0,0,640,static_cast<int>(state.headerPx+2*state.rowPx),
            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SetScrollImmediate(state,MaxScrollPx(state));
        const int before=state.total-1;
        const float oldTop=LayoutRowVisual(state,before,1.f).top;
        const float oldScroll=state.scrollNow;
        const auto path=state.entries[before].path;
        state.selected=before; state.selectedFlags.assign(state.total,0); state.selectedFlags[before]=1;
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE|FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ,
            nullptr,OPEN_EXISTING,0,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"live edit handle");
        const auto time=stamp(base+(++revision)*10000000ULL);
        if(SameKey(key,PKEY_Size)) {
            Check(SetFilePointer(file,1024+revision,nullptr,FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
                SetEndOfFile(file),"live size edit");
        } else if(!SameKey(key,PKEY_FileAttributes))
            Check(SetFileTime(file,SameKey(key,PKEY_DateCreated)?&time:nullptr,
                SameKey(key,PKEY_DateAccessed)?&time:nullptr,SameKey(key,PKEY_DateModified)?&time:nullptr),"live date edit");
        CloseHandle(file);
        if(SameKey(key,PKEY_FileAttributes))
            Check(SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_ARCHIVE),"live Shell-property edit");
        hint(path,true);
        const int after=GpuRowForNativePath(state,path);
        Check(after>=0 && after<before && state.selected==after && state.selectedFlags[after] &&
            std::is_sorted(state.entries.begin(),state.entries.end(),[&](const Entry& a,const Entry& b) {
                return EntryPrecedes(a,b,state.sort);
            }),"live file edit did not preserve selection at its active-sort rank");
        Check(state.rowTransitionActive &&
            fabsf(LayoutRowVisual(state,after,0).top-oldTop)<.1f,
            "live resort skipped the existing row movement animation");
        Check(state.animationActive && state.scrollTarget<oldScroll && state.scrollNow>state.scrollTarget &&
            fabsf(RevealScrollTarget(state,after,state.scrollTarget)-state.scrollTarget)<.1f,
            "modified item was not followed with the shared animated scroll");
        StopRowTransition(state); SetScrollImmediate(state,state.scrollTarget);
        const float settledScroll=state.scrollNow;
        hint(path,false);
        Check(!state.animationActive && state.scrollNow==settledScroll,
            "unchanged metadata restarted the modified-item reveal");
        if(SameKey(key,PKEY_FileAttributes)) SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);
    }
    // The rename commit updates the displayed identity before its worker
    // request. Exercise that same handoff for name, extension and type sorts.
    for(const PROPERTYKEY key : {PKEY_FileName,PKEY_FileExtension,PKEY_ItemTypeText}) {
        choose(key);
        const int before=state.total-1;
        const auto oldPath=state.entries[before].path;
        const auto name=L"ZZ-renamed-"+std::to_wstring(++revision)+L".zzz";
        const auto path=JoinPath(directory,name);
        Check(MoveFileW(oldPath.c_str(),path.c_str()),"live rename fixture");
        state.entries[before].path=path; state.entries[before].name=name;
        state.sortRevealPath=path; state.sortRevealOldIndex=before;
        state.selected=before; state.selectedFlags.assign(state.total,0); state.selectedFlags[before]=1;
        QueueDirectoryRefresh(state,true,true); settle();
        const int after=GpuRowForNativePath(state,path);
        Check(after>=0 && state.selected==after && std::is_sorted(state.entries.begin(),state.entries.end(),
            [&](const Entry& a,const Entry& b) { return EntryPrecedes(a,b,state.sort); }),
            "rename did not use the shared active sort/comparator");
        if(after!=before) Check(state.rowTransitionActive,"rename resort was not animated");
        StopRowTransition(state);
    }
    // A raw Win32 watcher has no item path. The layout worker's previous
    // accepted sort snapshot still finds an offscreen, unselected edit.
    choose(PKEY_Size);
    SetScrollImmediate(state,0.f);
    const auto unselected=state.entries.front().path;
    state.selected=-1; state.selectedFlags.assign(state.total,0);
    HANDLE edit=CreateFileW(unselected.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Check(edit!=INVALID_HANDLE_VALUE && SetFilePointer(edit,0,nullptr,FILE_BEGIN)!=INVALID_SET_FILE_POINTER &&
        SetEndOfFile(edit),"watch-only sort edit"); CloseHandle(edit);
    ObserveDirectoryWatch(state,GetTickCount64(),true);
    UpdateWatchedDirectory(state,GetTickCount64()); settle();
    Check(GpuRowForNativePath(state,unselected)==state.total-1 && state.animationActive &&
        state.scrollTarget>state.scrollNow && state.selected==-1,
        "watch-only unselected edit did not animate scrolling to its new rank");
    StopRowTransition(state); SetScrollImmediate(state,state.scrollTarget);
    choose(PKEY_FileName);
    WatchDirectory(state);
    Check(Eventually([&] { ReceiveDirectoryResults(state); return state.directoryWatch &&
        state.directoryWatch->registration; }),"live Shell notification registration");
    UpdateWatchedDirectory(state,GetTickCount64());
    settle(); StopRowTransition(state);
    const auto oldPath=state.entries.back().path;
    const auto renamedPath=JoinPath(directory,L"ZZZ-live-Shell-rename.txt");
    state.selected=-1; state.selectedFlags.assign(state.total,0);
    Check(MoveFileW(oldPath.c_str(),renamedPath.c_str()),"external Shell rename fixture");
    SHChangeNotify(SHCNE_RENAMEITEM,SHCNF_PATHW|SHCNF_FLUSHNOWAIT,oldPath.c_str(),renamedPath.c_str());
    Check(Eventually([&] {
        MSG message{};
        while(PeekMessageW(&message,state.hwnd,kMsgShellItemCreated,kMsgShellItemCreated,PM_REMOVE))
            DispatchMessageW(&message);
        return state.watchedChangePending;
    }),"real Shell rename notification did not reach the shared directory batcher");
    Check(Eventually([&] {
        UpdateWatchedDirectory(state,GetTickCount64()); ReceiveDirectoryResults(state);
        return !state.watchedChangePending && !state.directoryPending && GpuRowForNativePath(state,renamedPath)>=0;
    }),"external Shell rename batch did not settle");
    Check(GpuRowForNativePath(state,renamedPath)<state.total-1 && state.rowTransitionActive,
        "external Shell rename did not update and animate the active sort");
    StopRowTransition(state); CloseDirectoryWatch(state);
    // Overflow and unchanged enrichment must not restart names/animation.
    const auto generation=state.directoryToken;
    state.shellItemOverflow=true; state.shellVisualChangeAt=GetTickCount64();
    FlushShellChangeHints(state,GetTickCount64()+kShellVisualCoalesceMs+1);
    Check(Eventually([&] { ReceiveDirectoryResults(state); return !state.sortCheckPending; }),"sort overflow observation");
    Check(state.directoryToken==generation && !state.watchedChangePending && !state.rowTransitionActive,
        "unchanged sorted overflow replaced or animated the displayed snapshot");
    const std::vector<std::wstring> retry{state.entries.front().path};
    {
        ExclusiveSrwLock lock(state.background->directoryLock);
        RevalidateActiveSort(state,&retry);
        Check(!state.sortCheckPending && !state.sortUpdatedItems.empty(),
            "contended sort submission blocked or lost its retry intent");
    }
    UpdateWatchedDirectory(state,GetTickCount64());
    Check(Eventually([&] { ReceiveDirectoryResults(state); return !state.sortCheckPending; }) &&
        state.directoryToken==generation && !state.watchedChangePending,"sort submission retry did not settle unchanged");
    state.sort.explicitOrder=false;
    QueueShellItemUpdate(state,state.entries.front().path);
    FlushShellChangeHints(state,GetTickCount64()+kShellVisualCoalesceMs+1);
    Check(Eventually([&] { ReceiveDirectoryResults(state); return !state.sortCheckPending; }) &&
        state.directoryToken==generation && !state.watchedChangePending,
        "unchanged natural-order hints replaced the displayed snapshot");
    state.sort=DefaultSort();
    QueueDirectoryRefresh(state,true,true); settle(); StopRowTransition(state);
    SetScrollImmediate(state,MaxScrollPx(state));
    const int naturalBefore=state.total-1;
    const auto naturalOld=state.entries[naturalBefore].path;
    const auto naturalName=L"!natural-first.txt";
    const auto naturalPath=JoinPath(directory,naturalName);
    Check(MoveFileW(naturalOld.c_str(),naturalPath.c_str()),"natural-order rename fixture");
    state.entries[naturalBefore].name=naturalName; state.entries[naturalBefore].path=naturalPath;
    state.sortRevealPath=naturalPath; state.sortRevealOldIndex=naturalBefore;
    state.selected=naturalBefore; state.selectedFlags.assign(state.total,0); state.selectedFlags[naturalBefore]=1;
    QueueDirectoryRefresh(state,true,true); settle();
    Check(GpuRowForNativePath(state,naturalPath)==0 && state.selected==0 &&
        state.rowTransitionActive && state.animationActive && state.scrollTarget<state.scrollNow,
        "implicit natural-order rename did not animate its new location and scroll to it");
    StopRowTransition(state); SetScrollImmediate(state,state.scrollTarget);
    // Fresh values at an unchanged rank still advance the namespace baseline.
    auto baseline=state.entries;
    const auto hash=DirectoryMetadataHash(baseline);
    baseline.front().sortNumber+=1; baseline.front().sortValueKind=2;
    Check(DirectoryMetadataHash(baseline)!=hash,"sort-key-only update retained its stale snapshot fingerprint");
    SortSpec mixed; mixed.kind=SortKind::Type; mixed.explicitOrder=true;
    Entry filesystem, shell;
    filesystem.path=L"file"; filesystem.name=L"first"; filesystem.sortText=L"A";
    shell.path=L"windhawk:pidl:private-fixture"; shell.name=L"second"; shell.sortText=L"Z";
    Check(EntryPrecedes(filesystem,shell,mixed) && !EntryPrecedes(shell,filesystem,mixed),
        "shared comparator changed its key semantics between the two identities");
    UnregisterAnimationWindow(state.hwnd); Stop(state.background);
    RemoveWindowSubclass(state.hwnd,LiveSortNotifications,1);
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.hwnd);
    for(const auto& file : std::filesystem::directory_iterator(directory))
        SetFileAttributesW(file.path().c_str(),FILE_ATTRIBUTE_NORMAL);
    std::filesystem::remove_all(directory);
    ViewState volumes;
    volumes.hwnd=CreateWindowExW(0,L"STATIC",L"live-capacity-sort",WS_CHILD,
        0,0,640,360,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    volumes.frameHwnd=frame; volumes.nativeHwnd=volumes.activeTabHost=volumes.hwnd;
    volumes.thisPcGpu=volumes.visible=volumes.inScope=volumes.nativeRowsComplete=true;
    volumes.nativeRowsNeedCapture=false; volumes.folderPath=kThisPcLocation;
    volumes.sort.key=PKEY_FreeSpace; volumes.sort.kind=SortKind::ShellProperty;
    volumes.sort.direction=SORT_DESCENDING; volumes.sort.explicitOrder=true;
    volumes.driveCapacityRequestedAt=GetTickCount64();
    for(const wchar_t* path : {L"Y:\\",L"Z:\\"}) {
        Entry entry; entry.path=path; entry.name=path; entry.folder=true;
        volumes.nativeRows.push_back(entry);
        DriveCapacity capacity; capacity.root=path; capacity.valid=capacity.hasFreeBytes=true;
        capacity.total=1000; capacity.used=volumes.thisPcCapacities.empty() ? 900 : 200;
        volumes.thisPcCapacities.push_back(capacity); volumes.thisPcSpaceRequestedPaths.push_back(path);
    }
    UpdateRefreshRate(volumes); UpdateMetrics(volumes); UpdateColumns(volumes);
    RefreshThisPcRows(volumes,false);
    Check(volumes.entries.front().path==L"Z:\\" && !volumes.rowTransitionActive,"initial capacity sort or initial-content animation");
    volumes.selected=1; volumes.selectedFlags={0,1};
    volumes.thisPcCapacities[0].used=0;
    RefreshThisPcRows(volumes,false);
    Check(volumes.entries.front().path==L"Y:\\" && volumes.selected==0 && volumes.rowTransitionActive,
        "cached drive capacity change did not animate its active sort rank and selection");
    QueryPerformanceCounter(&volumes.rowTransitionStartedQpc);
    const auto origin=volumes.rowTransitionStartedQpc.QuadPart;
    volumes.thisPcCapacities[0].used=5;
    RefreshThisPcRows(volumes,false);
    Check(volumes.rowTransitionActive && volumes.rowTransitionStartedQpc.QuadPart==origin,
        "stable-rank capacity enrichment interrupted the preceding sort animation");
    UnregisterAnimationWindow(volumes.hwnd); Stop(volumes.background); DestroyWindow(volumes.hwnd);
    std::cout<<"PASS: live date/size/created/accessed/Shell-property edits, explicit and natural-order renames, real Shell notifications, animated ranks/scroll follow, unchanged/overflow observations and preserved selection\n";
}
