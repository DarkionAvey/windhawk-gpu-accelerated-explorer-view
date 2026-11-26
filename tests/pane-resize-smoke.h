// Production AttachView/OverlayProc/NativeHostSubclass/presentation lane over a
// private native hierarchy. Pane toggles and splitter drags are real SetWindowPos
// calls on the content host, as Explorer's layout issues them; nothing sends
// fabricated WM_SIZE/WINDOWPOS messages. Expected buffer extents come from the
// frame's own client rectangle and the displayed chain's DXGI description.
static void TestPaneResizePresentation(const std::wstring& directory,bool grid) {
    Regression::Scope scope(grid ? "view=grid" : "view=details");
    Check(SUCCEEDED(OleInitialize(nullptr)) && PrepareTextRuntime(),"pane resize runtime");
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    for(const auto* name:{L"CabinetWClass",L"CtrlNotifySink",L"SHELLDLL_DefView"}) {cls.lpszClassName=name; RegisterClassW(&cls);}
    cls.lpfnWndProc=OverlayProc; cls.lpszClassName=kOverlayClass; RegisterClassW(&cls);
    constexpr int kLeft=300; // Space a navigation pane occupies left of the content host.
    constexpr int kTop=100, kListHeight=600; // Command/address bars above it; panes never change its height.
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CabinetWClass",L"pane resize fixture",
        WS_POPUP|WS_VISIBLE,-32000,-32000,1200,700,nullptr,nullptr,cls.hInstance,nullptr);
    HWND host=CreateWindowExW(0,L"CtrlNotifySink",L"",WS_CHILD|WS_VISIBLE,kLeft,kTop,600,kListHeight,frame,nullptr,cls.hInstance,nullptr);
    HWND native=CreateWindowExW(0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,0,0,600,kListHeight,host,nullptr,cls.hInstance,nullptr);
    Check(frame && host && native,"pane resize native hierarchy");
    FocusFixture::Session focus(frame,nullptr);
    struct Cleanup {
        HWND frame; std::shared_ptr<TabBackground> work;
        ~Cleanup() {
            DestroyWindow(frame);
            if(work && work->presentation.thread) WaitForSingleObject(work->presentation.thread,10000);
        }
    } cleanup{frame};
    ComPtr<ColumnBridgeFixture::FolderView> response; response.Attach(new ColumnBridgeFixture::FolderView);
    response->mode=grid ? FVM_ICON : FVM_DETAILS;
    ComPtr<IShellItem> item;
    Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
        SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&response->folder))),"pane resize private folder");
    ComPtr<IFolderView2> view; response.As(&view); ComPtr<IShellBrowser> browser;
    AttachView(native,view,response->folder,browser,directory);
    const HWND overlay=OverlayForTab(frame); auto* state=State(overlay);
    Check(state && state->presenter,"pane resize attachment"); cleanup.work=state->background;
    // Owner dispatch consumes receipts; observe them before it does.
    ColumnBridgeFixture::Receipts receipts(overlay);
    // Wait for the lane to show a frame of the current model in the displayed chain.
    const auto shown=[&](const char* phase,bool standby=false) {
        auto& latest=receipts.latest;
        const bool done=Eventually([&] {
            {std::lock_guard lock(state->presenter->mutex); if(state->presenter->receipt) latest=state->presenter->receipt;}
            ColumnBridgeFixture::Pump();
            return latest && latest->sequence>=state->presentationSequence && latest->swapchain &&
                latest->presentedSwapchain==latest->swapchain &&
                (standby ? latest->standby : latest->accepted && !latest->standby);
        });
        if(!done) std::cout<<"PANE RESIZE "<<phase<<" receipt="<<bool(latest)<<" seq="<<(latest ? latest->sequence : 0)
            <<"/"<<state->presentationSequence<<" accepted="<<(latest && latest->accepted)
            <<" standby="<<(latest && latest->standby)<<" chain="<<(latest ? latest->swapchain : 0)
            <<"/"<<(latest ? latest->presentedSwapchain : 0)<<" visible="<<IsWindowVisible(overlay)<<std::endl;
        Check(done,phase);
        return latest;
    };
    const auto extent=[](ULONG_PTR chain) {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        Check(chain && SUCCEEDED(reinterpret_cast<IDXGISwapChain1*>(chain)->GetDesc1(&desc)),"displayed chain description");
        return SIZE{LONG(desc.Width),LONG(desc.Height)};
    };
    const auto frameClient=[&] {RECT r{}; GetClientRect(frame,&r); return SIZE{r.right,r.bottom};};
    const auto boundsOf=[&](HWND window) {
        RECT r{}; GetWindowRect(window,&r); MapWindowPoints(nullptr,frame,reinterpret_cast<POINT*>(&r),2); return r;
    };
    const auto resizeHost=[&](int width) {
        Check(SetWindowPos(host,nullptr,0,0,width,kListHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"content host resize");
        SetWindowPos(native,nullptr,0,0,width,kListHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    };
    Check(Eventually([&] {ColumnBridgeFixture::Pump(); return state->loaded && state->total>0;}),"pane resize names");
    auto first=shown("first frame was not shown");
    const auto chain=first->swapchain;
    const auto reach=frameClient();
    Check(extent(chain).cx==reach.cx && extent(chain).cy==kListHeight,
        "first buffers did not span the frame width at the list's own height");
    // Pane toggles and splitter steps: size-only host changes inside the frame.
    for(const int width:{900,450,610,611,1200-kLeft,500,880}) {
        Regression::Scope step(("host-width="+std::to_string(width)).c_str());
        const auto before=state->presentationSequence;
        resizeHost(width);
        // No message is dispatched here: the overlay followed inside the host's
        // own WM_WINDOWPOSCHANGED, and its WM_SIZE published the new layout.
        const RECT expected=boundsOf(host), actual=boundsOf(overlay);
        Check(EqualRect(&expected,&actual),"overlay did not follow a size-only host change in the same layout pass");
        Check(state->presentationSequence>before,"resize did not publish its layout before returning to the message loop");
        const auto accepted=shown("resized frame was not shown");
        Check(accepted->swapchain==chain,"a resize inside the frame replaced the displayed swap chain");
    }
    // A move is not followed synchronously: navigations lay new views out
    // through transient positions. The posted follow-up repositions it.
    {
        const RECT settled=boundsOf(overlay);
        Check(SetWindowPos(host,nullptr,0,kTop,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),"content host move");
        const RECT held=boundsOf(overlay);
        Check(EqualRect(&held,&settled),"overlay followed a transient host move inside the layout pass");
        Check(Eventually([&] {
            ColumnBridgeFixture::Pump(); const RECT a=boundsOf(overlay), e=boundsOf(host); return EqualRect(&a,&e);
        }),"posted follow-up did not reposition the overlay after a host move");
        Check(SetWindowPos(host,nullptr,kLeft,kTop,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),"content host move back");
        Check(Eventually([&] {
            ColumnBridgeFixture::Pump(); const RECT a=boundsOf(overlay), e=boundsOf(host); return EqualRect(&a,&e);
        }),"overlay did not return with the host");
    }
    // A larger frame (maximize, programmatic growth) needs one larger chain,
    // covering the new frame client area.
    Check(SetWindowPos(frame,nullptr,0,0,1800,700,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"frame growth");
    resizeHost(1800-kLeft); // Wider than the first chain.
    const auto grown=shown("frame growth was not shown");
    if(grown->swapchain==chain || extent(grown->swapchain).cx!=frameClient().cx)
        std::cout<<"PANE RESIZE growth chain-replaced="<<(grown->swapchain!=chain)<<" buffer="<<extent(grown->swapchain).cx
            <<"x"<<extent(grown->swapchain).cy<<" frame="<<frameClient().cx<<"x"<<frameClient().cy
            <<" overlay="<<boundsOf(overlay).right-boundsOf(overlay).left<<std::endl;
    Check(grown->swapchain!=chain && extent(grown->swapchain).cx==frameClient().cx,
        "frame growth did not allocate the new frame reach");
    // Shrinking while on screen keeps the displayed chain.
    Check(SetWindowPos(frame,nullptr,0,0,1000,700,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"frame shrink");
    resizeHost(1000-kLeft);
    Check(shown("frame shrink was not shown")->swapchain==grown->swapchain,"on-screen shrink replaced the displayed chain");
    // Off screen, the chain returns to the frame width and list height and is published before
    // standby completes, so the hidden tab keeps exactly one image.
    ShowWindow(overlay,SW_HIDE);
    const auto hidden=shown("hidden tab did not reach standby",true);
    Check(hidden->swapchain!=grown->swapchain && extent(hidden->swapchain).cx==frameClient().cx &&
        extent(hidden->swapchain).cy==kListHeight,"hidden tab kept buffers beyond the frame width or list height");
    ShowWindow(overlay,SW_SHOWNOACTIVATE);
    resizeHost(950-kLeft);
    Check(shown("returned tab resize was not shown")->swapchain==hidden->swapchain,
        "a pane toggle after the tab returned rebuilt its chain");
    std::cout<<"PASS: "<<(grid ? "grid" : "details")
        <<" pane toggles/splitter steps follow the host in its layout pass without replacing the displayed chain; "
          "moves use the posted path; buffers span the frame width at the list height; off-screen trim keeps one image\n";
    response.Reset(); view.Reset(); item.Reset();
    OleUninitialize();
}
