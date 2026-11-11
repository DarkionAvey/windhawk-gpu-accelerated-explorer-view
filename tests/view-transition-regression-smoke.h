// Component geometry checks use independently observed settled endpoints and
// DPI arithmetic. Native integration below uses real Shell COM notifications;
// it never fabricates input, WM_COMMAND, or automation callbacks.
namespace ViewTransitionRegression {
inline void Near(float actual,float expected,const char* reason) {
    if(!std::isfinite(actual) || !std::isfinite(expected) || std::fabs(actual-expected)>.02f) {
        std::cout<<"GEOMETRY actual="<<actual<<" expected="<<expected<<std::endl;
        Check(false,reason);
    }
}
inline void Rect(D2D1_RECT_F actual,D2D1_RECT_F expected,const char* reason) {
    Near(actual.left,expected.left,reason); Near(actual.top,expected.top,reason);
    Near(actual.right,expected.right,reason); Near(actual.bottom,expected.bottom,reason);
}
inline D2D1_RECT_F Between(D2D1_RECT_F a,D2D1_RECT_F b,float p) {
    return {a.left*(1-p)+b.left*p,a.top*(1-p)+b.top*p,
        a.right*(1-p)+b.right*p,a.bottom*(1-p)+b.bottom*p};
}
inline void TextAnchorPath(const ItemGeometry& item,const ItemGeometry& before,const ItemGeometry& after,float p) {
    const float x0=before.wrapped ? .5f : 0.f, x1=after.wrapped ? .5f : 0.f;
    const float y0=before.wrapped ? 0.f : .5f, y1=after.wrapped ? 0.f : .5f;
    const auto anchor=[](D2D1_RECT_F r,float x,float y) {
        return D2D1::Point2F(r.left+(r.right-r.left)*x,r.top+(r.bottom-r.top)*y);
    };
    const auto a=anchor(before.text,x0,y0), b=anchor(after.text,x1,y1);
    const auto actual=anchor(item.text,item.textCenter,item.textVertical);
    Near(item.textCenter,x0*(1-p)+x1*p,"text centering jumped to destination alignment");
    Near(item.textVertical,y0*(1-p)+y1*p,"text paragraph alignment jumped to destination");
    Near(actual.x,a.x*(1-p)+b.x*p,"text anchor wandered horizontally away from its endpoint path");
    Near(actual.y,a.y*(1-p)+b.y*p,"text anchor wandered vertically away from its endpoint path");
    Near(item.text.right-item.text.left,(before.text.right-before.text.left)*(1-p)+
        (after.text.right-after.text.left)*p,"text box width did not interpolate");
    Near(item.text.bottom-item.text.top,(before.text.bottom-before.text.top)*(1-p)+
        (after.text.bottom-after.text.top)*p,"text box height did not interpolate");
}
inline void Fonts() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"transition fonts");
}
inline void AdvanceClock(LARGE_INTEGER origin,double fraction) {
    LARGE_INTEGER frequency{}; ::QueryPerformanceFrequency(&frequency);
    AnimationClockFixture::instant.QuadPart=origin.QuadPart+
        LONGLONG(frequency.QuadPart*double(kUiTransitionDurationMs)*fraction/1000.);
}
inline LRESULT CALLBACK Receive(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==kMsgBindShellViewEvents || msg==kMsgShellViewModeEvent || msg==kMsgShellVisibilityChanged ||
        msg==kMsgShellSelectionEvent || msg==kMsgNativeSelectionChanged)
        return OverlayProc(hwnd,msg,wp,lp);
    return DefSubclassProc(hwnd,msg,wp,lp);
}
}

static void TestViewMorphGeometry(UINT dpi) {
    using namespace ViewTransitionRegression;
    StyleFixture::Session style; StyleFixture::dpi=dpi; Fonts();
    for(bool rtl : {false,true}) for(const auto pair : {
            std::pair{FVM_DETAILS,FVM_ICON},std::pair{FVM_ICON,FVM_DETAILS},
            std::pair{FVM_TILE,FVM_CONTENT},std::pair{FVM_CONTENT,FVM_TILE},
            std::pair{FVM_TILE,FVM_DETAILS},std::pair{FVM_DETAILS,FVM_TILE}}) {
        Regression::Scope scope("morph dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl)+
            " from="+std::to_string(pair.first)+" to="+std::to_string(pair.second));
        PresentationFixture::Tab tab(L"snapshot endpoints",48,rtl,false,false,1800,1100);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        AnimationClockFixture::Freeze clock;
        ApplyViewLayout(state,{UINT(pair.first),96},false);
        std::array<ItemGeometry,6> before;
        for(int row=0;row<6;++row) before[size_t(row)]=LayoutItem(state,LayoutRowVisual(state,row,1));
        ApplyViewLayout(state,{UINT(pair.second),96},true,true);
        FinishColumnTransition(state);
        Check(state.rowTransitionActive && state.reflowLayoutTransition,"view switch lost its shaped snapshot");
        std::array<ItemGeometry,6> destination;
        for(int row=0;row<6;++row) destination[size_t(row)]=LayoutItem(state,LayoutRowVisual(state,row,1));
        for(float p : {0.f,.125f,.25f,.5f,.75f,1.f}) for(int row=0;row<6;++row) {
            const auto item=LayoutItem(state,LayoutRowVisual(state,row,p));
            Rect(item.icon,Between(before[size_t(row)].icon,destination[size_t(row)].icon,p),
                "icon departed from the direct path between settled endpoints");
            TextAnchorPath(item,before[size_t(row)],destination[size_t(row)],p);
        }
        // Reverse while mid-flight: the next snapshot starts at the current
        // pose, including component geometry, not either settled endpoint.
        state.rowTransitionStartedQpc=AnimationClockFixture::instant;
        AdvanceClock(state.rowTransitionStartedQpc,.4);
        const float p=1.f-std::pow(.6f,3.f);
        const auto interrupted=LayoutItem(state,LayoutRowVisual(state,0,p));
        ApplyViewLayout(state,{UINT(pair.first),96},true,true);
        const auto resumed=LayoutItem(state,LayoutRowVisual(state,0,0));
        Rect(resumed.icon,interrupted.icon,"reverse view command discarded the presented icon");
        Rect(resumed.text,interrupted.text,"reverse view command discarded the presented filename");
        // A worker snapshot taken during the morph must carry its shape for
        // later identity matching, not just the moving row's rectangle.
        DirectoryJob snapshot; CaptureItemSnapshot(state,snapshot);
        Check(!snapshot.oldViewport.empty() && snapshot.oldViewport.front().shaped,
            "directory snapshot discarded an interrupted component morph");
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: bidirectional icon/name paths, wide list to grid, multi-column identities and reversal snapshots\n";
}

// No device, pixel readback, queued input or real-time deadline is involved.
// DirectWrite supplies frozen local ink metrics; independent endpoint arithmetic
// specifies where those glyphs belong while their containing box changes.
static void TestViewMorphTextAnchors() {
    using namespace ViewTransitionRegression;
    WorkerComApartment apartment;
    StyleFixture::Session style; Fonts();
    AnimationClockFixture::Freeze clock;
    for(UINT dpi:{96u,144u,192u}) for(bool rtl:{false,true})
    for(const auto pair:{std::pair{FVM_LIST,FVM_ICON},std::pair{FVM_ICON,FVM_LIST},
                        std::pair{FVM_DETAILS,FVM_ICON},std::pair{FVM_ICON,FVM_CONTENT}}) {
        Regression::Scope scope("text anchors dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl)+
            " from="+std::to_string(pair.first)+" to="+std::to_string(pair.second));
        StyleFixture::dpi=dpi;
        PresentationFixture::Tab tab(L"text anchor geometry",48,rtl,false,false,1800,1100);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        ApplyViewLayout(state,{UINT(pair.first),96},false);
        const auto before=LayoutItem(state,LayoutRowVisual(state,0,1));
        ApplyViewLayout(state,{UINT(pair.second),96},true,true); FinishColumnTransition(state);
        const auto after=LayoutItem(state,LayoutRowVisual(state,0,1));
        // Shape only once, independently of animation progress, just like a
        // retained mask. Include both leading/list and centered/grid masks.
        for(bool wrapped:{false,true}) {
            ComPtr<IDWriteTextFormat> format;
            Check(SUCCEEDED(g_writeFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,18.f*dpi/96.f,L"",&format)),"frozen anchor font");
            format->SetReadingDirection(rtl ? DWRITE_READING_DIRECTION_RIGHT_TO_LEFT:DWRITE_READING_DIRECTION_LEFT_TO_RIGHT);
            format->SetTextAlignment(wrapped ? DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);
            format->SetParagraphAlignment(wrapped ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR:DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            TextLayoutCache::Layout frozen;
            constexpr float maskWidth=1400.f,maskHeight=160.f;
            Check(SUCCEEDED(g_writeFactory->CreateTextLayout(L"Applications",12,format.Get(),maskWidth,maskHeight,&frozen.text)),
                "independent frozen text layout");
            DWRITE_TEXT_METRICS ink{}; Check(SUCCEEDED(frozen.text->GetMetrics(&ink)),"independent frozen ink metrics");
            for(float p:{0.f,.125f,.25f,.5f,.75f,.875f,1.f}) {
                const auto item=LayoutItem(state,LayoutRowVisual(state,0,p));
                TextAnchorPath(item,before,after,p);
                const float width=item.text.right-item.text.left,height=item.text.bottom-item.text.top;
                for(float scale:{.75f,1.f,1.5f}) {
                    const auto offset=TextAlignmentOffset(frozen,width,height,item.textCenter,item.textVertical,scale);
                    const float nativeX=wrapped ? .5f : rtl ? 1.f : 0.f, nativeY=wrapped ? 0.f : .5f;
                    const float desiredX=rtl ? 1.f-item.textCenter:item.textCenter;
                    const float physicalLeft=rtl ? 1800.f-item.text.right:item.text.left;
                    const float actualX=physicalLeft+(width-maskWidth*scale)*nativeX+ink.left*scale+offset.x;
                    const float actualY=item.text.top+(height-maskHeight*scale)*nativeY+ink.top*scale+offset.y;
                    Near(actualX,physicalLeft+width*desiredX-ink.width*scale*desiredX,
                        "frozen text mask moved independently of its horizontal anchor");
                    Near(actualY,item.text.top+height*item.textVertical-ink.height*scale*item.textVertical,
                        "frozen text mask moved independently of its vertical anchor");
                }
            }
            Check(frozen.text->GetMaxWidth()==maskWidth && frozen.text->GetMaxHeight()==maskHeight,
                "moving frozen text reshaped its layout bounds");
        }
        state.rowTransitionStartedQpc=AnimationClockFixture::instant;
        AdvanceClock(state.rowTransitionStartedQpc,.5);
        const auto interrupted=LayoutItem(state,LayoutRowVisual(state,0,.875f));
        ApplyViewLayout(state,{UINT(pair.first),96},true,true);
        const auto resumed=LayoutItem(state,LayoutRowVisual(state,0,0));
        Rect(resumed.text,interrupted.text,"retarget discarded the displayed text box");
        Near(resumed.textCenter,interrupted.textCenter,"retarget discarded partial horizontal alignment");
        Near(resumed.textVertical,interrupted.textVertical,"retarget discarded partial paragraph alignment");
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: deterministic text anchors, frozen local ink metrics, RTL, DPI and interrupted view morphs\n";
}

static void TestFractionalTouchZoom(UINT dpi) {
    using namespace ViewTransitionRegression;
    StyleFixture::Session style; StyleFixture::dpi=dpi; Fonts();
    for(bool rtl : {false,true}) for(UINT mode : {UINT(FVM_CONTENT),UINT(FVM_TILE)})
        for(bool drive : {false,true}) {
        if(drive && mode!=FVM_TILE) continue;
        Regression::Scope scope("fractional pinch dpi="+std::to_string(dpi)+" mode="+std::to_string(mode)+
            " rtl="+std::to_string(rtl)+" drive="+std::to_string(drive));
        PresentationFixture::Tab tab(L"fractional touch",12,rtl,false,false,1000,700);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        state.thisPcGpu=drive;
        ApplyViewLayout(state,{mode,48},false);
        POINT contact{400,240};
        float previous=0;
        for(int sample=0;sample<20;++sample) {
            const double preference=32.11+sample*.027;
            ApplyRowHeight(state,preference,&contact);
            const float font=std::max(float(MulDiv(7,int(dpi),96)),
                float(preference*dpi/96.)*(mode==FVM_TILE ? g_gridFontPercent.load() : g_fontPercent.load())/100.f);
            const float line=font*1.5f,gap=float(MulDiv(2,int(dpi),96));
            const float expected=mode==FVM_CONTENT ? line*2+gap : drive ?
                line*2+MulDiv(kDriveBarHeight96,int(dpi),96)+gap*2 : line*3+gap*2;
            Near(state.iconPx,expected,"text-sized icon quantized fractional touch scale");
            const auto item=LayoutItem(state,LayoutRowVisual(state,0,1));
            Near(item.text.bottom-item.text.top,line,"layout line pitch disagrees with fractional metrics");
            Check(!state.zoomAnimating && (sample==0 || state.iconPx>previous),
                "direct touch waited for an integer boundary or started a timed zoom");
            previous=state.iconPx;
        }
        AnimationClockFixture::Freeze clock;
        const double from=32.11+19*.027;
        ApplyRowHeight(state,40.);
        Near(float(state.zoomFromHeight96),float(from),"wheel takeover discarded the fractional starting preference");
        const auto origin=state.zoomStartedQpc;
        AdvanceClock(origin,.5);
        state.touchpadPinching=false;
        ApplyManipulationPinch(state,0,&contact);
        const double expected=from+(40.-from)*.875;
        Near(float(state.touchpadPinchHeight96),float(expected),"touch takeover inverted layout geometry instead of tracking scale");
        ApplyManipulationPinch(state,float(std::log(1.001)),&contact);
        Near(float(state.preciseRowHeight96[state.gridView]),float(expected*1.001),
            "first direct sample snapped to the previous animated endpoint");
        Check(!state.zoomAnimating,"direct touch failed to take over the animated zoom");
        // Trackpad keeps its existing animated-target policy.
        state.touchpadPinching=false;
        ApplyManipulationPinch(state,float(std::log(1.04)));
        Check(state.zoomAnimating && state.preciseRowHeight96[state.gridView]==0.,
            "trackpad lost its animated target policy");
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: fractional Content/Tiles/drive icons and wheel/trackpad-to-touch preference handoff\n";
}

static void TestStickyGroupCollapseAnchor() {
    using namespace ViewTransitionRegression;
    Fonts();
    for(bool rtl : {false,true}) for(UINT mode : {UINT(FVM_DETAILS),UINT(FVM_CONTENT),UINT(FVM_TILE)})
        for(bool nextCollapsed : {false,true}) {
        Regression::Scope scope("collapse mode="+std::to_string(mode)+" rtl="+std::to_string(rtl)+
            " nextCollapsed="+std::to_string(nextCollapsed));
        PresentationFixture::Tab tab(L"sticky collapse",480,rtl,false,false,1000,450);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        AnimationClockFixture::Freeze clock;
        for(int i=0;i<state.total;++i) {
            state.entries[size_t(i)].groupKey=std::to_wstring(i/60);
            state.entries[size_t(i)].groupTitle=L"Bucket "+std::to_wstring(i/60);
        }
        state.grouping.enabled=true;
        ApplyViewLayout(state,{mode,48},false); RebuildGroupSections(state);
        const auto settle=[&] {
            LARGE_INTEGER now=AnimationClockFixture::instant;
            AdvanceClock(now,1.1); AdvanceAnimationFrame(state,false);
        };
        if(nextCollapsed) { ToggleGroupSection(state,3); settle(); }
        const float expected=state.sections[2].documentTop;
        state.scrollNow=state.scrollTarget=expected+
            (state.sections[2].documentEnd-expected)*.6f;
        Check(GroupAtDocument(state,state.scrollNow)==2 &&
            LayoutGroupHeader(state,2,1).top==state.headerPx,"collapse fixture did not reach sticky C");
        const auto selected=state.selectedFlags;
        PressGroupHeader(state,2);
        Near(state.scrollTarget,expected,"collapse retained its old depth or targeted the next section");
        settle();
        Near(state.scrollNow,expected,"collapsed group's own heading did not remain at the top");
        Check(state.sections[2].collapsed && state.sections[3].collapsed==nextCollapsed &&
            state.selectedFlags==selected,"collapse changed a neighboring group or selection");
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: sticky collapse retains its own heading with expanded/collapsed neighbors in list/Content/Tiles and RTL\n";
}

static void TestViewMorphDirectoryDiff(const std::wstring& fixture) {
    using namespace ViewTransitionRegression;
    const auto root=std::filesystem::path(fixture)/L"morph-directory-diff";
    std::filesystem::create_directory(root);
    for(int i=0;i<12;++i) std::ofstream(root/(L"item-"+std::to_wstring(i)+L".txt"))<<i;
    OptionEffects::Canvas canvas(root,12,96,false);
    auto& state=canvas.state;
    UpdateRefreshRate(state); // The canvas paints offscreen; normal startup owns this initialization.
    Check(state.qpcFrequency>0.,"diff fixture animation clock was not initialized");
    AnimationClockFixture::Freeze clock;
    ApplyViewLayout(state,{FVM_DETAILS,16},false);
    Check(SUCCEEDED(state.folderView->SetViewModeAndIconSize(FVM_ICON,96)),"native mode before deletion");
    FolderViewMode requested;
    Check(ReadNativeViewMode(state,requested),"native mode observation before deletion");
    ApplyViewLayout(state,requested,true,true);
    FinishColumnTransition(state);
    Check(state.rowTransitionActive,"diff fixture did not start a view morph");
    state.rowTransitionStartedQpc=AnimationClockFixture::instant;
    AdvanceClock(state.rowTransitionStartedQpc,.4);
    const auto path=state.entries[5].path;
    const float p=1.f-std::pow(.6f,3.f);
    const auto before=LayoutItem(state,LayoutRowVisual(state,5,p));
    DirectoryJob captured; CaptureItemSnapshot(state,captured);
    Check(!captured.oldViewport.empty() && captured.oldViewport.front().shaped,
        "deletion request failed to capture the current morph");
    const auto removed=state.entries.front().path;
    Check(DeleteFileW(removed.c_str()),"delete private file during view morph");
    QueueDirectoryRefresh(state,true);
    Check(Eventually([&] {ReceiveDirectoryResults(state);
        return !state.directoryPending && state.total==11;}),"real deletion did not reach the morphing folder");
    const int row=GpuRowForNativePath(state,path);
    if(row<0 || !state.rowTransitionActive || !state.reflowLayoutTransition)
        std::cout<<"DIFF row="<<row<<" active="<<state.rowTransitionActive<<" reflow="<<state.reflowLayoutTransition
            <<" grid="<<state.gridView<<" mode="<<state.layoutNativeMode<<" startup="<<state.startupAnimationsSuppressed
            <<" zoom="<<state.zoomAnimating<<" captures="<<captured.oldViewport.size()<<" scroll="<<state.scrollNow
            <<" oldScroll="<<captured.oldScroll<<" rowPx="<<state.rowPx<<" oldRow="<<captured.oldRowPx
            <<" instant="<<bool(state.instantInteractionSort)<<" loaded="<<state.loaded<<std::endl;
    Check(row>=0 && state.rowTransitionActive && state.reflowLayoutTransition,
        "directory diff discarded the surviving item's component transition");
    const auto after=LayoutItem(state,LayoutRowVisual(state,row,0));
    Rect(after.icon,before.icon,"directory diff rebound the icon to a different item or settled position");
    Rect(after.text,before.text,"directory diff discarded the travelling filename position");
    Check(state.disappearingRows.size()==1,"deletion lost the outgoing morph ghost");
    for(const auto& motion:state.rowMotion)
        Check(motion.newIndex>=0 && motion.newIndex<state.total,"deleted index became a destination row");
    std::cout<<"PASS: real private-file deletion preserves surviving icon/name geometry during a view morph\n";
}

// Intentional read-only This PC integration; all filesystem navigation goes
// to process-private folders. No user's Explorer instance is accessed.
static void TestViewEventRoundTrip(const std::wstring& fixture,bool thisPc) {
    using namespace ViewTransitionRegression;
    Fonts();
    const auto root=std::filesystem::path(fixture)/L"view-event-roundtrip";
    std::filesystem::create_directories(root/L"other");
    std::ofstream(root/L"first.txt")<<"one";
    std::ofstream(root/L"other"/L"second.txt")<<"two";
    ProviderSelectionFixture::Browser host(true);
    auto& state=host.state; FocusFixture::Session focus(host.frame,state.hwnd);
    // Only real queued production continuations are forwarded. No test code
    // constructs input messages or invokes the event sink.
    Check(SetWindowSubclass(state.hwnd,Receive,714,0),"view event receiver");
    struct Cleanup { ViewState& state; ~Cleanup() {
        DetachShellSelectionEvents(state); UnbindNativeViewInput(state.nativeHwnd,state.hwnd);
        RemoveWindowSubclass(state.hwnd,ViewTransitionRegression::Receive,714); DropTarget(state);
    }} cleanup{state};
    host.Navigate(root.wstring(),2);
    Check(EnsureTarget(state),"event roundtrip GPU target");
    Check(AttachShellSelectionEvents(state,nullptr) && state.selectionEventCookie,
        "initial native-view subscription");
    PIDLIST_ABSOLUTE raw=nullptr;
    Check(SUCCEEDED(thisPc ? SHGetKnownFolderIDList(FOLDERID_ComputerFolder,0,nullptr,&raw) :
        SHParseDisplayName(root.c_str(),nullptr,&raw,0,nullptr)),"event roundtrip origin identity");
    OwnedPidl origin(raw);
    OwnedPidl other; raw=nullptr;
    Check(SUCCEEDED(SHParseDisplayName((root/L"other").c_str(),nullptr,&raw,0,nullptr)),"event roundtrip other identity");
    other.reset(raw);
    for(int visit=0;visit<3;++visit) {
        Regression::Scope scope("view events thisPc="+std::to_string(thisPc)+" visit="+std::to_string(visit));
        const HWND oldNative=state.nativeHwnd;
        host.sink->navigationComplete=false;
        Check(SUCCEEDED(host.browser->BrowseToIDList(visit==1 ? other.get() : origin.get(),SBSP_ABSOLUTE)),
            "real roundtrip navigation");
        Check(Eventually([&] {host.Pump(); return host.sink->navigationComplete && host.sink->view;}),
            "roundtrip Shell navigation did not complete");
        auto view=host.sink->view;
        ComPtr<IShellView> native; Check(SUCCEEDED(view.As(&native)),"roundtrip native Shell view");
        HWND hwnd=nullptr; Check(SUCCEEDED(native->GetWindow(&hwnd)),"roundtrip native HWND");
        ComPtr<IShellFolder> folder; std::wstring path;
        Check(GetFolderPath(view.Get(),path,folder),"roundtrip native folder identity");
        auto browser=state.shellBrowser;
        state.nativeHwnd=oldNative; // OnViewCreated observed the new HWND before attachment.
        AttachView(hwnd,view,folder,browser,path);
        Check(Eventually([&] {host.Pump(); ReceiveDirectoryResults(state);
            return state.selectionEventCookie && state.selectionEventNativeView==hwnd &&
                state.selectionEventEpoch==state.token;}),"confirmed navigation did not rebind native event source");
        // Deliberately leave the HWND input observer unbound. Mode and size
        // changes must work from real COM callbacks alone, as XAML does.
        UnbindNativeViewInput(state.nativeHwnd,state.hwnd);
        for(const auto step : {FolderViewMode{FVM_ICON,96},FolderViewMode{FVM_ICON,48},FolderViewMode{FVM_DETAILS,16}}) {
            state.directoryModePinned=true;
            const unsigned focusCalls=FocusFixture::sets;
            Check(SUCCEEDED(view->SetViewModeAndIconSize(FOLDERVIEWMODE(step.mode),step.iconSize)),"COM-only mode request");
            Check(Eventually([&] {host.Pump(); ReceiveDirectoryResults(state);
                return state.layoutNativeMode==step.mode && (step.mode!=FVM_ICON || state.layoutIconSize==step.iconSize) &&
                    !state.nativeModeSignalQueued && !state.nativeViewCommandPending;}),"COM mode change missed after roundtrip");
            Check(FocusFixture::sets==focusCalls,"view event changed keyboard focus");
            AdvanceRowHeight(state,true); FinishColumnTransition(state); StopRowTransition(state);
        }
        // Same native view/source, new folder epoch: an old sink must not be
        // reused with the epoch permanently rejected by all event receivers.
        auto oldSink=state.selectionEventSink;
        ++state.token;
        Check(AttachShellSelectionEvents(state,nullptr) && state.selectionEventEpoch==state.token &&
            state.selectionEventSink.Get()!=oldSink.Get(),"same-view subscription retained a stale folder generation");
        Check(SUCCEEDED(view->SetViewModeAndIconSize(FVM_TILE,48)),"new generation mode request");
        Check(Eventually([&] {host.Pump(); ReceiveDirectoryResults(state); return state.layoutNativeMode==FVM_TILE;}),
            "new generation rejected its own native mode event");
        AdvanceRowHeight(state,true); FinishColumnTransition(state); StopRowTransition(state);
    }
    std::cout<<"PASS: real COM-only view and icon-size events across navigation and same-source generation changes\n";
}
