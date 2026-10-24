// Private offscreen rendering plus a deliberately unpumped owner STA. This
// checks the committed DWM animation contract, not monitor scan-out/FPS.
static void TestColumnFitComposition(HWND frame) {
    const bool automatic=g_autoRefitColumns.exchange(false);
    const auto filenameOnly=g_filenameOnly.exchange(ViewOption::GridOnly);
    for(bool rtl:{false,true}) {
        ViewState state;
        state.hwnd=CreateWindowExW(0,L"STATIC",L"column-fit-composition",WS_CHILD,
            0,0,800,480,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"fit composition window");
        state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
        state.background->hwnd=state.hwnd;
        state.visible=state.inScope=state.loaded=state.columnsView=state.detailsViewActive=true;
        state.rtlLayout=rtl; state.total=40; state.entries.resize(40); state.selectedFlags.assign(40,0);
        state.firstContentSubmittedAt=1; state.deferColumnProviders=false;
        state.featureInitializationStep=kOverlayFeatureSteps;
        for(int i=0;i<state.total;++i) {
            auto& e=state.entries[i]; e.name=L"Item "+std::to_wstring(i);
            e.type=L"Type only"; e.size=L"77 KB";
            e.detailsReady=true; e.columnEpoch=state.columnEpoch;
            e.cells={{PKEY_ItemTypeText,e.type},{PKEY_Size,e.size}};
        }
        for(const auto& key:{PKEY_FileName,PKEY_ItemTypeText,PKEY_Size}) {
            ExplorerColumn c; c.key=key;
            c.title=ClassifySort(key)==SortKind::Name ? L"Name" : SameKey(key,PKEY_Size) ? L"Size" : L"Kind";
            c.width=ClassifySort(key)==SortKind::Name ? 180 : 290;
            state.columns.push_back(c);
        }
        state.sort.explicitOrder=true; state.sort.key=PKEY_Size;
        UpdateMetrics(state); UpdateColumns(state); UpdateRefreshRate(state);
        Check(EnsureTarget(state),"fit composition target");
        const auto pixels=[&](ID2D1Bitmap1* source) {
            ComPtr<ID2D1Bitmap1> cpu;
            const auto size=source->GetPixelSize();
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                source->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(size,nullptr,0,&props,&cpu)) &&
                SUCCEEDED(cpu->CopyFromBitmap(nullptr,source,nullptr)),"fit composition readback");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"fit composition map");
            std::vector<BYTE> result(size_t(size.width)*size.height*4);
            for(UINT32 y=0;y<size.height;++y)
                memcpy(result.data()+size_t(y)*size.width*4,map.bits+y*map.pitch,size_t(size.width)*4);
            cpu->Unmap(); return result;
        };
        const auto ink=[&](ID2D1Bitmap1* source,UINT32 top,UINT32 bottom) {
            const auto p=pixels(source); const auto size=source->GetPixelSize(); size_t result=0;
            for(UINT32 y=top;y<std::min(bottom,size.height);++y)
                for(UINT32 x=0;x<size.width;++x) result+=p[(size_t(y)*size.width+x)*4+3]!=0;
            return result;
        };
        // Exercise the existing fitting route, shared by auto fit and divider
        // gestures, rather than constructing an unrelated visual-tree fixture.
        RequestAnimatedColumnRefit(state,state.columns);
        Check(state.columnTransitionActive && !state.columnModeTransition,"fit did not start ordinary column motion");
        Check(Eventually([&] { Paint(state); return !state.textRasterPending; }),"fit glyph sources never completed");
        Check(state.modeRootAttached && state.modeComposition.published && state.modeComposition.widths &&
            state.modeComposition.fitted.size()==state.columns.size(),"ordinary fit was still CPU-present driven");
        Check((PipelineFlags(state)&(1u<<29))!=0,"diagnostics failed to distinguish DWM fitting from CPU motion");
        const auto header=UINT32(state.headerPx);
        for(auto& col:state.modeComposition.fitted) {
            Check(ink(col.content.bitmap.Get(),0,header)>0 &&
                ink(col.content.bitmap.Get(),header,header+UINT32(state.rowPx*4))>0,
                "fit source lost column caption or body pixels in LTR/RTL");
            Check(ink(col.trailing.bitmap.Get(),0,header)>0,"fit lost its independently anchored divider/chevron");
        }
        // A neighbor's changing content must never leak through a changing
        // clip or share the source surface of another column.
        const auto name=pixels(state.modeComposition.fitted[0].content.bitmap.Get());
        const auto oldType=pixels(state.modeComposition.fitted[1].content.bitmap.Get());
        for(auto& e:state.entries) { e.type=L"Different type"; e.cells[0].text=e.type; }
        Check(Eventually([&] { Paint(state); return !state.textRasterPending; }),"changed fit source never completed");
        Check(pixels(state.modeComposition.fitted[0].content.bitmap.Get())==name &&
            pixels(state.modeComposition.fitted[1].content.bitmap.Get())!=oldType,
            "independent fit sources duplicated/stretched neighbor text");
        D2D1_MATRIX_3X2_F transform{}; state.target->GetTransform(&transform);
        Check(transform._11==1.f && transform._22==1.f && !transform._31 && !transform._32 &&
            !state.modeComposition.painting,"column source transform escaped the paint scope");
        // Start a fresh real retarget, then block ONLY this test's UI thread
        // for longer than the 31–47 ms native stalls in the supplied capture.
        auto previous=CaptureColumnLayout(state);
        state.columns[0].width=300; state.columns[1].width=180; state.columns[2].width=280;
        UpdateColumns(state); StartColumnTransition(state,previous); Paint(state);
        Check(state.modeComposition.widths && state.modeComposition.published,"retarget failed to commit its independent scene");
        const auto origin=state.columnTransitionStartedQpc.QuadPart;
        const auto root=state.modeComposition.root.Get();
        const auto position=state.columns[1].start;
        DCOMPOSITION_FRAME_STATISTICS before{},after{};
        Check(SUCCEEDED(state.composition->GetFrameStatistics(&before)),"fit composition clock before pause");
        Sleep(65); // No dispatch, Paint, frame callback, input or live Explorer.
        Check(SUCCEEDED(state.composition->GetFrameStatistics(&after)) && after.currentTime.QuadPart>before.currentTime.QuadPart &&
            state.columns[1].start==position && state.modeComposition.root.Get()==root &&
            state.columnTransitionStartedQpc.QuadPart==origin,"owner pause retimed or replaced the committed fit");
        AdvanceColumnTransition(state);
        Check(state.columns[1].start!=position,"fit hit geometry did not catch up to its absolute DWM clock");
        if(state.columnTransitionActive) {
            SetScrollImmediate(state,90.f); Paint(state);
            Check(state.scrollNow==90.f && state.modeComposition.root.Get()==root &&
                state.columnTransitionStartedQpc.QuadPart==origin,"concurrent scroll restarted or blocked a fitted-column scene");
        }
        FinishColumnTransition(state); Paint(state);
        Check(!state.modeRootAttached && state.modeComposition.fitted.empty(),"settled fit retained independent planes");
        // Pathological widths and active pointer/spring gestures use the
        // ordinary shared renderer without exposing a stale committed root.
        previous=state.columns; state.columns[0].width=1600; UpdateColumns(state);
        StartColumnTransition(state,previous); Paint(state);
        Check(!state.modeRootAttached && state.modeComposition.fitted.empty() &&
            state.rejectedColumnComposition==state.columnTransitionStartedQpc.QuadPart,
            "oversized fit allocated unbounded per-column surfaces or retried allocation each frame");
        FinishColumnTransition(state); Paint(state);
        UnregisterAnimationWindow(state.hwnd); Stop(state.background); DropTarget(state); DestroyWindow(state.hwnd);
    }
    g_autoRefitColumns=automatic;
    g_filenameOnly=filenameOnly;
    std::cout<<"PASS: LTR/RTL ordinary column fits commit independent DWM position/width/alpha curves; per-column header/body/edge pixels, neighbor isolation, 65 ms owner pause, catch-up, scrolling, retarget and bounded fallback\n";
}
