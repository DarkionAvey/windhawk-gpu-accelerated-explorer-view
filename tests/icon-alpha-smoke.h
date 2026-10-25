// Real Shell/WIC/native-HICON/GPU comparisons, using private temporary fixtures.
namespace IconAlphaFixture {
void Save(IWICImagingFactory* wic,const IconPixels& pixels,const std::filesystem::path& path) {
    ComPtr<IWICBitmap> source; ComPtr<IWICFormatConverter> converted;
    ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
    Check(SUCCEEDED(wic->CreateBitmapFromMemory(pixels.width,pixels.height,GUID_WICPixelFormat32bppPBGRA,
        pixels.stride,UINT(pixels.data.size()),const_cast<BYTE*>(pixels.data.data()),&source)) &&
        SUCCEEDED(wic->CreateFormatConverter(&converted)) && SUCCEEDED(converted->Initialize(source.Get(),
        GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr)) && SUCCEEDED(frame->Initialize(nullptr)),"icon alpha PNG encoder");
    auto format=GUID_WICPixelFormat32bppBGRA;
    Check(SUCCEEDED(frame->SetSize(pixels.width,pixels.height)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
        SUCCEEDED(frame->WriteSource(converted.Get(),nullptr)) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),
        "icon alpha PNG write");
}
void Valid(const IconPixels& pixels) {
    Check(pixels.Ready(),"missing icon alpha pixels");
    for(UINT y=0;y<pixels.height;++y) for(UINT x=0;x<pixels.width;++x) {
        const auto* p=pixels.data.data()+y*pixels.stride+x*4;
        Check(p[0]<=p[3] && p[1]<=p[3] && p[2]<=p[3],"Shell image was mislabeled premultiplied: RGB exceeds alpha");
    }
}
IconPixels Read(ViewState& state,ID2D1Bitmap1* surface) {
    const auto size=surface->GetPixelSize(); ComPtr<ID2D1Bitmap1> cpu;
    const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        surface->GetPixelFormat());
    Check(SUCCEEDED(state.target->CreateBitmap(size,nullptr,0,&props,&cpu)) &&
        SUCCEEDED(cpu->CopyFromBitmap(nullptr,surface,nullptr)),"icon alpha GPU readback");
    D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"icon alpha GPU map");
    IconPixels pixels; pixels.width=size.width; pixels.height=size.height; pixels.stride=size.width*4;
    pixels.data.resize(size_t(pixels.stride)*pixels.height);
    for(UINT y=0;y<pixels.height;++y) memcpy(pixels.data.data()+y*pixels.stride,map.bits+y*map.pitch,pixels.stride);
    cpu->Unmap(); return pixels;
}
}

static void TestIconAlphaQuality(const std::wstring& fixture) {
    using namespace IconAlphaFixture;
    ComPtr<IWICImagingFactory> wic;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"icon alpha WIC");
    const auto empty=std::filesystem::path(fixture)/L"icon-alpha-empty";
    Check(std::filesystem::create_directory(empty),"icon alpha private folder");
    ViewState state;
    IconJob job; job.path=empty.wstring(); job.pixelSize=256; job.token=state.background->iconGeneration.load();
    HRESULT status=E_FAIL;
    const auto actual=LoadShellItemIcon(wic.Get(),job,state.background->iconProviders[0],&status);
    Check(SUCCEEDED(status),"real Shell image-factory icon"); Valid(actual);

    // A separate native HICON path provides a trusted alpha reference for the
    // same empty-folder artwork, without assuming a particular Windows theme.
    SHFILEINFOW info{};
    Check(SHGetFileInfoW(empty.c_str(),0,&info,sizeof(info),SHGFI_SYSICONINDEX)!=0,"native folder index");
    ComPtr<IImageList> list; HICON rawIcon=nullptr;
    Check(SUCCEEDED(SHGetImageList(SHIL_JUMBO,IID_PPV_ARGS(&list))) &&
        SUCCEEDED(list->GetIcon(info.iIcon,ILD_TRANSPARENT,&rawIcon)),"native folder HICON");
    auto native=DecodeShellIcon(wic.Get(),rawIcon); DestroyIcon(rawIcon); Valid(native);
    Check(actual.width==native.width && actual.height==native.height,"native folder comparison dimensions");
    size_t partial=0;
    for(UINT y=0;y<actual.height;++y) for(UINT x=0;x<actual.width;++x) {
        const auto* a=actual.data.data()+y*actual.stride+x*4;
        const auto* b=native.data.data()+y*native.stride+x*4;
        partial+=b[3]>0 && b[3]<255;
        for(int channel=0;channel<4;++channel)
            Check(abs(int(a[channel])-int(b[channel]))<=1,"factory icon edges differ from native HICON pixels");
    }
    Check(partial>100,"native icon alpha comparison did not exercise antialiased edges/shadows");

    // Transparent PNG thumbnails take the same factory import path. Compare
    // against the WIC decoder so no already-premultiplied byte is darkened twice.
    IconPixels image; image.width=image.height=64; image.stride=256; image.data.resize(64*256);
    for(UINT y=0;y<64;++y) for(UINT x=0;x<64;++x) {
        auto* p=image.data.data()+y*image.stride+x*4;
        const BYTE alpha=BYTE((x*255)/63); p[3]=alpha;
        p[0]=BYTE(alpha*20/255); p[1]=BYTE(alpha*80/255); p[2]=alpha;
    }
    const auto png=std::filesystem::path(fixture)/L"icon-alpha.png"; Save(wic.Get(),image,png);
    ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
    Check(SUCCEEDED(wic->CreateDecoderFromFilename(png.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder)) &&
        SUCCEEDED(decoder->GetFrame(0,&frame)),"transparent PNG reference");
    const auto expected=DecodeIconPixels(wic.Get(),frame.Get());
    job.path=png.wstring(); job.extension=L".png"; job.pixelSize=64; job.allowThumbnail=true;
    const auto thumbnail=LoadShellItemIcon(wic.Get(),job,state.background->iconProviders[0],&status); Valid(thumbnail);
    Check(thumbnail.thumbnailWidth==64 && thumbnail.thumbnailHeight==64,"transparent PNG thumbnail dimensions");
    for(UINT y=8;y<56;++y) for(UINT x=8;x<56;++x) {
        const auto* a=thumbnail.data.data()+y*thumbnail.stride+x*4;
        const auto* b=expected.data.data()+y*expected.stride+x*4;
        for(int channel=0;channel<4;++channel)
            Check(abs(int(a[channel])-int(b[channel]))<=1,"transparent thumbnail fringed or was premultiplied twice");
    }

    const auto factoryBefore=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"icon alpha fonts");
    state.hwnd=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"private icon alpha GPU",
        WS_POPUP,-32000,-32000,768,300,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    state.frameHwnd=state.nativeHwnd=state.activeTabHost=state.hwnd;
    state.visible=state.inScope=state.detailsViewActive=true;
    Check(state.hwnd && EnsureTarget(state),"icon alpha target");
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> surface;
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(768,300),nullptr,0,&props,&surface)),"icon alpha surface");
    state.target->SetTarget(surface.Get()); state.paintWidth=768;
    IconRecord loaded,reference; loaded.pixels=actual; reference.pixels=native;
    Check(UploadIconBitmap(state,loaded) && UploadIconBitmap(state,reference),"icon alpha upload");
    const auto draw=[&](IconRecord& icon,float size,bool rtl,float opacity,unsigned background) {
        state.iconPx=size; state.rtlLayout=rtl;
        state.target->BeginDraw(); state.target->Clear(Packed(background));
        Check(DrawCachedIcon(state,icon,20,20,opacity),"icon alpha shared draw");
        Check(SUCCEEDED(state.target->EndDraw()),"icon alpha draw completion");
        return Read(state,surface.Get());
    };
    for(float size:{24.f,48.f,96.f,160.f,192.f,256.f}) for(bool rtl:{false,true})
        for(float opacity:{1.f,.5f}) for(unsigned background:{0x181818u,0xFFFFFFu}) {
            const auto a=draw(loaded,size,rtl,opacity,background);
            const auto b=draw(reference,size,rtl,opacity,background);
            for(size_t i=0;i<a.data.size();++i)
                Check(abs(int(a.data[i])-int(b.data[i]))<=1,"GPU icon edges differ from native source on dark/light/Cut/DPI sizes");
        }
    // Optional review artifact: actual old interpretation, corrected loader,
    // and independent native HICON, with the same geometry and GPU filter.
    wchar_t preview[MAX_PATH]{};
    if(GetEnvironmentVariableW(L"GPU_OVERLAY_ICON_ALPHA_PREVIEW",preview,ARRAYSIZE(preview))) {
        ComPtr<IShellItemImageFactory> item; HBITMAP raw=nullptr; ComPtr<IWICBitmap> wrong;
        Check(SUCCEEDED(SHCreateItemFromParsingName(empty.c_str(),nullptr,IID_PPV_ARGS(&item))) &&
            SUCCEEDED(item->GetImage({256,256},SIIGBF(SIIGBF_ICONONLY|SIIGBF_BIGGERSIZEOK),&raw)),"old alpha preview source");
        const auto imported=wic->CreateBitmapFromHBITMAP(raw,nullptr,WICBitmapUsePremultipliedAlpha,&wrong); DeleteObject(raw);
        Check(SUCCEEDED(imported),"old alpha preview import");
        IconRecord old; old.pixels=DecodeIconPixels(wic.Get(),wrong.Get()); Check(UploadIconBitmap(state,old),"old alpha preview upload");
        state.iconPx=192; state.rtlLayout=false;
        ComPtr<IDWriteTextFormat> label;
        Check(SUCCEEDED(g_writeFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,20,L"en-US",&label)),"alpha preview labels");
        state.target->BeginDraw(); state.target->Clear(Packed(0x181818)); state.brush->SetColor(Packed(0xFFFFFF));
        const wchar_t* labels[]={L"Before",L"Corrected",L"Native HICON reference"};
        IconRecord* records[]={&old,&loaded,&reference};
        for(int column=0;column<3;++column) {
            const auto box=D2D1::RectF(float(column*256+12),8,float((column+1)*256),40);
            state.target->DrawTextW(labels[column],UINT32(wcslen(labels[column])),label.Get(),box,state.brush.Get());
            DrawCachedIcon(state,*records[column],column*256.f+20,48);
        }
        Check(SUCCEEDED(state.target->EndDraw()),"alpha preview render"); Save(wic.Get(),Read(state,surface.Get()),preview);
    }
    DropTarget(state); DestroyWindow(state.hwnd); g_writeFactory=factoryBefore;
    frame.Reset(); decoder.Reset(); list.Reset();
    Check(DeleteFileW(png.c_str()) && RemoveDirectoryW(empty.c_str()),"private icon alpha fixture cleanup");
    std::cout<<"PASS: real Shell straight-alpha icons match native HICON texels; transparent PNG alpha is applied once; GPU dark/light, LTR/RTL, Cut and 24..256px edges\n";
}
