static void SaveGridPreview(ViewState& state, const wchar_t* environment=L"GPU_OVERLAY_GRID_PREVIEW") {
    wchar_t path[MAX_PATH]{};
    if (!GetEnvironmentVariableW(environment,path,ARRAYSIZE(path))) return;
    const auto size = state.backbuffer->GetPixelSize();
    ComPtr<ID2D1Bitmap1> cpu;
    const auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ |
        D2D1_BITMAP_OPTIONS_CANNOT_DRAW,state.backbuffer->GetPixelFormat());
    Check(SUCCEEDED(state.target->CreateBitmap(size,nullptr,0,&props,&cpu)) &&
        SUCCEEDED(cpu->CopyFromBitmap(nullptr,state.backbuffer.Get(),nullptr)), "preview readback");
    D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"preview map");
    ComPtr<IWICImagingFactory> wic; ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IWICBitmap> bitmap; ComPtr<IWICFormatConverter> converted;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path,GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr)) && SUCCEEDED(frame->Initialize(nullptr)), "preview encoder");
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;
    Check(SUCCEEDED(wic->CreateBitmapFromMemory(size.width,size.height,GUID_WICPixelFormat32bppPBGRA,
        map.pitch,map.pitch*size.height,map.bits,&bitmap)) && SUCCEEDED(wic->CreateFormatConverter(&converted)) &&
        SUCCEEDED(converted->Initialize(bitmap.Get(),format,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(frame->SetSize(size.width,size.height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
        SUCCEEDED(frame->WriteSource(converted.Get(),nullptr)) && SUCCEEDED(frame->Commit()) &&
        SUCCEEDED(encoder->Commit()), "preview PNG write");
    cpu->Unmap();
}

static void TestThumbnailDecorations(ViewState& state) {
    ComPtr<ID2D1Image> original; state.target->GetTarget(&original);
    ComPtr<ID2D1Bitmap1> surface, readback;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(320,320),nullptr,0,&props,&surface)),
        "thumbnail decoration target");
    props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(320,320),nullptr,0,&props,&readback)),
        "thumbnail decoration readback");
    state.target->SetTarget(surface.Get());
    const float oldSize=state.iconPx, oldPaint=state.paintWidth;
    const int oldDpi=state.textFormatDpi;
    const float oldRadius=g_thumbnailCornerRadius96;
    state.paintWidth=320;
    IconPixels image;
    image.width=80; image.height=40; image.stride=320; image.data.resize(40*320);
    for(size_t i=0;i<image.data.size();i+=4) {
        image.data[i]=40; image.data[i+1]=80; image.data[i+2]=220; image.data[i+3]=255;
    }
    ThumbnailStyle style; style.filmstrip=true;
    style.badge.width=style.badge.height=16; style.badge.stride=64;
    style.badge.data.assign(16*64,0);
    for(size_t i=0;i<style.badge.data.size();i+=4) {
        style.badge.data[i]=250; style.badge.data[i+3]=255;
    }
    IconRecord decorated, plain;
    decorated.pixels=DecorateThumbnail(IconPixels(image),style);
    plain.pixels=image;
    Check(decorated.pixels.thumbnailWidth==88 && decorated.pixels.thumbnailHeight==40 &&
        decorated.pixels.badgeSize==16 && decorated.pixels.width==88 && decorated.pixels.height==58,
        "film/badge atlas lost source aspect or isolated badge band");
    Check(UploadIconBitmap(state,decorated) && UploadIconBitmap(state,plain),"decoration upload");
    auto draw=[&](IconRecord& record,float opacity=1.f,float angle=0.f) {
        state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
        const auto transform=D2D1::Matrix3x2F::Identity(); state.target->SetTransform(transform);
        Check(DrawCachedIcon(state,record,40,40,opacity,angle),"decoration shared draw");
        D2D1_MATRIX_3X2_F after{}; state.target->GetTransform(&after);
        Check(memcmp(&after,&transform,sizeof(after))==0,"thumbnail wiggle leaked its transform");
        Check(SUCCEEDED(state.target->EndDraw()) &&
            SUCCEEDED(readback->CopyFromBitmap(nullptr,surface.Get(),nullptr)),"decoration render/readback");
    };
    auto pixel=[&](int x,int y) {
        D2D1_MAPPED_RECT map{};
        Check(SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ,&map)),"decoration map");
        const auto* p=map.bits+y*map.pitch+x*4;
        const std::array<BYTE,4> value{p[0],p[1],p[2],p[3]};
        readback->Unmap(); return value;
    };
    for(int dpi : {96,192}) {
        state.textFormatDpi=dpi; state.iconPx=96.f*dpi/96;
        const int left=static_cast<int>(state.rtlLayout ? 320-40-state.iconPx : 40);
        const float h=state.iconPx*40/88;
        const int top=static_cast<int>(40+(state.iconPx-h)*.5f);
        const int bottom=static_cast<int>(40+(state.iconPx+h)*.5f);
        const int middle=left+static_cast<int>(state.iconPx*.5f);
        draw(decorated);
        auto center=pixel(middle,top+static_cast<int>(h*.5f));
        auto corner=pixel(left+1,top+1);
        // Sample above the badge, which covers the left rail in RTL.
        auto rail=pixel(left+2*dpi/96,top+static_cast<int>(h*.25f));
        const int badgeX=state.rtlLayout ? left+8*dpi/96 : left+static_cast<int>(state.iconPx)-8*dpi/96;
        auto badge=pixel(badgeX,bottom-8*dpi/96);
        auto shadow=pixel(middle,bottom+2*dpi/96);
        if (!(corner[3]<120 && rail[0]==rail[1] && rail[1]==rail[2] && rail[3]==255))
            std::cerr << "THUMBNAIL rtl=" << state.rtlLayout << " dpi=" << dpi <<
                " corner=" << int(corner[0]) << ',' << int(corner[1]) << ',' << int(corner[2]) << ',' << int(corner[3]) <<
                " rail=" << int(rail[0]) << ',' << int(rail[1]) << ',' << int(rail[2]) << ',' << int(rail[3]) << '\n';
        Check(center==std::array<BYTE,4>{40,80,220,255},"thumbnail lost original center pixels");
        Check(corner[3]<120 && rail[0]==rail[1] && rail[1]==rail[2] && rail[3]==255,
            "rounded thumbnail failed to clip complete film rails");
        Check(badge==std::array<BYTE,4>{250,0,0,255},"app badge missed trailing LTR/RTL corner");
        Check(shadow[3]>0 && shadow[3]<80 && shadow[0]==0,"cheap thumbnail shadow absent/opaque");
        draw(decorated,.5f,5.f); // Includes film, badge and shadow in Cut transform/alpha.
        draw(decorated,.5f);
        const auto dim=pixel(middle,top+static_cast<int>(h*.5f));
        const auto dimBadge=pixel(badgeX,bottom-8*dpi/96);
        Check(abs(dim[2]-110)<=1 && dim[3]<180 && dimBadge[0]>=125 && dimBadge[0]<180 &&
            dimBadge[3]<230,"thumbnail/badge lost shared Cut opacity");
        // The global test radius is independent of either selection setting.
        g_thumbnailCornerRadius96=0; draw(decorated);
        Check(pixel(left+1,top+1)[3]>240,"global thumbnail radius did not control film corners");
        g_thumbnailCornerRadius96=oldRadius;
    }
    state.iconPx=96; state.textFormatDpi=96;
    draw(plain);
    const int left=state.rtlLayout ? 320-40-96 : 40;
    Check(pixel(left+1,65)==std::array<BYTE,4>{40,80,220,255},
        "ordinary icon acquired thumbnail rounding/badge/frame");
    state.gridView=false; draw(decorated); state.gridView=true;
    Check(pixel(left+48,100)==std::array<BYTE,4>{40,80,220,255} &&
        pixel(left+48,120)[3]==0,"list fallback displayed thumbnail atlas badge band");
    const bool listThumbnails=g_listThumbnails.exchange(true);
    state.gridView=false; state.iconPx=32; draw(decorated);
    const int listLeft=state.rtlLayout ? 320-40-32 : 40;
    Check(pixel(listLeft+16,56)[3]>240 && pixel(listLeft,49)[3]<230 &&
        pixel(listLeft+16,65)[3]>0 && pixel(listLeft+16,65)[3]<80,
        "list thumbnail lost aspect, rounded corners or cheap shadow at icon size");
    // The same opaque 2:1 image isolates small-thumbnail corner geometry from
    // film rails/badges. A quarter-edge sample must retain a straight top;
    // half-edge radii turn this area into the end of a pill.
    plain.pixels.thumbnailWidth=image.width; plain.pixels.thumbnailHeight=image.height;
    for(int dpi : {96,192}) for(int size : {16,32}) {
        state.textFormatDpi=dpi; state.iconPx=static_cast<float>(size*dpi/96);
        draw(plain);
        const int left=static_cast<int>(state.rtlLayout ? 320-40-state.iconPx : 40);
        const int height=static_cast<int>(state.iconPx*.5f), top=40+height/2;
        Check(pixel(left,top)[3]<200 && pixel(left+height/4,top)[3]>240,
            "small thumbnail lost visible rounding or acquired pill-shaped ends");
    }
    plain.pixels.thumbnailWidth=plain.pixels.thumbnailHeight=0;
    state.textFormatDpi=96;
    g_listThumbnails=listThumbnails; state.gridView=true; state.iconPx=96;
    // A type that opts out of the badge allocates only its thumbnail image.
    auto unadorned=DecorateThumbnail(IconPixels(image),ThumbnailStyle{});
    Check(unadorned.width==80 && unadorned.height==40 && !unadorned.badgeSize,
        "unbadged thumbnail retained an empty badge allocation");
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    state.iconPx=48;
    for (IconRecord* record : {&plain,&decorated}) {
        std::vector<double> times;
        for (int frame=0;frame<35;++frame) {
            LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
            for (int row=0;row<6;++row) for (int column=0;column<6;++column)
                DrawCachedIcon(state,*record,column*53.f,row*53.f);
            Check(SUCCEEDED(state.target->EndDraw()),"thumbnail profile submission");
            QueryPerformanceCounter(&end);
            if(frame>=5) times.push_back(1000.*(end.QuadPart-begin.QuadPart)/frequency.QuadPart);
        }
        std::sort(times.begin(),times.end());
        std::cout << "PROFILE: 36 cached thumbnails decorated=" << (record==&decorated) <<
            " rtl=" << state.rtlLayout << " CPU submit+EndDraw median=" << times[times.size()/2] <<
            " p95=" << times[times.size()*95/100] << " ms (offscreen, no scan-out)\n";
    }
    state.iconPx=oldSize; state.paintWidth=oldPaint; state.textFormatDpi=oldDpi;
    state.target->SetTarget(original.Get());
}

static void TestGridFilenameSelection(ViewState& state) {
    const auto enabled=g_filenameOnly.exchange(ViewOption::AlwaysOn);
    const Entry first=state.entries[0],second=state.entries[1];
    const auto flags=state.selectedFlags;
    const int selected=state.selected,hovered=state.hovered;
    const auto search=std::move(state.typeSearch); state.typeSearch.clear();
    state.entries[0].name=L"Install macOS 27 release Beta"; state.entries[0].path=L"grid-label-fixture:0";
    state.entries[1].name=L"Short"; state.entries[1].path=L"grid-label-fixture:1";
    state.selectedFlags.assign(state.total,0); state.selectedFlags[0]=1;
    state.selected=0; state.hovered=-1;
    SelectionShape shape;
    const auto geometry=LayoutItem(state,{0,state.rowPx,1,DocumentRowLeft(state,0)});
    const auto bounds=SelectionRect(state,0,0,state.rowPx,0,0,ListEndX(state),
        DocumentRowLeft(state,0),0,&shape);
    const float padding=2.f*DpiOf(state.hwnd)/96.f+.5f;
    if (!(shape.count && (geometry.wrapped ? shape.count>=2 : shape.count==1) &&
        bounds.top>=geometry.text.top-padding && bounds.bottom<=geometry.text.bottom+padding))
        std::cerr << "LABEL mode=" << state.cachedNativeMode << " rtl=" << state.rtlLayout <<
            " lines=" << shape.count << " font=" << state.fontPx << " row=" << state.rowPx <<
            " text=" << geometry.text.left << ',' << geometry.text.top << ',' << geometry.text.right << ',' << geometry.text.bottom <<
            " bounds=" << bounds.left << ',' << bounds.top << ',' << bounds.right << ',' << bounds.bottom << '\n';
    Check(shape.count && (geometry.wrapped ? shape.count>=2 : shape.count==1) &&
        bounds.top>=geometry.text.top-padding && bounds.bottom<=geometry.text.bottom+padding,
        "filename-only grid bounds included icon or failed to follow wrapped lines");
    const auto& cached=state.textLayouts[geometry.wrapped ? 3 : 0].layouts.at(state.entries[0].name);
    const auto* layout=cached.text.Get();
    SelectionShape translated;
    SelectionRect(state,0,11.25f,state.rowPx,0,0,ListEndX(state),
        DocumentRowLeft(state,0),0,&translated);
    Check(cached.filenameSelectionReady && cached.text.Get()==layout &&
        translated.count==shape.count,"translated selection rebuilt the cached label shape");
    ComPtr<ID2D1Bitmap1> cpu;
    const auto size=state.backbuffer->GetPixelSize();
    const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|
        D2D1_BITMAP_OPTIONS_CANNOT_DRAW,state.backbuffer->GetPixelFormat());
    Check(SUCCEEDED(state.target->CreateBitmap(size,nullptr,0,&props,&cpu)),"label selection readback");
    auto paint=[&] {
        Paint(state);
        Check(SUCCEEDED(cpu->CopyFromBitmap(nullptr,state.backbuffer.Get(),nullptr)),"label selection copy");
    };
    auto alpha=[&](float logicalX,float y) {
        const int x=static_cast<int>(state.rtlLayout ? state.paintWidth-logicalX : logicalX);
        D2D1_MAPPED_RECT map{};
        Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"label selection map");
        const int value=map.bits[static_cast<int>(y)*map.pitch+x*4+3];
        cpu->Unmap(); return value;
    };
    paint();
    const float iconX=(geometry.icon.left+geometry.icon.right)*.5f;
    const float iconY=(geometry.icon.top+geometry.icon.bottom)*.5f;
    Check(alpha(iconX,iconY)==0,"filename-only full Paint highlighted the icon cell");
    POINT background{};
    for(unsigned i=0;i<shape.count;++i) {
        const auto& line=shape.lines[i];
        bool found=false;
        // A trimmed label can start at its clip edge with no side padding.
        // Find background coverage, rather than sampling a letter's ink.
        for(int y=static_cast<int>(ceilf(line.top))+1; !found && y<line.bottom-1; ++y)
            for(int x=static_cast<int>(ceilf(line.left))+1; x<line.right-1; ++x)
                if (alpha(x+.5f,y+.5f)==102) {
                    found=true; if(!i) background={x,y}; break;
                }
        if(!found) std::cerr << "LABEL alpha mode=" << state.cachedNativeMode <<
            " rtl=" << state.rtlLayout << " part=" << i << " rect=" << line.left << ',' <<
            line.top << ',' << line.right << ',' << line.bottom << '\n';
        Check(found,"selected filename line lacks the configured selection background");
    }
    if(shape.count>1) {
        const auto shortest=std::min_element(shape.lines.begin(),shape.lines.begin()+shape.count,
            [](const auto& a,const auto& b) {return a.right-a.left<b.right-b.left;});
        Check(shortest->left>bounds.left+2.f || shortest->right<bounds.right-2.f,
            "wrapped filename selection used the whole label block width");
        const float outside=shortest->left>bounds.left+2.f ? bounds.left+1 : bounds.right-1;
        Check(alpha(outside,(shortest->top+shortest->bottom)*.5f)==0,
            "shorter wrapped line inherited the wider line's background");
    }
    state.hovered=0; paint();
    Check(abs(alpha(background.x+.5f,background.y+.5f)-179)<=3,
        "filename-only hover and selection no longer compose");
    Check(alpha(iconX,iconY)==0,"hover reintroduced whole-cell highlighting");
    if (geometry.wrapped && !state.rtlLayout) SaveGridPreview(state,L"GPU_OVERLAY_LABEL_PREVIEW");
    state.selectedFlags[0]=0; paint();
    Check(abs(alpha(background.x+.5f,background.y+.5f)-51)<=3,
        "hover-only filename highlight is missing");
    BeginHoverTransition(state,-1,true); state.hovered=-1; paint();
    Check(alpha(iconX,iconY)==0,"pointer-leave animation reverted to the icon cell");
    StopHoverAnimation(state);
    state.selected=1; state.selectedFlags[1]=1;
    StartKeyboardSelectionAnimation(state,1,0,bounds.left,bounds.right); paint();
    Check(alpha(iconX,iconY)==0,"moving keyboard highlight reverted to the icon cell");
    StopKeyboardSelectionAnimation(state);
    g_filenameOnly=ViewOption::AlwaysOff; state.selected=0; state.selectedFlags[0]=1; state.selectedFlags[1]=0; paint();
    Check(abs(alpha(iconX,iconY)-102)<=3,"disabled filename-only setting lost whole-cell selection");
    for(auto policy : {ViewOption::GridOnly,ViewOption::AlwaysOn,ViewOption::AlwaysOff}) {
        g_filenameOnly=policy; paint();
        Check((alpha(iconX,iconY)==0)==(policy!=ViewOption::AlwaysOff),
            "filename selection dropdown did not scope grid highlights");
        state.gridView=false;
        const auto row=RowHighlightRect(state,0,state.rowPx);
        const auto selectedRow=SelectionRect(state,0,0,state.rowPx,NameTextOrigin(state),
            ListEndX(state)-8,ListEndX(state));
        Check((selectedRow.left>row.left)==(policy==ViewOption::AlwaysOn),
            "filename selection dropdown did not scope list highlights");
        state.gridView=true;
    }
    state.entries[0]=first; state.entries[1]=second;
    state.selectedFlags=flags; state.selected=selected; state.hovered=hovered;
    state.typeSearch=search; g_filenameOnly=enabled;
}

// Exercise the shared item geometry and production paint/selection paths.
static void TestIconGrid(HWND frame, const std::wstring& fixture) {
    const auto filenamePolicy=g_filenameOnly.exchange(ViewOption::AlwaysOff);
    const int radius = g_selectionRadius.exchange(0);
    const int gridRadius = g_gridSelectionRadius.exchange(0);
    const int selection = g_selectionAlpha.exchange(40), hover = g_hoverAlpha.exchange(20);
    const int both = g_bothAlpha.exchange(70);
    const auto picture = fixture+L"\\grid-fixture.bmp";
    BITMAPFILEHEADER fileHeader{}; BITMAPINFOHEADER info{};
    std::vector<BYTE> image(160*80*3);
    for (int y=0;y<80;++y) for (int x=0;x<160;++x) {
        const int offset=(y*160+x)*3;
        image[offset]=static_cast<BYTE>(40+x); image[offset+1]=static_cast<BYTE>(60+y*2); image[offset+2]=220;
    }
    fileHeader.bfType=0x4D42; fileHeader.bfOffBits=sizeof(fileHeader)+sizeof(info);
    fileHeader.bfSize=fileHeader.bfOffBits+static_cast<DWORD>(image.size());
    info.biSize=sizeof(info); info.biWidth=160; info.biHeight=80; info.biPlanes=1;
    info.biBitCount=24; info.biSizeImage=static_cast<DWORD>(image.size());
    HANDLE file=CreateFileW(picture.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD written=0;
    Check(file!=INVALID_HANDLE_VALUE && WriteFile(file,&fileHeader,sizeof(fileHeader),&written,nullptr) &&
        WriteFile(file,&info,sizeof(info),&written,nullptr) &&
        WriteFile(file,image.data(),static_cast<DWORD>(image.size()),&written,nullptr), "grid bitmap fixture");
    CloseHandle(file);
    for (bool rtl : {false, true}) {
        ViewState state;
        state.hwnd = CreateWindowExW(0, L"STATIC", L"grid-fixture", WS_CHILD,
            0, 0, 640, 480, frame, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(state.hwnd != nullptr, "grid window");
        state.frameHwnd = frame; state.nativeHwnd = state.activeTabHost = state.hwnd;
        FocusFixture::Session focus(state.frameHwnd,state.nativeHwnd);
        state.background->hwnd = state.hwnd;
        state.gridView = state.visible = state.inScope = state.loaded = true;
        state.columnsView = false; state.rtlLayout = rtl;
        state.total = 113; state.entries.resize(state.total);
        for (int i=0; i<state.total; ++i) {
            state.entries[i].name = L"Flower in the park - a long filename " + std::to_wstring(i) + L".txt";
            state.entries[i].path = fixture + L"\\file" + std::to_wstring(i) + L".txt";
        }
        state.selectedFlags.assign(state.total, 0);
        state.entries[0].name=L"Flower panorama.bmp"; state.entries[0].path=picture;
        UpdateRefreshRate(state);
        Check(EnsureTarget(state), "grid GPU target");
        auto savedSwap = std::move(state.swapChain);
        auto savedBitmap = std::move(state.backbuffer);
        const auto targetProps = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            savedBitmap->GetPixelFormat());
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(640,480), nullptr, 0,
            &targetProps, &state.backbuffer)), "grid fixed target");
        state.target->SetTarget(state.backbuffer.Get());
        for (int size : {16,48,96,256}) {
            state.cachedNativeMode = size == 16 ? FVM_SMALLICON : FVM_ICON;
            state.nativeIconSize = size;
            state.scrollNow = state.scrollTarget = 0.f;
            UpdateMetrics(state);
            Check(state.itemsPerRow >= 2 && state.rowPx > size &&
                DocumentRowTop(state, 1) == 0.f && DocumentRowLeft(state, 1) > 0.f &&
                DocumentRowTop(state, state.itemsPerRow) == state.rowPx,
                "grid item geometry did not pack rows/cells");
            Check(DocumentHeight(state) == ItemLineCount(state, state.total)*state.rowPx,
                "grid scroll extent counted individual files as full rows");
            for (int index : {0,1,state.itemsPerRow,state.total-1}) {
                const float logical = ListStartX(state)+DocumentRowLeft(state,index)+state.itemWidth*.5f;
                const int x = static_cast<int>(rtl ? 640.f-logical : logical);
                const int y = static_cast<int>(DocumentRowTop(state,index)+state.rowPx*.5f);
                if (y < 480) Check(RowFromPoint(state,y,x) == index, "grid pointer hit selected another cell");
            }
            Check(VisibleItemRows(state,0).size() <=
                static_cast<size_t>((480/state.rowPx+2)*state.itemsPerRow), "grid scanned offscreen items");
            state.selected = 1;
            Check(KeyboardNavigationTarget(state,VK_DOWN) == 1+state.itemsPerRow &&
                KeyboardNavigationTarget(state,rtl ? VK_LEFT : VK_RIGHT) == 2 &&
                KeyboardNavigationTarget(state,VK_END) == state.total-1,
                "grid keyboard directions did not follow cell geometry/RTL");
            state.selectedFlags.assign(state.total,0); state.selectedFlags[0] = 1;
            state.selected = 0; state.hovered = 1;
            state.typeSearch = L"Flower"; state.lastTyped = GetTickCount64();
            Paint(state);
            if (size != 16) {
                auto label = state.textLayouts[3].layouts.find(state.entries[0].name);
                Check(label != state.textLayouts[3].layouts.end() &&
                    label->second.text->GetWordWrapping() == DWRITE_WORD_WRAPPING_WRAP &&
                    label->second.text->GetTextAlignment() == DWRITE_TEXT_ALIGNMENT_CENTER,
                    "icon labels did not reuse a centered/wrapped cached layout");
            }
            const auto labelGeometry = LayoutItem(state,{0,state.rowPx,1,DocumentRowLeft(state,1)});
            const RECT rename = RenameEditorRect(state,1);
            POINT origin{rename.left,rename.top}; MapWindowPoints(frame,state.hwnd,&origin,1);
            Check(origin.y >= labelGeometry.text.top && rename.right-rename.left <=
                labelGeometry.text.right-labelGeometry.text.left+1.f,
                "grid rename covered the icon or escaped its label cell");
            std::wstring path; bool attrs = false;
            const auto key = IconKey(state,state.entries[0],path,attrs);
            Check(size <= 32 ? key == L"type:.bmp" : key.find(L"@") != std::wstring::npos,
                "different icon resolutions aliased the same cache key");
            if (size == 48) {
                ComPtr<ID2D1Bitmap1> pixel;
                const auto props = D2D1::BitmapProperties1(
                    D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                    state.backbuffer->GetPixelFormat());
                Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1,1),nullptr,0,&props,&pixel)),
                    "grid alpha readback");
                auto alpha = [&](int index) {
                    const float logical = ListStartX(state)+DocumentRowLeft(state,index)+state.itemWidth*.5f;
                    const UINT x = static_cast<UINT>(rtl ? 640.f-logical : logical);
                    const auto rect = D2D1::RectU(x,4,x+1,5);
                    Check(SUCCEEDED(pixel->CopyFromBitmap(nullptr,state.backbuffer.Get(),&rect)),"grid pixel copy");
                    D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(pixel->Map(D2D1_MAP_OPTIONS_READ,&map)),"grid pixel map");
                    const int a = map.bits[3]; pixel->Unmap(); return a;
                };
                Check(abs(alpha(0)-102) <= 3 && abs(alpha(1)-51) <= 3,
                    "hover/selection on adjacent grid cells affected each other's backgrounds");
                state.hovered=0; Paint(state);
                Check(abs(alpha(0)-179) <= 3, "overlapping grid hover/selection did not compose both backgrounds");
                state.hovered=1;
            }
            if (size == 96) {
                const std::unordered_map<std::wstring,unsigned> wanted{{key,0}};
                Check(RequestEntryIcon(state,state.entries[0],false,wanted), "large icon request");
                Check(Eventually([&] { ReceiveIcons(state,false); auto* record=FindIconRecord(state,key);
                    return record && record->resolved; }), "large icon worker completion");
                const auto* record = FindIconRecord(state,key);
                Check(record->pixels.Ready() && record->pixels.width > 32,
                    "large grid received only the legacy small icon image");
                Check(record->pixels.thumbnailWidth && record->pixels.thumbnailHeight,
                    "Shell thumbnail-only result was not distinguished from an icon");
                Check(record->pixels.width > record->pixels.height,
                    "large grid requested an icon instead of the Shell image thumbnail");
                auto* ready=FindIconRecord(state,key);
                Check(UploadIconBitmap(state,*ready), "thumbnail GPU upload");
                Paint(state); if (!rtl) SaveGridPreview(state);
                TestThumbnailDecorations(state);
                const bool listThumbnails=g_listThumbnails.exchange(true);
                state.gridView=false; const float gridIcon=state.iconPx; state.iconPx=20;
                const auto listKey=IconKey(state,state.entries[0],path,attrs);
                Check(!attrs && listKey!=key && path==picture,
                    "list thumbnails shared a type icon key instead of the file's image identity");
                const std::unordered_map<std::wstring,unsigned> listWanted{{listKey,0}};
                Check(RequestEntryIcon(state,state.entries[0],false,listWanted),"cold list thumbnail request");
                Check(Eventually([&] { ReceiveIcons(state,false); auto* image=FindIconRecord(state,listKey);
                    return image && image->resolved; }),"list thumbnail worker completion");
                const auto* listImage=FindIconRecord(state,listKey);
                Check(listImage->pixels.thumbnailWidth && listImage->pixels.thumbnailWidth<=32 &&
                    listImage->pixels.thumbnailHeight<listImage->pixels.thumbnailWidth &&
                    ready->bitmap && ready->pixels.thumbnailWidth>32,
                    "32px list request did not fetch a real thumbnail, or modified the grid image");
                g_listThumbnails=false;
                const auto iconKey=IconKey(state,state.entries[0],path,attrs);
                Check(attrs && iconKey==L"type:.bmp","disabled list thumbnails did not return to normal icon demand");
                g_listThumbnails=listThumbnails; state.gridView=true; state.iconPx=gridIcon;
                const float previousSize=state.iconPx; state.iconPx=256;
                const auto larger=IconKey(state,state.entries[0],path,attrs);
                Check(!FindIconRecord(state,larger) && FindIconRecord(state,larger,true)==ready,
                    "changing icon sizes discarded a usable cached image");
                auto& replacement = state.icons.try_emplace(larger).first->second;
                replacement.pixels = ready->pixels;
                replacement.resolved = true;
                Check(FindIconRecord(state,larger,true)==&replacement &&
                    FindIconRecord(state,larger,true,true)==ready,
                    "CPU-ready replacement displaced the displayed GPU bitmap before upload");
                const auto uploads=VisibleIconUploads(state);
                Check(std::find(uploads.begin(),uploads.end(),&replacement)!=uploads.end(),
                    "displayed fallback prevented the higher-resolution texture from uploading");
                Check(UploadIconBitmap(state,replacement) &&
                    FindIconRecord(state,larger,true,true)==&replacement,
                    "GPU-ready image did not replace the scaled fallback");
                state.iconPx=previousSize;
                state.icons.try_emplace(larger).first->second.pending=true;
                InvalidateShellIconKey(state,state.entries[0].path);
                Check(!FindIconRecord(state,key) && !FindIconRecord(state,larger),
                    "item update left stale image-size variants in the cache");
            }
            if (size==16 || size==96) TestGridFilenameSelection(state);
        }
        state.cachedNativeMode = FVM_ICON; state.nativeIconSize = 48;
        state.hovered = state.selected = -1; state.typeSearch.clear();
        UpdateMetrics(state);
        const int columns = state.itemsPerRow;
        auto physical = [&](float x, float y) {
            return POINT{static_cast<LONG>(rtl ? 640.f-x : x), static_cast<LONG>(y)};
        };
        const POINT start = physical(ListStartX(state)+state.itemWidth*1.1f, state.rowPx*.1f);
        PrepareMarquee(state,start,false); state.marqueeActive = true;
        UpdateMarquee(state,physical(ListStartX(state)+state.itemWidth*2.9f,state.rowPx*2.9f));
        for (int i=0; i<state.total; ++i)
            Check(static_cast<bool>(state.selectedFlags[i]) ==
                (i/columns < 3 && i%columns >= 1 && i%columns <= 2),
                "marquee selected whole grid rows instead of its cell rectangle");
        UpdateMarquee(state,physical(ListStartX(state)+state.itemWidth*1.9f,state.rowPx*2.9f));
        for (int i=0; i<state.total; ++i)
            Check(static_cast<bool>(state.selectedFlags[i]) == (i/columns < 3 && i%columns == 1),
                "horizontal marquee shrink left selected holes/extra columns");
        const POINT edge=physical(ListStartX(state)+state.itemWidth*1.9f,500.f);
        UpdateMarqueeAutoscroll(state,edge);
        for (int i=0;i<12;++i) {
            QueryPerformanceCounter(&state.marqueeLastScrollQpc);
            state.marqueeLastScrollQpc.QuadPart-=static_cast<LONGLONG>(state.qpcFrequency/60.);
            UpdateMarqueeAutoscroll(state,edge,true);
        }
        Check(state.scrollNow > 0.f && state.selected >= 0 && state.selectedFlags[state.selected] &&
            std::count(state.selectedFlags.begin(),state.selectedFlags.end(),1) > 3,
            "grid marquee edge scroll lost selection/focus");
        state.marqueePossible = state.marqueeActive = false;
        state.marqueeFrameActive=false; state.scrollNow=state.scrollTarget=0.f;
        state.sections.clear(); state.rowGroup.assign(state.total,-1);
        ViewState::GroupSection group; group.first=0; group.end=5;
        state.sections.push_back(group); group.first=5; group.end=state.total;
        state.sections.push_back(group);
        for (int i=0;i<state.total;++i) state.rowGroup[i]=i<5?0:1;
        LayoutGroupSections(state);
        Check(DocumentRowLeft(state,5)==0.f && DocumentRowTop(state,5)==
            state.sections[1].documentTop+GroupHeadingHeight(state), "group grid did not restart at its first cell");
        state.selected=1;
        Check(KeyboardNavigationTarget(state,VK_DOWN)==6,
            "grid arrows changed columns when crossing an incomplete group row");
        state.sections[0].collapsed=true; state.sections[0].fraction=0; LayoutGroupSections(state);
        Check(!DisplayedRow(state,0) && RowAtDocument(state,DocumentRowTop(state,5)+1.f,1.f)==5,
            "collapsed groups left grid hit targets/gaps");
        state.sections.clear(); state.rowGroup.clear(); LayoutGroupSections(state);
        state.selected = -1;
        SetScrollImmediate(state,500.f);
        const float anchor=(state.scrollNow+ViewHeight(state)*.5f)/state.rowPx*state.itemsPerRow;
        const float icon=state.iconPx;
        const int oldColumns=state.itemsPerRow;
        const int moving=VisibleItemRows(state,0.f).front()+oldColumns-1;
        const auto from=LayoutRowVisual(state,moving,1.f);
        const float fromScroll=state.scrollNow;
        ApplyRowHeight(state,80);
        const auto captured=LayoutRowVisual(state,moving,0.f);
        Check(state.zoomLayoutTransition && state.itemsPerRow<oldColumns &&
            fabsf(captured.top-from.top)<.1f && fabsf(captured.left-from.left)<.1f &&
            fabsf(captured.width-from.width)<.1f && state.scrollNow==fromScroll,
            "zoom repacking snapped cells/width/viewport instead of capturing the displayed layout");
        QueryPerformanceCounter(&state.zoomStartedQpc);
        state.zoomStartedQpc.QuadPart-=static_cast<LONGLONG>(state.qpcFrequency*.09);
        state.rowTransitionStartedQpc=state.zoomStartedQpc;
        AdvanceRowHeight(state);
        Check(state.zoomAnimating && state.iconPx > icon,
            "grid zoom skipped its shared intermediate animation");
        const auto halfway=LayoutRowVisual(state,moving,.875f);
        Check(fabsf(halfway.left-from.left)>.1f &&
            fabsf(halfway.left-DocumentRowLeft(state,moving))>.1f,
            "overflow cell did not interpolate between its old and new columns");
        const auto beforeRetarget=LayoutRowVisual(state,moving,LayoutTransitionProgress(state,true,state.rowTransitionStartedQpc));
        ApplyRowHeight(state,90);
        const auto retarget=LayoutRowVisual(state,moving,0.f);
        Check(fabsf(retarget.left-beforeRetarget.left)<3.f &&
            fabsf(retarget.top-beforeRetarget.top)<3.f,
            "continuous zoom restarted cell motion at a stale layout");
        AdvanceRowHeight(state,true);
        const float after=(state.scrollNow+ViewHeight(state)*.5f)/state.rowPx*state.itemsPerRow;
        Check(!state.zoomAnimating && fabsf(after-anchor)<.05f,
            "grid zoom did not preserve its document item anchor through repacking");
        // A selection near either edge must stay visible through changing
        // column counts, not just reappear after the final frame.
        state.selected = 75;
        state.selectedFlags.assign(state.total, 0);
        state.selectedFlags[state.selected] = 1;
        for (bool grouped : {false, true}) {
            state.grouping.enabled = grouped;
            for (int i=0; i<state.total; ++i)
                state.entries[i].groupKey = i<25 ? L"first" : L"second";
            RebuildGroupSections(state);
            state.rowHeight96[state.gridView] = 34;
            UpdateMetrics(state);
            for (int destination : {80,24}) {
                const bool growing = destination == 80;
                SetScrollImmediate(state, DocumentRowTop(state,state.selected) +
                    (growing ? state.rowPx-ViewHeight(state)+2.f : -2.f));
                const auto start = LayoutRowVisual(state,state.selected,1.f);
                const float oldScroll = state.scrollNow;
                ApplyRowHeight(state,destination);
                Check(state.zoomAnimating && state.scrollNow == oldScroll &&
                    fabsf(LayoutRowVisual(state,state.selected,0.f).top-start.top)<.1f,
                    "selection-aware grid zoom snapped its starting viewport");
                const auto began = state.zoomStartedQpc;
                for (double ms : {18.,54.,90.,135.,179.,181.}) {
                    state.zoomStartedQpc = began;
                    state.zoomStartedQpc.QuadPart -= static_cast<LONGLONG>(
                        ms*state.qpcFrequency/1000.);
                    state.rowTransitionStartedQpc = state.zoomStartedQpc;
                    AdvanceRowHeight(state);
                    const float progress = state.zoomAnimating ? EaseOutCubic(
                        UiTransitionProgress(state,state.zoomStartedQpc)) : 1.f;
                    const auto visual = LayoutRowVisual(state,state.selected,progress);
                    if (visual.top < state.headerPx-.5f ||
                        visual.top+visual.height > state.headerPx+ViewHeight(state)+.5f)
                        std::cerr << "GRID ZOOM rtl=" << rtl << " grouped=" << grouped <<
                            " destination=" << destination << " ms=" << ms <<
                            " progress=" << progress << " top=" << visual.top <<
                            " height=" << visual.height << " scroll=" << state.scrollNow <<
                            " limit=" << MaxScrollPx(state) << " viewport=" << ViewHeight(state) << "\n";
                    Check(visual.top >= state.headerPx-.5f &&
                        visual.top+visual.height <= state.headerPx+ViewHeight(state)+.5f,
                        "grid zoom/repacking moved selection outside the viewport");
                }
                Check(!state.zoomAnimating && !state.animationActive,
                    "selection reveal started an independent scroll animation");
            }
        }
        state.grouping.enabled = false; RebuildGroupSections(state);
        // A caret without selection must not override the center anchor.
        state.selectedFlags[state.selected] = 0;
        SetScrollImmediate(state,500.f);
        const float unselectedAnchor=(state.scrollNow+ViewHeight(state)*.5f)/
            state.rowPx*state.itemsPerRow;
        ApplyRowHeight(state,50); AdvanceRowHeight(state,true);
        Check(fabsf((state.scrollNow+ViewHeight(state)*.5f)/state.rowPx*
            state.itemsPerRow-unselectedAnchor)<.05f,
            "unselected caret stole the zoom viewport");
        // The display can exceed both the old zoom and texture-size limits.
        // Captions and provider request tiers must stop growing independently.
        const auto windowData=SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(&state));
        const auto listZoom=state.rowHeight96[false];
        ApplyRowHeight(state,96); AdvanceRowHeight(state,true);
        const float cappedFont=state.fontPx, previousIcon=state.iconPx;
        const auto cappedFormat=state.textFormat.Get();
        OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,4*WHEEL_DELTA),0);
        Check(state.rowHeight96[true]==104 && state.zoomAnimating && state.iconPx==previousIcon,
            "grid Ctrl+wheel stopped at the old cap or snapped its geometry");
        QueryPerformanceCounter(&state.zoomStartedQpc);
        state.zoomStartedQpc.QuadPart-=static_cast<LONGLONG>(state.qpcFrequency*.09);
        state.rowTransitionStartedQpc=state.zoomStartedQpc;
        AdvanceRowHeight(state);
        Check(state.iconPx>previousIcon && state.iconPx<state.zoomToIconPx &&
            state.fontPx==cappedFont && state.textFormat.Get()==cappedFormat,
            "extended grid zoom lost interpolation or rebuilt capped caption formats");
        AdvanceRowHeight(state,true);
        ApplyRowHeight(state,512); AdvanceRowHeight(state,true);
        const float largeIcon=state.iconPx;
        const auto imageKey=[&] {
            std::wstring lookup; bool attrs=false;
            return IconKey(state,state.entries[0],lookup,attrs);
        };
        const auto largeKey=imageKey();
        Check(largeIcon>512 && state.itemsPerRow==1 && IconRequestSize(state)==512 &&
            state.fontPx==cappedFont && state.rowHeight96[false]==listZoom,
            "extra-large grid image stayed capped, grew its caption/requests or changed list zoom");
        Paint(state);
        Check(state.target && state.fontPx==cappedFont,"oversized grid paint lost its target/caption");
        state.manipulationEnabled=true; state.touchpadPinching=false;
        OverlayProc(state.hwnd,kMsgManipulationPinch,0,
            static_cast<LPARAM>(std::lround(logf(1.2f)*65536.f)));
        AdvanceRowHeight(state,true);
        Check(state.rowHeight96[true]>512 && state.iconPx>largeIcon &&
            state.fontPx==cappedFont && imageKey()==largeKey,
            "extended grid pinch hit a second cap or allocated a larger image tier");
        state.manipulationEnabled=state.touchpadPinching=false;
        const int gridZoom=state.rowHeight96[true];
        state.gridView=false;
        ApplyRowHeight(state,512); AdvanceRowHeight(state,true);
        OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA),0);
        Check(state.rowHeight96[false]==96 && state.rowPx==Scale96(96,DpiOf(state.hwnd)) &&
            !state.zoomAnimating && state.rowHeight96[true]==gridZoom,
            "extended grid zoom removed the list cap or leaked between view families");
        state.rowHeight96[false]=listZoom; state.gridView=true;
        ApplyRowHeight(state,24); AdvanceRowHeight(state,true);
        Check(state.rowHeight96[true]==24 && state.fontPx<cappedFont,
            "extended zoom could not return to its minimum caption/image size");
        ApplyRowHeight(state,50); AdvanceRowHeight(state,true);
        SetWindowLongPtrW(state.hwnd,GWLP_USERDATA,windowData);
        state.scrollNow=state.scrollTarget=0.f;
        std::vector<CapturedRow> capture(1);
        capture[0].oldIndex=1; capture[0].newIndex=0; capture[0].oldTop=0;
        capture[0].oldHeight=state.rowPx; capture[0].oldLeft=state.itemWidth;
        StartRowTransition(state,std::move(capture),state.total+1,state.rowPx,0);
        Check(LayoutRowVisual(state,0,.5f).left > 0.f &&
            LayoutRowVisual(state,0,.5f).left < state.itemWidth,
            "grid deletion lost horizontal item movement");
        StopRowTransition(state); UnregisterAnimationWindow(state.hwnd);
        g_selectionRadius=3; g_gridSelectionRadius=11;
        Check(SelectionRadius(state,100,100)==Scale96(11,DpiOf(state.hwnd)),
            "grid corner radius did not use its own setting");
        state.gridView=false;
        Check(SelectionRadius(state,100,100)==Scale96(3,DpiOf(state.hwnd)),
            "grid corner radius changed list corner radius");
        g_selectionRadius=0; g_gridSelectionRadius=0;
        // This PC uses the same grid packing/input and themed bar renderer,
        // but reserves a horizontal icon and two lines before enrichment.
        state.thisPcGpu=state.gridView=true; state.columnsView=false;
        state.total=4; state.entries.assign(4,Entry{});
        state.rowHeight96[true]=0; state.nativeIconSize=48;
        state.selected=0; state.selectedFlags={1,0,0,0}; state.hovered=1;
        state.typeSearch.clear(); state.scrollNow=state.scrollTarget=0;
        const wchar_t* roots[]={L"C:\\",L"D:\\",L"F:\\",L"Z:\\"};
        for(int i=0;i<4;++i) {
            auto& entry=state.entries[i]; entry.path=roots[i]; entry.folder=true;
            entry.name=L"Local disk ("+std::wstring(1,roots[i][0])+L":)";
            entry.propertyEpoch=state.propertyEpoch;
        }
        state.entries[0].driveTotal=1000; state.entries[0].driveUsed=750;
        state.entries[0].cells.push_back({PKEY_Computer_DecoratedFreeSpace,L"250 GB free of 1 TB"});
        state.entries[2].driveUsedPercent=35; // Device with no byte count.
        for(UINT mode : {FVM_TILE,FVM_ICON,FVM_SMALLICON,FVM_THUMBNAIL}) {
            state.cachedNativeMode=mode; UpdateMetrics(state); MakeTextFormats(state,true);
            const auto item=LayoutItem(state,LayoutRowVisual(state,0,1.f));
            const auto bar=LayoutDriveBar(state,0,item);
            Check(state.itemsPerRow==2 && !item.wrapped && item.text.left>=item.icon.right &&
                item.usage.top>=item.text.bottom && item.capacity.top>=item.usage.bottom &&
                item.capacity.bottom<item.cell.bottom && item.capacity.right<=item.cell.right &&
                bar.visible && bar.labelRight==item.text.right,
                "drive tiles lost their shared two-line horizontal layout");
            Check(RowAtDocument(state,0,state.itemWidth*1.5f)==1 &&
                DocumentRowTop(state,2)==state.rowPx && KeyboardNavigationTarget(state,VK_DOWN)==2,
                "drive tiles diverged from grid hit testing, packing or keyboard movement");
            const auto pending=LayoutItem(state,LayoutRowVisual(state,1,1.f));
            Check(!LayoutDriveBar(state,1,pending).visible &&
                LayoutDriveBar(state,2,LayoutItem(state,LayoutRowVisual(state,2,1.f))).visible,
                "unknown capacity produced a bar, or percent-only storage lost its bar");
            state.entries[1].driveTotal=1000; state.entries[1].driveUsed=400;
            state.entries[1].cells={{PKEY_Computer_DecoratedFreeSpace,L"600 GB free of 1 TB"}};
            const auto ready=LayoutItem(state,LayoutRowVisual(state,1,1.f));
            Check(pending.icon.left==ready.icon.left && pending.text.top==ready.text.top &&
                pending.cell.right==ready.cell.right && pending.capacity.bottom==ready.capacity.bottom,
                "late capacity changed tile geometry instead of just its content");
            Paint(state);
            ComPtr<ID2D1Bitmap1> pixel;
            const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ |
                D2D1_BITMAP_OPTIONS_CANNOT_DRAW,state.backbuffer->GetPixelFormat());
            Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1,1),nullptr,0,&props,&pixel)),
                "drive bar readback");
            auto alpha=[&](float fraction) {
                const float logical=bar.track.left+(bar.track.right-bar.track.left)*fraction;
                const UINT x=static_cast<UINT>(rtl ? 640.f-logical : logical);
                const UINT y=static_cast<UINT>((bar.track.top+bar.track.bottom)*.5f);
                const auto source=D2D1::RectU(x,y,x+1,y+1);
                Check(SUCCEEDED(pixel->CopyFromBitmap(nullptr,state.backbuffer.Get(),&source)),"drive bar pixels");
                D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(pixel->Map(D2D1_MAP_OPTIONS_READ,&map)),"drive bar map");
                const int value=map.bits[3]; pixel->Unmap(); return value;
            };
            Check(alpha(.25f)>alpha(.9f)+20,"drive usage fill did not render over its themed track in LTR/RTL");
            state.entries[1].driveTotal=state.entries[1].driveUsed=0; state.entries[1].cells.clear();
        }
        wchar_t tilePreview[MAX_PATH]{};
        if(GetEnvironmentVariableW(L"GPU_OVERLAY_DRIVE_TILE_PREVIEW",tilePreview,ARRAYSIZE(tilePreview))) {
            // Preview uses the real drive-icon worker; pixels/tests above
            // remain independent of drive readiness or provider timing.
            state.entries[1].driveTotal=1000; state.entries[1].driveUsed=400;
            state.entries[1].cells={{PKEY_Computer_DecoratedFreeSpace,L"600 GB free of 1 TB"}};
            RequestVisibleIcons(state);
            Check(Eventually([&] {
                ReceiveIcons(state,false);
                std::wstring path; bool attrs=false;
                const auto key=IconKey(state,state.entries[0],path,attrs);
                auto* icon=FindIconRecord(state,key);
                return icon && icon->resolved && UploadIconBitmap(state,*icon);
            }),"drive preview icon");
            for(auto* icon : VisibleIconUploads(state)) UploadIconBitmap(state,*icon);
            const COLORREF textColor=state.nativeTextColor;
            state.nativeTextColor=RGB(230,230,230);
            Paint(state); if(!rtl) SaveGridPreview(state,L"GPU_OVERLAY_DRIVE_TILE_PREVIEW");
            state.nativeTextColor=textColor;
        }
        ApplyRowHeight(state,60); AdvanceRowHeight(state,true);
        Check(state.itemsPerRow==1 && state.iconPx>48 &&
            LayoutItem(state,LayoutRowVisual(state,0,1.f)).capacity.bottom<state.rowPx,
            "drive tile zoom did not reuse grid reflow or reserve capacity bounds");
        StopRowTransition(state); UnregisterAnimationWindow(state.hwnd);
        state.gridView=false; state.columnsView=true; UpdateMetrics(state);
        UpdateColumns(state);
        const auto inlineItem=LayoutItem(state,LayoutRowVisual(state,0,1.f));
        const auto inlineBar=LayoutDriveBar(state,0,inlineItem);
        Check(inlineBar.visible && inlineBar.track.left>inlineItem.text.left &&
            inlineBar.track.bottom<=inlineItem.text.bottom,
            "drive tile changes broke the existing inline Details usage track");
        Stop(state.background); DropTarget(state); DestroyWindow(state.hwnd);
    }
    Check(DeleteFileW(picture.c_str()) != FALSE,"thumbnail fixture cleanup");
    g_selectionRadius=radius; g_gridSelectionRadius=gridRadius;
    g_selectionAlpha=selection; g_hoverAlpha=hover; g_bothAlpha=both;
    g_filenameOnly=filenamePolicy;
    std::cout << "PASS: icon grids and This PC drive tiles in LTR/RTL, capacity/percent-only/unknown states and stable enrichment, bar pixels, zoom, real Shell thumbnails, one-texture film/badge decorations, 96/192 DPI rounded corners/shadow/Cut, GPU-ready image replacement, labels, input/groups and 2D deletion\n";
}
