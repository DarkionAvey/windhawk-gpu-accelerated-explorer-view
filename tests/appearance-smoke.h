static void TestAppearanceRefinements(HWND frame, const std::wstring& fixture) {
    ViewState state;
    state.hwnd=CreateWindowExW(0,L"STATIC",L"appearance",WS_CHILD,0,0,640,480,frame,
        nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.hwnd!=nullptr,"appearance fixture window");
    state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
    state.visible=state.inScope=state.detailsViewActive=state.loaded=state.columnsView=true;
    state.fontPx=16; state.rowPx=34; state.headerPx=38;
    UpdateRefreshRate(state); Check(EnsureTarget(state),"appearance GPU target");
    ComPtr<ID2D1Image> original; state.target->GetTarget(&original);
    ComPtr<ID2D1Bitmap1> surface,cpu;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(360,160),nullptr,0,&props,&surface)),"appearance surface");
    props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(360,160),nullptr,0,&props,&cpu)),"appearance readback");
    state.target->SetTarget(surface.Get()); state.paintWidth=360;
    auto render=[&](auto draw) {
        state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f)); draw();
        Check(SUCCEEDED(state.target->EndDraw()) && SUCCEEDED(cpu->CopyFromBitmap(nullptr,surface.Get(),nullptr)),"appearance draw");
        D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"appearance map");
        std::vector<BYTE> pixels(360*160*4);
        for(size_t y=0;y<160;++y) memcpy(pixels.data()+y*360*4,map.bits+y*map.pitch,360*4);
        cpu->Unmap(); return pixels;
    };
    auto alpha=[](const auto& pixels) { size_t sum=0; for(size_t i=3;i<pixels.size();i+=4) sum+=pixels[i]; return sum; };
    for(auto axis : {ScrollAxis::Vertical,ScrollAxis::Horizontal}) {
        const bool horizontal=axis==ScrollAxis::Horizontal;
        const auto bar=horizontal ? MakeScrollbarGeometry(axis,10,280,100,116,2000,200,500,24,1) :
            MakeScrollbarGeometry(axis,300,316,10,140,2000,200,500,24,1);
        Check(bar.usable,"appearance scrollbar fixture bounds");
        auto draw=[&](bool hot,bool pressed) { return render([&] { DrawScrollbar(state,bar,0xFFFFFF,hot,pressed); }); };
        auto& motion=state.scrollbarMotion[horizontal ? 1 : 0];
        auto elapsed=[&](double ms) { QueryPerformanceCounter(&motion.started); motion.started.QuadPart-=LONGLONG(state.qpcFrequency*ms/1000.); };
        for(bool end:{false,true}) {
            auto cap=bar;
            cap.thumbStart=end ? (horizontal ? bar.right:bar.bottom)-bar.thumbLength :
                (horizontal ? bar.left:bar.top);
            const auto pixels=render([&] {DrawScrollbar(state,cap,0xFFFFFF,false,false);});
            const auto at=[&](float along) {
                const int x=int(horizontal ? along:(bar.left+bar.right)*.5f);
                const int y=int(horizontal ? (bar.top+bar.bottom)*.5f:along);
                return pixels[(size_t(y)*360+x)*4+3];
            };
            Check(at(cap.thumbStart+1)==0 && at(cap.thumbStart+cap.thumbLength-2)==0 &&
                at(cap.thumbStart+cap.thumbLength*.5f)>0,
                "scrollbar thumb caps lack transparent end padding");
        }
        const auto rest=draw(false,false);
        Check(draw(true,false)==rest && motion.started.QuadPart && NeedsAnimationPaint(state),
            "scrollbar hover snapped instead of starting a frame-paced transition");
        elapsed(20); const auto middle=draw(true,false);
        elapsed(90); const auto hot=draw(true,false);
        Check(alpha(rest)<alpha(middle) && alpha(middle)<alpha(hot) && !motion.started.QuadPart,
            "scrollbar hover had no intermediate pixels or failed to settle at 80 ms");
        Check(draw(true,true)==hot,"scrollbar press snapped opacity");
        elapsed(90); const auto pressed=draw(true,true);
        Check(alpha(pressed)>alpha(hot) && draw(true,false)==pressed,"scrollbar press/release did not interpolate");
        elapsed(90); Check(draw(true,false)==hot,"scrollbar release did not return to hover");
        draw(false,false); elapsed(90); Check(draw(false,false)==rest,"scrollbar leave did not restore idle pixels");
        draw(true,false); elapsed(20); draw(true,false); draw(false,false);
        Check(motion.activeFrom>0 && motion.activeFrom<1,"scrollbar reversal lost elapsed visual progress");
        elapsed(90); draw(false,false);
        Check(!state.scrollbarMotion[horizontal ? 0 : 1].started.QuadPart,"one scrollbar animated the other axis");
        draw(true,false); auto gone=bar; gone.usable=false;
        render([&] { DrawScrollbar(state,gone,0xFFFFFF,false,false); });
        Check(!motion.started.QuadPart,"removed scrollbar stranded an animation");
        draw(true,false); ResetMouseGestureForNavigation(state);
        Check(!motion.started.QuadPart,"navigation retained scrollbar animation work");
    }
    for(unsigned color : {0xFFFFFFu,0x202020u}) {
        const auto muted=render([&] { Text(state,L"Date Modified",10,10,300,60,color,true); });
        const auto sorted=render([&] { Text(state,L"Date Modified",10,10,300,60,color,true,1,true); });
        Check(alpha(muted)>0 && alpha(muted)<alpha(sorted),"unsorted header was not muted with the supplied theme color");
    }
    for(const auto& key : {PKEY_FileName,PKEY_DateModified}) {
        ExplorerColumn c; c.key=key; c.title=L"Column"; c.width=220; state.columns.push_back(c);
    }
    UpdateColumns(state);
    auto textBounds=[&](const ExplorerColumn& column,bool header) {
        return ColumnTextBounds(state,size_t(&column-state.columns.data()),header,ListStartX(state),ListEndX(state),DpiOf(state.hwnd));
    };
    const auto caption=textBounds(state.columns[1],true);
    Check(caption.left-ListStartX(state)-state.columns[1].start==Scale96(16,DpiOf(state.hwnd)) &&
        state.columns[1].end+ListStartX(state)-caption.right==Scale96(16,DpiOf(state.hwnd)),"header padding disagrees with caption geometry");
    Check(textBounds(state.columns[0],true).left==NameTextOrigin(state),"Name header lost filename alignment");
    for(bool rtl : {false,true}) for(float scroll : {0.f,53.f}) {
        state.rtlLayout=rtl; state.horizontalScroll=scroll;
        for(const auto& column : state.columns) {
            state.sort.explicitOrder=true; state.sort.key=column.key;
            const auto header=textBounds(column,true), body=textBounds(column,false);
            Check(header.left==body.left,"column caption and body have different X origins");
            Check(header.right<=ListEndX(state) && body.right<=ListEndX(state),"column text escaped the viewport");
        }
    }
    state.horizontalScroll=0; state.sort.explicitOrder=false;
    const bool dot=g_dimDotFiles.exchange(true);
    Entry e; e.name=L"file.txt"; e.path=L"appearance:file";
    Check(ItemContentOpacity(state,e)==1,"ordinary item was dimmed");
    state.clipboardPaths.insert(e.path);
    Check(ItemContentOpacity(state,e)==1,"Copy dimmed an ordinary item");
    state.clipboardIsCut=true;
    const float dim=ItemContentOpacity(state,e);
    Check(dim==kDimmedItemContentOpacity,"Cut alpha changed");
    e.attributes=FILE_ATTRIBUTE_HIDDEN;
    Check(ItemContentOpacity(state,e)==dim,"hidden plus Cut compounded alpha");
    state.clipboardPaths.clear();
    state.clipboardIsCut=false;
    Check(ItemContentOpacity(state,e)==dim,"hidden attribute was not dimmed");
    e.name=L".folder"; e.attributes=0;
    Check(ItemContentOpacity(state,e)==dim,"dotfile/dotfolder option did not dim");
    g_dimDotFiles=false;
    Check(ItemContentOpacity(state,e)==1,"dotfile option could not be disabled");
    e.attributes=FILE_ATTRIBUTE_HIDDEN;
    Check(ItemContentOpacity(state,e)==dim,"dotfile option disabled Windows hidden dimming");
    Entry normal=e; normal.attributes=0;
    Check(SameEntryMetadata(normal,e) && DirectoryMetadataHash({normal})!=DirectoryMetadataHash({e}),
        "hidden attribute changes either refetched unchanged cells or failed to update presentation");
    g_dimDotFiles=dot;
    state.target->SetTarget(original.Get());
    // Exercise both bars in the real Paint path. Isolated DrawScrollbar tests
    // cannot detect an absent horizontal bar resetting the vertical animation.
    state.total=120; state.entries.resize(state.total); state.selectedFlags.resize(state.total);
    for(auto& item : state.entries) { item.name=L"item"; item.detailsReady=true; item.propertyEpoch=state.propertyEpoch; }
    state.firstContentSubmittedAt=GetTickCount64(); state.featureInitializationStep=5;
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    const auto proc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(state.hwnd,GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(SampledPointerProc)));
    const bool scrollbar=g_scrollbar.exchange(true);
    PointerFixture::Session pointer;
    auto paint=[&] { auto swap=std::move(state.swapChain); Paint(state); state.swapChain=std::move(swap); };
    auto age=[&](ScrollbarMotion& motion) {
        motion.started.QuadPart-=LONGLONG(state.qpcFrequency*.090);
    };
    for(bool rtl : {false,true}) for(int mode : {0,1,2}) {
        state.rtlLayout=rtl; state.columnsView=mode!=1;
        state.columns[0].width=mode==2 ? 1000 : 220; UpdateColumns(state);
        SetScrollImmediate(state,0); ResetMouseGestureForNavigation(state); paint();
        const auto absent=HorizontalBar(state);
        Check(absent.axis==ScrollAxis::Horizontal && absent.usable==(mode==2),"horizontal geometry lost its axis or overflow state");
        for(auto axis : {ScrollAxis::Vertical,ScrollAxis::Horizontal}) {
            const bool horizontal=axis==ScrollAxis::Horizontal;
            const auto bar=horizontal ? HorizontalBar(state) : Scrollbar(state);
            if(!bar.usable) continue;
            POINT down{LONG(horizontal ? bar.thumbStart+bar.thumbLength*.5f : (bar.left+bar.right)*.5f),
                LONG(horizontal ? (bar.top+bar.bottom)*.5f : bar.thumbStart+bar.thumbLength*.5f)};
            down.x=LogicalX(state,down.x); PointerFixture::At(state.hwnd,down);
            OverlayProc(state.hwnd,WM_MOUSEMOVE,0,MAKELPARAM(down.x,down.y)); paint();
            auto& motion=state.scrollbarMotion[horizontal ? 1 : 0];
            Check(motion.activeTo==1 && motion.started.QuadPart,"Paint lost the hovered scrollbar animation");
            age(motion); paint();
            Check(motion.activeTo==1 && !motion.started.QuadPart,"hover never settled through full Paint");
            PointerFixture::held=true;
            OverlayProc(state.hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(down.x,down.y)); paint();
            Check(GetCapture()==state.hwnd && motion.pressedTo==1 && motion.started.QuadPart,"thumb press lost capture or its appearance transition");
            age(motion); paint();
            Check(motion.pressedTo==1 && !motion.started.QuadPart,"press never settled through full Paint");
            POINT moved=down; if(horizontal) moved.x+=rtl ? -70 : 70; else moved.y+=70;
            PointerFixture::At(state.hwnd,moved);
            OverlayProc(state.hwnd,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(moved.x,moved.y));
            Check((horizontal ? state.horizontalTarget : state.scrollTarget)>0,"thumb drag did not move the scroll destination");
            PointerFixture::held=false;
            OverlayProc(state.hwnd,WM_LBUTTONUP,0,MAKELPARAM(moved.x,moved.y)); paint();
            Check(GetCapture()!=state.hwnd && motion.pressedTo==0,"thumb release retained capture or pressed appearance");
            OverlayProc(state.hwnd,WM_MOUSELEAVE,0,0); paint(); age(motion); paint();
            Check(motion.activeTo==0 && !motion.started.QuadPart,"mouse leave did not settle scrollbar appearance");
        }
    }
    g_scrollbar=scrollbar;
    SetWindowLongPtrW(state.hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(proc));
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
    UnregisterAnimationWindow(state.hwnd); DropTarget(state); DestroyWindow(state.hwnd);
    // The actual background enumerator must carry the hidden flag into rows.
    const auto file=JoinPath(fixture,L"appearance-hidden.txt");
    HANDLE handle=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
    Check(handle!=INVALID_HANDLE_VALUE,"hidden fixture file"); CloseHandle(handle);
    ViewState names; names.hwnd=MessageWindow(); names.background->hwnd=names.hwnd;
    names.folderPath=fixture; names.visibility={true,true};
    QueueDirectoryRefresh(names); const auto rows=WaitForComplete(*names.background,names.directoryToken);
    const auto item=std::find_if(rows.entries.begin(),rows.entries.end(),[&](const auto& entry) { return entry.path==file; });
    Check(item!=rows.entries.end() && (item->attributes&FILE_ATTRIBUTE_HIDDEN),"background enumeration lost hidden attributes");
    NativeRowIdentity identity; identity.namespaceItem=true;
    ITEMIDLIST* pidl=nullptr; Check(SUCCEEDED(SHParseDisplayName(file.c_str(),nullptr,&pidl,0,nullptr)),"hidden Shell PIDL");
    identity.absolute.reset(pidl); Entry shell;
    Check(SUCCEEDED(ResolveNativeRowIdentity(identity,shell)) && (shell.attributes&FILE_ATTRIBUTE_HIDDEN),
        "namespace enumeration lost hidden attributes");
    Stop(names.background); DestroyWindow(names.hwnd);
    SetFileAttributesW(file.c_str(),FILE_ATTRIBUTE_NORMAL); Check(DeleteFileW(file.c_str()),"hidden fixture cleanup");
    std::cout<<"PASS: 80 ms scrollbar pixels and full-paint LTR/RTL hover/press/drag/release with and without horizontal overflow; aligned themed headers; shared hidden/dotfile/Cut dimming\n";
}

static void TestItemOpacityTransitions(HWND frame, const std::wstring& fixture) {
    ViewState state;
    state.hwnd=CreateWindowExW(0,L"STATIC",L"opacity",WS_CHILD,0,0,640,480,frame,
        nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.hwnd!=nullptr,"opacity fixture window");
    state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
    state.visible=state.inScope=state.detailsViewActive=state.loaded=true;
    state.firstContentSubmittedAt=GetTickCount64(); state.featureInitializationStep=5;
    UpdateMetrics(state); Check(EnsureTarget(state),"opacity GPU target");
    ExplorerColumn name; name.key=PKEY_FileName; name.width=250;
    ExplorerColumn date; date.key=PKEY_DateModified; date.width=220;
    state.columns={name,date}; UpdateColumns(state);
    state.total=3; state.entries.resize(3); state.selectedFlags.resize(3);
    for(int i=0;i<3;++i) {
        auto& e=state.entries[i]; e.name=L"Opacity "+std::to_wstring(i)+L".txt";
        e.path=JoinPath(fixture,e.name); e.date=L"20/10/2026 12:25";
        e.detailsReady=true; e.propertyEpoch=state.propertyEpoch;
    }
    const auto first=state.entries[0].path, hidden=state.entries[1].path, cut=state.entries[2].path;
    state.entries[1].attributes=FILE_ATTRIBUTE_HIDDEN;
    auto paint=[&] { auto swap=std::move(state.swapChain); Paint(state); state.swapChain=std::move(swap); };
    auto& rows=state.itemOpacityRows;
    auto age=[&](double ms) {
        LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
        for(auto& [key,motion] : rows) if(motion.started.QuadPart)
            motion.started.QuadPart=now.QuadPart-LONGLONG(state.qpcFrequency*ms/1000.);
    };
    auto alpha=[&](const std::wstring& path) {
        return ItemContentOpacity(state,*std::find_if(state.entries.begin(),state.entries.end(),
            [&](const auto& e) { return e.path==path; }),true);
    };
    // Optional clipboard attachment can follow the first names-only paint.
    paint();
    testClipboard.Seed(DROPEFFECT_MOVE,cut); RefreshClipboardItems(state); paint();
    Check(rows.at(first).to==1 && rows.at(hidden).to==kDimmedItemContentOpacity &&
        rows.at(cut).from==kDimmedItemContentOpacity && !state.itemOpacityAnimating,
        "existing hidden/Cut items faded on initial directory/clipboard attachment");
    for(int mode : {0,1,2}) {
        state.columnsView=mode==0; state.gridView=mode==2;
        ++state.token; paint();
        Check(!state.itemOpacityAnimating,"view entry animated existing opacity");
        state.entries[0].attributes=FILE_ATTRIBUTE_HIDDEN; paint();
        Check(rows.at(first).from==1 && rows.at(first).to==kDimmedItemContentOpacity &&
            state.itemOpacityAnimating,"hidden toggle snapped instead of fading content");
        const auto started=rows.at(first).started.QuadPart;
        paint();
        Check(rows.at(first).started.QuadPart==started,"unchanged paints restarted the opacity clock");
        age(60); paint(); const float middle=alpha(first);
        Check(middle>kDimmedItemContentOpacity && middle<1,"hidden fade had no intermediate opacity");
        state.entries[0].attributes=0; paint();
        Check(rows.at(first).from>=kDimmedItemContentOpacity && rows.at(first).from<=middle &&
            rows.at(first).to==1,"opacity reversal jumped back to an endpoint");
        age(181); paint();
        Check(alpha(first)==1 && !state.itemOpacityAnimating,"180 ms hidden fade did not settle");
    }
    // The same clipboard identities can switch between Copy and Cut. Exercise
    // WM_CLIPBOARDUPDATE rather than recording a local Copy/Cut command.
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    for(int mode : {0,1,2}) {
        state.columnsView=mode==0; state.gridView=mode==2;
        const auto copied=first+L'\0'+hidden;
        testClipboard.Seed(DROPEFFECT_COPY,copied);
        OverlayProc(state.hwnd,WM_CLIPBOARDUPDATE,0,0); paint(); age(181); paint();
        Check(alpha(first)==1 && alpha(hidden)==kDimmedItemContentOpacity && !state.itemOpacityAnimating,
            "external Copy faded list/grid content or compounded hidden alpha");
        testClipboard.Seed(DROPEFFECT_MOVE,copied);
        OverlayProc(state.hwnd,WM_CLIPBOARDUPDATE,0,0); paint();
        Check(rows.at(first).from==1 && rows.at(first).to==kDimmedItemContentOpacity &&
            !rows.at(hidden).started.QuadPart,"same-path Copy to Cut did not fade exclusively for Cut");
        age(181); paint();
        testClipboard.Seed(DROPEFFECT_COPY,copied);
        OverlayProc(state.hwnd,WM_CLIPBOARDUPDATE,0,0);
        Check(state.itemOpacityAnimating,"same-path Cut to Copy lost its inverse-fade wake");
        paint();
        Check(rows.at(first).from==kDimmedItemContentOpacity && rows.at(first).to==1,
            "same-path Cut to Copy retained dimmed content");
        age(181); paint();
        Check(alpha(first)==1 && !state.itemOpacityAnimating,"Cut to Copy fade failed to settle");
    }
    testClipboard.Seed(DROPEFFECT_MOVE,cut); RefreshClipboardItems(state); paint(); age(181); paint();
    // Clipboard replacement changes both sets in the same paint; overlap with
    // hidden state has no second dimming level or redundant transition.
    testClipboard.Seed(DROPEFFECT_MOVE,first+L'\0'+hidden); RefreshClipboardItems(state); paint();
    Check(rows.at(first).from==1 && rows.at(first).to==kDimmedItemContentOpacity &&
        rows.at(cut).from==kDimmedItemContentOpacity && rows.at(cut).to==1 &&
        !rows.at(hidden).started.QuadPart,"replacing Cut failed to fade both sets or compounded hidden alpha");
    age(60); paint();
    Check(alpha(first)>kDimmedItemContentOpacity && alpha(first)<1 &&
        alpha(cut)>kDimmedItemContentOpacity && alpha(cut)<1,"Cut replacement had no intermediate alpha");
    const auto firstStart=rows.at(first).started.QuadPart;
    std::swap(state.entries[0],state.entries[2]); paint();
    Check(rows.at(first).started.QuadPart==firstStart,"row reorder restarted identity-owned opacity");
    age(181); paint();
    RefreshClipboardItems(state,true); paint();
    Check(rows.at(first).from==kDimmedItemContentOpacity && rows.at(first).to==1 &&
        !rows.at(hidden).started.QuadPart,"Cut cancellation failed to fade in or animated a still-hidden item");
    // Use the production view transition without changing directory tokens.
    // The old mode loop above only exercised settled layout flags.
    state.graphicsReady=true;
    ApplyViewLayout(state,{FVM_DETAILS,48},false); Paint(state);
    for(const UINT mode : {UINT(FVM_ICON),UINT(FVM_DETAILS)}) {
        testClipboard.Seed(DROPEFFECT_MOVE,first); RefreshClipboardItems(state); Paint(state); age(181); Paint(state);
        ApplyViewLayout(state,{mode,48},true); Paint(state);
        Check(state.modeRootAttached,"Cut mode fixture bypassed the compositor transition");
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
        UnregisterAnimationWindow(state.hwnd); // Prior Cut fade has settled.
        OverlayProc(state.hwnd,WM_KEYDOWN,VK_ESCAPE,0);
        bool registered=false;
        AcquireSRWLockShared(&g_frameLock);
        registered=std::any_of(g_activeFrames.begin(),g_activeFrames.end(),[&](const auto& f) { return f.hwnd==state.hwnd; });
        ReleaseSRWLockShared(&g_frameLock);
        Check(registered,"Cut cancellation dropped its repaint clock before opacity could retarget");
        Paint(state);
        Check(state.clipboardPaths.empty() && rows.at(first).to==1 && !state.clipboardIconsVisible,
            "view switch/Escape desynchronized Cut wiggle and opacity target");
        age(181); Paint(state);
        Check(alpha(first)==1 && alpha(hidden)==kDimmedItemContentOpacity && !state.itemOpacityAnimating,
            "view switch/Escape retained Cut opacity after its fade completed");
        FinishColumnTransition(state);
    }
    ++state.token; paint();
    Check(!state.itemOpacityAnimating && alpha(first)==1 && alpha(hidden)==kDimmedItemContentOpacity,
        "new/cached directory token replayed an old opacity transition");
    // Exercise the production hide handler without showing any test windows.
    testClipboard.Seed(DROPEFFECT_MOVE,first); RefreshClipboardItems(state); paint();
    Check(state.itemOpacityAnimating,"hide test had no in-flight fade");
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&state));
    WINDOWPOS pos{}; pos.hwnd=state.hwnd; pos.flags=SWP_HIDEWINDOW;
    OverlayProc(state.hwnd,WM_WINDOWPOSCHANGED,0,reinterpret_cast<LPARAM>(&pos));
    Check(rows.empty() && !state.itemOpacityAnimating,"hidden overlay retained opacity history");
    paint(); Check(!state.itemOpacityAnimating,"returning to a hidden surface replayed a fade");
    // Only the displayed viewport retains observations, even in a large folder.
    state.gridView=state.columnsView=false; state.total=1000;
    state.entries.resize(state.total); state.selectedFlags.resize(state.total);
    for(int i=3;i<state.total;++i) {
        state.entries[i].name=L"Item "+std::to_wstring(i);
        state.entries[i].path=JoinPath(fixture,state.entries[i].name);
    }
    SetScrollImmediate(state,0); paint(); const size_t viewport=rows.size();
    SetScrollImmediate(state,MaxScrollPx(state)); paint();
    Check(rows.size()<=viewport+1 && !rows.contains(first),"opacity history grew with directory traversal");
    SetScrollImmediate(state,0); paint();
    Check(!state.itemOpacityAnimating && alpha(first)==kDimmedItemContentOpacity,
        "scrolling back animated a pre-existing Cut item");
    // UI diagnostics alone must capture the actual drawing path, without
    // enabling high-volume pipeline events or starting extra rendering work.
    const bool uiLogging=g_uiTransitionLogging.exchange(true);
    state.background->pipeline=std::make_unique<PipelineTrace>();
    state.background->pipelinePublished=state.background->pipeline.get();
    TraceUiState(state,L"ui.test.paint"); paint();
    std::array<PipelineRecord,128> records;
    const size_t count=state.background->pipeline->Drain(records.data(),records.size());
    const auto has=[&](const wchar_t* prefix) {
        return std::any_of(records.begin(),records.begin()+count,[&](const auto& record) {
            return record.operation && wcsncmp(record.operation,prefix,wcslen(prefix))==0;
        });
    };
    Check(has(L"ui.item[") && has(L"ui.label.rect") && has(L"ui.icon.rect") &&
        has(L"ui.icon.") && has(L"ui.text.") && has(L"ui.surface[") &&
        state.background->pipeline->frame>0,"UI-only tracing missed actual opacity/geometry/icon/text/presentation records");
    g_uiTransitionLogging=false; EnsurePipelineTrace(state); paint();
    Check(state.background->pipeline->count==0 && !state.uiTracePaint && !state.uiTraceDrawItem,
        "disabled UI diagnostics retained per-frame output");
    g_uiTransitionLogging=uiLogging;
    RefreshClipboardItems(state,true);
    SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,0);
    UnregisterAnimationWindow(state.hwnd); DropTarget(state); DestroyWindow(state.hwnd);
    std::cout<<"PASS: 180 ms list/grid opacity, initial hidden/Cut baseline, reversal/replacement/cancel, actual compositor view switch/Escape wake and settlement, hidden overlap, reorder, navigation/hide reset and bounded history\n";
}
