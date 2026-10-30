// Actual per-tab GPU owners, private windows/models and substituted focus.
// The host deliberately does not dispatch messages while lanes render.
#ifdef PRESENTATION_MODEL_FIELDS
namespace PresentationFixture {
template<class Predicate>
std::shared_ptr<const PresentationReceipt> Await(ViewState& state,Predicate predicate) {
    Check(bool(state.presenter),"missing presentation endpoint");
    state.presenter->diagnostics=true;
    std::shared_ptr<const PresentationReceipt> result;
    const bool acknowledged=Eventually([&] {
        {std::lock_guard lock(state.presenter->mutex); result=state.presenter->receipt;}
        return result && result->sequence==state.presentationSequence && predicate(*result);
    });
    if(!acknowledged) std::cout<<"PRESENTATION revision timeout expected="<<state.presentationSequence
        <<" receipt="<<bool(result)<<" sequence="<<(result ? result->sequence : 0)
        <<" ready="<<(result && result->ready)<<" accepted="<<(result && result->accepted)
        <<" scene="<<(result && result->scenePublished)<<" columns="<<(result && result->columns)
        <<" rows="<<(result && result->rows)<<" frames="<<(result ? result->frames : 0)
        <<" failure="<<std::hex<<(result ? result->failure : S_OK)<<std::dec<<std::endl;
    Check(acknowledged,"presentation owner did not acknowledge the current revision");
    return result;
}
inline HWND held=nullptr;
inline HANDLE entered=nullptr,released=nullptr,exited=nullptr;
inline void Probe(LPTHREAD_START_ROUTINE procedure,LPVOID context) {
    if(procedure!=TabWorkerEntry || !context) return;
    auto& worker=*static_cast<TabBackground::Worker*>(context);
    if(worker.lane==TabBackground::Lane::Presentation && worker.owner->hwnd==held) {
        SetEvent(entered); WaitForSingleObject(released,INFINITE); SetEvent(exited);
    }
}
struct Hold {
    Hold(HWND target) {
        held=target; entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        released=CreateEventW(nullptr,TRUE,FALSE,nullptr); exited=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        AuditFixture::threadProbe=Probe;
    }
    void Release() {SetEvent(released);}
    ~Hold() {
        AuditFixture::threadProbe=nullptr; SetEvent(released);
        const bool waiting=WaitForSingleObject(entered,0)==WAIT_OBJECT_0;
        if(!waiting || WaitForSingleObject(exited,5000)==WAIT_OBJECT_0) {
            CloseHandle(entered); CloseHandle(released); CloseHandle(exited);
        }
        held=nullptr;
    }
};
struct Tab {
    ViewState state;
    HWND frame=nullptr;
    Tab(const wchar_t* label,int count,bool rtl=false,bool grid=false,bool grouped=false,int width=800,int height=480,
        const wchar_t* frameClass=L"STATIC") {
        frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,frameClass,L"private presentation frame",
            WS_POPUP|WS_VISIBLE,-32000,-32000,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        state.hwnd=CreateWindowExW(0,L"STATIC",label,WS_CHILD|WS_VISIBLE,0,0,width,height,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(frame && state.hwnd,"presentation fixture windows");
        state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
        state.background->hwnd=state.hwnd; state.folderPath=std::wstring(L"presentation-fixture:")+label;
        state.visible=state.inScope=state.loaded=state.detailsViewActive=true;
        state.firstContentSubmittedAt=1; state.deferColumnProviders=false; state.featureInitializationStep=kOverlayFeatureSteps;
        state.rtlLayout=rtl; state.total=count; state.entries.resize(size_t(count)); state.selectedFlags.assign(size_t(count),1);
        state.nativeTextColor=state.nativeHeaderTextColor=RGB(255,255,255);
        state.clipboardInitialized=true;
        for(int i=0;i<count;++i) {
            auto& entry=state.entries[size_t(i)]; entry.name=L"Name "+std::to_wstring(i);
            entry.path=state.folderPath+L":"+std::to_wstring(i); entry.folder=true;
            entry.groupKey=std::to_wstring(i/20); entry.groupTitle=L"Group "+entry.groupKey;
        }
        ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=450; state.columns={name};
        ApplyViewLayout(state,{UINT(grid ? FVM_ICON : FVM_DETAILS),48},false);
        state.grouping.enabled=grouped;
        UpdateMetrics(state); RebuildGroupSections(state); UpdateColumns(state); UpdateRefreshRate(state);
        state.presentationEntries=std::make_shared<const std::vector<Entry>>(state.entries);
        state.presentationModelFolder=state.folderPath;
        state.presentationOrderHash=state.displayedOrderHash;
        state.presentationModelGrouping=state.displayedGrouping;
        auto& icon=state.defaultFolderIcon; icon.pixels.width=icon.pixels.height=32; icon.pixels.stride=128;
        icon.pixels.data.assign(4096,255); icon.requestedSize=32;
    }
    void Start() {Check(StartPresentation(state),"presentation lane startup"); state.presenter->diagnostics=true; SubmitPresentation(state);}
    std::shared_ptr<const PresentationReceipt> Receipt() {
        std::lock_guard lock(state.presenter->mutex); return state.presenter->receipt;
    }
    template<class Predicate> std::shared_ptr<const PresentationReceipt> Await(Predicate predicate) {
        std::shared_ptr<const PresentationReceipt> result;
        const bool done=Eventually([&] {result=Receipt(); return result && predicate(*result);});
        if(!done) std::cout<<"PRESENTATION timeout expected="<<state.presentationSequence<<" receipt="<<bool(result)
            <<" seq="<<(result ? result->sequence : 0)<<" ready="<<(result && result->ready)
            <<" accepted="<<(result && result->accepted)<<" rows="<<(result && result->rows)
            <<" groups="<<(result && result->groups)
            <<" columns="<<(result && result->columns)<<" scene="<<(result && result->scenePublished)
            <<" standby="<<(result && result->standby)<<" frames="<<(result ? result->frames : 0)
            <<" stop="<<state.background->stopping.load()<<std::endl;
        Check(done,"presentation receipt without owner dispatch");
        return result;
    }
    ~Tab() {
        if(state.presenter) {
            ReleasePresentation(state,true); state.background->Stop();
            WaitForSingleObject(state.background->presentation.thread,10000);
        }
        UnregisterAnimationWindow(state.hwnd); KillTimer(state.hwnd,kTimerId);
        DestroyWindow(state.hwnd); DestroyWindow(frame); Stop(state.background);
    }
};

void Mutation(Tab& tab,bool insert) {
    auto& s=tab.state;
    auto headings=CaptureVisibleGroupHeaders(s);
    std::vector<CapturedRow> rows;
    for(int i:VisibleItemRows(s,kRowTransitionOverscan)) {
        const auto visual=LayoutRowVisual(s,i,CurrentRowProgress(s));
        CapturedRow capture; capture.oldIndex=i; capture.newIndex=insert ? i+1 : i==0 ? -1 : i-1;
        capture.entry=s.entries[size_t(i)]; capture.selected=ModelSelected(s,i);
        capture.oldTop=visual.top; capture.oldHeight=visual.height; capture.oldLeft=visual.left;
        capture.oldWidth=visual.width; capture.oldOpacity=visual.opacity; rows.push_back(std::move(capture));
    }
    const int total=s.total; const float row=s.rowPx;
    if(insert) {
        auto entry=s.entries.front(); entry.name=L"Restored"; entry.path+=L":restored";
        s.entries.insert(s.entries.begin(),std::move(entry)); ++s.total;
    } else {s.entries.erase(s.entries.begin()); --s.total;}
    s.selectedFlags.assign(s.total,1); RebuildGroupSections(s);
    s.presentationEntries=std::make_shared<const std::vector<Entry>>(s.entries);
    s.presentationModelGrouping=s.displayedGrouping;
    if(s.gridView || s.grouping.enabled) StartGroupingTransition(s,std::move(rows),std::move(headings),row,false);
    else StartRowTransition(s,std::move(rows),total,row,s.scrollNow);
    Check(s.rowTransitionActive,"presentation mutation did not start shared motion");
}

void GroupAnimationClock(bool rtl,bool grid) {
    Tab bridge(L"group input clock",80,rtl,grid,true);
    Tab renderer(L"group rendering clock",80,rtl,grid,true);
    auto& input=bridge.state; auto& output=renderer.state;
    auto lane=std::make_shared<PresentationLane>(); lane->lifetime=input.lifetimeToken;
    lane->hwnd=input.hwnd; input.presenter=lane;
    // Fixed clock samples reproduce the last subpixel frames and arbitrarily
    // delayed paint/receipt delivery without depending on sleeps or GPU speed.
    input.qpcFrequency=output.qpcFrequency=1000000.;
    output.graphicsPaintSerial=output.lastAcceptedPaint=1;
    output.token=input.token; output.directoryToken=input.directoryToken;
    const float expanded=DocumentHeight(input);
    const float body=input.rowPx*ItemLineCount(input,input.sections.back().end-input.sections.back().first)+GroupHeadingHeight(input);
    for(bool collapse:{true,false}) {
        auto& group=input.sections.back();
        group.collapsed=collapse; group.from=group.fraction; group.to=collapse ? 0.f : 1.f;
        group.started.QuadPart=1000000; input.groupAnimating=true;
        PublishGroupIntents(input);
        const auto before=group.fraction;
        // A receipt from the previous intent must not finish a reversed gesture.
        if(!collapse) {
            PublishPresentationReceipt(output,lane); ConsumePresentation(input);
            Check(input.groupAnimating && group.started.QuadPart && group.fraction==before,
                "stale group receipt completed a newer intent");
        }
        output.sections=input.sections; output.groupAnimating=true;
        output.presentationGroupIntents=input.presentationGroupIntents;
        const float target=expanded-(collapse ? body : 0.f);
        for(double progress:{.975,.999,1.,4.}) {
            LARGE_INTEGER sample{};
            sample.QuadPart=1000000+LONGLONG(kUiTransitionDurationMs*1000*progress);
            AdvanceGroupAnimation(output,sample); LayoutGroupSections(output);
            PublishPresentationReceipt(output,lane);
            const auto receipt=bridge.Receipt();
            Check(receipt && receipt->accepted && receipt->groups==(progress<1.),
                "group completion used rounded easing instead of elapsed duration");
            if(progress==.975) Check(fabsf(receipt->documentHeight-target)<.02f && receipt->groups,
                "near-terminal frame did not reproduce the old height-only wait race");
            ConsumePresentation(input);
            Check(input.groupAnimating==output.groupAnimating &&
                input.sections.back().started.QuadPart==output.sections.back().started.QuadPart &&
                input.sections.back().fraction==output.sections.back().fraction &&
                DocumentHeight(input)==DocumentHeight(output),
                "delayed group receipt changed the rendered input geometry");
            if(progress>=1.) Check(!input.groupAnimating && DocumentHeight(input)==target,
                "completed group receipt lost final native/input geometry");
        }
    }
    input.presenter.reset();
}
}

static void TestPresentationOwners() {
    {
        using WaitForClock=DWORD(WINAPI*)(UINT,const HANDLE*,DWORD);
        const auto clock=reinterpret_cast<WaitForClock>(::GetProcAddress(
            GetModuleHandleW(L"dcomp.dll"),"DCompositionWaitForCompositorClock"));
        if(clock) {
            HANDLE control=CreateEventW(nullptr,FALSE,TRUE,nullptr);
            Check(control!=nullptr,"native compositor control event");
            const auto interrupted=clock(1,&control,5000);
            const auto tick=clock(1,&control,5000);
            CloseHandle(control);
            Check(interrupted==WAIT_OBJECT_0 && tick==WAIT_OBJECT_0+1,
                "native compositor clock did not distinguish control wake from heartbeat");
        }
    }
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"presentation fonts");
    const auto before=g_gpuDevices.load();
    const bool smooth=g_smooth.exchange(true),automatic=g_autoRefitColumns.exchange(false);
    g_frameStopping=false;
    for(bool rtl:{false,true}) for(bool grid:{false,true}) PresentationFixture::GroupAnimationClock(rtl,grid);
    {
        Check(!g_frameThread && !g_frameEvent,"frame-stop fixture requires an idle scheduler");
        g_frameEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        HWND window=MessageWindow();
        AuditFixture::framePulseProbe=+[] {g_frameStopping=true; SetEvent(g_frameEvent);};
        RegisterAnimationWindow(window);
        const bool stopped=WaitForSingleObject(g_frameThread,5000)==WAIT_OBJECT_0;
        // Rescue a failing historical loop so the assertion can report instead
        // of leaving its catch/teardown stuck on that same wait.
        if(!stopped) SetEvent(g_frameEvent);
        ShutDownFrames(); g_frameStopping=false; DestroyWindow(window);
        Check(stopped,"frame shutdown consumed its stop event and waited forever");
    }
    {
        PresentationFixture::Tab a(L"A",30000,true),b(L"B",300,false,true);
        FocusFixture::Session focus(a.frame,a.state.hwnd);
        PresentationFixture::Hold hold(a.state.hwnd);
        a.Start(); Check(WaitForSingleObject(PresentationFixture::entered,5000)==WAIT_OBJECT_0,"held renderer did not enter");
        for(int i=0;i<60;++i) {a.state.hovered=i%12; SubmitPresentation(a.state);}
        std::shared_ptr<const PresentationModel> queued;
        {std::lock_guard lock(a.state.presenter->mutex); queued=a.state.presenter->pending;}
        Check(queued && queued->sequence==a.state.presentationSequence && queued->directory==a.state.presentationEntries &&
            queued->items.size()<4096,"render mailbox retained obsolete frames or copied the directory per frame");
        const auto pixelIdentity=a.state.defaultFolderIcon.pixels.data.Identity();
        Check(!queued->images.empty() && queued->images.back().pixels.data.Identity()==pixelIdentity,
            "frame copied thumbnail bytes instead of sharing immutable pixels");
        a.state.selectedFlags[0]=0; a.state.defaultFolderIcon.pixels.data[0]=0;
        a.state.clipboardPaths.insert(a.state.entries[0].path);
        Check(queued->selectedFlags[0] && queued->images.back().pixels.data[0]==255 && queued->clipboard.empty(),
            "selection, clipboard or pixel mutation changed an immutable pending frame");
        a.state.entries[0].name=L"Fresh visible metadata"; a.state.clipboardPaths.clear();
        SubmitPresentation(a.state); const auto latest=a.state.presentationSequence;
        b.Start();
        const auto readyB=b.Await([](const auto& r) {return r.ready && r.accepted && r.glyphBytes;});
        Check(readyB->thread!=GetCurrentThreadId() && !a.Receipt(),"a held tab serialized another tab's rendering");
        hold.Release();
        const auto readyA=a.Await([&](const auto& r) {return r.ready && r.accepted && r.sequence==latest && r.glyphBytes;});
        Check(readyA->thread!=readyB->thread && readyA->thread!=GetCurrentThreadId() &&
            !a.state.d3d && !a.state.target && !a.state.swapChain && readyA->device && readyA->swapchain,
            "GPU resources or submission still belong to Explorer's owner thread");
        Check(readyA->modelRows==30000 && readyA->overrides<1000,"large model was expanded into each presentation frame");
        const auto first=std::find_if(readyA->samples.begin(),readyA->samples.end(),[](const auto& item) {return item.index==0;});
        Check(first!=readyA->samples.end() && !first->selected && first->name==SelectionPathHash(L"Fresh visible metadata"),
            "presented model mixed names and selection from different revisions");
        ConsumePresentation(a.state);
        {
            const auto old=CaptureColumnLayout(a.state);
            a.state.columns.front().width=500;
            ExplorerColumn date; date.key=PKEY_DateModified; date.title=L"Modified"; date.width=180;
            a.state.columns.push_back(date); UpdateColumns(a.state); StartColumnTransition(a.state,old);
            SubmitPresentation(a.state);
            const auto moving=a.Await([](const auto& r) {return r.accepted && r.columns && r.scenePublished;});
            ConsumePresentation(a.state);
            Check(a.state.columnTransitionStartedQpc.QuadPart==moving->columnClock.QuadPart &&
                a.state.presentationColumnOrigin.QuadPart==moving->columnOrigin.QuadPart,
                "bridge confused column intent with its committed animation clock");
            SubmitPresentation(a.state);
            a.Await([](const auto& r) {return r.accepted && !r.columns && !r.scenePublished;});
            ConsumePresentation(a.state);
        }
        const auto frames=readyA->frames;
        MoveScroll(a.state,std::min(MaxScrollPx(a.state),a.state.rowPx*12));
        const float from=a.state.scrollNow,target=a.state.scrollTarget;
        SubmitPresentation(a.state);
        // Await polls only the immutable receipt. It does not dispatch an
        // Explorer/overlay message, advance owner clocks or invoke Paint.
        const auto scrolled=a.Await([&](const auto& r) {return r.accepted && r.frames>frames+1 && fabsf(r.scroll-target)<.01f;});
        Check(a.state.scrollNow==from && scrolled->scroll>from,"scrolling waited for owner-thread animation advancement");
        ConsumePresentation(a.state);
        Check(fabsf(a.state.scrollNow-target)<.01f,"input bridge did not synchronize to the presented scroll position");
        const auto stale=std::make_shared<PresentationReceipt>(*scrolled);
        stale->token=a.state.token+100; stale->scroll=0;
        {std::lock_guard lock(a.state.presenter->mutex); a.state.presenter->receipt=stale;}
        ConsumePresentation(a.state);
        Check(a.state.scrollNow==target,"a stale presentation receipt rewound the current tab");

        const auto zoomFrom=b.state.rowPx;
        ApplyRowHeight(b.state,70.); SubmitPresentation(b.state);
        const auto zoomed=b.Await([&](const auto& r) {return r.accepted && r.rowHeight>zoomFrom+1 && r.frames>readyB->frames+1;});
        Check(b.state.rowPx==zoomFrom && zoomed->rowHeight>zoomFrom,"grid zoom still advances on Explorer's thread");
        b.Await([](const auto& r) {return r.accepted && !r.zoom && !r.rows;}); ConsumePresentation(b.state);
        Check(!b.state.zoomAnimating && b.state.rowPx>zoomFrom &&
            RowFromPoint(b.state,int(b.state.headerPx+10),int(b.state.rtlLayout ? 780 : 20))>=0,
            "completed zoom did not synchronize input geometry");
        // The last accepted image/device survives a real hide and resume.
        const auto device=scrolled->device,swapchain=scrolled->swapchain;
        ++a.state.token; ++a.state.directoryToken; // A view changed before its next frame.
        ShowWindow(a.state.hwnd,SW_HIDE); SubmitPresentation(a.state);
        ++a.state.directoryToken; // Names revalidation begins after the hide packet.
        a.Await([&](const auto& r) {return r.ready && r.standby && r.control && r.sequence==a.state.presentationSequence;});
        ConsumePresentation(a.state);
        Check(a.state.graphicsStandby,"names revalidation discarded visibility completion");
        ShowWindow(a.state.hwnd,SW_SHOWNOACTIVATE); SubmitPresentation(a.state);
        const auto resumed=a.Await([&](const auto& r) {return r.accepted && r.sequence==a.state.presentationSequence && !r.standby;});
        Check(resumed->device==device && resumed->swapchain==swapchain,"tab resume recreated its warm presentation resources");
        Check(g_gpuDevices.load()==before+2,"presentation duplicated per-tab GPU devices");
    }
    Check(Eventually([&] {return g_gpuDevices.load()==before;}),"presentation GPU resources outlived their lane");
    {
        PresentationFixture::Tab tab(L"cold hidden",40);
        PresentationFixture::Hold hold(tab.state.hwnd);
        tab.Start(); Check(WaitForSingleObject(PresentationFixture::entered,5000)==WAIT_OBJECT_0,"cold hide renderer gate");
        ShowWindow(tab.state.hwnd,SW_HIDE); SubmitPresentation(tab.state); hold.Release();
        tab.Await([&](const auto& r) {return r.sequence==tab.state.presentationSequence && !r.ready;});
        ConsumePresentation(tab.state);
        Check(tab.state.graphicsReady && !tab.state.graphicsSuspended,"an unstarted hidden image revoked GPU startup");
        ShowWindow(tab.state.hwnd,SW_SHOWNOACTIVATE); SubmitPresentation(tab.state);
        tab.Await([](const auto& r) {return r.ready && r.accepted;});
    }
    for(bool rtl:{false,true}) for(bool grid:{false,true}) for(bool grouped:{false,true}) {
        PresentationFixture::Tab tab(L"motion",80,rtl,grid,grouped);
        FocusFixture::Session focus(tab.frame,tab.state.hwnd);
        tab.Start(); tab.Await([](const auto& r) {return r.ready && r.accepted && r.glyphBytes;});
        for(bool insert:{false,true}) {
            PresentationFixture::Mutation(tab,insert);
            if(!grid) {
                const auto old=CaptureColumnLayout(tab.state);
                tab.state.columns.front().width=insert ? 1100 : 950;
                UpdateColumns(tab.state); StartColumnTransition(tab.state,old);
            }
            const auto prior=tab.Receipt();
            const auto previous=prior ? prior->frames : 0;
            MoveScroll(tab.state,std::min(MaxScrollPx(tab.state),tab.state.scrollNow+8));
            const float target=tab.state.scrollTarget;
            SubmitPresentation(tab.state);
            const auto sequence=tab.state.presentationSequence;
            tab.Await([&](const auto& r) {return r.accepted && r.sequence==sequence && r.frames>previous+1 && !r.rows && !r.columns && fabsf(r.scroll-target)<.01f;});
            Check(tab.state.rowTransitionActive,"test accidentally pumped the native model");
            ConsumePresentation(tab.state);
            Check(!tab.state.rowTransitionActive && !tab.state.columnTransitionActive &&
                !tab.state.animationActive,"completed independent motion kept the bridge's frame loop alive");
        }
        if(grouped) {
            // Change a section outside the current viewport. Its geometry must
            // be retained when packets carry only viewport enrichment.
            auto& s=tab.state; const int index=int(s.sections.size())-1;
            auto& section=s.sections[size_t(index)];
            for(int i=section.first;i<section.end;++i) s.selectedFlags[size_t(i)]=0;
            s.selected=-1;
            const float expected=DocumentHeight(s)-s.rowPx*ItemLineCount(s,section.end-section.first)-GroupHeadingHeight(s);
            ToggleGroupSection(s,index); SubmitPresentation(s);
            const auto sequence=s.presentationSequence;
            tab.Await([&](const auto& r) {return r.accepted && r.sequence==sequence &&
                r.groupIntents==s.presentationGroupIntents && !r.groups && fabsf(r.documentHeight-expected)<.02f;});
            ConsumePresentation(s);
            Check(!s.groupAnimating && fabsf(DocumentHeight(s)-expected)<.02f,"offscreen group animation lost native/input geometry");
        }
    }
    {
        std::vector<std::unique_ptr<PresentationFixture::Tab>> tabs;
        ComPtr<IDXGIFactory1> factory; ComPtr<IDXGIAdapter1> basic; ComPtr<IDXGIAdapter3> adapter;
        if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapters1(0,&basic))) basic.As(&adapter);
        UINT64 peak=0;
        for(int i=0;i<20;++i) {
            auto tab=std::make_unique<PresentationFixture::Tab>((L"warm"+std::to_wstring(i)).c_str(),100,false,true,false,3840,2160);
            tab->Start(); tab->Await([](const auto& r) {return r.ready && r.accepted && r.glyphBytes;});
            ShowWindow(tab->state.hwnd,SW_HIDE); SubmitPresentation(tab->state);
            tab->Await([](const auto& r) {return r.ready && r.standby;});
            tabs.push_back(std::move(tab));
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if(adapter && SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))) peak=std::max(peak,info.CurrentUsage);
        }
        Check(g_gpuDevices.load()==before+20,"worker standby discarded warm tab devices");
        Check(!adapter || peak<=2688ULL*1024*1024,"twenty presentation owners retained oversized GPU allocations");
        for(auto& tab:tabs) Check(Eventually([&] {return !tab->state.presenter->animating.load();}),"hidden presentation lane did not become idle");
        auto& tab=*tabs.front(); const auto warm=tab.Receipt();
        for(int i=0;i<3;++i) {
            ShowWindow(tab.state.hwnd,SW_SHOWNOACTIVATE); SubmitPresentation(tab.state);
            const auto resumed=tab.Await([](const auto& r) {return r.accepted && !r.standby;});
            Check(resumed->device==warm->device && resumed->swapchain==warm->swapchain && resumed->glyphBytes,
                "repeated tab activation reloaded its image or glyph resources");
            ShowWindow(tab.state.hwnd,SW_HIDE); SubmitPresentation(tab.state);
            tab.Await([](const auto& r) {return r.ready && r.standby;});
        }
        tabs.clear(); Check(Eventually([&] {return g_gpuDevices.load()==before;}),"closed presentation tabs retained devices");
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if(adapter) adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info);
        Check(!adapter || info.CurrentUsage<=256ULL*1024*1024,"closed presentation tabs retained GPU allocations");
        std::cout<<"PRESENTATION MEMORY tabs=20 4K peak-local-MiB="<<peak/1048576.<<" closed-local-MiB="<<info.CurrentUsage/1048576.<<std::endl;
    }
    ShutDownFrames(); g_frameStopping=false;
    g_smooth=smooth; g_autoRefitColumns=automatic;
    g_writeFactory.Reset();
    std::cout<<"PASS: independent tab-owned GPU lanes; blocked startup/coalescing, immutable large names/selection/images/clipboard, revision-consistent content, unpumped-owner scroll and grid zoom, stale receipts, warm hide/resume and drained teardown\n";
}
#else
static void TestPresentationOwners() {Check(false,"presentation still runs on Explorer's owner thread");}
#endif
