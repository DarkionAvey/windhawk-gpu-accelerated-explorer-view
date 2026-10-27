// Real folder jobs and private HWND messages; count the actual color APIs.
#include <fstream>
static void TestStyleEvents(const std::wstring& fixture) {
    StyleFixture::Session faults;
    const bool showButton=g_showNativeViewToggleButton.exchange(false);
    const bool accent=g_dynamicAccent.exchange(true);
    const bool touchpad=g_touchpadDirectManipulation.exchange(false);
    const auto selection=g_selectionRgb.load();
    struct Restore {
        bool show,accent,touchpad; COLORREF selection;
        ~Restore() {g_showNativeViewToggleButton=show; g_dynamicAccent=accent;
            g_touchpadDirectManipulation=touchpad; g_selectionRgb=selection;}
    } restore{showButton,accent,touchpad,selection};
    const auto root=std::filesystem::path(fixture)/L"style-events";
    for(const auto* leaf:{L"a",L"b"}) {
        std::filesystem::create_directories(root/leaf);
        std::ofstream(root/leaf/L"private.txt")<<"style fixture";
    }
    {
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private style frame",
        WS_POPUP|WS_VISIBLE,-32000,-32000,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(frame!=nullptr,"style frame");
    ViewState first,second;
    struct Cleanup {
        HWND frame; ViewState& first; ViewState& second;
        ~Cleanup() {
            RemoveWindowSubclass(frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass));
            for(auto* state:{&first,&second}) {
                UnregisterTabOverlay(frame,state->activeTabHost,state->hwnd);
                SetWindowLongPtrW(state->hwnd,GWLP_USERDATA,0);
                UnregisterAnimationWindow(state->hwnd); Stop(state->background);
            }
            DestroyWindow(frame);
        }
    } cleanup{frame,first,second};
    for(auto* state:{&first,&second}) {
        state->hwnd=CreateWindowExW(0,L"STATIC",L"style overlay",WS_CHILD|WS_VISIBLE,
            0,0,640,480,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        state->nativeHwnd=CreateWindowExW(0,L"STATIC",L"style native",WS_CHILD|WS_VISIBLE,
            0,0,640,480,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state->hwnd && state->nativeHwnd,"style tab windows");
        state->frameHwnd=frame; state->activeTabHost=state->nativeHwnd;
        state->background->hwnd=state->hwnd;
        state->visible=state->inScope=state->detailsViewActive=true;
        state->folderPath=(root/L"a").wstring();
        ComPtr<AttachmentFolderView> view; view.Attach(new AttachmentFolderView);
        ComPtr<IShellItem> item;
        Check(SUCCEEDED(SHCreateItemFromParsingName(state->folderPath.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&view->folder))),"private style folder");
        view.As(&state->folderView); state->folder=view->folder;
        SetWindowLongPtrW(state->hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
        RegisterTabOverlay(frame,state->activeTabHost,state->hwnd);
        RefreshNativePalette(*state);
    }
    Check(SetWindowSubclass(frame,ExplorerFrameSubclass,reinterpret_cast<UINT_PTR>(ExplorerFrameSubclass),0),
        "style frame subscription");
    const auto probes=[] {return std::array<unsigned,4>{StyleFixture::themeQueries,
        StyleFixture::systemColorQueries,StyleFixture::accentQueries,StyleFixture::activations};};
    const auto drain=[] {
        MSG message{}; unsigned count=0;
        while(PeekMessageW(&message,nullptr,kMsgStyleChanged,kMsgStyleChanged,PM_REMOVE)) {
            OverlayProc(message.hwnd,message.message,message.wParam,message.lParam); ++count;
        }
        return count;
    };
    const auto initial=probes();
    // Force real directory completion and multiple maintenance passes. The
    // pre-0.9.253 timer's first eligible pass would query theme/accent here.
    for(int i=0;i<6;++i) {
        first.folderPath=(root/(i%2 ? L"a" : L"b")).wstring();
        QueueDirectoryRefresh(first,true);
        auto result=WaitForComplete(*first.background,first.directoryToken);
        PublishDirectoryResult(*first.background,std::move(result)); ReceiveDirectoryResults(first);
        Check(first.loaded && first.total==1,"style navigation did not publish its private folder");
        first.lastRefresh=GetTickCount64();
        for(int tick=0;tick<8;++tick) UpdateDirectory(first);
    }
    Check(probes()==initial && first.nativeTextColor==RGB(240,240,240),
        "directory traversal/maintenance queried color providers");
    Check(StyleFixture::visibilityQueries==0,
        "directory maintenance polled Shell visibility settings");
    StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},RGB(210,230,240)}};
    StyleFixture::accent=0xFF18A060;
    SendMessageW(frame,WM_THEMECHANGED,0,0);
    SendMessageW(frame,WM_SYSCOLORCHANGE,0,0);
    SendMessageW(frame,WM_DWMCOLORIZATIONCOLORCHANGED,StyleFixture::accent,TRUE);
    Check(probes()==initial,"frame event performed color work synchronously");
    Check(drain()==2 && first.nativeTextColor==RGB(210,230,240) &&
        second.nativeTextColor==first.nativeTextColor && g_selectionRgb==RGB(0x18,0xA0,0x60),
        "coalesced frame events missed tabs without a toggle button");
    Check(StyleFixture::themeQueries==initial[0]+8 && StyleFixture::activations==initial[3]+2,
        "one event burst performed repeated palette resolutions");
    const auto changed=probes();
    for(UINT action:{UINT(SPI_SETWORKAREA),UINT(SPI_SETWHEELSCROLLLINES)})
        SendMessageW(frame,WM_SETTINGCHANGE,action,0);
    SendMessageW(frame,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(L"Environment"));
    SendMessageW(frame,WM_FONTCHANGE,0,0);
    Check(drain()==2 && probes()==changed,"unrelated/font settings rechecked colors");
    // The original WM_SETTINGCHANGE string is destroyed before owner dispatch.
    {
        std::wstring area=L"ImmersiveColorSet";
        StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},RGB(20,30,40)}};
        SendMessageW(frame,WM_SETTINGCHANGE,0,reinterpret_cast<LPARAM>(area.c_str()));
        area.assign(200,L'X');
    }
    Check(drain()==2 && first.nativeTextColor==RGB(20,30,40) && second.nativeTextColor==RGB(20,30,40),
        "dark-mode event was lost or borrowed its string past dispatch");
    StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},RGB(230,210,190)}};
    for(int i=0;i<12;++i) OverlayProc(first.hwnd,WM_SYSCOLORCHANGE,0,0);
    Check(drain()==1 && first.nativeTextColor==RGB(230,210,190) && second.nativeTextColor==RGB(20,30,40),
        "direct overlay events failed to coalesce or altered another tab");
    StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},RGB(190,210,230)}};
    OverlayProc(first.hwnd,kMsgSettings,0,0);
    Check(drain()==1 && first.nativeTextColor==RGB(190,210,230),"mod settings missed the queued palette refresh");
    const auto after=probes();
    OverlayProc(first.hwnd,kMsgStyleChanged,WPARAM(first.lifetimeToken+1),0);
    Check(drain()==0 && probes()==after,"stale style message queried colors");
    Check(!first.viewToggle && !second.viewToggle,"style events created optional controls");
    // Sent frame broadcasts must reach the same queue as posted/native hints.
    // Inactive tabs keep the hint until they own the active view again.
    struct RestoreHosts {
        ViewState& first; ViewState& second; HWND a,b;
        ~RestoreHosts() {first.activeTabHost=a; second.activeTabHost=b;}
    } hosts{first,second,first.activeTabHost,second.activeTabHost};
    first.activeTabHost=frame;
    first.rowTransitionActive=true;
    StyleFixture::showHidden=StyleFixture::showProtected=true;
    const auto visibilityReads=StyleFixture::visibilityQueries;
    for(int i=0;i<12;++i) SendMessageW(frame,WM_SETTINGCHANGE,0,0);
    Check(StyleFixture::visibilityQueries==visibilityReads,"sent settings broadcast performed synchronous reads");
    const auto drainVisibility=[] {
        MSG message{};
        while(PeekMessageW(&message,nullptr,kMsgShellVisibilityChanged,kMsgShellVisibilityChanged,PM_REMOVE))
            OverlayProc(message.hwnd,message.message,message.wParam,message.lParam);
    };
    drainVisibility();
    Check(StyleFixture::visibilityQueries==visibilityReads && first.nativeSettingsHintPending &&
        second.nativeSettingsHintPending,"animation/inactive guards consumed the settings hint");
    StopRowTransition(first);
    Check(!LayoutAnimationActive(first,true),"private style fixture still animates before settings replay");
    PostShellVisibilityProbe(first); drainVisibility();
    Check(StyleFixture::visibilityQueries==visibilityReads+1 && first.visibility.showHidden &&
        first.visibility.showProtected && !first.nativeSettingsHintPending && second.nativeSettingsHintPending &&
        !second.visibility.showHidden,"sent broadcast was repeated, lost or applied to an inactive tab");
    first.activeTabHost=hosts.a; second.activeTabHost=frame;
    PostShellVisibilityProbe(second); drainVisibility();
    Check(StyleFixture::visibilityQueries==visibilityReads+2 && second.visibility.showHidden &&
        second.visibility.showProtected && !second.nativeSettingsHintPending,"inactive settings hint was lost");
    // This PC must reconcile visibility before requesting its replacement rows.
    second.folderView.Reset(); second.thisPcGpu=true;
    StyleFixture::showHidden=StyleFixture::showProtected=false;
    PostShellVisibilityProbe(second); drainVisibility();
    Check(StyleFixture::visibilityQueries==visibilityReads+3 && !second.visibility.showHidden &&
        !second.visibility.showProtected && second.nativeRowsNeedCapture && second.nativeRowsMessageQueued,
        "This PC recaptured rows using stale visibility settings");
    drain();
    }
    Check(std::filesystem::weakly_canonical(root).parent_path()==std::filesystem::weakly_canonical(fixture),
        "style fixture cleanup escaped its private parent");
    Check(Eventually([&] {
        std::error_code error; std::filesystem::remove_all(root,error); return !error;
    }),"style fixture cleanup did not settle");
    std::cout<<"PASS: zero color/visibility polling across six directory jobs/48 maintenance passes; coalesced style and sent settings events, inactive-tab replay, This PC visibility and stale-message guards\n";
}
