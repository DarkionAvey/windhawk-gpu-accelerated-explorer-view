// Native text colors remain authoritative even when unrelated theme/app
// resources disagree with the surface that Explorer actually paints.
static void TestThemeForegroundAuthority() {
    StyleFixture::Session faults;
    ViewState state;
    const COLORREF body=RGB(240,240,240), header=RGB(220,230,240);
    StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},body},
        {{3,HP_HEADERITEM,HIS_NORMAL,TMT_TEXTCOLOR},header},
        {{1,0,0,TMT_FILLCOLOR},RGB(255,255,255)}};
    StyleFixture::appBg={255,255,255,255}; StyleFixture::appFg={255,0,0,0};
    RefreshNativePalette(state);
    Check(state.nativeTextColor==body && state.nativeHeaderTextColor==header,
        "conflicting background overrode successful native foreground colors");
    for(unsigned i=0;i<32;++i) {
        StyleFixture::appBg=i%2 ? ABI::Windows::UI::Color{255,0,0,0} :
            ABI::Windows::UI::Color{255,255,255,255};
        StyleFixture::appFg=i%2 ? ABI::Windows::UI::Color{255,255,255,255} :
            ABI::Windows::UI::Color{255,0,0,0};
        StyleFixture::colors[{1,0,0,TMT_FILLCOLOR}]=i%2 ? RGB(0,0,0) : RGB(255,255,255);
        Check(!RefreshNativePalette(state,i%3==0) && state.nativeTextColor==body &&
            state.nativeHeaderTextColor==header,"maintenance/settings changed valid native colors");
    }
    Check(StyleFixture::fillQueries==0,"transparent list consulted an unrelated theme fill");
    StyleFixture::colors.clear(); // All native queries fail, including outputs written on failure.
    StyleFixture::appBg={255,255,255,255}; StyleFixture::appFg={255,0,0,0};
    Check(!RefreshNativePalette(state,true) && state.nativeTextColor==body &&
        state.nativeHeaderTextColor==header,"transient lookup failure replaced valid native colors");
    StyleFixture::failApp=true;
    Check(!RefreshNativePalette(state,true) && state.nativeTextColor==body &&
        state.nativeHeaderTextColor==header,"app activation failure replaced valid native colors");
    StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},COLORREF(CLR_INVALID)},
        {{3,HP_HEADERITEM,HIS_NORMAL,TMT_TEXTCOLOR},COLORREF(0x01000001)}};
    Check(!RefreshNativePalette(state) && state.nativeTextColor==body &&
        state.nativeHeaderTextColor==header,"invalid successful result replaced retained colors");
    // Genuine new native colors still update, including black and low-contrast
    // colors that cannot be judged against an inherited/unknown background.
    for(COLORREF color:{RGB(0,0,0),RGB(255,255,255),RGB(25,25,25),RGB(190,140,210)}) {
        StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},color},
            {{3,HP_HEADERITEM,HIS_NORMAL,TMT_TEXTCOLOR},color},
            {{1,0,0,TMT_FILLCOLOR},color}};
        RefreshNativePalette(state,true);
        Check(state.nativeTextColor==color && state.nativeHeaderTextColor==color,
            "valid native color was classified as an invalid fallback");
    }
    StyleFixture::highContrast=true; StyleFixture::text=RGB(30,40,50);
    RefreshNativePalette(state,true);
    Check(state.nativeTextColor==StyleFixture::text && state.nativeHeaderTextColor==StyleFixture::text,
        "high contrast system colors were altered");
    StyleFixture::highContrast=false; StyleFixture::colors.clear(); RefreshNativePalette(state,true);
    Check(state.nativeTextColor==RGB(190,140,210) && state.nativeHeaderTextColor==RGB(190,140,210),
        "temporary high contrast replaced retained native theme colors");
    for(COLORREF invalid:{COLORREF(CLR_INVALID),COLORREF(0x01000001),COLORREF(0xFFABCDEF)}) {
        ViewState invalidCase;
        StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},invalid},
            {{2,0,0,TMT_TEXTCOLOR},invalid},
            {{2,LVP_LISTITEM,LISS_NORMAL,TMT_TEXTCOLOR},RGB(220,180,120)},
            {{3,HP_HEADERITEM,HIS_NORMAL,TMT_TEXTCOLOR},invalid}};
        RefreshNativePalette(invalidCase);
        Check(invalidCase.nativeTextColor==RGB(220,180,120) &&
            invalidCase.nativeHeaderTextColor==invalidCase.nativeTextColor,
            "invalid theme result prevented the next valid native source");
        Stop(invalidCase.background);
    }
    ViewState second;
    StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},RGB(20,70,30)}};
    RefreshNativePalette(second);
    StyleFixture::colors.clear(); RefreshNativePalette(state);
    Check(second.nativeTextColor==RGB(20,70,30) && state.nativeTextColor==RGB(190,140,210),
        "one tab's native palette replaced another tab's retained colors");
    Stop(state.background); Stop(second.background);
    std::cout<<"PASS: native foreground authority with conflicting fill/app colors, repeated maintenance/settings, transient failures, real theme changes, high contrast and tab isolation\n";
}

static void TestResolvedStyles() {
    TestThemeForegroundAuthority();
    StyleFixture::Session faults;
    const auto factory=g_writeFactory;
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"style font factory");
    const auto configured=ConfiguredFontFamily();
    struct Restore {
        std::wstring family; ComPtr<IDWriteFactory> factory;
        ~Restore() {AcquireSRWLockExclusive(&g_fontFamilyLock); g_fontFamily=family;
            ReleaseSRWLockExclusive(&g_fontFamilyLock); g_writeFactory=factory;}
    } restore{configured,factory};
    AcquireSRWLockExclusive(&g_fontFamilyLock); g_fontFamily.clear(); ReleaseSRWLockExclusive(&g_fontFamilyLock);
    ViewState state;
    state.hwnd=CreateWindowExW(0,L"STATIC",L"private-style-fixture",WS_POPUP,
        0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.hwnd!=nullptr,"style window"); state.nativeHwnd=state.hwnd;
    // The classic system foreground is black while Windows' app background is dark.
    Check(RefreshNativePalette(state) && state.nativeTextColor==RGB(240,240,240) &&
        state.nativeHeaderTextColor==state.nativeTextColor,"dark palette retained classic black text");
    Check(StyleFixture::activations==1,"initial app palette resolution");
    StyleFixture::appFg={255,24,24,24};
    Check(!RefreshNativePalette(state) && StyleFixture::activations==1,
        "maintenance reactivated a palette provider or discarded copied tab colors");
    RefreshNativePalette(state,true);
    Check(state.nativeTextColor==RGB(24,24,24),"app fallback was judged against an unknown inherited surface");
    StyleFixture::appFg={255,240,240,240};
    StyleFixture::colors.clear(); StyleFixture::appBg.A=StyleFixture::appFg.A=0;
    RefreshNativePalette(state,true);
    Check(state.nativeTextColor==RGB(24,24,24) && state.nativeAppBackground==CLR_INVALID &&
        state.nativeAppForeground==CLR_INVALID,"transparent WinRT colors were accepted as opaque RGB");
    StyleFixture::failApp=true;
    StyleFixture::colors={{{1,0,0,TMT_FILLCOLOR},RGB(24,24,24)}};
    RefreshNativePalette(state,true);
    Check(state.nativeTextColor==RGB(24,24,24),"API failure overwrote the last resolved fallback color");
    StyleFixture::highContrast=true; StyleFixture::window=RGB(10,20,30); StyleFixture::text=RGB(30,40,50);
    RefreshNativePalette(state,true);
    Check(state.nativeTextColor==StyleFixture::text && state.nativeHeaderTextColor==StyleFixture::text,
        "high contrast system colors were altered");
    StyleFixture::highContrast=false; StyleFixture::window=RGB(255,255,255); StyleFixture::text=RGB(0,0,0);
    StyleFixture::failApp=false; StyleFixture::appBg={255,24,24,24}; StyleFixture::appFg={255,240,240,240};
    StyleFixture::colors.clear(); RefreshNativePalette(state,true);
    ViewState second;
    StyleFixture::appBg={255,250,250,250}; StyleFixture::appFg={255,10,10,10}; RefreshNativePalette(second);
    RefreshNativePalette(state);
    Check(state.nativeTextColor==RGB(240,240,240) && second.nativeTextColor==RGB(10,10,10),
        "tabs overwrote each other's cached app palettes");
    state.viewToggle=CreateWindowExW(0,L"STATIC",L"style-button",WS_CHILD,0,0,20,20,
        state.hwnd,nullptr,GetModuleHandleW(nullptr),nullptr);
    Check(state.viewToggle!=nullptr,"style button window");
    RefreshNativePalette(state,true);
    Check(ValidRgb(state.viewToggleBackground) && ValidRgb(state.viewToggleForeground) &&
        ColorContrast(state.viewToggleBackground,state.viewToggleForeground)>=2.,"status chrome lacked validated colors");
    StyleFixture::appBg.A=StyleFixture::appFg.A=0; state.viewTogglePaletteDirty=true;
    RefreshNativePalette(state,true);
    Check(state.viewToggleBackground==RGB(255,255,255) && state.viewToggleForeground==RGB(0,0,0),
        "status chrome accepted transparent palette entries");
    state.ReleaseViewToggle();
    for(UINT dpi:{0u,1u,71u,769u,UINT_MAX}) {StyleFixture::dpi=dpi; Check(DpiOf(state.hwnd)==96,"invalid window DPI");}
    StyleFixture::dpi=96;
    state.presentationOnly=true;
    for(int dpi:{0,-1,INT_MAX}) {state.presentationDpi=dpi; Check(ViewDpi(state)==96,"invalid copied DPI");}
    state.presentationDpi=144;
    for(LONG height:{0L,1L,LONG_MIN,LONG_MAX,-2000L}) {
        state.presentationFont=FallbackUiFont(144); state.presentationFont.lfHeight=height;
        Check(ValidNativeFont(NativeHeaderFont(state),144),"invalid copied font height escaped validation");
    }
    state.presentationFont=FallbackUiFont(144);
    std::fill(std::begin(state.presentationFont.lfFaceName),std::end(state.presentationFont.lfFaceName),L'X');
    Check(ValidNativeFont(NativeHeaderFont(state),144),"unterminated native face escaped validation");
    state.presentationOnly=false;
    StyleFixture::systemFont=FallbackUiFont(96); wcscpy_s(StyleFixture::systemFont.lfFaceName,L"Arial");
    for(LONG height:{0L,1L,LONG_MIN,LONG_MAX}) {
        StyleFixture::themeFont=FallbackUiFont(96); StyleFixture::themeFont.lfHeight=height;
        Check(wcscmp(NativeHeaderFont(state).lfFaceName,L"Arial")==0,"invalid successful theme font suppressed system fallback");
    }
    StyleFixture::themeFont=FallbackUiFont(96);
    StyleFixture::themeFont.lfFaceName[0]=L'\n';
    Check(wcscmp(NativeHeaderFont(state).lfFaceName,L"Arial")==0,"control character font name accepted");
    for(const auto& requested:{std::wstring(L"Definitely absent Windhawk font 7E55"),std::wstring(300,L'X'),
            std::wstring(L"Segoe\nUI"),std::wstring(L"Segoe UI\0unexpected",19)}) {
        Check(ResolveManualFont(requested).family.empty(),"missing/malformed font did not resolve to native fallback");
    }
    StyleFixture::themeFont=FallbackUiFont(96);
    AcquireSRWLockExclusive(&g_fontFamilyLock); g_fontFamily=L"Definitely absent Windhawk font 7E55";
    ReleaseSRWLockExclusive(&g_fontFamilyLock);
    Check(MakeTextFormats(state),"fallback text formats");
    for(auto* format:{state.textFormat.Get(),state.headerFormat.Get(),state.sortedHeaderFormat.Get()}) {
        WCHAR family[LF_FACESIZE]{};
        Check(SUCCEEDED(format->GetFontFamilyName(family,LF_FACESIZE)) && wcscmp(family,L"Segoe UI")==0,
            "missing configured family did not fall back consistently for body and headers");
    }
    AcquireSRWLockExclusive(&g_fontFamilyLock); g_fontFamily.clear(); ReleaseSRWLockExclusive(&g_fontFamilyLock);
    wcscpy_s(StyleFixture::themeFont.lfFaceName,L"Absent native family 7E55");
    Check(MakeTextFormats(state),"missing native family lacked an installed fallback");
    StyleFixture::themeFont=FallbackUiFont(96);
    for(float size:{0.f,-1.f,std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN(),1.e30f}) {
        state.fontPx=size;
        Check(MakeTextFormats(state,true) && std::isfinite(state.textFormat->GetFontSize()) &&
            state.textFormat->GetFontSize()==17.f,"unusable body size prevented rendering");
    }
    // The actual dark render target disagrees with the theme/app backgrounds.
    // Native foregrounds must survive for text, group labels and chrome.
    StyleFixture::colors={{{1,0,0,TMT_TEXTCOLOR},RGB(240,240,240)},
        {{3,HP_HEADERITEM,HIS_NORMAL,TMT_TEXTCOLOR},RGB(240,240,240)},
        {{1,0,0,TMT_FILLCOLOR},RGB(255,255,255)}};
    StyleFixture::appBg={255,255,255,255}; StyleFixture::appFg={255,0,0,0};
    RefreshNativePalette(state,true);
    Check(EnsureTarget(state),"style GPU target");
    ComPtr<ID2D1Image> original; state.target->GetTarget(&original);
    ComPtr<ID2D1Bitmap1> surface,cpu;
    auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(300,100),nullptr,0,&props,&surface)),"style surface");
    props.bitmapOptions=D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    Check(SUCCEEDED(state.target->CreateBitmap(D2D1::SizeU(300,100),nullptr,0,&props,&cpu)),"style readback");
    state.target->SetTarget(surface.Get()); state.paintWidth=300;
    const unsigned color=ThemeRgb(state.nativeTextColor);
    state.target->BeginDraw(); state.target->Clear(Packed(0x181818));
    Text(state,L"Local Disk",5,0,200,40,color);
    Text(state,L"May 2026",5,40,200,80,ThemeRgb(state.nativeHeaderTextColor),true);
    Fill(state,10,85,90,89,color);
    Check(SUCCEEDED(state.target->EndDraw()) && SUCCEEDED(cpu->CopyFromBitmap(nullptr,surface.Get(),nullptr)),"style draw");
    D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"style pixels");
    unsigned glyphs=0, headerGlyphs=0;
    for(unsigned y=0;y<40;++y) for(unsigned x=0;x<200;++x)
        if(map.bits[y*map.pitch+x*4]>100) ++glyphs;
    for(unsigned y=40;y<80;++y) for(unsigned x=0;x<200;++x)
        if(map.bits[y*map.pitch+x*4]>100) ++headerGlyphs;
    const bool chrome=map.bits[86*map.pitch+20*4]>100;
    cpu->Unmap(); state.target->SetTarget(original.Get());
    Check(glyphs>40 && headerGlyphs>10 && chrome,
        "conflicting backgrounds removed native body/header glyphs or chrome pixels");
    DropTarget(state); DestroyWindow(state.hwnd); state.hwnd=nullptr;
    Stop(state.background); Stop(second.background);
    std::cout<<"PASS: theme/app/system color failures, high contrast, tab-local palette cache, malformed fonts/sizes/DPI, conflicting-background offscreen glyph/chrome pixels\n";
}
