// Compare shared Text() raster reuse with actual DirectWrite output, including
// transparent backgrounds. Both render paths use the real offscreen GPU.
template<class State>
static void TestTextRaster(State& state) {
    RefreshTextRendering(state,true);
    ComPtr<ID2D1Image> original; state.target->GetTarget(&original);
    ComPtr<ID2D1Bitmap1> surface, readback;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(360,160),nullptr,0,&props,&surface)),"text surface");
    props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(360,160),nullptr,0,&props,&readback)),"text readback");
    state.target->SetTarget(surface.Get()); state.paintWidth=360;
    auto capture=[&](const std::wstring& text,bool cached,bool wrapped,float width,unsigned color,float opacity,bool deferred=false,bool filename=false) {
        state.target->BeginDraw(); ConfigureTextTarget(state,state.target.Get());
        state.target->Clear(D2D1::ColorF(0,0.f));
        if (cached) {
            if constexpr (requires { state.frameProfile.textDeferred; })
                Text(state,text,20,20,20+width,120,color,false,opacity,false,wrapped,0,deferred,filename);
            else Text(state,text,20,20,20+width,120,color,false,opacity,false,wrapped);
        }
        else {
            auto layout=ResolveTextLayout(state,text,width,100,false,false,nullptr,wrapped,filename);
            state.brush->SetColor(Packed(color,opacity));
            const float x=state.rtlLayout ? 340-width : 20;
            state.target->DrawTextLayout(D2D1::Point2F(x,20),layout,state.brush.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        Check(SUCCEEDED(state.target->EndDraw()) && SUCCEEDED(readback->CopyFromBitmap(nullptr,surface.Get(),nullptr)),"text draw/copy");
        D2D1_MAPPED_RECT mapped{}; Check(SUCCEEDED(readback->Map(D2D1_MAP_OPTIONS_READ,&mapped)),"text map");
        std::vector<BYTE> pixels(360*160*4);
        for(size_t y=0;y<160;++y) memcpy(pixels.data()+y*360*4,mapped.bits+y*mapped.pitch,360*4);
        readback->Unmap(); return pixels;
    };
    for (bool rtl : {false,true}) {
        state.rtlLayout=rtl; Check(MakeTextFormats(state),"text direction format");
        for (bool wrapped : {false,true}) for (float opacity : {1.f,.35f}) for(unsigned color : {0xD9E1EDu,0x202020u}) {
            const std::wstring text=rtl ? L"زهرة في الحديقة Flower in the park.jpg" : L"Flower in the park and a long filename.jpg";
            const auto reference=capture(text,false,wrapped,120,color,opacity);
            const auto drawn=capture(text,true,wrapped,120,color,opacity);
            size_t nonzero=0,totalDifference=0,alphaDifference=0,alpha=0,referenceAlpha=0; unsigned maximum=0;
            for(size_t i=0;i<drawn.size();++i) {
                nonzero+=drawn[i]!=0;
                const auto difference=static_cast<unsigned>(abs(int(drawn[i])-reference[i]));
                maximum=std::max(maximum,difference); totalDifference+=difference;
                if(i%4==3) { alphaDifference+=difference; alpha+=drawn[i]; referenceAlpha+=reference[i]; }
            }
            // A8 compositing and direct glyph rendering differ in edge alpha
            // correction and subpixel rounding, especially for RTL runs. Use
            // normalized coverage/placement error, not a bit-exact edge test.
            const bool matches=nonzero>100 && totalDifference<drawn.size() &&
                alphaDifference<referenceAlpha*.12 && abs(double(alpha)-referenceAlpha)<referenceAlpha*.12;
            if (!matches) std::cout<<"TEXT PIXELS rtl="<<rtl<<" wrapped="<<wrapped<<" opacity="<<opacity
                <<" nonzero="<<nonzero<<" max-difference="<<maximum<<" alpha="<<alpha<<" reference-alpha="<<referenceAlpha<<std::endl;
            Check(matches,"cached glyphs changed trimming, wrapping, direction, color or opacity");
        }
    }
    state.rtlLayout=false; Check(MakeTextFormats(state),"restore text direction");
    const std::wstring shared=L"20/10/2026 00:43";
    capture(shared,true,false,120,0xFFFFFF,1); capture(shared,true,false,210,0xFFFFFF,1);
    auto& variants=state.textLayouts[0].layouts.at(shared).rasters;
    Check(variants.size()==2,"identical text in different columns lost its geometry variants");
    auto first=variants[0].bitmap.Get(); auto second=variants[1].bitmap.Get();
    capture(shared,true,false,120,0xFFFFFF,1); capture(shared,true,false,210,0xFFFFFF,1);
    Check(variants.size()==2 && variants[0].bitmap.Get()==first && variants[1].bitmap.Get()==second,
        "unchanged text rebuilt its glyph surfaces");
    const float oldFont=state.fontPx;
    state.fontPx=16.f;
    for(bool rtl : {false,true}) {
        state.rtlLayout=rtl; Check(MakeTextFormats(state),"intrinsic text format");
        state.columnTransitionActive=true;
        const std::wstring shortText=rtl ? L"زهرة.txt" : L"Flower.jpg";
        ID2D1Bitmap1* mask=nullptr;
        for(float width : {160.f,210.f,300.f,180.f}) {
            const auto reference=capture(shortText,false,false,width,0xFFFFFF,1);
            const auto drawn=capture(shortText,true,false,width,0xFFFFFF,1);
            size_t error=0,coverage=0;
            for(size_t i=3;i<drawn.size();i+=4) {
                error+=abs(int(drawn[i])-reference[i]); coverage+=reference[i];
            }
            Check(coverage>0 && error<coverage*.12,"intrinsic raster changed text placement during auto-fit");
            auto& masks=state.textLayouts[0].layouts.at(shortText).rasters;
            Check(masks.size()==1 && (!mask || masks.front().bitmap.Get()==mask),
                "stable text rebuilt its raster for intermediate column widths");
            mask=masks.front().bitmap.Get();
        }
        state.columnTransitionActive=false;
    }
    state.rtlLayout=false; state.fontPx=oldFont; Check(MakeTextFormats(state),"restore raster font");
    if constexpr (requires { state.frameProfile.textDeferred; }) {
        state.textRasterPainting=true;
        state.textRasterBuildTicks=static_cast<LONGLONG>(state.qpcFrequency*.002);
        const auto waiting=capture(L"Unprepared metadata",true,false,210,0xFFFFFF,1,true);
        Check(std::none_of(waiting.begin(),waiting.end(),[](BYTE value) { return value!=0; }) &&
            state.textRasterPending && !state.textLayouts[0].layouts.contains(L"Unprepared metadata"),
            "exhausted metadata budget still shaped/drew the cold value");
        const auto name=capture(L"Immediate filename.jpg",true,false,210,0xFFFFFF,1);
        Check(std::any_of(name.begin(),name.end(),[](BYTE value) { return value!=0; }),
            "metadata deferral postponed a filename");
        state.textRasterBuildTicks=0;
        const auto ready=capture(L"Unprepared metadata",true,false,210,0xFFFFFF,1,true);
        Check(std::any_of(ready.begin(),ready.end(),[](BYTE value) { return value!=0; }) &&
            !state.textLayouts[0].layouts.at(L"Unprepared metadata").rasters.empty(),
            "deferred metadata did not resume on the next paced budget");
        state.textRasterBuildTicks=static_cast<LONGLONG>(state.qpcFrequency*.002);
        Check(capture(L"Unprepared metadata",true,false,210,0xFFFFFF,1,true)==ready,
            "exhausted metadata budget removed a cell already visible from cache");
        // A value already drawn through the direct path is ready too. Changing
        // its clipping width must not turn it back into invisible cold data.
        const std::wstring seen=L"Previously visible metadata with an ellipsis";
        const auto direct=capture(seen,true,false,160,0xFFFFFF,1);
        Check(std::any_of(direct.begin(),direct.end(),[](BYTE v) { return v!=0; }) &&
            state.textLayouts[0].layouts.at(seen).rasters.empty(),"ready direct-text fixture");
        for(float width : {110.f,240.f,120.f}) {
            const auto reference=capture(seen,false,false,width,0xFFFFFF,1);
            Check(capture(seen,true,false,width,0xFFFFFF,1,true)==reference,
                "changing bounds hid previously drawn metadata after its raster budget ran out");
        }
        // The last zoom frame changes the font after zoomAnimating is cleared.
        // Filename drawing/column fitting can adopt that font before metadata.
        // Known values must remain visible even after their old GPU masks retire.
        for (bool rtl : {false,true}) {
            state.rtlLayout=rtl; state.fontPx=16.f;
            Check(MakeTextFormats(state),"zoom metadata starting font");
            for(const auto* value : {L"Previously shown date",L"Previously shown type"}) {
                state.textRasterBuildTicks=0;
                const auto shown=capture(value,true,false,210,0xFFFFFF,1,true);
                Check(std::any_of(shown.begin(),shown.end(),[](BYTE b) { return b!=0; }),
                    "font continuity fixture did not display its initial value");
            }
            ColumnTextWidth(state,L"Measured but never displayed",false);
            state.fontPx=19.f;
            Check(MakeTextFormats(state,true),"zoom metadata destination font");
            ColumnTextWidth(state,L"Filename adopting destination font",false);
            state.textRasterBuildTicks=static_cast<LONGLONG>(state.qpcFrequency*.002);
            for(const auto* value : {L"Previously shown date",L"Previously shown type"}) {
                const auto after=capture(value,true,false,210,0xFFFFFF,1,true);
                const auto reference=capture(value,false,false,210,0xFFFFFF,1);
                Check(after==reference && std::any_of(after.begin(),after.end(),[](BYTE b) { return b!=0; }),
                    "font generation change hid previously visible metadata at an exhausted budget");
            }
            const auto cold=capture(L"Measured but never displayed",true,false,210,0xFFFFFF,1,true);
            Check(std::none_of(cold.begin(),cold.end(),[](BYTE b) { return b!=0; }),
                "font continuity disabled cold metadata preparation budgeting");
        }
        state.rtlLayout=false; state.fontPx=oldFont;
        Check(MakeTextFormats(state),"restore zoom metadata font");
        state.textRasterPainting=false;
    }
    state.textRasterPainting=true; state.textRasterBuildTicks=static_cast<LONGLONG>(state.qpcFrequency*.002);
    capture(L"Budgeted cold text",true,false,120,0xFFFFFF,1);
    state.textRasterPainting=false;
    Check(state.textRasterPending && state.textLayouts[0].layouts.at(L"Budgeted cold text").rasters.empty(),
        "cold raster preparation ignored the frame budget");
    const auto middlePolicy=g_middleEllipsis.load();
    const bool grid=state.gridView, rtl=state.rtlLayout;
    for(bool gridMode : {false,true}) for(bool rtlMode : {false,true}) {
        state.gridView=gridMode; state.rtlLayout=rtlMode; MakeTextFormats(state);
        for(auto policy : {ViewOption::GridOnly,ViewOption::AlwaysOn,ViewOption::AlwaysOff}) {
            g_middleEllipsis=policy;
            const bool expected=policy==ViewOption::AlwaysOn || (policy==ViewOption::GridOnly && gridMode);
            for(const std::wstring name : {
                L"The very long name of a picture with several parts and the final extension.jpg",
                L"An extensionless folder with a very long descriptive name and trailing letters",
                L".a-long-hidden-filename-without-any-further-extension"}) {
                const float width=std::max(130.f,state.fontPx*8.f);
                const float height=state.fontPx*(gridMode ? 3.f : 1.5f);
                auto* layout=ResolveTextLayout(state,name,width,height,false,false,nullptr,gridMode,true);
                Check(layout!=nullptr,"filename trimming layout");
                DWRITE_TRIMMING trimming{}; ComPtr<IDWriteInlineObject> sign;
                Check(SUCCEEDED(layout->GetTrimming(&trimming,&sign)) &&
                    (trimming.delimiterCount>0)==(expected && !gridMode) &&
                    state.textLayouts[gridMode ? 3 : 0].layouts.at(name).middleTrim==expected,
                    "middle ellipsis dropdown did not follow its view scope");
                DWRITE_HIT_TEST_METRICS runs[64]{}; UINT32 count=0;
                Check(SUCCEEDED(layout->HitTestTextRange(static_cast<UINT32>(name.size()-3),3,
                    0,0,runs,ARRAYSIZE(runs),&count)),"filename suffix hit test");
                const bool suffixVisible=std::any_of(runs,runs+count,[&](const auto& m) {
                    return m.isText && !m.isTrimmed && m.left>=-.5f && m.left+m.width<=width+.5f &&
                        m.top>=0 && m.top+m.height<=height+.5f;
                });
                if(suffixVisible!=expected) std::wcerr<<L"TRIM suffix grid="<<gridMode<<L" rtl="<<rtlMode
                    <<L" policy="<<static_cast<int>(policy)<<L" name="<<name<<L"\n";
                Check(suffixVisible==expected,"filename ellipsis did not preserve the trailing text in the visible layout");
                Check(layout==ResolveTextLayout(state,name,130,40,false,false,nullptr,gridMode,true),
                    "unchanged filename trimming rebuilt its shaped text");
                const auto first=capture(name,true,gridMode,130,0xFFFFFF,1,false,true);
                Check(first==capture(name,true,gridMode,130,0xFFFFFF,1,false,true),
                    "middle-trimmed text changed between identical cached renders");
            }
        }
    }
    state.gridView=grid; state.rtlLayout=rtl; g_middleEllipsis=middlePolicy;
    MakeTextFormats(state);
    // Geometry-variant replacement and the shaped-value LRU must retain old
    // GPU masks until the existing after-paint cleanup, just like font changes.
    const std::wstring retiring=L"A sufficiently long cached label with many clipped geometry variants";
    capture(retiring,true,false,50,0xFFFFFF,1);
    const auto firstMask=state.textLayouts[0].layouts.at(retiring).rasters.front().bitmap.Get();
    for(float width : {60.f,70.f,80.f,90.f,100.f}) capture(retiring,true,false,width,0xFFFFFF,1);
    const auto retained=[&](ID2D1Bitmap1* mask) {
        return std::any_of(state.retiredTextLayouts.begin(),state.retiredTextLayouts.end(),
            [&](const auto& old) { return std::any_of(old.rasters.begin(),old.rasters.end(),
                [&](const auto& raster) { return raster.bitmap.Get()==mask; }); });
    };
    Check(retained(firstMask),"raster variant eviction released its GPU mask inside text drawing");
    const auto lruMask=state.textLayouts[0].layouts.at(retiring).rasters.front().bitmap.Get();
    for(int i=0;i<4096;++i)
        ResolveTextLayout(state,L"audit shaped value "+std::to_wstring(i),90,100);
    Check(!state.textLayouts[0].layouts.contains(retiring) && retained(lruMask),
        "text LRU eviction released GPU masks before paced cleanup");
    state.target->SetTarget(original.Get());
    // A real font change transfers old surfaces; paced paints must drain them.
    Check(MakeTextFormats(state),"text font refresh");
    ColumnTextWidth(state,L"Budgeted cold text",false);
    Check(!state.retiredTextLayouts.empty(),"font change synchronously discarded the old raster cache");
    for(int i=0;i<100 && !state.retiredTextLayouts.empty();++i) Paint(state);
    Check(state.retiredTextLayouts.empty(),"retired text resources stopped draining");
    std::cout<<"PASS: shared text raster pixels, trimming/wrapping/RTL, opacity, geometry variants, intrinsic reuse during auto-fit, metadata continuity across font changes, cold budget and paced glyph retirement\n";
}
