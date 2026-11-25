// XML is parsed by the production XmlLite reader. Graphics checks exercise
// production drawing into private targets, with independent pixel expectations.
namespace ThemeRegression {
inline std::shared_ptr<Theme::Document> Parse(std::wstring_view xml) {
    auto document=Theme::Parser{}.Parse(xml);
    Theme::CompileStyles(*document); return document;
}
inline void Reject(std::wstring_view xml) {
    bool rejected=false;
    try { (void)Parse(xml); } catch(const Theme::ParseError&) {rejected=true;}
    Check(rejected,"malformed/unsafe XML was accepted");
}
inline void Near(float actual,float expected,float error,const char* message) {
    Check(std::isfinite(actual) && fabsf(actual-expected)<=error,message);
}
inline std::wstring Element(std::wstring_view name,std::wstring_view properties) {
    return L"<theme><element name=\""+std::wstring(name)+L"\" "+std::wstring(properties)+L" /></theme>";
}
inline void Fonts() {
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))),"theme fixture fonts");
}
inline void Load(OptionEffects::Settings& settings,const std::wstring& xml) {
    settings.Set(L"theme",xml); LoadSettings();
    Check(SettingsFixture::strings==0,"theme setting string ownership leaked");
}
struct OffscreenPalette {
    unsigned long long next=0;
    std::optional<std::tuple<std::shared_ptr<const Theme::Document>,Theme::Mode,COLORREF,COLORREF,
        COLORREF,COLORREF,COLORREF,unsigned long long,std::array<float,3>>> previous;
    void Resolve(ViewState& view) {
        const auto theme=view.themeHighContrast ? nullptr : view.themeSnapshot;
        const auto opacity=HighlightOpacities(theme.get(),view.themeMode);
        const auto inputs=std::tuple{theme,view.themeMode,view.nativeEditBackground,view.nativeTextColor,
            view.tipBaseBackground,view.tipBaseForeground,g_selectionRgb.load(),g_swipePalette.load(),opacity};
        if(previous==inputs) return;
        previous=inputs;
        view.paletteRevision=++next;
        auto seed=std::make_shared<ContrastPalette>();
        seed->accent=g_swipePalette.load();
        view.contrastPalette=ResolveContrastPalette({theme,view.themeMode,view.nativeEditBackground,
            view.nativeTextColor,view.tipBaseBackground,view.tipBaseForeground,g_selectionRgb.load(),
            opacity,next,0,false,view.themeHighContrast,{}},
            g_swipePalette.load());
    }
};
struct Surface {
    OffscreenPalette palette;
    static constexpr UINT width=480,height=240;
    ViewState state;
    HWND frame=nullptr;
    ComPtr<ID2D1Image> previous;
    ComPtr<ID2D1Bitmap1> target,cpu;
    Surface() {
        frame=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"private theme pixels",
            WS_POPUP,-32000,-32000,width,height,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        state.hwnd=CreateWindowExW(0,L"STATIC",L"theme canvas",WS_CHILD,0,0,width,height,
            frame,nullptr,GetModuleHandleW(nullptr),nullptr);
        state.frameHwnd=state.activeTabHost=frame; state.nativeHwnd=state.hwnd;
        state.visible=state.inScope=state.detailsViewActive=true; state.fontPx=14; state.rowPx=32;
        Check(frame && state.hwnd && EnsureTarget(state),"theme GPU target");
        state.target->GetTarget(&previous);
        auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&props,&target)),"theme surface");
        props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(width,height),nullptr,0,&props,&cpu)),"theme readback");
        state.target->SetTarget(target.Get()); state.paintWidth=width;
        state.presentationOnly=true; state.presentationDpi=96;
        Check(GetClientRect(state.hwnd,&state.presentationClient),"theme presentation viewport");
    }
    ~Surface() {
        if(state.target) state.target->SetTarget(previous.Get()); target.Reset(); cpu.Reset(); previous.Reset();
        DropTarget(state); DestroyWindow(state.hwnd); state.hwnd=nullptr; DestroyWindow(frame);
    }
    template<class Draw> std::vector<DWORD> Render(Draw draw) {
        state.themeSnapshot=Theme::Snapshot(); palette.Resolve(state);
        AccentDrawScope scope(AccentPalette(state)); PrepareHighlightBrushes(state);
        state.target->BeginDraw(); state.target->SetTransform(D2D1::Matrix3x2F::Identity());
        state.target->Clear(D2D1::ColorF(0,0.f)); draw(state);
        Check(SUCCEEDED(state.target->EndDraw()) && SUCCEEDED(cpu->CopyFromBitmap(nullptr,target.Get(),nullptr)),"theme draw/readback");
        D2D1_MAPPED_RECT mapped{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&mapped)),"theme pixel mapping");
        std::vector<DWORD> pixels(width*height);
        for(UINT y=0;y<height;++y) memcpy(pixels.data()+size_t(y)*width,mapped.bits+y*mapped.pitch,width*4);
        cpu->Unmap(); return pixels;
    }
};
inline DWORD At(const std::vector<DWORD>& pixels,int x,int y) {return pixels.at(size_t(y)*Surface::width+x);}
inline unsigned Channel(DWORD pixel,unsigned shift) {return (pixel>>shift)&255;}
template<class Diagonal> void CheckRadius(Diagonal pixel,float expected,const char* message) {
    // At x=y, the corner circle crosses at r - r/sqrt(2).
    // Test whole pixels on either side of the curve, clear of the AA edge.
    const float crossing=expected*(1.f-std::sqrt(.5f));
    const int outside=int(std::floor(crossing))-1;
    const int inside=int(std::ceil(crossing));
    Check(outside>=0 && Channel(pixel(outside),24)==0 && Channel(pixel(inside),24)==255,message);
}
// Inspect a straight border edge, away from corners/text. Expectations describe
// the visible pattern rather than calling the mod's stroke-style helpers.
template<class Draw> void BorderPatterns(Draw draw) {
    size_t dashedRun=0,dottedRun=0;
    for(const auto* style:{L"solid",L"dashed",L"dotted",L"none"}) {
        Regression::Scope scope("border-style="+Regression::Narrow(style));
        const auto row=draw(style);
        Check(row.size()>=16,"border pattern sample is too short");
        size_t ink=0,gaps=0,run=0,longest=0;
        for(DWORD p:row) {
            Check((p&0xffff)==0 && Channel(p,16)==Channel(p,24),"border pattern changed its red color/premultiplication");
            if(Channel(p,24)>64) {++ink; longest=std::max(longest,++run);} else {++gaps;run=0;}
        }
        if(std::wstring_view(style)==L"solid") Check(ink==row.size(),"solid border contains gaps");
        else if(std::wstring_view(style)==L"none")
            Check(std::all_of(row.begin(),row.end(),[](DWORD p){return p==0;}),"disabled border still draws pixels");
        else {
            Check(ink>0 && gaps>0,"pattern border has no ink or no gaps");
            if(std::wstring_view(style)==L"dashed") dashedRun=longest; else dottedRun=longest;
        }
    }
    Check(dashedRun>dottedRun,"dashes and round dots did not produce distinct visible lengths");
}
inline void BorderCacheReleased(const ThemeBorderStrokes& cache) {
    for(const auto& stroke:cache.styles) Check(!stroke,"device teardown retained a border stroke style");
}

struct Tooltip {
    OffscreenPalette palette;
    StyleFixture::Session style;
    LayeredPixelsFixture::Session capture;
    ViewState state;
    HWND window=nullptr;
    explicit Tooltip(UINT dpi=96) {
        StyleFixture::dpi=dpi; StyleFixture::text=RGB(240,240,240);
        StyleFixture::themeFont.lfHeight=-MulDiv(12,dpi,96);
        wcscpy_s(StyleFixture::themeFont.lfFaceName,L"Segoe UI");
        window=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"private themed tooltip",
            WS_POPUP,-32000,-32000,480,240,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(window!=nullptr,"theme tooltip window");
        state.hwnd=window; state.itemTip.hwnd=window; state.fontPx=float(MulDiv(14,dpi,96));
        state.nativePaletteResolved=true; state.nativeAppBackground=RGB(24,24,24);
        state.itemTip.name=L"pagefile.sys";
        state.itemTip.rows={{L"Size",L"800 MB"},{L"Date",L"13/09/2025 10:44"}};
        Check(SUCCEEDED(g_writeFactory->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,state.fontPx,L"",&state.textFormat)),"theme tooltip font");
    }
    ~Tooltip() {state.itemTip.DropTarget(); state.itemTip.hwnd=nullptr; state.hwnd=nullptr; DestroyWindow(window);}
    void Show(Theme::Mode mode=Theme::Mode::Light,bool highContrast=false) {
        state.themeSnapshot=Theme::Snapshot(); state.themeMode=mode; state.itemTip.highContrast=highContrast;
        state.presentationOnly=true; state.presentationDpi=StyleFixture::dpi;
        ResolveItemTipPalette(state); palette.Resolve(state); ResolveItemTipPalette(state);
        Check(LayoutItemTip(state,RECT{0,0,800,600}),"themed tooltip layout");
        RenderItemTip(state.itemTip);
        Check(!LayeredPixelsFixture::pixels.empty(),"tooltip submitted no pixels");
    }
    DWORD Pixel(int x,int y) const {
        const auto& tip=state.itemTip;
        return LayeredPixelsFixture::pixels.at(size_t(y+tip.margin)*LayeredPixelsFixture::size.cx+x+tip.margin);
    }
};
}

static void TestThemedColumnHeader() {
    using namespace ThemeRegression;
    StyleFixture::Session style;
    OptionEffects::Settings settings;
    Fonts();
    const auto drive=Parse(LR"(<theme><element name="DrawDriveBar">
        <element name="fill" height="7px" />
        <element name="track" height="3px"><dark height="4px" /></element>
    </element></theme>)");
    Check(drive->driveBars[0][0].surface.height==7.f && drive->driveBars[0][1].surface.height==3.f &&
        drive->driveBars[1][1].surface.height==4.f,"nested storage bar names or their appearance scopes changed");
    for(int dpi:{96,144,192}) {
        Regression::Scope scope("column header dpi="+std::to_string(dpi));
        StyleFixture::dpi=dpi;
        PresentationFixture::Tab tab(L"themed column header",1,false,false,false,640,380);
        auto& state=tab.state;
        state.selectedFlags.Fill(0); state.selected=state.hovered=-1;
        ApplyViewLayout(state,{FVM_DETAILS,16},false);
        state.columns[0].width=MulDiv(220,dpi,96);
        ExplorerColumn type;
        type.key=PKEY_ItemTypeText; type.title=L"Type"; type.width=MulDiv(70,dpi,96);
        state.columns.push_back(std::move(type));
        UpdateColumns(state);
        Check(MakeTextFormats(state),"column header fonts");
        TouchInputTest::RenderedTouch output;
        const auto load=[&](const wchar_t* xml) {
            Load(settings,xml);
            state.themeSnapshot=Theme::Snapshot();
            UpdateMetrics(state);
        };
        const auto checkFont=[&](float expected) {
            auto* caption=ResolveTextLayout(state,L"Name",120.f,state.headerPx,true);
            float actual=0.f;
            Check(caption && SUCCEEDED(caption->GetFontSize(0,&actual)),"themed header caption layout");
            Near(actual,expected,.01f,"the header caption did not use the expected font size");
            return caption;
        };
        load(LR"(<theme><element name="DrawColumnHeader" border-opacity="35%" height="auto" font-size="auto" text-color="accent">
            <element name="horizontal-divider" border-width="1.5px" border-color="accent"><dark border-opacity="none" /></element>
            <element name="vertical-divider" border-width="0px"><dark border-width="2px" border-color="#00ff00" border-opacity="50%" /></element>
        </element></theme>)");
        g_swipePalette=RGB(255,0,0);
        const float initialRow=state.rowPx;
        Check(state.headerPx==initialRow,"auto header height did not match its row");
        checkFont(state.fontPx);
        BeginAnimatedZoom(state,56);
        AdvanceRowHeight(state,true);
        Check(state.rowPx==float(MulDiv(56,dpi,96)) && state.headerPx==state.rowPx &&
            state.rowPx!=initialRow,"auto header height did not follow zoom");
        checkFont(state.fontPx);
        const int bottom=int(state.headerPx), thickness=int(std::round(1.5*dpi/96.0));
        const auto light=output.Capture(state);
        const auto pixels=[&](const auto& frame,int x,int y) {
            const auto width=output.renderer.backbuffer->GetPixelSize().width;
            const auto offset=(size_t(y)*width+x)*4;
            return std::array<unsigned,4>{frame.at(offset+2),frame.at(offset+1),frame.at(offset),frame.at(offset+3)};
        };
        for(int y=bottom-thickness-1;y<bottom;++y) {
            const auto pixel=pixels(light,4,y);
            const unsigned expected=y>=bottom-thickness ? 89u:0u;
            Check(pixel==std::array<unsigned,4>{expected,0,0,expected},
                "nested horizontal rule did not inherit opacity, resolve accent or scale its thickness");
        }
        size_t captionPixels=0;
        for(int y=0;y<bottom-thickness;++y) for(int x=0;x<200;++x) {
            const auto pixel=pixels(light,x,y);
            if(pixel[0]>0 && pixel[1]==0 && pixel[2]==0) ++captionPixels;
        }
        Check(captionPixels>0,"accent header caption was not drawn");
        const auto checkVertical=[&](const auto& frame,bool enabled) {
            const int center=int(std::round(ListStartX(state)+state.columns[0].end));
            const int width=MulDiv(2,dpi,96);
            // The right edge of the Name column is clear of its caption.
            const int start=int(std::floor(center-width*.5f+.5f));
            for(int x=start-1;x<=start+width;++x) {
                const unsigned expected=enabled && x>=start && x<start+width ? 128u:0u;
                Check(pixels(frame,x,bottom/2)==std::array<unsigned,4>{0,expected,0,expected},
                    "vertical divider width, opacity or independent appearance override is wrong");
            }
        };
        checkVertical(light,false);
        state.themeMode=Theme::Mode::Dark;
        const auto dark=output.Capture(state);
        checkVertical(dark,true);
        Check(pixels(dark,4,bottom-1)==std::array<unsigned,4>{},
            "disabling the horizontal subelement affected only part of its rule");
        state.themeMode=Theme::Mode::Light;

        load(LR"(<theme><element name="DrawColumnHeader" font-size="42%" height="12px" /></theme>)");
        const float expected=.42f*MulDiv(56,dpi,96);
        auto* caption=checkFont(expected);
        DWRITE_TEXT_METRICS metrics{};
        Check(SUCCEEDED(caption->GetMetrics(&metrics)) && state.headerPx>=metrics.height+2*MulDiv(2,dpi,96),
            "a fixed header height clipped the percentage-sized caption or its padding");
        load(LR"(<theme><element name="DrawColumnHeader" font-size="20px" /></theme>)");
        checkFont(20.f*dpi/96.f);
        Check(state.headerPx==float(MulDiv(38,dpi,96)),"omitting height lost the regular Details default");
        state.themeHighContrast=true;
        UpdateMetrics(state);
        checkFont(state.headerFormat->GetFontSize());
    }
}

static void TestThemedGroupHeading() {
    using namespace ThemeRegression;
    StyleFixture::Session style;
    OptionEffects::Settings settings;
    Fonts();
    Surface surface;
    auto& state=surface.state;
    constexpr int dpi=144;
    StyleFixture::dpi=dpi;
    state.presentationDpi=dpi;
    state.themeMode=Theme::Mode::Light;
    state.rowPx=80.f;
    state.fontPx=18.f;
    Check(MakeTextFormats(state),"group heading fonts");
    const std::wstring title=L"MMMMMMMM";
    const auto verify=[&](const wchar_t* attributes,float expectedFont,float requestedHeight) {
        Load(settings,Element(L"PaintGroupHeading",attributes));
        state.themeSnapshot=Theme::Snapshot();
        const float height=GroupHeadingHeight(state);
        ComPtr<IDWriteTextLayout> reference;
        Check(SUCCEEDED(g_writeFactory->CreateTextLayout(title.data(),UINT32(title.size()),
            state.textFormat.Get(),400.f,200.f,&reference)) &&
            SUCCEEDED(reference->SetFontSize(expectedFont,{0,UINT32(title.size())})),
            "independent group title layout");
        DWRITE_TEXT_METRICS metrics{};
        Check(SUCCEEDED(reference->GetMetrics(&metrics)) &&
            height>=metrics.height+2*MulDiv(2,dpi,96),"group heading clipped the themed title or padding");
        if(requestedHeight>0.f) Near(height,requestedHeight,.01f,"group heading height");
        for(bool raster : {false,true}) {
            state.textRasterUnavailable=!raster;
            const auto pixels=surface.Render([&](ViewState& view) {
                PaintGroupHeading(view,title,false,0.f,height,1.f,Surface::width,Surface::width,0xffffff);
            });
            int left=Surface::width,right=-1;
            for(int y=0;y<int(height);++y) for(int x=0;x<int(Surface::width);++x) {
                const DWORD pixel=At(pixels,x,y);
                if(Channel(pixel,16)>0 && (pixel&0xffff)==0) {
                    left=std::min(left,x);
                    right=std::max(right,x);
                }
            }
            // M has ink near both edges of its advance. An ellipsis or a
            // body-sized raster cannot cover the independently measured span.
            Check(right-left+1>=metrics.width-expectedFont*.5f &&
                right-left+1<=metrics.width+1.f,"painted group title was truncated or used the wrong font size");
        }
    };
    verify(LR"(height="auto" font-size="auto" text-color="#ff0000")",18.f,80.f);
    state.rowPx=100.f;
    state.fontPx=22.f;
    Check(MakeTextFormats(state),"group heading zoom fonts");
    verify(LR"(height="auto" font-size="auto" text-color="#ff0000")",22.f,100.f);
    verify(LR"(height="8px" font-size="40%" text-color="#ff0000")",40.f,0.f);
    verify(LR"(height="50px" font-size="20px" text-color="#ff0000")",30.f,75.f);
}

static void TestThemedThumbnail() {
    using namespace ThemeRegression;
    StyleFixture::Session style; OptionEffects::Settings settings; Fonts(); Surface surface;
    auto& state=surface.state;
    state.gridView=true;
    IconRecord landscape,portrait,thin,association;
    landscape.pixels=DecorateThumbnail(DragPileFixture::Solid(64,32,false),ThumbnailStyle{});
    portrait.pixels=DecorateThumbnail(DragPileFixture::Solid(32,64,false),ThumbnailStyle{});
    thin.pixels=DecorateThumbnail(DragPileFixture::Solid(64,8,false),ThumbnailStyle{});
    association.pixels=DragPileFixture::Solid(64,32,false);
    for(auto* image:{&landscape,&portrait,&thin,&association})
        Check(UploadIconBitmap(state,*image),"thumbnail theme fixture bitmap");
    const auto theme=[&](const std::wstring& attributes) { Load(settings,Element(L"DrawThumbnail",attributes)); };
    const auto render=[&](IconRecord& image,float itemHeight) {
        return surface.Render([&](ViewState& view) {
            Check(DrawCachedIcon(view,image,100.f,25.f,1.f,0.f,nullptr,nullptr,itemHeight),"themed thumbnail draw");
        });
    };
    for(int dpi:{96,144,192}) for(bool rtl:{false,true}) {
        Regression::Scope scope("thumbnail dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl));
        const float scale=dpi/96.f;
        state.presentationDpi=state.textFormatDpi=dpi; state.iconPx=80*scale; state.rtlLayout=rtl;
        theme(LR"(border-radius="15%" shadow-opacity="none")");
        for(const auto& sample:{std::array{80,40},std::array{40,80},std::array{80,10}}) {
            const int width=MulDiv(sample[0],dpi,96),height=MulDiv(sample[1],dpi,96);
            const int left=100+int(std::ceil((state.iconPx-width)*.5f));
            const int physical=rtl ? int(Surface::width)-left-width:left;
            const int top=25+int(std::ceil((state.iconPx-height)*.5f));
            auto& image=sample[1]==80 ? portrait:sample[1]==10 ? thin:landscape;
            for(float item:{100.f,140.f}) {
                const auto pixels=render(image,item*scale);
                CheckRadius([&](int offset) {return At(pixels,physical+offset,top+offset);},
                    std::min(item*.15f*scale,std::min(width,height)*.5f),
                    "thumbnail percentage did not use the item height or cap to the fitted artwork");
                Check(At(pixels,physical+width/2,top+height/2)==0xffdc0000u,"thumbnail artwork was lost");
            }
        }

        const int width=MulDiv(80,dpi,96),height=MulDiv(40,dpi,96),top=25+(width-height)/2,bottom=top+height;
        const int left=rtl ? int(Surface::width)-100-width:100,mid=left+width/2;
        const int borderWidth=MulDiv(10,dpi,96),shadowY=bottom+borderWidth+MulDiv(2,dpi,96);
        theme(LR"(border-radius="0%" border-width="10px" border-color="#ffffff" shadow-opacity="0")");
        const auto square=render(landscape,100*scale);
        Check(At(square,left-borderWidth,top-borderWidth)==0xffffffffu &&
            At(square,left,top)==0xffdc0000u && At(square,left-borderWidth-1,top)==0,
            "square thumbnail border rounded its corner, covered artwork, or escaped the frame");
        Check(At(square,mid,bottom+borderWidth-1)==0xffffffffu && At(square,mid,shadowY)==0,
            "zero shadow opacity drew outside the enlarged frame");
        theme(LR"(border-radius="0%" border-width="10px" border-color="#ffffff" shadow-opacity="100%")");
        const auto shadow=At(render(landscape,100*scale),mid,shadowY);
        theme(LR"(border-radius="0%" border-width="10px" border-color="#ffffff" shadow-opacity="50%")");
        const auto dim=At(render(landscape,100*scale),mid,shadowY);
        Check((shadow&0xffffff)==0 && Channel(shadow,24)>Channel(dim,24) && Channel(dim,24)>0,
            "thumbnail shadow did not follow the outer border or honor its opacity");

        theme(LR"(border-radius="0%" shadow-opacity="none")");
        const auto undecorated=render(landscape,100*scale);
        for(const auto* disabled:{LR"(border-width="10px" border-color="transparent")",
                LR"(border-width="0px" border-color="#ffffff")",LR"(border-width="10px" border-style="none")"}) {
            theme(std::wstring(LR"(border-radius="0%" shadow-opacity="none" )")+disabled);
            Check(render(landscape,100*scale)==undecorated,"disabled border changed thumbnail pixels or bounds");
        }
        BorderPatterns([&](const wchar_t* pattern) {
            theme(L"border-radius=\"0%\" shadow-opacity=\"0\" border-width=\"2px\" border-color=\"#ff0000\" border-style=\""+
                std::wstring(pattern)+L"\"");
            const auto pixels=render(landscape,100*scale);
            std::vector<DWORD> edge;
            for(int offset=MulDiv(10,dpi,96);offset<width-MulDiv(10,dpi,96);++offset)
                edge.push_back(At(pixels,left+offset,top-MulDiv(1,dpi,96)));
            return edge;
        });
        Load(settings,L"");
        const auto native=render(landscape,100*scale),icon=render(association,100*scale);
        theme(LR"(border-radius="0%" border-width="10px" border-color="#ffffff" shadow-opacity="0")");
        Check(render(association,100*scale)==icon,"thumbnail theme changed a file association icon");
        state.themeHighContrast=true;
        Check(render(landscape,100*scale)==native,"high contrast retained thumbnail customization");
        state.themeHighContrast=false;
    }
    std::cout<<"PASS: thumbnail item-height radius, aspect ratios, square exterior borders, shadow extent/opacity and fallbacks\n";
}

static void TestThemedDragThumbnail() {
    using namespace ThemeRegression;
    StyleFixture::Session style; OptionEffects::Settings settings; Fonts(); Surface surface;
    IconRecord record;
    record.pixels=DecorateThumbnail(DragPileFixture::Solid(64,32,false),ThumbnailStyle{});
    Check(UploadIconBitmap(surface.state,record),"drag thumbnail theme fixture bitmap");
    std::array<ComPtr<ID2D1Bitmap>,kDragPreviewImages> artwork;
    artwork[0]=record.bitmap;
    for(int dpi:{96,144,192}) {
        Regression::Scope scope("drag thumbnail dpi="+std::to_string(dpi));
        DragPreview preview; preview.dpi=dpi; preview.side=float(MulDiv(80,dpi,96));
        preview.effect=DROPEFFECT_COPY; preview.pixels=record.pixels; preview.itemHeight96=120.f;
        const auto load=[&](const wchar_t* attributes) {
            Load(settings,Element(L"DrawThumbnail",attributes));
            surface.state.themeSnapshot=Theme::Snapshot();
            preview.thumbnailTheme=CaptureThumbnailTheme(surface.state);
        };
        const auto render=[&](bool shadow) {
            std::array<DragShadow,kDragPreviewImages> shadows;
            if(shadow) {
                shadows[0]=CreateDragShadow(surface.state.target.Get(),artwork[0].Get(),preview,preview.side,dpi,
                    ThumbnailRadius96(*preview.thumbnailTheme,preview.itemHeight96),
                    DragThumbnailBorder(preview,preview.pixels),surface.state.themeBorderStrokes);
                Check(shadows[0].bitmap!=nullptr,"drag thumbnail shadow bitmap");
            }
            return surface.Render([&](ViewState& view) {
                DrawDragPreview(view.target.Get(),preview,&artwork,nullptr,nullptr,&shadows,&view.themeBorderStrokes);
            });
        };
        const auto origin=[&] {
            const auto rect=LayoutDragPreview(preview).artwork;
            return POINT{LONG(std::floor(rect.left+.5f)),LONG(std::floor(rect.top+.5f))};
        };
        load(LR"(border-radius="10%")");
        auto pos=origin();
        const auto rounded=render(false);
        CheckRadius([&](int offset) {return At(rounded,pos.x+offset,pos.y+offset);},
            float(MulDiv(12,dpi,96)),"drag thumbnail radius did not use the source item height");
        load(LR"(border-radius="0%")");
        pos=origin();
        const int width=MulDiv(10,dpi,96),probe=width+MulDiv(4,dpi,96),mid=MulDiv(20,dpi,96);
        const auto unframed=At(render(true),pos.x-probe,pos.y+mid);
        load(LR"(border-radius="0%" border-width="10px" border-color="#ffffff")");
        pos=origin();
        const auto framed=render(false);
        Check(At(framed,pos.x-width,pos.y-width)==0xffffffffu && At(framed,pos.x,pos.y)==0xffdc0000u,
            "drag thumbnail did not keep the artwork and exterior border square");
        Check(At(framed,pos.x-probe,pos.y+mid)==0,"drag thumbnail border exceeded its requested width");
        const auto shadow=At(render(true),pos.x-probe,pos.y+mid);
        Check((shadow&0xffffff)==0 && Channel(shadow,24)>Channel(unframed,24),
            "drag thumbnail shadow did not grow with the visible frame");
    }
    std::cout<<"PASS: drag thumbnail percentage radius, square exterior frame and blurred frame silhouette\n";
}

static void TestThemeParserXml() {
    using namespace ThemeRegression;
    Check(Parse(L" \t\r\n ")->elements.empty(),"blank theme did not clear");
    auto doc=Parse(LR"(<?xml version="1.0"?><theme><!-- note --><element name="unregistered:state" unknown="&lt;&amp;&quot;&gt;" title="Ω" /></theme>)");
    Check(*doc->Find(Theme::Mode::Dark,L"unregistered:state",L"unknown")==L"<&\">",
        "XML entities or unregistered properties were not retained");
    Check(*doc->Find(Theme::Mode::Light,L"unregistered:state",L"title")==L"\u03a9","UTF-16 setting text was damaged");
    const wchar_t* fragments[]={
        LR"(<theme><alpha background="#123456" /></theme><theme><beta border-width="2" /></theme>)",
        LR"(<theme><alpha background="#123456"></alpha><beta border-width="2"></beta></theme>)",
        LR"(<alpha background="#123456" /><beta border-width="2" /><theme><gamma /></theme>)"};
    for(const auto* xml:fragments) {
        const auto parsed=Parse(xml);
        Check(parsed->Find(Theme::Mode::Light,L"alpha",L"background") &&
            *parsed->Find(Theme::Mode::Dark,L"beta",L"border-width")==L"2","wrapper/paired-tag fragment changed properties");
    }
    doc=Parse(LR"(<theme><element name="same" value="first"><dark value="dark" /></element></theme><same value="last" />)");
    Check(*doc->Find(Theme::Mode::Light,L"same",L"value")==L"last" &&
        *doc->Find(Theme::Mode::Dark,L"same",L"value")==L"dark","merged entries lost mode/universal precedence");
    doc=Parse(L"<x a='1' a='2'/>");
    Check(*doc->Find(Theme::Mode::Light,L"x",L"a")==L"2","duplicate attributes did not use the last value");
    for(const auto* xml:{L"<theme>",L"<theme><x></theme>",L"<x>text</x>",
        L"<x><y /></x>",L"<light background='#fff'/>",L"<x><dark><light /></dark></x>",
        L"<x xmlns='urn:foreign'/>",L"<element name=''/>",L"<x bad='&undefined;'/>",
        L"<!DOCTYPE theme [<!ENTITY x 'expanded'>]><theme><x a='&x;'/></theme>"}) Reject(xml);
    Reject(std::wstring(65537,L' '));
    Reject(L"<element name='"+std::wstring(257,L'a')+L"' />");
    std::wstring deep;
    for(int i=0;i<18;++i) deep+=L"<theme>";
    for(int i=0;i<18;++i) deep+=L"</theme>";
    Reject(deep);
    std::wstring many=L"<theme>";
    for(int i=0;i<2050;++i) many+=L"<x />";
    Reject(many+L"</theme>");
    std::cout<<"PASS: native XML parsing, generic properties, entities, fragment tolerance, scopes and resource/security limits\n";
}

static void TestThemeParserValues() {
    using namespace ThemeRegression;
    Check(Theme::ReadColor(L"#39f").rgb==0x3399ff && Theme::ReadColor(L"#ABC").rgb==0xaabbcc,
        "three-digit CSS colors were not expanded per channel");
    const auto doc=Parse(LR"xml(<theme>
      <element name="FillSelectionRect" background="#123" border-width="1px" />
      <element name="FillSelectionRect:selected" background="linear-gradient(to right, #ff000080, #0000ff)">
        <dark background-color="#abcdef" border-width="2" /></element>
      <element name="FillSelectionRect:hover" background="not-a-color" border-width="-1" />
      <element name="RenderItemTip" border-radius="9px" box-shadow="-4px 6px 12px 2px #12345680">
        <dark box-shadow="none" /></element>
    </theme>)xml");
    const auto& light=doc->highlights[0]; const auto& dark=doc->highlights[1];
    Check(light[0].background.kind==Theme::Paint::Kind::Linear && light[0].background.stops[0].color.rgb==0xff0000,
        "gradient colors not compiled");
    Near(light[0].background.stops[0].color.alpha,128.f/255.f,.000001f,"CSS suffix alpha was not parsed");
    Check(dark[0].background.kind==Theme::Paint::Kind::Solid && dark[0].background.color.rgb==0xabcdef &&
        dark[0].borderWidth==2.f,"mode background-color did not override universal background");
    Check(light[1].background.color.rgb==0x112233 && light[1].borderWidth==1.f,
        "bad property values destroyed inherited values");
    Check(dark[2].background.color.rgb==0xabcdef && dark[2].borderWidth==2.f,
        "selected-hover did not inherit selected properties");
    const auto& tip=doc->tooltips[0];
    Check(tip.borderRadius==9 && tip.shadow && tip.shadow->x==-4 && tip.shadow->y==6 && tip.shadow->blur==12 &&
        tip.shadow->spread==2 && tip.shadow->color.rgb==0x123456 && !doc->tooltips[1].shadow->enabled,
        "tooltip corner/shadow compilation or mode override changed");
    for(const auto& [direction,x,y]:std::initializer_list<std::tuple<const wchar_t*,float,float>>{
        {L"to right",1,0},{L"to left",-1,0},{L"to top",0,-1},{L"to bottom",0,1},{L"90deg",1,0},{L"180deg",0,1}}) {
        const auto paint=Theme::ReadPaint(L"linear-gradient("+std::wstring(direction)+L", #000, #fff)");
        Near(paint.direction.x,x,.00001f,"gradient X direction"); Near(paint.direction.y,y,.00001f,"gradient Y direction");
    }
    Check(Theme::ReadPaint(L"linear-gradient(to bottom right, #000, #fff)").cornerDirection,"corner direction lost");
    auto paint=Theme::ReadPaint(L"linear-gradient(#000, #100 20%, #200, #300 60%, #400)");
    for(size_t i=0;i<5;++i) Near(paint.stops[i].offset,std::array{0.f,.2f,.4f,.6f,1.f}[i],.000001f,"implicit stop spacing");
    paint=Theme::ReadPaint(L"linear-gradient(#000 0%, #000 50%, #fff 50%, #fff 100%)");
    Check(paint.stops[1].offset==paint.stops[2].offset,"hard stop was smoothed away");
    Check(Theme::ReadPaint(L"linear-gradient(#12345680, #12345680)").kind==Theme::Paint::Kind::Solid,
        "equal gradient colors still require a gradient");
    for(const auto* value:{L"linear-gradient(#000)",L"linear-gradient(#000 70%, #fff 20%)",L"linear-gradient(to somewhere, #000, #fff)",
        L"linear-gradient(#000,)",L"linear-gradient(#000 -1%, #fff)",L"#gggggg"}) {
        bool failed=false; try {(void)Theme::ReadPaint(value);} catch(const std::invalid_argument&) {failed=true;}
        Check(failed,"invalid paint accepted");
    }
    for(const auto* value:{L"0 0 -1px #000",L"0 0 65px #000",L"129px 0 #000",L"0 0 0 -129px #000",L"inset 0 0 #000",L"0 0 #000, 0 0 #fff"}) {
        bool failed=false; try {(void)Theme::ReadShadow(value);} catch(const std::invalid_argument&) {failed=true;}
        Check(failed,"invalid/unsupported shadow accepted");
    }
    const auto fallback=Parse(LR"(<theme><element name="RenderItemTip" background="#123456" box-shadow="0 0 4px #000"><dark background="nan" box-shadow="0 0 99px" /></element></theme>)");
    Check(fallback->tooltips[1].background.color.rgb==0x123456 && fallback->tooltips[1].shadow->blur==4,
        "invalid mode values did not keep inherited valid properties");
    std::cout<<"PASS: canonical style bindings, inheritance, CSS colors, gradients, hard stops and bounded shadows\n";
}

static void TestThemeParserSettings() {
    using namespace ThemeRegression;
    OptionEffects::Settings settings;
    settings.Set(L"theme",Element(L"RenderItemTip",LR"(background="#123456")"));
    SettingsFixture::reads.clear();
    Check(!LoadSettings(),"theme-only edit was reported as an unrelated option change");
    const auto valid=Theme::Snapshot();
    Check(valid && valid->tooltips[0].background.color.rgb==0x123456 && SettingsFixture::reads[L"theme"]==1 &&
        SettingsFixture::strings==0,"theme setting not loaded once or host string leaked");
    Load(settings,L"<theme><element");
    Check(Theme::Snapshot()==valid,"malformed document replaced the last valid snapshot");
    Load(settings,LR"(<theme><element name="RenderItemTip" background="#abcdef" broken=unquoted border-width="2px" />)");
    Check(Theme::Snapshot()->tooltips[0].background.color.rgb==0xabcdef && Theme::Snapshot()->tooltips[0].borderWidth==2,
        "settings load discarded intact properties around malformed XML");
    Load(settings,L" \t ");
    Check(Theme::Snapshot()->elements.empty() && !Theme::Snapshot()->highlightEnabled[0],"clearing theme retained overrides");
    Check(valid->tooltips[0].background.color.rgb==0x123456,"publishing invalidated a retained immutable snapshot");
    settings.Set(L"Selection.selectionOpacity",L"41");
    Check(LoadSettings(),"ordinary option change was mistaken for a theme-only edit");
    std::cout<<"PASS: real settings host, theme-only routing decision, last-valid retention, blank reset and snapshot ownership\n";
}

static void TestThemeParserRecovery() {
    using namespace ThemeRegression;
    const auto doc=Parse(LR"xml(<theme>
      <element name="FillSelectionRect:selected" background="#123" border-width="1" background="#456">
        <dark background="#abcdef" />
      </element>
      <element name="FillSelectionRect:selected" border-color="#ff0000" broken=unquoted border-width="2px" />
      <element name="" ignored="#000" />
      <element name="FillSelectionRect:hover" background="#00ff00" unfinished="oops
      <element name="FillSelectionRect:selected:hover" background="#0000ff" />
      <element name="RenderItemTip" background="#333333" border-radius="8px" broken="&undefined;" box-shadow="0 4px 8px #00000080" />
    </theme>)xml");
    const auto& selected=doc->highlights[0][0];
    Check(selected.background.color.rgb==0x445566 && selected.borderWidth==2 && selected.borderColor.rgb==0xff0000,
        "repeated element/attribute definitions did not merge with the last intact value");
    Check(doc->highlights[1][0].background.color.rgb==0xabcdef && doc->highlights[1][0].borderWidth==2,
        "recovery lost mode-specific properties or shared later overrides");
    Check(doc->highlights[0][1].background.color.rgb==0x00ff00 && doc->highlights[0][2].background.color.rgb==0x0000ff,
        "a broken quote discarded its intact prefix or a later element");
    Check(doc->tooltips[0].background.color.rgb==0x333333 && doc->tooltips[0].borderRadius==8 &&
        doc->tooltips[0].shadow && doc->tooltips[0].shadow->blur==8,"bad entity erased intact sibling properties");
    const auto unclosed=Parse(LR"(<theme><element name="RenderItemTip" background="#abcdef" border-width="3px")");
    Check(unclosed->tooltips[0].background.color.rgb==0xabcdef && unclosed->tooltips[0].borderWidth==3,
        "missing tag closure erased complete attributes");
    const auto merged=Parse(LR"(<theme><element name="RenderItemTip" background="#111111"><dark border-radius="7" /></element></theme>
        <theme><element name="RenderItemTip" background="#222222"><dark border-radius="9" /></element></theme>)");
    Check(merged->tooltips[0].background.color.rgb==0x222222 && merged->tooltips[1].borderRadius==9,
        "repeated element/mode blocks did not merge with last property precedence");
    const auto isolated=Parse(LR"(<theme><element name="RenderItemTip" background="#123456">
        <element broken="unknown"><dark background="#ff0000" /></element></theme>)");
    Check(isolated->tooltips[1].background.color.rgb==0x123456,"damaged element's appearance leaked into its preceding sibling");
    Reject(LR"(<!DOCTYPE theme [<!ENTITY e SYSTEM "file:///not-accessed">]><theme><x a="ok" />)");
    Reject(std::wstring(65537,L' '));
    std::cout<<"PASS: repeated element/mode merges, duplicate attributes, bad attribute/entity recovery, missing quotes/closures and later intact elements\n";
}

static void TestThemedHighlight(Theme::Highlight kind,bool common=false) {
    using namespace ThemeRegression;
    StyleFixture::Session style;
    OptionEffects::Settings settings; Fonts(); Surface surface;
    settings.Set(L"Selection.selectionRadius",L"0"); settings.Set(L"Selection.gridSelectionRadius",L"0");
    settings.Set(L"Selection.selectionOpacity",L"100"); settings.Set(L"Hover.hoverOpacity",L"100");
    settings.Set(L"Hover.hoverSelectedOpacity",L"100"); settings.Set(L"Selection.selectionColor",L"#112233");
    LoadSettings();
    const wchar_t* name=common ? L"FillSelectionRect" : kind==Theme::Highlight::Selected ? L"FillSelectionRect:selected" :
        kind==Theme::Highlight::Hover ? L"FillSelectionRect:hover" : L"FillSelectionRect:selected:hover";
    const auto render=[&](float alpha=1.f) {
        return surface.Render([&](ViewState& state) {
            const float scale=state.presentationDpi/96.f;
            FillSelectionRect(state,D2D1::RectF(20*scale,20*scale,200*scale,80*scale),alpha,nullptr,false,
                kind==Theme::Highlight::SelectedHover ? Theme::Highlight::Hover:kind,
                kind==Theme::Highlight::SelectedHover ? 1.f:0.f);
        });
    };
    for(UINT dpi:{96u,144u,192u}) for(bool rtl:{false,true}) {
        Regression::Scope scope(std::string("theme element=")+Regression::Narrow(name)+" dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl));
        surface.state.presentationDpi=dpi; surface.state.rtlLayout=rtl;
        const int left=rtl ? 480-MulDiv(200,dpi,96):MulDiv(20,dpi,96);
        const int right=rtl ? 480-MulDiv(20,dpi,96):MulDiv(200,dpi,96);
        const int y=MulDiv(50,dpi,96);
        Load(settings,Element(name,LR"(background="transparent" border-color="#ff0000" border-width="1px")"));
        const auto border=render(); const int width=int(std::floor(dpi/96.f+.5f));
        for(int x=left-2;x<left+width+3;++x)
            Check(At(border,x,y)==(x>=left && x<left+width ? 0xffff0000u:0u),"border width/inset is not independent whole-pixel DPI arithmetic");
        Check(At(border,(left+right)/2,y)==0,"transparent border-only theme gained a fill");
        BorderPatterns([&](const wchar_t* pattern) {
            Load(settings,Element(name,L"background=\"transparent\" border-color=\"#ff0000\" border-width=\"2px\" border-style=\""+
                std::wstring(pattern)+L"\""));
            const auto pixels=render(); Check(pixels==render(),"warm highlight border changed pixels");
            std::vector<DWORD> edge;
            for(int x=left+MulDiv(10,dpi,96);x<right-MulDiv(10,dpi,96);++x) edge.push_back(At(pixels,x,MulDiv(20,dpi,96)));
            Check(At(pixels,left-1,y)==0,"highlight pattern escaped its bounds");
            return edge;
        });
        Load(settings,Element(name,LR"xml(background="linear-gradient(to right, #ff0000, #0000ff)" border-width="0")xml"));
        const auto reads=SettingsFixture::reads;
        const auto gradient=render(); Check(gradient==render(),"warm dither cache changed pixels");
        Check(reads==SettingsFixture::reads,"drawing reparsed settings");
        Check(At(gradient,left-1,y)==0 && At(gradient,right,y)==0,"gradient changed element bounds");
        for(int x:{left+(right-left)/4,left+3*(right-left)/4}) {
            double red=0,blue=0;
            for(int row=y;row<y+8;++row) {red+=Channel(At(gradient,x,row),16); blue+=Channel(At(gradient,x,row),0);}
            const double t=(x+.5-left)/double(right-left);
            Check(std::abs(red/8.-255*(1-t))<2 && std::abs(blue/8.-255*t)<2,"dithered gradient differs from independently interpolated color");
        }
        size_t bytes=0,brushes=0;
        for(const auto& stateRamps:surface.state.highlightResources.ramps) for(const auto& ramp:stateRamps.slots) if(ramp.brush) {
            ComPtr<ID2D1Bitmap> bitmap; ramp.brush->GetBitmap(&bitmap); const auto size=bitmap->GetPixelSize();
            bytes+=size_t(size.width)*size.height*4; ++brushes;
        }
        Check(brushes && bytes<=3*1024*1024,"dither texture storage is missing or exceeds the bounded per-tab budget");
        Load(settings,Element(name,LR"(background="#ff000080" border-width="0")"));
        const auto translucent=render(.4f);
        Check(std::abs(int(At(translucent,(left+right)/2,y)>>24)-51)<=1,"color alpha did not multiply existing mod opacity");
        Load(settings,L"<theme><element name=\""+std::wstring(name)+L"\" background=\"#ff0000\"><dark background=\"#00ff00\" /></element></theme>");
        surface.state.themeMode=Theme::Mode::Dark;
        Check(At(render(),(left+right)/2,y)==0xff00ff00u,"dark element override did not change rendered pixels");
        surface.state.themeHighContrast=true;
        Check(At(render(),(left+right)/2,y)==0xff112233u,"high contrast did not bypass the theme");
        surface.state.themeHighContrast=false; surface.state.themeMode=Theme::Mode::Light;
    }
    Load(settings,L""); const auto cleared=render();
    Check(At(cleared,280,100)==0xff112233u,"clearing theme did not restore legacy fill");
    DropTarget(surface.state);
    BorderCacheReleased(surface.state.themeBorderStrokes);
    Check(std::all_of(surface.state.highlightResources.gradients.begin(),surface.state.highlightResources.gradients.end(),
        [](const auto& gradient) {return !gradient;}),"device teardown retained gradients");
    for(const auto& ramps:surface.state.highlightResources.ramps) for(const auto& ramp:ramps.slots)
        Check(!ramp.brush,"device teardown retained dithered textures");
    // Restore the surface target for its RAII cleanup after verifying release.
    Check(EnsureTarget(surface.state),"theme target recreation");
    surface.previous.Reset(); surface.state.target->GetTarget(&surface.previous);
    surface.target.Reset(); surface.cpu.Reset();
    if(!common && kind==Theme::Highlight::Selected) settings.RequireExercised(L"theme-selection");
    std::cout<<"PASS: per-element pixels, DPI borders, solid/gradient alpha, LTR/RTL, mode overrides, clear and device lifetime\n";
}

static void TestThemedScrollbar(bool thumb) {
    using namespace ThemeRegression;
    StyleFixture::Session style;
    OptionEffects::Settings settings; Fonts(); Surface surface;
    settings.Set(L"Selection.selectionOpacity",L"0"); settings.Set(L"Hover.hoverOpacity",L"0");
    settings.Set(L"Hover.hoverSelectedOpacity",L"0"); LoadSettings();
    const std::wstring name=thumb ? L"DrawScrollbar:thumb":L"DrawScrollbar:track";
    const std::wstring other=thumb ? L"DrawScrollbar:track":L"DrawScrollbar:thumb";
    const auto compiled=Parse(L"<theme><element name=\""+name+
        L"\" background=\"#111111\" border-width=\"1px\" border-radius=\"3px\" />"+
        L"<element name=\""+name+L"\" border-color=\"#123456\" />"+
        L"<element name=\""+name+L":hover\" background=\"#222222\" border-radius=\"invalid\"><dark border-width=\"2px\" /></element>"+
        L"<element name=\""+name+L":pressed\" background=\"accent\" /></theme>");
    for(size_t mode=0;mode<2;++mode) {
        const auto& states=compiled->scrollbars[mode][thumb ? 0:1];
        Check(states[0].borderRadius==3 && states[0].borderColor.rgb==0x123456 && states[0].borderWidth==1,
            "merged scrollbar properties changed");
        Check(states[1].background.color.rgb==0x222222 && states[2].background.color.accent &&
            states[2].borderRadius==3 && states[2].borderColor.rgb==0x123456 &&
            states[2].borderWidth==(mode ? 2.f:1.f),"pressed scrollbar did not inherit hover/base appearance");
        Check(compiled->scrollbars[mode][thumb ? 1:0][2].background.kind==Theme::Paint::Kind::Default,
            "scrollbar definition leaked to the other part");
    }
    const auto theme=[&](const std::wstring& properties) {
        return L"<theme><element name=\""+other+L"\" background=\"transparent\" border-radius=\"0\" />"+
            L"<element name=\""+name+L"\" "+properties+L"</theme>";
    };
    bool horizontal=false; size_t interaction=0;
    const auto render=[&] {
        return surface.Render([&](ViewState& state) {
            const float s=state.presentationDpi/96.f;
            const auto axis=horizontal ? ScrollAxis::Horizontal:ScrollAxis::Vertical;
            const auto bar=MakeScrollbarGeometry(axis,20*s,(horizontal ? 110:44)*s,
                20*s,(horizontal ? 44:110)*s,180*s,80*s,60*s,1.f);
            Check(bar.usable,"private scrollbar geometry");
            // Settle at an exact pose, independent of scheduler/animation time.
            auto& motion=state.scrollbarMotion[horizontal ? 1:0]; motion={};
            motion.activeFrom=motion.activeTo=interaction ? 1.f:0.f;
            motion.pressedFrom=motion.pressedTo=interaction==2 ? 1.f:0.f;
            DrawScrollbar(state,bar,0xffffff,interaction!=0,interaction==2);
        });
    };
    const auto pixel=[&](const std::vector<DWORD>& pixels,int cross,int along) {
        int x=horizontal ? along:cross, y=horizontal ? cross:along;
        if(surface.state.rtlLayout) x=int(Surface::width)-1-x;
        return At(pixels,x,y);
    };
    const auto noRamps=[&] {
        for(const auto& ramps:surface.state.scrollbarResources.ramps)
            for(const auto& ramp:ramps.slots) Check(!ramp.brush,"solid/cleared scrollbar retained dither textures");
    };
    for(UINT dpi:{96u,144u,192u}) for(bool rtl:{false,true}) for(bool axis:{false,true}) {
        horizontal=axis; surface.state.presentationDpi=dpi; surface.state.rtlLayout=rtl;
        const float s=dpi/96.f;
        Regression::Scope scope("scrollbar "+Regression::Narrow(name)+" dpi="+std::to_string(dpi)+
            " rtl="+std::to_string(rtl)+" horizontal="+std::to_string(axis));
        Load(settings,theme(LR"(background="#112233" border-radius="0"><dark background="#445566" /></element>)"+
            std::wstring(L"<element name=\"")+name+L":hover\" background=\"#7799bb\"><dark background=\"#335577\" /></element>"+
            L"<element name=\""+name+L":pressed\" background=\"accent\" />"));
        for(auto mode:{Theme::Mode::Light,Theme::Mode::Dark}) for(size_t state=0;state<3;++state) {
            surface.state.themeMode=mode; interaction=state;
            const auto pixels=render();
            const COLORREF accent=COLORREF(g_swipePalette.load()&0xffffff);
            const DWORD expected=state==2 ? 0xff000000u|(DWORD(GetRValue(accent))<<16)|(DWORD(GetGValue(accent))<<8)|GetBValue(accent):
                state==1 ? (mode==Theme::Mode::Dark ? 0xff335577u:0xff7799bbu):mode==Theme::Mode::Dark ? 0xff445566u:0xff112233u;
            Check(pixel(pixels,MulDiv(32,dpi,96),MulDiv(thumb ? 70:30,dpi,96))==expected,
                "shared axis appearance/state color or independent opacity is wrong");
            noRamps();
        }
        surface.state.themeMode=Theme::Mode::Light; interaction=0;
        Load(settings,theme(LR"(background="transparent" border-color="#ff0000" border-width="1px" border-radius="0" />)"));
        const auto border=render();
        const int edge=int(std::floor((20+(thumb ? 24*.32:0))*s+.5));
        const int along=MulDiv(thumb ? 70:30,dpi,96), width=int(std::floor(s+.5));
        for(int c=edge-2;c<edge+width+3;++c)
            Check(pixel(border,c,along)==(c>=edge && c<edge+width ? 0xffff0000u:0u),
                "scrollbar border moved its bounds or failed whole-pixel DPI scaling");
        Check(pixel(border,MulDiv(32,dpi,96),along)==0,"border-only scrollbar gained a background");
        for(size_t state=0;state<3;++state) {
            interaction=state;
            BorderPatterns([&](const wchar_t* pattern) {
                Load(settings,theme(L"background=\"transparent\" border-color=\"#ff0000\" border-width=\"2px\" border-radius=\"0\" border-style=\""+
                    std::wstring(pattern)+L"\" />"));
                const auto pixels=render(); Check(pixels==render(),"warm scrollbar border changed pixels");
                const int cross=int(std::floor((20+(thumb ? 24*(state ? .12:.32):0))*s+.5));
                std::vector<DWORD> samples;
                for(int a=MulDiv(thumb ? 60:30,dpi,96);a<MulDiv(thumb ? 80:100,dpi,96);++a) samples.push_back(pixel(pixels,cross,a));
                Check(pixel(pixels,cross-1,along)==0,"scrollbar pattern escaped its bounds");
                return samples;
            });
        }
        interaction=0;
        Load(settings,theme(LR"(background="#ff000080" border-radius="0" />)"));
        const auto translucent=render();
        Check(pixel(translucent,MulDiv(32,dpi,96),along)==0x80800000u,"scrollbar paint alpha inherited highlight/native opacity");
        interaction=1;
        Load(settings,theme(L"background=\"linear-gradient("+std::wstring(horizontal ? L"to right":L"to bottom")+
            L", #ff0000, #0000ff)\" border-radius=\"0\"><dark background=\"#00ff00\" /></element>"));
        const auto reads=SettingsFixture::reads;
        const auto gradient=render(); Check(gradient==render(),"warm scrollbar dither cache changed pixels");
        Check(SettingsFixture::reads==reads,"scrollbar drawing reread settings");
        const int start=int(std::floor((thumb ? 50+24*.12:20)*s+.5));
        const int end=int(std::floor((thumb ? 90-24*.12:110)*s+.5));
        for(int a:{start+(end-start)/4,start+3*(end-start)/4}) {
            double red=0,blue=0;
            const int cross=MulDiv(32,dpi,96)-4;
            for(int c=cross;c<cross+8;++c) {red+=Channel(pixel(gradient,c,a),16);blue+=Channel(pixel(gradient,c,a),0);}
            double t=(a+.5-start)/double(end-start);
            // CSS directions are physical. RTL mirrors the bar's placement,
            // while "to right" still points toward the screen's right edge.
            if(horizontal && rtl) t=1-t;
            // A nearest-sampled ramp has half a texel of position error,
            // plus at most one 8-bit color step from quantization/dithering.
            unsigned resolution=64; while(resolution<unsigned(end-start)) resolution*=2;
            const double error=255./(2*resolution)+1;
            Check(std::abs(red/8-255*(1-t))<=error && std::abs(blue/8-255*t)<=error,
                "scrollbar gradient differs from independent interpolation");
        }
        size_t brushes=0,bytes=0;
        for(const auto& ramps:surface.state.scrollbarResources.ramps) for(const auto& ramp:ramps.slots) if(ramp.brush) {
            ComPtr<ID2D1Bitmap> bitmap; ramp.brush->GetBitmap(&bitmap); const auto size=bitmap->GetPixelSize();
            ++brushes; bytes+=size_t(size.width)*size.height*4;
        }
        Check(brushes && bytes<=6*1024*1024,"scrollbar gradients missing or exceed bounded storage");
        surface.state.themeMode=Theme::Mode::Dark;
        Check(pixel(render(),MulDiv(32,dpi,96),along)==0xff00ff00u,"appearance change retained a stale gradient"); noRamps();
        surface.state.themeMode=Theme::Mode::Light;
        Load(settings,theme(LR"(background="#ff0000" border-radius="100000px" />)"));
        const auto rounded=render();
        const int crossEdge=int(std::floor((20+ (thumb ? 24*.12:0))*s+.5));
        const int axialEdge=thumb ? start:MulDiv(20,dpi,96);
        Check(pixel(rounded,crossEdge,axialEdge)==0 && pixel(rounded,MulDiv(32,dpi,96),along)==0xffff0000u,
            "scrollbar radius did not clamp to the existing shape");
        Load(settings,theme(LR"(background="#ff0000" border-radius="25%"><dark border-radius="0%" /></element>)"));
        const auto percent=render();
        CheckRadius([&](int offset) {return pixel(percent,crossEdge+offset,axialEdge+offset);},
            .25f*24.f*(thumb ? .76f:1.f)*s,"scrollbar percent radius did not use cross-axis thickness");
        surface.state.themeMode=Theme::Mode::Dark;
        Check(pixel(render(),crossEdge,axialEdge)==0xffff0000u,"scrollbar dark zero percent did not square the corner");
        surface.state.themeMode=Theme::Mode::Light;
        Load(settings,L"");
        constexpr DWORD shades[2][3]={{0xffa5a5a5,0xff959595,0xff858585},
                                      {0xff757575,0xff858585,0xff959595}};
        for(auto mode:{Theme::Mode::Light,Theme::Mode::Dark}) for(interaction=0;interaction<3;++interaction) {
            surface.state.themeMode=mode;
            Check(pixel(render(),MulDiv(32,dpi,96),MulDiv(70,dpi,96))==
                shades[mode==Theme::Mode::Dark][interaction],"default scrollbar thumb shade differs");
        }
        interaction=0; surface.state.themeMode=Theme::Mode::Light;
        const auto normal=render(); noRamps();
        surface.state.themeHighContrast=true;
        const auto contrast=render();
        // Idle high contrast uses the supplied white foreground at 44% on
        // the transparent target. The idle track contributes no coverage.
        Check(pixel(contrast,MulDiv(32,dpi,96),MulDiv(70,dpi,96))==0x70707070u &&
              pixel(contrast,MulDiv(32,dpi,96),MulDiv(30,dpi,96))==0,
              "high contrast scrollbar lost foreground color or idle opacity");
        Load(settings,theme(LR"(background="#ff0000" border-width="3px" border-radius="0" />)"));
        Check(render()==contrast,"custom theme changed high contrast scrollbar pixels");
        surface.state.themeHighContrast=false;
        Load(settings,L""); Check(render()==normal,"clearing theme did not restore default scrollbar pixels");
    }
    DropTarget(surface.state); noRamps();
    BorderCacheReleased(surface.state.themeBorderStrokes);
    for(const auto& gradient:surface.state.scrollbarResources.gradients) Check(!gradient,"device teardown retained scrollbar gradients");
    std::cout<<"PASS: scrollbar "<<(thumb ? "thumb":"track")<<" axes, states, light/dark, DPI/RTL pixels, borders, corners, alpha, gradients, cache and native fallback\n";
}

static void TestThemeStorageScale() {
    using namespace ThemeRegression;
    const auto doc=Parse(LR"(<theme><element name="DrawDriveBar:fill"><scale>
      <stop offset="0%" background="accent" /><stop offset="70%" background="accent" />
      <stop offset="70%"><light background="#ff0000" /><dark background="#00ff00" /></stop>
      <stop offset="90%"><light background="#0000ff" /><dark background="#ff0000" /></stop>
      <stop offset="bad" background="#ffffff" />
    </scale></element></theme>)");
    for(size_t mode=0;mode<2;++mode) {
        const auto& drive=doc->driveBars[mode][0];
        Check(drive.enabled && drive.scaled && drive.paints[69].color.accent,"scale changed accent before the hard stop");
        Check(drive.paints[70].color.rgb==(mode ? 0x00ff00u:0xff0000u),"duplicate percentage did not switch to the last stop");
        Check(drive.paints[80].color.rgb==(mode ? 0x808000u:0x800080u),"scale interpolation did not mix independent channels");
        Check(drive.paints[100].color.rgb==(mode ? 0xff0000u:0x0000ffu),"missing final stop did not extend through 100%");
        Check(!doc->driveBars[mode][1].enabled,"fill scale leaked into track");
    }
    const auto modes=Parse(LR"(<element name="DrawDriveBar:fill"><scale>
      <stop offset="0%" background="#123456" />
      <light><stop offset="0%" background="accent" /><stop offset="80%" background="#ff0000" /></light>
      <dark><stop offset="0%" background="accent" /><stop offset="95%" background="#00ff00" /></dark>
    </scale></element>)");
    Check(modes->driveBars[0][0].paints[80].color.rgb==0xff0000 &&
        modes->driveBars[1][0].paints[80].color.accentWeight>0 && modes->driveBars[1][0].paints[100].color.rgb==0x00ff00,
        "appearance stop lists did not replace shared stops or preserve accent interpolation");
    const auto gradients=Parse(LR"xml(<element name="DrawDriveBar:track"><dark><scale>
      <stop offset="20%" background="linear-gradient(to bottom, #ff0000, #000000)" />
      <stop offset="80%" background="linear-gradient(to bottom, #0000ff, #ffffff)" />
    </scale></dark></element>)xml");
    const auto& paints=gradients->driveBars[1][1].paints;
    Check(paints[0].stops[0].color.rgb==0xff0000 && paints[100].stops[0].color.rgb==0x0000ff &&
        paints[50].stops[0].color.rgb==0x800080 && paints[50].stops[1].color.rgb==0x808080,
        "gradient stop interpolation or endpoint extension differs");
    const auto recovered=Parse(LR"(<element name="DrawDriveBar:fill"><scale><stop offset="0%" background="#123456" broken= />
      <stop offset="90%" background="#abcdef" /></scale></element>)");
    Check(recovered->driveBars[0][0].paints[0].color.rgb==0x123456 && recovered->driveBars[0][0].paints[100].color.rgb==0xabcdef,
        "XML recovery lost scale scopes or complete stop properties");
    std::cout<<"PASS: storage scales, appearance scopes, hard stops, interpolation, missing endpoints, accent and malformed XML recovery\n";
}

static void TestThemedStoragePaint(bool fill) {
    using namespace ThemeRegression;
    StyleFixture::Session style; OptionEffects::Settings settings; Fonts(); Surface surface;
    const size_t part=fill ? 0:1; const auto name=fill ? L"DrawDriveBar:fill":L"DrawDriveBar:track";
    float fraction=.4f;
    const auto render=[&](float alpha=1.f) {
        return surface.Render([&](ViewState& view) {
            const float s=view.presentationDpi/96.f;
            Check(DrawDriveBar(view,part,D2D1::RectF(20*s,20*s,200*s,40*s),fraction,0xffffff,alpha),"storage paint route");
        });
    };
    for(UINT dpi:{96u,120u,144u,192u}) for(bool rtl:{false,true}) {
        Regression::Scope scope(std::string("storage paint ")+(fill ? "fill":"track")+" dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl));
        surface.state.presentationDpi=dpi; surface.state.rtlLayout=rtl;
        const int start=MulDiv(20,dpi,96),end=MulDiv(fill ? 92:200,dpi,96);
        const int left=rtl ? 480-end:start,right=rtl ? 480-start:end,y=MulDiv(30,dpi,96),top=MulDiv(20,dpi,96);
        Load(settings,Element(name,LR"(background="transparent" border-color="#ff0000" border-width="1px" border-radius="0")"));
        const auto border=render(); const int width=MulDiv(1,dpi,96);
        for(int x=left-2;x<left+width+3;++x)
            Check(At(border,x,y)==(x>=left && x<left+width ? 0xffff0000u:0u),"storage border moved outside bounds or lost DPI thickness");
        BorderPatterns([&](const wchar_t* pattern) {
            Load(settings,Element(name,L"background=\"transparent\" border-color=\"#ff0000\" border-width=\"2px\" border-radius=\"0\" border-style=\""+std::wstring(pattern)+L"\""));
            const auto pixels=render(); std::vector<DWORD> row;
            for(int x=left+MulDiv(6,dpi,96);x<right-MulDiv(6,dpi,96);++x) row.push_back(At(pixels,x,top));
            return row;
        });
        Load(settings,Element(name,LR"(background="#ff000080" border-style="none" border-radius="0")"));
        Check(std::abs(int(At(render(.5f),(left+right)/2,y)>>24)-64)<=1,"storage color alpha did not multiply item fade");
        for(bool clipped:{false,true}) {
            Load(settings,Element(name,L"background=\"linear-gradient(to right, #ff0000, #0000ff)\" border-radius=\"0\" background-origin=\""+
                std::wstring(clipped ? L"track-box":L"border-box")+L"\""));
            const auto reads=SettingsFixture::reads;
            const auto gradient=render(); Check(gradient==render() && SettingsFixture::reads==reads,"warm storage gradient changed or reread settings");
            const int originLeft=clipped ? (rtl ? 480-MulDiv(200,dpi,96):start):left;
            const int originRight=clipped ? (rtl ? 480-start:MulDiv(200,dpi,96)):right;
            for(int x:{left+(right-left)/4,left+3*(right-left)/4}) {
                double red=0,blue=0;
                for(int row=y-4;row<y+4;++row) {red+=Channel(At(gradient,x,row),16);blue+=Channel(At(gradient,x,row),0);}
                const double t=(x+.5-originLeft)/double(originRight-originLeft);
                Check(std::abs(red/8-255*(1-t))<2 && std::abs(blue/8-255*t)<2,"storage gradient clipping/direction differs from independent interpolation");
            }
            Check(At(gradient,left-1,y)==0 && At(gradient,right,y)==0,"storage fill escaped its used-space bounds");
            size_t bytes=0;
            for(const auto& states:surface.state.driveBarResources->paints) for(const auto& paint:states) for(const auto& ramp:paint.ramps.slots) if(ramp.brush) {
                ComPtr<ID2D1Bitmap> bitmap; ramp.brush->GetBitmap(&bitmap); const auto size=bitmap->GetPixelSize();
                bytes+=size_t(size.width)*size.height*4;
            }
            Check(bytes>0 && bytes<=2*101*512*8*4,"storage dither cache missing or exceeds its finite budget");
        }
        Load(settings,L"<element name=\""+std::wstring(name)+L"\" background=\"#ff0000\" border-radius=\"8px\"><dark background=\"#00ff00\" /></element>");
        Check(At(render(),left,top)==0,"storage rounded corner missing");
        surface.state.themeMode=Theme::Mode::Dark;
        Check(At(render(),(left+right)/2,y)==0xff00ff00u,"storage dark paint retained stale gradient");
        Load(settings,Element(name,LR"(background="#ff0000" border-radius="30%")"));
        const auto percent=render();
        CheckRadius([&](int offset) {return At(percent,left+offset,top+offset);},
            float(MulDiv(20,dpi,96))*.30f,"storage percent radius did not use the part height");
        surface.state.themeHighContrast=true; const auto contrast=surface.Render([&](ViewState& view) {
            Check(!DrawDriveBar(view,part,D2D1::RectF(20,20,200,40),fraction,0xffffff,1.f),"high contrast did not return to native storage painting");
        });
        Check(!surface.state.driveBarResources && std::all_of(contrast.begin(),contrast.end(),[](DWORD p){return p==0;}),"high contrast retained custom storage resources/pixels");
        surface.state.themeHighContrast=false;surface.state.themeMode=Theme::Mode::Light;
    }
    Load(settings,L""); surface.Render([&](ViewState& view){Check(!DrawDriveBar(view,part,D2D1::RectF(20,20,200,40),fraction,0xffffff,1.f),"clearing storage theme retained custom route");});
    DropTarget(surface.state); Check(!surface.state.driveBarResources,"device teardown retained storage cache"); BorderCacheReleased(surface.state.themeBorderStrokes);
    std::cout<<"PASS: storage "<<(fill ? "fill":"track")<<" border patterns, DPI/RTL, alpha, stretched/clipped gradients, corners, modes and cache lifetime\n";
}

static void TestThemedStorageHeights() {
    using namespace ThemeRegression;
    StyleFixture::Session style; OptionEffects::Settings settings; Fonts(); Surface surface;
    settings.Set(L"Appearance.fontSizePercent",L"50"); settings.Set(L"Appearance.nativeGridTextPercent",L"50");
    auto entries=std::make_shared<std::vector<Entry>>(1);
    entries->front().name=L"Disk"; entries->front().driveTotal=1000;
    surface.state.presentationEntries=entries; surface.state.total=1; surface.state.thisPcGpu=true;
    surface.state.presentationClient={0,0,LONG(Surface::width),LONG(Surface::height)};
    const auto xml=[](const wchar_t* fill,const wchar_t* track) {
        return L"<theme><element name=\"DrawDriveBar:fill\" background=\"#ff0000\" border-radius=\"0\" height=\""+
            std::wstring(fill)+L"\" /><element name=\"DrawDriveBar:track\" background=\"#ff0000\" border-radius=\"0\" height=\""+track+L"\" /></theme>";
    };
    for(UINT dpi:{96u,120u,144u,192u}) for(bool tiles:{false,true}) for(bool rtl:{false,true}) {
        Regression::Scope scope("storage height dpi="+std::to_string(dpi)+" tiles="+std::to_string(tiles)+" rtl="+std::to_string(rtl));
        auto& state=surface.state; const float s=dpi/96.f;
        state.presentationDpi=dpi; state.gridView=tiles; state.layoutNativeMode=tiles ? FVM_TILE:FVM_DETAILS;
        state.rtlLayout=rtl; state.rowPx=(tiles ? 100:60)*s; state.fontPx=10*s; state.iconPx=16*s; state.itemWidth=200*s;
        Check(MakeTextFormats(state),"storage height fonts");
        const RowVisual visual{20*s,state.rowPx,1.f,0,200*s};
        DriveBarGeometry geometry; ItemGeometry item;
        const auto render=[&](size_t part) {
            return surface.Render([&](ViewState& view) {
                item=LayoutItem(view,visual); geometry=LayoutDriveBar(view,0,item);
                Check(geometry.visible,"storage height private geometry");
                Check(DrawDriveBar(view,part,part==0 ? geometry.fill:geometry.track,1.f,0xffffff,1.f),"storage theme drawing route");
            });
        };
        Load(settings,xml(L"8px",L"2px"));
        std::array<double,2> centers{};
        for(size_t part=0;part<2;++part) {
            const auto pixels=render(part);
            const auto bounds=part==0 ? geometry.fill:geometry.track;
            const int expected=MulDiv(part==0 ? 8:2,dpi,96);
            const int logicalX=int((bounds.left+bounds.right)*.5f);
            const int x=rtl ? int(Surface::width)-1-logicalX:logicalX;
            int first=-1,last=-1,count=0;
            for(int y=0;y<int(Surface::height);++y) {
                const DWORD p=At(pixels,x,y);
                Check(p==0 || p==0xffff0000u,"height introduced a fractional/blurred edge");
                if(p) {if(first<0) first=y;last=y;++count;}
            }
            Check(count==expected && last-first+1==expected,"storage paint height differs from independent DPI arithmetic");
            centers[part]=(first+last+1)/2.;
            Check(std::abs(centers[part]-(bounds.top+bounds.bottom)*.5)<.501,"pixel snapping moved the shared center by more than half a pixel");
        }
        const double permitted=MulDiv(8,dpi,96)%2==MulDiv(2,dpi,96)%2 ? 0.:.5;
        Check(std::abs(centers[0]-centers[1])==permitted,"crisp odd/even heights did not preserve the nearest shared center");
        const float row=state.rowPx;
        Load(settings,xml(L"1000px",L"1000px")); render(0);
        const float available=tiles ? visual.height-2*MulDiv(2,dpi,96)-3*state.fontPx-2*MulDiv(2,dpi,96):
            std::min(item.text.bottom,item.cell.bottom)-std::max(item.text.top,item.cell.top);
        const float expected=floorf(std::max(0.f,std::min(available,visual.height*.5f)));
        Near(geometry.fill.bottom-geometry.fill.top,expected,.001f,"storage height exceeded available space or font percentage cap");
        Near(geometry.track.bottom-geometry.track.top,expected,.001f,"track height cap differs from fill");
        Check(state.rowPx==row && item.cell.bottom-item.cell.top==visual.height-2*MulDiv(2,dpi,96),"theme height changed row/cell height");
        if(tiles) Check(geometry.fill.top>=item.text.bottom && geometry.fill.bottom<=item.capacity.top,"expanded storage overlaps tile text");
        for(size_t hidden=0;hidden<2;++hidden) {
            Load(settings,xml(hidden==0 ? L"0px":L"8px",hidden==1 ? L"0px":L"2px"));
            state.driveBarResources.reset();
            const auto absent=render(hidden);
            Check(std::all_of(absent.begin(),absent.end(),[](DWORD p){return p==0;}),"zero-height part drew pixels");
            Check(!state.driveBarResources,"zero-height draw allocated paint resources");
            const auto visible=render(1-hidden);
            Check(std::any_of(visible.begin(),visible.end(),[](DWORD p){return p!=0;}),"zero-height part hid the other part");
            // Release before the next isolated zero-height allocation check.
            state.driveBarResources.reset();
        }
    }
    std::cout<<"PASS: independent storage heights, crisp DPI/RTL pixels including 125%, parity-aware centering, caps, tile text clearance and zero-height isolation\n";
}

static void TestThemedMarquee() {
    using namespace ThemeRegression;
    StyleFixture::Session style;
    OptionEffects::Settings settings; Fonts(); Surface surface;
    const auto parsed=Parse(LR"(<theme><element name="DrawMarquee" border-style="dashed" border-width="2px">
        <light border-style="invalid" /><dark border-style="dotted" /></element></theme>)");
    Check(parsed->marquees[0].borderStyle==Theme::BorderStyle::Dashed && parsed->marquees[1].borderStyle==Theme::BorderStyle::Dotted,
        "border-style mode override/invalid inheritance changed");
    settings.Set(L"Selection.marqueeOpacity",L"100");
    bool handled=false;
    const auto render=[&](float fade=1.f,bool line=false) {
        return surface.Render([&](ViewState& state) {
            const float s=state.presentationDpi/96.f;
            handled=DrawMarquee(state,D2D1::RectF(20*s,20*s,(line ? 20:200)*s,80*s),
                std::stoi(settings.values.at(L"Selection.marqueeOpacity"))/100.f*fade);
        });
    };
    for(UINT dpi:{96u,144u,192u}) for(bool rtl:{false,true}) {
        Regression::Scope scope("marquee theme dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl));
        surface.state.presentationDpi=dpi; surface.state.rtlLayout=rtl;
        const int left=MulDiv(20,dpi,96),right=MulDiv(200,dpi,96),top=MulDiv(20,dpi,96),y=MulDiv(50,dpi,96);
        Load(settings,Element(L"DrawMarquee",LR"(background="transparent" border-color="#ff0000" border-width="1px" border-radius="0")"));
        const auto border=render(); Check(handled,"marquee theme was not drawn");
        const int width=int(std::floor(dpi/96.f+.5f));
        for(int x=left-2;x<left+width+3;++x)
            Check(At(border,x,y)==(x>=left && x<left+width ? 0xffff0000u:0u),"marquee border width/inset/DPI/physical RTL placement differs");
        Check(At(border,(left+right)/2,y)==0,"border-only marquee gained a fill");
        for(bool line:{false,true}) BorderPatterns([&](const wchar_t* pattern) {
            Load(settings,Element(L"DrawMarquee",L"background=\"transparent\" border-color=\"#ff0000\" border-width=\"2px\" border-radius=\"0\" border-style=\""+
                std::wstring(pattern)+L"\""));
            const auto pixels=render(1.f,line); Check(pixels==render(1.f,line),"warm marquee border changed pixels");
            std::vector<DWORD> row;
            if(line) for(int a=MulDiv(30,dpi,96);a<MulDiv(70,dpi,96);++a) row.push_back(At(pixels,left,a));
            else for(int x=left+MulDiv(10,dpi,96);x<right-MulDiv(10,dpi,96);++x) row.push_back(At(pixels,x,top));
            return row;
        });
        Load(settings,Element(L"DrawMarquee",LR"(background="#ff000080" border-style="none" border-radius="0")"));
        Check(At(render(),left,y)==0x80800000u,"none border suppressed the fill or retained an outline");
        settings.Set(L"Selection.marqueeOpacity",L"50"); LoadSettings();
        const auto faded=render(.5f);
        Check(std::abs(int(At(faded,(left+right)/2,y)>>24)-32)<=1,"marquee color alpha, option opacity and release fade do not multiply");
        Load(settings,Element(L"DrawMarquee",LR"xml(background="linear-gradient(to right, #ff0000, #0000ff)" border-width="0" border-radius="0")xml"));
        const auto reads=SettingsFixture::reads;
        const auto gradient=render(); Check(gradient==render() && SettingsFixture::reads==reads,"warm marquee gradient changed or reread settings");
        for(int x:{left+(right-left)/4,left+3*(right-left)/4}) {
            double red=0,blue=0,alpha=0;
            for(int a=y;a<y+8;++a) {red+=Channel(At(gradient,x,a),16);blue+=Channel(At(gradient,x,a),0);alpha+=Channel(At(gradient,x,a),24);}
            const double t=(x+.5-left)/double(right-left);
            Check(std::abs(red/8-127.5*(1-t))<2 && std::abs(blue/8-127.5*t)<2 && std::abs(alpha/8-127.5)<1,
                "marquee gradient differs from independent interpolation/opacity");
        }
        size_t bytes=0,brushes=0;
        for(const auto& ramp:surface.state.marqueeResources.ramps.slots) if(ramp.brush) {
            ComPtr<ID2D1Bitmap> bitmap; ramp.brush->GetBitmap(&bitmap); const auto size=bitmap->GetPixelSize();
            bytes+=size_t(size.width)*size.height*4; ++brushes;
        }
        Check(brushes && bytes<=1024*1024,"marquee ramp storage missing or unbounded");
        settings.Set(L"Selection.marqueeOpacity",L"100"); LoadSettings();
        Check((At(render(),(left+right)/2,y)>>24)==255,"marquee opacity change retained a stale ramp");
        Load(settings,LR"(<theme><element name="DrawMarquee" background="#ff0000" border-width="0" border-radius="12px">
            <dark background="#00ff00" /></element></theme>)");
        const auto rounded=render();
        Check(At(rounded,left,top)==0 && At(rounded,(left+right)/2,y)==0xffff0000u,"marquee corner radius or fill is wrong");
        for(const auto& ramp:surface.state.marqueeResources.ramps.slots) Check(!ramp.brush,"solid marquee retained a stale ramp");
        surface.state.themeMode=Theme::Mode::Dark;
        Check(At(render(),(left+right)/2,y)==0xff00ff00u,"marquee dark override did not render");
        Load(settings,Element(L"DrawMarquee",LR"(background="#ff0000" border-width="0" border-radius="25%")"));
        const auto percent=render();
        CheckRadius([&](int offset) {return At(percent,left+offset,top+offset);},
            float(MulDiv(60,dpi,96))*.25f,"marquee percent radius did not use the shorter side");
        surface.state.themeHighContrast=true; const auto contrast=render();
        Check(!handled && std::all_of(contrast.begin(),contrast.end(),[](DWORD p){return p==0;}),"high contrast did not defer to native marquee drawing");
        surface.state.themeHighContrast=false; surface.state.themeMode=Theme::Mode::Light;
        Load(settings,L""); render(); Check(!handled,"clearing theme did not restore native marquee routing");
    }
    DropTarget(surface.state); BorderCacheReleased(surface.state.themeBorderStrokes);
    Check(!surface.state.marqueeResources.gradient,"device teardown retained marquee gradient");
    for(const auto& ramp:surface.state.marqueeResources.ramps.slots) Check(!ramp.brush,"device teardown retained marquee texture");
    std::cout<<"PASS: marquee physical RTL/DPI geometry, border patterns/degenerate line, corners, fill/gradient alpha, opacity/fade, modes, cache and native fallback\n";
}

static void TestThemedTooltip() {
    using namespace ThemeRegression;
    OptionEffects::Settings settings; Fonts();
    for(UINT dpi:{96u,144u,192u}) {
        Regression::Scope scope("tooltip theme dpi="+std::to_string(dpi));
        Tooltip tooltip(dpi); auto& tip=tooltip.state.itemTip;
        Load(settings,Element(L"RenderItemTip",LR"(background="#202020" border-color="#ff0000" border-width="1px" border-radius="0%" box-shadow="none")"));
        tooltip.Show();
        Check(tip.margin==0 && tooltip.Pixel(0,0)==0xffff0000u,"tooltip zero percent square border/disabled shadow pixels");
        Check(tooltip.Pixel(tip.size.cx/2,tip.size.cy-3)==0xff202020u,"tooltip solid background pixel");
        for(int shift:{0,8,16}) {
            const unsigned foreground=(tip.foreground>>shift)&255;
            const unsigned expected=(foreground*65+32*35)/100;
            Check(((tip.label>>shift)&255)==expected,"muted metadata did not blend against custom background");
        }
        const SIZE size=tip.size;
        BorderPatterns([&](const wchar_t* pattern) {
            Load(settings,Element(L"RenderItemTip",L"background=\"transparent\" border-color=\"#ff0000\" border-width=\"2px\" border-radius=\"0\" box-shadow=\"none\" border-style=\""+
                std::wstring(pattern)+L"\""));
            tooltip.Show(); const auto first=LayeredPixelsFixture::pixels; RenderItemTip(tip);
            Check(first==LayeredPixelsFixture::pixels,"warm tooltip border changed pixels");
            Check(tip.size.cx==size.cx && tip.size.cy==size.cy,"border pattern changed tooltip layout");
            std::vector<DWORD> row;
            for(int x=MulDiv(10,dpi,96);x<tip.size.cx-MulDiv(10,dpi,96);++x) row.push_back(tooltip.Pixel(x,0));
            return row;
        });
        Load(settings,Element(L"RenderItemTip",LR"(background="#bcbcbc" border-width="0" border-radius="0" box-shadow="8px 10px 0px 0px #000000")"));
        tooltip.Show();
        Check(tip.size.cx==size.cx && tip.size.cy==size.cy,"shadow customization changed text layout");
        const int dx=MulDiv(8,dpi,96),dy=MulDiv(10,dpi,96);
        Check(tooltip.Pixel(tip.size.cx+dx/2,tip.size.cy/2)==0xff000000u &&
            tooltip.Pixel(tip.size.cx/2,tip.size.cy+dy/2)==0xff000000u,"hard offset shadow missing or blurred");
        Check(tooltip.Pixel(-1,tip.size.cy/2)==0,"positive shadow leaked to the opposite side");
        const auto shadow=tip.shadowBitmap;
        const auto pixels=LayeredPixelsFixture::pixels; RenderItemTip(tip);
        Check(shadow && tip.shadowBitmap==shadow && pixels==LayeredPixelsFixture::pixels,"tooltip shadow cache not reused");
        Load(settings,Element(L"RenderItemTip",LR"(background="#20202080" border-radius="8px" box-shadow="-6px -6px 12px 2px #ff000080")"));
        tooltip.Show();
        Check((tooltip.Pixel(tip.size.cx/2,tip.size.cy-4)>>24)==128,"outer shadow tinted the translucent body");
        const auto outer=tooltip.Pixel(-MulDiv(3,dpi,96),tip.size.cy/2);
        Check((outer>>24)>0 && (outer&0x00ffffu)==0 && Channel(outer,16)>0,"colored soft shadow has wrong hue or no spread");
        Load(settings,Element(L"RenderItemTip",LR"xml(background="linear-gradient(to right, #202020, #404040)" border-radius="12px" box-shadow="none")xml"));
        tooltip.Show();
        Check(tooltip.Pixel(0,0)==0 && (tooltip.Pixel(tip.size.cx/2,tip.size.cy-4)>>24)==255,"rounded gradient has wrong corner/interior alpha");
        Check(Channel(tooltip.Pixel(4,tip.size.cy/2),0)<Channel(tooltip.Pixel(tip.size.cx-5,tip.size.cy/2),0),"tooltip gradient direction missing");
        Load(settings,Element(L"RenderItemTip",LR"(background="#202020" border-width="0" border-radius="25%" box-shadow="none")"));
        tooltip.Show();
        Check(tip.size.cx==size.cx && tip.size.cy==size.cy,"percent radius changed tooltip layout");
        CheckRadius([&](int offset) {return tooltip.Pixel(offset,offset);},
            std::min(size.cx,size.cy)*.25f,"tooltip percent radius included shadow margins or used the wrong side");
        Load(settings,L"<theme><element name=\"RenderItemTip\" background=\"#ff0000\" box-shadow=\"none\"><dark background=\"#00ff00\" /></element></theme>");
        tooltip.Show(Theme::Mode::Dark);
        Check(tooltip.Pixel(tip.size.cx/2,tip.size.cy-3)==0xff00ff00u,"tooltip dark override did not render");
        tooltip.Show(Theme::Mode::Dark,true);
        Check(tip.style.background.kind==Theme::Paint::Kind::Default,"high contrast retained tooltip customization");
        tip.DropTarget(); Check(!tip.shadowBitmap && !tip.gradient,"tooltip device loss retained resources");
        BorderCacheReleased(tip.borderStrokes);
    }
    std::cout<<"PASS: tooltip body/border/corners, cached hard/soft colored shadows, opacity, metadata palette, appearance and DPI\n";
}

static void TestThemedTooltipAntialiasing() {
    using namespace ThemeRegression;
    OptionEffects::Settings settings; Fonts(); Tooltip tooltip;
    auto& tip=tooltip.state.itemTip;
    BOOL smoothing=TRUE; UINT smoothingType=0;
    Check(SystemParametersInfoW(SPI_GETFONTSMOOTHING,0,&smoothing,0) &&
        SystemParametersInfoW(SPI_GETFONTSMOOTHINGTYPE,0,&smoothingType,0),"read-only system font policy");
    for(const auto* policy:{L"system",L"smooth",L"aliased"}) for(bool opaque:{false,true}) for(bool gradient:{false,true}) {
        Regression::Scope scope("tooltip AA="+Regression::Narrow(policy)+" opaque="+std::to_string(opaque)+" gradient="+std::to_string(gradient));
        settings.Set(L"Appearance.textAntialiasing",policy);
        const auto paint=gradient ? (opaque ? L"linear-gradient(#808080, #a0a0a0)":L"linear-gradient(#808080, #a0a0a080)") :
            (opaque ? L"#808080":L"#80808080");
        Load(settings,Element(L"RenderItemTip",L"background=\""+std::wstring(paint)+L"\" border-radius=\"0\" box-shadow=\"none\""));
        tooltip.Show();
        const auto expected=!wcscmp(policy,L"aliased") || (!wcscmp(policy,L"system") && !smoothing) ?
            D2D1_TEXT_ANTIALIAS_MODE_ALIASED : !wcscmp(policy,L"system") && opaque && smoothingType==FE_FONTSMOOTHINGCLEARTYPE ?
            D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE;
        Check(tip.target->GetTextAntialiasMode()==expected,"tooltip AA does not independently match user/OS/opacity policy");
        Check(tip.opaqueBackground==opaque,"gradient endpoint/solid opacity eligibility is wrong");
        if(expected==D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE)
            for(DWORD pixel:LayeredPixelsFixture::pixels) Check(std::abs(int(Channel(pixel,16))-int(Channel(pixel,8)))<=1 &&
                std::abs(int(Channel(pixel,8))-int(Channel(pixel,0)))<=1,"transparent grayscale text produced RGB fringes");
    }
    settings.Set(L"Appearance.textAntialiasing",L"system");
    Load(settings,Element(L"RenderItemTip",LR"(background="#808080" border-radius="4096px" box-shadow="none")"));
    tooltip.Show();
    if(smoothing) Check(tip.target->GetTextAntialiasMode()==D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE,"ClearType crossed rounded transparent corners");
    std::cout<<"PASS: native system preferences, smooth/aliased choices, opaque/transparent gradient eligibility and actual fringe-free grayscale pixels\n";
}

static void TestThemeCheckboxStates() {
    using namespace ThemeRegression;
    const auto doc=Parse(LR"xml(<theme>
      <element name="DrawSelectionCheckbox" background="#111111" border-radius="25%" border-width="1px" />
      <element name="DrawSelectionCheckbox" border-color="#123456" />
      <element name="DrawSelectionCheckbox:hover" background="#222222"><dark border-width="2px" border-radius="2px" /></element>
      <element name="DrawSelectionCheckbox:pressed" background="#333333" border-radius="-1%" />
      <element name="DrawSelectionCheckbox:checked" background="accent"><light border-color="#abcdef" /></element>
      <element name="DrawSelectionCheckbox:checked:hover" background="#444444" />
      <element name="DrawSelectionCheckbox:checked:pressed" background="#555555" />
      <element name="DrawSelectionCheckbox:checked:pressed" background="#666666" broken=unquoted border-style="dotted" />
    </theme>)xml");
    for(size_t mode=0;mode<2;++mode) {
        Check(doc->checkboxEnabled[mode],"checkbox styles were not enabled");
        const auto& states=doc->checkboxes[mode];
        for(size_t i=0;i<6;++i) {
            const bool pixelRadius=(i==1 || i==2) && mode;
            Check(states[i].borderRadius==(pixelRadius ? 2.f:25.f) && states[i].borderRadiusPercent==!pixelRadius &&
                states[i].borderWidth==(pixelRadius ? 2.f:1.f),
                "checkbox state lost inherited radius or appearance border width");
            Check(states[i].borderColor.rgb==(i>=3 && !mode ? 0xabcdefu:0x123456u),
                "checkbox checked appearance leaked into unchecked/other appearance");
        }
        Check(states[0].background.color.rgb==0x111111 && states[1].background.color.rgb==0x222222 &&
            states[2].background.color.rgb==0x333333 && states[3].background.color.accent &&
            states[4].background.color.rgb==0x444444 && states[5].background.color.rgb==0x666666 &&
            states[5].borderStyle==Theme::BorderStyle::Dotted,
            "checkbox states, duplicate merge/last value or malformed attribute recovery changed");
        Check(doc->highlights[mode][0].background.kind==Theme::Paint::Kind::Default &&
            doc->scrollbars[mode][0][0].background.kind==Theme::Paint::Kind::Default,
            "checkbox properties leaked to unrelated elements");
    }
    const auto inherited=Parse(LR"(<element name="DrawSelectionCheckbox" background="#111111" />
        <element name="DrawSelectionCheckbox:hover" background="#222222" />
        <element name="DrawSelectionCheckbox:checked" background="accent" />)");
    for(const auto& states:inherited->checkboxes) Check(states[2].background.color.rgb==0x222222 &&
        states[4].background.color.accent && states[5].background.color.accent,
        "missing checkbox interaction state did not inherit hover/checked");
    std::cout<<"PASS: six checkbox states, appearance inheritance, duplicate merge/last value, recovery and element isolation\n";
}

static void TestThemedCheckboxPixels() {
    using namespace ThemeRegression;
    OptionEffects::Settings settings; Fonts(); StyleFixture::Session style; Surface surface;
    auto& state=surface.state; state.nativeTextColor=RGB(230,230,230); state.nativeEditBackground=RGB(32,32,32);
    size_t interaction=0; float fade=1.f; bool large=false;
    const auto render=[&] {return surface.Render([&](ViewState& view) {
        const float scale=view.presentationDpi/96.f;
        DrawSelectionCheckbox(view,D2D1::RectF(20*scale,20*scale,(large ? 100:40)*scale,(large ? 100:40)*scale),
            interaction>=3,fade,interaction%3==1,interaction%3==2);
    });};
    const auto pixel=[&](const std::vector<DWORD>& pixels,int x,int y) {
        return At(pixels,state.rtlLayout ? int(Surface::width)-1-x:x,y);
    };
    for(UINT dpi:{96u,120u,144u,192u}) for(bool rtl:{false,true}) {
        Regression::Scope scope("checkbox pixels dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl));
        state.presentationDpi=dpi; state.rtlLayout=rtl; state.themeMode=Theme::Mode::Light;
        const int left=MulDiv(20,dpi,96),right=MulDiv(40,dpi,96),y=MulDiv(30,dpi,96);
        Load(settings,L""); interaction=0; const auto native=render();
        Load(settings,Element(L"DrawSelectionCheckbox",LR"(background="transparent" border-color="#ff0000" border-width="1px" border-radius="0")"));
        const auto border=render(); const int width=MulDiv(1,dpi,96);
        for(int x=left-2;x<left+width+3;++x)
            Check(pixel(border,x,y)==(x>=left && x<left+width ? 0xffff0000u:0u),"checkbox border lost whole-pixel DPI thickness or escaped its bounds");
        Check(pixel(border,(left+right)/2,y)==0,"border-only checkbox gained a background");
        large=true;
        BorderPatterns([&](const wchar_t* pattern) {
            Load(settings,Element(L"DrawSelectionCheckbox",L"background=\"transparent\" border-color=\"#ff0000\" border-width=\"2px\" border-radius=\"0\" border-style=\""+
                std::wstring(pattern)+L"\""));
            const auto pixels=render(); std::vector<DWORD> samples;
            for(int along=MulDiv(30,dpi,96);along<MulDiv(90,dpi,96);++along) samples.push_back(pixel(pixels,left,along));
            return samples;
        });
        large=false;
        std::wstring xml=L"<theme>";
        constexpr const wchar_t* names[]={L"",L":hover",L":pressed",L":checked",L":checked:hover",L":checked:pressed"};
        for(size_t i=0;i<6;++i) xml+=L"<element name=\"DrawSelectionCheckbox"+std::wstring(names[i])+L"\" border-width=\"0\" border-radius=\"0\">"+
            L"<light background=\"#"+std::wstring(6,wchar_t(L'1'+i))+L"\" /><dark background=\"#"+std::wstring(6,wchar_t(L'a'+i))+L"\" /></element>";
        Load(settings,xml+L"</theme>");
        for(size_t mode=0;mode<2;++mode) for(interaction=0;interaction<6;++interaction) {
            state.themeMode=static_cast<Theme::Mode>(mode);
            const auto pixels=render(); const unsigned value=unsigned((mode ? 10:1)+interaction)*17;
            Check(pixel(pixels,left+2,left+2)==(0xff000000u|value*0x10101),"checkbox appearance or checked/hover/pressed state drew the wrong fill");
        }
        interaction=0; state.themeMode=Theme::Mode::Light;
        Load(settings,Element(L"DrawSelectionCheckbox",LR"(background="#ff000080" border-width="0" border-radius="0")"));
        fade=.5f; Check(pixel(render(),left+2,left+2)==0x40400000u,"checkbox color alpha did not multiply reveal opacity"); fade=1.f;
        Load(settings,Element(L"DrawSelectionCheckbox",LR"(background="#ff0000" border-width="0" border-radius="999999px")"));
        const auto round=render();
        Check(pixel(round,left,left)==0 && pixel(round,(left+right)/2,y)==0xffff0000u,"checkbox extreme radius was not clamped within its size");
        Load(settings,Element(L"DrawSelectionCheckbox",LR"(background="#ff0000" border-width="0" border-radius="40%")"));
        const auto percent=render();
        // The fixture's 20-DIP box gives 8 DIP at 40%, integral at each test DPI.
        Load(settings,Element(L"DrawSelectionCheckbox",LR"(background="#ff0000" border-width="0" border-radius="8px")"));
        Check(percent==render() && percent!=round,"checkbox percent radius did not use the box size");
        Load(settings,Element(L"DrawSelectionCheckbox",LR"xml(background="linear-gradient(to right, #ff0000, #0000ff)" border-width="0" border-radius="0")xml"));
        const auto reads=SettingsFixture::reads; const auto gradient=render();
        Check(gradient==render() && SettingsFixture::reads==reads,"warm checkbox gradient changed pixels or reread settings");
        for(int x=left+2;x<right-2;++x) {
            // CSS directions remain physical when the checkbox placement is mirrored.
            const double position=(x-left+.5)/double(right-left);
            const double t=rtl ? 1-position:position;
            const auto p=pixel(gradient,x,y);
            Check(std::abs(int(Channel(p,16))-255*(1-t))<3 && std::abs(int(Channel(p,0))-255*t)<3,
                "checkbox gradient differs from independent interpolation");
        }
        state.themeHighContrast=true; Check(render()==native,"high contrast did not restore native checkbox pixels"); state.themeHighContrast=false;
        Load(settings,L""); Check(render()==native,"clearing theme did not restore native checkbox pixels");
    }
    DropTarget(state); Check(!state.selectionCheckGlyph && !state.checkboxResources.snapshot,"checkbox device teardown retained geometry or theme");
    BorderCacheReleased(state.themeBorderStrokes);
    std::cout<<"PASS: checkbox states, light/dark, DPI/RTL border patterns, radius clamp, alpha, gradients and native fallback\n";
}

static void TestThemedCheckboxCheckmarkCache() {
    using namespace ThemeRegression;
    OptionEffects::Settings settings; Fonts(); StyleFixture::Session style; Surface surface;
    auto& state=surface.state; state.nativeTextColor=RGB(240,240,240); state.nativeEditBackground=RGB(32,32,32);
    bool checked=true;
    const auto render=[&] {return surface.Render([&](ViewState& view) {
        DrawSelectionCheckbox(view,D2D1::RectF(20,20,40,40),checked,1.f);
    });};
    ComPtr<ID2D1PathGeometry> glyph;
    for(const auto* background:{L"#101010",L"#eeeeee",L"linear-gradient(to right, #101010, #eeeeee)",
                                L"linear-gradient(to right, #eeeeee, #101010)",L"#ffffff80",L"accent"}) {
        Regression::Scope scope("checkbox mark "+Regression::Narrow(background));
        Load(settings,Element(L"DrawSelectionCheckbox",L"background=\""+std::wstring(background)+L"\" border-width=\"0\" border-radius=\"0\""));
        checked=false; const auto plain=render(); checked=true; const auto tick=render();
        if(!glyph) glyph=state.selectionCheckGlyph;
        Check(glyph && glyph==state.selectionCheckGlyph,"theme change rebuilt the normalized checkmark geometry");
        const auto reads=SettingsFixture::reads;
        const auto queries=std::array{StyleFixture::activations,StyleFixture::themeQueries,StyleFixture::systemColorQueries};
        auto brush=state.checkboxResources.gradients[3];
        std::array<ID2D1BitmapBrush*,9> ramps{};
        for(size_t i=0;i<ramps.size();++i) ramps[i]=state.checkboxResources.ramps[3].slots[i].brush.Get();
        Check(tick==render() && glyph==state.selectionCheckGlyph && brush==state.checkboxResources.gradients[3] && SettingsFixture::reads==reads,
            "warm checkbox paint rebuilt resources, changed pixels or read settings");
        Check(queries==std::array{StyleFixture::activations,StyleFixture::themeQueries,StyleFixture::systemColorQueries},"checkbox paint queried native color providers");
        size_t changed=0;
        for(int y=20;y<40;++y) for(int x=20;x<40;++x) changed+=At(plain,x,y)!=At(tick,x,y);
        Check(changed>20 && changed<200,"checkbox checkmark is missing or fills the checkbox");
        const auto mark=state.checkboxResources.marks[3];
        Check(mark.a>0 && mark.a<1,"checkbox checkmark is invisible or harshly opaque");
        const bool lightFirst=std::wstring_view(background).starts_with(L"#eeeeee") || std::wstring_view(background).starts_with(L"linear-gradient(to right, #eeeeee");
        const bool darkFirst=std::wstring_view(background)==L"#101010" || std::wstring_view(background).starts_with(L"linear-gradient(to right, #101010");
        if(lightFirst || darkFirst) {
            Check(lightFirst ? mark.r==0:mark.r==1,"checkmark contrast did not follow solid/first gradient color");
            bool softened=false;
            for(int y=20;y<40;++y) for(int x=20;x<40;++x) {
                const int a=Channel(At(plain,x,y),16),b=Channel(At(tick,x,y),16);
                if(lightFirst ? b<a && b>0:b>a && b<255) softened=true;
            }
            Check(softened,"checkmark pixels do not show translucent contrasting ink");
        }
        for(size_t i=0;i<ramps.size();++i) Check(ramps[i]==state.checkboxResources.ramps[3].slots[i].brush.Get(),"warm checkbox paint replaced dither texture");
        UINT64 bytes=0;
        for(const auto& ramp:state.checkboxResources.ramps[3].slots) if(ramp.brush) {
            ComPtr<ID2D1Bitmap> bitmap; ramp.brush->GetBitmap(&bitmap); const auto size=bitmap->GetPixelSize(); bytes+=UINT64(size.width)*size.height*4;
        }
        Check(bytes<=64*8*4,"one checkbox state exceeds the fixed gradient cache budget");
        if(brush) Check(bytes>0,"checkbox gradient did not use a cached dither texture");
    }
    Load(settings,Element(L"DrawSelectionCheckbox",LR"(background="accent" border-width="0" border-radius="0")"));
    auto palette=g_swipePalette.load();
    struct RestorePalette {unsigned long long value; ~RestorePalette(){g_swipePalette.store(value);}} restore{palette};
    g_swipePalette.store((palette&~0xffffffull)|RGB(16,16,16)); const auto dark=render();
    Check(state.checkboxResources.marks[3].r==1,"dark accent did not produce light checkmark");
    g_swipePalette.store((palette&~0xffffffull)|RGB(238,238,238)); const auto light=render();
    Check(dark!=light && state.checkboxResources.marks[3].r==0,"accent change retained stale fill/checkmark");
    Load(settings,Element(L"DrawSelectionCheckbox",LR"(background="transparent" border-width="0")"));
    state.nativeEditBackground=RGB(16,16,16); render(); Check(state.checkboxResources.marks[3].r==1,"transparent fill ignored dark native background");
    state.nativeEditBackground=RGB(238,238,238); render(); Check(state.checkboxResources.marks[3].r==0,"native background change retained stale checkmark");
    Check(glyph==state.selectionCheckGlyph,"palette changes rebuilt cached checkmark geometry");
    DropTarget(state); Check(!state.selectionCheckGlyph && !state.checkboxResources.snapshot,"device teardown retained checkbox cache");
    for(const auto& gradient:state.checkboxResources.gradients) Check(!gradient,"device teardown retained checkbox gradient");
    for(const auto& ramps:state.checkboxResources.ramps) for(const auto& ramp:ramps.slots) Check(!ramp.brush,"device teardown retained checkbox texture");
    std::cout<<"PASS: visible translucent checkmarks, first-stop contrast, accent/native background invalidation, warm geometry/brush reuse and bounded device caches\n";
}

// Both consumers share cached palette/gradient preparation, but retain their own geometry.
static void TestThemedAccentSurface(bool badgeSurface) {
    using namespace ThemeRegression;
    OptionEffects::Settings settings; Fonts(); StyleFixture::Session style; Surface surface;
    auto& state=surface.state; state.nativeTextColor=RGB(240,240,240); state.nativeEditBackground=RGB(32,32,32);
    Check(MakeTextFormats(state),"badge count text format");
    const wchar_t* name=badgeSurface ? L"PaintGroupBadge":L"DrawSwipeIndicator";
    auto& cache=badgeSurface ? state.groupBadgeResources:state.swipeResources;
    SectionBadge count; count.text=L"12";
    GroupBadgeLayout badge; badge.badge=&count; badge.reveal=1.f;
    SwipeIndicator motion; motion.direction=1; motion.originY=40;
    const auto pose=[&](SwipeIndicator::Property property,float value) {motion.springs[property].value=motion.springs[property].target=value;};
    pose(SwipeIndicator::Scale,1); pose(SwipeIndicator::X,20); pose(SwipeIndicator::Width,120);
    pose(SwipeIndicator::Height,40); pose(SwipeIndicator::Alpha,1); pose(SwipeIndicator::Length,16); pose(SwipeIndicator::ArrowHeight,9);
    float opacity=1.f; bool content=false;
    const auto render=[&] {return surface.Render([&](ViewState& view) {
        const float scale=view.presentationDpi/96.f;
        badge.pill=D2D1::RectF(20*scale,20*scale,140*scale,60*scale);
        badge.glyph=content ? 20*scale:0; badge.glyphLeft=28*scale;
        badge.textLeft=60*scale; badge.textWidth=content ? 40*scale:0;
        const auto saved=count.text; if(!content) count.text.clear();
        motion.originY=40*scale; pose(SwipeIndicator::Alpha,opacity); pose(SwipeIndicator::ArrowAlpha,content ? 1.f:0.f);
        if(badgeSurface) PaintGroupBadge(view,badge,opacity);
        else DrawSwipeIndicator(view,motion,LARGE_INTEGER{},float(Surface::width),0,float(Surface::height));
        count.text=saved;
    });};
    const auto compiled=Parse(Element(name,LR"(background="#123456" border-width="2px" border-style="dashed" border-radius="3px")"));
    for(size_t mode=0;mode<2;++mode) {
        const auto& styles=badgeSurface ? compiled->groupBadges[mode]:compiled->swipeIndicators[mode];
        Check(styles[0].background.color.rgb==0x123456 && styles[0].borderWidth==2 && styles[0].borderStyle==Theme::BorderStyle::Dashed,
            "accent surface properties did not compile");
    }
    for(UINT dpi:{96u,144u,192u}) for(bool rtl:{false,true}) for(int direction:{1,-1}) {
        if(badgeSurface && direction<0) continue;
        Regression::Scope scope(std::string(badgeSurface ? "badge":"swipe")+" dpi="+std::to_string(dpi)+" rtl="+std::to_string(rtl)+" direction="+std::to_string(direction));
        state.presentationDpi=dpi; state.rtlLayout=rtl; state.themeMode=Theme::Mode::Light; motion.direction=direction;
        const bool left=badgeSurface ? !rtl:(direction>0)!=rtl;
        const int x=left ? MulDiv(20,dpi,96):int(Surface::width)-MulDiv(140,dpi,96);
        const int y=MulDiv(40,dpi,96),width=MulDiv(120,dpi,96),top=MulDiv(20,dpi,96);
        Load(settings,L""); const auto native=render();
        Load(settings,Element(name,LR"(background="transparent" border-color="#ff0000" border-width="1px" border-radius="0")"));
        const auto border=render(); const int thickness=MulDiv(1,dpi,96);
        for(int px=x-2;px<x+thickness+3;++px) Check(At(border,px,y)==(px>=x && px<x+thickness ? 0xffff0000u:0u),
            "accent surface border escaped bounds or lost DPI thickness");
        Check(At(border,x+width/2,y)==0,"border-only accent surface gained a fill");
        BorderPatterns([&](const wchar_t* pattern) {
            Load(settings,Element(name,L"background=\"transparent\" border-color=\"#ff0000\" border-width=\"2px\" border-radius=\"0\" border-style=\""+std::wstring(pattern)+L"\""));
            const auto pixels=render(); std::vector<DWORD> samples;
            for(int px=x+MulDiv(8,dpi,96);px<x+width-MulDiv(8,dpi,96);++px) samples.push_back(At(pixels,px,top));
            return samples;
        });
        Load(settings,L"<theme><element name=\""+std::wstring(name)+L"\" border-width=\"0\" border-radius=\"0\"><light background=\"#ff0000\" /><dark background=\"#00ff00\" /></element></theme>");
        Check(At(render(),x+width/2,y)==0xffff0000u,"light accent surface override is wrong");
        state.themeMode=Theme::Mode::Dark; Check(At(render(),x+width/2,y)==0xff00ff00u,"dark accent surface override is stale"); state.themeMode=Theme::Mode::Light;
        Load(settings,Element(name,LR"(background="#ff000080" border-width="0" border-radius="0")"));
        opacity=.5f; if(badgeSurface) badge.reveal=.5f;
        Check(At(render(),x+width/2,y)==(badgeSurface ? 0x20200000u:0x40400000u),"accent surface alpha did not multiply animation/reveal");
        opacity=1; badge.reveal=1;
        Load(settings,Element(name,LR"xml(background="linear-gradient(to right, #ff0000, #0000ff)" border-width="0" border-radius="0")xml"));
        const auto gradient=render(); const auto reads=SettingsFixture::reads;
        auto brush=cache.gradients[0]; auto ramp=cache.ramps[0].slots[0].brush;
        Check(gradient==render() && cache.gradients[0]==brush && cache.ramps[0].slots[0].brush==ramp && SettingsFixture::reads==reads,
            "warm accent surface changed pixels, rebuilt brushes or read settings");
        Check(brush && ramp,"accent gradient did not use cached native/dither brushes");
        for(int px=x+3;px<x+width-3;++px) {
            const double t=(px-x+.5)/width; const auto p=At(gradient,px,y);
            Check(std::abs(int(Channel(p,16))-255*(1-t))<2 && std::abs(int(Channel(p,0))-255*t)<2,
                "accent gradient differs from independent physical-direction interpolation or has a center seam");
        }
        if(!badgeSurface) {
            pose(SwipeIndicator::Far,20);
            Load(settings,Element(name,LR"(background="#ff0000" border-width="0" border-radius="0")")); const auto a=render();
            Load(settings,Element(name,LR"(background="#ff0000" border-width="0" border-radius="9999px")"));
            Check(a==render(),"swipe theme radius changed its animated shape");
            Check(At(a,left ? x+width-1:x,top)==0,"swipe lost its dynamic far corner");
            pose(SwipeIndicator::Far,0);
        } else {
            Load(settings,Element(name,LR"(background="#ff0000" border-width="0" border-radius="99999px")"));
            Check(At(render(),x,top)==0,"group badge radius did not clamp to its bounds");
            Load(settings,Element(name,LR"(background="#ff0000" border-width="0" border-radius="25%")"));
            const auto percent=render();
            CheckRadius([&](int offset) {return At(percent,x+offset,top+offset);},
                MulDiv(40,dpi,96)*.25f,"badge percent radius did not use the shorter side");
        }
        state.themeHighContrast=true; Check(render()==native,"accent surface high contrast did not restore native pixels"); state.themeHighContrast=false;
        Load(settings,L""); Check(render()==native,"clearing theme did not restore native accent surface");
    }
    state.presentationDpi=96; state.rtlLayout=false; motion.direction=1; content=true;
    for(const auto* background:{L"#101010",L"#eeeeee",L"linear-gradient(to right, #101010, #eeeeee)"}) {
        Load(settings,Element(name,L"background=\""+std::wstring(background)+L"\" border-width=\"0\" border-radius=\"0\""));
        content=false; const auto plain=render(); content=true; const auto pixels=render();
        const bool dark=std::wstring_view(background)!=L"#eeeeee";
        Check(cache.marks[0].r==(dark ? 1.f:0.f) && cache.marks[0].a>0 && cache.marks[0].a<1,
            "accent foreground is not translucent or ignores the first gradient stop");
        size_t glyph=0,text=0;
        for(int yy=20;yy<60;++yy) for(int xx=20;xx<140;++xx) if(At(plain,xx,yy)!=At(pixels,xx,yy)) {
            if(xx<55) ++glyph; if(xx>=60) ++text;
        }
        Check(badgeSurface ? glyph>20 && text>5:text>20,"badge checkmark/count or swipe chevron did not draw with calculated foreground");
    }
    auto glyph=state.selectionCheckGlyph; render(); render();
    Check(!badgeSurface || (glyph && glyph==state.selectionCheckGlyph),"warm badge rebuilt its shared checkmark geometry");
    DropTarget(state); Check(!cache.snapshot && !cache.gradients[0],"device teardown retained accent surface cache");
    for(const auto& ramp:cache.ramps[0].slots) Check(!ramp.brush,"device teardown retained accent gradient texture");
    BorderCacheReleased(state.themeBorderStrokes);
    std::cout<<"PASS: "<<(badgeSurface ? "group badge":"swipe indicator")<<" DPI/RTL, physical gradients, border patterns, opacity, contrast, native fallback and caches\n";
}

static void TestThemedSwipeComposition() {
    using namespace ThemeRegression;
    OptionEffects::Settings settings; Fonts(); StyleFixture::Session style; Surface surface;
    auto& state=surface.state; state.nativeTextColor=RGB(240,240,240); state.nativeEditBackground=RGB(32,32,32);
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency); state.qpcFrequency=double(frequency.QuadPart);
    auto& motion=state.swipeIndicator; motion.direction=1; motion.originY=80; QueryPerformanceCounter(&motion.origin);
    for(auto [p,value]:std::initializer_list<std::pair<SwipeIndicator::Property,double>>{{SwipeIndicator::X,20},{SwipeIndicator::Scale,1},
        {SwipeIndicator::Width,120},{SwipeIndicator::Height,40},{SwipeIndicator::Alpha,1},{SwipeIndicator::ArrowAlpha,1},
        {SwipeIndicator::Length,16},{SwipeIndicator::ArrowHeight,9}}) motion.springs[p].value=motion.springs[p].target=value;
    const auto render=[&] {state.themeSnapshot=Theme::Snapshot(); surface.palette.Resolve(state); Check(RenderSwipeFeedback(state),"themed swipe compositor submission");};
    Load(settings,Element(L"DrawSwipeIndicator",LR"(background="#101010" border-width="1px")")); render();
    Check(state.swipeComposition[0].visible && state.swipeCompositionSerial>0,"swipe fixture produced no foreground visual");
    const auto serial=state.swipeCompositionSerial; render(); Check(state.swipeCompositionSerial==serial,"unchanged swipe was needlessly recomposed");
    Load(settings,Element(L"DrawSwipeIndicator",LR"(background="#eeeeee" border-width="2px")")); render();
    Check(state.swipeCompositionSerial>serial && state.swipeResources.marks[0].r==0,"settled swipe retained old theme or contrast");
    Load(settings,Element(L"DrawSwipeIndicator",LR"(background="accent" border-width="0")"));
    const auto palette=g_swipePalette.load();
    struct RestorePalette {unsigned long long value; ~RestorePalette(){g_swipePalette.store(value);}} restore{palette};
    g_swipePalette.store((palette&~0xffffffull)|RGB(16,16,16)); render(); const auto accentSerial=state.swipeCompositionSerial;
    g_swipePalette.store((palette&~0xffffffull)|RGB(238,238,238)); render();
    Check(state.swipeCompositionSerial>accentSerial && state.swipeResources.marks[0].r==0,"settled swipe retained old accent contrast");
    motion.committed=true; ++state.swipeFeedbackRevision; QueryPerformanceCounter(&motion.origin); render();
    auto fade=state.swipeComposition[0].fade; const auto started=state.swipeComposition[0].fadeStarted.QuadPart;
    Check(fade,"committed themed swipe has no compositor fade");
    const auto fadingSerial=state.swipeCompositionSerial;
    state.themeMode=Theme::Mode::Dark; render();
    Check(state.swipeCompositionSerial>fadingSerial && fade==state.swipeComposition[0].fade && started==state.swipeComposition[0].fadeStarted.QuadPart,
        "appearance update failed to repaint or restarted committed swipe fade");
    state.themeHighContrast=true; render(); Check(!state.swipeResources.snapshot,"high contrast retained custom swipe resources");
    DropTarget(state); Check(!state.swipeComposition[0].visual && !state.swipeResources.snapshot,"device teardown retained themed compositor state");
    std::cout<<"PASS: settled/committed swipe theme updates, composition reuse, preserved fade clock, high contrast and teardown\n";
}
