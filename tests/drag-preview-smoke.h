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
        state.dragPreviewEntries[0]=state.entries[0]; state.dragPreviewEpoch=state.mouseFolderEpoch; state.dragPreviewSize=96;
        RequestVisibleIcons(state);
        Check(state.dragPreview->pixels.data.Identity()==shown.data.Identity(),"drag request discarded immediate displayed pixels");
        Check(Eventually([&] {ReceiveIcons(state,false); return state.dragPreview->pixels.width>=96;}),
            "existing image workers did not enrich the drag preview");
        const auto sharp=state.dragPreview->pixels;
        state.dragPreview.reset();
        // A real PNG must enrich a Details drag even when list thumbnails are
        // disabled. The ordinary view keeps its association icon unchanged.
        const auto imagePath=std::filesystem::path(fixture+L"-drag-preview.png");
        struct ImageCleanup {std::filesystem::path path; ~ImageCleanup() {std::error_code e; std::filesystem::remove(path,e);}} cleanup{imagePath};
        ComPtr<IWICImagingFactory> wic;
        Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"drag PNG factory");
        IconPixels image; image.width=192; image.height=96; image.stride=image.width*4;
        image.data.assign(size_t(image.stride)*image.height,255);
        for(UINT y=0;y<image.height;++y) for(UINT x=0;x<image.width;++x) {
            auto* p=image.data.data()+size_t(y)*image.stride+x*4;
            p[0]=BYTE(40+x); p[1]=BYTE(60+y); p[2]=BYTE(x<image.width/2 ? 220:70);
        }
        IconAlphaFixture::Save(wic.Get(),image,imagePath);
        const bool listThumbnails=g_listThumbnails.exchange(false);
        struct RestoreThumbnails {bool value; ~RestoreThumbnails() {g_listThumbnails=value;}} restoreThumbnails{listThumbnails};
        state.gridView=false; state.cachedNativeMode=FVM_DETAILS;
        state.entries[0].path=imagePath.wstring(); state.entries[0].name=imagePath.filename().wstring(); state.entries[0].folder=false;
        IconPixels listed;
        RequestVisibleIcons(state);
        Check(Eventually([&] {
            ReceiveIcons(state,false); std::wstring path; bool attrs=false;
            auto* record=FindIconRecord(state,IconKey(state,state.entries[0],path,attrs),true);
            if(record && record->pixels.Ready()) listed=record->pixels;
            return listed.Ready();
        }) && !listed.thumbnailWidth,"Details fixture did not retain its ordinary icon");
        state.dragPreview=std::make_shared<DragPreview>(); state.dragPreview->pixels=listed; state.dragPreview->side=128;
        state.dragPreviewEntries[0]=state.entries[0]; state.dragPreviewEpoch=state.mouseFolderEpoch; state.dragPreviewSize=128;
        RequestVisibleIcons(state);
        Check(state.dragPreview->pixels.data.Identity()==listed.data.Identity(),"thumbnail request discarded immediate shown icon");
        Check(Eventually([&] {ReceiveIcons(state,false); return state.dragPreview->pixels.thumbnailWidth>0;}),
            "real PNG thumbnail did not replace the drag icon with list thumbnails disabled");
        const auto thumbnail=state.dragPreview->pixels;
        Check(!FileThumbnails(state) && thumbnail.thumbnailWidth==2*thumbnail.thumbnailHeight,
            "drag thumbnails changed the list setting or lost source proportions");
        state.dragPreview.reset();
        // Ask the actual filesystem target for feedback, without dropping or
        // changing focus/clipboard. No manufactured operation descriptions.
        ComPtr<IShellItem> sourceItem,destinationItem;
        Check(SUCCEEDED(SHCreateItemFromParsingName(imagePath.c_str(),nullptr,IID_PPV_ARGS(&sourceItem))) &&
            SUCCEEDED(SHCreateItemFromParsingName(fixture.c_str(),nullptr,IID_PPV_ARGS(&destinationItem))),"drag text Shell items");
        ComPtr<IDataObject> data;
        ComPtr<IDropTarget> destination;
        Check(SUCCEEDED(sourceItem->BindToHandler(nullptr,BHID_DataObject,IID_PPV_ARGS(&data))) &&
            SUCCEEDED(destinationItem->BindToHandler(nullptr,BHID_SFUIObject,IID_PPV_ARGS(&destination))),"drag text Shell binding");
        std::array<ShellDropText,4> shellTexts;
        for(int operation=1;operation<4;++operation) {
            DWORD effect=DROPEFFECT_COPY|DROPEFFECT_MOVE|DROPEFFECT_LINK;
            const DWORD keys=operation==1 ? MK_CONTROL:operation==2 ? MK_SHIFT:MK_CONTROL|MK_SHIFT;
            const auto enter=destination->DragEnter(data.Get(),keys,POINTL{},&effect);
            shellTexts[operation]=ReadShellDropText(data.Get(),effect);
            destination->DragLeave();
            Check(SUCCEEDED(enter) && effect==DWORD(operation==3 ? DROPEFFECT_LINK:operation) &&
                !shellTexts[operation].text.empty() && shellTexts[operation].insert.length,
                "native filesystem target did not provide its localized transaction description");
            Check(ReadShellDropText(data.Get(),DROPEFFECT_NONE).text.empty(),"blocked effect retained an earlier transaction caption");
            ClearShellDropDescription(data.Get());
            Check(ReadShellDropText(data.Get(),effect).text.empty(),"target leave retained an earlier transaction caption");
        }
        const auto& shellText=shellTexts[1];
        if(!state.headerFormat) Check(MakeTextFormats(state),"drag caption fonts");
        DragPreview captionPreview; captionPreview.side=128; captionPreview.dpi=96;
        UpdateDragCaption(state,captionPreview,shellText,600.f);
        // Independent DirectWrite width expectation in the actual caption face.
        ComPtr<IDWriteTextLayout> reference; DWRITE_TEXT_METRICS metrics{};
        Check(SUCCEEDED(g_writeFactory->CreateTextLayout(L"abcdefghijklmnopqrstuvwxyz",26,
            captionPreview.captionFormat.Get(),100000,100000,&reference)) && SUCCEEDED(reference->GetMetrics(&metrics)),
            "caption font width reference");
        const float expectedWidth=std::ceil(metrics.widthIncludingTrailingWhitespace*40.f/26.f);
        Check(captionPreview.captionAvailable==std::min(600.f,expectedWidth),"caption width did not follow measured font metrics");
        UpdateDragCaption(state,captionPreview,shellText,50.f);
        Check(captionPreview.captionAvailable==50.f,"caption exceeded a narrow work area");
        captionPreview.dpi=192; UpdateDragCaption(state,captionPreview,shellText,6000.f);
        Check(std::abs(captionPreview.captionAvailable-2*expectedWidth)<=1.f,"caption width failed to scale with monitor DPI");
        captionPreview.dpi=96; UpdateDragCaption(state,captionPreview,shellText,600.f);
        Check(captionPreview.caption && captionPreview.captionSize.width>0.f && captionPreview.captionSize.height>0.f &&
            !UpdateDragCaption(state,captionPreview,shellText,600.f),"drag caption layout was absent or rebuilt unchanged feedback");
        const auto captionGeometry=LayoutDragPreview(captionPreview);
        const POINT captionCursor{960,540};
        const auto captionPosition=PositionDragPreview(captionPreview,captionCursor,false);
        for(const auto& text:shellTexts) {
            UpdateDragCaption(state,captionPreview,text,600.f);
            const auto geometry=LayoutDragPreview(captionPreview);
            const auto position=PositionDragPreview(captionPreview,captionCursor,false);
            Check(geometry.artwork.left==captionGeometry.artwork.left && geometry.artwork.top==captionGeometry.artwork.top &&
                position.x==captionPosition.x && position.y==captionPosition.y,
                "native description width changes moved the icon canvas or pointer anchor");
        }
        Check(EnsureTarget(state),"drag preview GPU target");
        {
            // Exercise the real layered-window/DComp contract offscreen. The
            // popup uses the already-created tab device and has no callback.
            auto preview=std::make_shared<DragPreview>(); preview->pixels=sharp;
            preview->imageCount=kDragPreviewImages;
            for(size_t i=1;i<kDragPreviewImages;++i) {preview->Image(i).pixels=i&1 ? thumbnail:sharp;
                preview->Image(i).tilt=DragStackTilt(std::to_wstring(i),i);}
            preview->side=64; preview->background=RGB(32,32,32); preview->foreground=RGB(245,245,245);
            preview->token=ULONG_PTR(g_nextToken.fetch_add(1));
            preview->window=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOREDIRECTIONBITMAP,
                L"STATIC",L"private drag preview",WS_POPUP,-32000,-32000,128,128,tab.frame,nullptr,GetModuleHandleW(nullptr),nullptr);
            Check(preview->window && SetLayeredWindowAttributes(preview->window,0,255,LWA_ALPHA) &&
                SetPropW(preview->window,L"WindhawkGpuDragPreview",reinterpret_cast<HANDLE>(preview->token)),"private drag popup");
            const auto device=state.d3d.Get();
            Check(RenderDragPreview(state,preview) && preview->rendered->load() && state.dragComposition.visual &&
                state.dragComposition.curve && state.dragComposition.opacityCurve && state.dragComposition.opacity && state.d3d.Get()==device,"drag preview did not reuse tab GPU/compositor scale and fade animations");
            const auto entryScale=state.dragComposition.curve.Get();
            const auto entryFade=state.dragComposition.opacityCurve.Get();
            const auto artwork=state.dragComposition.artwork[0].Get(); preview->effect=DROPEFFECT_COPY;
            const auto rear=state.dragComposition.artwork[1].Get();
            Check(artwork && RenderDragPreview(state,preview) && state.dragComposition.artwork[0].Get()==artwork,
                "effect feedback uploaded the same artwork again");
            Check(state.dragComposition.curve.Get()==entryScale && state.dragComposition.opacityCurve.Get()==entryFade,
                "effect feedback restarted drag entry scale/fade");
            Check(rear && state.dragComposition.artwork[1].Get()==rear && state.dragComposition.artwork[2].Get()==artwork &&
                state.dragComposition.artwork[3].Get()==rear,"stack images reuploaded identical shared pixels");
            preview->animate=false;
            Check(RenderDragPreview(state,preview) && !state.dragComposition.curve && !state.dragComposition.opacityCurve &&
                !state.dragComposition.scale && !state.dragComposition.opacity,"disabled animations retained drag entry scale/fade");
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
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1440,1200),nullptr,0,&properties,&surface)),"drag preview sheet");
        state.target->SetTarget(surface.Get()); state.target->BeginDraw();
        state.target->Clear(D2D1::ColorF(0,0.f));
        DragTextStyle sheetStyle; RefreshTextRenderingForWindow(sheetStyle,state.hwnd,true);
        for(int row=0;row<4;++row) for(int column=0;column<4;++column) {
            DragPreview preview; preview.side=128; preview.dpi=96; preview.pixels=row<2 ? sharp:thumbnail;
            preview.background=row&1 ? RGB(32,32,32):RGB(248,248,248);
            preview.foreground=row&1 ? RGB(245,245,245):RGB(24,24,24);
            preview.accent=RGB(0,100,200); preview.rtl=row>=2; preview.count=column==3 ? 1200:column+1;
            preview.captionAvailable=260.f;
            preview.imageCount=column==3 ? kDragPreviewImages:size_t(column+1);
            for(size_t i=1;i<preview.imageCount;++i) {
                preview.Image(i).pixels=i&1 ? thumbnail:sharp;
                preview.Image(i).tilt=(i&1 ? -1.f:1.f)*3.f*float(i);
            }
            preview.effect=column==0 ? DROPEFFECT_NONE:column==1 ? DROPEFFECT_COPY:column==2 ? DROPEFFECT_MOVE:DROPEFFECT_LINK;
            if(column) UpdateDragCaption(state,preview,shellTexts[column],260.f);
            state.target->SetTransform(D2D1::Matrix3x2F::Translation(float(column*360),float(row*300)));
            DrawDragPreview(state.target.Get(),preview,nullptr,nullptr,&sheetStyle);
        }
        Check(SUCCEEDED(state.target->EndDraw()),"drag preview color emoji/theme draw");
        auto sheet=IconAlphaFixture::Read(state,surface.Get());
        ComPtr<ID2D1SolidColorBrush> glyphBrush;
        Check(SUCCEEDED(state.target->CreateSolidColorBrush(D2D1::ColorF(1,1,1),&glyphBrush)),"drag glyph brush");
        state.target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE); // Compare emoji artwork, independently of opaque text.
        state.target->SetTextRenderingParams(nullptr); // Match the recording context's native emoji policy, not the preceding caption's policy.
        for(float symbolSize:{48.f,128.f*.58f*.82f,192.f*.58f*.82f,256.f*.58f*.82f})
        for(const auto* emoji:{L"\U0001F6AB",L"\U0001F4CB",L"\u27A1\uFE0F",L"\u2B05\uFE0F",L"\U0001F517"}) {
            DragGlyph cached;
            state.target->SetTransform(D2D1::Matrix3x2F::Identity());
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
            DrawCenteredDragLabel(state.target.Get(),glyphBrush.Get(),emoji,L"Segoe UI Emoji",symbolSize,
                D2D1::RectF(168,168,232,232),RGB(255,255,255),true,&cached);
            Check(SUCCEEDED(state.target->EndDraw()),"centered operation emoji draw");
            const auto glyph=IconAlphaFixture::Read(state,surface.Get());
            UINT left=glyph.width,top=glyph.height,right=0,bottom=0;
            for(UINT y=0;y<glyph.height;++y) for(UINT x=0;x<glyph.width;++x)
                if(glyph.data[size_t(y)*glyph.stride+x*4+3]>16) {
                    left=std::min(left,x); right=std::max(right,x+1); top=std::min(top,y); bottom=std::max(bottom,y+1);
                }
            const bool centered=right>left && bottom>top && std::abs(float(left+right)*.5f-200.f)<=1.f &&
                std::abs(float(top+bottom)*.5f-200.f)<=1.f;
            if(!centered) std::cerr<<"EMOJI CENTER utf16="<<unsigned(emoji[0])<<","<<unsigned(emoji[1])<<" size="<<symbolSize
                <<" bounds="<<left<<","<<top<<","<<right<<","<<bottom<<" ink="<<cached.bounds.left<<","<<cached.bounds.top
                <<","<<cached.bounds.right<<","<<cached.bounds.bottom<<"\n";
            Check(centered,"operation emoji ink is not centered in both axes");
            // Visible width is the half-coverage extent of the shape; the faint
            // antialiasing tail (alpha>16) extends past the outline by up to a
            // texel per side. Fractional edges still allow +/-1 texel.
            UINT halfLeft=glyph.width,halfRight=0;
            for(UINT y=0;y<glyph.height;++y) for(UINT x=0;x<glyph.width;++x)
                if(glyph.data[size_t(y)*glyph.stride+x*4+3]>128) {halfLeft=std::min(halfLeft,x); halfRight=std::max(halfRight,x+1);}
            const bool commonWidth=halfRight>halfLeft && halfRight-halfLeft>=UINT(std::floor(symbolSize)-1) &&
                halfRight-halfLeft<=UINT(std::ceil(symbolSize)+1);
            if(!commonWidth) {
                float dpiX=0,dpiY=0; state.target->GetDpi(&dpiX,&dpiY);
                std::cerr<<"EMOJI WIDTH utf16="<<unsigned(emoji[0])<<","<<unsigned(emoji[1])
                    <<" size="<<symbolSize<<" bounds="<<left<<","<<top<<","<<right<<","<<bottom<<" half="<<halfLeft<<".."<<halfRight
                    <<" command-bounds="<<cached.bounds.left<<","<<cached.bounds.top<<","<<cached.bounds.right<<","<<cached.bounds.bottom
                    <<" dpi="<<dpiX<<","<<dpiY<<" mactype="<<bool(GetModuleHandleW(L"MacType64.dll") || GetModuleHandleW(L"MacType64.Core.dll"))<<"\n";
            }
            // Compare against the font's original regular color artwork.
            // Semibold synthesizes bold and visibly flattens the 🚫 edge,
            // including when using Windows' direct text drawing path.
            ComPtr<IDWriteTextFormat> directFormat; ComPtr<IDWriteTextLayout> direct;
            Check(SUCCEEDED(g_writeFactory->CreateTextFormat(L"Segoe UI Emoji",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,symbolSize,L"",&directFormat)) &&
                SUCCEEDED(g_writeFactory->CreateTextLayout(emoji,UINT32(wcslen(emoji)),directFormat.Get(),64,64,&direct)),"native emoji reference");
            direct->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            if(!commonWidth) {
                DWRITE_OVERHANG_METRICS ink{}; DWRITE_TEXT_METRICS metrics{};
                direct->GetOverhangMetrics(&ink); direct->GetMetrics(&metrics);
                std::cerr<<"EMOJI FONT size="<<directFormat->GetFontSize()<<" weight="<<directFormat->GetFontWeight()
                    <<" ink="<<ink.left<<","<<ink.top<<","<<ink.right<<","<<ink.bottom
                    <<" metrics="<<metrics.left<<","<<metrics.top<<","<<metrics.width<<","<<metrics.height<<"\n";
            }
            const auto& b=cached.bounds; const float scale=symbolSize/(b.right-b.left);
            state.target->SetTransform(D2D1::Matrix3x2F::Scale(scale,scale)*D2D1::Matrix3x2F::Translation(
                200.f-(b.left+b.right)*.5f*scale,200.f-(b.top+b.bottom)*.5f*scale));
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
            state.target->DrawTextLayout(D2D1::Point2F(),direct.Get(),glyphBrush.Get(),D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
            Check(SUCCEEDED(state.target->EndDraw()),"direct native emoji draw");
            const auto native=IconAlphaFixture::Read(state,surface.Get());
            size_t changed=0;
            int largest=0;
            for(size_t i=0;i<glyph.data.size();++i) {
                const int difference=std::abs(int(glyph.data[i])-int(native.data[i]));
                changed+=difference>1; largest=std::max(largest,difference);
            }
            if(changed && emoji[0]==L'\xD83D' && emoji[1]==L'\xDEAB')
                std::cerr<<"EMOJI NATIVE size="<<symbolSize<<" changed="<<changed<<" largest="<<largest<<" bounds="<<cached.bounds.left
                    <<","<<cached.bounds.top<<","<<cached.bounds.right<<","<<cached.bounds.bottom<<"\n";
            if(!commonWidth) {
                for(BYTE threshold:{BYTE(0),BYTE(1),BYTE(16),BYTE(128)}) {
                    UINT l=native.width,r=0,t=native.height,bottomEdge=0;
                    for(UINT y=0;y<native.height;++y) for(UINT x=0;x<native.width;++x)
                        if(native.data[size_t(y)*native.stride+x*4+3]>threshold) {
                            l=std::min(l,x); r=std::max(r,x+1); t=std::min(t,y); bottomEdge=std::max(bottomEdge,y+1);
                        }
                    std::cerr<<"EMOJI REFERENCE alpha>"<<unsigned(threshold)<<" bounds="<<l<<","<<t<<","<<r<<","<<bottomEdge
                        <<" changed="<<changed<<"\n";
                }
            }
            Check(commonWidth,"operation emoji did not use the shared visible width");
            Check(emoji[0]!=L'\xD83D' || emoji[1]!=L'\xDEAB' || !changed,
                "prohibition emoji differed from the unclipped native font rendering");
        }
        for(int dpi:{96,144,192}) for(bool rtl:{false,true}) for(size_t count:{size_t(1),kDragPreviewImages}) {
            DragPreview preview; preview.side=128.f*dpi/96.f; preview.dpi=dpi; preview.rtl=rtl; preview.imageCount=count;
            preview.pixels.width=preview.pixels.height=32; preview.pixels.stride=128; preview.pixels.data.assign(4096,0);
            preview.count=int(count);
            for(size_t i=1;i<count;++i) {preview.Image(i).pixels=i&1 ? sharp:thumbnail; preview.Image(i).tilt=3.f*i*(i&1 ? -1.f:1.f);}
            const auto extent=DragPreviewExtent(preview);
            ComPtr<ID2D1Bitmap1> exact;
            Check(SUCCEEDED(state.target->CreateBitmap(extent,nullptr,0,&properties,&exact)),"exact drag popup surface");
            const auto render=[&](ID2D1Bitmap1* target) {
                state.target->SetTarget(target); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
                state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f)); DrawDragPreview(state.target.Get(),preview);
                Check(SUCCEEDED(state.target->EndDraw()),"popup boundary render"); return IconAlphaFixture::Read(state,target);
            };
            const auto bounded=render(exact.Get()),reference=render(surface.Get());
            for(UINT y=0;y<reference.height;++y) for(UINT x=0;x<reference.width;++x) {
                const auto* pixel=reference.data.data()+size_t(y)*reference.stride+x*4;
                if(x>=extent.width || y>=extent.height) Check(!pixel[3],"drag popup clipped emoji or rotated-stack pixels");
                else for(UINT channel=0;channel<4;++channel)
                    Check(std::abs(int(pixel[channel])-int(bounded.data[size_t(y)*bounded.stride+x*4+channel]))<=1,
                        "emoji texels changed at the actual popup surface boundary");
            }
        }
        {
            const auto old=g_textAntialiasing.load();
            struct Restore {TextAntialiasing mode; ~Restore() {g_textAntialiasing=mode; ++g_textRenderingRevision;}} restore{old};
            DragTextStyle style;
            DragPreview preview; preview.side=128; preview.dpi=96; preview.count=6;
            preview.background=RGB(24,24,24); preview.foreground=RGB(240,240,240); preview.accent=RGB(40,40,40);
            preview.badgePalette=static_cast<unsigned long long>(preview.accent) |
                (static_cast<unsigned long long>(preview.foreground)<<24) | (255ULL<<48);
            preview.pixels.width=preview.pixels.height=32; preview.pixels.stride=128; preview.pixels.data.assign(4096,0);
            UpdateDragCaption(state,preview,shellText,260.f);
            const auto geometry=LayoutDragPreview(preview);
            const auto listSerial=state.textRenderingSerial;
            for(auto mode:{TextAntialiasing::System,TextAntialiasing::Smooth,TextAntialiasing::Aliased}) {
                g_textAntialiasing=mode; ++g_textRenderingRevision;
                RefreshTextRenderingForWindow(style,state.hwnd,true);
                state.target->SetTarget(surface.Get()); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
                state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
                DrawDragPreview(state.target.Get(),preview,nullptr,nullptr,&style);
                Check(SUCCEEDED(state.target->EndDraw()),"drag text rendering choice");
                ComPtr<IDWriteRenderingParams> effective; state.target->GetTextRenderingParams(&effective);
                const auto actualMode=state.target->GetTextAntialiasMode();
                const bool overridden=effective.Get()!=style.textRenderingParams.Get() || actualMode!=style.opaqueTextAntialiasMode;
                // The normal text-quality suite recognizes this same native
                // font customizer. Verify its effective policy and pixels too.
                if(overridden) Check(GetModuleHandleW(L"MacType64.dll") || GetModuleHandleW(L"MacType64.Core.dll"),
                    "unexplained drag text policy override");
                Check(listSerial==state.textRenderingSerial && style.textAntialiasing==mode,
                    "drag text ignored shared policy or invalidated the tab glyph cache");
                const auto pixels=IconAlphaFixture::Read(state,surface.Get());
                const float height=preview.side*.38f*preview.dragBadgeScaling;
                const auto count=D2D1::RectF(geometry.pile.left+height*.25f,geometry.pile.top+height*.25f,
                    geometry.pile.left+height*.75f,geometry.pile.top+height*.75f);
                auto caption=geometry.caption; caption.left+=geometry.edge; caption.top+=geometry.edge;
                caption.right-=geometry.edge; caption.bottom-=geometry.edge;
                for(auto area:{count,caption}) {
                    size_t partial=0,colored=0;
                    for(UINT y=UINT(std::ceil(area.top));y<UINT(area.bottom);++y) for(UINT x=UINT(std::ceil(area.left));x<UINT(area.right);++x) {
                        const auto* p=pixels.data.data()+size_t(y)*pixels.stride+x*4;
                        colored+=p[0]!=p[1] || p[1]!=p[2];
                        const BYTE background=area.top==count.top ? 40:24;
                        partial+=p[0]>background && p[0]<240;
                        Check(p[3]==255,"subpixel drag text left its opaque background");
                    }
                    if(actualMode==D2D1_TEXT_ANTIALIAS_MODE_ALIASED) Check(!partial && !colored,"drag caption/count antialiased in aliased mode");
                    if(actualMode==D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE) Check(partial>5 && !colored,"drag caption/count did not use grayscale smoothing");
                    if(actualMode==D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE &&
                        effective->GetClearTypeLevel()>0 && effective->GetPixelGeometry()!=DWRITE_PIXEL_GEOMETRY_FLAT)
                        Check(colored>5,"System drag text lost Windows subpixel rendering");
                }
            }
        }
        size_t rounded=0,red=0;
        for(UINT y=0;y<sheet.height;++y) for(UINT x=0;x<sheet.width;++x) {
            const auto* p=sheet.data.data()+y*sheet.stride+x*4;
            rounded+=p[3]>0 && p[3]<200;
            if(x<360) red+=p[2]>140 && p[2]>p[1]*1.5 && p[2]>p[0]*1.5;
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
            const auto geometry=LayoutDragPreview(preview);
            const UINT x=UINT((geometry.artwork.left+geometry.artwork.right)*.5f),y=UINT((geometry.artwork.top+geometry.artwork.bottom)*.5f);
            const auto* center=image.data.data()+y*image.stride+x*4;
            const auto* margin=image.data.data()+UINT(geometry.artwork.top-3)*image.stride+x*4;
            Check(center[2]>210 && center[0]<10 && margin[3]==0,"thumbnail was cropped, sampled its atlas badge or retained a tile background");
            const auto extent=DragPreviewExtent(preview);
            Check(extent.width<=UINT(dpi*2) && DragPreviewBadgeSize(preview)>=GetSystemMetricsForDpi(SM_CXICON,dpi)*.9f,
                "drag preview/badge did not follow system DPI geometry");
            const RECT work{-1920,-1080,0,0};
            const POINT cursor{-960,-540};
            const auto position=PositionDragPreview(preview,cursor,false);
            Check(std::abs(position.x+(geometry.artwork.left+geometry.artwork.right)*.5f-cursor.x)<=.5f &&
                std::abs(position.y+geometry.artwork.bottom-cursor.y)<=.5f,
                "drag artwork hotspot included transparent padding or badge offset");
            const auto touch=PositionDragPreview(preview,cursor,true);
            Check(touch.y+LONG(extent.height)==cursor.y,"touch preview overlaps the promoted contact point");
            auto portrait=preview;
            std::swap(portrait.pixels.thumbnailWidth,portrait.pixels.thumbnailHeight);
            const auto replacement=PositionDragPreview(portrait,cursor,false);
            Check(position.x==replacement.x && position.y==replacement.y,
                "background thumbnail aspect changes moved the stationary drag hotspot");
            // Translation must stay continuous beyond screen and work-area edges.
            // This expectation is independent of the layout hotspot arithmetic.
            for(bool promoted:{false,true}) for(POINT edge:{POINT{work.left,work.top},
                POINT{work.right,work.bottom},POINT{work.left-500,work.top-500},POINT{500,500}}) {
                const auto origin=PositionDragPreview(preview,cursor,promoted);
                const auto moved=PositionDragPreview(preview,edge,promoted);
                Check(moved.x-origin.x==edge.x-cursor.x && moved.y-origin.y==edge.y-cursor.y,
                    "drag preview stopped following the pointer at a screen boundary");
            }
        }
        state.target->SetTarget(previous.Get()); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    {
        const auto root=std::filesystem::path(fixture+L"-drag-pile");
        for(int i=0;i<7;++i) std::filesystem::create_directories(root/std::to_wstring(i));
        struct Cleanup {std::filesystem::path root; ~Cleanup() {std::error_code error;
            for(int i=0;i<7;++i) std::filesystem::remove(root/std::to_wstring(i),error); std::filesystem::remove(root,error);}} cleanup{root};
        ProviderSelectionFixture::Browser host; host.Navigate(root.wstring(),7);
        auto& state=host.state;
        for(int i=0;i<6;++i) Check(SUCCEEDED(state.folderView->SelectItem(i,SVSI_SELECT|SVSI_NOTAKEFOCUS|
            (i ? 0:SVSI_DESELECTOTHERS))),"native pile selection");
        Check(CaptureNativeSelection(state,false) && Eventually([&] {host.Pump();
            return !state.pendingNativeSelection && state.nativeSelectionObservedPaths.size()==6;}),"native pile identity import");
        const int front=GpuRowForNativePath(state,state.nativeSelectionObservedPaths.back());
        const auto rows=DragPreviewRows(state,front,6);
        Check(rows.size()==kDragPreviewImages && rows.front()==front,"pile failed to cap native selection at five with the grabbed item on top");
        for(int row:rows) Check(state.selectedFlags[size_t(row)] && std::count(rows.begin(),rows.end(),row)==1,
            "pile included an unselected or duplicate native identity");
        for(size_t i=0;i<rows.size();++i) Check(std::abs(DragStackTilt(state.entries[size_t(rows[i])].path,i))==3.f*i &&
            std::abs(DragStackTilt(state.entries[size_t(rows[i])].path,i))<=12.f,
            "pile rotated its front item or exceeded the progressive portrait tilt cap");
    }
    g_writeFactory=fonts;
    std::cout<<"PASS: immediate shared Shell pixels; real PNG background thumbnails with Details thumbnails disabled; transparent previews, equal-width operation emoji, texture reuse and uncropped atlas-aware images at three DPIs (offscreen components)\n";
}

namespace DragPileFixture {
inline IconPixels Solid(UINT width,UINT height,bool thumbnail) {
    IconPixels pixels; pixels.width=width; pixels.height=height; pixels.stride=width*4;
    if(thumbnail) {pixels.thumbnailWidth=width; pixels.thumbnailHeight=height;}
    pixels.data.assign(size_t(pixels.stride)*height,0);
    for(size_t i=0;i<size_t(width)*height;++i) {auto* p=pixels.data.data()+i*4; p[2]=220; p[3]=255;} // Opaque red.
    return pixels;
}
struct Box {LONG left=LONG_MAX,top=LONG_MAX,right=LONG_MIN,bottom=LONG_MIN; bool Empty() const {return right<left;}};
template<class Match> Box Find(const IconPixels& image,Match match) {
    Box box;
    for(UINT y=0;y<image.height;++y) for(UINT x=0;x<image.width;++x)
        if(match(image.data.data()+size_t(y)*image.stride+x*4)) {
            box.left=std::min<LONG>(box.left,x); box.top=std::min<LONG>(box.top,y);
            box.right=std::max<LONG>(box.right,x+1); box.bottom=std::max<LONG>(box.bottom,y+1);
        }
    return box;
}
// Draws a pile on a private offscreen target of the tab's device.
struct Sheet {
    PresentationFixture::Tab tab{L"drag pile",1,false,true};
    ViewState& state=tab.state;
    ComPtr<ID2D1Image> previous;
    ComPtr<ID2D1Bitmap1> surface;
    Sheet() {
        Check(EnsureTarget(state),"drag pile GPU target");
        state.target->GetTarget(&previous);
        const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1024,1024),nullptr,0,&properties,&surface)),"drag pile sheet");
    }
    ~Sheet() {state.target->SetTarget(previous.Get());}
    IconPixels Draw(const DragPreview& preview) {
        state.target->SetTarget(surface.Get()); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
        state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
        DrawDragPreview(state.target.Get(),preview);
        Check(SUCCEEDED(state.target->EndDraw()),"drag pile draw");
        return IconAlphaFixture::Read(state,surface.Get());
    }
};
}

// Pile thumbnails round their corners by the grid thumbnail rule (8 px at
// 96 DPI, at most a fifth of the shorter side), worked out from the pile's
// own drawn size and DPI; association icons stay square. Measured where the
// corner curve leaves the drawn thumbnail's top edge.
static void TestDragPileThumbnailCorners() {
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"drag pile fonts");
    DragPileFixture::Sheet sheet;
    bool failed=false;
    for(int dpi:{96,144,192}) for(int side96:{96,32}) for(bool thumbnail:{true,false}) {
        DragPreview preview; preview.dpi=dpi; preview.side=float(side96)*dpi/96.f; preview.effect=DROPEFFECT_COPY;
        preview.background=RGB(24,24,24); preview.foreground=RGB(245,245,245);
        preview.pixels=DragPileFixture::Solid(64,64,thumbnail);
        const auto image=sheet.Draw(preview);
        // The thumbnail's full-coverage extent; its top row starts after the curve.
        const auto red=[](const BYTE* p) {return p[3]>=250 && p[2]>200 && p[1]<40 && p[0]<40;};
        const auto full=DragPileFixture::Find(image,red);
        Check(!full.Empty(),"drag pile thumbnail was not drawn");
        const float width=float(full.right-full.left),height=float(full.bottom-full.top);
        const float radius=thumbnail ? std::min(8.f*dpi/96.f,.2f*std::min(width,height)):0.f;
        const float expected=radius>=.5f ? radius-std::sqrt(radius*radius-(radius-.5f)*(radius-.5f)):0.f;
        LONG first=full.left;
        const BYTE* row=image.data.data()+size_t(full.top)*image.stride;
        while(first<full.right && row[first*4+3]<128) ++first;
        const float measured=float(first-full.left);
        if(std::abs(measured-expected)>1.f) failed=true;
        std::cout<<"PILE CORNER dpi="<<dpi<<" side="<<preview.side<<" thumbnail="<<thumbnail<<" drawn="<<width<<"x"<<height
            <<" radius="<<radius<<" curve="<<measured<<" expected="<<expected<<std::endl;
    }
    Check(!failed,"drag pile thumbnail corners do not follow the thumbnail rounding rule at the pile's size and DPI");
    std::cout<<"PASS: drag pile thumbnails round by the grid thumbnail rule at their own size and DPI (96/144/192, large "
        "and small piles); association icons stay square\n";
}

// The item count sits at the whole pile's leading top corner, not the front
// item's: a small front image must not pull it into the pile's middle.
static void TestDragPileCountBadge() {
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"drag pile fonts");
    DragPileFixture::Sheet sheet;
    bool failed=false;
    for(int dpi:{96,192}) for(bool rtl:{false,true}) {
        DragPreview preview; preview.dpi=dpi; preview.side=96.f*dpi/96.f; preview.rtl=rtl; preview.effect=DROPEFFECT_COPY;
        preview.background=RGB(24,24,24); preview.foreground=RGB(245,245,245);
        const COLORREF badge=RGB(0,200,0);
        preview.badgePalette=static_cast<unsigned long long>(badge) |
            (static_cast<unsigned long long>(RGB(255,255,255))<<24) | (255ULL<<48);
        // Square icons, so the pile's corners are its geometric corners; the
        // front one is small and wide.
        preview.imageCount=4;
        preview.pixels=DragPileFixture::Solid(64,16,false);
        for(size_t i=1;i<preview.imageCount;++i) {
            preview.Image(i).pixels=DragPileFixture::Solid(64,64,false);
            preview.Image(i).tilt=DragStackTilt(std::to_wstring(i),i);
        }
        const auto red=[](const BYTE* p) {return p[3]>=128 && p[2]>100 && p[1]<60 && p[0]<60;};
        preview.count=1;
        const auto pile=DragPileFixture::Find(sheet.Draw(preview),red);
        preview.count=4;
        const auto counted=sheet.Draw(preview);
        const auto marker=DragPileFixture::Find(counted,[](const BYTE* p) {return p[3]>=250 && p[1]>180 && p[2]<40 && p[0]<40;});
        Check(!pile.Empty() && !marker.Empty(),"drag pile or its count badge was not drawn");
        const LONG horizontal=rtl ? marker.right-pile.right : marker.left-pile.left;
        const LONG vertical=marker.top-pile.top;
        std::cout<<"PILE BADGE dpi="<<dpi<<" rtl="<<rtl<<" pile="<<pile.left<<","<<pile.top<<"-"<<pile.right<<","<<pile.bottom
            <<" badge="<<marker.left<<","<<marker.top<<"-"<<marker.right<<","<<marker.bottom<<std::endl;
        failed|=std::abs(horizontal)>1 || std::abs(vertical)>1;
    }
    Check(!failed,"the count badge is not at the pile's leading top corner");
    std::cout<<"PASS: the drag count badge sits at the whole pile's top-left (top-right in RTL) at 96/192 DPI, "
        "independent of a small front item\n";
}
