// Real private filesystem mutations feed the production names/grouping lane.
// Readback checks decoration away from labels/icons; no fabricated messages
// or DirectoryResults, user folders, clipboard or desktop focus are involved.
static void TestZebraSnapshots(const std::wstring& fixture, bool grouped, bool empty, bool disabled) {
    const bool oldZebra=g_zebra.exchange(true), oldEmpty=g_emptyRows.exchange(empty);
    const int oldOpacity=g_zebraOpacity.exchange(disabled?0:80);
    auto oldCaption=std::move(g_emptyFolderCaption); g_emptyFolderCaption.clear();
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"zebra fonts");
    for(bool rtl:{false,true}) {
        Regression::Scope scope(std::string("zebra grouped=")+std::to_string(grouped)+
            " empty="+std::to_string(empty)+" disabled="+std::to_string(disabled)+" rtl="+std::to_string(rtl));
        const auto directory=JoinPath(fixture,rtl?L"zebra-rtl":L"zebra-ltr");
        Check(std::filesystem::create_directory(directory),"private zebra directory");
        const auto create=[&](const std::wstring& name) {
            const auto path=JoinPath(directory,name);
            HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            Check(file!=INVALID_HANDLE_VALUE,"private zebra file"); CloseHandle(file); return path;
        };
        std::vector<std::wstring> files;
        for(int i=0;i<(grouped?7:8);++i) files.push_back(create(L"N"+std::to_wstring(i)+L".txt"));
        if(grouped) {create(L"A.txt"); create(L"Z.txt");}
        ViewState state;
        state.hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private zebra pixels",
            WS_POPUP,-32000,-32000,640,640,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"zebra window");
        state.nativeHwnd=state.frameHwnd=state.activeTabHost=state.hwnd;
        FocusFixture::Session focus(state.frameHwnd,state.nativeHwnd);
        state.background->hwnd=state.hwnd; state.folderPath=directory;
        state.visible=state.inScope=state.detailsViewActive=true;
        state.rtlLayout=rtl; state.layoutNativeMode=FVM_LIST;
        state.sort.explicitOrder=true; state.sort.kind=SortKind::Name;
        state.sort.key=PKEY_FileName; state.sort.direction=SORT_ASCENDING;
        state.grouping.enabled=grouped; state.grouping.key=PKEY_FileName;
        state.nativeTextColor=state.nativeHeaderTextColor=RGB(255,255,255);
        state.selected=state.hovered=-1;
        UpdateMetrics(state); UpdateColumns(state); UpdateRefreshRate(state);
        const auto refresh=[&] {
            QueueDirectoryRefresh(state,true);
            auto result=WaitForComplete(*state.background,state.directoryToken);
            PublishDirectoryResult(*state.background,std::move(result)); ReceiveDirectoryResults(state);
        };
        refresh(); StopRowTransition(state);
        Check(state.total==(grouped?9:8),"zebra names did not enumerate private files");
        Check(EnsureTarget(state),"zebra GPU target");
        auto swap=std::move(state.swapChain);
        AnimationClockFixture::Freeze clock;
        const auto advance=[&](double ms) {
            AnimationClockFixture::instant.QuadPart+=LONGLONG(state.qpcFrequency*ms/1000.);
        };
        if(grouped) {
            Check(state.sections.size()==3 && state.sections[1].key==L"N","real alphabetical groups");
            ToggleGroupSection(state,0); ToggleGroupSection(state,2);
            advance(kUiTransitionDurationMs+1.);
            AdvanceGroupAnimation(state,AnimationClockFixture::instant);
            Check(state.sections[0].fraction==0.f && state.sections[2].fraction==0.f,"native group intent did not settle");
        }
        const auto pixels=[&] {
            Paint(state);
            ComPtr<ID2D1Bitmap1> cpu;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                state.backbuffer->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(state.backbuffer->GetPixelSize(),nullptr,0,&props,&cpu)) &&
                SUCCEEDED(cpu->CopyFromBitmap(nullptr,state.backbuffer.Get(),nullptr)),"zebra readback");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"zebra map");
            std::vector<int> alpha(cpu->GetPixelSize().height);
            const int x=rtl?240:400;
            for(size_t y=0;y<alpha.size();++y) alpha[y]=map.bits[y*map.pitch+x*4+3];
            cpu->Unmap(); return alpha;
        };
        const auto noTail=[&](const auto& alpha) {
            if(!grouped) return;
            const int end=int(std::ceil(state.headerPx+state.sections.back().documentEnd-state.scrollNow));
            for(int y=std::max(0,end+2);y<int(alpha.size())-2;++y)
                Check(alpha[y]==0,"stripe escaped below collapsed Z");
        };
        auto initial=pixels(); noTail(initial);
        const int firstStripe=int(state.headerPx+(grouped?state.sections[1].documentTop+GroupHeadingHeight(state):state.rowPx)+state.rowPx*.5f);
        Check(initial[firstStripe]==(disabled?0:102),"independent 80 percent stripe alpha or zero-opacity bypass");
        if(disabled) Check(!state.zebraSnapshotInitialized && state.zebraBands.empty() &&
            state.zebraTargetBands.empty() && !state.zebraExtentAnimating,"zero opacity captured or animated stripes");
        const auto remove=[&](int i) {
            Check(DeleteFileW(files[i].c_str()),"real zebra deletion"); refresh(); StopRowTransition(state);
        };
        remove(0);
        auto start=pixels(); noTail(start);
        if(!disabled && (grouped || !empty)) {
            Check(state.zebraExtentAnimating,"changed stripe snapshot did not start independently of item motion");
            advance(kUiTransitionDurationMs*.25);
            auto middle=pixels(); noTail(middle);
            if(!grouped) {
                Check(middle[firstStripe]==initial[firstStripe],"trailing deletion moved an unchanged stripe");
                const int oldBottom=int(state.headerPx+state.rowPx*7.75f);
                Check(initial[oldBottom]>0,"trailing sample missed the interior of its original stripe");
                Check(middle[oldBottom]==0,"removed stripe did not close its gap");
                remove(1); remove(2);
                const auto retarget=pixels();
                Check(retarget==middle,"interrupted snapshot jumped from its last submitted pixels");
            }
            advance(kUiTransitionDurationMs+1.); pixels();
            Check(!state.zebraExtentAnimating,"snapshot did not settle after accepted submission");
        }
        // Delete the remaining N files, including the sole striped item from
        // the reported case. Collapsed A/Z remain real enumerated sections.
        for(int i=1;i<int(files.size());++i) {
            if(!grouped && !empty && !disabled && i<=2) continue;
            remove(i); const auto changed=pixels(); noTail(changed);
            if(!grouped && empty) Check(changed==initial && !state.zebraExtentAnimating,
                "empty-row stripes moved or animated during a real deletion");
            advance(kUiTransitionDurationMs*.25); noTail(pixels());
            advance(kUiTransitionDurationMs+1.); noTail(pixels());
        }
        if(grouped) Check(state.sections.size()==2 && state.sections.back().key==L"Z" &&
            state.sections.back().fraction==0.f,"deletion lost collapsed group state");
        if(disabled) Check(state.zebraBands.empty() && state.zebraFromBands.empty() &&
            state.zebraTargetBands.empty() && !state.zebraExtentAnimating,"zero opacity performed snapshot work after changes");
        // Restore real content, and expand the final section to exercise the
        // optional continuation into empty viewport space.
        create(L"N-return.txt"); refresh(); StopRowTransition(state); pixels();
        advance(kUiTransitionDurationMs+1.); pixels();
        if(grouped && !disabled) {
            ToggleGroupSection(state,int(state.sections.size())-1);
            advance(kUiTransitionDurationMs+1.); AdvanceGroupAnimation(state,AnimationClockFixture::instant);
            pixels(); // Submit the start of the independently deferred stripe growth.
            advance(kUiTransitionDurationMs+1.);
            const auto expanded=pixels();
            const int y=int(state.headerPx+state.sections.back().documentEnd+state.rowPx*.5f);
            Check(expanded[y]==(empty?102:0),"final expanded section ignored empty-stripe setting");
        }
        UnregisterAnimationWindow(state.hwnd); Stop(state.background);
        DropTarget(state); swap.Reset(); DestroyWindow(state.hwnd);
    }
    g_emptyFolderCaption=std::move(oldCaption);
    g_writeFactory.Reset(); g_zebra=oldZebra; g_emptyRows=oldEmpty; g_zebraOpacity=oldOpacity;
    std::cout<<"PASS: real filesystem zebra snapshots, grouped boundaries, collapse, interruption and disabled decoration\n";
}
