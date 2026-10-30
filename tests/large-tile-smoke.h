// Private GPU surfaces and readiness faults; no live Explorer or desktop input.
static void TestLargeTileBackpressure() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"large tile fonts");
    {
        PresentationFixture::Tab tab(L"large tile backpressure",12,false,true,false,2560,1440);
        auto& state=tab.state;
        Check(EnsureTarget(state) && state.frameLatencyWaitable,"missing waitable swap chain");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        ComPtr<IDXGISwapChain2> paced; UINT latency=0;
        Check(SUCCEEDED(state.swapChain->GetDesc1(&desc)) &&
            (desc.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) &&
            SUCCEEDED(state.swapChain.As(&paced)) && SUCCEEDED(paced->GetMaximumFrameLatency(&latency)) &&
            latency==1,"swap chain queues stale frames");
        state.presentationOnly=true; state.presentationActive=true;
        state.presentationClient={0,0,2560,1440}; state.presentationDpi=96;
        state.rowHeight96[1]=1600; UpdateMetrics(state); UpdateColumns(state);
        Check(state.itemsPerRow==1 && state.iconPx>2000,"large tile fixture did not reach one item per row");
        state.graphicsPaintSerial=state.lastAcceptedPaint=10;
        state.presentationContentAccepted=true;
        AuditFixture::busyFrame=state.frameLatencyWaitable; AuditFixture::busyFrameProbes=0;
        for(int i=0;i<1000;++i) {
            state.hovered=i%12; // New input must stay current without drawing rejected frames.
            Paint(state);
        }
        Check(AuditFixture::busyFrameProbes==1000 && state.graphicsPaintSerial==10 &&
            state.lastAcceptedPaint==10 && state.presentPending && !state.presentationContentAccepted &&
            state.hovered==3,"busy GPU redrew large tiles or discarded current input");
        auto lane=std::make_shared<PresentationLane>();
        PublishPresentationReceipt(state,lane);
        Check(lane->receipt && !lane->receipt->accepted && !lane->receipt->content,
            "deferred revision claimed the preceding frame's acceptance");
        LARGE_INTEGER now{},frequency{}; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&frequency);
        state.rejectedPresentQpc.QuadPart=now.QuadPart+frequency.QuadPart;
        Check(!PresentationFrameReady(state),"rejected Present bypassed the retry cadence");
        state.rejectedPresentQpc.QuadPart=now.QuadPart-frequency.QuadPart;
        Check(PresentationFrameReady(state),"rejected Present stranded an acquired DXGI slot");
        state.rejectedPresentQpc={}; AuditFixture::busyFrame=nullptr;
        Check(Eventually([&] {Paint(state); return state.lastAcceptedPaint>10 && !state.presentPending;}),
            "ready GPU did not resume the latest large tile frame");
        Check(ResizeTarget(state,1920,1080),"waitable swap chain resize lost its creation flags");
        const HANDLE handle=state.frameLatencyWaitable;
        DropTarget(state);
        DWORD flags=0;
        Check(!state.frameLatencyWaitable && !GetHandleInformation(handle,&flags),
            "device teardown leaked the frame latency handle");
    }
    g_writeFactory.Reset();
    const HANDLE abandoned=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    Check(abandoned!=nullptr,"destructor handle fixture");
    {ViewState state; state.frameLatencyWaitable=abandoned;}
    DWORD flags=0;
    Check(!GetHandleInformation(abandoned,&flags),"view destruction leaked its readiness handle");
    {
        ViewState broken; broken.graphicsReady=true;
        const HANDLE closed=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        Check(closed && CloseHandle(closed),"failed readiness handle fixture");
        broken.frameLatencyWaitable=closed; // -1 is a valid current-process pseudo-handle for waits.
        Check(!PresentationFrameReady(broken) && !broken.graphicsReady && !broken.frameLatencyWaitable &&
            FAILED(broken.graphicsFailure),"failed readiness wait stranded GPU ownership instead of entering recovery");
    }
    // Exercise the real render worker while the owner deliberately does not
    // pump its receipt message. Busy wakes cannot replace a successful receipt
    // for unchanged input; a newer, not-yet-drawn revision must remain deferred.
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"busy worker fonts");
    {
        PresentationFixture::Tab tab(L"busy render owner",12,false,true,false,2560,1440);
        FocusFixture::Session focus(tab.frame,tab.state.hwnd);
        tab.state.rowHeight96[1]=1600; UpdateMetrics(tab.state); UpdateColumns(tab.state);
        tab.Start();
        tab.Await([](const auto& r) {return r.accepted && !r.opacity;});
        Check(Eventually([&] {return !tab.state.presenter->animating.load();}),"large tile worker did not settle");
        const auto accepted=tab.Receipt();
        AuditFixture::busySharedProbes=0;
        AuditFixture::busyPresentationThread=accepted->thread;
        struct ClearBusy {~ClearBusy() {AuditFixture::busyPresentationThread=0;}} clear;
        SetEvent(tab.state.presenter->wake);
        // Fence the idle wake through the real generation-control path. An
        // unchanged tab must not enter DXGI just because a late pulse arrived.
        ++tab.state.directoryToken; SynchronizePresentationGeneration(tab.state);
        const auto idle=tab.Await([&](const auto& r) {return r.directoryToken==tab.state.directoryToken;});
        Check(!AuditFixture::busySharedProbes.load() && idle->accepted && idle->frames==accepted->frames,
            "idle heartbeat probed the GPU or lost the accepted pixels");
        tab.state.hovered=5; SubmitPresentation(tab.state);
        const auto sequence=tab.state.presentationSequence;
        const auto deferred=tab.Await([&](const auto& r) {return r.sequence==sequence && !r.accepted;});
        Check(Eventually([] {return AuditFixture::busySharedProbes.load()>10;}),"changed model did not retry GPU readiness");
        Check(deferred->frames==accepted->frames && !deferred->content,"new busy revision claimed stale pixels");
        AuditFixture::busyPresentationThread=0; SetEvent(tab.state.presenter->wake);
        tab.Await([&](const auto& r) {return r.sequence==sequence && r.accepted && r.frames>accepted->frames;});
    }
    ShutDownFrames(); g_frameStopping=false; g_writeFactory.Reset();
    std::cout<<"PASS: 1,000 busy wakes at one >2K icon per row perform no draw/upload; latest input, receipt acceptance, retry cadence, waitable resize and handle teardown\n";
}

static void TestLargeTileSampling() {
    Check(IconInterpolation(256,256,256,256,0)==D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR &&
        IconInterpolation(96,96,256,256,0)==D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC &&
        IconInterpolation(384,384,256,256,0)==D2D1_INTERPOLATION_MODE_LINEAR &&
        IconInterpolation(2048,2048,256,256,0)==D2D1_INTERPOLATION_MODE_LINEAR &&
        IconInterpolation(2048,1024,256,128,5)==D2D1_INTERPOLATION_MODE_LINEAR &&
        IconInterpolation(2048,1024,4096,2048,0)==D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,
        "large magnification uses cubic or downscaling lost its prefilter");
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"large sampling fonts");
    {
        PresentationFixture::Tab tab(L"large sampling",12,false,true,false,2560,1440);
        auto& state=tab.state; Check(EnsureTarget(state),"large sampling target");
        ComPtr<ID2D1Image> previous; state.target->GetTarget(&previous);
        auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> surface;
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(2560,1440),nullptr,0,&props,&surface)),
            "large sampling surface");
        state.target->SetTarget(surface.Get()); state.paintWidth=2560;
        IconRecord icon; icon.pixels.width=icon.pixels.height=256; icon.pixels.stride=1024;
        icon.pixels.data.resize(256*1024);
        for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) {
            auto* p=icon.pixels.data.data()+y*1024+x*4;
            p[0]=BYTE(x/2); p[1]=BYTE(y/2); p[2]=100; p[3]=128;
        }
        Check(UploadIconBitmap(state,icon),"large sampling upload");
        ComPtr<ID3D11DeviceContext> context; state.d3d->GetImmediateContext(&context);
        ComPtr<ID3D11Query> disjoint,start,end;
        D3D11_QUERY_DESC query{D3D11_QUERY_TIMESTAMP_DISJOINT,0};
        Check(SUCCEEDED(state.d3d->CreateQuery(&query,&disjoint)),"GPU frequency query");
        query.Query=D3D11_QUERY_TIMESTAMP;
        Check(SUCCEEDED(state.d3d->CreateQuery(&query,&start)) &&
            SUCCEEDED(state.d3d->CreateQuery(&query,&end)),"GPU timestamp queries");
        for(float size:{512.f,2048.f}) for(bool oldFilter:{true,false}) {
            std::vector<double> times;
            for(int frame=0;frame<14;++frame) {
                context->Begin(disjoint.Get()); context->End(start.Get());
                state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
                state.iconPx=size;
                if(oldFilter) {
                    const auto rect=D2D1::RectF(0,0,size,size),source=D2D1::RectF(0,0,256,256);
                    state.target->DrawBitmap(icon.bitmap.Get(),&rect,1,
                        D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC,&source,nullptr);
                } else Check(DrawCachedIcon(state,icon,0,0),"large shared icon draw");
                Check(SUCCEEDED(state.target->EndDraw()),"large sampling draw");
                context->End(end.Get()); context->End(disjoint.Get()); context->Flush();
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT clock{}; UINT64 a=0,b=0;
                Check(Eventually([&] {return context->GetData(disjoint.Get(),&clock,sizeof(clock),0)==S_OK &&
                    context->GetData(start.Get(),&a,sizeof(a),0)==S_OK &&
                    context->GetData(end.Get(),&b,sizeof(b),0)==S_OK;}),"GPU timing completion");
                if(frame>=4 && !clock.Disjoint && clock.Frequency && b>=a)
                    times.push_back(1000.*double(b-a)/clock.Frequency);
            }
            Check(!times.empty(),"no valid GPU timing samples"); std::sort(times.begin(),times.end());
            std::cout<<"PROFILE: cached icon side="<<size<<" filter="<<(oldFilter ? "previous-cubic" : "adaptive")
                <<" GPU median="<<times[times.size()/2]<<" p95="<<times.back()
                <<" ms (offscreen timestamps; includes clear, no scan-out)\n";
        }
        // Real pixel readback: the shared route must keep Cut alpha, aspect,
        // transform restoration and RTL positioning while using the fast filter.
        props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        ComPtr<ID2D1Bitmap1> cpu;
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(2560,1440),nullptr,0,&props,&cpu)),"large readback");
        for(bool rtl:{false,true}) {
            state.rtlLayout=rtl; state.iconPx=1024;
            const auto transform=D2D1::Matrix3x2F::Translation(3,4);
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f)); state.target->SetTransform(transform);
            Check(DrawCachedIcon(state,icon,100,50,.5f,1.5f),"large rotated Cut icon");
            D2D1_MATRIX_3X2_F after{}; state.target->GetTransform(&after);
            Check(!memcmp(&after,&transform,sizeof(after)),"large icon leaked its rotation");
            Check(SUCCEEDED(state.target->EndDraw()) && SUCCEEDED(cpu->CopyFromBitmap(nullptr,surface.Get(),nullptr)),
                "large Cut copy");
            D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"large Cut map");
            const unsigned x=rtl ? 1951 : 615;
            const auto* p=map.bits+566*map.pitch+x*4;
            const bool correct=p[2]>=49 && p[2]<=51 && p[3]>=63 && p[3]<=65;
            cpu->Unmap(); Check(correct,"large RTL/Cut icon lost opacity or position");
        }
        state.target->SetTransform(D2D1::Matrix3x2F::Identity()); state.target->SetTarget(previous.Get());
        DropTarget(state);
    }
    g_writeFactory.Reset();
    std::cout<<"PASS: large icon magnification, native pixel snapping, cubic minification, RTL/Cut readback and restored rotation\n";
}
