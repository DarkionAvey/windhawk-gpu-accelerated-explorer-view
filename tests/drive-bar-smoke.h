// Private offscreen windows and synthetic item identities. No Shell probes,
// real drive access, focus changes, or live Explorer input.
static void TestDriveLowSpaceClassification() {
    struct Case { ULONGLONG total, used; int percent; bool low; };
    const ULONGLONG maximum = ~ULONGLONG(0);
    // Explicit expectations cover strict boundaries, sub-percent precision,
    // unknown/invalid values and the full 64-bit range.
    const Case cases[] = {
        {1000,0,-1,false}, {1000,899,-1,false}, {1000,900,-1,false},
        {1000,901,-1,true}, {1000,1000,-1,true}, {1000,1001,-1,false},
        {9300,8812,-1,true}, {11,9,-1,false}, {11,10,-1,true},
        {1,0,-1,false}, {1,1,-1,true}, {0,0,-1,false}, {0,1,-1,false},
        {maximum,maximum,-1,true}, {maximum,maximum-1,-1,true},
        {maximum,maximum/2,-1,false},
        {18446744073709551615ULL,16602069666338596453ULL,-1,false},
        {18446744073709551615ULL,16602069666338596454ULL,-1,true},
        {0,0,0,false}, {0,0,90,false}, {0,0,91,true}, {0,0,100,true},
        {0,0,101,false}, {0,0,-2,false},
        // A provider can supply total capacity with only a usage percentage.
        {1000,0,91,true}, {1000,0,90,false}
    };
    for (const auto& sample : cases) {
        Regression::Scope scope("total="+std::to_string(sample.total)+
            " used="+std::to_string(sample.used)+" percent="+std::to_string(sample.percent));
        Entry entry; entry.driveTotal=sample.total; entry.driveUsed=sample.used;
        entry.driveUsedPercent=sample.percent;
        Check(DriveHasLowSpace(entry)==sample.low,"low-space classification");
    }
}

static void TestDriveBarColors(HWND frame) {
    StyleFixture::Session style;
    const auto selection=g_selectionRgb.exchange(RGB(0,180,0));
    const auto zebra=g_zebra.exchange(false);
    struct Restore {
        COLORREF selection; bool zebra;
        ~Restore() {g_selectionRgb=selection; g_zebra=zebra;}
    } restore{selection,zebra};
    for (int dpi : {96,144}) for (bool rtl : {false,true}) for (bool tiles : {false,true}) {
        Regression::Scope scope("drive colors dpi="+std::to_string(dpi)+
            " rtl="+std::to_string(rtl)+" tiles="+std::to_string(tiles)+
            " mode="+(tiles ? "Tiles" : "Details"));
        StyleFixture::dpi=UINT(dpi);
        ViewState state;
        state.hwnd=CreateWindowExW(0,L"STATIC",L"drive colors",WS_CHILD,
            0,0,900,400,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"drive color window");
        state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
        state.background->hwnd=state.hwnd;
        state.visible=state.inScope=state.loaded=state.thisPcGpu=true;
        state.gridView=tiles; state.columnsView=state.detailsViewActive=!tiles;
        state.layoutNativeMode=tiles ? FVM_TILE : FVM_DETAILS;
        state.rtlLayout=rtl; state.folderPath=kThisPcLocation;
        state.firstContentSubmittedAt=1; state.featureInitializationStep=kOverlayFeatureSteps;
        state.total=1; state.entries.resize(1); state.selectedFlags.assign(1,0);
        auto& entry=state.entries[0];
        entry.name=L"Storage"; entry.path=L"windhawk:bar-color-fixture";
        entry.folder=entry.detailsReady=true; entry.columnEpoch=state.columnEpoch;
        ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=800;
        state.columns={name};
        UpdateMetrics(state); UpdateColumns(state);
        Check(EnsureTarget(state),"drive color target");
        // Light -> high contrast -> dark, using the production palette refresh.
        for (int theme : {0,1,2}) {
            Regression::Scope themeScope(std::string("theme=")+
                (theme==0 ? "light" : theme==1 ? "high-contrast" : "dark"));
            StyleFixture::highContrast=theme==1;
            StyleFixture::appBg=theme==0 ? ABI::Windows::UI::Color{255,255,255,255} :
                ABI::Windows::UI::Color{255,24,24,24};
            RefreshNativePalette(state,true);
            Check(state.driveBarWarningColor==(theme==1 ? RGB(12,110,210) : RGB(232,17,35)),
                "drive warning palette did not follow high contrast or restore red");
            struct Sample { ULONGLONG total,used; int percent; bool low; };
            // Each warning is followed by normal usage to verify recovery.
            for (const auto& sample : {Sample{1000,901,-1,true}, {1000,900,-1,false},
                                      {1000,0,91,true}, {1000,0,90,false}}) {
                Regression::Scope sampleScope("capacity-bytes="+std::to_string(sample.total)+
                    " used-bytes="+std::to_string(sample.used)+
                    " used-percent="+std::to_string(sample.percent)+
                    " expected-low="+std::to_string(sample.low)+" expected-fill-RGB="+
                    (sample.low ? (theme==1 ? "(12,110,210)" : "(232,17,35)") : "(0,180,0)"));
                entry.driveTotal=sample.total; entry.driveUsed=sample.used;
                entry.driveUsedPercent=sample.percent;
                // Match OptionEffects::Canvas::Render: retain both resources
                // until readback ends so Paint cannot Present and rotate buffers.
                struct Capture {
                    ViewState& state;
                    ComPtr<IDXGISwapChain1> swap;
                    HANDLE latency;
                    ~Capture() {state.swapChain=std::move(swap); state.frameLatencyWaitable=latency;}
                } capture{state,std::move(state.swapChain),std::exchange(state.frameLatencyWaitable,nullptr)};
                Paint(state);
                const auto bar=LayoutDriveBar(state,0,LayoutItem(state,LayoutRowVisual(state,0,1.f)));
                Check(bar.visible,"drive color track");
                ComPtr<ID2D1Bitmap1> cpu;
                const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|
                    D2D1_BITMAP_OPTIONS_CANNOT_DRAW,state.backbuffer->GetPixelFormat());
                Check(SUCCEEDED(state.target->CreateBitmap(state.backbuffer->GetPixelSize(),nullptr,0,&props,&cpu)) &&
                    SUCCEEDED(cpu->CopyFromBitmap(nullptr,state.backbuffer.Get(),nullptr)),"drive color readback");
                D2D1_MAPPED_RECT map{};
                Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"drive color map");
                const float logical=bar.track.left+(bar.track.right-bar.track.left)*.25f;
                const UINT x=UINT(rtl ? float(cpu->GetPixelSize().width)-logical : logical);
                const UINT y=UINT((bar.track.top+bar.track.bottom)*.5f);
                Regression::Scope coordinateScope("pixel=("+std::to_string(x)+","+std::to_string(y)+
                    ") buffer=("+std::to_string(cpu->GetPixelSize().width)+","+
                    std::to_string(cpu->GetPixelSize().height)+")");
                Check(x<cpu->GetPixelSize().width && y<cpu->GetPixelSize().height,"drive color sample bounds");
                const auto* pixel=map.bits+size_t(y)*map.pitch+x*4;
                // Channel dominance is independent of the classifier and of the
                // theme text underneath the translucent fill (BGRA readback).
                const int channel=sample.low ? (theme==1 ? 0 : 2) : 1;
                Regression::Scope pixelScope("observed-BGRA=("+std::to_string(pixel[0])+","+
                    std::to_string(pixel[1])+","+std::to_string(pixel[2])+","+std::to_string(pixel[3])+
                    ") expected-alpha=[216,218] expected-dominant-channel="+
                    (channel==0 ? "B" : channel==1 ? "G" : "R")+" gap>40");
                // Interior alpha is .82 + .18 * (1 - .82), with byte rounding.
                Check(pixel[3]>=216 && pixel[3]<=218 && int(pixel[channel])>int(pixel[(channel+1)%3])+40 &&
                    int(pixel[channel])>int(pixel[(channel+2)%3])+40,"drive fill color pixels");
                cpu->Unmap();
            }
        }
        UnregisterAnimationWindow(state.hwnd); Stop(state.background); DropTarget(state); DestroyWindow(state.hwnd);
    }
}

static void TestInlineDriveBars(HWND frame) {
    for(int dpi:{96,137,144,192}) for(float label:{20.f,120.f,480.f}) {
        const float needed=InlineDriveContentWidth(label,dpi);
        Check(InlineDriveLabelWidth(needed,label,dpi)+.01f>=label,
            "inline bar fitting disagrees with the displayed label partition");
        Check(needed-InlineDriveLabelWidth(needed,label,dpi)-2.f*Scale96(kDriveBarSideMargin96,dpi)+.01f>=
            Scale96(kDriveBarMinimumWidth96,dpi),"custom DPI rounding made a fitted track too narrow to draw");
        Check(InlineDriveLabelWidth(needed-.125f,label,dpi)==0.f,
            "inline track took space from a complete drive label");
        Check(InlineDriveLabelWidth(float(Scale96(40,dpi)),label,dpi)==0.f,
            "narrow inline geometry crossed its column");
    }
    const auto filenameOnly=g_filenameOnly.exchange(ViewOption::GridOnly);
    for(bool rtl:{false,true}) for(bool reordered:{false,true}) {
        ViewState state;
        state.hwnd=CreateWindowExW(0,L"STATIC",L"inline-drive-bars",WS_CHILD,
            0,0,900,400,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"inline drive window");
        state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
        state.background->hwnd=state.hwnd;
        state.visible=state.inScope=state.loaded=state.detailsViewActive=state.columnsView=state.thisPcGpu=true;
        state.rtlLayout=rtl; state.folderPath=kThisPcLocation;
        state.firstContentSubmittedAt=1; state.featureInitializationStep=kOverlayFeatureSteps;
        state.total=3; state.entries.resize(3); state.selectedFlags.assign(3,0);
        const wchar_t* labels[]={L"Disk",L"A much longer storage label",L"An extremely long storage label that must use filename trimming"};
        for(int i=0;i<3;++i) {
            auto& e=state.entries[i]; e.name=labels[i]; e.path=L"windhawk:bar-fixture:"+std::to_wstring(i);
            e.folder=e.detailsReady=true; e.driveTotal=1000; e.driveUsed=i*500;
            e.columnEpoch=state.columnEpoch;
        }
        const auto column=[](PROPERTYKEY key,const wchar_t* title,int width) {
            ExplorerColumn c; c.key=key; c.title=title; c.width=width; return c;
        };
        const auto name=column(PKEY_FileName,L"Name",460);
        const auto percent=column(PKEY_PercentFull,L"Percent full",140);
        const auto total=column(PKEY_Capacity,L"Total size",160);
        state.columns=reordered ? std::vector<ExplorerColumn>{percent,name,total} :
            std::vector<ExplorerColumn>{name,percent,total};
        const size_t nameIndex=reordered ? 1 : 0;
        UpdateMetrics(state); UpdateColumns(state); UpdateRefreshRate(state);
        Check(EnsureTarget(state),"inline drive target");
        const auto bars=[&] {
            std::vector<DriveBarGeometry> result;
            for(int i=0;i<3;++i)
                result.push_back(LayoutDriveBar(state,i,LayoutItem(state,LayoutRowVisual(state,i,1.f))));
            for(const auto& bar:result)
                Check(bar.visible && fabsf(bar.track.left-result[0].track.left)<.001f &&
                    fabsf(bar.track.right-result[0].track.right)<.001f &&
                    bar.labelRight<bar.track.left,"different drive labels produced misaligned tracks");
            return result;
        };
        // Short labels must not acquire a widening empty band as the window
        // grows. The whole width increase belongs to the common track.
        const wchar_t* shortLabels[]={L"Local Disk (C:)",L"Toshiba (D:)",L"Local Disk (F:)"};
        for(int i=0;i<3;++i) state.entries[i].name=shortLabels[i];
        const float measured=SharedInlineDriveLabelWidth(state);
        float expected=0.f;
        for(const auto* label:shortLabels) expected=std::max(expected,ColumnTextWidth(state,label,false));
        Check(fabsf(measured-expected)<.01f,"shared drive label band did not use intrinsic label widths");
        const auto compact=bars();
        const float gap=float(Scale96(kDriveBarSideMargin96,ViewDpi(state)));
        const float textLeft=LayoutItem(state,LayoutRowVisual(state,0,1.f)).text.left;
        Check(fabsf(compact[0].track.left-textLeft-gap-std::ceil(measured+1.f))<.01f,
            "short drive labels retained a proportional empty band");
        SetWindowPos(state.hwnd,nullptr,0,0,1300,400,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        state.columns[nameIndex].width+=400; UpdateColumns(state);
        const auto wide=bars();
        Check(fabsf(wide[0].track.left-compact[0].track.left)<.01f &&
            fabsf(wide[0].track.right-compact[0].track.right-400.f)<.01f,
            "window growth expanded label spacing instead of the remaining-space track");
        Check(SharedInlineDriveLabelWidth(state)==measured,"resizing changed the intrinsic label measurement");
        const float font=state.fontPx;
        state.fontPx*=1.4f; MakeTextFormats(state);
        Check(SharedInlineDriveLabelWidth(state)>measured,"font change reused stale drive-label dimensions");
        state.fontPx=font; MakeTextFormats(state);
        Check(fabsf(SharedInlineDriveLabelWidth(state)-measured)<.01f,"returning to the original font changed label dimensions");
        state.entries[1].name=L"A newly resolved long storage label";
        Check(SharedInlineDriveLabelWidth(state)>measured,"late label update reused an obsolete shared width");
        SetWindowPos(state.hwnd,nullptr,0,0,900,400,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        state.columns[nameIndex].width-=400; UpdateColumns(state);
        for(int i=0;i<3;++i) state.entries[i].name=labels[i];
        Check(!LayoutDriveBar(state,2,LayoutItem(state,LayoutRowVisual(state,2,1.f))).visible,
            "an ellipsizing drive label retained its inline track");
        for(int i=0;i<3;++i) state.entries[i].name=shortLabels[i];
        // Shrink the real Name column through both the bar and label cutoffs.
        // DirectWrite, rather than a guessed character width, checks whether
        // every label fits whenever a track is offered. Continue shrinking
        // after ellipsis starts so minimum-width rounding cannot revive it.
        const int originalWidth=state.columns[nameIndex].width;
        bool hidden=false,trimmed=false;
        for(int width=originalWidth;width>0;--width) {
            state.columns[nameIndex].width=width; UpdateColumns(state);
            for(int i=0;i<3;++i) {
                const auto item=LayoutItem(state,LayoutRowVisual(state,i,1.f));
                const auto bar=LayoutDriveBar(state,i,item);
                if(!bar.visible) hidden=true;
                Check(!hidden || !bar.visible,"drive track reappeared as the Name column shrank");
                const float right=bar.visible ? bar.labelRight : item.text.right;
                if(auto* text=ResolveTextLayout(state,state.entries[i].name,right-item.text.left,
                        item.text.bottom-item.text.top,false,false,nullptr,false,true)) {
                    DWRITE_LINE_METRICS line{}; UINT32 count=0;
                    Check(SUCCEEDED(text->GetLineMetrics(&line,1,&count)) && count==1,
                        "drive label line metrics");
                    Check(!bar.visible || !line.isTrimmed,"visible track forced drive-label ellipsis");
                    trimmed|=line.isTrimmed;
                }
            }
        }
        Check(hidden && trimmed,"shrinking Name column never exercised hidden tracks and ellipsis");
        state.columns[nameIndex].width=originalWidth; UpdateColumns(state);
        const auto savedColumns=state.columns;
        state.columns={savedColumns[nameIndex]};
        state.columns[0].width=Scale96(55,ViewDpi(state));
        const int labelOnlyWidth=int(std::ceil(2.f*EdgeMarginPx(state)+
            NameTextOrigin(state)-NameColumnStart(state)+measured+1.f+8.f+gap));
        Check(SetWindowPos(state.hwnd,nullptr,0,0,labelOnlyWidth,400,
            SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE),"label-only viewport");
        UpdateColumns(state);
        Check(!LayoutDriveBar(state,0,LayoutItem(state,LayoutRowVisual(state,0,1.f))).visible &&
            !AutoRefitWouldImproveClipping(state),"hidden optional tracks caused a needless column refit");
        state.columns=savedColumns;
        SetWindowPos(state.hwnd,nullptr,0,0,900,400,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        UpdateColumns(state);
        const auto aligned=bars();
        g_filenameOnly=ViewOption::AlwaysOn;
        const auto selection=CurrentSelectionRect(state,2,false);
        const auto hover=PointerHoverRect(state,2);
        Check(selection.right<aligned[2].track.left && hover.right<aligned[2].track.left,
            "filename-only feedback extended beneath the aligned storage track");
        g_filenameOnly=ViewOption::GridOnly;
        AnimationClockFixture::Freeze clock;
        const auto sample=[&] {
            const auto logical=bars();
            ModeCompositionFrame source(state);
            Check(source.active && !source.Rows(),"mode transition did not use canonical column sources");
            const auto canonical=bars();
            const float translation=source.columns[nameIndex][0]-state.columns[nameIndex].start;
            for(size_t i=0;i<logical.size();++i)
                Check(fabsf(canonical[i].track.left+translation-logical[i].track.left)<.01f &&
                    fabsf(canonical[i].track.right+translation-logical[i].track.right)<.01f &&
                    fabsf(canonical[i].labelRight+translation-logical[i].labelRight)<.01f,
                    "canonical source changed the animated width or viewport clipping of dependent contents");
        };
        // The supplied capture enters Details from a full-width Name layout.
        // Check every phase against the ordinary renderer, including the last
        // frame before handoff, then interrupt and reverse on the same clock.
        SetColumnViewMode(state,false,false);
        SetColumnViewMode(state,true,true);
        auto origin=state.columnTransitionStartedQpc;
        for(double progress:{0.,.15,.35,.6,.9,.99999}) {
            AnimationClockFixture::instant.QuadPart=origin.QuadPart+
                LONGLONG(state.qpcFrequency*kUiTransitionDurationMs*.001*progress);
            AdvanceColumnTransition(state); sample();
        }
        const auto before=bars(); FinishColumnTransition(state);
        const auto settled=bars();
        Check(fabsf(before[0].track.left-settled[0].track.left)<.05f &&
            fabsf(before[0].track.right-settled[0].track.right)<.05f,
            "drive tracks jumped at Details settlement");
        SetColumnViewMode(state,false,true); origin=state.columnTransitionStartedQpc;
        AnimationClockFixture::instant.QuadPart=origin.QuadPart+
            LONGLONG(state.qpcFrequency*kUiTransitionDurationMs*.00035);
        AdvanceColumnTransition(state); sample();
        const auto reversing=bars(); SetColumnViewMode(state,true,true);
        const auto reversed=bars();
        Check(fabsf(reversing[0].track.left-reversed[0].track.left)<.01f &&
            fabsf(reversing[0].track.right-reversed[0].track.right)<.01f,
            "drive tracks jumped on a mid-animation reversal");
        sample();
        // Real GPU sources must contain the moving track, including its two
        // ends. Compare source pixels against geometry before the handoff.
        Check(Eventually([&] { Paint(state); return !state.textRasterPending; }),"inline source glyphs");
        Check(state.modeComposition.published,"inline source was not published");
        {
            ModeCompositionFrame source(state);
            const auto track=bars()[0].track;
            auto* bitmap=state.modeComposition.names.bitmap.Get();
            ComPtr<ID2D1Bitmap1> cpu;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                bitmap->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(bitmap->GetPixelSize(),nullptr,0,&props,&cpu)) &&
                SUCCEEDED(cpu->CopyFromBitmap(nullptr,bitmap,nullptr)),"inline source readback");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"inline source map");
            const UINT y=UINT((track.top+track.bottom)*.5f);
            const UINT width=bitmap->GetPixelSize().width;
            for(float fraction:{.05f,.5f,.95f}) {
                const float logical=track.left+(track.right-track.left)*fraction;
                const UINT x=UINT(rtl ? float(width)-logical : logical);
                Check(x<width && map.bits[size_t(y)*map.pitch+x*4+3]>0,
                    "animated inline source lost track pixels before settlement");
            }
            cpu->Unmap();
        }
        Check(!state.modeComposition.logicalColumns && !state.modeComposition.painting,
            "canonical layout escaped its paint scope");
        FinishColumnTransition(state); Paint(state);
        Check(!state.modeRootAttached,"inline source did not hand back to the settled renderer");
        UnregisterAnimationWindow(state.hwnd); Stop(state.background); DropTarget(state); DestroyWindow(state.hwnd);
    }
    g_filenameOnly=filenameOnly;
    std::cout<<"PASS: remaining-space aligned drive tracks, intrinsic label/font updates, monotonic hiding before ellipsis, optional-track clipping preflight, filename feedback and standard/custom DPI; canonical/live geometry, moved Name columns, LTR/RTL, Details handoff/reversal and GPU source pixels\n";
}
