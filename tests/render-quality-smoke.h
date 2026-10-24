// Real GPU/GDI readback on private windows; no desktop capture or font-setting changes.
static void TestRenderQuality() {
    const auto oldMode=g_textAntialiasing.load();
    struct RestoreMode {TextAntialiasing mode; ~RestoreMode() {g_textAntialiasing=mode; ++g_textRenderingRevision;}} restore{oldMode};
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"quality fonts");
    ViewState state;
    HWND frame=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"quality fixture",
        WS_POPUP,-32000,-32000,360,160,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.hwnd=CreateWindowExW(0,L"STATIC",L"quality view",WS_CHILD,0,0,360,160,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
    state.visible=state.inScope=state.detailsViewActive=true; state.fontPx=17; state.rowPx=34;
    Check(frame && state.hwnd && EnsureTarget(state),"quality target");
    ComPtr<ID2D1Image> previous; state.target->GetTarget(&previous);
    ComPtr<ID2D1Bitmap1> surface,cpu;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(360,160),nullptr,0,&props,&surface)),"quality surface");
    props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(360,160),nullptr,0,&props,&cpu)),"quality readback");
    state.target->SetTarget(surface.Get()); state.paintWidth=360;
    const auto read=[&] {
        Check(SUCCEEDED(state.target->EndDraw()) && SUCCEEDED(cpu->CopyFromBitmap(nullptr,surface.Get(),nullptr)),"quality draw/copy");
        D2D1_MAPPED_RECT mapped{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&mapped)),"quality map");
        std::vector<DWORD> pixels(360*160);
        for(size_t y=0;y<160;++y) memcpy(pixels.data()+y*360,mapped.bits+y*mapped.pitch,360*4);
        cpu->Unmap(); return pixels;
    };
    const std::wstring label=L"Explorer Aa 123 flower الملف.txt";
    std::vector<std::vector<DWORD>> samples;
    const bool macType=GetModuleHandleW(L"MacType64.dll") || GetModuleHandleW(L"MacType64.Core.dll");
    bool customizerOverrides=false;
    for(auto mode:{TextAntialiasing::System,TextAntialiasing::Smooth,TextAntialiasing::Aliased}) {
        g_textAntialiasing=mode; RefreshTextRendering(state,true);
        for(int repeat=0;repeat<2;++repeat) {
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
            Text(state,label,12.2f,15.3f,348.2f,70.3f,0xFFFFFF,false,.6f);
            const auto pixels=read();
            ComPtr<IDWriteRenderingParams> effective;
            state.target->GetTextRenderingParams(&effective);
            const bool overridden=state.target->GetTextAntialiasMode()!=state.textAntialiasMode ||
                effective.Get()!=state.textRenderingParams.Get();
            if(mode!=TextAntialiasing::System && overridden) {
                Check(macType,"unexplained native text policy override");
                customizerOverrides=true;
            }
            Check(state.target->GetTextAntialiasMode()==state.textRasterTarget->GetTextAntialiasMode(),
                "native font customizer applies different policies to direct and cached text");
            size_t ink=0,partial=0;
            for(DWORD pixel:pixels) {
                const unsigned alpha=pixel>>24;
                ink+=alpha!=0; partial+=alpha>1 && alpha<151;
                Check(((pixel>>16)&255)==((pixel>>8)&255) && ((pixel>>8)&255)==(pixel&255),
                    "transparent grayscale text contains RGB fringes");
            }
            Check(ink>100 && pixels.front()==0 && pixels.back()==0,"text filled its transparent backdrop or disappeared");
            if(mode==TextAntialiasing::Aliased && state.target->GetTextAntialiasMode()==D2D1_TEXT_ANTIALIAS_MODE_ALIASED)
                Check(partial==0,"aliased choice still antialiased glyph edges");
            if(state.target->GetTextAntialiasMode()==D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE ||
                mode==TextAntialiasing::Smooth)
                Check(partial>50,"grayscale choice lost antialiasing");
            if(repeat==0) samples.push_back(pixels);
            else Check(samples.back()==pixels,"warm glyph cache changed selected antialiasing");
        }
        const auto& variants=state.textLayouts[0].layouts.at(label).rasters;
        Check(std::any_of(variants.begin(),variants.end(),[&](const auto& raster) {
            return raster.rendering==state.textRenderingSerial;
        }),"raster cache did not include the rendering revision");
    }
    if(!customizerOverrides) Check(samples.size()==3 && samples[1]!=samples[2],"text choices did not change real glyph pixels");
    else std::cout<<"PROFILE: active MacType overrides native text choices; direct/cache effective policies and pixels remain consistent\n";
    g_textAntialiasing=TextAntialiasing::System; RefreshTextRendering(state,true);
    ComPtr<IDWriteRenderingParams> native;
    Check(SUCCEEDED(g_writeFactory->CreateMonitorRenderingParams(state.textRenderingMonitor,&native)) &&
        state.textRenderingParams && native->GetGamma()==state.textRenderingParams->GetGamma() &&
        native->GetEnhancedContrast()==state.textRenderingParams->GetEnhancedContrast(),"Windows mode replaced native monitor tuning");

    // Exact 1:1 texels stay exact through fractional placement/source transforms.
    IconRecord icon; std::array<DWORD,64> texels{};
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) texels[y*8+x]=((x+y)&1) ? 0xFFFFFFFF : 0xFF000000;
    const auto imageProps=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(8,8),texels.data(),32,&imageProps,&icon.bitmap)),"quality icon");
    state.iconPx=8;
    for(bool rtl:{false,true}) for(bool translated:{false,true}) {
        state.rtlLayout=rtl;
        const float x=translated ? .35f : 0.f,y=translated ? .45f : 0.f;
        state.target->SetTransform(D2D1::Matrix3x2F::Translation(x,y));
        state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
        Check(DrawCachedIcon(state,icon,20.2f,20.3f),"quality icon drawing");
        const auto pixels=read();
        const int left=int(std::lround((rtl ? 360-20.2f-8 : 20.2f)+x)),top=int(std::lround(20.3f+y));
        size_t ink=0;
        for(int row=0;row<160;++row) for(int column=0;column<360;++column) {
            const DWORD pixel=pixels[size_t(row)*360+column];
            if(column>=left && column<left+8 && row>=top && row<top+8) {
                Check(pixel==texels[size_t(row-top)*8+column-left],"fractional icon placement blurred a texel"); ++ink;
            } else Check(pixel==0,"icon snapping leaked fractional edge coverage");
        }
        Check(ink==64,"snapped icon changed its physical size");
    }
    state.rtlLayout=false;
    for(float dpi:{96.f,120.f,144.f,192.f}) {
        state.target->SetDpi(dpi,dpi);
        const auto point=SnapDrawPoint(state.target.Get(),D2D1::Point2F(17.3f,29.7f));
        Check(fabsf((point.x+.35f)*dpi/96.f-roundf((point.x+.35f)*dpi/96.f))<.0001f &&
            fabsf((point.y+.45f)*dpi/96.f-roundf((point.y+.45f)*dpi/96.f))<.0001f,"snapping ignored transform/DPI");
    }
    state.target->SetDpi(96,96); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
    for(bool grid:{false,true}) for(float size:{19.f,33.f,72.f,130.f}) {
        state.gridView=grid; state.iconPx=size;
        Entry entry; entry.path=L"private-quality:icon"; entry.name=L"icon.ext";
        std::wstring path; bool attributes=false;
        const auto key=IconKey(state,entry,path,attributes);
        Check(IconRequestSize(state)>=size && (size<=32 || key.starts_with(L"@")),
            "DPI/zoom icon source stayed in the 32-pixel tier");
    }
    {
        ViewState invisible; Entry entry; entry.name=L"invisible"; entry.path=L"private-quality:invisible";
        for(float opacity:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN()}) {
            Fill(invisible,0,0,50,50,0xFFFFFF,opacity);
            Text(invisible,L"must not shape",0,0,200,30,0xFFFFFF,false,opacity);
            DrawActualIcon(invisible,entry,0,0,opacity);
            Check(DrawCachedIcon(invisible,icon,0,0,opacity),"zero-opacity icon was treated as an unresolved provider");
        }
        Check(!invisible.textRenderingSerial && invisible.icons.empty() && !invisible.iconTick &&
            invisible.textLayouts[0].layouts.empty(),"invisible primitives resolved fonts/icons or built caches");
    }
    state.target->SetTarget(previous.Get());
    DropTarget(state); DestroyWindow(state.hwnd); DestroyWindow(frame);
    std::cout<<"PASS: native monitor tuning, transparent smooth/aliased glyph pixels and warm masks; exact icon texels, LTR/RTL/transform/DPI snapping, resolution tiers and zero-opacity work elision\n";
}

static void TestToggleRepaint() {
    ViewState state;
    HWND frame=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"toggle backdrop",
        WS_POPUP,-32000,-32000,160,120,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.viewToggle=CreateWindowExW(0,L"BUTTON",L"",WS_CHILD|WS_CLIPSIBLINGS,17,23,32,32,frame,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.viewToggleBackground=RGB(20,230,50); state.viewToggleForeground=RGB(10,10,10); state.viewToggleDpi=96;
    unsigned parentPaints=0;
    SUBCLASSPROC backdrop=[](HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data)->LRESULT {
        if(msg==WM_ERASEBKGND || msg==WM_PRINTCLIENT) {
            ++*reinterpret_cast<unsigned*>(data);
            // Model Explorer's frame acknowledging a request without painting.
            return 1;
        }
        return DefSubclassProc(hwnd,msg,wp,lp);
    };
    Check(frame && state.viewToggle && SetWindowSubclass(frame,backdrop,230,reinterpret_cast<DWORD_PTR>(&parentPaints)),"toggle backdrop fixture");
    state.viewToggleBufferedPaint=SUCCEEDED(BufferedPaintInit());
    BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),32,-32,1,32,BI_RGB};
    void* bits=nullptr; HDC dc=CreateCompatibleDC(nullptr);
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    Check(dc && bitmap,"toggle backdrop readback"); const auto old=SelectObject(dc,bitmap);
    const auto paint=[&](bool hot,UINT flags=0) {
        state.viewToggleHot=hot;
        DRAWITEMSTRUCT item{}; item.CtlType=ODT_BUTTON; item.hwndItem=state.viewToggle;
        item.hDC=dc; item.rcItem={0,0,32,32}; item.itemState=flags; DrawViewToggle(state,item); GdiFlush();
        return std::vector<DWORD>(static_cast<DWORD*>(bits),static_cast<DWORD*>(bits)+32*32);
    };
    const bool buffered=state.viewToggleBufferedPaint;
    for(bool useBuffer:{true,false}) {
        state.viewToggleBufferedPaint=buffered && useBuffer;
        memset(bits,0xCD,32*32*4);
        const auto idle=paint(false);
        Check(std::all_of(idle.begin(),idle.end(),[](DWORD p){return (p&0xFFFFFF)==0x14E632u;}),
            "first toggle paint left undefined pixels when the parent did not paint");
        for(int repeat=0;repeat<32;++repeat) {
            const auto hot=paint(true),pressed=paint(true,ODS_SELECTED);
            Check(hot!=idle && pressed!=hot && paint(false)==idle,"toggle hover/press pixels survived a full repaint");
        }
    }
    state.viewToggleBufferedPaint=buffered;
    Check(!parentPaints,"toggle still relies on Explorer's frame repainting status chrome");
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
    state.ReleaseViewToggle(); RemoveWindowSubclass(frame,backdrop,230); DestroyWindow(frame);
    std::cout<<"PASS: complete toggle background on first paint and successive hover/press/idle paints, with buffered painting and direct GDI fallback; no parent-paint dependency\n";
}
