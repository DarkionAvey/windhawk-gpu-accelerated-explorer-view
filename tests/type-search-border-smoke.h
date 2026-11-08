// Inspect real Text() pixels against a separately shaped DirectWrite layout.
static void TestTypeSearchBorder(const std::wstring& fixture) {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"search border fonts");
    PresentationFixture::Tab tab(L"search border",1,false,false,false,1024,240);
    auto& state=tab.state;
    Check(EnsureTarget(state),"search border target");
    ComPtr<ID2D1Image> previous; state.target->GetTarget(&previous);
    ComPtr<ID2D1Bitmap1> surface;
    const auto properties=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(1024,240),nullptr,0,&properties,&surface)),"search border surface");
    state.target->SetTarget(surface.Get()); state.paintWidth=1024;
    unsigned edges=0;
    for(int dpi:{96,120,144,192}) for(bool rtl:{false,true})
        for(bool wrapped:{false,true}) for(bool narrow:{false,true}) {
        Regression::Scope scope("search border dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl)+
            " wrapped="+std::to_string(wrapped)+" narrow="+std::to_string(narrow));
        state.rtlLayout=rtl; state.fontPx=20.f*dpi/96.f;
        Check(MakeTextFormats(state),"search border formats");
        const std::wstring name=rtl ? L"مجلد ملفات البرامج الطويل":L"Call of Duty Modern Warfare";
        const UINT32 prefix=wrapped ? UINT32(name.size()):7;
        const float left=40,top=30,width=std::round((narrow ? 70.f:360.f)*dpi/96.f);
        ComPtr<IDWriteTextLayout> line;
        Check(SUCCEEDED(g_writeFactory->CreateTextLayout(L"Mg",2,state.textFormat.Get(),width,240,&line)),"search border font measurement");
        DWRITE_TEXT_METRICS lineMetrics{};
        Check(SUCCEEDED(line->GetMetrics(&lineMetrics)),"search border line metrics");
        const float height=std::ceil(lineMetrics.height)*(wrapped ? 2.f:1.f);
        state.target->BeginDraw(); state.target->Clear(D2D1::ColorF(0,1.f));
        const std::vector<DWRITE_TEXT_RANGE> highlight{{0,prefix}};
        Text(state,name,left,top,left+width,top+height,0xFFFFFF,false,1.f,false,wrapped,&highlight);
        Check(SUCCEEDED(state.target->EndDraw()),"search border draw");
        const auto pixels=IconAlphaFixture::Read(state,surface.Get());
        const float physicalLeft=rtl ? 1024-left-width:left;
        ComPtr<IDWriteTextLayout> reference;
        Check(SUCCEEDED(g_writeFactory->CreateTextLayout(name.c_str(),UINT32(name.size()),state.textFormat.Get(),
            width,height,&reference)),"independent search border layout");
        if(wrapped) {
            reference->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
            reference->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            reference->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        }
        DWRITE_HIT_TEST_METRICS ranges[64]{}; UINT32 count=0;
        Check(SUCCEEDED(reference->HitTestTextRange(0,prefix,physicalLeft,top,ranges,ARRAYSIZE(ranges),&count)),"search border reference ranges");
        const auto rounded=[](float value) {return int(std::floor(value+.5f));};
        const auto gold=[&](int x,int y) {
            Check(x>=0 && y>=0 && x<int(pixels.width) && y<int(pixels.height),"search border pixel bounds");
            const auto* p=pixels.data.data()+size_t(y)*pixels.stride+size_t(x)*4;
            return p[2]>=180 && p[1]>=150 && p[0]<=140;
        };
        unsigned visible=0;
        for(UINT32 i=0;i<count;++i) {
            const auto& m=ranges[i];
            if(!m.isText || m.isTrimmed || m.width<=0 || m.height<=0) continue;
            const int l=std::max(rounded(physicalLeft-1),rounded(m.left-1));
            const int r=std::min(rounded(physicalLeft+width+1),rounded(m.left+m.width+1));
            const int t=std::max(rounded(top),rounded(m.top));
            const int b=std::min(rounded(top+height),rounded(m.top+m.height));
            if(r-l<8 || b-t<8) continue; // Straight spans, clear of rounded corners.
            Check(gold(l,(t+b)/2),"type-search left border was clipped");
            Check(gold(r-1,(t+b)/2),"type-search right border was clipped");
            Check(gold((l+r)/2,t),"type-search top border was clipped");
            Check(gold((l+r)/2,b-1),"type-search bottom border was clipped");
            ++visible; edges+=4;
        }
        Check(visible!=0,"search border test had no visible prefix");
        if(dpi==96 && !rtl && !wrapped && !narrow) {
            ComPtr<IWICImagingFactory> wic;
            Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&wic))),"search border preview encoder");
            wchar_t exported[MAX_PATH]{};
            const DWORD length=GetEnvironmentVariableW(L"GPU_OVERLAY_TYPE_SEARCH_PREVIEW",exported,ARRAYSIZE(exported));
            Check(length<ARRAYSIZE(exported),"search border preview path length");
            IconAlphaFixture::Save(wic.Get(),pixels,length ? std::filesystem::path(exported):
                std::filesystem::path(fixture)/L"type-search-border.png");
        }
    }
    state.target->SetTarget(previous.Get());
    std::cout<<"PASS: "<<edges<<" type-search border edges in real GPU pixels across DPI, RTL, wrapped and narrow labels\n";
}
