// Private process/targets only. DXGI reports this process's adapter usage,
// not total card usage or displayed frame rate. Stop a diagnostic baseline
// early rather than reproducing the user's multi-gigabyte exhaustion.
#include <dxgi1_4.h>
#include <psapi.h>
static void TestGraphicsMemoryProbe() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"memory probe fonts");
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"GPU memory fixture",
        WS_POPUP,-32000,-32000,3840,2160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(frame!=nullptr,"memory probe frame");
    std::vector<std::unique_ptr<ViewState>> tabs;
    ComPtr<IDXGIAdapter3> adapter;
    UINT64 baseline=0;
    const auto report=[&](const char* phase) {
        DXGI_QUERY_VIDEO_MEMORY_INFO local{},shared{};
        const HRESULT hr=adapter ? adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&local) : E_NOINTERFACE;
        if(adapter) adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL,&shared);
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
        Check(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))!=FALSE,
            "memory probe private usage");
        std::cout<<"GPU MEMORY "<<phase<<" tabs="<<tabs.size()<<" local-MiB="<<local.CurrentUsage/1048576.
            <<" shared-MiB="<<shared.CurrentUsage/1048576.<<" budget-MiB="<<local.Budget/1048576.
            <<" private-MiB="<<memory.PrivateUsage/1048576.<<" query-hr=0x"<<std::hex<<static_cast<unsigned>(hr)<<std::dec<<std::endl;
        return local.CurrentUsage;
    };
    report("before");
    for(unsigned i=0;i<4;++i) {
        auto state=std::make_unique<ViewState>();
        state->hwnd=CreateWindowExW(0,L"STATIC",L"private memory surface",WS_CHILD,
            0,0,3840,2160,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(state->hwnd!=nullptr,"memory probe child");
        state->frameHwnd=state->activeTabHost=frame; state->nativeHwnd=state->hwnd;
        state->fontPx=16; state->rowPx=32; state->loaded=true;
        Check(EnsureTarget(*state),"memory probe target");
        if(!adapter) {
            ComPtr<IDXGIAdapter> actual;
            Check(SUCCEEDED(state->dxgiDevice->GetAdapter(&actual)),"memory probe adapter"); actual.As(&adapter);
            baseline=report("adapter");
        }
        Paint(*state);
        bool pressure=false;
        for(unsigned first=0;first<1024 && !pressure;first+=128) {
            state->target->BeginDraw();
            for(unsigned j=first;j<first+128;++j) {
                const float x=static_cast<float>((j%16)*220),y=static_cast<float>((j/16)%64*32);
                Text(*state,L"Memory glyph "+std::to_wstring(j),x,y,x+210,y+28,0xFFFFFF);
            }
            Check(SUCCEEDED(state->target->EndDraw()),"memory probe glyph draw");
            pressure=report("glyphs")>baseline+512ULL*1024*1024;
        }
        SetColumnViewMode(*state,false);
        Paint(*state);
        std::cout<<"GPU MEMORY retained-scene="<<state->modeRootAttached<<" glyph-estimate-MiB="
            <<state->textLayouts[0].rasterBytes/1048576.<<std::endl;
        tabs.push_back(std::move(state));
        const auto used=report("created");
        if(pressure || used>baseline+512ULL*1024*1024) { std::cout<<"GPU MEMORY diagnostic safety limit reached\n"; break; }
    }
    for(auto& state : tabs) { DropTarget(*state); DestroyWindow(state->hwnd); }
    tabs.clear(); DestroyWindow(frame);
    report("released");
    g_writeFactory.Reset();
}

// Route the actual production hide, retirement and paint handlers. Other
// messages use a stock window: no Shell providers, native commands or focus.
static LRESULT CALLBACK MemoryFixtureProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_NCCREATE) {
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        return TRUE;
    }
    if(msg==kMsgRetireGraphics || msg==kMsgFrame || msg==WM_WINDOWPOSCHANGED || msg==WM_PAINT) return OverlayProc(hwnd,msg,wp,lp);
    return DefWindowProcW(hwnd,msg,wp,lp);
}
static void TestGraphicsResourceLifecycle() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"lifecycle fonts");
    WNDCLASSW cls{}; cls.lpfnWndProc=MemoryFixtureProc; cls.hInstance=GetModuleHandleW(nullptr);
    cls.lpszClassName=L"PrivateGpuMemoryLifecycle";
    Check(RegisterClassW(&cls)!=0,"lifecycle window class");
    HWND frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private offscreen memory fixture",
        WS_POPUP|WS_VISIBLE,-32000,-32000,3840,2160,nullptr,nullptr,cls.hInstance,nullptr);
    Check(frame!=nullptr,"lifecycle frame");
    FocusFixture::Session privateFocus(frame,nullptr);
    std::vector<std::unique_ptr<ViewState>> tabs;
    const unsigned initialDevices=g_gpuDevices.load();
    g_gpuMemoryLogging=true; // Exercise production DXGI/category snapshots, without altering other logging.
    ComPtr<IDXGIAdapter3> adapter;
    UINT64 peak=0;
    const auto memory=[&](const char* phase) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if(adapter) adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info);
        std::cout<<"GPU WARM "<<phase<<" local-MiB="<<info.CurrentUsage/1048576.<<std::endl;
    };
    const auto drain=[&](HWND hwnd) {
        MSG msg{};
        while(PeekMessageW(&msg,hwnd,kMsgRetireGraphics,kMsgRetireGraphics,PM_REMOVE)) DispatchMessageW(&msg);
    };
    const auto settle=[&](ViewState& state) {
        const bool settled=Eventually([&] {
            drain(state.hwnd);
            if(state.graphicsStandbyPending) SendMessageW(state.hwnd,kMsgFrame,0,0);
            return state.graphicsStandby && !state.graphicsStandbyPending;
        });
        if(!settled) std::cout<<"GPU STANDBY timeout ready="<<state.graphicsReady<<" standby="<<state.graphicsStandby
            <<" pending="<<state.graphicsStandbyPending<<" scene="<<state.modeRootAttached<<" started="<<state.modeComposition.started.QuadPart
            <<" rows="<<state.rowTransitionActive<<" columns="<<state.columnTransitionActive<<" mode="<<state.columnModeTransition
            <<" present-pending="<<state.presentPending<<" visible="<<IsWindowVisible(state.hwnd)
            <<" native-pending="<<state.nativePresentationPending<<" initial-pending="<<state.initialModePending
            <<" ops="<<state.graphicsOperations<<" initializing="<<state.graphicsInitializing<<" painting="<<state.textRasterPainting
            <<" overlay-init="<<state.overlayInitializing<<" queued="<<state.graphicsReleaseQueued
            <<" state-matches="<<(State(state.hwnd)==&state)<<" inactive="<<InactiveGraphics(state)
            <<" reported-visible="<<PresentationVisible(state)<<" presenter="<<bool(state.presenter)
            <<" view-id="<<(state.entries.empty() ? 0 : SelectionPathHash(state.entries[0].path))
            <<" retired-glyphs="<<state.retiredTextLayouts.size()<<" retired-icons="<<state.retiredIconBitmaps.size()<<std::endl;
        Check(settled,"standby scene/cache retirement did not finish on frame completion");
    };
    const auto pixels=[&](ViewState& state,ID2D1Bitmap* source) {
        ComPtr<ID2D1Bitmap1> cpu;
        const auto size=source->GetPixelSize();
        const auto format=source->GetPixelFormat();
        const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,format);
        Check(SUCCEEDED(state.target->CreateBitmap(size,nullptr,0,&props,&cpu)) &&
            SUCCEEDED(cpu->CopyFromBitmap(nullptr,source,nullptr)),"warm bitmap readback");
        D2D1_MAPPED_RECT map{};
        Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"warm bitmap map");
        const size_t rowBytes=size.width*(format.format==DXGI_FORMAT_A8_UNORM ? 1 : 4);
        std::vector<BYTE> result(rowBytes*size.height);
        for(UINT32 y=0;y<size.height;++y) memcpy(result.data()+y*rowBytes,map.bits+y*map.pitch,rowBytes);
        cpu->Unmap(); return result;
    };
    for(unsigned i=0;i<20;++i) {
        auto state=std::make_unique<ViewState>();
        state->frameHwnd=state->activeTabHost=frame;
        state->visible=state->inScope=state->detailsViewActive=state->loaded=true;
        state->fontPx=16; state->rowPx=32;
        state->hwnd=CreateWindowExW(0,cls.lpszClassName,L"private tab",WS_CHILD|WS_VISIBLE,
            0,0,3840,2160,frame,nullptr,cls.hInstance,state.get());
        Check(state->hwnd!=nullptr,"lifecycle tab");
        state->entries.emplace_back(); state->entries.back().name=L"retained private model";
        state->entries.back().path=L"memory-fixture:"+std::to_wstring(i)+L".png";
        state->total=1; state->selected=0;
        Check(EnsureTarget(*state),"lifecycle target");
        const auto device=state->d3d.Get();
        const auto swapchain=state->swapChain.Get();
        const auto format=state->textFormat.Get();
        std::wstring lookup; bool attributes=false;
        const auto iconKey=IconKey(*state,state->entries[0],lookup,attributes);
        auto& icon=state->icons[iconKey]; icon.resolved=true;
        icon.pixels.width=icon.pixels.height=32; icon.pixels.stride=128;
        icon.pixels.data.assign(4096,255);
        Check(UploadIconBitmap(*state,icon),"private cached icon upload");
        AccountIconCache(*state,iconKey,icon);
        if(!adapter) adapter=MemoryAdapter(*state);
        Paint(*state);
        if(!i) memory("initial-frame");
        state->target->BeginDraw();
        for(unsigned j=0;j<(i ? 256u : 1400u);++j)
            Text(*state,L"Lifecycle glyph "+std::to_wstring(j),0,0,210,28,0xFFFFFF);
        Check(SUCCEEDED(state->target->EndDraw()),"lifecycle glyphs");
        Check(GlyphCacheBytes(*state)<=kGlyphCacheBudget,"glyph allocation budget ignored retired textures");
        if(!i) memory("glyph-stress");
        SetColumnViewMode(*state,false); Paint(*state);
        Check(Eventually([&] {
            Paint(*state);
            const auto found=state->textLayouts[0].layouts.find(state->entries[0].name);
            return found!=state->textLayouts[0].layouts.end() && !found->second.rasters.empty() && icon.painted==state->lastAcceptedPaint;
        }),"visible name/icon did not become ready before hide");
        auto glyph=state->textLayouts[0].layouts.at(state->entries[0].name).rasters.back().bitmap;
        auto visibleIcon=icon.bitmap;
        const auto glyphPixels=pixels(*state,glyph.Get());
        const auto iconPixels=pixels(*state,visibleIcon.Get());
        auto& offscreen=state->icons[L"memory-fixture:offscreen"];
        offscreen.pixels.width=offscreen.pixels.height=32; offscreen.pixels.stride=128;
        offscreen.pixels.data.assign(4096,127);
        Check(UploadIconBitmap(*state,offscreen),"offscreen cached icon upload");
        if(i%2) []<class S>(S& s) {
            if constexpr(requires {s.modeComposition.rows;}) {
                FinishColumnTransition(s);
                CapturedRow old; old.oldIndex=old.newIndex=0; old.entry=s.entries[0];
                old.oldTop=s.headerPx+s.rowPx; old.oldHeight=s.rowPx; old.oldOpacity=1.f;
                std::vector<CapturedRow> rows; rows.push_back(std::move(old));
                StartRowTransition(s,std::move(rows),s.total+1,s.rowPx,s.scrollNow); Paint(s);
                Check(s.modeComposition.rows && s.modeComposition.published,"4K lifecycle row scene");
            }
        }(*state);
        state->animationActive=true;
        RegisterAnimationWindow(state->hwnd);
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if(adapter && SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info))) peak=std::max(peak,info.CurrentUsage);
        if(!i) {
            ShowWindow(state->hwnd,SW_HIDE);
            Check(state->graphicsReleaseQueued,"hide did not queue graphics retirement");
            ShowWindow(state->hwnd,SW_SHOWNOACTIVATE);
            drain(state->hwnd);
            Check(state->d3d.Get()==device && !state->graphicsSuspended,"late hide retired a visible tab");
            state->nativePresentationPending=true;
            ShowWindow(state->hwnd,SW_HIDE);
            SendMessageW(state->hwnd,kMsgRetireGraphics,state->lifetimeToken,0);
            Check(state->d3d.Get()==device,"active tab waiting for native geometry churned its GPU domain");
            state->nativePresentationPending=false;
            state->graphicsInitializing=true;
            ShowWindow(state->hwnd,SW_HIDE);
            SendMessageW(state->hwnd,kMsgRetireGraphics,state->lifetimeToken,0);
            Check(state->d3d.Get()==device,"nested initialization released live graphics");
            state->graphicsInitializing=false;
            {
                GraphicsContinuation finished(*state);
                SendMessageW(state->hwnd,kMsgRetireGraphics,state->lifetimeToken,0);
                Check(state->d3d.Get()==device,"nested owner scope released live graphics after its initialization flag cleared");
            }
        } else ShowWindow(state->hwnd,SW_HIDE);
        settle(*state);
        // Test-only completion fence: measure reclaimed resources after DWM
        // has processed the release transaction, without guessing a delay.
        Check(SUCCEEDED(state->composition->WaitForCommitCompletion()),"standby composition completion");
        DwmFlush();
        if(!i) memory("standby");
        if(!state->d3d || !state->target || state->modeRootAttached || GlyphCacheBytes(*state)>=kGlyphCacheBudget/8 ||
            state->graphicsSuspended || !state->visible || !state->inScope || state->selected!=0 ||
            state->entries[0].name!=L"retained private model")
            std::cout<<"GPU LIFECYCLE failure-tab="<<i<<" device="<<bool(state->d3d)<<" target="<<bool(state->target)
                <<" scene="<<state->modeRootAttached<<" glyphs="<<GlyphCacheBytes(*state)<<" suspended="<<state->graphicsSuspended
                <<" visible="<<state->visible<<" scope="<<state->inScope<<" selected="<<state->selected
                <<" ops="<<state->graphicsOperations<<" init="<<state->graphicsInitializing<<" paint="<<state->textRasterPainting
                <<" native-pending="<<state->nativePresentationPending<<" initial-pending="<<state->initialModePending
                <<" queued="<<state->graphicsReleaseQueued<<" window-visible="<<IsWindowVisible(state->hwnd)
                <<" active="<<TabOwnsActiveView(*state)<<std::endl;
        Check(state->d3d.Get()==device && state->swapChain.Get()==swapchain && state->textFormat.Get()==format &&
            state->target && !state->modeRootAttached && GlyphCacheBytes(*state)<kGlyphCacheBudget/8 &&
            !state->graphicsSuspended && state->visible && state->inScope && state->selected==0 &&
            state->entries[0].name==L"retained private model","hidden tab lost its warm image or retained offscreen cache/scene");
        Check(icon.bitmap.Get()==visibleIcon.Get() && !offscreen.bitmap && offscreen.pixels.Ready() &&
            state->textLayouts[0].layouts.at(state->entries[0].name).rasters.back().bitmap.Get()==glyph.Get(),
            "standby discarded visible glyph/icon textures or CPU icon data");
        Check(pixels(*state,glyph.Get())==glyphPixels && pixels(*state,visibleIcon.Get())==iconPixels,
            "composition recycling/driver trim changed visible glyph or icon pixels");
        // A stray WM_PAINT must not touch the ready hidden image.
        const auto hiddenPaint=state->graphicsPaintSerial;
        SendMessageW(state->hwnd,WM_PAINT,0,0);
        Check(state->d3d.Get()==device && state->graphicsPaintSerial==hiddenPaint && g_gpuDevices.load()==initialDevices+i+1,
            "hidden paint recreated or repainted a GPU domain");
        {
            ExclusiveSrwLock lock(g_frameLock);
            Check(std::none_of(g_activeFrames.begin(),g_activeFrames.end(),[&](const auto& f) { return f.hwnd==state->hwnd; }),
                "settled standby retained animation wakeups");
        }
        state->animationActive=false;
        tabs.push_back(std::move(state));
    }
    auto& resumed=*tabs.front();
    auto savedFormat=resumed.textFormat;
    auto savedDevice=resumed.d3d; auto savedSwapchain=resumed.swapChain;
    auto savedComposition=resumed.composition;
    auto savedGlyph=resumed.textLayouts[0].layouts.at(resumed.entries[0].name).rasters.back().bitmap;
    for(unsigned cycle=0;cycle<12;++cycle) {
        const auto before=resumed.graphicsPaintSerial;
        const auto cachedFrame=pixels(resumed,resumed.backbuffer.Get());
        ShowWindow(resumed.hwnd,SW_SHOWNOACTIVATE);
        Check(EnsureTarget(resumed) && resumed.textFormat.Get()==savedFormat.Get() && resumed.selected==0 &&
            resumed.d3d.Get()==savedDevice.Get() && resumed.swapChain.Get()==savedSwapchain.Get() &&
            resumed.graphicsPaintSerial==before && resumed.lastAcceptedPaint!=0,
            "tab return rebuilt graphics or required a repaint before its cached image was available");
        Check(pixels(resumed,resumed.backbuffer.Get())==cachedFrame,
            "cached tab frame changed before repaint on return");
        Paint(resumed);
        Check(resumed.textLayouts[0].layouts.at(resumed.entries[0].name).rasters.back().bitmap.Get()==savedGlyph.Get(),
            "tab return rebuilt the visible glyph texture");
        ShowWindow(resumed.hwnd,SW_HIDE); settle(resumed);
        Check(resumed.composition.Get()==savedComposition.Get(),"ordinary tab return recycled composition without an animation pool");
    }
    savedComposition.Reset();
    ShowWindow(resumed.hwnd,SW_SHOWNOACTIVATE);
    SetColumnViewMode(resumed,!resumed.columnsView);
    Check(Eventually([&] { Paint(resumed); return resumed.modeRootAttached; }),"replacement-failure scene setup");
    const auto beforeFailure=resumed.composition.Get();
    AuditFixture::failComposition=true;
    ShowWindow(resumed.hwnd,SW_HIDE); settle(resumed);
    AuditFixture::failComposition=false;
    Check(resumed.graphicsReady && resumed.composition.Get()==beforeFailure && resumed.compositionSurfacePoolUsed &&
        resumed.d3d.Get()==savedDevice.Get() && resumed.swapChain.Get()==savedSwapchain.Get(),
        "failed pool replacement destroyed a usable warm tab");
    ShowWindow(resumed.hwnd,SW_SHOWNOACTIVATE);
    Check(EnsureTarget(resumed),"warm return after failed pool replacement");
    Paint(resumed); // Active paint resumes the warm lifetime, like production activation.
    ShowWindow(resumed.hwnd,SW_HIDE); settle(resumed);
    Check(resumed.graphicsReady && !resumed.compositionSurfacePoolUsed,"later hide did not retry pool retirement");
    ShowWindow(resumed.hwnd,SW_SHOWNOACTIVATE); Paint(resumed);
    SetColumnViewMode(resumed,!resumed.columnsView);
    Check(Eventually([&] { Paint(resumed); return resumed.modeRootAttached; }),"nested-activation scene setup");
    const auto beforeActivation=resumed.composition.Get();
    bool nestedActivation=false;
    AuditFixture::compositionProbe=[&] {
        nestedActivation=true;
        // The same activation intent consumed by PresentCachedTabSurface;
        // this private fixture has no Shell host to position over.
        resumed.graphicsStandby=resumed.graphicsStandbyPending=false;
        ShowWindow(resumed.hwnd,SW_SHOWNOACTIVATE);
    };
    ShowWindow(resumed.hwnd,SW_HIDE); drain(resumed.hwnd);
    Check(nestedActivation && !AuditFixture::compositionProbe && resumed.graphicsReady &&
        resumed.composition.Get()==beforeActivation && IsWindowVisible(resumed.hwnd) && !resumed.graphicsStandby,
        "nested activation lost its already resumed composition target");
    Paint(resumed); ShowWindow(resumed.hwnd,SW_HIDE); settle(resumed);
    Check(!resumed.compositionSurfacePoolUsed,"pool retirement after nested activation");
    DXGI_QUERY_VIDEO_MEMORY_INFO final{};
    if(adapter) adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&final);
    std::cout<<"GPU LIFECYCLE tabs=20 peak-local-MiB="<<peak/1048576.
        <<" standby-local-MiB="<<final.CurrentUsage/1048576.<<" warm-devices="<<g_gpuDevices.load()-initialDevices<<std::endl;
    Check(g_gpuDevices.load()==initialDevices+20,"standby did not preserve tab-owned graphics domains");
    Check(!adapter || final.CurrentUsage<=2688ULL*1024*1024,
        "20 warm 4K tabs retained oversized scene/driver allocations");
    savedGlyph.Reset(); savedSwapchain.Reset(); savedDevice.Reset(); savedComposition.Reset(); savedFormat.Reset();
    for(auto& state : tabs) {
        DropTarget(*state);
        SetWindowLongPtrW(state->hwnd,GWLP_USERDATA,0);
        DestroyWindow(state->hwnd); Stop(state->background);
    }
    tabs.clear(); DestroyWindow(frame); UnregisterClassW(cls.lpszClassName,cls.hInstance);
    g_writeFactory.Reset();
    g_gpuMemoryLogging=false;
    Check(g_gpuDevices.load()==initialDevices,"closed tabs retained graphics domains");
    DXGI_QUERY_VIDEO_MEMORY_INFO closed{};
    if(adapter) adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&closed);
    std::cout<<"GPU LIFECYCLE closed-local-MiB="<<closed.CurrentUsage/1048576.<<std::endl;
    Check(!adapter || closed.CurrentUsage<=256ULL*1024*1024,"closed warm tabs retained GPU allocations");
    std::cout<<"PASS: 20 warm 4K tab images preserve glyph/icon/device identities; bounded offscreen caches, scene retirement, repeated instant return and full close cleanup\n";
}
