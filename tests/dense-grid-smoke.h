// Log-sized private viewport; generated identities never reach the filesystem.
static void TestDenseRowPublication() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"dense row fonts");
    for(bool rtl:{false,true}) {
        PresentationFixture::Tab tab(L"dense row publication",4000,rtl,true,false,4751,1958);
        auto& s=tab.state;
        s.rowHeight96[1]=41; UpdateMetrics(s); UpdateColumns(s);
        Check(EnsureTarget(s),"dense row publication target");
        LARGE_INTEGER frequency{},begin{},end{}; QueryPerformanceFrequency(&frequency);
        std::vector<double> build,commit;
        size_t cells=0,bytes=0;
        // Publish successive real insertion scenes without waiting for their
        // clocks to finish. No Shell/filesystem/focus/clipboard APIs are used.
        for(int pass=0;pass<12;++pass) {
            std::vector<CapturedRow> captures;
            const float p=LayoutTransitionProgress(s,s.rowTransitionActive,s.rowTransitionStartedQpc);
            for(int i:VisibleItemRows(s,kRowTransitionOverscan)) {
                const auto v=LayoutRowVisual(s,i,p);
                CapturedRow c; c.oldIndex=i; c.newIndex=i+1;
                c.oldTop=v.top; c.oldHeight=v.height; c.oldOpacity=v.opacity;
                c.oldLeft=v.left; c.oldWidth=v.width; captures.push_back(std::move(c));
            }
            auto entry=s.entries.front(); entry.path=L"dense-insertion:"+std::to_wstring(pass);
            s.entries.insert(s.entries.begin(),std::move(entry)); ++s.total;
            s.selectedFlags.assign(size_t(s.total),0);
            StartGroupingTransition(s,std::move(captures),{},s.rowPx);
            Check(s.rowTransitionActive && PrepareRowComposition(s),"dense insertion scene preparation");
            QueryPerformanceCounter(&begin);
            Check(PublishRowScene(s),"dense insertion scene build");
            QueryPerformanceCounter(&end);
            build.push_back(1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart);
            QueryPerformanceCounter(&begin);
            Check(SUCCEEDED(s.compTarget->SetRoot(s.modeComposition.root.Get())) && SUCCEEDED(s.composition->Commit()),
                "dense insertion scene commit");
            QueryPerformanceCounter(&end);
            commit.push_back(1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart);
            s.modeRootAttached=s.modeComposition.published=true;
            cells=s.modeComposition.items.size();
            bytes=s.modeComposition.feedback.estimatedBytes+s.modeComposition.names.estimatedBytes+s.modeComposition.metadata.estimatedBytes;
            Check(cells>400 && cells<size_t(s.total),"dense insertion lost viewport culling");
            for(const auto& item:s.modeComposition.items) if(item.key>=0) {
                const auto from=LayoutRowVisual(s,item.key,0.f),to=LayoutRowVisual(s,item.key,1.f);
                Check(fabsf(item.from.top-from.top)<.02f && fabsf(item.to.top-to.top)<.02f &&
                    fabsf(item.from.left-from.left)<.02f && fabsf(item.to.left-to.left)<.02f,
                    "dense replacement disagrees with shared hit geometry");
            }
        }
        std::sort(build.begin(),build.end()); std::sort(commit.begin(),commit.end());
        std::cout<<"PROFILE: dense insertion rtl="<<rtl<<" cells="<<cells<<" bytes="<<bytes
            <<" scene-build CPU p50="<<build[build.size()/2]<<" p95="<<build[build.size()-2]
            <<" commit p50="<<commit[commit.size()/2]<<" max="<<commit.back()<<" ms (private compositor, no scan-out)"<<std::endl;
    }
    g_writeFactory.Reset();
}
static void TestDenseIconLookup() {
    ViewState s;
    std::vector<std::wstring> keys;
    IconPixels pixels; pixels.width=pixels.height=16; pixels.stride=64; pixels.data.assign(1024,255);
    for(int i=0;i<512;++i) {
        const auto base=L"wh-dense-icon:"+std::wstring(256,L'x')+std::to_wstring(i);
        keys.push_back(SizedImageKey(base,256));
        s.icons[keys.back()].pixels=pixels;
    }
    LARGE_INTEGER begin{},end{},frequency{};
    QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
    for(int pass=0;pass<16;++pass) for(const auto& key:keys)
        Check(FindIconRecord(s,key,true)==&s.icons.at(key),"small artwork lost its exact-tier fallback");
    QueryPerformanceCounter(&end);
    std::cout<<"PROFILE: 512 long provider icon keys fallback-ms="
        <<1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart/16
        <<" (CPU lookup, no provider calls)"<<std::endl;
    const auto base=BaseIconKey(keys.front());
    const auto lower=SizedImageKey(base,96), higher=SizedImageKey(base,512);
    s.icons[lower].pixels=pixels; s.icons[higher].pixels=pixels;
    s.icons[higher].pixels.width=s.icons[higher].pixels.height=512;
    s.icons[higher].pixels.stride=512*4; s.icons[higher].pixels.data.assign(512*512*4,255);
    std::wstring resolved;
    Check(FindIconRecord(s,keys.front(),true,false,&resolved)==&s.icons.at(higher) && resolved==higher,
        "tier lookup changed adequate-source preference or resolved identity");
    Check(FindIconRecord(s,higher,true,false,&resolved,true)==&s.icons.at(lower) && resolved==lower,
        "tier lookup lost skip-exact fallback ordering");
    // Full-string and allocation-free tier probes must retain exact identities
    // across rehash, explicit @32 keys, nested separators and opaque Unicode.
    std::vector<std::wstring> identities{L"",L"wh:plain",L"@32|wh:plain",L"@00096|wh:plain",
        L"@not-a-tier|raw|identity",L"@64|@literal|value",L"@literal|value",L"wh:زهرة|תמונה"};
    identities.emplace_back(L"wh:one\0two",10);
    for(const auto& identity:identities) s.icons[identity].pixels=pixels;
    s.icons.rehash(s.icons.size()*2);
    for(const auto& identity:identities)
        Check(FindIconRecord(s,identity)==&s.icons.at(identity),"icon lookup aliased a distinct raw identity");
    Check(FindIconRecord(s,L"@192|@literal|value",true,false,&resolved)==&s.icons.at(L"@literal|value") &&
        resolved==L"@literal|value","unsized literal fallback changed identity parsing");
    s.defaultFolderIcon.pixels=pixels;
    Check(FindIconRecord(s,SizedImageKey(kDefaultFolderIconKey,256),true,false,&resolved)==&s.defaultFolderIcon &&
        resolved==kDefaultFolderIconKey,"shared folder fallback lost its reserved identity");
    std::vector<std::wstring> paths{L"",L"C:\\Alpha",L"c:\\alpha",L"C:\\alpha\\Beta",L"C:\\álpha",
        L"wh:زهرة",L"wh:תמונה",std::wstring(1024,L'a')+L"A",std::wstring(1024,L'a')+L"b"};
    paths.emplace_back(L"same\0trailing",13); paths.emplace_back(L"same\0other",10);
    for(const auto& a:paths) for(const auto& b:paths)
        Check(ClipboardPathLess{}(a,b)==(_wcsicmp(a.c_str(),b.c_str())<0),
            "fast identity comparison changed case, Unicode or terminator behavior");
    for(size_t length=0;length<40;++length) for(size_t position=0;position<=length;++position) {
        std::wstring a(length,L'א'),b=a;
        if(position<length) b[position]=L'B';
        Check(ClipboardPathLess{}(a,b)==(_wcsicmp(a.c_str(),b.c_str())<0) &&
            ClipboardPathLess{}(b,a)==(_wcsicmp(b.c_str(),a.c_str())<0),"path block boundary changed ordering");
        if(position<length) {
            a[position]=b[position]=0;
            if(position+1<length) b[position+1]=L'C';
            Check(!ClipboardPathLess{}(a,b) && !ClipboardPathLess{}(b,a),"path block skipped an embedded terminator");
        }
    }
}

static void TestDenseProviderFrames() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"provider frame fonts");
    {
        PresentationFixture::Tab tab(L"dense provider frames",1003,false,true,false,4751,1958);
        auto& s=tab.state;
        s.presentationOnly=s.presentationActive=true;
        s.presentationClient={0,0,4751,1958}; s.presentationDpi=96;
        s.rowHeight96[1]=41; UpdateMetrics(s); UpdateColumns(s);
        Check(EnsureTarget(s) && UploadIconBitmap(s,s.defaultFolderIcon),"provider frame icon");
        std::fill(s.selectedFlags.begin(),s.selectedFlags.end(),0);
        for(int i=0;i<s.total;++i) {
            auto& e=s.entries[size_t(i)];
            e.name=L"Provider item "+std::to_wstring(i)+L".exe"; e.folder=false;
            e.path=L"wh-dense-provider:"+std::wstring(1024,L'a')+std::to_wstring(i);
            e.date=L"27/10/2026 20:00"; e.type=L"Application"; e.size=L"12 KB";
            if(i<512) {
                std::wstring path; bool attributes=false;
                const auto key=IconKey(s,e,path,attributes);
                auto& icon=s.icons[key]; icon.pixels=s.defaultFolderIcon.pixels;
                icon.bitmap=s.defaultFolderIcon.bitmap; icon.resolved=true;
                icon.requestedSize=ImageKeySize(key);
                AccountIconCache(s,key,icon);
            }
        }
        s.presentationEntries=std::make_shared<const std::vector<Entry>>(s.entries);
        LARGE_INTEGER begin{},end{},frequency{};
        QueryPerformanceFrequency(&frequency);
        // Exercise actual owner-side snapshot acquisition as well as painting.
        s.presentationOnly=false;
        QueryPerformanceCounter(&begin);
        for(int n=0;n<12;++n) {
            const auto model=CapturePresentationModel(s);
            Check(model->items.size()==1003 && model->images.size()==513,
                "dense provider snapshot lost nearby rows or ready icons");
        }
        QueryPerformanceCounter(&end);
        std::cout<<"PROFILE: 1003 provider rows / 512 cached icons / 1024-character identities capture-ms="
            <<1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart/12<<std::endl;
        s.presentationOnly=true;
        struct Offscreen {
            ViewState& state; ComPtr<IDXGISwapChain1> swap; HANDLE latency;
            ~Offscreen() {state.swapChain=std::move(swap); state.frameLatencyWaitable=latency;}
        } offscreen{s,std::move(s.swapChain),std::exchange(s.frameLatencyWaitable,nullptr)};
        for(int n=0;n<300;++n) {Paint(s); if(!s.textRasterPending) break;}
        Check(!s.textRasterPending,"provider frame text did not warm");
        const auto bytes=GlyphCacheBytes(s), icons=s.icons.size();
        QueryPerformanceCounter(&begin);
        for(int n=0;n<24;++n) {
            s.scrollNow=s.scrollTarget=float(n%8)*.125f;
            s.frameProfile.recording=true; s.frameProfile.textMisses=0;
            Paint(s);
            Check(s.frameProfile.textMisses==0,"provider frame rebuilt warm labels");
        }
        QueryPerformanceCounter(&end);
        std::cout<<"PROFILE: dense provider paint-ms="
            <<1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart/24
            <<" (offscreen CPU submission, no provider calls or scan-out)"<<std::endl;
        Check(GlyphCacheBytes(s)==bytes && s.icons.size()==icons,"provider frame added cache resources");
        const auto visible=VisibleItemRows(s,0.f);
        std::vector<ItemGeometry> geometry;
        for(int i:visible) geometry.push_back(LayoutItem(s,LayoutRowVisual(s,i,1.f)));
        for(int stage=0;stage<3;++stage) {
            QueryPerformanceCounter(&begin);
            for(int n=0;n<24;++n) {
                if(stage!=0) {s.target->BeginDraw(); s.target->Clear(D2D1::ColorF(0,0.f));}
                for(size_t j=0;j<visible.size();++j) {
                    const auto& e=ModelEntry(s,visible[j]); const auto& g=geometry[j];
                    if(stage==0) Check(ItemContentOpacity(s,e,true)>0,"provider opacity");
                    else if(stage==1) DrawActualIcon(s,e,g.icon.left,g.icon.top);
                    else Text(s,e.name,g.text.left,g.text.top,g.text.right,g.text.bottom,
                        0xFFFFFF,false,1,false,g.wrapped,0,false,true);
                }
                if(stage!=0) Check(SUCCEEDED(s.target->EndDraw()),"provider isolated stage");
            }
            QueryPerformanceCounter(&end);
            std::cout<<"PROFILE: isolated provider "<<(stage==0 ? "opacity" : stage==1 ? "icons" : "labels")
                <<" CPU-ms="<<1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart/24<<std::endl;
        }
    }
    g_writeFactory.Reset();
}

static void TestDenseGridDrawing() {
    TestDenseRowPublication();
    TestDenseIconLookup();
    TestDenseProviderFrames();
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"dense grid fonts");
    const auto ellipsis=g_middleEllipsis.load();
    g_middleEllipsis=ViewOption::GridOnly;
    for(int dpi:{96,144,192}) for(bool rtl:{false,true}) {
        PresentationFixture::Tab tab(L"dense grid",1003,rtl,true,false,4751,1958);
        auto& s=tab.state;
        s.presentationOnly=s.presentationActive=true;
        s.presentationClient={0,0,4751,1958}; s.presentationDpi=dpi;
        s.rowHeight96[1]=41; UpdateMetrics(s); UpdateColumns(s);
        for(int i=0;i<s.total;++i) {
            auto& e=s.entries[size_t(i)];
            e.name=(i%3 ? L"A long file name with wrapped text and preserved suffix " : L"Short ")+
                std::to_wstring(i)+L".txt";
            s.selectedFlags[size_t(i)]=i%7==0;
        }
        s.presentationEntries=std::make_shared<const std::vector<Entry>>(s.entries);
        Check(EnsureTarget(s),"dense grid target");
        // Keep the real target/paint path, bypassing scan-out/readiness waits.
        struct Offscreen {
            ViewState& state;
            ComPtr<IDXGISwapChain1> swap;
            HANDLE latency;
            ~Offscreen() {state.swapChain=std::move(swap); state.frameLatencyWaitable=latency;}
        } offscreen{s,std::move(s.swapChain),std::exchange(s.frameLatencyWaitable,nullptr)};
        const auto draw=[&] {
            s.frameProfile.recording=true;
            s.frameProfile.textHits=s.frameProfile.textMisses=s.frameProfile.textDeferred=0;
            Paint(s);
        };
        for(int n=0;n<300;++n) {draw(); if(!s.textRasterPending) break;}
        Check(!s.textRasterPending,"dense grid text did not warm within its existing budget");
        std::cout<<"PROFILE: dense grid warm pending="<<s.textRasterPending
            <<" hits/misses="<<s.frameProfile.textHits<<"/"<<s.frameProfile.textMisses
            <<" glyph-bytes="<<GlyphCacheBytes(s)<<std::endl;
        const auto bytes=GlyphCacheBytes(s);
        const auto serial=s.graphicsPaintSerial;
        size_t hits=0,misses=0;
        LARGE_INTEGER begin{},end{},frequency{};
        QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&begin);
        for(int frame=0;frame<24;++frame) {
            // Continuous translation, with selected labels using the same shape.
            s.scrollNow=s.scrollTarget=float(frame%8)*.125f;
            draw(); hits+=s.frameProfile.textHits; misses+=s.frameProfile.textMisses;
        }
        QueryPerformanceCounter(&end);
        std::cout<<"PROFILE: dense grid dpi="<<dpi<<" rtl="<<rtl<<" row="<<s.rowPx
            <<" visible="<<VisibleItemRows(s,0.f).size()<<" hits/misses="<<hits<<"/"<<misses
            <<" glyph-bytes="<<bytes<<"->"<<GlyphCacheBytes(s)
            <<" paint-ms="<<1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart/24
            <<" (offscreen CPU submission, no scan-out)"<<std::endl;
        Check(hits>1000 && misses==0,"fractional scrolling rebuilt warmed grid label masks");
        Check(s.graphicsReady && s.graphicsPaintSerial==serial+24,"dense grid skipped drawing or lost its target");
        Check(GlyphCacheBytes(s)==bytes,"translated grid labels consumed more cache memory");
        if(dpi==96 && !rtl) {
            s.zoomFromRowPx=s.rowPx; s.zoomFromFontPx=s.fontPx; s.zoomFromIconPx=s.iconPx;
            s.zoomAnimating=true; s.rowHeight96[1]=49;
            std::vector<double> times;
            for(int tick=0;tick<24;++tick) {
                UpdateMetrics(s,float(tick+1)/24); Check(MakeTextFormats(s,true),"dense zoom format");
                QueryPerformanceCounter(&begin); draw(); QueryPerformanceCounter(&end);
                times.push_back(1000.*double(end.QuadPart-begin.QuadPart)/frequency.QuadPart);
                Check(s.graphicsReady,"dense zoom lost its target");
            }
            std::sort(times.begin(),times.end());
            std::cout<<"PROFILE: dense wrapped-label zoom CPU p50="<<times[times.size()/2]
                <<" p95="<<times[times.size()*95/100]<<" max="<<times.back()<<std::endl;
        }
    }
    g_middleEllipsis=ellipsis;
    g_writeFactory.Reset();
    std::cout<<"PASS: dense wrapped labels retain their masks and memory through fractional LTR/RTL scrolling\n";
}
