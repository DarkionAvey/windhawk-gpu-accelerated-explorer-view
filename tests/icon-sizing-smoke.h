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
