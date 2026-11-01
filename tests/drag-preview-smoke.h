// Offscreen GPU component checks plus actual tab-owned Shell image workers.
// No desktop input, clipboard/focus changes, or fabricated Windows messages.
static void TestDragPreview(const std::wstring& fixture) {
    const auto fonts=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"drag preview fonts");
    {
        PresentationFixture::Tab tab(L"drag preview",1,false,true);
        auto& state=tab.state;
        state.entries[0].path=fixture; state.entries[0].name=L"private fixture";
        state.iconPx=32; state.gridView=true;
        RequestVisibleIcons(state);
        IconPixels shown;
        Check(Eventually([&] {
            ReceiveIcons(state,false);
            std::wstring path; bool attrs=false;
            auto* icon=FindIconRecord(state,IconKey(state,state.entries[0],path,attrs),true);
            if(icon && icon->pixels.Ready()) shown=icon->pixels;
            return shown.Ready();
        }),"drag preview initial Shell artwork");
        state.dragPreview=std::make_shared<DragPreview>();
        state.dragPreview->pixels=shown; state.dragPreview->side=96;
        state.dragPreviewEntry=state.entries[0]; state.dragPreviewEpoch=state.mouseFolderEpoch; state.dragPreviewSize=96;
        RequestVisibleIcons(state);
        Check(state.dragPreview->pixels.data.Identity()==shown.data.Identity(),"drag request discarded immediate displayed pixels");
        Check(Eventually([&] {ReceiveIcons(state,false); return state.dragPreview->pixels.width>=96;}),
            "existing image workers did not enrich the drag preview");
        const auto sharp=state.dragPreview->pixels;
        state.dragPreview.reset();
        Check(EnsureTarget(state),"drag preview GPU target");
        {
            // Exercise the real layered-window/DComp contract offscreen. The
            // popup uses the already-created tab device and has no callback.
            auto preview=std::make_shared<DragPreview>(); preview->pixels=sharp;
            preview->side=64; preview->background=RGB(32,32,32); preview->foreground=RGB(245,245,245);
            preview->token=ULONG_PTR(g_nextToken.fetch_add(1));
            preview->window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP,
                L"STATIC",L"private drag preview",WS_POPUP,-32000,-32000,128,128,tab.frame,nullptr,GetModuleHandleW(nullptr),nullptr);
            Check(preview->window && SetLayeredWindowAttributes(preview->window,0,255,LWA_ALPHA) &&
                SetPropW(preview->window,L"WindhawkGpuDragPreview",reinterpret_cast<HANDLE>(preview->token)),"private drag popup");
            const auto device=state.d3d.Get();
            Check(RenderDragPreview(state,preview) && preview->rendered->load() && state.dragComposition.visual &&
                state.dragComposition.curve && state.d3d.Get()==device,"drag preview did not reuse tab GPU/compositor animation");
            const auto artwork=state.dragComposition.artwork.Get(); preview->effect=DROPEFFECT_COPY;
            Check(artwork && RenderDragPreview(state,preview) && state.dragComposition.artwork.Get()==artwork,
                "effect feedback uploaded the same artwork again");
            Check(RenderDragPreview(state,nullptr) && !state.dragComposition.target && !state.dragComposition.plane.bitmap,
                "drag preview resources survived cancellation");
            ++preview->token;
            Check(RenderDragPreview(state,preview) && !state.dragComposition.visual,"stale HWND identity rendered another drag");
            DestroyWindow(preview->window);
        }
        ComPtr<ID2D1Image> previous; state.target->GetTarget(&previous);
        ComPtr<ID2D1Bitmap1> surface;
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(720,480),nullptr,0,&properties,&surface)),"drag preview sheet");
        state.target->SetTarget(surface.Get()); state.target->BeginDraw();
        state.target->Clear(D2D1::ColorF(0,0.f));
        for(int row=0;row<4;++row) for(int column=0;column<4;++column) {
            DragPreview preview; preview.side=64; preview.dpi=96; preview.pixels=sharp;
            preview.background=row&1 ? RGB(32,32,32):RGB(248,248,248);
            preview.foreground=row&1 ? RGB(245,245,245):RGB(24,24,24);
            preview.accent=RGB(0,100,200); preview.rtl=row>=2; preview.count=column==3 ? 1200:column+1;
            preview.effect=column==0 ? DROPEFFECT_NONE:column==1 ? DROPEFFECT_COPY:column==2 ? DROPEFFECT_MOVE:DROPEFFECT_LINK;
            state.target->SetTransform(D2D1::Matrix3x2F::Translation(float(column*180),float(row*120)));
            DrawDragPreview(state.target.Get(),preview);
        }
        Check(SUCCEEDED(state.target->EndDraw()),"drag preview color emoji/theme draw");
        auto sheet=IconAlphaFixture::Read(state,surface.Get());
        ComPtr<ID2D1SolidColorBrush> glyphBrush;
        Check(SUCCEEDED(state.target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&glyphBrush)),"drag glyph brush");
        for(const auto* emoji:{L"\U0001F6AB",L"\U0001F4CB",L"\u27A1\uFE0F",L"\u2B05\uFE0F",L"\U0001F517"}) {
            state.target->SetTransform(D2D1::Matrix3x2F::Identity());
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
            DrawCenteredDragLabel(state.target.Get(),glyphBrush.Get(),emoji,L"Segoe UI Emoji",48,
                D2D1::RectF(40,40,104,104),RGB(255,255,255),true);
            Check(SUCCEEDED(state.target->EndDraw()),"centered operation emoji draw");
            const auto glyph=IconAlphaFixture::Read(state,surface.Get());
            UINT left=glyph.width,top=glyph.height,right=0,bottom=0;
            for(UINT y=0;y<glyph.height;++y) for(UINT x=0;x<glyph.width;++x)
                if(glyph.data[size_t(y)*glyph.stride+x*4+3]>16) {
                    left=std::min(left,x); right=std::max(right,x+1); top=std::min(top,y); bottom=std::max(bottom,y+1);
                }
            Check(right>left && bottom>top && std::abs(float(left+right)*.5f-72.f)<=1.f &&
                std::abs(float(top+bottom)*.5f-72.f)<=1.f,"operation emoji ink is not centered in both axes");
            if(std::abs(float(right-left)-48.f)>1.f)
                std::cerr<<"EMOJI WIDTH utf16="<<unsigned(emoji[0])<<","<<unsigned(emoji[1])
                    <<" bounds="<<left<<","<<top<<","<<right<<","<<bottom<<"\n";
            Check(std::abs(float(right-left)-48.f)<=1.f,"operation emoji did not use the shared visible width");
        }
        size_t rounded=0,red=0;
        for(UINT y=0;y<sheet.height;++y) for(UINT x=0;x<sheet.width;++x) {
            const auto* p=sheet.data.data()+y*sheet.stride+x*4;
            rounded+=p[3]>0 && p[3]<200;
            if(x<180) red+=p[2]>140 && p[2]>p[1]*1.5 && p[2]>p[0]*1.5;
        }
        Check(rounded>200,"drag preview lost antialiased alpha edges");
        Check(red>100,"large prohibition emoji is absent or monochrome");
        wchar_t capture[MAX_PATH]{};
        if(GetEnvironmentVariableW(L"GPU_OVERLAY_DRAG_PREVIEW",capture,ARRAYSIZE(capture))) {
            ComPtr<IWICImagingFactory> wic;
            Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"drag preview capture WIC");
            IconAlphaFixture::Save(wic.Get(),sheet,capture);
        }
        // A non-square image with a separate blue atlas badge must retain its
        // entire red content and letterbox vertically, without showing the band.
        DragPreview preview; preview.side=96; preview.background=RGB(24,24,24); preview.foreground=RGB(245,245,245);
        auto& p=preview.pixels; p.width=80; p.height=58; p.stride=320;
        p.thumbnailWidth=80; p.thumbnailHeight=40; p.badgeSize=16; p.data.assign(size_t(p.stride)*p.height,0);
        for(UINT y=0;y<p.height;++y) for(UINT x=0;x<p.width;++x) {
            auto* pixel=p.data.data()+y*p.stride+x*4; pixel[y<40 ? 2:0]=220; pixel[3]=255;
        }
        for(int dpi:{96,144,192}) for(bool rtl:{false,true}) {
            preview.side=float(dpi); preview.dpi=dpi; preview.rtl=rtl;
            state.target->SetTransform(D2D1::Matrix3x2F::Identity());
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f)); DrawDragPreview(state.target.Get(),preview);
            Check(SUCCEEDED(state.target->EndDraw()),"drag preview aspect draw");
            const auto image=IconAlphaFixture::Read(state,surface.Get());
            const UINT x=UINT(preview.side*.74f),y=UINT(preview.side*.74f);
            const auto* center=image.data.data()+y*image.stride+x*4;
            const auto* margin=image.data.data()+UINT(preview.side*.3f)*image.stride+x*4;
            Check(center[2]>210 && center[0]<10 && margin[2]<50,"thumbnail was cropped, stretched or sampled its atlas badge");
            const auto extent=DragPreviewExtent(preview);
            Check(extent.width<=UINT(dpi*2) && DragPreviewBadgeSize(preview)>=GetSystemMetricsForDpi(SM_CXICON,dpi)*.9f,
                "drag preview/badge did not follow system DPI geometry");
        }
        state.target->SetTarget(previous.Get()); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    g_writeFactory=fonts;
    std::cout<<"PASS: immediate shared Shell pixels, real background high-resolution enrichment, antialiased dark/light LTR/RTL previews, color prohibition emoji, bounded count badge and uncropped atlas-aware thumbnails at three DPIs (offscreen components)\n";
}
