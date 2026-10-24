static void TestResizeText(HWND frame) {
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
        for(int width : {980,900,1040,940,860,1000}) {
            Check(SetWindowPos(state.hwnd,nullptr,0,0,width,600,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"resize fixture bounds");
            OverlayProc(state.hwnd,WM_SIZE,SIZE_RESTORED,MAKELPARAM(width,600));
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
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.hwnd);
    }
    g_autoRefitColumns=refit;
    std::cout<<"PASS: LTR/RTL resize and wheel/pinch zoom retain populated metadata through settled/READY paints; actual font changes still refresh\n";
}
