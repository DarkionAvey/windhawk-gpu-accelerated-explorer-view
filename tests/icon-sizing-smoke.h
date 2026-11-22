// Native ICO/image-list extraction, a framed factory fallback, real per-tab
// workers and GPU readback. All files/windows are private fixtures.
static void TestNativeIconSizing(const std::wstring& fixture) {
    using namespace IconAlphaFixture;
    const auto iconPath=std::filesystem::path(fixture)/L"sizing-resource.ico";
    // One 48px native resource, with off-center 40x28px artwork and faint edges.
    std::vector<BYTE> file(22+sizeof(BITMAPINFOHEADER)+48*48*4+8*48,0);
    const auto word=[&](size_t offset,WORD value) {memcpy(file.data()+offset,&value,2);};
    const auto dword=[&](size_t offset,DWORD value) {memcpy(file.data()+offset,&value,4);};
    word(2,1); word(4,1); file[6]=file[7]=48; word(10,1); word(12,32);
    dword(14,DWORD(file.size()-22)); dword(18,22);
    BITMAPINFOHEADER header{}; header.biSize=sizeof(header); header.biWidth=48; header.biHeight=96;
    header.biPlanes=1; header.biBitCount=32; header.biSizeImage=48*48*4;
    memcpy(file.data()+22,&header,sizeof(header));
    for(int y=8;y<36;++y) for(int x=3;x<43;++x) {
        auto* p=file.data()+22+sizeof(header)+(size_t(47-y)*48+x)*4;
        p[0]=40; p[1]=160; p[2]=220; p[3]=BYTE(x==3 || y==8 ? 1 : 255);
    }
    HANDLE output=CreateFileW(iconPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD written=0; Check(output!=INVALID_HANDLE_VALUE && WriteFile(output,file.data(),DWORD(file.size()),&written,nullptr) &&
        written==file.size(),"private native ICO creation"); CloseHandle(output);
    struct Fixture {
        Fixture(const std::wstring& path) {FramedIconFixture::path=path;FramedIconFixture::icons=FramedIconFixture::thumbnails=0;}
        ~Fixture() {FramedIconFixture::path.clear();}
    } provider(iconPath.wstring());
    const auto fonts=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"icon sizing fonts");
    {
        PresentationFixture::Tab tab(L"native icon sizing",1,false,true,false,1400,700);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        state.cachedNativeMode=FVM_ICON; state.iconPx=256; state.rowPx=340; state.itemsPerRow=1;
        state.background->imageMemoryProbeAfter=ULLONG_MAX;
        auto& item=state.entries[0]; item.name=iconPath.filename().wstring(); item.path=iconPath.wstring(); item.folder=false;
        const bool thumbnails=g_listThumbnails.exchange(false);
        const auto request=[&] {
            std::wstring lookup; bool attributes=false; const auto key=IconKey(state,item,lookup,attributes);
            Check(RequestEntryIcon(state,item,false,{{key,0}}),"native icon demand");
            Check(Eventually([&] {ReceiveIcons(state,false); auto* record=FindIconRecord(state,key);
                if(record) return record->resolved && record->pixels.Ready() && !record->pending;
                record=FindIconRecord(state,key,true); return record && record->pixels.Ready();}),"native icon worker delivery");
            return key;
        };
        const auto first=request(); auto* record=FindIconRecord(state,first);
        Check(record && record->pixels.width>16 && record->pixels.width<=48 && record->pixels.height<=48 &&
            !record->pixels.thumbnailWidth && FramedIconFixture::thumbnails>0 && FramedIconFixture::icons==0,
            "worker used a framed factory icon or downsampled the padded jumbo canvas");
        state.iconPx=512; const auto second=request();
        Check(FindIconRecord(state,second)->sourceLimited,"native source-size ceiling was not retained");
        state.gridView=false; state.iconPx=900;
        const auto fallbackKey=request();
        Check(!FindIconRecord(state,fallbackKey) && FindIconRecord(state,fallbackKey,true)->pixels.Ready(),
            "list fixture did not reuse a CPU-only different-size image");
        const auto model=CapturePresentationModel(state);
        Check(std::any_of(model->images.begin(),model->images.end(),[&](const auto& image){return image.key==second;}),
            "presentation handoff omitted the cached icon tier");
        Check(EnsureTarget(state),"native sizing target");
        state.iconUploadsPending=true; UploadVisibleIcons(state);
        record=FindIconRecord(state,fallbackKey,true,true);
        Check(record && record->bitmap,"list size fallback was drawn but never uploaded");
        const auto pixelsId=record->pixels.data.Identity();
        const auto bitmap=record->bitmap.Get();
        auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap1> surface;
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(700,700),nullptr,0,&props,&surface)),"sizing surface");
        state.target->SetTarget(surface.Get()); state.paintWidth=700;
        for(bool grid:{false,true}) for(bool rtl:{false,true}) for(float side:{32.f,96.f,256.f,512.f}) {
            state.gridView=grid; state.rtlLayout=rtl; state.iconPx=side;
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
            Check(DrawCachedIcon(state,*record,32,32),"native fitted icon draw");
            Check(SUCCEEDED(state.target->EndDraw()),"native fitted icon completion");
            const auto image=Read(state,surface.Get()); int left=700,right=0,top=700,bottom=0;
            for(int y=0;y<700;++y) for(int x=0;x<700;++x) if(image.data[size_t(y)*image.stride+x*4+3]) {
                left=std::min(left,x);right=std::max(right,x+1);top=std::min(top,y);bottom=y+1;
            }
            const float expectedHeight=side*record->pixels.height/record->pixels.width;
            Check(abs(float(right-left)-side)<=2 && abs(float(bottom-top)-expectedHeight)<=2,
                "native icon did not fill its list/grid slot or lost its aspect ratio");
        }
        Check(record->pixels.data.Identity()==pixelsId && record->bitmap.Get()==bitmap,
            "zoom changed CPU pixels or reuploaded a texture");
        // Refresh the real private ICO through the normal worker. Invalidation
        // and CPU delivery must both retain the old GPU artwork until upload.
        state.gridView=true; state.rtlLayout=false; state.iconPx=512;
        InvalidateShellIconKey(state,item.path);
        Check(record->pixels.data.Identity()==pixelsId && record->bitmap.Get()==bitmap && record->needsRefresh,
            "item icon invalidation discarded ready artwork");
        const auto refreshed=request();
        record=FindIconRecord(state,refreshed);
        Check(record && record->pixels.data.Identity()!=pixelsId && record->bitmap.Get()==bitmap &&
            record->bitmapPixels.data.Identity()==pixelsId,"CPU icon delivery discarded the displayed texture");
        Check(UploadIconBitmap(state,*record) && record->bitmap.Get()!=bitmap &&
            record->bitmapPixels.data.Identity()==record->pixels.data.Identity(),"ready replacement icon was not published");
        const auto freshPixels=record->pixels.data.Identity(); const auto freshBitmap=record->bitmap.Get();
        const bool visible=state.visible; state.visible=false;
        InvalidateShellIcons(state); state.visible=visible;
        Check(record->pixels.data.Identity()==freshPixels && record->bitmap.Get()==freshBitmap && record->needsRefresh,
            "global icon invalidation discarded ready artwork");
        surface.Reset(); DropTarget(state); state.gridView=false; state.rtlLayout=false;state.iconPx=900;
        Check(EnsureTarget(state),"recreated sizing target"); state.iconUploadsPending=true; UploadVisibleIcons(state);
        Check(FindIconRecord(state,fallbackKey,true,true)->bitmap!=nullptr,"target recreation lost a list size fallback");
        g_listThumbnails=thumbnails; DropTarget(state);
    }
    g_writeFactory=fonts;
    Check(DeleteFileW(iconPath.c_str()),"native ICO cleanup");
    IconPixels blank; blank.width=blank.height=8;blank.stride=32;blank.data.assign(256,0);
    Check(!FitShellIconPixels(std::move(blank)).Ready(),"empty icon suppressed its association fallback");
    std::cout<<"PASS: native ICO worker preserves artwork through item/global invalidation and CPU/GPU replacement; cached zoom/target recreation, fallback and grid/list/RTL 32..512px fitting\n";
}

// Small slots must use the icon resource's own small artwork (often hand-
// tuned), not a downscaled large image. A private ICO holds one entry per
// real Shell image-list size, queried from Windows, each in its own colour.
static void TestSmallIconArtwork(const std::wstring& fixture) {
    using namespace IconAlphaFixture;
    const auto listSide=[](int list) {
        ComPtr<IImageList> images; int width=0,height=0;
        Check(SUCCEEDED(SHGetImageList(list,IID_PPV_ARGS(&images))) &&
            SUCCEEDED(images->GetIconSize(&width,&height)) && width>0,"Shell image-list size");
        return width;
    };
    const int small=listSide(SHIL_SMALL), large=listSide(SHIL_LARGE);
    Check(small<large && large<256,"distinct small/large Shell image lists");
    const auto derived=ShellImageListSides();
    Check(int(derived[0])==small && int(derived[1])==large && int(derived[2])==listSide(SHIL_EXTRALARGE),
        "metric-derived image-list sizes differ from the real Shell image lists");
    struct Art {int side; BYTE b,g,r;};
    const Art arts[]={{small,0,0,220},{large,220,0,0}};
    // Opaque one-pixel checker and gradients: any resampling of the small
    // entry, by Windows or the mod, changes these exact BGR bytes.
    const auto reference=[](int x,int y) {
        return std::array<BYTE,3>{BYTE(x*7),BYTE(y*11),BYTE((x+y)&1 ? 230 : 150)};
    };
    std::vector<BYTE> file(6+16*std::size(arts),0);
    const auto word=[&](size_t offset,WORD value) {memcpy(file.data()+offset,&value,2);};
    const auto dword=[&](size_t offset,DWORD value) {memcpy(file.data()+offset,&value,4);};
    word(2,1); word(4,WORD(std::size(arts)));
    for(size_t i=0;i<std::size(arts);++i) {
        const int side=arts[i].side; const size_t mask=size_t((side+31)/32*4)*side;
        const size_t offset=file.size(), bytes=sizeof(BITMAPINFOHEADER)+size_t(side)*side*4+mask;
        const size_t entry=6+16*i; file[entry]=file[entry+1]=BYTE(side);
        word(entry+4,1); word(entry+6,32); dword(entry+8,DWORD(bytes)); dword(entry+12,DWORD(offset));
        file.resize(offset+bytes,0);
        BITMAPINFOHEADER header{}; header.biSize=sizeof(header); header.biWidth=side; header.biHeight=2*side;
        header.biPlanes=1; header.biBitCount=32; header.biSizeImage=DWORD(side*side*4);
        memcpy(file.data()+offset,&header,sizeof(header));
        for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
            auto* pixel=file.data()+offset+sizeof(header)+(size_t(side-1-y)*side+x)*4;
            if(i) {pixel[0]=arts[i].b; pixel[1]=arts[i].g; pixel[2]=arts[i].r;}
            else {const auto ref=reference(x,y); pixel[0]=ref[0]; pixel[1]=ref[1]; pixel[2]=ref[2];}
            pixel[3]=255;
        }
    }
    const auto iconPath=std::filesystem::path(fixture)/L"small-artwork.ico";
    HANDLE output=CreateFileW(iconPath.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    DWORD written=0; Check(output!=INVALID_HANDLE_VALUE && WriteFile(output,file.data(),DWORD(file.size()),&written,nullptr) &&
        written==file.size(),"private multi-size ICO creation"); CloseHandle(output);
    const auto fonts=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"small artwork fonts");
    {
        PresentationFixture::Tab tab(L"small icon artwork",1,false,true,false,900,500);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        state.background->imageMemoryProbeAfter=ULLONG_MAX;
        auto& item=state.entries[0]; item.name=iconPath.filename().wstring(); item.path=iconPath.wstring(); item.folder=false;
        const bool thumbnails=g_listThumbnails.exchange(false);
        // Large first, then the small slot: a zoom-down must not keep the
        // large tier's artwork as the small icon.
        for(const auto& [mode,side,grid,red]:{std::tuple{FVM_ICON,large,true,false},
            std::tuple{FVM_DETAILS,small,false,true},std::tuple{FVM_LIST,small,false,true},
            std::tuple{FVM_SMALLICON,small,false,true}}) {
            Regression::Scope scope("mode="+std::to_string(int(mode))+" side="+std::to_string(side));
            state.cachedNativeMode=mode; state.gridView=grid; state.iconPx=float(side);
            state.rowPx=float(side+8); state.itemsPerRow=1;
            std::wstring lookup; bool attributes=false; const auto key=IconKey(state,item,lookup,attributes);
            Check(RequestEntryIcon(state,item,false,{{key,0}}),"small artwork demand");
            // Until the small artwork is delivered, the slot keeps drawing the
            // ready larger tier; nothing is cleared before its replacement.
            if(red && !FindIconRecord(state,key)->pixels.Ready()) {
                auto* placeholder=FindIconRecord(state,key,true);
                Check(placeholder && placeholder->pixels.Ready(),"pending small artwork cleared the ready placeholder");
            }
            const bool delivered=Eventually([&] {ReceiveIcons(state,false); auto* record=FindIconRecord(state,key);
                return record && record->resolved && record->pixels.Ready() && !record->pending;});
            Check(delivered,"small artwork worker delivery");
            const auto& pixels=FindIconRecord(state,key)->pixels;
            const BYTE* data=std::as_const(pixels.data).data(); size_t redPixels=0,bluePixels=0;
            for(UINT y=0;y<pixels.height;++y) for(UINT x=0;x<pixels.width;++x) {
                const BYTE* p=data+size_t(y)*pixels.stride+x*4;
                if(p[3]<128) continue;
                if(p[2]>p[0]) ++redPixels; else if(p[0]>p[2]) ++bluePixels;
            }
            Check(red ? redPixels>0 && bluePixels==0 : bluePixels>0 && redPixels==0,
                "slot did not use the icon resource's artwork for its Shell image-list size");
            if(!red) continue;
            // Display check: draw the uploaded texture into its slot and compare
            // every pixel with the ICO's own small entry, byte for byte.
            Check(EnsureTarget(state),"small artwork target");
            state.iconUploadsPending=true; UploadVisibleIcons(state);
            auto* drawn=FindIconRecord(state,key,true,true);
            Check(drawn && drawn->bitmap && drawn==FindIconRecord(state,key),"small artwork was not uploaded");
            auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            ComPtr<ID2D1Bitmap1> surface; const UINT canvas=UINT(small)*4;
            Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(canvas,canvas),nullptr,0,&props,&surface)),"small artwork surface");
            state.target->SetTarget(surface.Get()); state.paintWidth=float(canvas);
            state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
            Check(DrawCachedIcon(state,*drawn,float(small),float(small)),"small artwork draw");
            Check(SUCCEEDED(state.target->EndDraw()),"small artwork draw completion");
            const auto image=Read(state,surface.Get());
            int left=int(canvas),top=int(canvas),right=0,bottom=0;
            for(UINT y=0;y<canvas;++y) for(UINT x=0;x<canvas;++x) if(image.data[size_t(y)*image.stride+x*4+3]) {
                left=std::min(left,int(x)); top=std::min(top,int(y)); right=std::max(right,int(x)+1); bottom=int(y)+1;
            }
            Check(right-left==small && bottom-top==small,"small artwork was not displayed at its native pixel size");
            for(int y=0;y<small;++y) for(int x=0;x<small;++x) {
                const BYTE* p=&image.data[size_t(top+y)*image.stride+size_t(left+x)*4];
                const auto ref=reference(x,y);
                Check(p[0]==ref[0] && p[1]==ref[1] && p[2]==ref[2] && p[3]==255,
                    "displayed small icon differs from the ICO's own 1:1 pixels");
            }
            state.target->SetTarget(nullptr); surface.Reset(); DropTarget(state);
        }
        g_listThumbnails=thumbnails;
    }
    g_writeFactory=fonts;
    Check(DeleteFileW(iconPath.c_str()),"multi-size ICO cleanup");
    std::cout<<"PASS: small Details/List/Small Icons slots show the ICO's own "<<small<<"px pixels 1:1, placeholder kept until swap; covered "<<
        (small%16 ? "fractional" : "whole-number")<<" scaling at system DPI "<<GetDpiForSystem()<<" (large "<<large<<"px)\n";
}

// Every Windows scaling step, not just this machine's: Windows' own per-DPI
// icon metrics (GetSystemMetricsForDpi) give the Shell list sizes; a slot of
// exactly that size must be requested at that size, never rounded past it.
static void TestImageTiersPerDpi() {
    size_t fractional=0;
    for(UINT dpi=96;dpi<=384;dpi+=24) {
        Regression::Scope scope("dpi="+std::to_string(dpi));
        const ShellListSides shell{UINT(GetSystemMetricsForDpi(SM_CXSMICON,dpi)),
            UINT(GetSystemMetricsForDpi(SM_CXICON,dpi)),UINT(MulDiv(48,int(dpi),96))};
        Check(shell[0]>0 && shell[1]>shell[0],"Windows per-DPI icon metrics");
        std::vector<UINT> visited;
        VisitImageTiers(shell,[&](UINT size) {visited.push_back(size); return true;});
        Check(std::is_sorted(visited.begin(),visited.end()) &&
            std::adjacent_find(visited.begin(),visited.end())==visited.end(),"image tiers not strictly ascending");
        for(const UINT side:shell) {
            Check(std::count(visited.begin(),visited.end(),side)==1,"Shell list size missing from image tiers");
            Check(ImageTierFor(side,4096.,shell)==side,"slot at a Shell list size was rounded past it");
            const auto above=ImageTierFor(side+.5,4096.,shell);
            Check(above>side,"slot just above a Shell list size used a smaller tier");
            if(side%16) ++fractional;
        }
        // Smaller visible slots still choose the nearest tier at or above them.
        Check(ImageTierFor(shell[0]-.5,4096.,shell)<=shell[0],"small slot skipped its Shell list size");
    }
    Check(fractional>0,"no fractional-scaling list sizes were exercised");
    std::cout<<"PASS: image tiers include Windows' small/large/extra-large list sizes at 100..400% ("<<fractional<<" non-16-multiple sizes)\n";
}
