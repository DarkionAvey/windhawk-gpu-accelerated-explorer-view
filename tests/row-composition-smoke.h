// Private GPU surfaces and an intentionally unpumped owner STA. DWM frame
// statistics establish compositor liveness, not monitor scan-out or live FPS.
template<class State>
static void TestRowComposition(HWND frame) {
    State state;
    if constexpr (!requires {state.modeComposition.rows;}) {
        Check(false,"structural animation still depends on owner-thread presentation");
    } else {
        const auto automatic=g_autoRefitColumns.exchange(false);
        for(bool rtl:{false,true}) for(bool grid:{false,true}) for(bool grouped:{false,true}) {
            State s;
            s.hwnd=CreateWindowExW(0,L"STATIC",L"private row compositor",WS_CHILD,
                0,0,800,480,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
            Check(s.hwnd!=nullptr,"row compositor window");
            s.frameHwnd=s.activeTabHost=frame; s.nativeHwnd=s.hwnd; s.background->hwnd=s.hwnd;
            FocusFixture::Session focus(frame,s.hwnd);
            s.visible=s.inScope=s.loaded=s.detailsViewActive=true; s.rtlLayout=rtl;
            s.firstContentSubmittedAt=1; s.featureInitializationStep=kOverlayFeatureSteps;
            s.deferColumnProviders=false; s.nativeTextColor=s.nativeHeaderTextColor=RGB(255,255,255);
            s.total=60; s.entries.resize(60); s.selectedFlags.assign(60,1);
            for(int i=0;i<s.total;++i) {
                auto& e=s.entries[i]; e.name=L"Visible row "+std::to_wstring(i);
                e.path=L"private-row:"+std::to_wstring(i); e.folder=true;
                e.groupKey=std::to_wstring(i/20); e.groupTitle=L"Group "+e.groupKey;
                e.detailsReady=true;
            }
            ExplorerColumn name; name.key=PKEY_FileName; name.title=L"Name"; name.width=400;
            s.columns={name}; s.grouping.enabled=grouped;
            ApplyViewLayout(s,{UINT(grid ? FVM_ICON : FVM_DETAILS),64},false);
            UpdateMetrics(s); UpdateColumns(s); RebuildGroupSections(s); UpdateRefreshRate(s);
            Check(EnsureTarget(s),"row compositor target");
            const auto iconSide=UINT(IconPixelSize(s));
            std::vector<UINT32> iconPixels(size_t(iconSide)*iconSide,0xFF3080C0);
            const auto iconProps=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED));
            Check(SUCCEEDED(s.target->CreateBitmap(D2D1::SizeU(iconSide,iconSide),iconPixels.data(),iconSide*4,
                &iconProps,&s.defaultFolderIcon.bitmap)),"row fixture icon");
            s.defaultFolderIcon.requestedSize=iconSide;
            Check(Eventually([&] {Paint(s); return !s.textRasterPending;}),"row compositor warm glyphs");
            const auto read=[&](ID2D1Bitmap1* source) {
                const auto size=source->GetPixelSize(); ComPtr<ID2D1Bitmap1> cpu;
                const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    source->GetPixelFormat());
                Check(SUCCEEDED(s.target->CreateBitmap(size,nullptr,0,&props,&cpu)) &&
                    SUCCEEDED(cpu->CopyFromBitmap(nullptr,source,nullptr)),"row source readback");
                D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"row source map");
                std::vector<BYTE> pixels(size_t(size.width)*size.height*4);
                for(UINT y=0;y<size.height;++y) memcpy(pixels.data()+size_t(y)*size.width*4,
                    map.bits+y*map.pitch,size_t(size.width)*4);
                cpu->Unmap(); return pixels;
            };
            const auto capture=[&](bool insert) {
                auto headers=CaptureVisibleGroupHeaders(s);
                std::vector<CapturedRow> rows;
                for(int i:VisibleItemRows(s,kRowTransitionOverscan)) {
                    const auto v=LayoutRowVisual(s,i,1.f);
                    CapturedRow c; c.oldIndex=i; c.newIndex=insert ? i+1 : i==0 ? -1 : i-1;
                    c.entry=s.entries[size_t(i)]; c.oldTop=v.top; c.oldHeight=v.height;
                    c.oldLeft=v.left; c.oldWidth=v.width; c.oldOpacity=v.opacity; c.selected=true;
                    rows.push_back(std::move(c));
                }
                const int oldTotal=s.total; const float row=s.rowPx;
                if(insert) {
                    auto e=s.entries.front(); e.name=L"Restored item";
                    e.path=L"private-restored:"+std::to_wstring(g_nextToken.fetch_add(1));
                    s.entries.insert(s.entries.begin(),std::move(e)); ++s.total;
                } else {s.entries.erase(s.entries.begin()); --s.total;}
                s.selectedFlags.assign(s.total,1); RebuildGroupSections(s);
                if(grid || grouped) StartGroupingTransition(s,std::move(rows),std::move(headers),row,false);
                else StartRowTransition(s,std::move(rows),oldTotal,row,s.scrollNow);
                Check(s.rowTransitionActive,"row fixture did not start real motion");
            };
            capture(false); Paint(s);
            Check(s.graphicsReady && s.modeRootAttached && s.modeComposition.rows && s.modeComposition.published,
                "structural animation still depends on owner-thread presentation");
            Check(s.rowTransitionStartedQpc.QuadPart && s.modeComposition.started.QuadPart==s.rowTransitionStartedQpc.QuadPart,
                "row scene did not publish the shared animation origin");
            Check(std::any_of(s.modeComposition.items.begin(),s.modeComposition.items.end(),
                [](const auto& item) {return item.key<0;}),"deleted item has no compositor ghost");
            const auto text=read(s.modeComposition.metadata.bitmap.Get());
            const auto icons=read(s.modeComposition.names.bitmap.Get());
            const auto feedback=read(s.modeComposition.feedback.bitmap.Get());
            const auto ink=[](const auto& p) {return std::any_of(p.begin(),p.end(),[](BYTE b) {return b!=0;});};
            std::cout<<"ROW SOURCES rtl="<<rtl<<" grid="<<grid<<" grouped="<<grouped<<" text="<<ink(text)
                <<" icons="<<ink(icons)<<" feedback="<<ink(feedback)<<" focus="<<s.inputFocusIndicators<<std::endl;
            Check(ink(text) && ink(icons) && ink(feedback),"packed sources lost item text, icons or selection");
            Check(!grouped || ink(read(s.modeComposition.groups.bitmap.Get())),"group headings missing from compositor");
            Check(s.modeComposition.names.bitmap->GetPixelSize().height<6*480,
                "row sources scale with the directory rather than the viewport");
            const auto root=s.modeComposition.root.Get(); const auto atlas=s.modeComposition.names.bitmap.Get();
            const auto atlasSize=atlas->GetPixelSize(); const auto sliceCount=s.modeComposition.items.size();
            const auto origin=s.rowTransitionStartedQpc.QuadPart; const auto paint=s.graphicsPaintSerial;
            const auto progress=s.rowTransitionPresentedProgress;
            DCOMPOSITION_FRAME_STATISTICS before{},after{};
            Check(SUCCEEDED(s.composition->GetFrameStatistics(&before)),"row compositor clock before blocked owner");
            Sleep(80); // No messages, Paint, frame callback, or provider work.
            Check(SUCCEEDED(s.composition->GetFrameStatistics(&after)) && after.currentTime.QuadPart>before.currentTime.QuadPart &&
                s.graphicsPaintSerial==paint && s.rowTransitionPresentedProgress==progress &&
                s.modeComposition.root.Get()==root && s.rowTransitionStartedQpc.QuadPart==origin &&
                read(s.modeComposition.metadata.bitmap.Get())==text,
                "blocked owner changed or repainted the committed row scene");
            Check(LayoutTransitionProgress(s,true,s.rowTransitionStartedQpc)>progress,
                "row hit geometry cannot catch up to compositor time");
            Paint(s);
            Check(s.modeComposition.root.Get()==root && s.modeComposition.names.bitmap.Get()==atlas &&
                s.rowTransitionStartedQpc.QuadPart==origin,"an enrichment paint rebuilt or retimed row motion");
            // A scene replacement from scroll or another names commit reuses
            // packed storage, rather than allocating per-item viewport surfaces.
            SetScrollImmediate(s,std::min(20.f,MaxScrollPx(s))); Paint(s);
            if(!s.modeComposition.rows || s.modeComposition.names.bitmap.Get()!=atlas || s.rowTransitionStartedQpc.QuadPart!=origin)
                std::cout<<"ROW SCROLL rows="<<s.modeComposition.rows<<" atlas="<<(s.modeComposition.names.bitmap.Get()==atlas)
                    <<" active="<<s.rowTransitionActive<<" origin="<<origin<<" now="<<s.rowTransitionStartedQpc.QuadPart
                    <<" size="<<atlasSize.width<<","<<atlasSize.height<<" slices="<<sliceCount<<" new="
                    <<s.modeComposition.names.bitmap->GetPixelSize().width<<","<<s.modeComposition.names.bitmap->GetPixelSize().height
                    <<" slices="<<s.modeComposition.items.size()<<std::endl;
            Check(s.modeComposition.rows && s.modeComposition.names.bitmap.Get()==atlas &&
                s.rowTransitionStartedQpc.QuadPart==origin,"scroll replaced packed storage or restarted row motion");
            const auto previous=s.modeComposition.items;
            const auto previousOrigin=s.modeComposition.started;
            const float previousScroll=s.scrollNow;
            capture(true);
            Sleep(60); // A new names snapshot waits before its scene can commit.
            Paint(s);
            const float oldProgress=LayoutTransitionProgress(s,true,previousOrigin,s.modeComposition.started);
            size_t matched=0;
            for(const auto& item:s.modeComposition.items) {
                const auto old=std::find_if(previous.begin(),previous.end(),[&](const auto& p) {return p.identity==item.identity;});
                if(old==previous.end()) continue;
                const float top=old->from.top+(old->to.top-old->from.top)*oldProgress-(s.scrollNow-previousScroll);
                const float alpha=old->from.opacity+(old->to.opacity-old->from.opacity)*oldProgress;
                Check(fabsf(item.from.top-top)<.02f && fabsf(item.from.opacity-alpha)<.001f,
                    "delayed replacement jumped from its snapshot instead of the committed compositor position");
                if(item.key>=0) Check(fabsf(LayoutRowVisual(s,item.key,0).top-item.from.top)<.02f,
                    "retargeted hit geometry disagrees with the compositor origin");
                ++matched;
            }
            Check(matched>0 && s.rowTransitionPresentedProgress==0.f,"replacement did not publish its new shared origin");
            StopRowTransition(s); Paint(s);
            Check(!s.modeRootAttached && !s.modeComposition.rows,"settled rows retained temporary compositor resources");
            SetScrollImmediate(s,0); capture(true); Paint(s);
            Check(s.modeComposition.rows && s.modeComposition.published &&
                std::any_of(s.modeComposition.items.begin(),s.modeComposition.items.end(),
                    [](const auto& item) {return item.key>=0 && item.from.opacity<item.to.opacity;}),
                "restored items did not commit an independent entrance fade");
            Sleep(30); capture(false); Paint(s); // Delete a still-entering item.
            for(const auto& item:s.modeComposition.items)
                Check(item.cell.bottom-item.cell.top+1.f>=std::max(item.from.height,item.to.height),
                    "retargeted growing ghost can sample a neighboring atlas cell");
            Sleep(kUiTransitionDurationMs+30);
            Check(s.rowTransitionActive && LayoutTransitionProgress(s,true,s.rowTransitionStartedQpc)==1.f,
                "blocked owner could not leave a committed animation to finish independently");
            // Hide through the real warm-tab retirement path and bake the
            // accepted image; no row atlas may survive standby or full teardown.
            s.visible=false; s.graphicsStandby=s.graphicsStandbyPending=true;
            AdvanceStandbyGraphics(s);
            Check(!s.modeRootAttached && !s.modeComposition.rows && s.graphicsReady,
                "hidden row animation lost the warm device or retained temporary planes");
            UnregisterAnimationWindow(s.hwnd); Stop(s.background); DropTarget(s); DestroyWindow(s.hwnd);
        }
        g_autoRefitColumns=automatic;
        std::cout<<"PASS: LTR/RTL Details/grid and grouped delete/restore use packed compositor layers; 80 ms unpumped owner, immutable sources/clock, enrichment, concurrent scroll storage reuse, settlement and warm standby\n";
    }
}
