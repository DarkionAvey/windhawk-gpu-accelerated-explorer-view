static LRESULT CALLBACK SampledPointerProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT || message == WM_CAPTURECHANGED)
        return OverlayProc(hwnd, message, wp, lp);
    return DefWindowProcW(hwnd, message, wp, lp);
}

static void TestScrollbarItemSteps(ViewState& state) {
    g_instantScrub=true;
    for (int i=0;i<state.total;++i) {
        auto& entry=state.entries[i];
        entry.groupKey=i<23 ? L"first" : i<80 ? L"second" : L"third";
        entry.groupTitle=entry.groupKey;
    }
    for (int mode : {0,1,2}) for (bool grouped : {false,true}) {
        state.gridView=mode==2; state.columnsView=mode==0;
        state.rowPx=state.gridView ? 113.f : 34.f; state.iconPx=state.gridView ? 48.f : 20.f;
        state.grouping.enabled=grouped; RebuildGroupSections(state,false); UpdateColumns(state);
        if (grouped) {
            // A collapsed first group must not expose its hidden item offsets.
            state.sections[0].collapsed=true; state.sections[0].fraction=0;
            LayoutGroupSections(state);
        }
        SetScrollImmediate(state,0);
        const auto bar=Scrollbar(state);
        const POINT down{LONG((bar.left+bar.right)*.5f),LONG(bar.thumbStart+bar.thumbLength*.5f)};
        PointerFixture::At(state.hwnd,down);
        Check(BeginScrollbarInteraction(state,down.x,down.y) && state.draggingScrollbar &&
            GetCapture()==state.hwnd,"item-step test did not grab the vertical thumb");
        const auto scrubTo=[&](float offset) {
            const auto geometry=Scrollbar(state);
            ScrubScrollbar(state,geometry.top+state.scrollbarGrabY+geometry.travel*offset/geometry.scrollRange);
            Check(state.scrollNow==state.scrollTarget && !state.animationActive,
                "instant item scrub retained smooth viewport motion");
        };
        if (!grouped) {
            for (int line : {1,2,8}) {
                const float offset=DocumentRowTop(state,line*state.itemsPerRow);
                scrubTo(offset+state.rowPx*.1f);
                Check(fabsf(state.scrollNow-offset)<.1f,"instant scrub did not align to an item line");
                scrubTo(offset+state.rowPx*.4f);
                Check(fabsf(state.scrollNow-offset)<.1f,"sub-item thumb motion moved a partial item");
                scrubTo(offset+state.rowPx*.6f);
                Check(fabsf(state.scrollNow-offset-state.rowPx)<.1f,"thumb crossing did not advance one item line");
            }
        } else {
            const float heading=GroupHeadingHeight(state);
            scrubTo(heading*.25f);
            Check(state.scrollNow==0,"collapsed heading scrub stopped between item boundaries");
            scrubTo(heading*.75f);
            Check(fabsf(state.scrollNow-state.sections[1].documentTop)<.1f,
                "collapsed heading scrub entered hidden rows");
            for (size_t group=1;group<state.sections.size();++group) {
                const auto& section=state.sections[group];
                const float offset=DocumentRowTop(state,section.first+state.itemsPerRow)-heading;
                const float step=DisplayRowHeight(state,section.first);
                scrubTo(offset+step*.1f);
                Check(fabsf(state.scrollNow-offset)<.1f,"grouped scrub ignored its item origin or sticky heading");
                scrubTo(offset+step*.4f);
                Check(fabsf(state.scrollNow-offset)<.1f,"grouped sub-item thumb motion moved a partial item");
                scrubTo(offset+step*.6f);
                Check(fabsf(state.scrollNow-offset-step)<.1f,"grouped scrub did not advance one item line");
            }
        }
        float previous=0;
        const float maximum=MaxScrollPx(state);
        for (int sample=0;sample<=128;++sample) {
            scrubTo(maximum*sample/128.f);
            Check(state.scrollNow>=previous-.01f,"item snapping reversed a forward thumb drag");
            previous=state.scrollNow;
        }
        Check(state.scrollNow==maximum,"item snapping made the final partial row unreachable");
        ReleaseCapture();
    }
    state.gridView=false; state.columnsView=true; state.rowPx=34; state.iconPx=20;
    state.grouping.enabled=false; RebuildGroupSections(state,false); UpdateColumns(state); SetScrollImmediate(state,0);
    // The option belongs to grabbing the thumb; a track click and wheel still
    // take the existing smooth path, even with instant item scrubbing enabled.
    const auto bar=Scrollbar(state);
    Check(BeginScrollbarInteraction(state,int((bar.left+bar.right)*.5f),int(bar.top+bar.travel*.7f)) &&
        !state.draggingScrollbar && state.animationActive && state.scrollNow<state.scrollTarget,
        "instant item scrubbing changed vertical track-click animation");
    SetScrollImmediate(state,0);
    BYTE saved[256]{},plain[256]{}; GetKeyboardState(saved); SetKeyboardState(plain);
    OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0);
    SetKeyboardState(saved);
    Check(state.animationActive && state.scrollNow<state.scrollTarget,"instant item scrubbing disabled smooth wheel scrolling");
    SetScrollImmediate(state,0);
}

static void TestSampledPointer() {
    // These direct frame calls model a compositor wake with no queued input.
    // Ambient desktop input must not leave a deferred WM_PAINT outstanding
    // between samples. Input-yield behavior has its own substituted-queue test.
    FrameInputFixture::Session inputQueue;
    ViewState state;
    state.hwnd = IconTestWindow();
    state.frameHwnd = state.activeTabHost = state.hwnd;
    state.background->hwnd = state.hwnd;
    state.visible = state.inScope = state.loaded = state.detailsViewActive = state.columnsView = true;
    state.featureInitializationStep = 5;
    state.rowPx = 34.f; state.headerPx = 26; state.fontPx = 16.f; state.iconPx = 20.f;
    state.total = 793; state.entries.resize(state.total); state.selectedFlags.resize(state.total);
    for (auto& entry : state.entries) { entry.name = L"item"; entry.detailsReady = true; }
    ExplorerColumn name; name.key = PKEY_FileName; name.title = L"Name"; name.width = 300;
    state.columns = {name}; UpdateColumns(state); UpdateRefreshRate(state);
    Check(EnsureTarget(state), "sampled pointer GPU target");
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
    const auto original = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(SampledPointerProc)));
    const bool instant = g_instantScrub.load(), smooth = g_smooth.exchange(true);
    PointerFixture::enabled = PointerFixture::held = true;
    auto frame = [&] { OverlayProc(state.hwnd, kMsgFrame, 0, 0); };
    for (bool snapped : {false, true}) {
        g_instantScrub = snapped;
        SetScrollImmediate(state, 0);
        const auto bar = Scrollbar(state);
        POINT down{LONG((bar.left+bar.right)*.5f), LONG(bar.thumbStart+bar.thumbLength*.5f)};
        PointerFixture::At(state.hwnd, down);
        Check(BeginScrollbarInteraction(state, down.x, down.y) && GetCapture() == state.hwnd &&
            NeedsAnimationFrames(state), "initial stationary grab did not retain sampling frames");
        // Withhold every WM_MOUSEMOVE for longer than the logged 128-146 ms
        // gaps. Each compositor wake must still consume the newest cursor.
        for (int i = 1; i <= 24; ++i) {
            const POINT point{down.x, down.y + (i <= 12 ? i : 24-i) * 12};
            PointerFixture::At(state.hwnd, point);
            frame();
            const auto geometry = Scrollbar(state);
            const float raw = ScrollFractionAt(geometry, float(point.y), state.scrollbarGrabY) * geometry.scrollRange;
            const float expected = snapped ? std::round(raw/state.rowPx)*state.rowPx : raw;
            Check(fabsf(state.scrollTarget-expected) < .1f, "frame held a stale thumb destination without mouse messages");
            if (snapped)
                Check(fabsf(state.scrollNow-expected)<.1f && !state.animationActive,
                    "instant vertical scrub retained viewport easing");
            else if (i==1)
                Check(state.animationActive && state.scrollNow<state.scrollTarget,
                    "smooth vertical scrub jumped instead of animating");
            const float target = state.scrollTarget;
            OverlayProc(state.hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(down.x, down.y));
            Check(state.scrollTarget == target, "old queued mouse move rewound a sampled thumb");
        }
        PointerFixture::At(state.hwnd, POINT{down.x, down.y+200}); frame();
        const auto clock = state.smoothStartedQpc.QuadPart;
        frame();
        Check(state.smoothStartedQpc.QuadPart == clock, "stationary sample restarted scroll easing");
        const float target = state.scrollTarget;
        PointerFixture::held = false;
        PointerFixture::At(state.hwnd, POINT{down.x, down.y+300}); frame();
        Check(state.scrollTarget == target, "sampler followed cursor after physical button release");
        ReleaseCapture(); PointerFixture::held = true;
        Check(!PointerDragActive(state) && !state.capturedPointer, "capture release retained a sampling gesture");
    }
    TestScrollbarItemSteps(state);
    // Horizontal dragging is always direct and never uses item snapping,
    // regardless of the vertical fast-scroll option or reading direction.
    state.columns[0].width = 1200;
    for (bool verticalInstant : {false,true}) for (bool rtl : {false, true}) {
        g_instantScrub=verticalInstant;
        state.rtlLayout = rtl; SetHorizontalScroll(state,0); UpdateColumns(state);
        const auto bar = HorizontalBar(state);
        const int logical = int(bar.thumbStart+bar.thumbLength*.5f);
        const POINT down{LogicalX(state, logical), LONG((bar.top+bar.bottom)*.5f)};
        PointerFixture::At(state.hwnd, down);
        Check(BeginScrollbarInteraction(state, down.x, down.y) && state.draggingHorizontal,
            "horizontal sample grab");
        const POINT moved{down.x+(rtl ? -60 : 60), down.y};
        PointerFixture::At(state.hwnd, moved); frame();
        const auto geometry = HorizontalBar(state);
        const float expected = ScrollFractionAt(geometry, float(LogicalX(state, moved.x)),
            state.horizontalGrabOffset)*geometry.scrollRange;
        Check(expected > 0 && fabsf(state.horizontalTarget-expected) < .1f,
            "horizontal/RTL capture did not consume sampled cursor");
        Check(fabsf(state.horizontalScroll-expected)<.1f && !state.horizontalAnimating,
            "vertical instant scrub setting changed direct horizontal/RTL dragging");
        for (int sample=1;sample<=8;++sample) {
            const POINT point{moved.x+(rtl ? -1 : 1)*sample*4,moved.y};
            PointerFixture::At(state.hwnd,point);
            OverlayProc(state.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(point.x,point.y));
            const auto geometry=HorizontalBar(state);
            const float offset=ScrollFractionAt(geometry,float(LogicalX(state,point.x)),
                state.horizontalGrabOffset)*geometry.scrollRange;
            Check(fabsf(state.horizontalScroll-offset)<.1f && !state.horizontalAnimating,
                "horizontal thumb introduced easing or item snapping during rapid input");
        }
        // Both endpoint clamps remain reachable in either direction/layout.
        for (bool end : {true,false}) {
            const auto geometry=HorizontalBar(state);
            const int logical=int(end ? geometry.right+50 : geometry.left-50);
            PointerFixture::At(state.hwnd,{LogicalX(state,logical),down.y}); frame();
            Check(state.horizontalTarget==(end ? geometry.scrollRange : 0.f),
                "horizontal scrub could not reach an endpoint");
            if (state.horizontalAnimating) {
                state.horizontalStartedQpc.QuadPart-=LONGLONG(state.qpcFrequency*.2); frame();
            }
            Check(state.horizontalScroll==state.horizontalTarget && !state.horizontalAnimating,
                "horizontal endpoint retained motion or missed its destination");
        }
        ReleaseCapture();
    }
    // Smooth scrolling is the master switch, even with instant scrubbing off.
    g_smooth=false; g_instantScrub=false;
    SetHorizontalScroll(state,0);
    auto horizontal=HorizontalBar(state); state.horizontalGrabOffset=horizontal.thumbLength*.5f;
    ScrubHorizontal(state,LogicalX(state,int(horizontal.left+horizontal.thumbLength*.5f+30)));
    Check(state.horizontalScroll>0 && state.horizontalScroll==state.horizontalTarget && !state.horizontalAnimating,
        "disabled smooth scrolling retained horizontal scrub easing");
    SetScrollImmediate(state,0);
    const auto vertical=Scrollbar(state); state.scrollbarGrabY=vertical.thumbLength*.5f;
    ScrubScrollbar(state,vertical.top+state.scrollbarGrabY+30);
    Check(state.scrollNow>0 && state.scrollNow==state.scrollTarget && !state.animationActive,
        "disabled smooth scrolling retained vertical scrub easing");
    g_smooth=true;
    state.rtlLayout = false; state.horizontalScroll = state.horizontalTarget = 0;
    state.columns[0].width = 300; UpdateColumns(state); SetScrollImmediate(state, 0);
    PrepareMarquee(state, POINT{40, 80}, false); SetCapture(state.hwnd);
    PointerFixture::At(state.hwnd, POINT{150, 250}); frame();
    Check(state.marqueeActive && state.marqueeCurrent.x == 150 && state.marqueeCurrent.y == 250 &&
        std::count(state.selectedFlags.begin(), state.selectedFlags.end(), 1) > 0,
        "marquee did not follow sampled movement without WM_MOUSEMOVE");
    ReleaseCapture();
    Check(!state.marqueeActive && std::count(state.selectedFlags.begin(), state.selectedFlags.end(), 1) == 0,
        "sampled marquee capture loss did not restore selection");
    // An unrelated tab must never consume the captured tab's cursor.
    ViewState other; other.hwnd = MessageWindow(); other.visible = other.detailsViewActive = true;
    other.draggingScrollbar = true; other.scrollTarget = 321.f;
    UpdateCapturedPointer(other);
    Check(other.scrollTarget == 321.f && !other.capturedPointer, "another tab consumed this capture");
    DestroyWindow(other.hwnd); Stop(other.background);
    PointerFixture::enabled = false;
    g_instantScrub = instant; g_smooth = smooth;
    UnregisterAnimationWindow(state.hwnd);
    SetWindowLongPtrW(state.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original));
    SetWindowLongPtrW(state.hwnd, GWLP_USERDATA, 0);
    Stop(state.background); DestroyWindow(state.hwnd);
    std::cout << "PASS: sampled captured drags survive withheld/stale mouse messages; vertical instant item steps in Details/List/grid and grouped/collapsed layouts, track/wheel smoothing, independent direct LTR/RTL horizontal dragging, endpoints, release, marquee and tab isolation\n";
}
