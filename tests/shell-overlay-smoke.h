// Shell icon overlays (cloud sync state, links, shares) through the real
// parent-folder IShellIconOverlay path, per-tab workers and GPU readback.
// A private shortcut receives the built-in link overlay by the same route as
// registered sync-provider overlays. Expectations come from Windows' own
// overlay composition (SHGFI_ADDOVERLAYS) and GDI reads of the system images.
namespace ShellOverlayFixture {
int ExpectedOverlay(const std::wstring& path) {
    SHFILEINFOW info{};
    Check(SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_ADDOVERLAYS|SHGFI_OVERLAYINDEX)!=0,
        "native overlay composition");
    if(info.hIcon) DestroyIcon(info.hIcon);
    const int overlay=(info.iIcon>>24)&0xFF;
    if(!overlay) return -1;
    ComPtr<IImageList> list; int image=-1;
    Check(SUCCEEDED(SHGetImageList(SHIL_LARGE,IID_PPV_ARGS(&list))) &&
        SUCCEEDED(list->GetOverlayImage(overlay,&image)) && image>0,"native overlay image index");
    return image;
}
// Opaque bounds of a system image-list entry in its own canvas.
RECT GlyphBounds(int image,UINT width,int& canvas,BYTE alpha=128) {
    ComPtr<IImageList> chosen;
    for(int kind:{SHIL_LARGE,SHIL_EXTRALARGE,SHIL_JUMBO}) {
        ComPtr<IImageList> list; int cx=0,cy=0;
        if(SUCCEEDED(SHGetImageList(kind,IID_PPV_ARGS(&list))) && SUCCEEDED(list->GetIconSize(&cx,&cy)) &&
            UINT(cx)==width) {chosen=list; canvas=cx; break;}
    }
    Check(chosen!=nullptr,"overlay artwork is not a system image-list size");
    HICON icon=nullptr; ICONINFO info{}; BITMAP bitmap{};
    Check(SUCCEEDED(chosen->GetIcon(image,ILD_TRANSPARENT,&icon)) && GetIconInfo(icon,&info) &&
        GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap),"native overlay bitmap");
    BITMAPINFO header{}; header.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    header.bmiHeader.biWidth=bitmap.bmWidth; header.bmiHeader.biHeight=-bitmap.bmHeight;
    header.bmiHeader.biPlanes=1; header.bmiHeader.biBitCount=32; header.bmiHeader.biCompression=BI_RGB;
    std::vector<DWORD> bits(size_t(bitmap.bmWidth)*bitmap.bmHeight);
    HDC dc=GetDC(nullptr);
    const int lines=GetDIBits(dc,info.hbmColor,0,bitmap.bmHeight,bits.data(),&header,DIB_RGB_COLORS);
    ReleaseDC(nullptr,dc);
    RECT bounds{bitmap.bmWidth,bitmap.bmHeight,0,0};
    for(int y=0;y<bitmap.bmHeight;++y) for(int x=0;x<bitmap.bmWidth;++x) if(bits[size_t(y)*bitmap.bmWidth+x]>>24>=alpha) {
        bounds.left=std::min<LONG>(bounds.left,x); bounds.top=std::min<LONG>(bounds.top,y);
        bounds.right=std::max<LONG>(bounds.right,x+1); bounds.bottom=std::max<LONG>(bounds.bottom,y+1);
    }
    DeleteObject(info.hbmColor); DeleteObject(info.hbmMask); DestroyIcon(icon);
    Check(lines==bitmap.bmHeight,"native overlay pixels");
    return bounds;
}
}

static void TestShellOverlays(const std::wstring& fixture) {
    using namespace ShellOverlayFixture;
    const auto target=std::filesystem::path(fixture)/L"overlay-target.txt";
    const auto link=std::filesystem::path(fixture)/L"overlay-link.lnk";
    HANDLE file=CreateFileW(target.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"private overlay target"); CloseHandle(file);
    {
        ComPtr<IShellLinkW> shortcut; ComPtr<IPersistFile> persist;
        Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shortcut))) &&
            SUCCEEDED(shortcut->SetPath(target.c_str())) && SUCCEEDED(shortcut.As(&persist)) &&
            SUCCEEDED(persist->Save(link.c_str(),TRUE)),"private shortcut");
    }
    const int expectedLink=ExpectedOverlay(link.wstring()), expectedPlain=ExpectedOverlay(target.wstring());
    std::cout<<"PROFILE: native overlay images link="<<expectedLink<<" plain="<<expectedPlain<<"\n";
    const auto fonts=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"overlay fonts");
    {
        PresentationFixture::Tab tab(L"shell overlays",2,false,true,false,800,480);
        auto& state=tab.state; FocusFixture::Session focus(tab.frame,state.hwnd);
        state.background->imageMemoryProbeAfter=ULLONG_MAX;
        for(size_t i=0;i<2;++i) {
            const auto& path=i ? target : link; auto& entry=state.entries[i];
            entry.name=path.filename().wstring(); entry.path=path.wstring(); entry.folder=false;
        }
        const Entry& shortcut=state.entries[0];
        const Entry& plain=state.entries[1];
        const auto resolve=[&](const Entry& entry) {
            const auto key=OverlayQueryKey(entry);
            Check(Eventually([&] {
                RequestEntryOverlay(state,entry,{{key,0}}); ReceiveIcons(state,false);
                const auto* record=FindIconRecord(state,key);
                return record && record->resolved && !record->pending;
            }),"overlay query delivery");
            return FindIconRecord(state,key);
        };
        // Base artwork carries no overlay of its own, so one is never doubled:
        // the shortcut's own icon equals its target type's association icon.
        for(float side:{32.f,96.f}) {
            Regression::Scope scope("base side="+std::to_string(int(side)));
            state.iconPx=side;
            const auto base=[&](const Entry& entry,bool association) {
                std::wstring lookup; bool attributes=false;
                const auto key=IconKey(state,entry,lookup,attributes,association);
                Check(Eventually([&] {
                    RequestEntryIcon(state,entry,association,{{key,0}}); ReceiveIcons(state,false);
                    const auto* record=FindIconRecord(state,key);
                    return record && record->resolved && !record->pending && record->pixels.Ready();
                }),"base icon delivery");
                return FindIconRecord(state,key);
            };
            const auto* own=base(shortcut,false); const auto* type=base(plain,true);
            Check(own->pixels.width==type->pixels.width && own->pixels.height==type->pixels.height &&
                std::equal(own->pixels.data.begin(),own->pixels.data.end(),type->pixels.data.begin(),type->pixels.data.end()),
                "the shortcut's base icon already contains an overlay");
        }
        state.iconPx=48;
        Check(resolve(shortcut)->overlayImage==expectedLink,"shortcut overlay differs from Windows' composition");
        Check(resolve(plain)->overlayImage==expectedPlain,"plain file overlay differs from Windows' composition");
        Check(expectedPlain>=0 || !OverlayArtwork(state,plain,false),"an item without an overlay found artwork");
        if(expectedLink>0) {
            const auto imageKey=OverlayImageKey(MakeIconKeyContext(state),expectedLink);
            Check(Eventually([&] {
                RequestEntryOverlay(state,shortcut,{{OverlayQueryKey(shortcut),0},{imageKey,0}});
                ReceiveIcons(state,false);
                const auto* record=FindIconRecord(state,imageKey);
                return record && record->pixels.Ready() && !record->pending;
            }),"overlay artwork delivery");
            auto* artwork=FindIconRecord(state,imageKey);
            Check(artwork->association && OverlayArtwork(state,shortcut,false)==artwork,
                "overlay artwork is not the shared image-list entry");
            // The presentation lane receives both the per-item answer and the
            // shared artwork, without querying the Shell itself.
            const auto model=CapturePresentationModel(state);
            Check(std::any_of(model->images.begin(),model->images.end(),[&](const auto& image) {
                return image.key==OverlayQueryKey(shortcut) && image.overlayQuery && image.overlayImage==expectedLink;}) &&
                std::any_of(model->images.begin(),model->images.end(),[&](const auto& image) {
                return image.key==imageKey && image.pixels.Ready();}),"presentation handoff omitted the overlay");

            int canvas=0;
            const auto glyph=GlyphBounds(expectedLink,artwork->pixels.width,canvas);
            const auto coverage=GlyphBounds(expectedLink,artwork->pixels.width,canvas,1);
            const float extent=float(std::max(coverage.right-coverage.left,coverage.bottom-coverage.top))/canvas;
            Check(glyph.right>glyph.left && glyph.bottom>glyph.top,"native overlay has no opaque glyph");
            Check(EnsureTarget(state),"overlay target");
            IconRecord base; base.pixels.width=base.pixels.height=64; base.pixels.stride=256;
            base.pixels.data.assign(size_t(64)*256,0);
            Check(UploadIconBitmap(state,base) && UploadIconBitmap(state,*artwork),"overlay uploads");
            auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED));
            ComPtr<ID2D1Bitmap1> surface;
            Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(400,400),nullptr,0,&props,&surface)),"overlay surface");
            state.target->SetTarget(surface.Get()); state.paintWidth=400;
            // Adornments never shrink below Windows' small-icon size for the window's DPI.
            const float smallest=float(GetSystemMetricsForDpi(SM_CXSMICON,GetDpiForWindow(state.hwnd)));
            const float largest=2.f*std::max(smallest,float(GetSystemMetricsForDpi(SM_CXICON,GetDpiForWindow(state.hwnd))));
            bool floored=false;
            for(bool rtl:{false,true}) for(float side:{8.f,32.f,96.f,256.f}) {
                Regression::Scope scope(std::string("rtl=")+(rtl ? "1" : "0")+" side="+std::to_string(int(side)));
                state.rtlLayout=rtl; state.iconPx=side;
                state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
                Check(DrawCachedIcon(state,base,40,40,1.f,0.f,nullptr,artwork),"overlay draw");
                Check(SUCCEEDED(state.target->EndDraw()),"overlay draw completion");
                const auto image=IconAlphaFixture::Read(state,surface.Get());
                int left=400,right=0,top=400,bottom=0;
                for(int y=0;y<400;++y) for(int x=0;x<400;++x) if(image.data[size_t(y)*image.stride+x*4+3]>=128) {
                    left=std::min(left,x); right=std::max(right,x+1); top=std::min(top,y); bottom=std::max(bottom,y+1);
                }
                // Derive the capped canvas from independent GDI alpha bounds
                // and OS metrics, keeping its artwork corner anchored.
                const float drawn=float(IconPixelSize(state));
                const float edge=std::min(std::max(drawn,smallest),largest/extent),scale=edge/float(canvas);
                floored|=drawn<smallest;
                const float origin=rtl ? float(state.paintWidth)-40.f-drawn : 40.f, bottomEdge=40.f+drawn;
                // RTL mirrors the corner, as the thumbnail type badge does.
                const float expectedLeft=rtl ? origin+drawn-glyph.right*scale : origin+glyph.left*scale;
                const float expectedRight=rtl ? origin+drawn-glyph.left*scale : origin+glyph.right*scale;
                Check(right>left && fabsf(left-expectedLeft)<=2.f && fabsf(right-expectedRight)<=2.f &&
                    fabsf(top-(bottomEdge-edge+glyph.top*scale))<=2.f &&
                    fabsf(bottom-(bottomEdge-edge+glyph.bottom*scale))<=2.f,
                    "overlay glyph missed its DPI-capped image-list corner of the drawn icon");
            }
            Check(floored,"no overlay draw exercised the small-icon minimum");

            // A thumbnail's type badge: a quarter of the slot, never below the
            // small-icon size, at the thumbnail's bottom corner.
            IconPixels photo; photo.width=96; photo.height=64; photo.stride=photo.width*4;
            photo.data.assign(size_t(photo.stride)*photo.height,0);
            for(size_t i=0;i<photo.data.size();i+=4) {photo.data[i+2]=200; photo.data[i+3]=255;}
            ThumbnailStyle style; style.badge.width=style.badge.height=48; style.badge.stride=192;
            style.badge.data.assign(size_t(192)*48,0);
            for(size_t i=0;i<style.badge.data.size();i+=4) {style.badge.data[i]=255; style.badge.data[i+3]=255;}
            IconRecord thumb; thumb.pixels=DecorateThumbnail(std::move(photo),style);
            Check(thumb.pixels.badgeSize==48 && UploadIconBitmap(state,thumb),"badged thumbnail upload");
            Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(400,400),nullptr,0,&props,&surface)),"badge surface");
            state.target->SetTarget(surface.Get());
            bool badgeFloored=false;
            for(bool rtl:{false,true}) for(float side:{32.f,96.f,256.f}) {
                Regression::Scope scope(std::string("badge rtl=")+(rtl ? "1" : "0")+" side="+std::to_string(int(side)));
                state.rtlLayout=rtl; state.iconPx=side;
                state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,0.f));
                Check(DrawCachedIcon(state,thumb,40,40),"badged thumbnail draw");
                Check(SUCCEEDED(state.target->EndDraw()),"badged thumbnail completion");
                const auto image=IconAlphaFixture::Read(state,surface.Get());
                int left=400,right=0,top=400,bottom=0;
                for(int y=0;y<400;++y) for(int x=0;x<400;++x) {
                    const auto* p=image.data.data()+size_t(y)*image.stride+x*4;
                    if(p[0]>=192 && p[2]<64) {
                        left=std::min(left,x); right=std::max(right,x+1); top=std::min(top,y); bottom=std::max(bottom,y+1);
                    }
                }
                const float drawn=float(IconPixelSize(state)), expected=std::clamp(drawn*.25f,smallest,largest);
                badgeFloored|=drawn*.25f<smallest;
                const float thumbHeight=drawn*64.f/96.f, thumbBottom=40.f+(drawn-thumbHeight)*.5f+thumbHeight;
                const float thumbLeft=rtl ? float(state.paintWidth)-40.f-drawn : 40.f;
                const float expectedLeft=rtl ? thumbLeft : thumbLeft+drawn-expected;
                Check(right>left && fabsf(float(right-left)-expected)<=2.f && fabsf(float(bottom-top)-expected)<=2.f &&
                    fabsf(left-expectedLeft)<=2.f && fabsf(bottom-thumbBottom)<=2.f,
                    "thumbnail badge is not slot-scaled with the small-icon minimum at its corner");
            }
            Check(badgeFloored,"no badge draw exercised the small-icon minimum");
            surface.Reset();
            // An item update re-resolves the overlay; the displayed one stays
            // until the replacement answer is accepted.
            InvalidateShellIconKey(state,OverlayQueryKey(shortcut));
            const auto* query=FindIconRecord(state,OverlayQueryKey(shortcut));
            Check(query->needsRefresh && query->overlayImage==expectedLink &&
                OverlayArtwork(state,shortcut,true)==artwork,"item update removed the displayed overlay");
            Check(resolve(shortcut)->overlayImage==expectedLink && !query->needsRefresh,"item update was not re-resolved");
            state.rtlLayout=false; DropTarget(state);
        }
    }
    g_writeFactory=fonts;
    Check(DeleteFileW(link.c_str()) && DeleteFileW(target.c_str()),"overlay fixture cleanup");
    std::cout<<"PASS: Shell overlays resolve per item off the UI thread, match Windows' own composition, share "
        "image-list artwork, reach the presentation lane, draw DPI-capped at their corner in LTR/RTL, survive item "
        "refresh and are never doubled by base artwork; overlays and type badges keep the small-icon minimum\n";
}
