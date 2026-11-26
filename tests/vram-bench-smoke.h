// Focused VRAM benchmark: one 3840x2160 Explorer-like frame with ten tab hosts
// (ShellTabWindowClass), each production-attached (AttachView, OverlayProc,
// NativeHostSubclass, presentation lane) over the private 512-item fixture.
// Each tab is used the way a heavy session would use it while selected: pane
// toggles, splitter steps and a frame-edge drag; then many tab switches, done
// as Explorer does them by showing one tab host and hiding the previous one.
// Reports the swap-chain bytes the mod retains (from the displayed chains' DXGI
// descriptions), the process's local video memory, and every chain replacement
// while its tab was selected (each one is a visible flicker).
// Component measurement over private windows, not live Explorer.
static void TestVramTabs4k(const std::wstring& directory) {
    Check(SUCCEEDED(OleInitialize(nullptr)) && PrepareTextRuntime(),"vram bench runtime");
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    for(const auto* name:{L"CabinetWClass",L"ShellTabWindowClass",L"CtrlNotifySink",L"SHELLDLL_DefView"}) {
        cls.lpszClassName=name; RegisterClassW(&cls);
    }
    cls.lpfnWndProc=OverlayProc; cls.lpszClassName=kOverlayClass; RegisterClassW(&cls);
    constexpr int kTabs=10, kWidth=3840, kHeight=2160, kLeft=420, kTop=240, kBottom=40;
    ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter1> adapter1; ComPtr<IDXGIAdapter3> adapter;
    if(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) && SUCCEEDED(factory->EnumAdapters1(0,&adapter1))) adapter1.As(&adapter);
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CabinetWClass",L"vram bench frame",
        WS_POPUP|WS_VISIBLE,-32000,-32000,kWidth,kHeight,nullptr,nullptr,cls.hInstance,nullptr);
    Check(frame!=nullptr,"vram bench frame");
    FocusFixture::Session focus(frame,nullptr);
    struct Tab {
        HWND tabHost=nullptr,host=nullptr,native=nullptr,overlay=nullptr;
        ViewState* state=nullptr;
        ComPtr<ColumnBridgeFixture::FolderView> response; ComPtr<IFolderView2> view; ComPtr<IShellBrowser> browser;
        std::unique_ptr<ColumnBridgeFixture::Receipts> receipts;
        std::shared_ptr<TabBackground> work;
        ULONG_PTR chain=0;
        bool selected=false;
    };
    std::vector<Tab> tabs(kTabs);
    unsigned visibleReplacements=0, offscreenReplacements=0;
    double switchMs=0; // Longest tab switch until the selected tab showed its image (includes parking the previous one).
    const auto latest=[](Tab& tab) {
        auto& r=tab.receipts->latest;
        {std::lock_guard lock(tab.state->presenter->mutex); if(tab.state->presenter->receipt) r=tab.state->presenter->receipt;}
        return r;
    };
    // Wait until the lane has shown (selected) or parked (unselected) the current model.
    const auto settle=[&](Tab& tab,bool selected,const char* phase) {
        std::shared_ptr<const PresentationReceipt> r;
        const bool done=Eventually([&] {
            ColumnBridgeFixture::Pump(); r=latest(tab);
            return r && r->sequence>=tab.state->presentationSequence && (selected ?
                r->swapchain && r->presentedSwapchain==r->swapchain && r->accepted && !r->standby && ::IsWindowVisible(tab.overlay) :
                r->standby);
        },30000);
        if(!done) std::cout<<"VRAM BENCH "<<phase<<" seq="<<(r ? r->sequence : 0)<<"/"<<tab.state->presentationSequence
            <<" accepted="<<(r && r->accepted)<<" standby="<<(r && r->standby)
            <<" inactive="<<InactiveGraphics(*tab.state)<<" owns="<<TabOwnsActiveView(*tab.state)
            <<" window="<<::IsWindowVisible(tab.overlay)<<std::endl;
        if(!done) {
            unsigned long long pending=0; bool posted=false;
            {std::lock_guard lock(tab.state->presenter->mutex); if(tab.state->presenter->pending) pending=tab.state->presenter->pending->sequence; posted=tab.state->presenter->receiptPosted;}
            std::cout<<"VRAM BENCH lane pending-model="<<pending<<" receipt-posted="<<posted
                <<" animating="<<tab.state->presenter->animating.load()<<" stopping="<<tab.state->presenter->stopping.load()
                <<" thread-alive="<<(tab.work->presentation.thread && WaitForSingleObject(tab.work->presentation.thread,0)==WAIT_TIMEOUT)
                <<" frames="<<(r ? r->frames : 0)<<" content-pending="<<(r && r->contentPending)<<" failed="<<(r && r->failed)
                <<" failure=0x"<<std::hex<<(r ? r->failure : S_OK)<<std::dec<<" columns="<<(r && r->columns)<<" rows="<<(r && r->rows)
                <<" owner-update="<<GetUpdateRect(tab.overlay,nullptr,FALSE)<<" owner-active="<<tab.state->presentationActive<<std::endl;
        }
        Check(done,phase);
        // Only a chain swapped under a tab that stayed on screen is visible.
        if(tab.chain && r->swapchain && r->swapchain!=tab.chain) ++(selected && tab.selected ? visibleReplacements : offscreenReplacements);
        if(r->swapchain) tab.chain=r->swapchain;
        tab.selected=selected;
    };
    const auto chainBytes=[](ULONG_PTR chain) -> unsigned long long {
        DXGI_SWAP_CHAIN_DESC1 d{};
        if(!chain || FAILED(reinterpret_cast<IDXGISwapChain1*>(chain)->GetDesc1(&d))) return 0;
        return 4ull*d.Width*d.Height*d.BufferCount;
    };
    const auto report=[&](const char* phase) {
        unsigned long long retained=0;
        std::string perTab;
        for(auto& tab:tabs) if(tab.state) {
            const auto r=latest(tab);
            if(!r) continue;
            retained+=chainBytes(r->swapchain);
            perTab+=std::string(r->swapchain ? "C" : "-")+(r->standby ? "s" : "a")+(::IsWindowVisible(tab.overlay) ? "v" : "h")+" ";
            if(r->presentedSwapchain && r->presentedSwapchain!=r->swapchain) retained+=chainBytes(r->presentedSwapchain);
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if(adapter) adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info);
        std::cout<<"VRAM BENCH phase="<<phase<<" chain-MiB="<<retained/1048576.
            <<" process-local-MiB="<<info.CurrentUsage/1048576.
            <<" visible-replacements="<<visibleReplacements<<" offscreen-replacements="<<offscreenReplacements
            <<" max-switch-ms="<<switchMs<<" tabs=["<<perTab<<"]"<<std::endl;
    };
    const auto resizeHost=[](Tab& tab,int width,int height) {
        SetWindowPos(tab.host,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SetWindowPos(tab.native,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    };
    const int fullWidth=kWidth-kLeft, fullHeight=kHeight-kTop-kBottom;
    // A heavy session on the selected tab: preview pane on/off, splitter
    // sweep, then a frame-edge drag (Explorer resizes the selected tab host).
    const auto use=[&](Tab& tab) {
        for(const int width:{fullWidth-1100,fullWidth,fullWidth-900,fullWidth-1300,fullWidth-700,fullWidth}) {
            resizeHost(tab,width,fullHeight); settle(tab,true,"pane/splitter step");
        }
        SendMessageW(frame,WM_ENTERSIZEMOVE,0,0);
        for(const int shrink:{40,160,320,200,0}) {
            SetWindowPos(frame,nullptr,0,0,kWidth-shrink,kHeight-shrink/2,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            SetWindowPos(tab.tabHost,nullptr,0,0,kWidth-shrink,kHeight-shrink/2,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            resizeHost(tab,fullWidth-shrink,fullHeight-shrink/2); settle(tab,true,"edge drag step");
        }
        SendMessageW(frame,WM_EXITSIZEMOVE,0,0);
        settle(tab,true,"edge drag end");
    };
    const auto select=[&](Tab* from,Tab& to) {
        LARGE_INTEGER begin{},end{},frequency{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
        ShowWindow(to.tabHost,SW_SHOWNOACTIVATE);
        if(from) ShowWindow(from->tabHost,SW_HIDE);
        if(from) settle(*from,false,"previous tab did not park");
        settle(to,true,"selected tab was not shown");
        QueryPerformanceCounter(&end);
        switchMs=std::max(switchMs,double(end.QuadPart-begin.QuadPart)*1000./double(frequency.QuadPart));
    };
    ComPtr<IShellItem> item;
    Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(),nullptr,IID_PPV_ARGS(&item))),"vram bench folder");
    for(int i=0;i<kTabs;++i) {
        Regression::Scope step("open tab="+std::to_string(i));
        auto& tab=tabs[size_t(i)];
        // A new tab appears selected; the previous one is hidden with it.
        tab.tabHost=CreateWindowExW(0,L"ShellTabWindowClass",L"",WS_CHILD|WS_VISIBLE,0,0,kWidth,kHeight,frame,nullptr,cls.hInstance,nullptr);
        if(i) ShowWindow(tabs[size_t(i-1)].tabHost,SW_HIDE);
        tab.host=CreateWindowExW(0,L"CtrlNotifySink",L"",WS_CHILD|WS_VISIBLE,kLeft,kTop,fullWidth,fullHeight,tab.tabHost,nullptr,cls.hInstance,nullptr);
        tab.native=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,0,0,fullWidth,fullHeight,tab.host,nullptr,cls.hInstance,nullptr);
        Check(tab.tabHost && tab.host && tab.native,"vram bench hierarchy");
        tab.response.Attach(new ColumnBridgeFixture::FolderView);
        tab.response->mode=i%2 ? FVM_ICON : FVM_DETAILS;
        Check(SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&tab.response->folder))),"vram bench binding");
        tab.response.As(&tab.view);
        AttachView(tab.native,tab.view,tab.response->folder,tab.browser,directory);
        tab.overlay=OverlayForTab(tab.tabHost); tab.state=State(tab.overlay);
        Check(tab.state && tab.state->presenter,"vram bench attachment");
        tab.work=tab.state->background;
        tab.receipts=std::make_unique<ColumnBridgeFixture::Receipts>(tab.overlay);
        Check(Eventually([&] {ColumnBridgeFixture::Pump(); return tab.state->loaded && tab.state->total>=512;},30000),"vram bench names");
        if(i) settle(tabs[size_t(i-1)],false,"previous tab did not park");
        settle(tab,true,"first frame");
        use(tab);
    }
    report("opened");
    for(int round=0;round<3;++round) {
        for(int i=0;i<kTabs;++i) {
            Regression::Scope step("round="+std::to_string(round)+" tab="+std::to_string(i));
            auto& from=tabs[size_t((i+kTabs-1)%kTabs)];
            auto& to=tabs[size_t(i)];
            select(&from,to);
            resizeHost(to,fullWidth-1100,fullHeight); settle(to,true,"switch pane toggle");
            resizeHost(to,fullWidth,fullHeight); settle(to,true,"switch pane toggle back");
        }
    }
    report("switched");
    std::cout<<"PASS: VRAM benchmark (focused measurement)\n";
    std::vector<std::shared_ptr<TabBackground>> work;
    for(auto& tab:tabs) {
        tab.receipts.reset(); tab.response.Reset(); tab.view.Reset(); tab.browser.Reset();
        work.push_back(tab.work);
    }
    DestroyWindow(frame);
    for(auto& w:work) if(w && w->presentation.thread) WaitForSingleObject(w->presentation.thread,10000);
    item.Reset();
    OleUninitialize();
}
