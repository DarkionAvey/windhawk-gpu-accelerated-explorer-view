// Exercise real USER mirroring, rather than setting rtlLayout on LTR HWNDs.
// All windows remain off screen; focus/cursor APIs use the private fixtures.
static void TestMirroredWindowCoordinates(const std::wstring& directory) {
    Check(SUCCEEDED(OleInitialize(nullptr)),"RTL window STA");
    const auto previousFactory=g_writeFactory;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"RTL window DirectWrite");
    WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW;
    for(const auto name : {L"CabinetWClass",L"CtrlNotifySink",L"SHELLDLL_DefView"}) {
        cls.lpszClassName=name; RegisterClassW(&cls);
    }
    cls.lpszClassName=kOverlayClass; cls.lpfnWndProc=OverlayProc; RegisterClassW(&cls);
    const bool smooth=g_smooth.exchange(false);
    const bool swipes=g_touchpadSwipeNavigation.exchange(false);
    const auto screenRect=[](HWND window) { RECT rect{}; GetWindowRect(window,&rect); return rect; };
    const auto pump=[] {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    };
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    for(const auto [mirrored,rtl] : {std::pair{true,true},std::pair{false,false},std::pair{false,true}}) {
        HWND frame=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|(mirrored ? WS_EX_LAYOUTRTL : 0),
            L"CabinetWClass",L"RTL coordinate fixture",WS_POPUP|WS_VISIBLE,
            -31000,-30000,900,700,nullptr,nullptr,cls.hInstance,nullptr);
        FocusFixture::Session focus(frame,frame);
        HWND host=CreateWindowExW(0,L"CtrlNotifySink",L"",WS_CHILD|WS_VISIBLE,
            75,25,760,600,frame,nullptr,cls.hInstance,nullptr);
        HWND native=CreateWindowExW(rtl ? WS_EX_RTLREADING : 0,L"SHELLDLL_DefView",L"",WS_CHILD|WS_VISIBLE,
            0,0,760,600,host,nullptr,cls.hInstance,nullptr);
        HWND nativeList=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE,
            30,40,650,500,native,nullptr,cls.hInstance,nullptr);
        const auto nativeBounds=screenRect(nativeList);
        const int nativeButtonX=(rtl != mirrored) ? 8 : 852;
        HWND nativeButton=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_VISIBLE,
            nativeButtonX,662,40,32,frame,nullptr,cls.hInstance,nullptr);
        Check(frame && host && native && nativeList && nativeButton,"RTL native hierarchy");
        auto* response=new AttachmentFolderView;
        ComPtr<IFolderView2> view; view.Attach(response);
        ComPtr<IShellItem> folder;
        Check(SUCCEEDED(SHCreateItemFromParsingName(directory.c_str(),nullptr,IID_PPV_ARGS(&folder))) &&
            SUCCEEDED(folder->BindToHandler(nullptr,BHID_SFObject,IID_PPV_ARGS(&response->folder))),"RTL folder binding");
        ComPtr<IShellBrowser> browser;
        AttachView(native,view,response->folder,browser,directory);
        HWND overlay=OverlayForTab(frame);
        auto* state=State(overlay);
        Check(state && !((GetWindowLongPtrW(overlay,GWL_EXSTYLE)) & WS_EX_LAYOUTRTL),
            "GPU HWND inherited mirrored client coordinates on top of mirrored rendering");
        FocusFixture::focus=overlay;
        Check(Eventually([&] { pump(); return state->loaded && state->total>20; }),"RTL names ready");
        if(!mirrored && !rtl) {
            const LANGID previous=UiLocaleFixture::language;
            UiLocaleFixture::language=MAKELANGID(LANG_ARABIC,SUBLANG_ARABIC_SAUDI_ARABIA);
            Check(ShellUiIsRtl(*state),"Arabic UI fallback did not enable RTL without native flags");
            UiLocaleFixture::language=previous;
            Check(!ShellUiIsRtl(*state),"English UI fallback retained Arabic direction");
        }
        // Explicitly choose the test UI direction, independent of the desktop's
        // fallback language. The third case still has genuine mirrored USER HWNDs.
        state->rtlLayout=rtl; MakeTextFormats(*state); state->featureInitializationStep=5;
        state->deferColumnProviders=false; state->nativeColumnsDirty=false;
        state->viewToggleNeighborSearched=false;
        RefreshViewToggleForFrame(frame);
        Check(state->viewToggleNeighbor==nativeButton,"RTL status switch searched the wrong edge");
        auto button=screenRect(state->viewToggle), neighbor=screenRect(nativeButton);
        Check(button.right-button.left==40 && button.bottom-button.top==32 && button.top==neighbor.top &&
            (rtl ? button.left>neighbor.right : button.right<neighbor.left),
            "RTL status switch double-mirrored its parent placement or mismatched native button size");
        Check(!(GetWindowLongPtrW(state->viewToggle,GWL_EXSTYLE)&WS_EX_LAYOUTRTL),"status switch retained mirrored GDI coordinates");
        // Also cover the fallback used for virtualized (non-HWND) status buttons.
        ShowWindow(nativeButton,SW_HIDE); RefreshViewToggleForFrame(frame);
        button=screenRect(state->viewToggle); const auto frameBounds=screenRect(frame);
        Check(rtl ? button.right<frameBounds.left+450 : button.left>frameBounds.left+450,
            "fallback status switch used the wrong physical edge");
        ShowWindow(nativeButton,SW_SHOWNOACTIVATE); RefreshViewToggleForFrame(frame);
        const auto at=[&](float logicalX,float y) {
            RECT client{}; GetClientRect(overlay,&client);
            const int x=static_cast<int>(rtl ? client.right-logicalX : logicalX);
            const auto bounds=screenRect(overlay);
            POINT point{bounds.left+x,bounds.top+static_cast<LONG>(y)};
            MapWindowPoints(nullptr,overlay,&point,1);
            Check(point.x==x,"screen/client mapping reversed a GPU physical point");
            return point;
        };
        PointerFixture::Session pointer;
        for(const UINT mode : {UINT(FVM_DETAILS),UINT(FVM_LIST),UINT(FVM_ICON)}) {
            ApplyViewLayout(*state,{mode,48},false); StopColumnTransition(*state);
            ExplorerColumn name; name.key=PKEY_FileName; name.width=320;
            ExplorerColumn date; date.key=PKEY_DateModified; date.width=260;
            ExplorerColumn size; size.key=PKEY_Size; size.width=260;
            state->columns={name,date,size}; UpdateMetrics(*state); UpdateColumns(*state);
            SetScrollImmediate(*state,0); Paint(*state);
            const auto overlayBounds=screenRect(overlay), hostBounds=screenRect(host);
            Check(EqualRect(&overlayBounds,&hostBounds),"mirrored overlay did not cover its native content host");
            const int row=state->gridView ? 1 : 2;
            const float logical=ListStartX(*state)+DocumentRowLeft(*state,row)+
                (state->gridView ? state->itemWidth*.5f : 90.f);
            const float y=state->headerPx+DocumentRowTop(*state,row)+state->rowPx*.5f;
            POINT point=at(logical,y);
            Check(RowFromPoint(*state,point.y,point.x)==row,"mirrored row/grid hitbox disagreed with visible geometry");
            PointerFixture::screen={overlayBounds.left+point.x,overlayBounds.top+point.y};
            SendMessageW(overlay,WM_MOUSEMOVE,0,MAKELPARAM(point.x,point.y));
            Check(state->hovered==row,"RTL mouse message hovered a different visible item");
            state->hovered=-1; RefreshHoverUnderCursor(*state);
            Check(state->hovered==row,"RTL sampled pointer disagreed with delivered mouse input");
            const auto bar=Scrollbar(*state);
            POINT thumb=at((bar.left+bar.right)*.5f,(bar.top+bar.bottom)*.5f);
            Check(OnScrollbar(*state,thumb.x,thumb.y),"RTL vertical scrollbar hitbox was on the opposite edge");
            if(state->columnsView) {
                const auto horizontal=HorizontalBar(*state);
                POINT end=at(horizontal.right,(horizontal.top+horizontal.bottom)*.5f);
                Check(OnHorizontalBar(*state,end.x,end.y),"RTL horizontal scrollbar hitbox mismatch");
                state->draggingHorizontal=true; state->horizontalGrabOffset=0; state->capturedPointer.reset();
                UpdateCapturedPointer(*state,end); state->draggingHorizontal=false;
                Check(state->horizontalScroll>0,"RTL horizontal drag moved in the wrong direction");
                SetHorizontalScroll(*state,0);
                state->manipulationEnabled=state->manipulationFrameActive=true;
                state->swipeReleased=false; state->touchpadPinching=false;
                OverlayProc(overlay,kMsgManipulationPan,static_cast<WPARAM>(static_cast<LONG>(rtl ? 30*256 : -30*256)),0);
                Check(state->horizontalScroll>0,"RTL touchpad pan moved opposite to physical content movement");
                state->manipulationEnabled=state->manipulationFrameActive=false;
                SetHorizontalScroll(*state,0);
                const int headerX=at(ListStartX(*state)+80,5).x;
                Check(HeaderColumnAt(*state,headerX,5)==0,"RTL column heading hitbox was mirrored twice");
            }
            if(state->gridView) {
                const float left=ListStartX(*state)+DocumentRowLeft(*state,row);
                const float top=state->headerPx+DocumentRowTop(*state,row);
                PrepareMarquee(*state,at(left+3,top+3),false);
                UpdateMarquee(*state,at(left+state->itemWidth-3,top+state->rowPx-3));
                Check(state->selectedFlags[row] && !state->selectedFlags[0],"RTL grid marquee selected the opposite cell");
                state->marqueePossible=state->marqueeActive=state->marqueeFrameActive=false;
            }
            // Real sibling EDIT placement must match the pixels painted inside
            // the independently LTR overlay, even when their parent is mirrored.
            state->selectedFlags.assign(state->entries.size(),0); state->selectedFlags[row]=1;
            state->selected=state->anchor=row;
            BeginRename(*state,row);
            Check(state->rename.editor!=nullptr,"RTL rename editor did not open");
            Check(!(GetWindowLongPtrW(state->rename.editor,GWL_EXSTYLE)&WS_EX_LAYOUTRTL),"rename EDIT inherited geometric mirroring");
            SetWindowTextW(state->rename.editor,L"اسم عربي mixed filename with spaces.txt");
            RepositionRenameEdit(*state);
            const auto editor=screenRect(state->rename.editor);
            const auto item=LayoutItem(*state,{state->headerPx+DocumentRowTop(*state,row)-state->scrollNow,
                DisplayRowHeight(*state,row),1.f,DocumentRowLeft(*state,row)});
            const int leading=at(item.text.left,0).x+overlayBounds.left;
            Check(std::abs((rtl ? editor.right : editor.left)-leading)<=1 &&
                editor.left>=overlayBounds.left && editor.right<=overlayBounds.right,
                "RTL rename editor was not anchored to its visible filename");
            CancelRename(*state);
        }
        // A status view command returns list focus after native dispatch. An
        // already queued focus-loss paint must coalesce with that intent.
        HWND address=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,120,24,frame,nullptr,cls.hInstance,nullptr);
        HWND statusHost=CreateWindowExW(0,L"STATIC",L"shared status canvas",WS_CHILD|WS_VISIBLE,
            0,662,900,32,frame,nullptr,cls.hInstance,nullptr);
        Check(address && statusHost,"view command focus guard fixture");
        {
            for(int route=0;route<4;++route) for(int guard=0;guard<8;++guard) {
                const HWND source=route==1 ? statusHost : route==2 ? frame : nativeButton;
                FocusFixture::focus=source; FocusFixture::foreground=frame;
                RefreshInputFocusIndicators(*state);
                QueueInputFocusUpdate(overlay);
                MSG click{}; click.hwnd=source; click.message=WM_LBUTTONUP;
                POINT point{(neighbor.left+neighbor.right)/2,(neighbor.top+neighbor.bottom)/2};
                MapWindowPoints(nullptr,source,&point,1); click.lParam=MAKELPARAM(point.x,point.y);
                if(route==3) { click.hwnd=frame; click.message=WM_COMMAND; click.wParam=MAKEWPARAM(1,BN_CLICKED);
                    click.lParam=reinterpret_cast<LPARAM>(nativeButton); }
                if(route==3) SendMessageW(frame,click.message,click.wParam,click.lParam);
                else { NativeViewInput input(NativeViewInput::Owner(&click)); }
                Check(FocusFixture::focus==source,"status view command changed focus inside native dispatch");
                MSG notification{};
                Check(PeekMessageW(&notification,overlay,kMsgInputFocusChanged,kMsgInputFocusChanged,PM_REMOVE),"status focus return did not queue");
                OverlayProc(overlay,notification.message,notification.wParam,notification.lParam);
                Check(FocusFixture::focus==source && state->viewControlFocusReturn && state->nativeModeSignalQueued,
                    "status focus request was consumed before the queued mode/presentation commit");
                const auto token=state->token; const auto tab=state->activeTabHost;
                const int selected=state->selected;
                response->mode=state->gridView ? FVM_DETAILS : FVM_ICON;
                response->modeProbe=[&] {
                    QueueInputFocusUpdate(overlay);
                    MSG nested{};
                    Check(PeekMessageW(&nested,overlay,kMsgInputFocusChanged,kMsgInputFocusChanged,PM_REMOVE),
                        "native mode pump did not receive its focus notification");
                    OverlayProc(overlay,nested.message,nested.wParam,nested.lParam);
                    Check(FocusFixture::focus==source && state->viewControlFocusReturn,
                        "native COM message pump consumed the focus request inside the mode transaction");
                };
                while(PeekMessageW(&notification,overlay,kMsgShellVisibilityChanged,kMsgShellVisibilityChanged,PM_REMOVE))
                    OverlayProc(overlay,notification.message,notification.wParam,notification.lParam);
                if(guard==1) FocusFixture::focus=address;
                if(guard==2) FocusFixture::foreground=nullptr;
                if(guard==3) ++state->token;
                if(guard==4) state->activeTabHost=nativeButton;
                if(guard==5) state->rename.editor=address;
                if(guard==6) FocusFixture::focus=nullptr;
                if(guard==7) FocusFixture::rejected=native;
                const HWND before=FocusFixture::focus;
                Check(PeekMessageW(&notification,overlay,kMsgInputFocusChanged,kMsgInputFocusChanged,PM_REMOVE),"status focus return did not queue");
                OverlayProc(overlay,notification.message,notification.wParam,notification.lParam);
                if(guard==0 || guard==6 || guard==7) {
                    if(FocusFixture::focus!=(guard!=7 ? native : overlay) || !state->inputFocusIndicators)
                        std::cerr<<"status return guard="<<guard<<" route="<<route
                            <<" source="<<source<<" before="<<before<<" after="<<FocusFixture::focus
                            <<" expected="<<(guard!=7 ? native : overlay)<<" indicators="<<state->inputFocusIndicators
                            <<" pending="<<state->viewControlFocusReturn<<" mode="<<state->layoutNativeMode
                            <<" visible="<<state->visible<<" scope="<<state->inScope<<" supported="<<state->detailsViewActive<<'\n';
                    Check(FocusFixture::focus==(guard!=7 ? native : overlay) && state->inputFocusIndicators,
                        "status view button left keyboard focus and hover feedback outside the GPU list");
                    Check(state->selected==selected && state->layoutNativeMode==response->mode,
                        "status view commit lost the selected item or left the old layout");
                    MSG escape{}; escape.hwnd=FocusFixture::focus; escape.message=WM_KEYDOWN; escape.wParam=VK_ESCAPE;
                    Check(escape.hwnd==overlay || OverlayKeyboardTarget(escape)==overlay,
                        "status return restored arrows but left Escape outside the overlay input route");
                } else {
                    if(FocusFixture::focus!=before) std::cerr<<"status guard="<<guard<<" route="<<route
                        <<" before="<<before<<" after="<<FocusFixture::focus<<" token="<<state->token<<" saved="<<token<<'\n';
                    Check(FocusFixture::focus==before,"stale status command stole focus from a newer control/tab/foreground/editor");
                }
                Check(!PeekMessageW(&notification,overlay,kMsgInputFocusChanged,kMsgInputFocusChanged,PM_NOREMOVE),"status focus return duplicated its coalesced wake");
                state->rename.editor=nullptr; state->token=token; state->activeTabHost=tab;
                FocusFixture::foreground=frame; FocusFixture::rejected=nullptr;
            }
        }
        DestroyWindow(address); DestroyWindow(statusHost);
        // History direction and its feedback use logical displacement, while
        // DirectManipulation supplies physical finger deltas.
        g_touchpadSwipeNavigation=true;
        for(const int direction : {1,-1}) {
            ClearSwipeFeedback(*state); state->touchpadPanX=state->touchpadPanY=0;
            state->manipulationEnabled=state->manipulationFrameActive=true;
            state->swipeReleased=false; state->touchpadPinching=false;
            const LONG physical=static_cast<LONG>(SwipeCommitDistance(*state)*1.2f*direction*(rtl ? -1 : 1)*256);
            OverlayProc(overlay,kMsgManipulationPan,static_cast<WPARAM>(physical),0);
            Check(state->swipeHorizontal && state->swipeFeedbackDirection==direction,"RTL swipe feedback used the wrong history direction");
            OverlayProc(overlay,kMsgManipulationStatus,2,0); // Posted finger-up/inertia protocol.
            MSG navigation{};
            Check(PeekMessageW(&navigation,overlay,kMsgManipulationNavigate,kMsgManipulationNavigate,PM_REMOVE) &&
                bool(navigation.wParam)==(direction>0),"RTL swipe release committed the opposite history action");
            ClearSwipeFeedback(*state); state->manipulationEnabled=state->manipulationFrameActive=false;
        }
        g_touchpadSwipeNavigation=false;
        DestroyWindow(overlay);
        const auto restored=screenRect(nativeList);
        Check(EqualRect(&restored,&nativeBounds) && IsWindowVisible(nativeList),"mirrored native restoration changed physical viewport placement");
        DestroyWindow(frame); pump();
    }
    g_touchpadSwipeNavigation=swipes; g_smooth=smooth;
    g_writeFactory=previousFactory; OleUninitialize();
    std::cout<<"PASS: actual mirrored/LTR hosts and hits; direct/shared/frame/sent status commands retain focus intent through queued mode commit, GPU fallback after rejected native focus, and stale/editor/tab/foreground guards\n";
}
