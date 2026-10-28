// Private calibrated contacts enter the production Ninput -> shared geometry/
// selection/Shell path through its native API boundary. No Windows messages or
// recognizer outputs are manufactured; OS packet delivery needs a device.
#include "touch-calibration.h"
namespace TouchInputTest {
struct Device : TouchCalibration {
    UINT32 frame=0,primary=0;
    DWORD tick=0,startTick=0;
    LARGE_INTEGER frequency{},startQpc{},frameQpc{};
    bool realTime=false,performance=true;
    Device() {
        Check(device!=nullptr,"private recognizer digitizer calibration");
        QueryPerformanceFrequency(&frequency); ResetTime();
    }
    void ResetTime(bool real=false) {
        realTime=real; tick=startTick=GetTickCount(); QueryPerformanceCounter(&startQpc); frameQpc=startQpc;
    }
    void Next(DWORD ms=16) {
        ++frame; tick=realTime ? GetTickCount() : tick+ms;
        if(realTime) QueryPerformanceCounter(&frameQpc);
    }
    POINTER_INFO Packet(ViewState& state,UINT32 id,POINT client,POINTER_FLAGS flags) {
        POINTER_INFO p{}; p.pointerType=PT_TOUCH; p.pointerId=id; p.frameId=frame;
        p.sourceDevice=device; p.hwndTarget=state.hwnd; p.historyCount=1;
        p.pointerFlags=flags|POINTER_FLAG_CONFIDENCE|(id==primary ? POINTER_FLAG_PRIMARY : 0);
        POINT screen=client; MapUiPoint(state.hwnd,nullptr,screen);
        p.ptPixelLocation=p.ptPixelLocationRaw=screen;
        p.ptHimetricLocation=p.ptHimetricLocationRaw=Himetric(screen);
        p.dwTime=tick;
        if(performance) p.PerformanceCount=realTime ? frameQpc.QuadPart :
            startQpc.QuadPart+UINT64(tick-startTick)*frequency.QuadPart/1000;
        p.ButtonChangeType=(flags&POINTER_FLAG_DOWN) ? POINTER_CHANGE_FIRSTBUTTON_DOWN :
            (flags&POINTER_FLAG_UP) ? POINTER_CHANGE_FIRSTBUTTON_UP : POINTER_CHANGE_NONE;
        return p;
    }
    void Send(ViewState& state,std::initializer_list<POINTER_INFO> packets) {
        Check(StartTouchRecognizer(state),"native touch context initialization");
        Check(state.visible,"native input test continued after fallback");
        FeedTouchFrames(state,state.touchRecognizer,1,UINT32(packets.size()),packets.begin());
        Check(state.visible,"native recognizer/control unexpectedly fell back to native view");
    }
    void SendMany(ViewState& state,const std::vector<POINTER_INFO>& packets) {
        Check(StartTouchRecognizer(state),"native multi-contact context initialization");
        FeedTouchFrames(state,state.touchRecognizer,1,UINT32(packets.size()),packets.data());
        Check(state.visible,"native multi-contact recognizer unexpectedly fell back");
    }
};
constexpr POINTER_FLAGS down=POINTER_FLAG_NEW|POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON;
constexpr POINTER_FLAGS move=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON;
constexpr POINTER_FLAGS up=POINTER_FLAG_UP;
static LRESULT CALLBACK Owner(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if((msg==WM_TIMER && wp==kTouchHoldTimerId) || msg==kMsgTouchDrag || msg==WM_CAPTURECHANGED)
        return OverlayProc(hwnd,msg,wp,lp);
    return DefWindowProcW(hwnd,msg,wp,lp);
}
void Install(ViewState& state) {
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    SetWindowLongPtrW(state.hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Owner));
}
void Reset(ViewState& state,Device& input,bool real=false) {
    StopTouch(state); state.touchRecognizer.reset(); ResetMouseGestureForNavigation(state);
    input.ResetTime(real); input.primary+=10;
    MSG message{}; while(PeekMessageW(&message,state.hwnd,kMsgTouchDrag,kMsgTouchDrag,PM_REMOVE)) {}
}
void Pump(ViewState& state) {
    MSG message{};
    while(PeekMessageW(&message,state.hwnd,kMsgTouchDrag,kMsgTouchDrag,PM_REMOVE))
        OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
    while(PeekMessageW(&message,state.hwnd,WM_TIMER,WM_TIMER,PM_REMOVE))
        if(message.wParam==kTouchHoldTimerId) OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
}
void Held(ViewState& state) {
    Check(state.touch.holdDeadline && state.touch.holdDeadlineQpc.QuadPart,"native hold deadline was not armed");
    Check(Eventually([&]{Pump(state); return state.touch.holding;},3000),"native hold deadline produced no holding event");
}
// Exercise the real owner -> immutable presentation -> GPU Paint boundary.
// Read pixels in empty content, so selection highlights cannot satisfy the
// marquee assertion. Focus remains substituted; no desktop input is injected.
struct RenderedTouch {
    ViewState renderer;
    std::shared_ptr<PresentationLane> lane=std::make_shared<PresentationLane>();
    std::vector<BYTE> Capture(ViewState& input) {
        const auto model=CapturePresentationModel(input);
        Check(ApplyPresentationModel(renderer,*model,lane),"touch presentation model rejected");
        Check(EnsureTarget(renderer),"touch presentation GPU target");
        auto swap=std::move(renderer.swapChain);
        // Offscreen readback does not Present/replenish DXGI's frame slots.
        const auto waitable=std::exchange(renderer.frameLatencyWaitable,nullptr);
        Paint(renderer); renderer.frameLatencyWaitable=waitable; renderer.swapChain=std::move(swap);
        const auto size=renderer.backbuffer->GetPixelSize();
        ComPtr<ID2D1Bitmap1> cpu;
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            renderer.backbuffer->GetPixelFormat());
        Check(SUCCEEDED(renderer.target->CreateBitmap(size,nullptr,0,&properties,&cpu)) &&
            SUCCEEDED(cpu->CopyFromBitmap(nullptr,renderer.backbuffer.Get(),nullptr)),"touch pixel readback");
        D2D1_MAPPED_RECT map{};
        Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"touch pixel mapping");
        std::vector<BYTE> pixels(size_t(size.width)*size.height*4);
        for(UINT32 y=0;y<size.height;++y) memcpy(pixels.data()+size_t(y)*size.width*4,map.bits+y*map.pitch,size.width*4);
        cpu->Unmap(); return pixels;
    }
    ~RenderedTouch() {DropTarget(renderer); Stop(renderer.background);}
};

void MarqueePixels(Device& input,const std::wstring& fixture) {
    for(bool grid:{false,true}) for(bool rtl:{false,true}) {
        PresentationFixture::Tab tab(L"touch marquee pixels",4,rtl,grid);
        auto& view=tab.state;
        auto* selection=SelectionUiFixture::Setup(tab,fixture); Install(view);
        HWND chrome=CreateWindowExW(0,L"STATIC",L"private Explorer chrome",WS_CHILD,0,0,10,10,
            tab.frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(chrome!=nullptr,"touch chrome focus fixture");
        FocusFixture::Session focus(tab.frame,chrome);
        RenderedTouch frame;
        RECT client{}; ViewClientRect(view,&client);
        Check(MaxScrollPx(view)==0.f,"marquee pixel fixture must fit vertically");
        const int blank=std::max(int(view.headerPx+DocumentHeight(view)+8),int(view.headerPx+ViewHeight(view)*.5f));
        Check(blank+8<client.bottom-60,"marquee pixel sample is not empty content");
        for(bool straight:{false,true}) {
            Reset(view,input); FocusFixture::focus=chrome;
            const auto before=frame.Capture(view);
            POINT origin{LogicalX(view,100),client.bottom-60},end{LogicalX(view,straight ? 100:200),LONG(view.headerPx+4)};
            input.Next(); input.Send(view,{input.Packet(view,input.primary,origin,down)});
            input.Next(); input.Send(view,{input.Packet(view,input.primary,end,move)});
            Check(view.marqueeActive && GetCapture()==view.hwnd,"touch pixel marquee did not start");
            const auto during=frame.Capture(view);
            const int x=straight ? origin.x-2:(origin.x+end.x)/2;
            unsigned changed=0;
            for(int y=blank;y<blank+8;++y) for(int p=x;p<x+5;++p) {
                const size_t offset=(size_t(y)*client.right+p)*4;
                changed+=memcmp(before.data()+offset,during.data()+offset,4)!=0;
            }
            Check(changed>=4,"captured touch selected items but marquee has no GPU pixels before any list click");
            Check(OverlayHasInputFocus(view) && FocusFixture::focus==view.nativeHwnd,
                "touch marquee failed the shared native list focus handoff");
            FocusFixture::foreground=nullptr;
            const auto background=frame.Capture(view);
            for(int y=blank;y<blank+8;++y) for(int p=x;p<x+5;++p) {
                const size_t offset=(size_t(y)*client.right+p)*4;
                Check(memcmp(before.data()+offset,background.data()+offset,4)==0,
                    "background touch marquee bypassed input focus suppression");
            }
            FocusFixture::foreground=tab.frame;
            input.Next(); input.Send(view,{input.Packet(view,input.primary,end,up)});
            WaitForPreparedSelection(view);
            Check(!view.marqueeActive && GetCapture()!=view.hwnd && !selection->selected.empty(),
                "visible touch marquee failed native selection commit");
        }
        StopTouchpad(view); SetWindowLongPtrW(view.hwnd,GWLP_USERDATA,0); DestroyWindow(chrome);
    }
}

void PinchThresholdPixels(Device& input,const std::wstring& fixture) {
    for(bool grid:{false,true}) for(bool rtl:{false,true}) for(int direction:{-1,1}) {
        PresentationFixture::Tab tab(L"touch pinch threshold pixels",64,rtl,grid);
        auto& view=tab.state;
        SelectionUiFixture::Setup(tab,fixture); Install(view);
        FocusFixture::Session focus(tab.frame,view.hwnd);
        Reset(view,input); view.rowHeight96[grid]=40; view.preciseRowHeight96[grid]=0.; UpdateMetrics(view);
        SetScrollImmediate(view,100);
        POINT a{300,220},b{420,220};
        input.Next(); input.Send(view,{input.Packet(view,input.primary,a,down)});
        input.Next(); input.Send(view,{input.Packet(view,input.primary,a,move),input.Packet(view,input.primary+1,b,down)});
        RenderedTouch frame;
        std::vector<BYTE> before;
        Check(Eventually([&]{before=frame.Capture(view); return !frame.renderer.textRasterPending;}),
            "touch pinch reference text preparation");
        const auto beforeRow=view.rowPx, beforeIcon=view.iconPx, beforeFont=view.fontPx, beforeScroll=view.scrollNow;
        while(view.touch.gesture==TouchGesture::Pending && b.x-a.x>60 && b.x-a.x<180) {
            a.x-=direction; b.x+=direction;
            input.Next(); input.Send(view,{input.Packet(view,input.primary,a,move),input.Packet(view,input.primary+1,b,move)});
            Check(view.rowPx==beforeRow && view.iconPx==beforeIcon && view.fontPx==beforeFont && view.scrollNow==beforeScroll,
                "gradual pinch resized or scrolled while crossing recognition slop");
        }
        Check(view.touch.gesture==TouchGesture::Pinch,"gradual radial movement never committed to pinch");
        const auto committed=frame.Capture(view);
        RECT client{}; ViewClientRect(view,&client);
        const auto differences=[&](const auto& pixels) {
            unsigned count=0;
            for(int y=int(view.headerPx)+4;y<client.bottom-60;++y) for(int x=40;x<client.right-40;++x) {
                const size_t p=(size_t(y)*client.right+x)*4;
                count+=memcmp(before.data()+p,pixels.data()+p,4)!=0;
            }
            return count;
        };
        Check(differences(committed)==0,"pinch recognition changed displayed row/icon/text pixels");
        const int span=b.x-a.x;
        a.x-=direction; b.x+=direction;
        input.Next(); input.Send(view,{input.Packet(view,input.primary,a,move),input.Packet(view,input.primary+1,b,move)});
        const double expected=40.*(b.x-a.x)/span;
        Check(fabs(view.preciseRowHeight96[grid]-expected)<.05 && differences(frame.Capture(view))>0,
            "post-threshold movement did not scale fractional metrics and actual GPU pixels");
        input.Next(); input.Send(view,{input.Packet(view,input.primary,a,up),input.Packet(view,input.primary+1,b,up)});
        StopTouchpad(view); SetWindowLongPtrW(view.hwnd,GWLP_USERDATA,0);
    }
}

void ScrollbarInteractions(Device& input,const std::wstring& fixture) {
    for(UINT dpi:{96u,192u}) for(bool rtl:{false,true}) {
        StyleFixture::Session metrics; StyleFixture::dpi=dpi;
        PresentationFixture::Tab tab(L"touch scrollbar pixels",64,rtl);
        auto& view=tab.state;
        view.columns[0].width=1400; UpdateColumns(view);
        Check(HorizontalBar(view).usable,"settled scrollbar fixture overflow");
        SelectionUiFixture::Setup(tab,fixture); Install(view);
        FocusFixture::Session focus(tab.frame,view.hwnd);
        AnimationClockFixture::Freeze clock;
        const auto advance=[&](double fraction) {
            AnimationClockFixture::instant.QuadPart+=LONGLONG(view.qpcFrequency*kUiTransitionDurationMs*fraction/1000.);
            AdvanceAnimationFrame(view,false);
        };
        Reset(view,input);
        POINT body{100,LONG(view.headerPx+view.rowPx*.5f)};
        input.Next(); input.Send(view,{input.Packet(view,input.primary,body,down)});
        input.Next(); input.Send(view,{input.Packet(view,input.primary,body,up)});
        Check(view.pointingDevice==PointingDevice::Touch && view.touchScrollbarTo[0]==0.f && view.touchScrollbarTo[1]==0.f,
            "touching list content enlarged idle scrollbars");
        for(bool horizontal:{false,true}) {
            Reset(view,input); SetScrollImmediate(view,0); SetHorizontalScroll(view,0); advance(1.1);
            RenderedTouch frame;
            const auto smallV=Scrollbar(view),smallH=HorizontalBar(view);
            const float viewport=ListViewportWidth(view);
            const auto before=frame.Capture(view);
            const auto target=horizontal ? HorizontalBar(view,true):Scrollbar(view,true);
            POINT point{LogicalX(view,LONG(horizontal ? target.thumbStart+target.thumbLength*.5f : (target.left+target.right)*.5f)),
                LONG(horizontal ? (target.top+target.bottom)*.5f : target.thumbStart+target.thumbLength*.5f)};
            Check(!HitScrollbar(horizontal ? smallH:smallV,float(LogicalX(view,point.x)),float(point.y),0),
                "touch press must land outside the small idle visual");
            PointerFixture::At(view.hwnd,{1,1});
            input.Next(); input.Send(view,{input.Packet(view,input.primary,point,down)});
            Check(view.pointingDevice==PointingDevice::Touch && view.touch.control &&
                (horizontal ? view.draggingHorizontal:view.draggingScrollbar) && GetCapture()==view.hwnd,
                "first touch missed the large idle scrollbar hit target");
            Check(view.touchScrollbarTo[horizontal ? 1:0]==1.f && view.touchScrollbarTo[horizontal ? 0:1]==0.f,
                "touch enlarged the unrelated scrollbar");
            Check(view.scrollNow==0.f && view.horizontalScroll==0.f,"expanded touch hit target moved content on DOWN");
            Check(Scrollbar(view).left==smallV.left && HorizontalBar(view).top==smallH.top,"touch scrollbar growth snapped");
            advance(.5);
            const auto middleV=Scrollbar(view),middleH=HorizontalBar(view);
            Check(horizontal ? middleH.top<smallH.top && middleV.left==smallV.left :
                middleV.left<smallV.left && middleH.top==smallH.top,"per-axis growth failed");
            frame.Capture(view);
            Check(frame.renderer.touchScrollbarTo==view.touchScrollbarTo &&
                (horizontal ? HorizontalBar(frame.renderer).top==middleH.top : Scrollbar(frame.renderer).left==middleV.left),
                "presentation model lost the independent touch size animation");
            input.Next(); input.Send(view,{input.Packet(view,input.primary,point,up)});
            Check(view.pointingDevice==PointingDevice::Touch && !view.draggingHorizontal && !view.draggingScrollbar &&
                GetCapture()!=view.hwnd && view.touchScrollbarTo[0]==0.f && view.touchScrollbarTo[1]==0.f,
                "release kept the idle bar large or discarded touch ownership");
            Check(Scrollbar(view).left==middleV.left && HorizontalBar(view).top==middleH.top,"release size reversal jumped");
            frame.Capture(view); // Deliver the real release before advancing its fade.
            advance(1.1);
            const auto after=frame.Capture(view);
            RECT client{}; ViewClientRect(view,&client);
            unsigned differences=0;
            int minX=client.right,minY=client.bottom,maxX=0,maxY=0;
            // Compare only the scrollbar strips, including their expanded area.
            for(int y=int(view.headerPx)+4;y<client.bottom;++y) for(int x=0;x<client.right;++x) {
                const int logical=LogicalX(view,x);
                if(logical<int(Scrollbar(view,true).left)-2 && y<int(HorizontalBar(view,true).top)-2) continue;
                const size_t p=(size_t(y)*client.right+x)*4;
                if(memcmp(before.data()+p,after.data()+p,4)!=0) {
                    ++differences; minX=std::min(minX,x); maxX=std::max(maxX,x); minY=std::min(minY,y); maxY=std::max(maxY,y);
                }
            }
            if(differences || view.touchScrollbarStarted[0].QuadPart || view.touchScrollbarStarted[1].QuadPart || ListViewportWidth(view)!=viewport)
                std::cerr<<"TOUCH BAR dpi="<<dpi<<" rtl="<<rtl<<" horizontal="<<horizontal<<" pixels="<<differences
                    <<" bounds="<<minX<<","<<minY<<".."<<maxX<<","<<maxY<<" hot="<<view.verticalScrollbarHot<<","<<view.horizontalScrollbarHot
                    <<" scroll="<<view.scrollNow<<","<<view.horizontalScroll<<" animation="<<view.touchScrollbarStarted[0].QuadPart<<","<<view.touchScrollbarStarted[1].QuadPart
                    <<" renderer-hot="<<frame.renderer.scrollbarMotion[0].activeTo<<","<<frame.renderer.scrollbarMotion[1].activeTo
                    <<" viewport="<<viewport<<"/"<<ListViewportWidth(view)<<std::endl;
            Check(differences==0 && !view.touchScrollbarStarted[0].QuadPart && !view.touchScrollbarStarted[1].QuadPart &&
                ListViewportWidth(view)==viewport,"idle touch scrollbar pixels did not return to normal or reflowed content");

            // A captured thumb remains engaged outside its track; actual USER
            // capture loss, not a manufactured cancel message, ends engagement.
            Reset(view,input); input.Next(); input.Send(view,{input.Packet(view,input.primary,point,down)});
            POINT moved=point;
            if(horizontal) {moved.x=LogicalX(view,LogicalX(view,point.x)+60); moved.y-=100;}
            else {moved.y+=60; moved.x=LogicalX(view,LogicalX(view,point.x)-100);}
            input.Next(); input.Send(view,{input.Packet(view,input.primary,moved,move)});
            Check((horizontal ? view.horizontalScroll:view.scrollTarget)>0.f && view.touchScrollbarTo[horizontal ? 1:0]==1.f,
                "touch thumb lost engagement outside the track or sampled the stationary mouse");
            ReleaseCapture();
            Check(!view.touch.active && !view.draggingHorizontal && !view.draggingScrollbar &&
                view.touchScrollbarTo[0]==0.f && view.touchScrollbarTo[1]==0.f,"capture loss retained touch enlargement");
            const auto canceled=horizontal ? view.horizontalScroll:view.scrollNow;
            input.Next(); input.Send(view,{input.Packet(view,input.primary,moved,move)});
            input.Next(); input.Send(view,{input.Packet(view,input.primary,moved,up)});
            Check((horizontal ? view.horizontalScroll:view.scrollNow)==canceled,"old packets resumed a canceled scrollbar");

            Reset(view,input); SetScrollImmediate(view,0); SetHorizontalScroll(view,0); advance(1.1);
            const auto track=horizontal ? HorizontalBar(view,true):Scrollbar(view,true);
            point={LogicalX(view,LONG(horizontal ? track.right-2:(track.left+track.right)*.5f)),
                LONG(horizontal ? (track.top+track.bottom)*.5f:track.bottom-2)};
            input.Next(); input.Send(view,{input.Packet(view,input.primary,point,down)});
            Check(!view.draggingHorizontal && !view.draggingScrollbar && view.touchScrollbarTo[horizontal ? 1:0]==1.f,
                "touch track press did not enlarge its scrollbar");
            input.Next(); input.Send(view,{input.Packet(view,input.primary,body,move)});
            Check(view.touch.down && view.touchScrollbarTo[0]==0.f && view.touchScrollbarTo[1]==0.f,
                "uncaptured contact leaving the track retained enlargement");
            input.Next(); input.Send(view,{input.Packet(view,input.primary,body,up)});
            SetTouchInputMode(view,false); UpdatePointerHover(view,point,false);
            Check(view.touchScrollbarTo[0]==0.f && view.touchScrollbarTo[1]==0.f,"mouse hover applied touch enlargement");
        }
        StopTouchpad(view); SetWindowLongPtrW(view.hwnd,GWLP_USERDATA,0);
    }
}
class ShellView final : public IShellView {
    ULONG refs=1;
public:
    HWND hwnd=nullptr;
    ComPtr<IContextMenu> menu;
    SelectionFolderView* selection=nullptr;
    SharedPidl parent;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        *out=nullptr; if(iid!=IID_IUnknown && iid!=IID_IShellView && iid!=IID_IOleWindow) return E_NOINTERFACE;
        *out=static_cast<IShellView*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {auto n=--refs; if(!n) delete this; return n;}
    HRESULT STDMETHODCALLTYPE GetWindow(HWND* out) override {*out=hwnd; return S_OK;}
    HRESULT STDMETHODCALLTYPE GetItemObject(UINT scope,REFIID iid,void** out) override {
        if(scope!=SVGIO_SELECTION) return E_NOTIMPL;
        if(iid==IID_IDataObject) {
            std::vector<PCUITEMID_CHILD> children;
            for(const auto& child:selection->selected) children.push_back(child.get());
            return SHCreateDataObject(parent.get(),UINT(children.size()),children.data(),nullptr,iid,out);
        }
        return menu.CopyTo(iid,out);
    }
#define TOUCH_UNUSED(name,...) HRESULT STDMETHODCALLTYPE name(__VA_ARGS__) override {return E_NOTIMPL;}
    TOUCH_UNUSED(ContextSensitiveHelp,BOOL)
    TOUCH_UNUSED(TranslateAccelerator,MSG*)
    TOUCH_UNUSED(EnableModeless,BOOL)
    TOUCH_UNUSED(UIActivate,UINT)
    TOUCH_UNUSED(Refresh)
    TOUCH_UNUSED(CreateViewWindow,IShellView*,LPCFOLDERSETTINGS,IShellBrowser*,RECT*,HWND*)
    TOUCH_UNUSED(DestroyViewWindow)
    TOUCH_UNUSED(GetCurrentInfo,LPFOLDERSETTINGS)
    TOUCH_UNUSED(AddPropertySheetPages,DWORD,LPFNSVADDPROPSHEETPAGE,LPARAM)
    TOUCH_UNUSED(SaveViewState)
    TOUCH_UNUSED(SelectItem,PCUITEMID_CHILD,SVSIF)
#undef TOUCH_UNUSED
};
}

static void TestTouchInput(const std::wstring& fixture) {
    using namespace TouchInputTest;
    WorkerComApartment apartment;
    const auto savedWrite=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"touch fonts");
    const bool swipe=g_touchpadSwipeNavigation.exchange(false),smooth=g_smooth.exchange(true),bars=g_scrollbar.exchange(true);
    Device input;
    TouchFixture::Session fixtureInput;
    FrameInputFixture::Session queue;
    PointerFixture::Session pointer;
    ModifierFixture::Session modifiers;
    {
    PresentationFixture::Tab tab(L"raw touch",512),peer(L"raw touch peer",512);
    auto& state=tab.state; auto& other=peer.state;
    const auto peerZoom=other.rowHeight96;
    auto* native=SelectionUiFixture::Setup(tab,fixture);
    Install(state); Install(other);
    FocusFixture::Session focus(tab.frame,state.hwnd);
    MenuFixture::Session menus(state.hwnd);
    ComPtr<SelectionCommandMenu> commands; commands.Attach(new SelectionCommandMenu); commands->defaultSelection=true;
    ComPtr<ShellView> shell; shell.Attach(new ShellView); shell->hwnd=state.nativeHwnd; shell->menu=commands;
    shell->selection=native; shell->parent=state.directoryFolderIdentity; native->commandView=shell;
    auto* browser=new TimingShellBrowser; browser->view=shell; state.shellBrowser.Attach(browser);
    state.shellOleInitialized=true;
    POINT point{100,150};
    Reset(state,input);
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    Check(state.touch.active && !state.marqueePossible && !state.mousePressActive && GetCapture()!=state.hwnd,
        "raw DOWN did not claim a real recognizer or armed mouse selection");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
    const int selected=RowFromPoint(state,point.y,point.x);
    Check(state.selected==selected && state.mouseClickRow==selected,"real Ninput tap did not select");
    WaitForPreparedSelection(state);
    // The physical taps can be within Windows' double-click interval while
    // the owner dispatches the second one late. Recognition uses the original
    // input times; reapplying a mouse wall-clock timeout must not discard it.
    Sleep(GetDoubleClickTime()+USER_TIMER_MINIMUM);
    input.Next(std::max(1u,GetDoubleClickTime()/4));
    ++input.primary;
    input.Send(state,{input.Packet(state,input.primary,point,down)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
    WaitForPreparedSelection(state);
    Check(TouchFixture::recognized.back().interactionId==INTERACTION_ID_TAP &&
        TouchFixture::recognized.back().arguments.tap.count==2,"native engine did not recognize the queued double tap");
    Check(commands->calls.size()==1,"recognized double tap was rejected by the mouse dispatch-time timeout");
    for(unsigned cycle=1;cycle<6;++cycle) {
        input.Next(GetDoubleClickTime()+1); ++input.primary;
        input.Send(state,{input.Packet(state,input.primary,point,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
        input.Next(std::max(1u,GetDoubleClickTime()*cycle/6)); ++input.primary;
        POINT retap=point; retap.x+=3*GetSystemMetricsForDpi(SM_CXDOUBLECLK,ViewDpi(state));
        Check(RowFromPoint(state,retap.y,retap.x)==selected,"retap contact must stay on the same private item");
        input.Send(state,{input.Packet(state,input.primary,retap,down)});
        input.Next(GetDoubleClickTime()/3); input.Send(state,{input.Packet(state,input.primary,retap,up)});
        WaitForPreparedSelection(state);
        if(commands->calls.size()!=cycle+1)
            std::cerr<<"DOUBLE cycle="<<cycle<<" gap="<<GetDoubleClickTime()*cycle/6
                <<" system="<<GetDoubleClickTime()<<" calls="<<commands->calls.size()
                <<" nativeCount="<<TouchFixture::recognized.back().arguments.tap.count<<"\n";
        Check(commands->calls.size()==cycle+1,"successive native double-tap cycle failed activation");
    }

    // Actual stationary timer -> native HOLD -> SECONDARY_TAP, followed by
    // native HOLD -> manipulation -> posted OLE drag. No fabricated events.
    input.performance=false;
    Reset(state,input,true); input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    Held(state); WaitForPreparedSelection(state);
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
    Check(MenuFixture::calls==1 && !state.touch.down,"native held release did not show one Shell menu");
    input.performance=false;
    Reset(state,input,true); input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    // Cover a dwTime-only digitizer and delayed low-priority WM_TIMER. The first
    // real movement after the system hold deadline still has to become drag.
    const auto holdWait=state.touch.holdDeadline-GetTickCount64();
    Sleep(DWORD(holdWait)+USER_TIMER_MINIMUM);
    WaitForPreparedSelection(state);
    const auto expectedDraggedPath=state.entries[size_t(state.touch.row)].path;
    const float beforeDrag=state.scrollNow;
    POINT moved=point; moved.y-=80;
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
    Check(state.touch.dragPending && state.scrollNow==beforeDrag && TouchFixture::drags==0,
        "held movement scrolled or entered OLE inside the recognizer callback");
    Pump(state);
    Check(TouchFixture::promotions==1 && TouchFixture::drags==1 && !state.shellDragRunning && !state.touch.active,
        "held movement failed native primary-contact promotion/Shell drag or retained its state");
    Check(TouchFixture::dragPaths==std::vector<std::wstring>{expectedDraggedPath},
        "touch OLE data object did not contain the actually selected held item");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,up)});
    input.performance=true;
    Check(MenuFixture::calls==1 && commands->calls.size()==6,"drag release became a click or context menu");

    MarqueePixels(input,fixture);
    PinchThresholdPixels(input,fixture);

    // One contact starts moving before the other; unequal speed/angle and
    // spacing drift must not turn that initialization into zoom. Ninput still
    // supplies every transform, including distance held pending by arbitration.
    for(UINT dpi:{96u,144u,192u}) for(bool history:{false,true})
        for(bool vertical:{false,true}) for(bool aligned:{true,false}) for(int sign:{-1,1}) {
        StyleFixture::Session metrics; StyleFixture::dpi=dpi;
        g_touchpadSwipeNavigation=history;
        Reset(state,input); state.columns[0].width=1400; UpdateMetrics(state); UpdateColumns(state);
        SetScrollImmediate(state,300); SetHorizontalScroll(state,200);
        const auto height=state.rowHeight96;
        const auto precise=state.preciseRowHeight96;
        const LONG radius=Scale96(60,dpi), lead=radius/4;
        POINT first{400,220},second=first;
        if(vertical==aligned) second.y+=2*radius; else second.x+=2*radius;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,down)});
        if(vertical) first.y+=sign*lead; else first.x+=sign*lead;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        Check(state.rowHeight96==height && state.preciseRowHeight96==precise && state.scrollNow==300 &&
            state.horizontalScroll==200 && !state.touchpadPinching,
            "a single early-moving contact stole two-finger swipe initialization for zoom/pan");
        const LONG step=GetSystemMetricsForDpi(vertical ? SM_CYDRAG:SM_CXDRAG,dpi);
        for(unsigned n=0;n!=6;++n) {
            if(vertical) {first.y+=sign*step; second.y+=sign*(step+1); second.x+=1;}
            else {first.x+=sign*step; second.x+=sign*(step+1); second.y+=1;}
            input.Next(); // Alternate array order, keeping actual pointer IDs.
            if(n%2) input.Send(state,{input.Packet(state,input.primary+1,second,move),input.Packet(state,input.primary,first,move)});
            else input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        }
        Check(state.touch.gesture==(vertical ? TouchGesture::Vertical:TouchGesture::Horizontal) &&
            state.rowHeight96==height && state.preciseRowHeight96==precise && !state.touchpadPinching,
            "imperfect staggered two-finger swipe resized items or failed to initialize");
        const auto& nativeMotion=TouchFixture::recognized.back().arguments.manipulation.cumulative;
        Check(fabsf((vertical ? state.scrollNow:state.horizontalScroll)-
            (vertical ? 300-nativeMotion.translationY:200-nativeMotion.translationX))<.4f,
            "pending two-finger swipe lost or repeated recognized translation");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,up),input.Packet(state,input.primary+1,second,up)});
        StopTouch(state);
    }
    g_touchpadSwipeNavigation=false;

    for(bool history:{false,true}) {
        g_touchpadSwipeNavigation=history;
        Reset(state,input); SetScrollImmediate(state,300);
        POINT first{340,220},second{460,220};
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,down)});
        const auto height=state.rowHeight96;
        first={352,184}; second={448,256}; // Same radius, rotated: not a pinch.
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        Check(state.touch.gesture==TouchGesture::Pending && state.rowHeight96==height && state.scrollNow==300,
            "rotation about the reference centroid was treated as radial scaling/scrolling");
        first.y-=60; second.y-=60;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        Check(state.touch.gesture==TouchGesture::Vertical && state.rowHeight96==height && state.scrollNow>300,
            "rotation noise prevented subsequent two-finger scrolling");
        // Additional/lifted fingers cannot reopen an already committed pan.
        POINT third{first.x+50,first.y};
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move),
            input.Packet(state,input.primary+2,third,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move),
            input.Packet(state,input.primary+2,third,up)});
        first.x-=30; second.x+=30; first.y-=40; second.y-=40;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        Check(state.touch.gesture==TouchGesture::Vertical && state.rowHeight96==height && !state.touchpadPinching,
            "contact topology change let pinch steal a committed pan");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,up),input.Packet(state,input.primary+1,second,up)});
        StopTouch(state);
    }
    g_touchpadSwipeNavigation=false;

    for(bool grid:{false,true}) for(bool grouped:{false,true}) for(bool rtl:{false,true}) for(UINT dpi:{96u,144u,192u}) {
        StyleFixture::Session metrics; StyleFixture::dpi=dpi;
        Reset(state,input); state.gridView=grid; state.columnsView=!grid;
        state.cachedNativeMode=grid ? FVM_ICON : FVM_DETAILS; state.rtlLayout=rtl; state.nativeIconSize=48;
        state.rowHeight96[grid]=40; state.preciseRowHeight96[grid]=0.; state.grouping.enabled=grouped;
        RebuildGroupSections(state); UpdateMetrics(state); UpdateColumns(state);
        state.columns[0].width=1400; UpdateColumns(state);
        point={LogicalX(state,100),LONG(state.headerPx+160)};
        SetScrollImmediate(state,300);
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
        moved=point; moved.y-=80;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
        Check(fabsf(state.scrollNow-380.f)<.2f && !state.marqueePossible && !state.mousePressActive,
            "raw contact pan failed through Ninput or entered mouse marquee");
        const auto count=TouchFixture::processCalls; const auto once=state.scrollNow;
        const auto duplicate=input.Packet(state,input.primary,moved,move);
        FeedTouchFrames(state,state.touchRecognizer,1,1,&duplicate);
        Check(TouchFixture::processCalls==count && state.scrollNow==once,"same pointer frame was processed twice");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,up)});
        Check(state.swipeReleased && state.selected==selected,"pan release selected a passing item or missed release");
        StopTouch(state);

        Reset(state,input); SetScrollImmediate(state,0);
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
        moved=point; moved.y+=80;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
        Check(state.scrollNow==0 && TouchFixture::overY>=79 && state.touch.panningFeedback,
            "native pan did not accumulate overscroll feedback");
        moved.y-=100; input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
        const float nativeTarget=std::max(0.f,-TouchFixture::recognized.back().arguments.manipulation.cumulative.translationY);
        Check(fabsf(state.scrollNow-nativeTarget)<.3f && abs(TouchFixture::overY)<1,
            "overscroll did not unwind before reentering content at the native recognized position");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,up)});
        StopTouch(state); Check(!state.touch.panningFeedback,"canceled inertia retained OS boundary feedback");

        Reset(state,input); SetScrollImmediate(state,DocumentRowTop(state,37)-160.f);
        point={LogicalX(state,100),LONG(state.headerPx+160)};
        const int anchor=RowFromPoint(state,point.y,point.x);
        Check(anchor>=0,"pinch fixture anchor in heading");
        const float fraction=(point.y-state.headerPx+state.scrollNow-DocumentRowTop(state,anchor))/DisplayRowHeight(state,anchor);
        const float beforePinch=state.rowPx, beforeScroll=state.scrollNow;
        POINT left{point.x-60,point.y},right{point.x+60,point.y};
        input.Next(); input.Send(state,{input.Packet(state,input.primary,left,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,left,move),input.Packet(state,input.primary+1,right,down)});
        left.x-=15; right.x+=15;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,left,move),input.Packet(state,input.primary+1,right,move)});
        Check(state.touch.gesture==TouchGesture::Pinch && state.touchpadPinching && state.rowPx==beforePinch &&
            state.scrollNow==beforeScroll && state.rowHeight96[grid]==40 && !state.zoomAnimating,
            "pinch threshold replayed recognition distance as a visible zoom jump");
        left.y+=60; right.y+=60;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,left,move),input.Packet(state,input.primary+1,right,move)});
        Check(state.rowPx==beforePinch && state.scrollNow==beforeScroll,
            "recognized stationary-scale pinch translated or resized before further radial movement");
        left.x-=15; right.x+=15;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,left,move),input.Packet(state,input.primary+1,right,move)});
        if(state.zoomAnimating || fabs(state.preciseRowHeight96[grid]-48.)>=.15) {
            const auto& motion=TouchFixture::recognized.back().arguments.manipulation;
            std::cerr<<"PINCH kind="<<int(state.touch.gesture)<<" height="<<state.preciseRowHeight96[grid]
                <<" expansion="<<motion.delta.expansion
                <<" scale="<<motion.cumulative.scale<<" translation="<<motion.delta.translationX<<","<<motion.delta.translationY<<"\n";
        }
        Check(!state.zoomAnimating && fabs(state.preciseRowHeight96[grid]-48.)<.15,"post-threshold pinch failed proportional direct scaling");
        const float retained=DocumentRowTop(state,anchor)+fraction*DisplayRowHeight(state,anchor)-state.scrollNow;
        Check(fabsf(retained-(point.y-state.headerPx))<1.f,"committed pinch independently scrolled instead of retaining its reference centroid");
        left.x+=15; right.x-=15;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,left,move),input.Packet(state,input.primary+1,right,move)});
        Check(fabs(state.preciseRowHeight96[grid]-40.)<.15,"native pinch reversal drifted from its recognition baseline");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,left,up),input.Packet(state,input.primary+1,right,up)});
        StopTouch(state);
    }
    state.grouping.enabled=false; RebuildGroupSections(state); state.gridView=false; state.columnsView=true;
    state.cachedNativeMode=FVM_DETAILS; state.rtlLayout=false; UpdateMetrics(state); UpdateColumns(state);

    // Radial intent is relative to the initial centroid/radius, independent
    // of separation, orientation, DPI and the optional history setting.
    for(bool history:{false,true}) for(UINT dpi:{96u,192u}) for(int direction:{-1,1})
        for(int axis:{0,1,2}) for(int span:{80,200}) {
        StyleFixture::Session metrics; StyleFixture::dpi=dpi;
        g_touchpadSwipeNavigation=history;
        Reset(state,input); state.rowHeight96[0]=40; state.preciseRowHeight96[0]=0.;
        UpdateMetrics(state); SetScrollImmediate(state,300); SetHorizontalScroll(state,200);
        const LONG offset=Scale96(span/2,dpi);
        point={400,LONG(state.headerPx+offset+80)};
        POINT radial{axis==1 ? 0:offset,axis==0 ? 0:offset};
        POINT first{point.x-radial.x,point.y-radial.y},second{point.x+radial.x,point.y+radial.y};
        const float beforePinch=state.rowPx;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,down)});
        first.x-=direction*radial.x/4; first.y-=direction*radial.y/4;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        Check(!state.touchpadPinching && state.scrollNow==300 && state.horizontalScroll==200,
            "pinch committed before the second contact demonstrated radial movement");
        second.x+=direction*radial.x/4; second.y+=direction*radial.y/4;
        input.Next(); input.Send(state,{input.Packet(state,input.primary+1,second,move),input.Packet(state,input.primary,first,move)});
        if(state.touch.gesture!=TouchGesture::Pinch || state.rowPx!=beforePinch)
            std::cerr<<"RADIAL history="<<history<<" dpi="<<dpi<<" direction="<<direction<<" axis="<<axis
                <<" span="<<span<<" kind="<<int(state.touch.gesture)<<" height="<<state.preciseRowHeight96[0]
                <<" nativeScale="<<TouchFixture::recognized.back().arguments.manipulation.cumulative.scale
                <<" contacts="<<state.touch.contacts<<" control="<<state.touch.control<<"\n";
        Check(state.touch.gesture==TouchGesture::Pinch &&
            state.rowPx==beforePinch && state.rowHeight96[0]==40 && state.scrollNow==300 && !state.swipeFeedbackActive &&
            state.horizontalScroll==200,"radial recognition changed the image at the threshold");
        const double committedSpan=std::hypot(double(second.x-first.x),double(second.y-first.y));
        first.x-=radial.x ? direction:0; first.y-=radial.y ? direction:0;
        second.x+=radial.x ? direction:0; second.y+=radial.y ? direction:0;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        const double expected=40.*std::hypot(double(second.x-first.x),double(second.y-first.y))/committedSpan;
        Check(fabs(state.preciseRowHeight96[0]-expected)<.05 && !state.zoomAnimating,
            "first post-threshold pixel movement failed continuous fractional scaling");
        // Translation after commitment cannot run a second gesture in parallel.
        const auto zoom=state.preciseRowHeight96[0]; const float scroll=state.scrollNow;
        POINT pinchAnchor=state.touch.pinchOrigin; MapUiPoint(nullptr,state.hwnd,pinchAnchor);
        const int anchorRow=RowFromPoint(state,pinchAnchor.y,pinchAnchor.x);
        Check(anchorRow>=0,"radial translation fixture anchor");
        const float anchorFraction=(pinchAnchor.y-state.headerPx+scroll-DocumentRowTop(state,anchorRow))/DisplayRowHeight(state,anchorRow);
        first.y+=50; second.y+=50; first.x+=30; second.x+=30;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        const float retained=DocumentRowTop(state,anchorRow)+anchorFraction*DisplayRowHeight(state,anchorRow)-state.scrollNow;
        Check(state.preciseRowHeight96[0]==zoom && state.scrollNow==scroll &&
            fabsf(retained-(pinchAnchor.y-state.headerPx))<.1f &&
            state.horizontalScroll==200 && !state.swipeFeedbackActive,"committed pinch also panned the list/columns/history");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,up)});
        const auto remainingZoom=state.preciseRowHeight96[0]; const float remainingScroll=state.scrollNow;
        first.y-=80; input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move)});
        Check(state.touch.gesture==TouchGesture::Pinch && state.preciseRowHeight96[0]==remainingZoom &&
            state.scrollNow==remainingScroll,"lifting a pinch contact switched gesture or resized from stale scale");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,up)}); StopTouch(state);
    }
    g_touchpadSwipeNavigation=false;

    // Replacing a contact before commitment must not inherit old scale debt.
    Reset(state,input); state.rowHeight96[0]=40; state.preciseRowHeight96[0]=0.;
    UpdateMetrics(state); SetScrollImmediate(state,300);
    {
        POINT first{340,220},second{460,220};
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,down)});
        first.x+=15;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,move)});
        const auto oldScale=TouchFixture::recognized.back().arguments.manipulation.cumulative.scale;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+1,second,up),
            input.Packet(state,input.primary+2,second,down)});
        const auto replacementScale=TouchFixture::recognized.back().arguments.manipulation.cumulative.scale;
        first.x-=15; second.x+=15;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+2,second,move)});
        if(state.touch.gesture!=TouchGesture::Pinch || state.rowHeight96[0]!=40)
            std::cerr<<"REPLACE kind="<<int(state.touch.gesture)<<" height="<<state.preciseRowHeight96[0]
                <<" oldScale="<<oldScale<<" replacementScale="<<replacementScale<<" native="
                <<TouchFixture::recognized.back().arguments.manipulation.cumulative.scale
                <<" pair="<<state.touch.pairTracked<<"\n";
        Check(state.touch.gesture==TouchGesture::Pinch && state.rowHeight96[0]==40 && state.preciseRowHeight96[0]==0.,
            "replacement contact inherited scale debt at recognition");
        first.x-=3; second.x+=3;
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,move),input.Packet(state,input.primary+2,second,move)});
        Check(fabs(state.preciseRowHeight96[0]-40.*141./135.)<.05,
            "replacement pair failed to scale from its committed span");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,first,up),input.Packet(state,input.primary+2,second,up)});
        StopTouch(state);
    }

    // An entirely fitting directory has no vertical scrolling to consume.
    // Use the same captured marquee transaction, including release and rollback.
    for(bool grid:{false,true}) for(bool rtl:{false,true}) {
        PresentationFixture::Tab fitting(L"fitting touch directory",1,rtl,grid);
        auto& view=fitting.state;
        auto* selection=SelectionUiFixture::Setup(fitting,fixture); Install(view);
        FocusFixture::Session fittingFocus(fitting.frame,view.hwnd);
        Reset(view,input);
        Check(MaxScrollPx(view)==0.f,"fitting directory unexpectedly needs vertical scrolling");
        POINT origin{LogicalX(view,40),LONG(view.headerPx+320)},end{LogicalX(view,200),LONG(view.headerPx+4)};
        const auto feedback=TouchFixture::begins;
        input.Next(); input.Send(view,{input.Packet(view,input.primary,origin,down)});
        input.Next(); input.Send(view,{input.Packet(view,input.primary,end,move)});
        Check(view.touch.gesture==TouchGesture::Marquee && view.marqueeActive && GetCapture()==view.hwnd &&
            view.scrollNow==0 && TouchFixture::begins==feedback && !view.touch.panningFeedback,
            "vertical touch in a fitting directory overscrolled instead of starting marquee");
        input.Next(); input.Send(view,{input.Packet(view,input.primary,end,up)});
        WaitForPreparedSelection(view);
        Check(!view.marqueeActive && GetCapture()!=view.hwnd && selection->selected.size()==1,
            "fitting-directory vertical marquee failed native selection commit/release");
        Reset(view,input);
        input.Next(); input.Send(view,{input.Packet(view,input.primary,origin,down)});
        input.Next(); input.Send(view,{input.Packet(view,input.primary,end,move)});
        ReleaseCapture();
        Check(!view.touch.active && !view.marqueeActive && selection->selected.size()==1,
            "fitting-directory marquee capture loss failed cancellation");
        StopTouchpad(view); SetWindowLongPtrW(view.hwnd,GWLP_USERDATA,0);
    }

    // Coalesced frame history is oldest-first to the recognizer; duplicate
    // messages for other pointers in a processed frame do not repeat it.
    Reset(state,input); SetScrollImmediate(state,300); point={100,200};
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    moved=point; moved.y-=50; input.Next(); const auto older=input.Packet(state,input.primary,moved,move);
    moved.y-=75; input.Next(); const auto newer=input.Packet(state,input.primary,moved,move);
    const POINTER_INFO history[]{newer,older};
    const auto beforeFrames=TouchFixture::processCalls;
    FeedTouchFrames(state,state.touchRecognizer,2,1,history);
    const auto nativeY=TouchFixture::recognized.back().arguments.manipulation.cumulative.translationY;
    Check(fabsf(state.scrollNow-(300.f-nativeY))<.2f && TouchFixture::processCalls==beforeFrames+2,
        "coalesced touch history did not apply each native recognized frame exactly once");
    StopTouch(state);
    g_touchpadSwipeNavigation=true;
    for(bool rtl:{false,true}) for(bool backward:{false,true}) for(bool commit:{false,true}) {
        Reset(state,input); state.rtlLayout=rtl; point={400,200};
        const float range=std::max(0.f,state.columnContentWidth-ListViewportWidth(state));
        Check(range>100.f,"edge-navigation fixture needs real column overflow");
        SetHorizontalScroll(state,range*.5f);
        POINT second{point.x+100,point.y};
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,move),input.Packet(state,input.primary+1,second,down)});
        const float sign=(backward!=rtl ? 1.f:-1.f);
        const auto shift=[&](float dx,DWORD ms=16) {
            point.x+=LONG(sign*dx); second.x+=LONG(sign*dx); input.Next(ms);
            input.Send(state,{input.Packet(state,input.primary,point,move),input.Packet(state,input.primary+1,second,move)});
        };
        second.x+=LONG(sign*8);
        shift(range*.125f);
        Check(state.touch.gesture==TouchGesture::Horizontal && !state.swipeFeedbackActive &&
            state.horizontalScroll>0 && state.horizontalScroll<range,"two-finger horizontal input did not consume columns before showing history feedback");
        second.x+=LONG(sign*8); // Real contact spacing varies during a parallel pan.
        const auto zoomBefore=state.preciseRowHeight96[state.gridView];
        shift(range*.5f+SwipeCommitDistance(state)+50.f);
        Check(state.touch.gesture==TouchGesture::Horizontal && state.preciseRowHeight96[state.gridView]==zoomBefore,
            "contact-span noise stole established horizontal scrolling for pinch zoom");
        const float nativeX=TouchFixture::recognized.back().arguments.manipulation.cumulative.translationX*(rtl ? -1.f:1.f);
        const float excess=nativeX+(backward ? -range*.5f:range*.5f);
        Check(fabsf(state.swipeEdgeX-excess)<.4f &&
            fabsf(state.horizontalScroll-(backward ? 0.f:range))<.1f && state.swipeFeedbackActive,
            "history preview counted consumed column space or missed the corresponding edge");
        MSG command{};
        Check(!PeekMessageW(&command,state.hwnd,kMsgManipulationNavigate,kMsgManipulationNavigate,PM_NOREMOVE),"touch history committed before release");
        if(!commit) {
            shift(-fabsf(excess)*.5f);
            Check(state.swipeFeedbackActive && fabsf(state.horizontalScroll-(backward ? 0.f:range))<.1f,
                "reversing an edge preview moved columns before unwinding its displacement");
            shift(-fabsf(state.swipeEdgeX)-range*.25f);
            Check(!state.swipeFeedbackActive && state.swipeEdgeX==0 &&
                state.horizontalScroll>0 && state.horizontalScroll<range,
                "reversing the edge preview did not return smoothly through the columns");
        }
        input.Next(GetDoubleClickTime());
        input.Send(state,{input.Packet(state,input.primary,point,move),input.Packet(state,input.primary+1,second,move)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up),input.Packet(state,input.primary+1,second,up)});
        const bool posted=PeekMessageW(&command,state.hwnd,kMsgManipulationNavigate,kMsgManipulationNavigate,PM_REMOVE)!=FALSE;
        Check(posted==commit,"native edge release failed to commit or committed a reversed gesture");
        if(posted) {
            Check(bool(command.wParam)==backward && command.lParam==LPARAM(state.mouseFolderEpoch),
                "native two-finger edge release direction/epoch failed");
            const auto before=browser->browseFlags.size();
            OverlayProc(command.hwnd,command.message,command.wParam,command.lParam);
            Check(browser->browseFlags.size()==before+1 && browser->browseFlags.back()==
                UINT(backward ? SBSP_NAVIGATEBACK:SBSP_NAVIGATEFORWARD),
                "posted native edge release did not reach the owning Shell browser");
        }
        AdvanceTouchInertia(state);
        Check(!PeekMessageW(&command,state.hwnd,kMsgManipulationNavigate,kMsgManipulationNavigate,PM_NOREMOVE),"inertia repeated a history command");
        if(commit) ValidateRect(state.hwnd,nullptr);
        StopTouch(state);
        if(commit) Check(GetUpdateRect(state.hwnd,nullptr,FALSE),"touch cancellation did not invalidate the rendered history preview");
    }
    g_touchpadSwipeNavigation=false; state.rtlLayout=false;

    // Finger count changes horizontal semantics only; vertical input always
    // uses the same native pan, including three/five contacts delivered to us.
    g_touchpadSwipeNavigation=true;
    for(unsigned count:{1u,2u,3u,5u}) {
        Reset(state,input); SetScrollImmediate(state,300); SetHorizontalScroll(state,100);
        point={100,220};
        const auto frame=[&](unsigned fingers,POINTER_FLAGS flags,bool adding=false) {
            std::vector<POINTER_INFO> packets;
            for(unsigned n=0;n<fingers;++n) packets.push_back(input.Packet(state,input.primary+n,
                POINT{point.x+LONG(n*40),point.y},adding && n+1<fingers ? move:flags));
            input.SendMany(state,packets);
        };
        for(unsigned n=1;n<=count;++n) {input.Next(); frame(n,down,true);}
        point.y-=80; input.Next(); frame(count,move);
        const float nativeY=TouchFixture::recognized.back().arguments.manipulation.cumulative.translationY;
        if(state.touch.gesture!=TouchGesture::Vertical || fabsf(state.scrollNow-(300-nativeY))>=.3f)
            std::cerr<<"VERTICAL count="<<count<<" kind="<<int(state.touch.gesture)<<" scroll="<<state.scrollNow
                <<" nativeY="<<nativeY<<" intent="<<state.touch.intentX<<","<<state.touch.intentY
                <<" contacts="<<state.touch.contacts<<" recognized="<<TouchFixture::recognized.back().contactCount<<"\n";
        Check(state.touch.gesture==TouchGesture::Vertical && fabsf(state.scrollNow-(300-nativeY))<.3f &&
            state.horizontalScroll==100 && !state.marqueePossible && !state.swipeFeedbackActive,
            "vertical native input changed behavior with finger count");
        input.Next(); frame(count,up); StopTouch(state);
    }
    Reset(state,input); SetScrollImmediate(state,0); SetHorizontalScroll(state,0);
    point={120,LONG(state.headerPx+70)}; moved={point.x+220,point.y+80};
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
    Check(state.touch.gesture==TouchGesture::Marquee && state.marqueeActive && GetCapture()==state.hwnd &&
        state.horizontalScroll==0 && state.scrollNow==0 && !state.swipeFeedbackActive,
        "one-finger horizontal input did not start shared marquee selection");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,up)});
    WaitForPreparedSelection(state);
    if(state.marqueePossible || state.marqueeActive || GetCapture()==state.hwnd ||
        std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)<=1 || native->selected.size()<=1)
        std::cerr<<"MARQUEE possible="<<state.marqueePossible<<" active="<<state.marqueeActive
            <<" capture="<<(GetCapture()==state.hwnd)<<" flags="<<std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)
            <<" native="<<native->selected.size()<<" rowPx="<<state.rowPx<<"\n";
    Check(!state.marqueePossible && !state.marqueeActive && GetCapture()!=state.hwnd &&
        std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1)>1 && native->selected.size()>1,
        "touch marquee release did not commit its native selection/release capture");
    const auto committed=state.selectedFlags;
    Reset(state,input); input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
    Check(state.marqueeActive,"marquee capture-loss fixture did not start");
    ReleaseCapture();
    Check(!state.touch.active && !state.marqueeActive && state.selectedFlags==committed,
        "native capture loss did not roll back touch marquee selection");
    Reset(state,input); input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
    Check(state.marqueeActive,"contact-addition fixture did not start marquee");
    POINT added{moved.x+100,moved.y};
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move),input.Packet(state,input.primary+1,added,down)});
    Check(state.touch.down && !state.marqueeActive && GetCapture()!=state.hwnd && state.selectedFlags==committed,
        "adding a contact retained marquee capture or its provisional selection");
    const auto priorScroll=state.scrollNow;
    moved.y-=80; added.y-=80;
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move),input.Packet(state,input.primary+1,added,move)});
    const auto nativeDelta=TouchFixture::recognized.back().arguments.manipulation.delta.translationY;
    Check(state.touch.gesture==TouchGesture::Vertical && fabsf(state.scrollNow-priorScroll+nativeDelta)<.3f,
        "adding a contact to marquee did not return input to native scrolling");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,up),input.Packet(state,input.primary+1,added,up)});
    StopTouch(state);
    g_touchpadSwipeNavigation=false;

    // Cancellation invalidates packet ownership and deferred actions, including
    // actual selection-worker completion after the contact was released.
    Reset(state,input); point={100,180}; input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    const auto before=state.scrollNow; ResetMouseGestureForNavigation(state);
    moved=point; moved.y-=100; input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,up)});
    Check(state.scrollNow==before && !state.touch.active,"old touch packets resurrected after navigation");
    {
        PresentationFixture::Tab cold(L"cold held touch selection",512);
        auto& waiting=cold.state;
        SelectionUiFixture::Setup(cold,fixture); Install(waiting);
        FocusFixture::Session coldFocus(cold.frame,waiting.hwnd);
        Reset(waiting,input,true);
        Select(waiting,0);
        SelectionUiFixture::Hold held(waiting.hwnd);
        SelectWithModifiers(waiting,100,false,true);
        StopScrollAnimation(waiting); SetScrollImmediate(waiting,DocumentRowTop(waiting,100)-100.f);
        point={100,LONG(waiting.headerPx+100+DisplayRowHeight(waiting,100)*.5f)};
        Check(waiting.pendingNativeSelection!=0,"bulk selection did not enter its real preparation lane");
        input.Next(); input.Send(waiting,{input.Packet(waiting,input.primary,point,down)});
        Held(waiting);
        moved=point; moved.y-=70; input.Next(); input.Send(waiting,{input.Packet(waiting,input.primary,moved,move)});
        Check(waiting.touch.dragPending && waiting.pendingNativeSelection && TouchFixture::drags==1,
            "held drag bypassed the pending selection lane");
        input.Next(); input.Send(waiting,{input.Packet(waiting,input.primary,moved,up)});
        held.Release(); WaitForPreparedSelection(waiting); Pump(waiting);
        Check(TouchFixture::drags==1 && !waiting.touch.dragPending,"released contact started OLE after selection completion");
        StopTouchpad(waiting); SetWindowLongPtrW(waiting.hwnd,GWLP_USERDATA,0);
    }
    Reset(state,input,true); input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    Held(state); WaitForPreparedSelection(state);
    POINT second{point.x+100,point.y}; input.Next();
    input.Send(state,{input.Packet(state,input.primary,point,move),input.Packet(state,input.primary+1,second,down)});
    point.y-=80; second.y-=80; input.Next();
    input.Send(state,{input.Packet(state,input.primary,point,move),input.Packet(state,input.primary+1,second,move)});
    Pump(state); Check(TouchFixture::drags==1,"second contact did not cancel held drag eligibility");
    StopTouch(state);

    ScrollbarInteractions(input,fixture);

    // Native double-tap recognition must preserve the existing completed
    // divider click and reach the same content-driven refit as the mouse.
    Reset(state,input); SetScrollImmediate(state,0); SetHorizontalScroll(state,0);
    state.columns[0].width=500;
    ExplorerColumn size; size.key=PKEY_Size; size.title=L"Size"; size.width=300;
    state.columns.push_back(size); UpdateColumns(state);
    point={LogicalX(state,LONG(ListStartX(state)+state.columns[0].end)),LONG(ColumnHeaderHeight(state)/2)};
    Check(DividerAt(state,point.x,point.y)==0,"touch divider fixture geometry");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
    Check(state.dividerClickTick!=0,"touch divider did not retain a completed first click");
    input.Next(std::max(1u,GetDoubleClickTime()/4));
    input.Send(state,{input.Packet(state,input.primary,point,down)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
    Check(state.columns[0].width<500 && state.dragDivider<0 && GetCapture()!=state.hwnd,
        "native divider double tap failed refit or retained capture");
    FinishColumnTransition(state);

    // Interleaved contexts retain their own contact and inertia state.
    Check(other.scrollNow==0 && !other.touch.active && other.rowHeight96==peerZoom,"touch state leaked into a peer tab");
    Reset(state,input); point={100,200}; SetScrollImmediate(state,100);
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    const auto firstId=input.primary;
    input.primary+=10; input.Next(); input.Send(other,{input.Packet(other,input.primary,point,down)});
    const auto secondId=input.primary;
    moved=point; moved.y-=40; input.Next();
    input.Send(state,{input.Packet(state,firstId,moved,move)});
    moved.y-=30; input.Next(); input.Send(other,{input.Packet(other,secondId,moved,move)});
    Check(fabsf(state.scrollNow-140)<.3f && fabsf(other.scrollNow-70)<.3f && state.touch.down && other.touch.down,
        "interleaved native recognizers serialized or shared their viewport state");
    StopTouch(state); StopTouch(other);
    Reset(state,input,true); SetScrollImmediate(state,100); point={100,250};
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    point.y-=80; input.Next(); input.Send(state,{input.Packet(state,input.primary,point,move)});
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
    const auto releasedAt=state.scrollNow;
    Check(state.touch.inertia && Eventually([&]{AdvanceTouchInertia(state); return state.scrollNow>releasedAt+.1f;},2000),
        "native release did not produce compositor-driven inertia");
    input.primary+=10; input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    const auto interruptedAt=state.scrollNow;
    AdvanceTouchInertia(state);
    Check(!state.touch.inertia && state.scrollNow==interruptedAt,"new contact did not interrupt native inertia");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up|POINTER_FLAG_CANCELED)});
    Check(!state.touch.active && !state.touch.dragPending,"canceled native contact retained gesture state");
    Reset(state,input,true); point={100,200};
    input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
    Held(state); WaitForPreparedSelection(state);
    moved=point; moved.y-=70; input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,move)});
    const auto beforeFailure=TouchFixture::drags;
    TouchFixture::promotionSucceeds=false; Pump(state); TouchFixture::promotionSucceeds=true;
    Check(TouchFixture::drags==beforeFailure && !state.touch.active && !state.touch.dragPromoted,
        "failed native drag promotion entered OLE or retained contact ownership");
    input.Next(); input.Send(state,{input.Packet(state,input.primary,moved,up)});
    Reset(state,input); StopTouchpad(state);

    // Explorer preferences enter through the existing owner style queue.
    // No registry settings, desktop focus or Windows input messages are changed.
    {
        StyleFixture::Session policy;
        StyleFixture::singleClick=true;
        for(const auto mode:{0u,1u,2u,3u}) for(const auto* browserMode:{L"yes",L"no",L"hover"}) {
            StyleFixture::iconUnderline=mode; StyleFixture::browserUnderline=browserMode;
            const auto value=ReadShellClickPolicy();
            const bool cold=mode==0 || (mode==3 && wcscmp(browserMode,L"yes")==0);
            const bool hot=cold || mode==2 || (mode==3 && wcscmp(browserMode,L"hover")==0);
            Check(value.singleClick && value.underlineCold==cold && value.underlineHot==hot,
                "Explorer click/underline/browser preference mapping failed");
        }
        QueueStyleChange(state,kStyleInput);
        MSG update{};
        Check(PeekMessageW(&update,state.hwnd,kMsgStyleChanged,kMsgStyleChanged,PM_REMOVE),"click preference owner update was not queued");
        OverlayProc(update.hwnd,update.message,update.wParam,update.lParam);
        Check(state.clickPolicy.singleClick,"owner style delivery did not apply Explorer single-click mode");
        Reset(state,input); SetScrollImmediate(state,0);
        point={100,LONG(state.headerPx+state.rowPx*2.5f)};
        POINT mouse{point.x,point.y+LONG(state.rowPx*3)};
        PointerFixture::At(state.hwnd,mouse);
        SetTouchInputMode(state,false); UpdatePointerHover(state,mouse,false);
        Check(state.hovered==RowFromPoint(state,mouse.y,mouse.x),"private mouse ownership setup");
        const auto calls=commands->calls.size();
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
        WaitForPreparedSelection(state);
        Check(commands->calls.size()==calls+1,"Explorer single-click option did not activate a native touch tap");
        const auto touched=state.selected;
        for(int n=0;n<4;++n) RefreshHoverUnderCursor(state);
        Check(state.pointingDevice==PointingDevice::Touch && state.hovered==-1 && state.selected==touched,
            "stationary mouse regained hover/selection ownership after touch release");
        modifiers.keys[VK_CONTROL]=0x80;
        input.Next(GetDoubleClickTime()+1); ++input.primary;
        input.Send(state,{input.Packet(state,input.primary,point,down)});
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,up)});
        modifiers.keys[VK_CONTROL]=0;
        WaitForPreparedSelection(state);
        Check(commands->calls.size()==calls+1,"single-click mode activated a modified selection tap");
        SetTouchInputMode(state,false); UpdatePointerHover(state,mouse,false);
        Check(state.pointingDevice==PointingDevice::Mouse && state.hovered==RowFromPoint(state,mouse.y,mouse.x),
            "mouse ownership did not resume hover after an explicit modality change");
        input.Next(); input.Send(state,{input.Packet(state,input.primary,point,move)});
        Check(state.pointingDevice==PointingDevice::Mouse,"stale touch update reclaimed active mouse ownership");
        StyleFixture::singleClick=false; state.clickPolicy=ReadShellClickPolicy();
    }
    StopTouchpad(state);
    TouchFixture::failCreate=true;
    Check(!StartTouchRecognizer(state) && !state.touchRecognizer,"failed native context creation left a callback owner");
    TouchFixture::failCreate=false;
    Check(TouchFixture::callbackThread==TouchFixture::owner && !TouchFixture::recognized.empty(),"recognizer callback did not run on owner STA");
    Check(std::any_of(TouchFixture::recognized.begin(),TouchFixture::recognized.end(),[](const auto& o){return o.interactionId==INTERACTION_ID_HOLD;}),"test never reached real native hold recognition");
    Check(std::any_of(TouchFixture::recognized.begin(),TouchFixture::recognized.end(),[](const auto& o){return o.interactionId==INTERACTION_ID_MANIPULATION && o.arguments.manipulation.delta.scale>1.01f;}),"test never reached real native pinch recognition");
    StopTouchpad(state); StopTouchpad(other); state.shellOleInitialized=false;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); SetWindowLongPtrW(other.hwnd,GWLP_USERDATA,0);
    }
    Check(TouchFixture::bindings.empty(),"native callback binding survived context destruction");
    g_touchpadSwipeNavigation=swipe; g_smooth=smooth; g_scrollbar=bars; g_writeFactory=savedWrite;
    std::cout<<"PASS: real Windows recognizer -> marquee GPU pixels before list focus and straight drags; no metric/pixel jump at pinch recognition, proportional post-threshold span scaling; six double-tap cycles, hold/Shell drag, 1/2/3/5-finger vertical pan, staggered swipes, contact replacement, fitting-directory marquee, capture/contact changes, two-finger column/history edges, grouped LTR/RTL pinch at 3 DPIs, coalesced frames, inertia and animated scrollbars; independent contexts\n";
}
