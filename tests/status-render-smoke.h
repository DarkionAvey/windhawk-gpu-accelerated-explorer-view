static void TestStatusRender(HWND frame) {
    ViewState state;
    state.hwnd=CreateWindowExW(0,L"STATIC",L"status-render",WS_CHILD,0,0,200,100,frame,
        nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.hwnd!=nullptr,"status render window");
    state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
    state.visible=state.inScope=state.detailsViewActive=true;
    Check(EnsureTarget(state),"status GPU target");
    wchar_t system[MAX_PATH]{};
    Check(GetSystemDirectoryW(system,ARRAYSIZE(system))!=0,"system icon directory");
    StatusIconCache cache;
    const auto icons=DecodeStatusIcons({std::wstring(system)+L"\\shell32.dll,-14"},state.hwnd,cache);
    Check(icons && icons->size()==1,"CloudMirror sample icon");
    std::vector<IconPixels> row{icons->front(),icons->front()};
    ComPtr<ID2D1Image> original; state.target->GetTarget(&original);
    ComPtr<ID2D1Bitmap1> surface,cpu;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(160,80),nullptr,0,&props,&surface)),"status surface");
    props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(160,80),nullptr,0,&props,&cpu)),"status readback");
    state.target->SetTarget(surface.Get());
    const auto render=[&](float right,float opacity,float top=8,float bottom=72) {
        state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
        Check(DrawStatusIcons(state,row,16,top,right,bottom,opacity),"draw status glyphs");
        Check(SUCCEEDED(state.target->EndDraw()) && SUCCEEDED(cpu->CopyFromBitmap(nullptr,surface.Get(),nullptr)),
            "status render completion");
        D2D1_MAPPED_RECT map{};
        Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"status pixels");
        std::vector<BYTE> result(160*80*4);
        for(size_t y=0;y<80;++y) memcpy(result.data()+y*160*4,map.bits+y*map.pitch,160*4);
        cpu->Unmap(); return result;
    };
    const auto alpha=[](const auto& pixels) {
        size_t total=0; for(size_t i=3;i<pixels.size();i+=4) total+=pixels[i]; return total;
    };
    for(bool rtl:{false,true}) {
        state.rtlLayout=rtl;
        const auto full=render(144,1.f),faded=render(144,.5f);
        Check(alpha(full)>0 && alpha(faded)>0 && alpha(faded)<alpha(full),"status transition opacity");
        const float side=float(GetSystemMetricsForDpi(SM_CXSMICON,ViewDpi(state)));
        const auto narrow=render(16+side,1.f);
        // Exactly one complete glyph survives; there is no partial second icon.
        Check(alpha(narrow)*2==alpha(full),"status row clipped or overflowed a glyph");
        Check(alpha(render(16+side-1,1.f))==0,"undersized column drew a partial icon");
        for(size_t y=0;y<80;++y) for(size_t x=0;x<160;++x)
            if(x<16 || x>=16+size_t(side))
                Check(narrow[(y*160+x)*4+3]==0,"status pixels escaped the column");
        size_t left=0,right=0;
        for(size_t y=0;y<80;++y) for(size_t x=0;x<160;++x)
            (x<80 ? left:right)+=full[(y*160+x)*4+3];
        Check(rtl ? right>left : left>right,"status logical leading edge ignored RTL");
    }
    Check(state.statusIconBitmaps.size()==1,"identical shared glyphs uploaded repeatedly");
    // Let Windows create a real oversized icon, then exercise both the bounded
    // worker decoder and the renderer with its original oversized pixels.
    ComPtr<IWICImagingFactory> factory;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))),"oversized icon WIC factory");
    HICON giant=static_cast<HICON>(CopyImage(LoadIconW(nullptr,IDI_INFORMATION),IMAGE_ICON,512,512,0));
    Check(giant!=nullptr,"Windows oversized icon");
    const UINT small=UINT(GetSystemMetricsForDpi(SM_CXSMICON,ViewDpi(state)));
    const auto bounded=DecodeShellIcon(factory.Get(),giant,nullptr,small);
    const auto originalPixels=DecodeShellIcon(factory.Get(),giant);
    DestroyIcon(giant);
    Check(bounded.Ready() && bounded.width<=small && bounded.height<=small &&
        originalPixels.Ready() && originalPixels.width==512 && originalPixels.height==512,
        "oversized status decode was not bounded before allocation");
    row={originalPixels};
    for(bool rtl:{false,true}) {
        state.rtlLayout=rtl;
        const auto clamped=render(16+float(small),1.f,8,8+float(small)/2);
        Check(alpha(clamped)>0,"oversized status icon disappeared in a short row");
        for(size_t y=0;y<80;++y) for(size_t x=0;x<160;++x)
            if(x<16 || x>=16+small || y<8 || y>=8+small/2)
                Check(clamped[(y*160+x)*4+3]==0,"oversized status icon escaped its cell");
        Check(alpha(render(16,1.f))==0,"zero-width status cell painted pixels");
    }
    state.target->SetTarget(original.Get()); surface.Reset(); cpu.Reset();
    DropTarget(state);
    Check(state.statusIconBitmaps.empty(),"device teardown retained status textures");
    DestroyWindow(state.hwnd);
    std::cout<<"PASS: offscreen status pixels, opacity, RTL, whole-glyph overflow and device teardown\n";
}
