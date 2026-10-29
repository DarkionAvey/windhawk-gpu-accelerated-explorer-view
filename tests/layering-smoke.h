// Exercise real foreground pixels, then replay prepared mode planes through
// the shared append operation using Microsoft's documented null-reference
// sibling ordering. The replay checks the submitted contract, not scan-out.
static void TestZoomHeaderLayering(HWND frame) {
    const bool automatic=g_autoRefitColumns.exchange(false);
    for(bool rtl : {false,true}) {
        ViewState state;
        state.hwnd=CreateWindowExW(0,L"STATIC",L"zoom header layering",WS_CHILD,0,0,640,360,
            frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"zoom layering window");
        state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
        FocusFixture::Session focus(frame,state.hwnd);
        state.visible=state.inScope=state.loaded=state.detailsViewActive=true;
        state.firstContentSubmittedAt=1; state.featureInitializationStep=kOverlayFeatureSteps;
        state.rtlLayout=rtl;
        state.nativeTextColor=RGB(255,0,0); state.nativeHeaderTextColor=RGB(255,255,255);
        state.entries.resize(100); state.total=100; state.selectedFlags.assign(100,1); state.selected=4;
        for(int i=0;i<100;++i) {
            auto& e=state.entries[i]; e.name=L"Zoomed body label "+std::to_wstring(i);
            e.path=L"zoom-layer:"+e.name; e.folder=true;
            e.type=L"Modified body"; e.detailsReady=true; e.columnEpoch=state.columnEpoch;
            e.propertyEpoch=state.propertyEpoch; e.cells={{PKEY_DateModified,e.type}};
        }
        ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=330;
        ExplorerColumn date; date.key=PKEY_DateModified; date.title=L"Date modified"; date.width=270;
        state.columns={name,date};
        ApplyViewLayout(state,{FVM_DETAILS,48},false); UpdateMetrics(state); UpdateColumns(state);
        Check(EnsureTarget(state),"zoom layering target"); UpdateRefreshRate(state);
        auto& icon=state.defaultFolderIcon;
        icon.pixels.width=icon.pixels.height=16; icon.pixels.stride=64;
        icon.pixels.data.resize(1024);
        for(size_t p=0;p<icon.pixels.data.size();p+=4) { icon.pixels.data[p+1]=255; icon.pixels.data[p+3]=255; }
        Check(UploadIconBitmap(state,icon),"zoom layering icon");
        state.selected=-1; state.selectedFlags.assign(100,0);
        const auto checkBands=[&](bool groups) {
            ComPtr<ID2D1Bitmap1> cpu;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                state.backbuffer->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(state.backbuffer->GetPixelSize(),nullptr,0,&props,&cpu)) &&
                SUCCEEDED(cpu->CopyFromBitmap(nullptr,state.backbuffer.Get(),nullptr)),"zoom layering readback");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"zoom layering map");
            const auto colored=[&](int from,int to,bool body=false) {
                unsigned result=0;
                const int left=body ? int(std::ceil(rtl ? 640-ListEndX(state) : ListStartX(state))) : 0;
                const int right=body ? int(std::floor(rtl ? 640-ListStartX(state) : ListEndX(state))) : 640;
                const int bottom=body ? ContentViewport(state).bottom : 360;
                for(int y=std::max(0,from);y<std::min(bottom,to);++y) for(int x=left;x<right;++x) {
                    const auto p=map.bits+y*map.pitch+x*4;
                    result+=p[0]!=p[1] || p[1]!=p[2];
                }
                return result;
            };
            const auto header=colored(0,int(state.headerPx));
            unsigned section=0;
            if(groups) for(size_t i=0;i<state.sections.size();++i) {
                const auto v=LayoutGroupHeader(state,i,1.f);
                section+=colored(int(std::ceil(std::max(state.headerPx,v.top))),int(std::floor(v.top+v.height)),true);
            }
            cpu->Unmap();
            Check(!header,"zoomed item pixels drew over column headers");
            Check(!section,"oversized item pixels drew through natural group-header bands");
        };
        for(int height : {24,48,96,24}) {
            ApplyRowHeight(state,height);
            for(double ms : {1.,45.,90.,179.,181.}) {
                QueryPerformanceCounter(&state.zoomStartedQpc);
                state.zoomStartedQpc.QuadPart-=static_cast<LONGLONG>(ms*state.qpcFrequency/1000.);
                AdvanceRowHeight(state);
                for(float partial : {.1f,.5f,.9f}) {
                    state.scrollNow=state.scrollTarget=state.rowPx*(4.f+partial);
                    auto swap=std::move(state.swapChain); Paint(state); state.swapChain=std::move(swap);
                    checkBands(false);
                }
            }
        }
        state.grouping.enabled=true;
        for(int i=0;i<state.total;++i) {
            state.entries[i].groupKey=std::to_wstring(i/2); state.entries[i].groupTitle=L"Group heading";
        }
        RebuildGroupSections(state); state.scrollNow=state.scrollTarget=0.f;
        state.iconPx=state.rowPx*3.f; // Defensive: item geometry can outgrow its allocated row during reflow.
        { auto swap=std::move(state.swapChain); Paint(state); state.swapChain=std::move(swap); }
        checkBands(true);
        // Reflow can overlap neighboring cells. Every label must remain above
        // every icon/shadow, including an icon belonging to a later item.
        state.grouping.enabled=false; state.entries.resize(2); state.total=2;
        state.selectedFlags.assign(2,0); RebuildGroupSections(state);
        ApplyViewLayout(state,{FVM_SMALLICON,48},false); FinishColumnTransition(state);
        state.rowPx=64.f; state.fontPx=16.f; state.iconPx=48.f;
        state.itemWidth=300.f; state.itemsPerRow=2; state.scrollNow=state.scrollTarget=0.f;
        Check(MakeTextFormats(state,true),"overlapping-cell font");
        state.entries[0].name=L"LABEL LABEL LABEL"; state.entries[1].name.clear();
        for(int i=0;i<2;++i) {
            auto& entry=state.entries[i]; entry.folder=false; entry.path=L"overlap:"+std::to_wstring(i)+L".png";
            std::wstring lookup; bool attrs=false;
            auto& record=state.icons[IconKey(state,entry,lookup,attrs)];
            record.bitmap.Reset(); record.bitmapAttempted=false;
            record.pixels.width=record.pixels.height=16; record.pixels.stride=64;
            record.pixels.data.assign(1024,0);
            for(size_t p=0;p<record.pixels.data.size();p+=4) { record.pixels.data[p+1]=255; record.pixels.data[p+3]=255; }
            record.pixels.thumbnailWidth=record.pixels.thumbnailHeight=16;
            Check(UploadIconBitmap(state,record),"overlapping-cell thumbnail");
        }
        const auto geometry=LayoutItem(state,{0.f,64.f,1.f,0.f,300.f});
        state.rowMotion={{0,64.f,0.f,0.f,300.f,false,1.f},
            {1,64.f,0.f,geometry.text.left-ListStartX(state)-Scale96(8,DpiOf(state.hwnd)),300.f,false,1.f}};
        state.rowMotionFirst=0; state.rowTransitionOldScroll=0.f;
        state.rowTransitionActive=state.zoomLayoutTransition=true;
        // Hold the exact initial animation phase for both pixel comparisons.
        QueryPerformanceCounter(&state.rowTransitionStartedQpc);
        state.rowTransitionStartedQpc.QuadPart+=static_cast<LONGLONG>(state.qpcFrequency*3600.);
        std::wstring lookup; bool attrs=false;
        auto& covering=state.icons.at(IconKey(state,state.entries[1],lookup,attrs));
        auto coveringBitmap=std::move(covering.bitmap);
        const auto snapshot=[&] {
            auto swap=std::move(state.swapChain); Paint(state); state.swapChain=std::move(swap);
            ComPtr<ID2D1Bitmap1> cpu;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                state.backbuffer->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(state.backbuffer->GetPixelSize(),nullptr,0,&props,&cpu)) &&
                SUCCEEDED(cpu->CopyFromBitmap(nullptr,state.backbuffer.Get(),nullptr)),"overlapping-cell readback");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"overlapping-cell map");
            std::vector<BYTE> result(640*360*4);
            for(int y=0;y<360;++y) memcpy(result.data()+y*640*4,map.bits+y*map.pitch,640*4);
            cpu->Unmap(); return result;
        };
        const auto uncovered=snapshot(); covering.bitmap=std::move(coveringBitmap);
        const auto overlapped=snapshot(); unsigned labelPixels=0;
        for(size_t p=0;p<uncovered.size();p+=4) if(uncovered[p]==0 && uncovered[p+1]==0 && uncovered[p+2]==255 && uncovered[p+3]==255) {
            ++labelPixels;
            Check(memcmp(uncovered.data()+p,overlapped.data()+p,4)==0,"a neighboring thumbnail covered foreground text during reflow");
        }
        Check(labelPixels>20,"overlapping-cell fixture had no opaque label pixels");
        StopRowTransition(state);
        UnregisterAnimationWindow(state.hwnd); DropTarget(state); Stop(state.background); DestroyWindow(state.hwnd);
    }
    g_autoRefitColumns=automatic;
    std::cout<<"PASS: LTR/RTL animated zoom/header bands, oversized natural group-header protection and labels above neighboring reflow thumbnails\n";
}

static void TestHighlightLayering(HWND frame) {
    const auto selection=g_selectionAlpha.exchange(80), hover=g_hoverAlpha.exchange(80);
    const auto combined=g_bothAlpha.exchange(80), marquee=g_marqueeAlpha.exchange(80);
    const auto accent=g_selectionRgb.exchange(RGB(0,96,220));
    const auto filenames=g_filenameOnly.exchange(ViewOption::AlwaysOff);
    const bool zebra=g_zebra.exchange(false);
    for(bool rtl : {false,true}) {
        ViewState state;
        state.hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"highlight layering",
            WS_POPUP|WS_VISIBLE,-32000,-32000,640,320,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state.hwnd!=nullptr,"layering window");
        state.nativeHwnd=state.hwnd; state.frameHwnd=state.activeTabHost=frame; state.rtlLayout=rtl;
        FocusFixture::Session focus(state.frameHwnd,state.nativeHwnd);
        HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,20,20,state.hwnd,
            nullptr,GetModuleHandleW(nullptr),nullptr);
        HWND chromeControl=CreateWindowExW(0,L"STATIC",L"",WS_CHILD,0,0,20,20,frame,
            nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(edit && chromeControl,"focus feedback controls");
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        state.visible=state.inScope=state.detailsViewActive=state.loaded=state.columnsView=true;
        state.firstContentSubmittedAt=GetTickCount64(); state.featureInitializationStep=5;
        state.nativeTextColor=RGB(255,255,255);
        UpdateMetrics(state); Check(EnsureTarget(state),"layering GPU target");
        ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=330;
        ExplorerColumn type; type.key=PKEY_ItemTypeText; type.title=L"Type"; type.width=250;
        state.columns={name,type}; UpdateColumns(state);
        Entry e; e.name=L"Wallpapers"; e.path=L"layering:Wallpapers"; e.folder=true;
        e.type=L"File folder"; e.detailsReady=true; e.propertyEpoch=state.propertyEpoch;
        state.entries={e}; state.total=1; state.selectedFlags={0};
        auto icon=[&] {
            std::wstring path; bool attrs=false;
            const auto key=IconKey(state,state.entries[0],path,attrs);
            auto& record=key==kDefaultFolderIconKey ? state.defaultFolderIcon : state.icons[key];
            record.bitmap.Reset(); record.bitmapAttempted=false;
            record.pixels.width=record.pixels.height=8; record.pixels.stride=32;
            record.pixels.data.assign(8*32,255);
            Check(UploadIconBitmap(state,record),"layering opaque icon");
        };
        auto pixels=[&](ID2D1Bitmap1* source) {
            const auto size=source->GetPixelSize();
            ComPtr<ID2D1Bitmap1> cpu;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                source->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(size,nullptr,0,&props,&cpu)) &&
                SUCCEEDED(cpu->CopyFromBitmap(nullptr,source,nullptr)),"layering readback");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"layering map");
            std::vector<BYTE> result(size.width*size.height*4);
            for(UINT32 y=0;y<size.height;++y) memcpy(result.data()+y*size.width*4,map.bits+y*map.pitch,size.width*4);
            cpu->Unmap(); return result;
        };
        const auto checkForeground=[&](const auto& reference,const auto& actual,UINT32 from,UINT32 to) {
            unsigned opaque=0;
            for(size_t p=size_t(from)*640*4;p<std::min(actual.size(),size_t(to)*640*4);p+=4)
                if(reference[p]==255 && reference[p+1]==255 && reference[p+2]==255 && reference[p+3]==255) {
                    ++opaque;
                    Check(memcmp(&reference[p],&actual[p],4)==0,"highlight tinted an opaque label/icon foreground pixel");
                }
            Check(opaque>40,"layering fixture contained no opaque foreground");
        };
        auto paint=[&] { auto swap=std::move(state.swapChain); Paint(state); state.swapChain=std::move(swap); };
        for(UINT mode : {FVM_DETAILS,FVM_LIST,FVM_ICON}) {
            ApplyViewLayout(state,{mode,96},false); FinishColumnTransition(state); icon();
            state.selected=-1; state.selectedFlags[0]=0; state.hovered=state.shellDropHotRow=-1;
            state.keyboardCaretVisible=state.marqueeActive=state.marqueeFading=false;
            for(int n=0;n<30;++n) { paint(); if(!state.textRasterPending) break; }
            const auto reference=pixels(state.backbuffer.Get());
            for(int feedback=0;feedback<7;++feedback) {
                state.selected=0; state.selectedFlags[0]=feedback==0 || feedback==2;
                state.hovered=feedback==1 || feedback==2 ? 0 : -1;
                state.shellDropHotRow=feedback==3 ? 0 : -1;
                state.keyboardCaretVisible=feedback==4;
                state.marqueeActive=feedback==5; state.marqueeFading=feedback==6;
                state.marqueeEndTick=GetTickCount64()+kUiTransitionDurationMs;
                state.marqueeStart={0,LONG(state.headerPx)};
                state.marqueeStartDocumentY=0;
                state.marqueeCurrent={640,LONG(state.headerPx+state.rowPx)};
                paint();
                checkForeground(reference,pixels(state.backbuffer.Get()),UINT32(state.headerPx),UINT32(state.headerPx+state.rowPx));
                if(feedback!=3) { // A Shell drop target is independent of keyboard focus.
                    const auto flags=state.selectedFlags;
                    const int selected=state.selected,hovered=state.hovered;
                    for(HWND focused : {edit,chromeControl,HWND{}}) {
                        FocusFixture::focus=focused; paint();
                        Check(pixels(state.backbuffer.Get())==reference,
                            "unfocused selection/hover/caret/marquee left visible pixels");
                    }
                    FocusFixture::focus=state.nativeHwnd; FocusFixture::foreground=nullptr; paint();
                    Check(pixels(state.backbuffer.Get())==reference && state.selectedFlags==flags &&
                        state.selected==selected && state.hovered==hovered,
                        "background Explorer retained highlights or lost logical selection/hover");
                    FocusFixture::foreground=frame; paint();
                    Check(state.inputFocusIndicators,"returning native-list focus did not restore feedback");
                    if(feedback<=2) Check(pixels(state.backbuffer.Get())!=reference,
                        "focus return retained selection logically but failed to restore its pixels");
                    if(feedback==0) {
                        const auto selectedPixels=pixels(state.backbuffer.Get());
                        // Real USER popup visibility/ownership, with only focus
                        // substituted. This is a pixel component check; live
                        // Explorer separately verifies the WinUI handoff.
                        HWND popup=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"menu feedback",
                            WS_POPUP|WS_VISIBLE,-32000,-32000,120,120,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
                        Check(popup!=nullptr,"selection-feedback owned popup");
                        FocusFixture::focus=chromeControl;
                        paint();
                        Check(pixels(state.backbuffer.Get())==reference,
                            "an unrelated owned popup acquired selection feedback");
                        state.nativeContextMenuToken=state.token;
                        state.nativeContextMenuFocus=chromeControl;
                        RefreshInputFocusIndicators(state);
                        paint();
                        Check(!OverlayHasInputFocus(state) && !state.inputFocusIndicators &&
                            state.selectionFeedbackVisible && pixels(state.backbuffer.Get())==selectedPixels,
                            "native popup lost selected pixels or granted keyboard input to the list");
                        const auto model=CapturePresentationModel(state);
                        Check(!model->inputFocus && model->selectionFeedback,
                            "presenter conflated native-menu selection feedback with input focus");
                        ShowWindow(popup,SW_HIDE); RefreshInputFocusIndicators(state); paint();
                        Check(!state.selectionFeedbackVisible && pixels(state.backbuffer.Get())==reference,
                            "dismissed native popup retained selected pixels");
                        Check(state.nativeContextMenuFocus==chromeControl,
                            "a popup replacement gap discarded the native invocation's focus host");
                        HWND replacement=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"replacement menu feedback",
                            WS_POPUP|WS_VISIBLE,-32000,-32000,120,120,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
                        Check(replacement!=nullptr,"replacement owned popup");
                        SetWindowPos(popup,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                        RefreshInputFocusIndicators(state); paint();
                        Check(pixels(state.backbuffer.Get())==selectedPixels,
                            "replacement popup lost feedback behind an older hidden enabled popup");
                        ShowWindow(replacement,SW_HIDE);
                        state.nativeContextMenuFocus=nullptr;
                        state.nativeContextMenuPending=true;
                        FocusFixture::focus=frame;
                        RefreshInputFocusIndicators(state);
                        Check(state.nativeContextMenuPending && !state.nativeContextMenuFocus,
                            "frame focus completed an asynchronous native menu request");
                        FocusFixture::focus=chromeControl;
                        RefreshInputFocusIndicators(state); paint();
                        Check(state.nativeContextMenuPending && !state.nativeContextMenuFocus &&
                            pixels(state.backbuffer.Get())==reference,
                            "early delegated focus lost the pending request or granted feedback without a popup");
                        ShowWindow(replacement,SW_SHOWNA);
                        RefreshInputFocusIndicators(state); paint();
                        Check(!state.nativeContextMenuPending && state.nativeContextMenuFocus==chromeControl &&
                            !state.inputFocusIndicators && pixels(state.backbuffer.Get())==selectedPixels,
                            "the asynchronous replacement popup failed to complete its focus handoff");
                        FocusFixture::focus=edit; paint();
                        Check(pixels(state.backbuffer.Get())==reference,"menu feedback followed a different focus owner");
                        FocusFixture::focus=chromeControl; ++state.nativeContextMenuToken; paint();
                        Check(pixels(state.backbuffer.Get())==reference,"menu feedback survived a folder generation change");
                        state.nativeContextMenuToken=state.token;
                        FocusFixture::ownerEnabled=false; paint();
                        Check(pixels(state.backbuffer.Get())==reference,"modal disabled frame retained menu selection feedback");
                        FocusFixture::ownerEnabled=true; FocusFixture::foreground=nullptr; paint();
                        Check(pixels(state.backbuffer.Get())==reference && state.selectedFlags==flags,
                            "background frame retained menu selection feedback or changed logical selection");
                        FocusFixture::foreground=frame;
                        DestroyWindow(replacement); RefreshInputFocusIndicators(state); paint();
                        Check(!state.selectionFeedbackVisible && pixels(state.backbuffer.Get())==reference,
                            "destroyed popup retained selection feedback");
                        state.nativeContextMenuFocus=nullptr;
                        DestroyWindow(popup);
                        FocusFixture::focus=state.nativeHwnd; paint();
                    }
                }
            }
        }
        // Focus delivery is coalesced and only invalidates after USER's
        // activation transaction, without importing or clearing selection.
        ValidateRect(state.hwnd,nullptr); FocusFixture::foreground=nullptr;
        QueueInputFocusUpdate(state.hwnd); QueueInputFocusUpdate(state.hwnd);
        MSG message{};
        Check(state.inputFocusMessageQueued && !GetUpdateRect(state.hwnd,nullptr,FALSE) &&
            PeekMessageW(&message,state.hwnd,kMsgInputFocusChanged,kMsgInputFocusChanged,PM_REMOVE),
            "focus callback painted synchronously or failed to queue");
        OverlayProc(state.hwnd,message.message,message.wParam+1,message.lParam);
        Check(state.inputFocusMessageQueued && state.inputFocusIndicators,
            "stale focus notification changed a different overlay lifetime");
        OverlayProc(state.hwnd,message.message,message.wParam,message.lParam);
        Check(!state.inputFocusMessageQueued && !state.inputFocusIndicators &&
            GetUpdateRect(state.hwnd,nullptr,FALSE) &&
            !PeekMessageW(&message,state.hwnd,kMsgInputFocusChanged,kMsgInputFocusChanged,PM_REMOVE),
            "focus notifications did not coalesce into one deferred repaint");
        FocusFixture::foreground=frame;
        state.marqueeActive=state.marqueeFading=state.keyboardCaretVisible=false;
        ApplyViewLayout(state,{FVM_DETAILS,48},false); FinishColumnTransition(state); icon();
        state.selected=0; state.selectedFlags[0]=1; state.hovered=0; state.shellDropHotRow=-1;
        SetColumnViewMode(state,false); Paint(state);
        Check(state.modeComposition.published,"layering mode scene was not prepared");
        const auto origin=state.modeComposition.started.QuadPart;
        FocusFixture::foreground=nullptr; Paint(state);
        const auto inactive=pixels(state.modeComposition.background.bitmap.Get());
        Check(std::none_of(inactive.begin()+size_t(state.modeComposition.header)*640*4,inactive.end(),
            [](BYTE b) { return b!=0; }) && state.modeComposition.started.QuadPart==origin,
            "committed mode scene retained inactive selection pixels or restarted its animation");
        FocusFixture::foreground=frame; Paint(state);
        struct Visual {
            ID2D1Bitmap1* bitmap=nullptr;
            std::vector<Visual*> children;
            HRESULT AddVisual(Visual* child,BOOL above,Visual* reference) {
                Check(!reference,"ordering fixture expects an append without a reference sibling");
                // The documented Windows null-reference contract.
                children.insert(above ? children.begin() : children.end(),child); return S_OK;
            }
        } root, background{state.modeComposition.background.bitmap.Get()},
            names{state.modeComposition.names.bitmap.Get()}, metadata{state.modeComposition.metadata.bitmap.Get()},
            chrome{state.modeComposition.chrome.bitmap.Get()};
        [&]<class V>(V& parent,const std::array<V*,4>& layers) {
            // Historical sources lack the new shared append operation; their
            // real marquee pixel check above still runs and exposes the tint.
            if constexpr(requires { AppendCompositionVisual(&parent,layers[0]); }) {
                for(auto* layer : layers) Check(SUCCEEDED(AppendCompositionVisual(&parent,layer)),"append composition layer");
                Check(parent.children.front()==layers.front() && parent.children.back()==layers.back(),
                    "composition append reversed background and chrome");
                const auto reference=pixels(state.modeComposition.names.bitmap.Get());
                state.target->SetTarget(state.backbuffer.Get());
                state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
                for(auto* layer : parent.children) if(layer->bitmap) state.target->DrawBitmap(layer->bitmap);
                Check(SUCCEEDED(state.target->EndDraw()),"mode layer replay");
                checkForeground(reference,pixels(state.backbuffer.Get()),UINT32(state.modeComposition.header),
                    UINT32(state.modeComposition.header+state.rowPx));
            }
            if constexpr(requires { PlaceItemContentVisual(&parent,layers[0],true); }) {
                V body;
                Check(SUCCEEDED(PlaceItemContentVisual(&body,layers[2],false)) &&
                    SUCCEEDED(PlaceItemContentVisual(&body,layers[1],true)),"place moved Name content");
                Check(body.children.front()==layers[1] && body.children.back()==layers[2],
                    "moving Name behind another column placed icons above metadata text");
                Check(parent.children.front()==layers.front() && parent.children.back()==layers.back(),
                    "body ordering moved background or chrome");
            }
        }(root,std::array{&background,&names,&metadata,&chrome});
        UnregisterAnimationWindow(state.hwnd); DropTarget(state);
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
        DestroyWindow(edit); DestroyWindow(chromeControl); DestroyWindow(state.hwnd);
    }
    g_selectionAlpha=selection; g_hoverAlpha=hover; g_bothAlpha=combined; g_marqueeAlpha=marquee;
    g_selectionRgb=accent; g_filenameOnly=filenames; g_zebra=zebra;
    std::cout<<"PASS: LTR/RTL Details/List/grid selection pixels survive native-menu popup focus without granting list input; dismissal, unrelated popups/focus, modal/background frames and folder generations clear feedback; focus repaints coalesce and inactive compositor pixels clear without retiming\n";
}
