// Private offscreen windows and synthetic item identities. No Shell probes,
// real drive access, focus changes, or live Explorer input.
static void TestInlineDriveBars(HWND frame) {
    for(int dpi:{96,137,144,192}) for(float label:{20.f,120.f,480.f}) {
        const float needed=InlineDriveContentWidth(label,dpi);
        Check(InlineDriveLabelWidth(needed,label,dpi)+.01f>=label,
            "inline bar fitting disagrees with the displayed label partition");
        Check(needed-InlineDriveLabelWidth(needed,label,dpi)-2.f*Scale96(kDriveBarSideMargin96,dpi)+.01f>=
            Scale96(kDriveBarMinimumWidth96,dpi),"custom DPI rounding made a fitted track too narrow to draw");
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
    std::cout<<"PASS: remaining-space aligned drive tracks, intrinsic label/font updates, wide/narrow resizing, filename feedback and standard/custom DPI; canonical/live geometry, moved Name columns, LTR/RTL, Details handoff/reversal and GPU source pixels\n";
}
