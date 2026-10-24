// Provider snapshots, not device-specific animation calls. Synthetic identities
// never access a real volume, and focus/clipboard are untouched.
static void TestNamespaceItemMutations(const std::wstring& fixture) {
    const auto factory=g_writeFactory;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"namespace mutation fonts");
    for(bool grid:{false,true}) for(bool grouped:{false,true}) for(bool rtl:{false,true}) for(bool sorted:{false,true}) {
        std::cout<<"NAMESPACE CASE grid="<<grid<<" grouped="<<grouped<<" rtl="<<rtl<<" sorted="<<sorted<<std::endl;
        PresentationFixture::Tab tab(L"namespace-mutations",0,rtl,grid,false,900,600);
        auto& s=tab.state;
        s.thisPcGpu=true; s.folderPath=kThisPcLocation; s.loaded=false;
        s.nativeRowsNeedCapture=false; s.nativeRowsViewToken=s.token;
        s.grouping.enabled=grouped; s.grouping.key=PKEY_FileName; s.grouping.ascending=TRUE;
        s.sort=DefaultSort(); s.sort.explicitOrder=sorted; s.sort.kind=SortKind::Name;
        s.sort.key=PKEY_FileName; s.sort.direction=SORT_ASCENDING;
        s.directoryModePinned=true; s.nativeRowsComplete=false;
        ApplyViewLayout(s,{UINT(grid ? FVM_ICON : FVM_DETAILS),48},false);
        UpdateMetrics(s); MakeTextFormats(s); UpdateRefreshRate(s);
        const auto item=[](const wchar_t* identity,const wchar_t* name) {
            Entry e; e.path=std::wstring(L"namespace-fixture:")+identity; e.name=name; e.folder=true; return e;
        };
        const std::vector<Entry> initial{item(L"a",L"Alpha"),item(L"c",L"Charlie"),item(L"e",L"Echo")};
        const Entry inserted=item(L"b",L"Bravo");
        const auto begin=[&] {
            s.nativeRowsToken=g_nextToken.fetch_add(1); s.nativeRowsViewToken=s.token;
            s.background->nativeRowsGeneration=s.nativeRowsToken;
            s.nativeRowsNeedCapture=false; s.nativeRowsPending=true;
        };
        const auto deliver=[&](std::vector<Entry> rows,bool complete=true) {
            DirectoryResult result; result.kind=DirectoryResult::NativeRows; result.token=s.nativeRowsToken;
            result.entries=std::move(rows); result.nativeRowsComplete=complete;
            PublishDirectoryResult(*s.background,std::move(result)); ReceiveDirectoryResults(s);
        };
        begin(); deliver(initial);
        Check(s.loaded && s.total==3 && !s.rowTransitionActive,"cold namespace snapshot animated as an insertion");
        s.selected=1; s.anchor=1; s.selectedFlags={0,1,0};
        const auto focus=initial[1].path;
        begin();
        DirectoryResult failed; failed.kind=DirectoryResult::NativeRows; failed.token=s.nativeRowsToken;
        failed.entries={initial.front()}; failed.error=ERROR_NOT_READY;
        PublishDirectoryResult(*s.background,std::move(failed)); ReceiveDirectoryResults(s);
        Check(s.loaded && s.total==3 && s.selected==1 && !s.nativeRowsPending && !s.rowTransitionActive,
            "failed provider revalidation was mistaken for removal of unseen items");
        begin(); deliver({initial.front()},false);
        Check(s.loaded && s.total==3 && s.selected==1,"provider preview reinitialized an already-loaded namespace");
        s.directoryPending=true; // A separate capacity request must remain pending.
        deliver({initial[0],inserted,initial[1],initial[2]});
        Check(s.directoryPending && s.loaded && s.total==4 && s.rowTransitionActive && s.enumerationError==ERROR_SUCCESS,
            "namespace membership completion failed to use the shared structural transition or finished capacity I/O");
        Check(s.disappearingRows.empty(),"surviving namespace items were recreated as deleted ghosts");
        for(const auto& old:initial) {
            const int index=GpuRowForNativePath(s,old.path);
            const auto motion=std::find_if(s.rowMotion.begin(),s.rowMotion.end(),[&](const auto& m) {
                return (grid || grouped ? m.newIndex : s.rowMotionFirst+int(&m-s.rowMotion.data()))==index;
            });
            Check(index>=0 && motion!=s.rowMotion.end() && !motion->entering && motion->oldOpacity>.99f,
                "surviving namespace identity lost its position/opacity during insertion");
        }
        Check(GpuRowForNativePath(s,focus)==s.selected && s.selectedFlags[s.selected],"namespace insertion lost selection identity");
        Check(s.entryPathIndex.size()==4,"namespace backend did not publish the shared item index");
        if(grid || grouped) Check(s.groupingLayoutTransition,"grouped/grid mutation bypassed the common 2D layout");
        QueryPerformanceCounter(&s.rowTransitionStartedQpc);
        const auto origin=s.rowTransitionStartedQpc.QuadPart;
        const auto revision=s.rowSceneRevision;
        begin(); deliver({initial[0],inserted,initial[1],initial[2]});
        Check(s.rowTransitionStartedQpc.QuadPart==origin && s.rowSceneRevision==revision,
            "unchanged native snapshot restarted structural motion");
        s.capacityToken=g_nextToken.fetch_add(1); s.background->capacityGeneration=s.capacityToken;
        DirectoryResult capacity; capacity.kind=DirectoryResult::DriveSpace; capacity.token=s.capacityToken;
        DriveCapacity value; value.root=initial[0].path; value.valid=value.hasFreeBytes=true;
        value.total=1000; value.used=250; value.type=L"Storage"; capacity.driveSpace.push_back(value);
        PublishDirectoryResult(*s.background,std::move(capacity)); ReceiveDirectoryResults(s); RefreshThisPcRows(s,false);
        Check(s.rowTransitionStartedQpc.QuadPart==origin && s.rowSceneRevision==revision && s.entries[0].driveUsed==250,
            "capacity enrichment reset an insertion or lost its fresh values");
        StopRowTransition(s); s.directoryPending=false;
        begin(); deliver(initial);
        Check(s.rowTransitionActive && s.disappearingRows.size()==1 && s.disappearingRows.front().entry.path==inserted.path,
            "namespace removal did not retain exactly its removed identity");
        Check(s.selected>=0 && s.entries[s.selected].path==focus,"namespace removal shifted selection to another item");
        StopRowTransition(s);
        begin(); deliver({});
        Check(s.loaded && s.total==0 && s.rowTransitionActive && s.disappearingRows.size()==3,
            "last namespace removals cleared the view without normal exit animation");
        StopRowTransition(s);
        begin(); deliver(initial);
        Check(s.rowTransitionActive && s.disappearingRows.empty(),"empty namespace recovery bypassed normal insertion");
        StopRowTransition(s);
        // Equal captions/counts must still reconcile different identities.
        auto sameLabels=initial; for(auto& e:sameLabels) e.name=L"Device";
        begin(); deliver(sameLabels); StopRowTransition(s);
        auto replacement=sameLabels; replacement[1].path+=L":replacement";
        begin(); deliver(replacement);
        Check(s.entryPathIndex.size()==3 && GpuRowForNativePath(s,sameLabels[1].path)<0 &&
            GpuRowForNativePath(s,replacement[1].path)>=0 && s.rowTransitionActive,
            "same-count/same-caption namespace replacement was mistaken for an unchanged model");
        StopRowTransition(s);
        QueueSystemShellChange(s,SHCNE_DRIVEADD|SHCNE_MEDIAINSERTED);
        s.shellVisualChangeAt=GetTickCount64()-kShellVisualCoalesceMs;
        FlushShellChangeHints(s,GetTickCount64());
        Check(s.nativeRowsNeedCapture && s.nativeRowsMessageQueued && !s.rowTransitionActive,
            "drive membership hint failed to request backend identities immediately, or animated before completion");
        // One actual independent presentation owner consumes the backend model
        // and advances shared motion while the Explorer owner does not dispatch.
        if(grid && grouped && rtl && !sorted) {
            s.nativeRowsNeedCapture=false; s.nativeRowsMessageQueued=false;
            Check(s.presentationEntries && s.presentationEntries->size()==size_t(s.total),"namespace did not publish immutable names");
            tab.Start(); PresentationFixture::Await(s,[](const auto& r) {return r.accepted;}); ConsumePresentation(s);
            begin(); auto more=replacement; more.push_back(item(L"f",L"Foxtrot")); deliver(more); Paint(s);
            const auto first=PresentationFixture::Await(s,[](const auto& r) {return r.accepted && r.rows;});
            const auto frames=first->frames,sequence=s.presentationSequence;
            const auto later=tab.Await([&](const auto& r) {return r.sequence==sequence && r.frames>frames+2 && r.accepted && !r.rows;});
            Check(later->device==first->device && later->swapchain==first->swapchain && later->modelRows==4,
                "namespace diff rebuilt GPU resources or stalled without owner dispatch");
            ConsumePresentation(s);
        }
    }
    // The same grouped Details fix must work for a real filesystem scan; it
    // must not depend on This PC, a hardware notification or a manual animation.
    const auto directory=JoinPath(fixture,L"grouped-item-mutations");
    Check(CreateDirectoryW(directory.c_str(),nullptr)!=FALSE,"grouped snapshot fixture");
    std::vector<std::wstring> files;
    const auto create=[&](const wchar_t* name) {
        const auto path=JoinPath(directory,name);
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"grouped private item"); CloseHandle(file); files.push_back(path); return path;
    };
    create(L"Alpha.txt"); create(L"Charlie.txt"); create(L"Echo.txt");
    {
        PresentationFixture::Tab tab(L"grouped-filesystem-mutations",0,false,false,false,900,600);
        auto& s=tab.state; s.folderPath=directory; s.loaded=false;
        s.grouping.enabled=true; s.grouping.key=PKEY_FileName;
        const auto refresh=[&] {
            QueueDirectoryRefresh(s,true);
            auto result=WaitForComplete(*s.background,s.directoryToken);
            PublishDirectoryResult(*s.background,std::move(result)); ReceiveDirectoryResults(s);
        };
        refresh(); Check(s.total==3 && s.sections.size()==3,"initial grouped filesystem layout");
        const auto inserted=create(L"Bravo.txt"); refresh();
        Check(s.total==4 && s.rowTransitionActive && s.groupingLayoutTransition && s.disappearingRows.empty(),
            "unchanged Group by setting blocked ordinary grouped Details insertion");
        StopRowTransition(s); Check(DeleteFileW(inserted.c_str())!=FALSE,"grouped private item removal"); refresh();
        Check(s.total==3 && s.rowTransitionActive && s.disappearingRows.size()==1 && s.disappearingHeaders.size()==1,
            "grouped Details removal lost its item/header exit or recreated survivors");
    }
    for(const auto& file:files) DeleteFileW(file.c_str());
    Check(RemoveDirectoryW(directory.c_str())!=FALSE,"grouped private directory cleanup");
    ShutDownFrames(); g_frameStopping=false; g_writeFactory=factory;
    std::cout<<"PASS: backend namespace membership snapshots use ordinary insertion/removal/replacement in LTR/RTL Details/grid, grouped/ungrouped, native/explicit order; survivors, partial/empty/unchanged snapshots, selection, capacity independence, upstream drive hints and independent GPU completion\n";
}
