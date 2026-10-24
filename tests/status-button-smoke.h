namespace StatusButtonFixture {
inline HWND held=nullptr;
inline HANDLE entered=nullptr,released=nullptr;
inline void Probe(LPTHREAD_START_ROUTINE procedure,LPVOID context) {
    if(procedure!=TabWorkerEntry || !context) return;
    auto& worker=*static_cast<TabBackground::Worker*>(context);
    if(worker.lane==TabBackground::Lane::Columns && worker.owner->hwnd==held) {
        SetEvent(entered); WaitForSingleObject(released,INFINITE);
    }
}
struct Hold {
    Hold(HWND hwnd) {
        held=hwnd; entered=CreateEventW(nullptr,TRUE,FALSE,nullptr); released=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        AuditFixture::threadProbe=Probe;
    }
    ~Hold() {AuditFixture::threadProbe=nullptr; SetEvent(released); held=nullptr;}
    void Close() {CloseHandle(entered); CloseHandle(released); entered=released=nullptr;}
};
}

static void TestStatusButtonLayout() {
    struct PhysicalCoordinates {
        DPI_AWARENESS_CONTEXT previous=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        ~PhysicalCoordinates() {SetThreadDpiAwarenessContext(previous);}
    } physical;
    struct PerformanceDiagnostics {
        bool previous=g_perf.exchange(true);
        ~PerformanceDiagnostics() {g_perf=previous;}
    } diagnostics;
    // Physical rectangles stay authoritative at fractional/large DPI, with
    // abutting or spaced peers and different native button dimensions.
    for(bool rtl:{false,true}) for(int dpi:{96,120,144,192}) for(int gap:{0,3,9}) {
        StatusButtonSnapshot s; s.query.client={-200,20,800,720}; s.query.band={-200,667,800,720};
        s.query.rtl=rtl; s.query.dpi=dpi;
        const int inner=rtl ? -120 : 620;
        const int outer=inner+(rtl ? -1 : 1)*(47+gap);
        s.buttons={{{inner,673,inner+47,714},nullptr},{{outer,673,outer+47,714},nullptr}};
        RECT placed{}; HWND neighbor=nullptr; int measured=-1;
        Check(StatusTogglePlacement(s,placed,neighbor,measured) && measured==gap &&
            placed.right-placed.left==47 && placed.bottom-placed.top==41 && placed.top==673 &&
            (rtl ? placed.left==inner+47+gap : placed.right==inner-gap),
            "status toggle guessed physical size, peer spacing or RTL position");
        s.buttons.clear(); Check(!StatusTogglePlacement(s,placed,neighbor,measured),
            "hidden native controls were reported as measured geometry");
    }

    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private status frame",
        WS_POPUP|WS_VISIBLE,-31000,-30000,900,700,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND content=CreateWindowExW(0,L"STATIC",L"content",WS_CHILD|WS_VISIBLE,0,0,900,640,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    HWND toolbar=CreateWindowExW(0,TOOLBARCLASSNAMEW,L"private virtual status buttons",
        WS_CHILD|WS_VISIBLE|CCS_NOPARENTALIGN|CCS_NORESIZE|CCS_NODIVIDER|TBSTYLE_FLAT,
        740,650,150,42,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(frame && content && toolbar,"virtual status toolbar fixture");
    FocusFixture::Session focus(frame,toolbar);
    SendMessageW(toolbar,TB_BUTTONSTRUCTSIZE,sizeof(TBBUTTON),0);
    SendMessageW(toolbar,TB_SETBUTTONSIZE,0,MAKELPARAM(41,37));
    TBBUTTON buttons[3]{};
    buttons[0].iBitmap=I_IMAGENONE; buttons[0].idCommand=101; buttons[0].fsState=TBSTATE_ENABLED;
    buttons[1].iBitmap=7; buttons[1].fsStyle=BTNS_SEP;
    buttons[2].iBitmap=I_IMAGENONE; buttons[2].idCommand=102; buttons[2].fsState=TBSTATE_ENABLED;
    Check(SendMessageW(toolbar,TB_ADDBUTTONS,3,reinterpret_cast<LPARAM>(buttons)),"virtual status buttons");
    ViewState state; state.hwnd=MessageWindow(); state.background->hwnd=state.hwnd;
    state.background->notificationLifetime=state.lifetimeToken;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    const auto query=[&] {return CaptureStatusButtonQuery(frame,content,false,120);};
    SetWindowPos(content,nullptr,0,0,1,1,SWP_NOACTIVATE|SWP_NOZORDER);
    Check(query().band.top>=query().client.bottom-Scale96(48,120),
        "partial content bounds exposed the directory tree to status discovery");
    SetWindowPos(content,nullptr,0,0,900,640,SWP_NOACTIVATE|SWP_NOZORDER);
    const auto pump=[] {MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) DispatchMessageW(&message);};
    const auto await=[&] {
        Check(Eventually([&] {pump(); ConsumeStatusButtonGeometry(state); return !state.viewToggleGeometryPending;}),
            "virtual status geometry did not complete asynchronously");
    };
    StatusButtonFixture::Hold hold(state.hwnd);
    RequestStatusButtonGeometry(state,query());
    Check(WaitForSingleObject(StatusButtonFixture::entered,5000)==WAIT_OBJECT_0,"status geometry did not use Columns lane");
    const auto stale=state.viewToggleGeometryToken;
    SetWindowPos(frame,nullptr,-30900,-29900,940,700,SWP_NOACTIVATE|SWP_NOZORDER);
    RequestStatusButtonGeometry(state,query());
    Check(state.viewToggleGeometryToken!=stale,"status resize did not supersede its old request");
    {ExclusiveSrwLock lock(state.background->directoryLock);
        Check(state.background->statusButtonJob && state.background->statusButtonJob->token==state.viewToggleGeometryToken,
            "status mailbox retained obsolete resize requests");}
    SetEvent(StatusButtonFixture::released); await();
    if(state.viewToggleGeometry.buttons.size()!=2)
    {
        std::cerr<<"STATUS virtual accessible="<<state.viewToggleGeometry.accessible<<" host="<<state.viewToggleGeometry.providerHost<<
            " roots="<<state.viewToggleGeometry.roots.size()<<" buttons="<<state.viewToggleGeometry.buttons.size()<<
            " scanned="<<state.viewToggleGeometry.scanned<<" offscreen="<<state.viewToggleGeometry.offscreen<<
            " result=0x"<<std::hex<<state.viewToggleGeometry.result<<std::dec<<"\n";
        const auto& q=state.viewToggleQuery;
        std::cerr<<"STATUS band="<<q.band.left<<","<<q.band.top<<","<<q.band.right<<","<<q.band.bottom<<"\n";
        for(const auto& item:state.viewToggleGeometry.observations)
            std::cerr<<"STATUS control type="<<item.type<<" offscreen="<<item.offscreen<<" rect="<<
                item.bounds.left<<","<<item.bounds.top<<","<<item.bounds.right<<","<<item.bounds.bottom<<"\n";
    }
    Check(state.viewToggleGeometry.accessible && state.viewToggleGeometry.providerHost==toolbar &&
        state.viewToggleGeometry.buttons.size()==2,"non-HWND status buttons silently used guessed placement");
    RECT first{},second{}; SendMessageW(toolbar,TB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&first));
    SendMessageW(toolbar,TB_GETITEMRECT,2,reinterpret_cast<LPARAM>(&second));
    MapWindowPoints(toolbar,nullptr,reinterpret_cast<POINT*>(&first),2);
    MapWindowPoints(toolbar,nullptr,reinterpret_cast<POINT*>(&second),2);
    RECT placed{}; HWND neighbor=nullptr; int gap=-1;
    Check(StatusTogglePlacement(state.viewToggleGeometry,placed,neighbor,gap) && !neighbor &&
        placed.right-placed.left==first.right-first.left && placed.bottom-placed.top==first.bottom-first.top &&
        placed.top==first.top && gap==second.left-first.right && placed.right==first.left-gap,
        "virtual status placement disagrees with actual native item rectangles");
    const auto completed=state.viewToggleGeometryToken;
    RequestStatusButtonGeometry(state,query());
    Check(state.viewToggleGeometryToken==completed && !state.viewToggleGeometryPending,"unchanged status layout restarted accessibility discovery");
    state.viewToggleGeometryPending=true;
    {ExclusiveSrwLock lock(state.background->directoryLock);
        state.background->statusButtonResult=state.viewToggleGeometry;
        ConsumeStatusButtonGeometry(state);
        Check(state.viewToggleLayoutQueued && state.viewToggleGeometryPending && state.background->statusButtonResult,
            "contended status completion lost its continuation");}
    state.viewToggleLayoutQueued=false; ConsumeStatusButtonGeometry(state);
    Check(!state.viewToggleGeometryPending,"retried status completion did not settle");
    state.viewToggleNeighborSearched=false;
    {ExclusiveSrwLock lock(state.background->directoryLock);
        RequestStatusButtonGeometry(state,query());
        Check(state.viewToggleLayoutQueued && !state.viewToggleNeighborSearched,
            "contended status submission lost its continuation");}
    state.viewToggleLayoutQueued=false; RequestStatusButtonGeometry(state,query()); await();
    for(bool hidden:{true,false}) {
        SendMessageW(toolbar,TB_HIDEBUTTON,101,MAKELPARAM(hidden,0)); SendMessageW(toolbar,TB_HIDEBUTTON,102,MAKELPARAM(hidden,0));
        state.viewToggleNeighborSearched=false; RequestStatusButtonGeometry(state,query()); await();
        Check(state.viewToggleGeometry.buttons.size()==(hidden ? 0u : 2u),"hidden/restored virtual buttons did not update fallback eligibility");
    }
    ShowWindow(toolbar,SW_HIDE); state.viewToggleNeighborSearched=false; RequestStatusButtonGeometry(state,query()); await();
    Check(state.viewToggleGeometry.buttons.empty() && !StatusTogglePlacement(state.viewToggleGeometry,placed,neighbor,gap),
        "hidden status host retained an obsolete anchor");
    UnregisterAnimationWindow(state.hwnd); Stop(state.background);
    hold.Close(); SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0); DestroyWindow(state.hwnd); DestroyWindow(frame);
    std::cout<<"PASS: measured status-button size/gap/placement at 96/120/144/192 DPI, LTR/RTL; actual virtual toolbar accessibility, coalesced/stale resize, cached reuse and hidden/restored controls\n";
}
