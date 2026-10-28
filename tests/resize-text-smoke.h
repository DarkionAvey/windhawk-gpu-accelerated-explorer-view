static LRESULT CALLBACK ResizeOverlaySubclass(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    // Let USER/DefWindowProc deliver the actual size notification.
    if(msg==WM_SIZE) return OverlayProc(hwnd,msg,wp,lp);
    return DefSubclassProc(hwnd,msg,wp,lp);
}

static void TestResizePresentation(HWND frame,const std::wstring& fixture) {
    const bool refit=g_autoRefitColumns.exchange(false);
    for(bool rtl:{false,true}) {
        ViewState state;
        state.hwnd=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,L"STATIC",L"resize presentation",
            WS_CHILD,0,0,800,480,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"resize presentation window");
        state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
        state.visible=state.inScope=state.detailsViewActive=state.loaded=state.columnsView=true;
        state.rtlLayout=rtl; state.firstContentSubmittedAt=1; state.deferColumnProviders=false;
        state.total=20; state.entries.resize(state.total);
        for(int i=0;i<state.total;++i) state.entries[i].name=L"Resized item "+std::to_wstring(i);
        ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=350;
        state.columns={name}; UpdateMetrics(state); UpdateColumns(state);
        Check(Eventually([&] {Paint(state); return state.lastAcceptedPaint && !state.presentPending;}),
            "resize initial frame was not accepted");
        const auto shown=state.swapChain;
        Check(state.presentedSwapChain==shown,"initial image was not attached after acceptance");
        DXGI_SWAP_CHAIN_DESC1 before{};
        Check(SUCCEEDED(shown->GetDesc1(&before)),"resize displayed chain dimensions");
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        Check(SetWindowSubclass(state.hwnd,ResizeOverlaySubclass,1,0),"resize presentation subclass");
        // Several real geometry changes before any paint must leave the
        // accepted image's buffers intact, including a return to its old size.
        for(const auto size:{SIZE{820,500},SIZE{760,440},SIZE{800,480},SIZE{840,520}}) {
            Check(SetWindowPos(state.hwnd,nullptr,0,0,size.cx,size.cy,
                SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"resize presentation bounds");
            DXGI_SWAP_CHAIN_DESC1 retained{};
            Check(SUCCEEDED(shown->GetDesc1(&retained)) && retained.Width==before.Width && retained.Height==before.Height,
                "resize destroyed the displayed buffers before replacement presentation");
        }
        state.presentationOnly=state.presentationActive=true;
        GetClientRect(state.hwnd,&state.presentationClient); state.presentationDpi=96;
        AuditFixture::busyFrame=state.frameLatencyWaitable;
        Paint(state);
        AuditFixture::busyFrame=nullptr; state.presentationOnly=false;
        Check(state.presentPending && state.presentedSwapChain==shown,
            "busy resized frame replaced the accepted image");
        Check(Eventually([&] {Paint(state); return !state.presentPending;}),"resized frame was not accepted");
        Check(state.swapChain!=shown && state.presentedSwapChain==state.swapChain,
            "resize did not attach the accepted replacement image");
        const auto body=state.textFormat,header=state.headerFormat;
        const auto factory=std::exchange(g_writeFactory,{});
        state.fontPx+=1.f;
        const bool refreshed=MakeTextFormats(state,true);
        g_writeFactory=factory;
        Check(!refreshed && state.textFormat==body && state.headerFormat==header && state.textFormatsDirty,
            "failed font refresh discarded usable formats or forgot the replacement");
        Check(EnsureTarget(state) && !state.textFormatsDirty && state.textFormat!=body,
            "font replacement did not recover through ordinary target readiness");
        RemoveWindowSubclass(state.hwnd,ResizeOverlaySubclass,1);
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
        UnregisterAnimationWindow(state.hwnd); DropTarget(state); DestroyWindow(state.hwnd);
    }
    {
        const auto destination=std::filesystem::path(fixture)/L"empty-replacement";
        Check(CreateDirectoryW(destination.c_str(),nullptr),"cold replacement private directory");
        PresentationFixture::Tab tab(L"pending names replacement",20);
        tab.Start();
        tab.Await([](const auto& receipt) {return receipt.accepted && !receipt.opacity;});
        Check(Eventually([&] {return !tab.state.presenter->animating.load();}),"pending navigation source did not settle");
        const auto accepted=tab.Receipt();
        ComPtr<IShellFolder> folder;
        // Use the actual speculative-navigation path, before Shell confirmation
        // is allowed to queue names. The destination is an actual private folder.
        ResetForNewDirectory(tab.state,destination.wstring(),folder,false,tab.frame,nullptr,true);
        unsigned long long heldFrame=0;
        for(int i=0;i<2;++i) {
            SubmitPresentation(tab.state);
            const auto sequence=tab.state.presentationSequence;
            const auto pending=tab.Await([&](const auto& r) {return r.sequence==sequence && r.control && !r.accepted;});
            // A pulse queued before navigation may finish before the loading
            // control is consumed. That receipt is the actual freeze boundary.
            if(!i) heldFrame=pending->frames;
            if(pending->frames!=heldFrame || pending->swapchain!=accepted->swapchain ||
                pending->device!=accepted->device || pending->failed)
                std::cout<<"PENDING NAV source/frame="<<accepted->frames<<"/"<<pending->frames
                    <<" chain="<<accepted->swapchain<<"/"<<pending->swapchain
                    <<" device="<<accepted->device<<"/"<<pending->device
                    <<" active="<<tab.state.visible<<","<<tab.state.inScope<<","<<tab.state.detailsViewActive
                    <<" failure="<<pending->failure<<" control="<<pending->control<<std::endl;
            Check(pending->frames==heldFrame && pending->frames>=accepted->frames && pending->swapchain==accepted->swapchain &&
                pending->device==accepted->device && !pending->failed,
                "pending navigation painted a placeholder or discarded its ready image");
        }
        QueueDirectoryRefresh(tab.state);
        Check(Eventually([&] {ReceiveDirectoryResults(tab.state); return tab.state.loaded;}),
            "empty replacement directory enumeration");
        SubmitPresentation(tab.state);
        const auto sequence=tab.state.presentationSequence;
        const auto empty=tab.Await([&](const auto& r) {return r.sequence==sequence && r.accepted && r.content;});
        Check(empty->frames>accepted->frames && empty->samples.empty(),"confirmed empty folder did not replace previous rows");
    }
    g_autoRefitColumns=refit;
    std::cout<<"PASS: LTR/RTL USER resize/backpressure and font failures preserve ready resources; real cold/empty navigation replaces only when names are ready\n";
}

static void TestResizeText(HWND frame) {
    // The generated Ctrl+wheel/pinch input must not inherit desktop Shift.
    ModifierFixture::Session modifiers;
    const bool refit = g_autoRefitColumns.exchange(false);
    for(bool rtl : {false,true}) {
        ViewState state;
        state.hwnd=CreateWindowExW(0,L"STATIC",L"resize text",WS_CHILD,0,0,1000,600,
            frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"resize text window");
        state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
        state.visible=state.inScope=state.detailsViewActive=state.loaded=state.columnsView=true;
        state.rtlLayout=rtl; state.firstContentSubmittedAt=GetTickCount64(); state.deferColumnProviders=false;
        UpdateMetrics(state);
        Check(EnsureTarget(state),"resize text GPU target");
        for(const auto& key : {PKEY_FileName,PKEY_DateModified,PKEY_ItemTypeText,PKEY_Size}) {
            ExplorerColumn column; column.key=key; column.title=L"Column"; column.width=210;
            state.columns.push_back(column);
        }
        UpdateColumns(state);
        state.total=117; state.entries.resize(state.total);
        for(int i=0;i<state.total;++i) {
            auto& e=state.entries[i]; e.name=L"File "+std::to_wstring(i)+L".txt";
            e.date=L"20/10/2026 12:25"; e.type=L"Text document"; e.size=L"123 KB";
            e.detailsReady=true; e.propertyEpoch=state.propertyEpoch;
        }
        // Read the buffer just drawn, before Present rotates a freshly resized
        // flip chain to its other, still empty buffer.
        auto draw=[&] {
            auto swap=std::move(state.swapChain);
            Paint(state);
            state.swapChain=std::move(swap);
        };
        for(int n=0;n<100;++n) { draw(); if(!state.textRasterPending) break; }
        Check(!state.textRasterPending,"resize text did not warm");
        const auto body=state.textFormat, header=state.headerFormat, sorted=state.sortedHeaderFormat;
        // Read the same full metadata columns in logical coordinates. RTL
        // moves them with the window; every cell must retain identical pixels.
        auto pixels=[&] {
            const auto first=state.columns[1].start+ListStartX(state)+8.f;
            const auto last=state.columns[2].end+ListStartX(state)-6.f;
            const UINT32 width=static_cast<UINT32>(last-first);
            const UINT32 height=static_cast<UINT32>(state.rowPx*8);
            const UINT32 x=static_cast<UINT32>(rtl ? state.paintWidth-last : first);
            const UINT32 y=static_cast<UINT32>(state.headerPx);
            ComPtr<ID2D1Bitmap1> cpu;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                state.backbuffer->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&props,&cpu)),"resize readback");
            const auto area=D2D1::RectU(x,y,x+width,y+height);
            Check(SUCCEEDED(cpu->CopyFromBitmap(nullptr,state.backbuffer.Get(),&area)),"resize text copy");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"resize text map");
            std::vector<BYTE> result(width*height*4);
            for(UINT32 row=0;row<height;++row) memcpy(result.data()+row*width*4,map.bits+row*map.pitch,width*4);
            cpu->Unmap(); return result;
        };
        const auto before=pixels();
        Check(std::any_of(before.begin(),before.end(),[](BYTE v) { return v!=0; }),"resize metadata pixels");
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        Check(SetWindowSubclass(state.hwnd,ResizeOverlaySubclass,1,0),"resize text subclass");
        for(int width : {980,900,1040,940,860,1000}) {
            Check(SetWindowPos(state.hwnd,nullptr,0,0,width,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"resize fixture bounds");
            state.frameProfile.recording=true;
            state.frameProfile.textHits=state.frameProfile.textMisses=state.frameProfile.textDeferred=0;
            draw();
            const auto after=pixels();
            if(after!=before) {
                size_t difference=0,error=0,alpha=0; for(size_t i=0;i<after.size();++i) {
                    difference+=after[i]!=before[i]; error+=abs(int(after[i])-before[i]);
                    if(i%4==3) alpha+=before[i];
                }
                std::cout<<"RESIZE PIXELS rtl="<<rtl<<" width="<<width<<" changed="<<difference
                    <<" error="<<error<<" alpha="<<alpha<<" header="<<state.headerPx<<" scroll="<<state.scrollNow
                    <<" body-reused="<<(state.textFormat==body)<<" row="<<state.rowPx
                    <<" hits/misses/deferred="<<state.frameProfile.textHits<<"/"<<state.frameProfile.textMisses
                    <<"/"<<state.frameProfile.textDeferred<<" retired="<<state.retiredTextLayouts.size()<<std::endl;
            }
            Check(after==before,"window resize blinked or erased resolved metadata");
            Check(state.textFormat==body && state.headerFormat==header && state.sortedHeaderFormat==sorted &&
                state.retiredTextLayouts.empty(),"geometry-only resize discarded font/text resources");
            Check(state.frameProfile.textDeferred==0,"window resize deferred metadata that was already visible");
        }
        state.frameProfile.recording=false;
        const auto propertyEpoch=state.propertyEpoch, columnEpoch=state.columnEpoch;
        // Exercise both real input handlers, including the first settled paint
        // and the READY notification after fingers lift. No property invalidation
        // or data delivery is involved in this already populated view.
        for(bool pinch : {false,true}) {
            state.manipulationEnabled=pinch; state.touchpadPinching=false;
            if(pinch) OverlayProc(state.hwnd,kMsgManipulationPinch,0,
                static_cast<LPARAM>(std::lround(logf(1.08f)*65536.f)));
            else OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
            Check(state.zoomAnimating,"metadata zoom input did not start its animation");
            for(double ms : {45.,90.,181.}) {
                QueryPerformanceCounter(&state.zoomStartedQpc);
                state.zoomStartedQpc.QuadPart-=static_cast<LONGLONG>(ms*state.qpcFrequency/1000.);
                AdvanceRowHeight(state);
                state.frameProfile.recording=true; state.frameProfile.textDeferred=0;
                draw();
                const auto shown=pixels();
                Check(state.frameProfile.textDeferred==0 &&
                    std::any_of(shown.begin(),shown.end(),[](BYTE b) { return b!=0; }),
                    "wheel/pinch zoom blinked populated metadata during animation or settlement");
            }
            if(pinch) OverlayProc(state.hwnd,kMsgManipulationStatus,0,0);
            draw();
            Check(state.propertyEpoch==propertyEpoch && state.columnEpoch==columnEpoch &&
                state.entries[0].date==L"20/10/2026 12:25" && state.entries[0].detailsReady,
                "presentation-only zoom invalidated populated properties");
        }
        state.frameProfile.recording=false;
        state.manipulationEnabled=state.touchpadPinching=false;
        // A real size change still rebuilds the body without touching headers.
        state.fontPx+=1.f;
        Check(MakeTextFormats(state,true) && state.textFormat!=body && state.headerFormat==header,
            "body-only refresh ignored an actual font change");
        UnregisterAnimationWindow(state.hwnd); DropTarget(state);
        RemoveWindowSubclass(state.hwnd,ResizeOverlaySubclass,1);
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.hwnd);
    }
    g_autoRefitColumns=refit;
    std::cout<<"PASS: LTR/RTL resize and wheel/pinch zoom retain populated metadata through settled/READY paints; actual font changes still refresh\n";
}
