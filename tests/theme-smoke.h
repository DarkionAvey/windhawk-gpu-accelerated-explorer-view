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
struct Surface {
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
    }
    ~Surface() {
        if(state.target) state.target->SetTarget(previous.Get()); target.Reset(); cpu.Reset(); previous.Reset();
        DropTarget(state); DestroyWindow(state.hwnd); state.hwnd=nullptr; DestroyWindow(frame);
    }
    template<class Draw> std::vector<DWORD> Render(Draw draw) {
        state.themeSnapshot=Theme::Snapshot(); PrepareHighlightBrushes(state);
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

struct Tooltip {
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
        ResolveItemTipPalette(state);
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
        Load(settings,L""); const auto native=render(); noRamps();
        Load(settings,theme(LR"(background="#ff0000" border-width="3px" border-radius="0" />)"));
        surface.state.themeHighContrast=true;
        Check(render()==native,"high contrast did not restore native scrollbar pixels");
        surface.state.themeHighContrast=false;
        Load(settings,L""); Check(render()==native,"clearing theme did not restore native scrollbar pixels");
    }
    DropTarget(surface.state); noRamps();
    for(const auto& gradient:surface.state.scrollbarResources.gradients) Check(!gradient,"device teardown retained scrollbar gradients");
    std::cout<<"PASS: scrollbar "<<(thumb ? "thumb":"track")<<" axes, states, light/dark, DPI/RTL pixels, borders, corners, alpha, gradients, cache and native fallback\n";
}

static void TestThemedTooltip() {
    using namespace ThemeRegression;
    OptionEffects::Settings settings; Fonts();
    for(UINT dpi:{96u,144u,192u}) {
        Regression::Scope scope("tooltip theme dpi="+std::to_string(dpi));
        Tooltip tooltip(dpi); auto& tip=tooltip.state.itemTip;
        Load(settings,Element(L"RenderItemTip",LR"(background="#202020" border-color="#ff0000" border-width="1px" border-radius="0" box-shadow="none")"));
        tooltip.Show();
        Check(tip.margin==0 && tooltip.Pixel(0,tip.size.cy/2)==0xffff0000u,"tooltip square border/disabled shadow pixels");
        Check(tooltip.Pixel(tip.size.cx/2,tip.size.cy-3)==0xff202020u,"tooltip solid background pixel");
        for(int shift:{0,8,16}) {
            const unsigned foreground=(tip.foreground>>shift)&255;
            const unsigned expected=(foreground*65+32*35)/100;
            Check(((tip.label>>shift)&255)==expected,"muted metadata did not blend against custom background");
        }
        const SIZE size=tip.size;
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
        Load(settings,L"<theme><element name=\"RenderItemTip\" background=\"#ff0000\" box-shadow=\"none\"><dark background=\"#00ff00\" /></element></theme>");
        tooltip.Show(Theme::Mode::Dark);
        Check(tooltip.Pixel(tip.size.cx/2,tip.size.cy-3)==0xff00ff00u,"tooltip dark override did not render");
        tooltip.Show(Theme::Mode::Dark,true);
        Check(tip.style.background.kind==Theme::Paint::Kind::Default,"high contrast retained tooltip customization");
        tip.DropTarget(); Check(!tip.shadowBitmap && !tip.gradient,"tooltip device loss retained resources");
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
