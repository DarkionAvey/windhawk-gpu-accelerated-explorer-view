// Opt-in native DWM check (--swipe-compositor). Unlike the offscreen bitmap
// flattening tests, this reads the actual composed pixels of its own small,
// nonactivating window. Native Ninput supplies every gesture; desktop input,
// clipboard and real keyboard focus are untouched.
static void TestSwipeCompositor() {
    using namespace TouchInputTest;
    const auto dpiContext=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto fonts=g_writeFactory;
    const auto palette=g_swipePalette.load();
    const bool history=g_touchpadSwipeNavigation.exchange(true);
    struct Restore {
        DPI_AWARENESS_CONTEXT dpi; ComPtr<IDWriteFactory> fonts; unsigned long long palette; bool history;
        ~Restore() {g_writeFactory=fonts; g_swipePalette=palette; g_touchpadSwipeNavigation=history; SetThreadDpiAwarenessContext(dpi);}
    } restore{dpiContext,fonts,palette,history};
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"native compositor fonts");
    using WaitForClock=DWORD(WINAPI*)(UINT,const HANDLE*,DWORD);
    const auto clock=reinterpret_cast<WaitForClock>(::GetProcAddress(GetModuleHandleW(L"dcomp.dll"),"DCompositionWaitForCompositorClock"));
    const HWND foreground=::GetForegroundWindow();
    MONITORINFO monitor{sizeof(monitor)};
    Check(GetMonitorInfoW(MonitorFromWindow(foreground,MONITOR_DEFAULTTOPRIMARY),&monitor)!=FALSE,"native compositor monitor");
    Device input; TouchFixture::Session contacts; StyleFixture::Session style;
    FrameInputFixture::Session queue; PointerFixture::Session pointer; ModifierFixture::Session modifiers;
    for(int count:{0,20}) for(bool rtl:{false,true}) {
        PresentationFixture::Tab tab(L"native composed swipes",count,rtl,false,false,360,360);
        auto& state=tab.state; Install(state);
        FocusFixture::Session focus(tab.frame,state.hwnd);
        Check(SetWindowPos(tab.frame,HWND_TOPMOST,monitor.rcWork.right-380,monitor.rcWork.bottom-380,0,0,
            SWP_NOACTIVATE|SWP_NOSIZE|SWP_SHOWWINDOW)!=FALSE,"nonactivating compositor window placement");
        StyleFixture::dpi=::GetDpiForWindow(state.hwnd);
        Check(EnsureTarget(state),"native compositor target");
        struct Graphics {ViewState& state; ~Graphics() {StopTouchpad(state); DropTarget(state);}} graphics{state};
        // A private blue content plane makes actual foreground coverage
        // unambiguous, independently of rows, selection and the user's theme.
        ViewState::ModePlane background;
        Check(CreateColumnPlane(state,background,{360,360}),"native compositor background allocation");
        state.target->SetTarget(background.bitmap.Get()); state.target->BeginDraw();
        state.target->Clear(D2D1::ColorF(0.f,0.f,1.f,1.f));
        Check(SUCCEEDED(state.target->EndDraw()) && PublishModePlane(state,background) &&
            SUCCEEDED(state.visual->SetContent(background.surface.Get())) && SUCCEEDED(state.composition->Commit()),
            "native compositor background publication");
        g_swipePalette=static_cast<unsigned long long>(RGB(255,0,0)) |
            (static_cast<unsigned long long>(RGB(255,255,255))<<24) | (255ULL<<48);
        const auto pixel=[&](POINT point) {
            Check(MapUiPoint(state.hwnd,nullptr,point),"native compositor pixel mapping");
            Check(WindowFromPoint(point)==state.hwnd,"native compositor fixture is occluded");
            HDC screen=GetDC(nullptr); const auto color=GetPixel(screen,point.x,point.y); ReleaseDC(nullptr,screen);
            Check(color!=CLR_INVALID,"native compositor screen read"); return color;
        };
        const auto frame=[&] {
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message); DispatchMessageW(&message);}
            Check(::GetForegroundWindow()==foreground,"native compositor fixture changed foreground ownership");
            AdvanceSwipeFeedback(state); Check(RenderSwipeFeedback(state),"native compositor foreground submission");
            if(clock) Check(clock(0,nullptr,1000)==WAIT_OBJECT_0,"native compositor heartbeat");
            else Check(SUCCEEDED(::DwmFlush()),"native compositor flush");
        };
        Check(Eventually([&] {frame(); return pixel({180,180})==RGB(0,0,255);}),"native compositor content did not display");
        // Reuse one foreground visual across successful fades, opposite edges,
        // cancellation and the following successful gesture, in both layouts.
        for(unsigned gesture=0;gesture<5;++gesture) {
            const bool backward=gesture<2 || gesture==4,commit=gesture!=3;
            Reset(state,input,true);
            SetHorizontalScroll(state,backward ? 0.f:state.columnContentWidth);
            const int sign=backward!=rtl ? 1:-1;
            POINT first{sign>0 ? 60:300,160},second{sign>0 ? 100:260,240};
            input.Next(); input.Send(state,{input.Packet(state,input.primary,first,down),input.Packet(state,input.primary+1,second,down)});
            const int distance=int(SwipeCommitDistance(state)*(commit ? 1.25f:.75f));
            first.x+=sign*distance; second.x+=sign*distance;
            input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
            Check(state.swipeFeedbackActive,"native compositor gesture was not recognized");
            POINT body{};
            const bool visible=Eventually([&] {
                frame(); LARGE_INTEGER now{}; ::QueryPerformanceCounter(&now);
                const auto layout=LayoutSwipeIndicator(state,state.swipeIndicator,now,360.f,state.headerPx,360.f,true);
                body={LONG(std::lround(layout.l+layout.w*.25f)),LONG(std::lround(layout.cy+layout.h*.25f))};
                return layout.w>8.f && body.x>=0 && body.x<360 && pixel(body)==RGB(255,0,0);
            });
            std::cout<<"COMPOSED SWIPE rows="<<count<<" rtl="<<rtl<<" gesture="<<gesture<<" visible="<<visible<<std::endl;
            Check(visible,"reused foreground visual did not display after its previous fade/cancel");
            input.Next(); input.Send(state,{input.Packet(state,input.primary,first,up),input.Packet(state,input.primary+1,second,up)});
            Check(state.swipeIndicator.committed==commit,"native compositor release did not match its threshold");
            Check(Eventually([&] {
                frame(); return !state.swipeIndicator.direction && pixel(body)==RGB(0,0,255) &&
                    (commit || !state.swipeComposition[0].visible);
            }),"native compositor fade/retraction did not remove its actual pixels");
        }
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
    }
    Check(::GetForegroundWindow()==foreground,"native compositor probe changed real focus");
    std::cout<<"PASS: actual DWM pixels across 20 native Back/Forward swipes, repeated success fades, cancellation and reuse; empty/populated, LTR/RTL, nonactivating private windows\n";
}
