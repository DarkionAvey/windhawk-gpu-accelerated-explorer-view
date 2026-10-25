// Compare the same steady translated-text workload on the selected source.
// This measures the render component, not Explorer or monitor frame timing.
template<class State>
static void ProfileAnimatedText(State& state) {
    std::vector<std::wstring> labels;
    for (int i = 0; i < 80; ++i)
        labels.push_back(L"Example filename and metadata " + std::to_wstring(i));
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    std::vector<double> times;
    for (int frame = 0; frame < 160; ++frame) {
        LARGE_INTEGER start{}, end{};
        QueryPerformanceCounter(&start);
        state.target->BeginDraw();
        state.target->Clear(D2D1::ColorF(0.f, 0.f, 0.f, 0.f));
        for (int i = 0; i < 80; ++i) {
            const float y = (i % 20) * 20.f - frame % 7;
            const float x = (i / 20) * 100.f;
            Text(state, labels[i], x, y, x + 98.f, y + 20.f,
                 0xffffff, false, .7f);
        }
        Check(SUCCEEDED(state.target->EndDraw()), "text profiling draw failed");
        QueryPerformanceCounter(&end);
        if (frame >= 20) times.push_back(1000. * (end.QuadPart-start.QuadPart) / frequency.QuadPart);
    }
    std::sort(times.begin(), times.end());
    std::cout << "PROFILE: 80 translated text cells median=" << times[times.size()/2]
              << " ms p95=" << times[times.size()*95/100] << " ms\n";
    LARGE_INTEGER fitStart{}, fitEnd{};
    QueryPerformanceCounter(&fitStart);
    for (int pass = 0; pass < 20; ++pass)
        for (const auto& text : labels) ColumnTextWidth(state, text, false);
    QueryPerformanceCounter(&fitEnd);
    std::cout << "PROFILE: repeated 80-cell column measurements=" <<
        1000. * (fitEnd.QuadPart-fitStart.QuadPart) / frequency.QuadPart / 20. << " ms/fit\n";
    if constexpr (requires { state.textLayouts; }) {
        const auto layout = ResolveTextLayout(state, labels[0], 98.f, 20.f);
        Check(layout && layout == ResolveTextLayout(state, labels[0], 98.f, 20.f),
              "unchanged text rebuilt its layout");
        Check(layout == ResolveTextLayout(state, labels[0], 56.f, 20.f) &&
              layout->GetMaxWidth() == 56.f, "column geometry did not reuse the shaped layout");
        const std::wstring measurement = L"Cold column measurement, reused when drawn";
        const float full = ColumnTextWidth(state, measurement, false);
        Check(full > 0.f && state.textLayouts[0].layouts.contains(measurement),
              "column fit shaped a disposable layout outside the renderer cache");
        const auto shaped = ResolveTextLayout(state, measurement, 40.f, 20.f);
        Check(shaped && ColumnTextWidth(state, measurement, false) == full &&
              shaped->GetMaxWidth() == 40.f,
              "cached intrinsic measurement reflowed or measured the clipped drawing extent");
    }
}

static void TestColumnViewTransition(HWND frame) {
    for (bool rtl : {false, true}) {
        ViewState state, other;
        state.hwnd = CreateWindowExW(0, L"STATIC", L"view-mode-transition", WS_CHILD,
            0, 0, 640, 400, frame, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(state.hwnd != nullptr, "view mode fixture window");
        state.frameHwnd = state.activeTabHost = frame; state.nativeHwnd = state.hwnd;
        state.background->hwnd = state.hwnd;
        state.visible = state.inScope = state.loaded = state.columnsView = true;
        state.rtlLayout = rtl;
        state.entries.resize(50); state.total = 50;
        auto column = [](PROPERTYKEY key, const wchar_t* title, int width) {
            ExplorerColumn c; c.key = key; c.title = title; c.width = width; return c;
        };
        state.columns = {column(PKEY_FileName, L"Name", 350),
            column(PKEY_ItemTypeText, L"Type", 240), column(PKEY_Size, L"Size", 240)};
        for (int i=0;i<state.total;++i) {
            auto& e=state.entries[i]; e.name=L"Mode row "+std::to_wstring(i); e.folder=true;
            e.type=L"File folder"; e.detailsReady=true; e.columnEpoch=state.columnEpoch;
            e.cells.push_back({PKEY_ItemTypeText,e.type});
        }
        UpdateMetrics(state); UpdateColumns(state); UpdateRefreshRate(state);
        Check(EnsureTarget(state), "view mode GPU target");
        const auto original = state.columns;
        const float fullHeader = state.headerPx;
        // Compare real header pixels against a short translated/faded settled
        // frame, while canonical compositor source pixels remain unchanged.
        const auto headerPixels = [&](ID2D1Bitmap1* source) {
            ComPtr<ID2D1Bitmap1> cpu;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                source->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(source->GetPixelSize(),nullptr,0,&props,&cpu)) &&
                SUCCEEDED(cpu->CopyFromBitmap(nullptr,source,nullptr)),"header slide readback");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"header slide map");
            std::vector<BYTE> result(size_t(fullHeader)*640*4);
            for(UINT32 y=0;y<UINT32(fullHeader);++y)
                memcpy(result.data()+size_t(y)*640*4,map.bits+y*map.pitch,640*4);
            cpu->Unmap(); return result;
        };
        const auto cpuPaint = [&] {
            auto swap=std::move(state.swapChain);
            Paint(state); state.swapChain=std::move(swap);
            return headerPixels(state.backbuffer.Get());
        };
        state.sort.explicitOrder=true; state.sort.key=PKEY_ItemTypeText;
        const auto settledHeader=cpuPaint();
        const auto checkSlide = [&](ID2D1Bitmap1* plane=nullptr) {
            const float header=state.headerPx;
            const float slide=static_cast<float>(Scale96(kColumnSlideDistance96,DpiOf(state.hwnd)));
            const size_t shift=plane ? 0 : size_t(std::lround(slide*(1.f-header/fullHeader)));
            // Sample whole-pixel displacement without changing its fixed-DIP
            // relation to the independently collapsing body/header extent.
            if(!plane) state.headerPx=fullHeader*(1.f-shift/slide);
            const auto pixels=plane ? headerPixels(plane) : cpuPaint();
            const float alpha=plane ? 1.f : ColumnModeVisibility(state);
            const size_t offset=shift*640*4;
            const size_t length=size_t(plane ? fullHeader : state.headerPx)*640*4;
            size_t opaque=0;
            for(size_t p=0;p<length;p+=4) {
                if(!settledHeader[offset+p+3]) continue;
                ++opaque;
                // Ordinary animated text may use direct drawing instead of
                // the gamma-corrected cached mask; allow small coverage error.
                int error=0;
                for(size_t c=0;c<4;++c) {
                    const int low=int(std::lround(settledHeader[offset+p+c]*alpha));
                    // The ordinary fallback fades brushes, so overlapping
                    // chevron strokes accumulate alpha. DComp fades once
                    // after composition and retains exact source coverage.
                    const int high=int(std::lround(settledHeader[offset+p+c]*(1.f-(1.f-alpha)*(1.f-alpha))));
                    error=std::max(error,std::max(low-int(pixels[p+c]),int(pixels[p+c])-high));
                }
                if(error>8)
                    std::cout<<"HEADER: plane="<<bool(plane)<<" rtl="<<rtl<<" h="<<state.headerPx
                        <<" xy="<<(p/4%640)<<","<<(p/4/640)<<" alpha="<<unsigned(pixels[p+3])
                        <<" expected="<<unsigned(settledHeader[offset+p+3])<<"\n";
                Check(error<=8,
                    "header container did not share the fixed-DIP vertical slide and fade");
            }
            Check(opaque>10,"header slide fixture had no visible header pixels");
            state.headerPx=header;
        };
        auto tick = [&](double ms) {
            QueryPerformanceCounter(&state.columnTransitionStartedQpc);
            state.columnTransitionStartedQpc.QuadPart -=
                static_cast<LONGLONG>(state.qpcFrequency * ms / 1000.);
            AdvanceColumnTransition(state);
        };
        SetColumnViewMode(state, false);
        const float slide=static_cast<float>(Scale96(kColumnSlideDistance96,DpiOf(state.hwnd)));
        for(size_t i=1;i<state.columnMotion.size();++i)
            Check(fabsf(state.columnMotion[i].toStart-state.columnMotion[i].fromStart-slide)<.01f,
                "mode exit displacement depended on column width");
        Check(!state.columnsView && state.columnModeTransition &&
            state.headerPx == fullHeader && state.columns[1].start == original[1].start,
            "Details to List snapped the header/columns at the mode switch");
        float header = fullHeader, secondary = original[1].start;
        for (double ms : {30., 60., 90., 120., 150.}) {
            tick(ms);
            Check(state.headerPx < header && state.headerPx > 0.f &&
                state.columns[1].start >= secondary, "view collapse reversed/overshot");
            header = state.headerPx; secondary = state.columns[1].start;
            UpdateMetrics(state); UpdateColumns(state); // resize/row delivery maintenance
            Check(state.columnModeTransition && state.headerPx == header &&
                state.columns[1].start == secondary, "maintenance snapped a moving mode");
            if(ms<=60.) checkSlide();
            UINT presentBefore=0,presentAfter=0;
            Check(SUCCEEDED(state.swapChain->GetLastPresentCount(&presentBefore)),"mode present counter before scene");
            Paint(state);
            Check(SUCCEEDED(state.swapChain->GetLastPresentCount(&presentAfter)) && presentBefore==presentAfter,
                "canonical mode pixels entered the ordinary swap chain before the scene transaction");
            Check(state.modeComposition.published && !state.modeComposition.painting &&
                state.modeComposition.started.QuadPart==state.columnTransitionStartedQpc.QuadPart &&
                state.headerPx==header && state.columns[1].start==secondary,
                "mode animation did not publish independent composition or leaked canonical paint geometry");
            checkSlide(state.modeComposition.background.bitmap.Get());
            if (ms==30.) {
                const auto ink=[&](ID2D1Bitmap1* source,UINT32 top) {
                    const auto size=source->GetPixelSize();
                    ComPtr<ID2D1Bitmap1> cpu;
                    const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,source->GetPixelFormat());
                    Check(SUCCEEDED(state.target->CreateBitmap(size,nullptr,0,&props,&cpu)) &&
                        SUCCEEDED(cpu->CopyFromBitmap(nullptr,source,nullptr)),"composition plane readback");
                    D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"composition plane map");
                    size_t count=0;
                    for(UINT32 y=top;y<size.height;++y) for(UINT32 x=0;x<size.width;++x) count+=map.bits[y*map.pitch+x*4+3]!=0;
                    cpu->Unmap(); return count;
                };
                // Cold metadata retains the normal bounded readiness policy.
                // Preparation is time-budgeted, so a busy machine may need
                // more than two paints. Wait for bounded readiness rather
                // than treating one deferred frame as missing metadata.
                Check(Eventually([&] {
                    Paint(state);
                    return ink(state.modeComposition.metadata.bitmap.Get(),0)>0;
                },1000),"composition metadata never became ready");
                const auto names=ink(state.modeComposition.names.bitmap.Get(),0);
                const auto metadata=ink(state.modeComposition.metadata.bitmap.Get(),0);
                const auto chrome=ink(state.modeComposition.chrome.bitmap.Get(),0);
                const auto overscan=ink(state.modeComposition.names.bitmap.Get(),400);
                Check(ink(state.modeComposition.background.bitmap.Get(),0)>0,"composition lost header/background pixels");
                std::cout<<"COMPOSITION PIXELS rtl="<<rtl<<" names="<<names<<" metadata="<<metadata<<" chrome="<<chrome<<" overscan="<<overscan<<'\n';
                Check(names>0 && metadata>0 && chrome>0 && overscan>0,
                    "composition lost foreground/metadata/scrollbar pixels or bottom overscan");
            }
            auto caption = state.textLayouts[1].layouts.find(L"Name");
            Check(caption != state.textLayouts[1].layouts.end() &&
                fabsf(caption->second.height - fullHeader) < .01f,
                "collapsing header reflowed caption height each frame");
        }
        tick(181.);
        Paint(state);
        Check(!state.modeComposition.started.QuadPart,"settled mode retained its composition planes");
        Check(!state.columnTransitionActive && state.headerPx == 0.f &&
            !ColumnsPresented(state) && HorizontalHeight(state) == 0.f,
            "List settlement retained a header/secondary presentation");
        SetColumnViewMode(state, true);
        Check(state.headerPx == 0.f && state.columnModeTransition &&
            fabsf(state.columnMotion[1].fromStart-state.columnMotion[1].toStart-slide)<.01f,
            "Details columns did not enter from the fixed-DIP offset");
        tick(60.);
        checkSlide();
        {
            AnimationClockFixture::Freeze instant;
            tick(60.); // Retarget continuity compares geometry at one animation instant.
            header = state.headerPx;
            const auto fixedHeaders=state.columnMotion;
            SetScrollImmediate(state, 120.f);
            Check(state.columnModeTransition && state.scrollNow == 120.f,
                "view-mode animation blocked concurrent scrolling");
            SetColumnViewMode(state, false);
            Check(fabsf(state.headerPx-header) < .2f && state.columnModeTransition,
                "reversing a mode change restarted at an endpoint");
            for(size_t i=0;i<fixedHeaders.size();++i)
                Check(ColumnBounds(state,i,true)==std::pair{fixedHeaders[i].headerStart,fixedHeaders[i].headerEnd},
                    "mode reversal borrowed the body's sideways positions for header captions");
            tick(60.); header = state.headerPx;
            SetColumnViewMode(state, true);
            Check(fabsf(state.headerPx-header) < .2f, "second mode reversal snapped the header");
        }
        checkSlide();
        header = state.headerPx; secondary = state.columns[1].start;
        for (double ms : {30., 60., 90., 120., 150., 181.}) {
            tick(ms);
            Check(state.headerPx >= header && state.columns[1].start <= secondary + .01f,
                "Details expansion or secondary-column entry rebounded");
            header = state.headerPx; secondary = state.columns[1].start;
        }
        Check(state.headerPx == fullHeader && !state.columnTransitionActive &&
            state.columns[1].start == original[1].start && other.headerPx == 38.f &&
            !other.columnTransitionActive, "mode settlement changed widths or another tab");
        for(UINT mode : {FVM_ICON,FVM_DETAILS,FVM_ICON,FVM_DETAILS}) {
            ApplyViewLayout(state,{mode,96},true);
            Check(state.columnModeTransition,"Details/grid change skipped the header container animation");
            tick(40.); checkSlide(); Paint(state);
            checkSlide(state.modeComposition.background.bitmap.Get());
        }
        FinishColumnTransition(state); Paint(state);
        SetColumnViewMode(state,false); Paint(state);
        Check(state.modeRootAttached && state.modeComposition.published,"resize mode scene was not committed");
        const auto scene=state.modeComposition.root.Get();
        const auto buffer=state.backbuffer.Get();
        const auto origin=state.modeComposition.started.QuadPart;
        Check(ResizeTarget(state,640,400) && state.modeRootAttached && state.modeComposition.published &&
            state.modeComposition.root.Get()==scene && state.backbuffer.Get()==buffer &&
            state.modeComposition.started.QuadPart==origin,
            "unchanged size exposed the stale ordinary frame or restarted the compositor scene");
        SetWindowPos(state.hwnd,nullptr,0,0,652,412,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        Check(ResizeTarget(state,652,412) && state.modeRootAttached && !state.modeComposition.started.QuadPart,
            "real resize exposed the stale ordinary frame before replacement pixels were ready");
        Paint(state);
        Check(state.modeRootAttached && state.modeComposition.published,"resized mode did not rebuild its own scene");
        FinishColumnTransition(state); Paint(state);
        Check(!state.modeRootAttached && !state.modeComposition.started.QuadPart,"mode settlement did not restore the ordinary visual root");
        SetColumnViewMode(state,true,false);
        state.columns.clear(); SetColumnViewMode(state, false); tick(90.);
        Check(state.columnModeTransition && state.headerPx > 0.f && state.headerPx < fullHeader,
            "header animation waited for column descriptors");
        FinishColumnTransition(state);
        UnregisterAnimationWindow(state.hwnd); Stop(state.background);
        DropTarget(state); DestroyWindow(state.hwnd);
    }
    std::cout << "PASS: whole Details/List/grid header uses a fixed-DIP vertical slide/fade on 180 ms pacing; compositor source pixels, reversal, scrolling, maintenance, pending descriptors, LTR/RTL and tab isolation\n";
}

static void TestColumnEntrance(HWND frame) {
    for (bool rtl : {false, true}) {
        ViewState state;
        state.hwnd = CreateWindowExW(0, L"STATIC", L"column-entrance", WS_CHILD,
            0, 0, 640, 400, frame, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(state.hwnd != nullptr, "column fixture window");
        state.nativeHwnd = state.hwnd;
        state.frameHwnd = state.activeTabHost = frame;
        state.background->hwnd = state.hwnd;
        state.visible = state.inScope = state.loaded = state.columnsView = true;
        state.rtlLayout = rtl;
        state.fontPx = 16.f; state.rowPx = 34.f;
        UpdateRefreshRate(state);
        Check(MakeTextFormats(state), "column fixture fonts");
        state.entries.resize(1); state.total = 1;
        auto& entry = state.entries[0];
        entry.name = L"a.txt"; entry.type = L"Text";
        entry.detailsReady = true;
        entry.columnEpoch = state.columnEpoch;
        entry.cells.push_back({PKEY_ItemTypeText, entry.type});
        auto column = [](PROPERTYKEY key, const wchar_t* title) {
            ExplorerColumn c; c.key = key; c.title = title; c.width = 145;
            return c;
        };
        state.columns = {column(PKEY_FileName, L"Name"), column(PKEY_ItemTypeText, L"Type")};
        RefitColumns(state, {});
        Check(!AutoRefitWouldImproveClipping(state), "original columns were not already fitted");
        const auto previous = state.columns;
        state.columns.push_back(column(PKEY_Author, L"Authors"));
        InvalidateDetailJobs(state);
        RefitColumns(state, previous);
        state.columnFitPendingEpoch = state.columnEpoch;
        Check(state.columnTransitionActive && !state.columnTransitionSpring &&
              state.columnMotion.back().entering, "column add did not start its ordinary entrance");
        const float slide=static_cast<float>(Scale96(kColumnSlideDistance96,DpiOf(state.hwnd)));
        Check(fabsf(state.columnMotion.back().fromStart-state.columnMotion.back().toStart-slide)<.01f &&
              state.columns.back().opacity==0.f,"column entrance did not use fixed-DIP travel with zero starting alpha");
        auto tick = [&](double ms) {
            QueryPerformanceCounter(&state.columnTransitionStartedQpc);
            state.columnTransitionStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*ms/1000.);
            AdvanceColumnTransition(state);
        };
        float last = state.columns.back().start, alpha=0.f;
        for (double ms : {30., 60., 90., 120., 150., 181.}) {
            tick(ms);
            Check(state.columns.back().start <= last+.01f,
                  "fixed-target column entrance overshot or reversed");
            Check(state.columns.back().opacity>=alpha && state.columns.back().opacity<=1.f,
                  "column entrance alpha reversed or overshot");
            alpha=state.columns.back().opacity;
            last = state.columns.back().start;
        }
        const float arrived = state.columns.back().start;
        Paint(state);
        Check(!state.columnPresentationPending,
              "column entrance did not submit its settled geometry before metadata refinement");
        auto metadata = [&](std::wstring text) {
            DirectoryResult result;
            result.kind = DirectoryResult::Details; result.token = state.directoryToken;
            DetailValue value; value.index = 0; value.columnEpoch = state.columnEpoch;
            value.cells.push_back({PKEY_Author, std::move(text)});
            result.details.push_back(std::move(value));
            PublishDirectoryResult(*state.background, std::move(result));
            ReceiveDirectoryResults(state);
        };
        metadata(L"A");
        if (state.columnTransitionActive)
            std::cout << "COLUMN: arrived at " << arrived << ", post-data target "
                      << state.columnMotion.back().toStart << " (all text already fits)\n";
        Check(!state.columnTransitionActive && state.columns.back().start == arrived &&
              !state.columnFitPendingEpoch,
              "column completion moved an already-readable entrance target back to the right");
        // A real newly discovered clipping problem must still receive a fit.
        state.columnFitPendingEpoch = state.columnEpoch;
        metadata(std::wstring(100, L'W'));
        Check(state.columnTransitionActive && state.columnMotion.back().toStart <= arrived,
              "necessary metadata fitting was disabled with the redundant completion");
        FinishColumnTransition(state);
        const auto beforeRemoval = state.columns;
        state.columns.pop_back();
        RefitColumns(state, beforeRemoval);
        const auto motion = state.columnMotion;
        Check(state.disappearingColumns.size()==1 &&
              fabsf(motion.back().toStart-motion.back().fromStart-slide)<.01f,
              "removed column disappeared instead of using fixed-DIP slide/fade");
        alpha=1.f;
        for (double ms : {30., 90., 150., 181.}) {
            tick(ms);
            for (size_t i = 0; i < state.columns.size()+state.disappearingColumns.size(); ++i)
            {
                const auto& column=PresentedColumn(state,i);
                Check(column.start >= std::min(motion[i].fromStart, motion[i].toStart)-.01f &&
                      column.start <= std::max(motion[i].fromStart, motion[i].toStart)+.01f,
                      "column removal acquired overshoot");
            }
            if(!state.disappearingColumns.empty()) {
                Check(state.disappearingColumns.back().opacity<alpha,
                    "removed column alpha did not decrease");
                alpha=state.disappearingColumns.back().opacity;
            }
        }
        Check(state.disappearingColumns.empty(),"settled column retained paint-only removal records");
        const auto removed = state.columns;
        state.columns.push_back(beforeRemoval.back());
        InvalidateDetailJobs(state);
        RefitColumns(state, removed); // Re-add with the same cached values.
        state.columnFitPendingEpoch = state.columnEpoch;
        FinishColumnTransition(state);
        const float cachedArrival = state.columns.back().start;
        Paint(state);
        metadata(std::wstring(100, L'W'));
        Check(!state.columnTransitionActive && state.columns.back().start == cachedArrival,
              "cached column re-addition introduced a second movement");
        const auto beforeRetoggle=CaptureColumnLayout(state);
        state.columns.pop_back(); RefitColumns(state,beforeRetoggle); tick(60.);
        const auto midway=CaptureColumnLayout(state);
        const auto fading=state.disappearingColumns.back();
        state.columns.push_back(beforeRetoggle.back()); RefitColumns(state,midway);
        Check(state.disappearingColumns.empty() &&
              fabsf(state.columns.back().start-fading.start)<.01f &&
              fabsf(state.columns.back().opacity-fading.opacity)<.01f,
              "rapid re-addition restarted column geometry/alpha instead of retargeting");
        FinishColumnTransition(state);
        Check(state.columns.back().opacity==1.f && state.disappearingColumns.empty(),
            "column settlement retained transient opacity");
        UnregisterAnimationWindow(state.hwnd);
        Stop(state.background);
        DestroyWindow(state.hwnd);
    }
    std::cout << "PASS: fitted column entrance stays monotonic across metadata completion in LTR/RTL; wide data, removal and cached re-addition remain\n";
}

template<class State>
static void TestZebraBackground(State& state) {
    StopRowTransition(state); StopScrollAnimation(state);
    StopKeyboardSelectionAnimation(state); StopHoverAnimation(state);
    state.sections.clear(); state.rowGroup.clear(); state.grouping.enabled = false;
    state.groupAnimating = state.zoomAnimating = false;
    state.visible = state.inScope = state.loaded = state.detailsViewActive = true;
    state.columnsView = false; state.headerPx = 0; state.rowPx = 20.f;
    state.scrollNow = state.scrollTarget = 0.f;
    state.selected = state.hovered = -1;
    state.firstContentSubmittedAt = 0;
    state.entries.clear(); state.total = 8;
    for (int i = 0; i < state.total; ++i) {
        Entry e; e.path = L"zebra"+std::to_wstring(i); state.entries.push_back(std::move(e));
    }
    state.selectedFlags.assign(state.total, 0);
    state.directoryToken = g_nextToken.fetch_add(1);
    state.background->directoryGeneration = state.directoryToken;
    state.background->hwnd = state.hwnd;
    state.displayedSort = state.sort;
    UpdateRefreshRate(state);
    const bool zebra = g_zebra.exchange(true), emptyRows = g_emptyRows.exchange(true);
    const int opacity = g_zebraOpacity.exchange(80);
    auto caption = std::move(g_emptyFolderCaption);
    g_emptyFolderCaption.clear();
    auto swapChain = std::move(state.swapChain);
    auto backbuffer = std::move(state.backbuffer);
    const auto targetProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, backbuffer->GetPixelFormat());
    Check(SUCCEEDED(state.target->CreateBitmap(backbuffer->GetPixelSize(), nullptr, 0,
        &targetProps, &state.backbuffer)), "zebra render target");
    state.target->SetTarget(state.backbuffer.Get());
    ComPtr<ID2D1Bitmap1> readback;
    const auto readProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, backbuffer->GetPixelFormat());
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1, 300), nullptr, 0,
        &readProps, &readback)), "zebra readback target");
    auto pixels = [&] {
        Paint(state);
        const auto rect = D2D1::RectU(250, 0, 251, 300);
        Check(SUCCEEDED(readback->CopyFromBitmap(nullptr, state.backbuffer.Get(), &rect)), "zebra copy");
        D2D1_MAPPED_RECT map{};
        Check(SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ, &map)), "zebra map");
        std::vector<int> alpha(300);
        for (size_t y = 0; y < alpha.size(); ++y) alpha[y] = map.bits[y*map.pitch+3];
        readback->Unmap(); return alpha;
    };
    auto diff = [&](int remove, int insert) {
        DirectoryResult result;
        result.kind = DirectoryResult::Complete; result.token = state.directoryToken;
        result.oldTotal = state.total; result.oldRowPx = state.rowPx;
        result.oldScroll = result.viewportCapturedScrollPx = state.scrollNow;
        result.entries = state.entries;
        if (remove >= 0) result.entries.erase(result.entries.begin()+remove);
        if (insert >= 0) {
            Entry e; e.path = L"inserted"+std::to_wstring(g_nextToken.fetch_add(1));
            result.entries.insert(result.entries.begin()+insert, std::move(e));
        }
        for (int i = 0; i < state.total; ++i) {
            CapturedRow old;
            old.oldIndex = i; old.oldTop = i*state.rowPx; old.oldHeight = state.rowPx;
            []<class Capture>(Capture& capture, int index) {
                if constexpr (requires { capture.oldStripe; }) capture.oldStripe = (index & 1) ? 1.f : 0.f;
            }(old, i);
            old.entry = state.entries[i];
            for (size_t j = 0; j < result.entries.size(); ++j)
                if (result.entries[j].path == old.entry.path) old.newIndex = static_cast<int>(j);
            result.oldViewport.push_back(std::move(old));
        }
        result.orderHash = DirectoryOrderHash(result.entries);
        result.metadataHash = DirectoryMetadataHash(result.entries);
        PublishDirectoryResult(*state.background, std::move(result));
        ReceiveDirectoryResults(state);
    };
    auto phase = [&](double ms) {
        QueryPerformanceCounter(&state.rowTransitionStartedQpc);
        state.rowTransitionStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*ms/1000.);
        return pixels();
    };
    const auto reference = pixels();
    Check(reference[30] > 80 && reference[50] == 0 && reference[270] > 80,
          "zebra fixture has no independent visible/empty bands");
    diff(1, -1);
    for (double ms : {1., 35., 90., 181.}) {
        const auto drawn = phase(ms);
        for (int y = 5; y < 295; ++y)
            Check(std::abs(drawn[y]-reference[y]) <= 1, "deletion moved or faded the zebra background");
    }
    diff(-1, 2);
    for (double ms : {1., 35., 90., 181.}) {
        const auto drawn = phase(ms);
        for (int y = 5; y < 295; ++y)
            Check(std::abs(drawn[y]-reference[y]) <= 1, "insertion moved or faded the zebra background");
    }
    g_emptyRows = false;
    StopRowTransition(state);
    Check(pixels()[150] > 80 && pixels()[170] == 0, "bounded zebra extent before deletion");
    diff(1, -1);
    if constexpr (requires { state.zebraExtentAnimating; }) {
        pixels(); // Submit the starting extent before advancing its clock.
        StopRowTransition(state);
        Check(state.zebraExtentAnimating && NeedsAnimationFrames(state),
              "foreground cancellation cancelled the background collapse");
        QueryPerformanceCounter(&state.zebraExtentStartedQpc);
        state.zebraExtentStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.037);
        const auto partial = pixels();
        Check(partial[145] > 80 && partial[155] == 0 && partial[30] == reference[30] &&
              partial[50] == 0, "surplus did not collapse only at the trailing edge");
        const float shown = state.zebraExtentPresented;
        diff(1, -1);
        Check(fabsf(state.zebraExtentFrom-shown) < .01f,
              "overlapping deletion restarted background height from a logical row count");
        QueryPerformanceCounter(&state.zebraExtentStartedQpc);
        state.zebraExtentStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.181);
        pixels();
        Check(!state.zebraExtentAnimating, "background collapse did not settle after submission");
        diff(-1, 1);
        Check(state.zebraExtentAnimating && state.zebraExtentTo > state.zebraExtentFrom,
              "insertion did not grow the trailing background extent");
    }
    g_zebra = zebra; g_emptyRows = emptyRows; g_zebraOpacity = opacity;
    g_emptyFolderCaption = std::move(caption);
    StopRowTransition(state);
    if constexpr (requires { state.zebraExtentAnimating; }) state.zebraExtentAnimating = false;
    state.backbuffer = std::move(backbuffer); state.swapChain = std::move(swapChain);
    state.target->SetTarget(state.backbuffer.Get());
    UnregisterAnimationWindow(state.hwnd);
    std::cout << "PASS: GPU zebra pixels stay fixed through delete/insert; independent trailing collapse/growth and overlapping changes\n";
}

template<class State>
static void TestBatchedDeletion(State& state, const std::wstring& fixture) {
    if constexpr (requires { state.rowTransitionPresentedProgress; }) {
        const auto previousTab=state.activeTabHost;
        state.activeTabHost=state.frameHwnd; // The visible private overlay belongs to this frame's active view.
        const auto directory = std::filesystem::path(fixture) / L"animation";
        std::filesystem::create_directory(directory);
        std::vector<std::filesystem::path> files;
        for (int i = 0; i < 64; ++i) {
            files.push_back(directory / (L"row" + std::to_wstring(i) + L".txt"));
            HANDLE file = CreateFileW(files.back().c_str(), GENERIC_WRITE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            Check(file != INVALID_HANDLE_VALUE, "animation fixture creation");
            CloseHandle(file);
        }
        StopRowTransition(state);
        StopScrollAnimation(state);
        StopKeyboardSelectionAnimation(state);
        StopHoverAnimation(state);
        state.sections.clear(); state.rowGroup.clear();
        state.grouping.enabled = state.groupAnimating = false;
        state.columnsView = false;
        state.loaded = false; state.total = 0;
        state.entries.clear(); state.selectedFlags.clear();
        state.selected = -1;
        state.visible = state.inScope = state.detailsViewActive = true;
        state.scrollNow = state.scrollTarget = 0.f;
        state.rowPx = 34.f; state.headerPx = 0;
        state.folderPath = directory.wstring();
        state.background->hwnd = state.hwnd;
        state.sort.explicitOrder = true;
        state.sort.kind = SortKind::Name;
        state.sort.direction = SORT_ASCENDING;
        auto refresh = [&] {
            QueueDirectoryRefresh(state, true);
            auto result = WaitForComplete(*state.background, state.directoryToken);
            PublishDirectoryResult(*state.background, std::move(result));
            ReceiveDirectoryResults(state);
        };
        refresh();
        Check(state.total == 64, "initial animation snapshot");
        UpdateRefreshRate(state);
        auto original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(+[](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
                if (msg == WM_PAINT || msg == kMsgFrame || msg == WM_KEYDOWN ||
                    msg == WM_WINDOWPOSCHANGED) return OverlayProc(hwnd, msg, wp, lp);
                return DefWindowProcW(hwnd, msg, wp, lp);
            })));
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
        SetWindowPos(state.frameHwnd, nullptr, -32000, -32000, 400, 400,
            SWP_NOACTIVATE | SWP_NOZORDER);
        ShowWindow(state.frameHwnd, SW_SHOWNOACTIVATE);
        ShowWindow(state.hwnd, SW_SHOWNOACTIVATE);
        auto frame = [&] {
            SendMessageW(state.hwnd, kMsgFrame, 0, 0);
            UpdateWindow(state.hwnd); // Simulate normal paint delivery after input.
        };
        auto removeRows = [&](int first, int count) {
            for (int i = first; i < first+count; ++i)
                Check(DeleteFileW(state.entries[i].path.c_str()), "batch fixture deletion");
            refresh();
        };
        removeRows(0, 4);
        Check(state.rowTransitionActive && state.total == 60, "batch did not start row movement");
        Sleep(210); // Simulate a busy Shell before its first animation paint.
        Check(!state.rowTransitionStartedQpc.QuadPart, "unpainted transition consumed its duration");
        Check(Eventually([&] { frame(); return state.rowTransitionStartedQpc.QuadPart != 0; }),
              "paced wake did not produce its paint");
        Check(state.rowTransitionPresentedProgress == 0.f, "first submitted deletion frame skipped the start");
        state.rowTransitionStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.045);
        Check(Eventually([&] { frame(); return state.rowTransitionPresentedProgress > 0.f; }),
              "deletion has no intermediate submitted frame");
        const std::wstring survivor = state.entries[0].path;
        const auto before = LayoutRowVisual(state, 0, state.rowTransitionPresentedProgress);
        const float ghostHeight = state.disappearingRows[0].oldHeight *
            (1.f-state.rowTransitionPresentedProgress);
        removeRows(1, 4);
        const int row = GpuRowForNativePath(state, survivor);
        const auto after = LayoutRowVisual(state, row, 0.f);
        Check(row >= 0 && after.top <= before.top+.01f &&
              after.top >= state.headerPx-.01f &&
              fabsf(before.opacity-after.opacity) < .001f,
              "overlapping batch moved backwards instead of sampling current motion");
        Check(state.disappearingRows.size() == 8 &&
              state.disappearingRows[4].oldHeight <= ghostHeight+.01f &&
              state.disappearingRows[4].oldHeight > 0.f && state.rowTransitionStartedQpc.QuadPart,
              "next batch dropped or reset the preceding outgoing rows");
        SetScrollImmediate(state, state.scrollNow);
        MoveScroll(state, state.scrollTarget);
        Check(state.rowTransitionActive, "no-op viewport update cancelled deletion movement");
        BYTE savedKeys[256]{}, neutralKeys[256]{};
        Check(GetKeyboardState(savedKeys) && SetKeyboardState(neutralKeys), "isolated input state");
        if constexpr (requires { state.framePaintPending; }) {
            FrameInputFixture::Session inputQueue;
            const bool perf = g_perf.exchange(true);
            const int paints = state.paintCalls;
            SendMessageW(state.hwnd, kMsgFrame, 0, 0);
            Check(!state.framePaintPending && paints+1 == state.paintCalls,
                  "paced frame waited for queue-idle WM_PAINT instead of completing its paint");
            UpdateWindow(state.hwnd);
            Check(paints+1 == state.paintCalls, "paced paint left a duplicate WM_PAINT invalidation");
            auto markQueued=[&] {
                RegisterAnimationWindow(state.hwnd);
                ExclusiveSrwLock lock(g_frameLock);
                for(auto& frame:g_activeFrames) if(frame.hwnd==state.hwnd) frame.queued=true;
            };
            auto stillQueued=[&] {
                ExclusiveSrwLock lock(g_frameLock);
                for(const auto& frame:g_activeFrames) if(frame.hwnd==state.hwnd) return frame.queued;
                return false;
            };
            for(UINT bits : {QS_KEY,QS_MOUSEMOVE,QS_MOUSEBUTTON,QS_RAWINPUT,QS_TOUCH,QS_POINTER}) {
                FrameInputFixture::pending=bits;
                markQueued();
                const int before=state.paintCalls;
                SendMessageW(state.hwnd,kMsgFrame,0,0);
                Check(state.framePaintPending && state.paintCalls==before && stillQueued() &&
                    GetUpdateRect(state.hwnd,nullptr,FALSE),
                    "paced frame painted ahead of pending hardware input");
                SendMessageW(state.hwnd,kMsgFrame,0,0);
                Check(state.framePaintPending && stillQueued() && state.paintCalls==before,
                    "input yield acknowledged its frame or admitted a duplicate paint");
                const int next=KeyboardNavigationTarget(state,VK_DOWN,true);
                SendMessageW(state.hwnd,WM_KEYDOWN,VK_DOWN,0);
                Check(state.selected==next && state.paintCalls==before,
                    "input yield bypassed logical keyboard selection or painted in its input stack");
                FrameInputFixture::pending=0;
                Check(GetUpdateRect(state.hwnd,nullptr,FALSE),"input consumed the deferred paint region");
                // Native restore notifications can keep normal WM_PAINT at
                // the back of the queue. Actual input completion must finish
                // the held frame while those posts are still waiting.
                constexpr UINT restorePost = WM_APP+0x500;
                for(int n=0;n<128;++n) Check(PostMessageW(state.hwnd,restorePost,n,0),"restore post fixture");
                ResumeInputDeferredFrame(state.hwnd,state.lifetimeToken,state.token,WM_KEYDOWN);
                Check(!state.framePaintPending && !stillQueued() && state.paintCalls==before+1,
                    "input completion did not present/ack exactly one frame before queue idle");
                MSG posted{}; unsigned remaining=0;
                while(PeekMessageW(&posted,state.hwnd,restorePost,restorePost,PM_REMOVE)) ++remaining;
                Check(remaining==128,"input frame continuation drained or lost native posts");
                ResumeInputDeferredFrame(state.hwnd,state.lifetimeToken,state.token,WM_KEYDOWN);
                Check(state.paintCalls==before+1,"input completion presented the held frame twice");
            }
            FrameInputFixture::pending=QS_KEY;
            markQueued(); SendMessageW(state.hwnd,kMsgFrame,0,0);
            Check(state.framePaintPending,"hide fixture did not retain its input-deferred frame");
            SetPresentationVisibility(state.hwnd,false);
            Check(!state.framePaintPending && !stillQueued(),"hiding stranded an input-deferred frame latch");
            FrameInputFixture::pending=0;
            SetPresentationVisibility(state.hwnd,true);
            const int restored=state.paintCalls;
            SendMessageW(state.hwnd,kMsgFrame,0,0);
            Check(!state.framePaintPending && state.paintCalls==restored+1,
                "showing after input yield could not resume normal paced painting");
            g_perf = perf;
            std::cout << "PASS: queued input runs first, completion presents one held frame before native restore posts drain; keyboard/mouse/raw/touch/pointer, deduplication, ack and hidden/show recovery\n";
        }
        const UINT wheelLines = g_wheelLines.exchange(3);
        OverlayProc(state.hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), 0);
        g_wheelLines = wheelLines;
        Check(state.rowTransitionActive && state.animationActive && state.scrollTarget > state.scrollNow,
              "wheel input could not run alongside deletion movement");
        const float progress = state.rowTransitionPresentedProgress;
        const float beforeScroll = state.scrollNow;
        const float top = LayoutRowVisual(state, row, progress).top;
        state.smoothStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.045);
        AdvanceScroll(state);
        Check(state.scrollNow > beforeScroll && fabsf(LayoutRowVisual(state, row, progress).top -
            top + state.scrollNow-beforeScroll) < .01f,
            "collapse attenuated the viewport delta instead of scrolling at full speed");
        Check(Eventually([&] { frame(); return state.rowTransitionStartedQpc.QuadPart != 0; }),
              "retargeted frame did not submit");
        state.rowTransitionStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.2);
        Check(Eventually([&] { frame(); return !state.rowTransitionActive; }),
              "settled submitted frame retained structural animation");
        Check(state.disappearingRows.empty() && state.total == 56,
              "visual ghosts changed the committed logical list");
        SetScrollImmediate(state, 0.f);
        Select(state, 1);
        for (int repeat = 0; repeat < 8; ++repeat) {
            const float beforeKey = AnimatedSelectionRow(state);
            OverlayProc(state.hwnd, WM_KEYDOWN, VK_DOWN, 0);
            Check(state.selected == repeat+2 && state.keyboardSelectionAnimating &&
                  fabsf(state.keyboardSelectionFromRow-beforeKey) < .04f,
                  "repeated arrow key restarted the selection from an obsolete position");
            state.keyboardSelectionStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.012);
            frame();
        }
        StopKeyboardSelectionAnimation(state);
        SetScrollImmediate(state, 0.f);
        const auto bar = Scrollbar(state);
        const int x = static_cast<int>((bar.left+bar.right)*.5f);
        const int y = static_cast<int>(bar.thumbStart+bar.thumbLength*.5f);
        const bool instantScrub = g_instantScrub.exchange(false);
        PointerFixture::Session pointer;
        PointerFixture::At(state.hwnd,POINT{x,y});
        OverlayProc(state.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        Check(state.draggingScrollbar, "thumb grab did not begin");
        float previous = state.scrollNow;
        for (int drag = 1; drag <= 8; ++drag) {
            // Four input samples precede each paint. Time elapses BEFORE
            // retargeting, as with a high polling-rate mouse, not afterward.
            for (int sample = 1; sample <= 4; ++sample) {
                float continued = state.scrollNow;
                if (state.animationActive) {
                    state.smoothStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.008);
                    continued = state.smoothStart + state.smoothDelta *
                        EaseOutCubic(UiTransitionProgress(state, state.smoothStartedQpc));
                }
                PointerFixture::At(state.hwnd,POINT{x,y+(drag-1)*20+sample*5});
                OverlayProc(state.hwnd, WM_MOUSEMOVE, MK_LBUTTON,
                    MAKELPARAM(x, y+(drag-1)*20+sample*5));
                if (state.scrollNow < continued-.5f)
                    std::cout << "RETARGET: discarded " << continued-state.scrollNow
                              << " px before paint, input " << (drag-1)*4+sample << '\n';
                Check(state.scrollNow >= continued-.5f,
                      "thumb retarget discarded elapsed motion between paints");
                Check(state.animationActive && state.scrollTarget > previous,
                      "continuous thumb drag lost its new destination");
            }
            frame();
            Check(state.scrollNow >= previous && state.scrollNow < state.scrollTarget,
                  "thumb drag reversed or snapped instead of retargeting smoothly");
            previous = state.scrollNow;
        }
        PointerFixture::At(state.hwnd,POINT{x,y+160}); PointerFixture::held=false;
        OverlayProc(state.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y+160));
        Check(!state.draggingScrollbar, "thumb release lost");
        const float completedTarget = state.scrollTarget;
        state.smoothStartedQpc.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency*.2);
        MoveScroll(state, completedTarget+50.f);
        Check(state.scrollNow == completedTarget && state.animationActive,
              "completed segment jumped to its replacement target during retarget");
        StopScrollAnimation(state);
        g_instantScrub = instantScrub;
        SetKeyboardState(savedKeys);
        SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
        SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
        ShowWindow(state.hwnd, SW_HIDE); ShowWindow(state.frameHwnd, SW_HIDE);
        state.activeTabHost=previousTab;
        Stop(state.background);
        for (const auto& file : files) std::filesystem::remove(file);
        Check(std::filesystem::remove(directory), "animation fixture cleanup");
        std::cout << "PASS: overlapping deletion batches and ghost continuity; one completed paced paint, concurrent wheel/collapse, repeated arrows and smooth thumb retargeting\n";
    }
}
