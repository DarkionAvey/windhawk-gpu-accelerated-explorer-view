// User options verified by their result, not merely by a changed value.
//
// Canvas tests: real private files in a real ExplorerBrowser, the production
// settings handler (LoadSettings + kMsgSettings), then GPU pixels read back
// from the actual renderer. Expected geometry comes from the option value,
// DPI arithmetic and an independent DirectWrite measurement of the configured
// font; never from the mod's own layout helpers.
//
// Live sweep: the production AttachView on a genuine DefView, the production
// Wh_ModSettingsChanged broadcast and a real message loop. Every value of
// every option is applied in every view while the view is idle or busy
// (zoom, rename, hover/scroll, pinch, scrollbar drag); a crash is reported
// with the option, value, view and state that caused it.
//
// Adding an option: list it in OptionCatalog (options-smoke.h) and in
// kCoverage below with the release test that asserts its visible effect.
// option-coverage fails until both are done.
namespace OptionEffects {
using Values=std::map<std::wstring,std::wstring>;

// Where each option's user-visible effect is asserted.
struct Coverage { const wchar_t* key; const wchar_t* test; };
inline const Coverage kCoverage[]={
    {L"Appearance.rowHeight",L"option-effects-list"},
    {L"Appearance.fontSizePercent",L"option-effects-list"},
    {L"Appearance.gridFontSizePercent",L"option-effects-grid"},
    {L"Appearance.fontFamily",L"option-effects-appearance"},
    {L"Appearance.textAntialiasing",L"option-effects-appearance"},
    {L"Appearance.secondaryTextOpacity",L"option-effects-appearance"},
    {L"Appearance.dimDotFiles",L"option-effects-appearance"},
    {L"Appearance.middleEllipsis",L"option-effects-appearance"},
    {L"Appearance.listThumbnails",L"user-options"},
    {L"Appearance.zebraStripes",L"option-effects-appearance"},
    {L"Appearance.zebraOpacity",L"option-effects-appearance"},
    {L"Appearance.showEmptyRows",L"option-effects-appearance"},
    {L"Appearance.edgeMargin",L"option-effects-appearance"},
    {L"Selection.filenameOnlySelection",L"option-effects-list"},
    {L"Selection.selectionColor",L"option-effects-appearance"},
    {L"Selection.selectionOpacity",L"option-effects-appearance"},
    {L"Selection.selectionRadius",L"option-effects-appearance"},
    {L"Selection.gridSelectionRadius",L"option-effects-grid"},
    {L"Selection.marqueeOpacity",L"option-effects-appearance"},
    {L"Hover.detailedTooltips",L"item-tips"},
    {L"Hover.animateHover",L"user-options"},
    {L"Hover.hoverOpacity",L"option-effects-appearance"},
    {L"Hover.hoverSelectedOpacity",L"option-effects-appearance"},
    {L"Navigation.showNativeViewToggleButton",L"option-live-sweep-details"},
    {L"Navigation.smoothScrolling",L"user-options"},
    {L"Navigation.touchpadDirectManipulation",L"style-events"},
    {L"Navigation.touchpadSwipeNavigation",L"touch-input"},
    {L"Navigation.showScrollbar",L"user-options"},
    {L"Navigation.instantScrollbarScrub",L"user-options"},
    {L"Gestures.doubleClickUp",L"additional-gestures"},
    {L"Gestures.middleClickNewTab",L"additional-gestures"},
    {L"Gestures.twoFingerContextMenu",L"additional-gestures"},
    {L"Columns.preSort",L"option-live-sweep-details"},
    {L"Columns.folderPlacement",L"option-live-sweep-details"},
    {L"Columns.autoRefitColumns",L"resize-auto-refit"},
    {L"enableLogging",L"option-live-sweep-details"},
};

// Settings host for the production LoadSettings path. Records which options
// a test changed so it can prove it exercised everything it claims.
struct Settings {
    Values values=OptionCatalog::Defaults();
    std::set<std::wstring> touched;
    Settings() {SettingsFixture::values=&values; LoadSettings();}
    ~Settings() {values=OptionCatalog::Defaults(); LoadSettings(); SettingsFixture::values=nullptr;}
    Settings(const Settings&)=delete;
    Settings& operator=(const Settings&)=delete;
    void Set(const std::wstring& key,const std::wstring& value) {
        Check(values.contains(key),"option test used a key that is not in the settings schema");
        values[key]=value; touched.insert(key);
    }
    void Reset(const std::wstring& key) {values[key]=OptionCatalog::Defaults().at(key);}
    void RequireExercised(const wchar_t* test) const {
        for(const auto& coverage:kCoverage) if(!wcscmp(coverage.test,test) && !touched.contains(coverage.key))
            throw std::runtime_error("option effect test did not exercise "+Regression::Narrow(coverage.key)+
                ", which kCoverage assigns to it");
    }
};

inline std::string Text(const std::wstring& key,const std::wstring& value) {
    return "option "+Regression::Narrow(key)+"="+Regression::Narrow(value);
}
inline const char* ModeName(UINT mode,int size) {
    switch(mode) {
    case FVM_DETAILS: return "Details";
    case FVM_LIST: return "List";
    case FVM_SMALLICON: return "Small icons";
    default: return size>=96 ? "Large icons" : "Medium icons";
    }
}

inline void MakeFiles(const std::filesystem::path& folder,const std::vector<std::wstring>& files,
                      const std::vector<std::wstring>& folders={}) {
    Check(std::filesystem::create_directory(folder),"option fixture folder");
    for(const auto& name:files) {
        HANDLE file=CreateFileW((folder/name).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"option fixture file");
        const char bytes[]="option fixture"; DWORD written=0;
        WriteFile(file,bytes,sizeof(bytes),&written,nullptr); CloseHandle(file);
    }
    for(const auto& name:folders) Check(CreateDirectoryW((folder/name).c_str(),nullptr)!=FALSE,"option fixture subfolder");
}

// ---------------------------------------------------------------- pixels --
struct Box {
    int left=INT_MAX,top=INT_MAX,right=INT_MIN,bottom=INT_MIN;  // Inclusive.
    bool Empty() const {return right<left;}
    int Width() const {return Empty() ? 0 : right-left+1;}
    int Height() const {return Empty() ? 0 : bottom-top+1;}
    void Add(int x,int y) {left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y);}
};
struct Image {
    int width=0,height=0;
    std::vector<BYTE> bgra;  // Premultiplied, as the swap chain stores it.
    const BYTE* At(int x,int y) const {return bgra.data()+(size_t(y)*width+x)*4;}
    BYTE Alpha(int x,int y) const {return At(x,y)[3];}
    // Failure evidence: a 32-bit BMP in the working directory, which the
    // runner retains with the logs. Alpha is kept so transparent pixels show.
    void Save(const std::string& name) const {
        BITMAPFILEHEADER file{}; BITMAPV5HEADER info{};
        info.bV5Size=sizeof(info); info.bV5Width=width; info.bV5Height=-height; info.bV5Planes=1;
        info.bV5BitCount=32; info.bV5Compression=BI_BITFIELDS; info.bV5RedMask=0x00FF0000;
        info.bV5GreenMask=0x0000FF00; info.bV5BlueMask=0x000000FF; info.bV5AlphaMask=0xFF000000;
        file.bfType=0x4D42; file.bfOffBits=sizeof(file)+sizeof(info); file.bfSize=DWORD(file.bfOffBits+bgra.size());
        std::ofstream out(name,std::ios::binary);
        out.write(reinterpret_cast<const char*>(&file),sizeof(file)); out.write(reinterpret_cast<const char*>(&info),sizeof(info));
        out.write(reinterpret_cast<const char*>(bgra.data()),std::streamsize(bgra.size()));
        std::cout<<"SAVED "<<name<<std::endl;
    }
    template<class Predicate> Box Find(RECT area,Predicate predicate) const {
        Box box;
        for(int y=std::max<LONG>(0,area.top);y<std::min<LONG>(height,area.bottom);++y)
            for(int x=std::max<LONG>(0,area.left);x<std::min<LONG>(width,area.right);++x)
                if(predicate(At(x,y))) box.Add(x,y);
        return box;
    }
};
// Opaque #FF0000 at 100% opacity: the probe selection colour.
inline bool Red(const BYTE* p) {return p[2]>=250 && p[1]<=8 && p[0]<=8 && p[3]>=250;}
inline bool Ink(const BYTE* p) {return p[3]>=128;}       // Half-covered edge: glyph heights.
inline bool Coverage(const BYTE* p) {return p[3]>=32;}  // Any visible glyph pixel: extents.

// The font the renderer actually uses, measured independently.
struct FontProbe {
    ComPtr<IDWriteTextFormat> format;
    explicit FontProbe(IDWriteTextFormat* like,const std::wstring& family={}) {
        Check(like!=nullptr,"option probe has no renderer text format");
        std::wstring name=family;
        if(name.empty()) {
            name.resize(like->GetFontFamilyNameLength()+1);
            Check(SUCCEEDED(like->GetFontFamilyName(name.data(),UINT32(name.size()))),"renderer font family");
            name.resize(wcslen(name.c_str()));
        }
        ComPtr<IDWriteFontCollection> collection; like->GetFontCollection(&collection);
        Check(SUCCEEDED(g_writeFactory->CreateTextFormat(name.c_str(),collection.Get(),like->GetFontWeight(),
            like->GetFontStyle(),like->GetFontStretch(),like->GetFontSize(),L"",&format)),"independent probe format");
    }
    float Advance(const std::wstring& text) const {
        ComPtr<IDWriteTextLayout> layout; DWRITE_TEXT_METRICS metrics{};
        Check(SUCCEEDED(g_writeFactory->CreateTextLayout(text.c_str(),UINT32(text.size()),format.Get(),
            100000.f,1000.f,&layout)) && SUCCEEDED(layout->GetMetrics(&metrics)),"independent text advance");
        return metrics.width;
    }
    float CapHeight() const {
        ComPtr<IDWriteFontCollection> collection; format->GetFontCollection(&collection);
        if(!collection) Check(SUCCEEDED(g_writeFactory->GetSystemFontCollection(&collection)),"system fonts");
        std::wstring name(format->GetFontFamilyNameLength()+1,L'\0');
        format->GetFontFamilyName(name.data(),UINT32(name.size()));
        UINT32 index=0; BOOL exists=FALSE; ComPtr<IDWriteFontFamily> family; ComPtr<IDWriteFont> font;
        Check(SUCCEEDED(collection->FindFamilyName(name.c_str(),&index,&exists)),"probe family lookup");
        if(!exists) Check(SUCCEEDED(collection->FindFamilyName(L"Segoe UI",&index,&exists)) && exists,"probe fallback family");
        Check(SUCCEEDED(collection->GetFontFamily(index,&family)) &&
            SUCCEEDED(family->GetFirstMatchingFont(format->GetFontWeight(),format->GetFontStretch(),
                format->GetFontStyle(),&font)),"probe font face");
        DWRITE_FONT_METRICS metrics{}; font->GetMetrics(&metrics);
        return format->GetFontSize()*metrics.capHeight/metrics.designUnitsPerEm;
    }
};

// Real Shell folder in a private offscreen ExplorerBrowser, painted by the
// GPU renderer into a buffer that is read back before presentation.
struct Canvas {
    StyleFixture::Session style;
    ModifierFixture::Session modifiers;  // Ctrl+wheel must not inherit the desktop's Shift.
    LANGID language=UiLocaleFixture::language;
    ProviderSelectionFixture::Browser host{true};
    ViewState& state=host.state;
    FocusFixture::Session focus{host.frame,host.state.hwnd};
    std::filesystem::path folder;
    Canvas(const std::filesystem::path& path,int count,int dpi,bool rtl) : folder(path) {
        StyleFixture::dpi=UINT(dpi);
        if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"option canvas fonts");
        host.Navigate(path.wstring(),count);
        // The mod derives direction from the Shell UI on every settings change;
        // an RTL UI language makes that genuinely RTL, not just this flag.
        if(rtl) UiLocaleFixture::language=MAKELANGID(LANG_ARABIC,SUBLANG_ARABIC_SAUDI_ARABIA);
        state.rtlLayout=ShellUiIsRtl(state);
        Check(state.rtlLayout==rtl,"option canvas direction");
        state.deferColumnProviders=false;
        state.featureInitializationStep=kOverlayFeatureSteps;  // A fully initialized overlay requests Details values.
        FinishColumnTransition(state); RefreshExplorerColumns(state,true);
        state.nativeTextColor=state.nativeHeaderTextColor=RGB(255,255,255);
        state.textFormatsDirty=true;
        Check(EnsureTarget(state),"option canvas GPU target");
        // Like the real overlay window, receive every private worker/selection
        // message (Details values, selection imports, hints). Painting stays
        // explicit so frames can be read back before presentation.
        Check(SetWindowSubclass(state.hwnd,Forward,kCanvasSubclass,0),"option canvas private messages");
    }
    ~Canvas() {RemoveWindowSubclass(state.hwnd,Forward,kCanvasSubclass); UiLocaleFixture::language=language;}
    static constexpr UINT_PTR kCanvasSubclass=0x0BE;
    static LRESULT CALLBACK Forward(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
        if(message>=WM_APP && message<0xC000) return OverlayProc(hwnd,message,wp,lp);
        return DefSubclassProc(hwnd,message,wp,lp);
    }
    void Settle() {
        Check(Eventually([&] {host.Pump(); ReceiveDirectoryResults(state);
            return state.loaded && !state.directoryPending;},30000),"option canvas did not settle after a change");
        AdvanceRowHeight(state,true); StopRowTransition(state); StopScrollAnimation(state);
        StopHoverAnimation(state); StopKeyboardSelectionAnimation(state); FinishColumnTransition(state);
        state.hovered=-1;
    }
    // What Windhawk does on a settings change, delivered to this tab's owner.
    // The cross-window broadcast itself is covered by the live sweep.
    void Apply(Settings& settings,const std::wstring& key,const std::wstring& value) {
        Regression::Scope scope(Text(key,value));
        settings.Set(key,value); LoadSettings();
        OverlayProc(state.hwnd,kMsgSettings,0,0);
        Settle();
    }
    // Change the native view like a user command does; the GPU follows the
    // native mode on every later settings change, so both must agree.
    void Layout(UINT mode,int size) {
        {
            NativeViewInput input(&state,true);
            Check(SUCCEEDED(state.folderView->SetViewModeAndIconSize(FOLDERVIEWMODE(mode),size)),"option canvas native view mode");
        }
        ApplyViewLayout(state,{mode,size},false); state.cachedNativeMode=mode;
        state.textFormatsDirty=true; MakeTextFormats(state); UpdateMetrics(state); UpdateColumns(state);
        SetScrollImmediate(state,0); Settle();
        Check(state.cachedNativeMode==mode && state.gridView==IsIconViewMode(mode),"option canvas GPU and native view modes disagree");
    }
    int Row(const std::wstring& name) const {
        const int row=GpuRowForNativePath(state,(folder/name).wstring());
        Check(row>=0,"option canvas row is missing");
        // Probes measure on screen: bring the item (and the row after it) into view.
        const float top=DocumentRowTop(state,row), bottom=top+DisplayRowHeight(state,row)*3.f;
        if(top<state.scrollNow || bottom>state.scrollNow+float(ViewHeight(state)))
            SetScrollImmediate(state,std::max(0.f,top-state.rowPx));
        return row;
    }
    void Select(std::initializer_list<int> rows) {
        state.selectedFlags.assign(size_t(state.total),0);
        for(int row:rows) state.selectedFlags[size_t(row)]=1;
        state.selected=state.anchor=rows.size() ? *rows.begin() : -1;
        state.keyboardCaretVisible=false;
    }
    Image Render() {
        // Read the buffer just drawn, before Present rotates the flip chain.
        struct Detach {
            ViewState& state; ComPtr<IDXGISwapChain1> swap; HANDLE latency;
            ~Detach() {state.swapChain=std::move(swap); state.frameLatencyWaitable=latency;}
        } detach{state,std::move(state.swapChain),std::exchange(state.frameLatencyWaitable,nullptr)};
        // Measure the settled result: rasterize all text and let option-driven
        // transitions (such as dotfile opacity) reach their destination.
        Check(Eventually([&] {Paint(state); return !state.textRasterPending && !state.itemOpacityAnimating;},10000) &&
            state.backbuffer && state.visible,"option canvas did not finish a settled GPU frame");
        const auto size=state.backbuffer->GetPixelSize();
        ComPtr<ID2D1Bitmap1> cpu;
        const auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ|D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            state.backbuffer->GetPixelFormat());
        Check(SUCCEEDED(state.target->CreateBitmap(size,nullptr,0,&props,&cpu)) &&
            SUCCEEDED(cpu->CopyFromBitmap(nullptr,state.backbuffer.Get(),nullptr)),"option canvas readback");
        D2D1_MAPPED_RECT map{}; Check(SUCCEEDED(cpu->Map(D2D1_MAP_OPTIONS_READ,&map)),"option canvas map");
        Image image; image.width=int(size.width); image.height=int(size.height);
        image.bgra.resize(size_t(image.width)*image.height*4);
        for(int y=0;y<image.height;++y) memcpy(image.bgra.data()+size_t(y)*image.width*4,map.bits+size_t(y)*map.pitch,size_t(image.width)*4);
        cpu->Unmap();
        return image;
    }
    // Physical pixel span of a logical horizontal range.
    std::pair<int,int> Span(float left,float right) const {
        RECT client{}; GetClientRect(state.hwnd,&client);
        return state.rtlLayout ? std::pair<int,int>{int(client.right-right),int(client.right-left)} :
            std::pair<int,int>{int(left),int(right)};
    }
    // Vertical band and name region of an item, used only to choose where to look.
    RECT RowBand(int row) const {
        const auto visual=LayoutRowVisual(state,row,1.f);
        RECT client{}; GetClientRect(state.hwnd,&client);
        return {0,LONG(std::floor(visual.top))-2,client.right,LONG(std::ceil(visual.top+visual.height))+2};
    }
    RECT NameArea(int row) {
        const auto g=LayoutItem(state,LayoutRowVisual(state,row,1.f));
        if(state.gridView) {
            // Small icons put the caption beside the icon; larger grids below it.
            if(g.text.left>=g.icon.right) {
                const auto span=Span(g.icon.right+1.f,g.cell.right);
                return {span.first,LONG(std::floor(g.cell.top)),span.second,LONG(std::ceil(g.cell.bottom))};
            }
            const auto span=Span(g.cell.left,g.cell.right);
            return {span.first,LONG(std::ceil(g.icon.bottom))+1,span.second,LONG(std::ceil(g.cell.bottom))};
        }
        const float end=ColumnsPresented(state) ? NameColumnEnd(state) : ListEndX(state);
        const auto span=Span(g.icon.right+1.f,end);
        const RECT band=RowBand(row);
        return {span.first,band.top,span.second,band.bottom};
    }
};

// Ink of a name, then the probe selection over it: the highlight must cover
// every glyph pixel, with the configured padding, and nothing should depend
// on the renderer's own idea of where the text is.
struct Highlight { Box ink, fill, firstUncovered; int uncovered=0; Image plain, selected; void Save(const std::string& name) const {plain.Save(name+"-plain.bmp"); selected.Save(name+"-selected.bmp");} };
inline Highlight MeasureHighlight(Canvas& canvas,int row) {
    canvas.Select({});
    const auto plain=canvas.Render();
    const RECT area=canvas.NameArea(row);
    Highlight result;
    result.ink=plain.Find(area,Coverage);
    canvas.Select({row});
    const auto selected=canvas.Render();
    RECT band=canvas.state.gridView ? RECT{0,0,selected.width,selected.height} : canvas.RowBand(row);
    if(canvas.state.gridView) {
        const auto g=LayoutItem(canvas.state,LayoutRowVisual(canvas.state,row,1.f));
        const auto span=canvas.Span(g.cell.left-2.f,g.cell.right+2.f);
        band={span.first,LONG(g.cell.top)-2,span.second,LONG(g.cell.bottom)+2};
    }
    result.fill=selected.Find(band,Red);
    for(int y=std::max<LONG>(0,area.top);y<std::min<LONG>(plain.height,area.bottom);++y)
        for(int x=std::max<LONG>(0,area.left);x<std::min<LONG>(plain.width,area.right);++x)
            if(Coverage(plain.At(x,y)) && selected.Alpha(x,y)<250) {++result.uncovered; result.firstUncovered.Add(x,y);}
    result.plain=plain; result.selected=selected;
    canvas.Select({});
    Check(!result.ink.Empty(),"option probe found no filename ink");
    Check(!result.fill.Empty(),"option probe found no selection highlight");
    return result;
}

inline void PrepareProbe(Canvas& canvas,Settings& settings) {
    canvas.Apply(settings,L"Selection.selectionColor",L"#FF0000");
    canvas.Apply(settings,L"Selection.selectionOpacity",L"100");
    canvas.Apply(settings,L"Selection.selectionRadius",L"0");
    canvas.Apply(settings,L"Selection.gridSelectionRadius",L"0");
    canvas.Apply(settings,L"Appearance.zebraStripes",L"false");
    canvas.Apply(settings,L"Hover.animateHover",L"false");
}

// List/Details: filename-only highlight must span exactly the name.
inline void CheckFilenameHighlight(Canvas& canvas,const std::wstring& name,const char* where) {
    auto& state=canvas.state;
    const int dpi=ViewDpi(state);
    const auto h=MeasureHighlight(canvas,canvas.Row(name));
    const float pad=float(Scale96(7,dpi));
    const float advance=FontProbe(state.textFormat.Get()).Advance(name);
    const float bearing=.15f*state.fontPx+2.f;
    const int leading=h.ink.left-h.fill.left, trailing=h.fill.right-h.ink.right;
    // An ellipsized name is highlighted to its visible end; the exact width
    // applies when the whole name fits before the Name column/list end.
    const auto end=canvas.Span(0.f,ColumnsPresented(state) ? NameColumnEnd(state) : ListEndX(state));
    const bool fits=state.rtlLayout ? h.ink.right+1-advance>=end.first+2 : h.ink.left+advance<=end.second-2;
    const bool exact=!fits || std::abs(h.fill.Width()-(advance+2*pad))<=2.5f;
    if(h.uncovered || !exact || leading<pad-2 || trailing<pad-2.5f || leading>pad+bearing || trailing>pad+bearing)
        std::cout<<"FILENAME HIGHLIGHT "<<where<<" name="<<Regression::Narrow(name)<<" fill="<<h.fill.left<<".."<<h.fill.right
            <<" ink="<<h.ink.left<<".."<<h.ink.right<<" expected-width="<<advance+2*pad<<" font="<<state.fontPx
            <<" uncovered="<<h.uncovered<<" dpi="<<dpi<<" rtl="<<state.rtlLayout<<std::endl, h.Save("option-filename-highlight");
    Check(!h.uncovered,"filename-only highlight left part of the filename uncovered");
    Check(exact,"filename-only highlight width differs from the measured filename plus its padding");
    Check(leading>=pad-2 && trailing>=pad-2.5f && leading<=pad+bearing && trailing<=pad+bearing,
        "filename-only highlight padding is not the configured inset on both sides");
}
inline void CheckFullRowHighlight(Canvas& canvas,const std::wstring& name) {
    const auto h=MeasureHighlight(canvas,canvas.Row(name));
    RECT client{}; GetClientRect(canvas.state.hwnd,&client);
    Check(!h.uncovered && h.fill.Width()>=client.right-Scale96(40,ViewDpi(canvas.state)),
        "full-row highlight did not span the row");
}
// Text size: the DirectWrite format and the rendered capital height.
inline void CheckFont(Canvas& canvas,int expected,const std::wstring& capitals,const char* what) {
    auto& state=canvas.state;
    Check(state.textFormat && std::abs(state.textFormat->GetFontSize()-float(expected))<.01f,what);
    canvas.Select({});
    const int row=canvas.Row(capitals);  // Scrolls it into view before the frame is drawn.
    const auto image=canvas.Render();
    const RECT area=canvas.NameArea(row);
    // Any visible coverage: thin stems of small text never reach 50%. The
    // antialiased fringe adds up to a pixel above and below the cap height.
    const auto ink=image.Find(area,Coverage);
    const float cap=FontProbe(state.textFormat.Get()).CapHeight();
    const bool capMatches=ink.Height()>=cap-1.f-cap*.05f && ink.Height()<=cap+2.5f+cap*.05f;
    if(!capMatches) {
        std::cout<<"FONT "<<what<<" expected-px="<<expected<<" cap="<<cap<<" ink-height="<<ink.Height()
            <<" area="<<area.left<<","<<area.top<<","<<area.right<<","<<area.bottom<<" image="<<image.width<<"x"<<image.height
            <<" row="<<state.rowPx<<" header="<<state.headerPx<<" total="<<state.total<<" scroll="<<state.scrollNow<<std::endl;
        image.Save("option-font-failure.bmp");
    }
    Check(capMatches,"rendered capital height does not match the configured text size");
}
inline int ListFont(int row96,int percent,int dpi) {return std::max(7,MulDiv(Scale96(row96,dpi),percent,100));}

// Row pitch from two highlighted rows two apart.
inline void CheckRowPitch(Canvas& canvas,int row96) {
    auto& state=canvas.state;
    const int first=canvas.Row(L"filler-01.txt"), third=first+2;
    canvas.Select({first,third});
    const auto image=canvas.Render();
    RECT client{}; GetClientRect(state.hwnd,&client);
    const RECT a=canvas.RowBand(first), b=canvas.RowBand(third);
    const auto boxA=image.Find(RECT{0,a.top,client.right,a.bottom},Red), boxB=image.Find(RECT{0,b.top,client.right,b.bottom},Red);
    canvas.Select({});
    const float expected=float(Scale96(row96,ViewDpi(state)));
    const float pitch=(boxB.top-boxA.top)*.5f;
    if(std::abs(pitch-expected)>.6f || std::abs(boxA.Height()-(expected-4.f))>1.5f)
        std::cout<<"ROW PITCH row96="<<row96<<" expected="<<expected<<" pitch="<<pitch<<" band="<<boxA.Height()<<std::endl;
    Check(std::abs(pitch-expected)<=.6f,"row pitch does not match the configured row height");
    Check(std::abs(boxA.Height()-(expected-4.f))<=1.5f,"row highlight height does not match the configured row height");
}

inline std::vector<std::wstring> ListNames() {
    std::vector<std::wstring> names={L"H",L"HHHH.TXT",L"Wide WWWW name.txt",L"iii narrow.txt",L".dotfile.txt",
        L"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA.WWWW",L"Café ünïcødé.txt"};
    for(int i=1;i<=30;++i) names.push_back(L"filler-"+std::wstring(i<10 ? L"0" : L"")+std::to_wstring(i)+L".txt");
    return names;
}

static void TestListLayouts(const std::wstring& fixture) {
    const auto names=ListNames();
    const auto folder=std::filesystem::path(fixture)/L"option-list";
    MakeFiles(folder,names,{L"Folder"});
    Settings settings;
    for(int dpi:{96,144}) for(bool rtl:{false,true}) {
        Regression::Scope scope("dpi="+std::to_string(dpi)+(rtl ? " RTL" : " LTR"));
        Canvas canvas(folder,int(names.size())+1,dpi,rtl);
        PrepareProbe(canvas,settings);
        for(UINT mode:{UINT(FVM_DETAILS),UINT(FVM_LIST)}) {
            Regression::Scope view(std::string("view=")+ModeName(mode,16));
            canvas.Layout(mode,16);
            for(int row96:{24,32,60,96}) for(int percent:{20,52,80}) {
                canvas.Apply(settings,L"Appearance.rowHeight",std::to_wstring(row96));
                canvas.Apply(settings,L"Appearance.fontSizePercent",std::to_wstring(percent));
                Regression::Scope values("row="+std::to_string(row96)+" text="+std::to_string(percent)+"%");
                CheckFont(canvas,ListFont(row96,percent,dpi),L"H","list text size did not reach the renderer format");
                canvas.Apply(settings,L"Selection.filenameOnlySelection",L"alwaysOff");
                CheckRowPitch(canvas,row96);
                CheckFullRowHighlight(canvas,L"Wide WWWW name.txt");
                canvas.Apply(settings,L"Selection.filenameOnlySelection",L"alwaysOn");
                for(const auto* name:{L"Wide WWWW name.txt",L"iii narrow.txt",L"Café ünïcødé.txt"})
                    CheckFilenameHighlight(canvas,name,"list");
                canvas.Apply(settings,L"Selection.filenameOnlySelection",L"gridOnly");
                CheckFullRowHighlight(canvas,L"iii narrow.txt");
            }
        }
    }
    settings.RequireExercised(L"option-effects-list");
    std::cout<<"PASS: row height, list text size and filename-only/full-row highlights match independent DPI arithmetic, "
        "DirectWrite advances and rendered glyph pixels in Details/List at 96/144 DPI, LTR/RTL\n";
}

static void TestGridLayouts(const std::wstring& fixture) {
    const std::vector<std::wstring> names={L"H",L"HHHH.TXT",L"Wide WWWW name.txt",L"iii.txt",
        L"A longer caption that wraps onto two lines.txt",L"filler-01.txt",L"filler-02.txt",L"filler-03.txt"};
    const auto folder=std::filesystem::path(fixture)/L"option-grid";
    MakeFiles(folder,names);
    Settings settings;
    for(int dpi:{96,144}) for(bool rtl:{false,true}) {
        Regression::Scope scope("dpi="+std::to_string(dpi)+(rtl ? " RTL" : " LTR"));
        Canvas canvas(folder,int(names.size()),dpi,rtl);
        PrepareProbe(canvas,settings);
        for(auto [mode,size]:std::initializer_list<std::pair<UINT,int>>{{FVM_SMALLICON,16},{FVM_ICON,48},{FVM_ICON,96}}) {
            Regression::Scope view(std::string("view=")+ModeName(mode,size));
            canvas.Layout(mode,size);
            for(int row96:{24,32,96}) for(int percent:{20,42,80}) {
                canvas.Apply(settings,L"Appearance.rowHeight",std::to_wstring(row96));
                canvas.Apply(settings,L"Appearance.gridFontSizePercent",std::to_wstring(percent));
                Regression::Scope values("row="+std::to_string(row96)+" grid text="+std::to_string(percent)+"%");
                CheckFont(canvas,ListFont(std::min(row96,96),percent,dpi),L"H","grid text size did not reach the renderer format");
                for(const auto* name:{L"Wide WWWW name.txt",L"A longer caption that wraps onto two lines.txt"}) {
                    canvas.Apply(settings,L"Selection.filenameOnlySelection",L"gridOnly");
                    const auto h=MeasureHighlight(canvas,canvas.Row(name));
                    // Outer padding always; the inner inset is symmetric and bounded by
                    // the room on both sides of the caption box (a rename EDIT covers it).
                    const float outer=2.f*dpi/96.f, pad=float(Scale96(3,dpi))+outer, bearing=.15f*canvas.state.fontPx+2.f;
                    const int leading=h.ink.left-h.fill.left, trailing=h.fill.right-h.ink.right;
                    const auto g=LayoutItem(canvas.state,LayoutRowVisual(canvas.state,canvas.Row(name),1.f));
                    const bool padded=leading>=outer-1 && trailing>=outer-1 && leading<=pad+bearing && trailing<=pad+bearing &&
                        std::abs(leading-trailing)<=2.f+.08f*canvas.state.fontPx;
                    if(h.uncovered || !padded)
                    {std::cout<<"GRID HIGHLIGHT name="<<Regression::Narrow(name)<<" fill="<<h.fill.left<<".."<<h.fill.right
                            <<" ink="<<h.ink.left<<","<<h.ink.top<<".."<<h.ink.right<<","<<h.ink.bottom<<" fill-y="<<h.fill.top<<".."<<h.fill.bottom<<" uncovered="<<h.uncovered<<" at="<<h.firstUncovered.left<<","<<h.firstUncovered.top<<".."<<h.firstUncovered.right<<","<<h.firstUncovered.bottom<<std::endl; h.Save("option-grid-highlight");}
                    Check(!h.uncovered,"grid filename highlight left caption pixels uncovered");
                    Check(padded,
                        "grid filename highlight is not the caption plus its padding");
                    const auto icon=canvas.Span(g.icon.left,g.icon.right);
                    Check(g.text.left>=g.icon.right ?
                        (rtl ? h.fill.right<=icon.first+1 : h.fill.left>=icon.second-1) : h.fill.top>=int(g.icon.bottom)-1,
                        "grid filename highlight covered the icon");
                    canvas.Apply(settings,L"Selection.filenameOnlySelection",L"alwaysOff");
                    const auto cell=MeasureHighlight(canvas,canvas.Row(name));
                    const int visibleBottom=std::min(int(g.cell.bottom),cell.selected.height);  // Cells may extend below the frame.
                    if(cell.uncovered || cell.fill.top>int(g.icon.top)+1 || cell.fill.Height()<visibleBottom-int(g.cell.top)-2)
                        std::cout<<"GRID CELL name="<<Regression::Narrow(name)<<" fill="<<cell.fill.left<<","<<cell.fill.top<<".."
                            <<cell.fill.right<<","<<cell.fill.bottom<<" cell="<<g.cell.left<<","<<g.cell.top<<".."<<g.cell.right<<","
                            <<g.cell.bottom<<" icon-top="<<g.icon.top<<" uncovered="<<cell.uncovered<<std::endl;
                    Check(!cell.uncovered && cell.fill.top<=int(g.icon.top)+1 && cell.fill.Height()>=visibleBottom-int(g.cell.top)-2,
                        "whole-item grid highlight did not cover the item cell");
                }
                // Corner rounding: the topmost highlight scanline is inset by
                // the circle's chord; zero radius keeps square corners.
                canvas.Apply(settings,L"Selection.filenameOnlySelection",L"alwaysOff");
                for(int radius:{0,16}) {
                    canvas.Apply(settings,L"Selection.gridSelectionRadius",std::to_wstring(radius));
                    const int row=canvas.Row(L"iii.txt");
                    canvas.Select({row}); const auto image=canvas.Render(); canvas.Select({});
                    const auto g=LayoutItem(canvas.state,LayoutRowVisual(canvas.state,row,1.f));
                    const auto span=canvas.Span(g.cell.left-2.f,g.cell.right+2.f);
                    const auto fill=image.Find(RECT{span.first,LONG(g.cell.top)-2,span.second,LONG(g.cell.bottom)+2},
                        [](const BYTE* p) {return p[2]>=128 && p[1]<=8 && p[3]>=128;});
                    const auto top=image.Find(RECT{span.first,fill.top,span.second,fill.top+1},
                        [](const BYTE* p) {return p[2]>=128 && p[1]<=8 && p[3]>=128;});
                    const float r=std::min(float(Scale96(radius,dpi)),std::min<float>(fill.Width(),fill.Height())*.5f);
                    const float inset=r-std::sqrt(std::max(0.f,r*r-(r-.5f)*(r-.5f)));
                    Check(std::abs((top.left-fill.left)-inset)<=1.5f,"grid corner rounding does not match the configured radius");
                }
                canvas.Apply(settings,L"Selection.gridSelectionRadius",L"0");
            }
        }
    }
    settings.RequireExercised(L"option-effects-grid");
    std::cout<<"PASS: grid text size, filename-only and whole-item highlights, and grid corner rounding match independent "
        "measurements in Small/Medium/Large icons at 96/144 DPI, LTR/RTL\n";
}

// Ctrl+wheel and pinch change the tab's zoom, keep the configured text ratio
// and keep highlights exact; a settings change returns to the option values.
static void TestZoomInputs(const std::wstring& fixture) {
    const auto names=ListNames();
    const auto folder=std::filesystem::path(fixture)/L"option-zoom";
    MakeFiles(folder,names);
    Settings settings;
    for(int dpi:{96,144}) {
        Regression::Scope scope("dpi="+std::to_string(dpi));
        Canvas canvas(folder,int(names.size()),dpi,false);
        auto& state=canvas.state;
        PrepareProbe(canvas,settings);
        canvas.Layout(FVM_DETAILS,16);
        canvas.Apply(settings,L"Selection.filenameOnlySelection",L"alwaysOn");
        const auto wheel=[&](int notches) {
            OverlayProc(state.hwnd,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WORD(short(notches*WHEEL_DELTA))),0);
            canvas.Settle();
        };
        int row96=32;
        for(int notches:{3,-5,40,-80}) {
            Regression::Scope step("Ctrl+wheel "+std::to_string(notches));
            wheel(notches); row96=std::clamp(row96+2*notches,24,96);
            Check(std::abs(state.rowPx-float(Scale96(row96,dpi)))<.01f,"Ctrl+wheel did not change row height by two pixels per notch within limits");
            CheckFont(canvas,ListFont(row96,52,dpi),L"H","Ctrl+wheel did not keep the list text size ratio");
            CheckFilenameHighlight(canvas,L"Wide WWWW name.txt","Ctrl+wheel");
        }
        for(float scale:{1.5f,.5f}) {
            Regression::Scope step("pinch x"+std::to_string(scale));
            const int before=state.rowHeight96[false] ? state.rowHeight96[false] : 32;
            ApplyManipulationStatus(state,1); ApplyManipulationPinch(state,std::log(scale)); ApplyManipulationStatus(state,0);
            canvas.Settle();
            row96=std::clamp(int(std::llround(before*double(scale))),24,96);
            Check(std::abs(state.rowPx-float(Scale96(row96,dpi)))<1.01f,"touchpad pinch did not scale the row height");
            CheckFilenameHighlight(canvas,L"Wide WWWW name.txt","pinch");
        }
        // A settings change resets zoom to the configured values.
        canvas.Apply(settings,L"Appearance.fontSizePercent",L"60");
        Check(std::abs(state.rowPx-float(Scale96(32,dpi)))<.01f,"settings change kept the old zoom");
        CheckFont(canvas,ListFont(32,60,dpi),L"H","settings change after zoom did not apply the new text size");
        CheckRowPitch(canvas,32);
        canvas.Apply(settings,L"Appearance.fontSizePercent",L"52");
        // Grid zoom scales icons proportionally and caps caption text.
        canvas.Layout(FVM_ICON,48);
        const float icon=state.iconPx;
        wheel(8);
        const int grid96=32+16;
        Check(std::abs(state.iconPx/icon-float(Scale96(grid96,dpi))/float(Scale96(32,dpi)))<.03f,
            "grid Ctrl+wheel did not scale icons in proportion to the zoom");
        CheckFont(canvas,ListFont(std::min(grid96,96),42,dpi),L"H","grid zoom did not keep its caption ratio");
        canvas.Apply(settings,L"Appearance.gridFontSizePercent",L"42");
        Check(std::abs(state.iconPx-icon)<.5f,"settings change kept the grid zoom");
    }
    std::cout<<"PASS: Ctrl+wheel and touchpad pinch change row height within limits, keep text ratio and exact filename "
        "highlights, scale grid icons proportionally, and a settings change restores option values\n";
}

static void TestAppearance(const std::wstring& fixture) {
    const std::vector<std::wstring> names={L"HHHH.TXT",L"Wide WWWW name.txt",L".dotfile.txt",
        L"AAAAAAAAAAAAAAAAAAAAAAAA.WWWW"};
    const auto folder=std::filesystem::path(fixture)/L"option-appearance";
    MakeFiles(folder,names);
    Settings settings;
    for(bool rtl:{false,true}) {
        Regression::Scope scope(rtl ? "RTL" : "LTR");
        Canvas canvas(folder,int(names.size()),96,rtl);
        auto& state=canvas.state;
        PrepareProbe(canvas,settings);
        canvas.Layout(FVM_DETAILS,16);
        RECT client{}; GetClientRect(state.hwnd,&client);
        const int wide=canvas.Row(L"Wide WWWW name.txt");
        const auto leadingEdge=[&](const Box& box) {return rtl ? client.right-1-box.right : box.left;};
        // Side margins: rounded rows stop at the margin; names move with it.
        canvas.Apply(settings,L"Selection.selectionRadius",L"8");
        canvas.Apply(settings,L"Selection.filenameOnlySelection",L"alwaysOff");
        int baseInk=0;
        for(int margin:{0,10,70}) {
            canvas.Apply(settings,L"Appearance.edgeMargin",std::to_wstring(margin));
            canvas.Select({wide}); const auto image=canvas.Render(); canvas.Select({});
            const RECT band=canvas.RowBand(wide);
            const auto fill=image.Find(band,[](const BYTE* p) {return p[2]>=128 && p[1]<=8 && p[3]>=128;});
            Check(std::abs(leadingEdge(fill)-margin)<=1,"side margin does not offset the row highlight");
            const auto ink=image.Find(canvas.NameArea(wide),[](const BYTE* p) {return p[1]>=128;});
            if(!margin) baseInk=leadingEdge(ink);
            else {
                if(std::abs(leadingEdge(ink)-baseInk-margin)>1)
                    std::cout<<"MARGIN margin="<<margin<<" ink="<<ink.left<<".."<<ink.right<<" base="<<baseInk<<" fill="<<fill.left<<".."<<fill.right<<std::endl;
                Check(std::abs(leadingEdge(ink)-baseInk-margin)<=1,"side margin did not move names by its value");
            }
        }
        canvas.Apply(settings,L"Appearance.edgeMargin",L"10");
        // List corner rounding: top scanline inset follows the radius; -1 is a pill.
        for(int radius:{0,8,-1}) {
            canvas.Apply(settings,L"Selection.selectionRadius",std::to_wstring(radius));
            canvas.Select({wide}); const auto image=canvas.Render(); canvas.Select({});
            const auto any=[](const BYTE* p) {return p[2]>=128 && p[1]<=8 && p[3]>=128;};
            const auto fill=image.Find(canvas.RowBand(wide),any);
            const auto top=image.Find(RECT{0,fill.top,client.right,fill.top+1},any);
            const float r=radius<0 ? fill.Height()*.5f : std::min<float>(float(radius),fill.Height()*.5f);
            const float inset=r-std::sqrt(std::max(0.f,r*r-(r-.5f)*(r-.5f)));
            Check(std::abs((rtl ? fill.right-top.right : top.left-fill.left)-inset)<=1.5f,
                "list corner rounding does not match the configured radius");
        }
        canvas.Apply(settings,L"Selection.selectionRadius",L"0");
        // Colour and opacity: a highlight pixel away from text.
        for(auto [color,opacity,r,g,b]:std::initializer_list<std::tuple<const wchar_t*,int,int,int,int>>{
                {L"#3399FF",40,0x33,0x99,0xFF},{L"#FF0000",100,255,0,0},{L"#00C000",0,0,0xC0,0}}) {
            canvas.Apply(settings,L"Selection.selectionColor",color);
            canvas.Apply(settings,L"Selection.selectionOpacity",std::to_wstring(opacity));
            canvas.Select({wide}); const auto image=canvas.Render(); canvas.Select({});
            const RECT band=canvas.RowBand(wide);
            const int x=rtl ? 4 : client.right-Scale96(30,96), y=(band.top+band.bottom)/2;
            const BYTE* p=image.At(x,y);
            const float a=opacity/100.f;
            Check(std::abs(p[3]-255*a)<=3 && std::abs(p[2]-r*a)<=3 && std::abs(p[1]-g*a)<=3 && std::abs(p[0]-b*a)<=3,
                "selection colour/opacity did not produce the configured highlight pixel");
        }
        canvas.Apply(settings,L"Selection.selectionColor",L"#FF0000");
        canvas.Apply(settings,L"Selection.selectionOpacity",L"100");
        // Hover over an unselected and over a selected row.
        for(int hover:{30,90}) for(int both:{40,100}) {
            canvas.Apply(settings,L"Hover.hoverOpacity",std::to_wstring(hover));
            canvas.Apply(settings,L"Hover.hoverSelectedOpacity",std::to_wstring(both));
            const int other=canvas.Row(L"HHHH.TXT");
            for(bool selected:{false,true}) {
                canvas.Select(selected ? std::initializer_list<int>{other} : std::initializer_list<int>{});
                state.inputFocusIndicators=true; state.hovered=other; state.hoverAnimationActive=false;
                const auto image=canvas.Render(); state.hovered=-1; canvas.Select({});
                const RECT band=canvas.RowBand(other);
                const int x=rtl ? 4 : client.right-Scale96(30,96);
                const float expected=255.f*(selected ? std::min(100,both) : hover)/100.f;
                Check(std::abs(image.Alpha(x,(band.top+band.bottom)/2)-expected)<=4.f,
                    selected ? "hover over a selected item did not use its configured opacity" :
                               "hover highlight did not use the configured opacity");
            }
        }
        // Stripes on alternate rows at half the configured opacity; with
        // "show stripes below the last file" they continue past the content.
        canvas.Apply(settings,L"Appearance.zebraStripes",L"true");
        for(int opacity:{20,100}) for(bool empty:{false,true}) {
            canvas.Apply(settings,L"Appearance.zebraOpacity",std::to_wstring(opacity));
            canvas.Apply(settings,L"Appearance.showEmptyRows",empty ? L"true" : L"false");
            const auto image=canvas.Render();
            const int x=rtl ? 4 : client.right-Scale96(30,96);
            std::vector<int> alphas;
            for(int row=0;row<state.total;++row) {const RECT band=canvas.RowBand(row); alphas.push_back(image.Alpha(x,(band.top+band.bottom)/2));}
            const float stripe=255.f*opacity/100.f*.5f;
            for(int row=0;row<state.total;++row)
                Check(std::abs(alphas[size_t(row)]-(row%2 ? stripe : 0.f))<=3.f,"stripe opacity or alternation is wrong");
            const RECT last=canvas.RowBand(state.total-1);
            const int belowOdd=last.bottom-2+int(state.rowPx*(state.total%2 ? .5f : 1.5f));
            if(belowOdd<image.height)
                Check(std::abs(image.Alpha(x,belowOdd)-(empty ? stripe : 0.f))<=3.f,"stripes below the last file ignored the option");
        }
        canvas.Apply(settings,L"Appearance.zebraStripes",L"false");
        // Marquee interior at its configured opacity, below the last file.
        for(int opacity:{15,60}) {
            canvas.Apply(settings,L"Selection.marqueeOpacity",std::to_wstring(opacity));
            const RECT last=canvas.RowBand(state.total-1);
            const int top=last.bottom+Scale96(20,96), bottom=std::min<int>(client.bottom-4,top+Scale96(80,96));
            Check(bottom>top+20,"marquee probe needs empty space below the last file");
            state.marqueeActive=true; state.marqueeFading=false;
            state.marqueeStart={client.right/4,top}; state.marqueeCurrent={client.right/2,bottom};
            state.marqueeStartDocumentY=float(top)-state.headerPx+state.scrollNow;
            const auto image=canvas.Render(); state.marqueeActive=false;
            Check(std::abs(image.Alpha(client.right*3/8,(top+bottom)/2)-255.f*opacity/100.f)<=3.f,
                "marquee interior did not use the configured opacity");
        }
        // Secondary text and dimmed dotfiles: peak ink alpha relative to names.
        const auto peak=[&](const Image& image,RECT area) {
            int best=0; for(int y=std::max<LONG>(0,area.top);y<std::min<LONG>(image.height,area.bottom);++y)
                for(int x=std::max<LONG>(0,area.left);x<std::min<LONG>(image.width,area.right);++x) best=std::max<int>(best,image.Alpha(x,y));
            return best;
        };
        int nameColumnEnd=0; for(const auto& column:state.columns) if(ClassifySort(column.key)==SortKind::Name) nameColumnEnd=int(ListStartX(state)+column.end);
        const bool detailsArrived=Eventually([&] {
            canvas.host.Pump(); ReceiveDirectoryResults(state); QueueVisibleDataFrame(state); Paint(state);
            return ModelEntry(state,wide).detailsReady;
        },30000);
        if(!detailsArrived) {
            const auto& entry=ModelEntry(state,wide);
            std::cout<<"DETAILS ready="<<entry.detailsReady<<" cells="<<entry.cells.size()<<" columns="<<state.columns.size()
                <<" columns-view="<<state.columnsView<<" defer="<<state.deferColumnProviders<<" step="<<state.featureInitializationStep
                <<" pending="<<state.detailResultsPending<<" epoch="<<entry.columnEpoch<<"/"<<state.columnEpoch
                <<" property="<<entry.propertyEpoch<<"/"<<state.propertyEpoch<<" mode="<<state.cachedNativeMode<<std::endl;
        }
        Check(detailsArrived,"Details values never arrived for the secondary-text probe");
        for(int opacity:{0,45,100}) {
            canvas.Apply(settings,L"Appearance.secondaryTextOpacity",std::to_wstring(opacity));
            const auto image=canvas.Render();
            const RECT band=canvas.RowBand(wide);
            const auto metadata=canvas.Span(float(nameColumnEnd+4),ListEndX(state));
            const int value=peak(image,RECT{metadata.first,band.top,metadata.second,band.bottom});
            const int name=peak(image,canvas.NameArea(wide));
            Check(name>=240 && std::abs(value-name*opacity/100.f)<=6.f,"Date/type/size opacity is not the configured fraction of name text");
        }
        for(bool dim:{true,false}) {
            canvas.Apply(settings,L"Appearance.dimDotFiles",dim ? L"true" : L"false");
            const auto image=canvas.Render();
            const int dot=peak(image,canvas.NameArea(canvas.Row(L".dotfile.txt"))), plain=peak(image,canvas.NameArea(wide));
            if(std::abs(dot-plain*(dim ? kDimmedItemContentOpacity : 1.f))>6.f)
                std::cout<<"DOTFILE dim="<<dim<<" dot-peak="<<dot<<" name-peak="<<plain<<std::endl;
            Check(std::abs(dot-plain*(dim ? kDimmedItemContentOpacity : 1.f))<=6.f,"dotfile dimming does not follow its option");
        }
        // Antialiasing: aliased text has no partial coverage; smooth text is grayscale.
        for(const auto* mode:{L"aliased",L"smooth"}) {
            canvas.Apply(settings,L"Appearance.textAntialiasing",mode);
            const auto image=canvas.Render();
            const RECT area=canvas.NameArea(wide);
            size_t ink=0,partial=0,colour=0;
            for(int y=std::max<LONG>(0,area.top);y<std::min<LONG>(image.height,area.bottom);++y)
                for(int x=std::max<LONG>(0,area.left);x<std::min<LONG>(image.width,area.right);++x) {
                    const BYTE* p=image.At(x,y); if(!p[3]) continue;
                    ++ink; if(p[3]<250) ++partial;
                    if(std::max({p[0],p[1],p[2]})-std::min({p[0],p[1],p[2]})>2) ++colour;
                }
            Check(ink>20,"antialiasing probe found no text");
            const bool aliased=!wcscmp(mode,L"aliased");
            Check(state.textAntialiasMode==(aliased ? D2D1_TEXT_ANTIALIAS_MODE_ALIASED : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE),
                "text antialiasing option did not reach the renderer's text mode");
            // A native font customizer (as the option's description says) may
            // rewrite rasterization; then only the requested mode is ours.
            const bool customizer=GetModuleHandleW(L"MacType64.dll") || GetModuleHandleW(L"MacType64.Core.dll");
            if(customizer) continue;
            if(aliased) Check(partial*100<=ink,"No antialiasing still rendered partial-coverage text");
            else Check(partial*20>=ink && !colour,"Smooth grayscale rendered aliased or colour-fringed text");
        }
        canvas.Apply(settings,L"Appearance.textAntialiasing",L"system");
        // Font family reaches body text: the filename-only highlight equals the
        // name's advance in that family.
        canvas.Apply(settings,L"Selection.filenameOnlySelection",L"alwaysOn");
        canvas.Apply(settings,L"Appearance.fontFamily",L"Courier New");
        {
            std::wstring family(state.textFormat->GetFontFamilyNameLength()+1,L'\0');
            state.textFormat->GetFontFamilyName(family.data(),UINT32(family.size()));
            Check(!_wcsicmp(family.c_str(),L"Courier New"),"font family did not reach the renderer");
            CheckFilenameHighlight(canvas,L"Wide WWWW name.txt","font family");
        }
        canvas.Apply(settings,L"Appearance.fontFamily",L"");
        // Middle ellipsis keeps the extension visible in a narrow column; end
        // trimming does not. Compare trailing glyph columns with a reference.
        canvas.Apply(settings,L"Selection.filenameOnlySelection",L"gridOnly");
        const int longRow=canvas.Row(names[3]);
        const auto profile=[&](const Image& image) {
            const auto ink=image.Find(canvas.NameArea(longRow),Ink);
            const int width=int(std::ceil(FontProbe(state.textFormat.Get()).Advance(L".WWWW")));
            const RECT band=canvas.RowBand(longRow);
            std::vector<int> columns;
            // The trailing glyph patch itself (rows x columns), not column sums:
            // "A..." and "WWWW" have similar column totals.
            for(int y=band.top;y<band.bottom;++y) for(int i=0;i<width;++i)
                columns.push_back(image.Alpha(ink.right-width+1+i,y));  // A left-to-right name ends at the right, also in RTL views.
            return columns;
        };
        const auto similarity=[](const std::vector<int>& a,const std::vector<int>& b) {
            double dot=0,na=0,nb=0; for(size_t i=0;i<a.size();++i) {dot+=double(a[i])*b[i]; na+=double(a[i])*a[i]; nb+=double(b[i])*b[i];}
            return na && nb ? dot/std::sqrt(na*nb) : 0.;
        };
        for(auto& column:state.columns) if(ClassifySort(column.key)==SortKind::Name) column.width=Scale96(700,96);
        UpdateColumns(state);
        const auto referenceImage=canvas.Render();  // Wide column: the whole name fits.
        const auto reference=profile(referenceImage);
        for(auto& column:state.columns) if(ClassifySort(column.key)==SortKind::Name) column.width=Scale96(200,96);
        UpdateColumns(state);
        // The extension patch must resemble the untrimmed suffix much more with
        // middle ellipsis than with end trimming (subpixel placement differs).
        double middle=0.,end=0.;
        for(const auto* mode:{L"alwaysOn",L"alwaysOff"}) {
            canvas.Apply(settings,L"Appearance.middleEllipsis",mode);
            const auto trimmed=canvas.Render();
            (!wcscmp(mode,L"alwaysOn") ? middle : end)=similarity(profile(trimmed),reference);
            if(!wcscmp(mode,L"alwaysOn")) trimmed.Save("option-ellipsis-middle.bmp");
        }
        if(middle<.8 || middle<end+.1) {
            std::cout<<"ELLIPSIS middle="<<middle<<" end="<<end<<std::endl; referenceImage.Save("option-ellipsis-reference.bmp");
        } else std::filesystem::remove("option-ellipsis-middle.bmp");
        Check(middle>=.8 && middle>=end+.1,"middle ellipsis did not keep the extension visible while end trimming hides it");
    }
    settings.RequireExercised(L"option-effects-appearance");
    std::cout<<"PASS: side margins, corner rounding, selection colour/opacity, hover opacities, stripes and stripes below the "
        "last file, marquee opacity, secondary text opacity, dotfile dimming, antialiasing modes, font family and middle "
        "ellipsis produce the configured GPU pixels in LTR/RTL\n";
}

// ------------------------------------------------------------ live sweep --
// Minimal browser events: navigation completion only.
struct BrowserEvents : IExplorerBrowserEvents {
    LONG references=1; bool complete=false;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(iid==IID_IUnknown || iid==__uuidof(IExplorerBrowserEvents)) {*out=this; AddRef(); return S_OK;}
        *out=nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ULONG(++references);}
    ULONG STDMETHODCALLTYPE Release() override {const LONG left=--references; if(!left) delete this; return ULONG(left);}
    HRESULT STDMETHODCALLTYPE OnNavigationPending(PCIDLIST_ABSOLUTE) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE OnViewCreated(IShellView*) override {return S_OK;}
    HRESULT STDMETHODCALLTYPE OnNavigationComplete(PCIDLIST_ABSOLUTE) override {complete=true; return S_OK;}
    HRESULT STDMETHODCALLTYPE OnNavigationFailed(PCIDLIST_ABSOLUTE) override {return S_OK;}
};

// The production attachment on a genuine DefView inside a private Explorer
// frame. Nothing reaches the user's Explorer, desktop focus or clipboard.
struct LiveExplorer {
    HWND frame=nullptr,overlay=nullptr,native=nullptr;
    ViewState* state=nullptr;
    ComPtr<IExplorerBrowser> browser;
    ComPtr<IFolderView2> folderView;
    BrowserEvents* events=new BrowserEvents;
    DWORD cookie=0;
    std::wstring path;
    explicit LiveExplorer(const std::wstring& location,UINT mode=FVM_DETAILS,int size=16) : path(location) {
        WNDCLASSW cls{}; cls.hInstance=GetModuleHandleW(nullptr); cls.lpfnWndProc=DefWindowProcW; cls.lpszClassName=L"CabinetWClass";
        RegisterClassW(&cls);
        Check(RegisterOverlayClass() || g_overlayClassRegistered,"live overlay class");
        frame=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CabinetWClass",L"private option sweep",
            WS_POPUP|WS_VISIBLE,-32000,-32000,1000,700,nullptr,nullptr,cls.hInstance,nullptr);
        Check(frame!=nullptr,"live Explorer frame");
        Check(SUCCEEDED(CoCreateInstance(CLSID_ExplorerBrowser,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&browser))),"live ExplorerBrowser");
        RECT bounds{0,0,1000,700}; FOLDERSETTINGS settings{mode,FWF_NOBROWSERVIEWSTATE};
        Check(SUCCEEDED(browser->Initialize(frame,&bounds,&settings)),"live browser initialization");
        browser->SetOptions(EBO_NOBORDER|EBO_NOTRAVELLOG);
        Check(SUCCEEDED(browser->Advise(events,&cookie)),"live browser events");
        PIDLIST_ABSOLUTE raw=nullptr;
        Check(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&raw,0,nullptr)),"live folder PIDL");
        OwnedPidl pidl(raw);
        Check(SUCCEEDED(browser->BrowseToIDList(pidl.get(),SBSP_ABSOLUTE)),"live navigation");
        Check(Eventually([&] {Pump(); return events->complete;},30000),"live navigation did not complete");
        ComPtr<IShellView> view;
        Check(SUCCEEDED(browser->GetCurrentView(IID_PPV_ARGS(&view))) && SUCCEEDED(view.As(&folderView)) &&
            SUCCEEDED(view->GetWindow(&native)),"live native view");
        ComPtr<IShellFolder> folder; Check(SUCCEEDED(folderView->GetFolder(IID_PPV_ARGS(&folder))),"live Shell folder");
        ComPtr<IShellBrowser> shell;
        ComPtr<IServiceProvider> services;
        if(FAILED(browser.As(&shell)) && SUCCEEDED(folderView.As(&services)))
            services->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&shell));
        Check(bool(shell),"live Shell browser");
        folderView->SetViewModeAndIconSize(FOLDERVIEWMODE(mode),size);  // Explorer opened in this view.
        AttachView(native,folderView,folder,shell,path);
        overlay=OverlayForTab(frame); state=overlay ? State(overlay) : nullptr;
        Check(state!=nullptr,"production attachment rejected the private Explorer view");
        if(!Eventually([&] {Pump(); return state->loaded && state->firstContentSubmittedAt && Presented();},30000))
            std::cout<<"LIVE attach loaded="<<state->loaded<<" content="<<state->firstContentSubmittedAt
                <<" visible="<<state->visible<<" scope="<<state->inScope<<" presenter="<<bool(state->presenter)
                <<" graphics="<<state->graphicsReady<<" failure=0x"<<std::hex<<unsigned(state->graphicsFailure)<<std::dec
                <<" sequence="<<state->presentationSequence<<" overlay-visible="<<::IsWindowVisible(overlay)
                <<" total="<<state->total<<" mode="<<state->cachedNativeMode<<" accepted="<<state->presentationAcceptedSequence<<std::endl;
        Check(state->loaded && state->firstContentSubmittedAt && Presented(),"attached view never presented");
    }
    ~LiveExplorer() {
        if(IsWindow(overlay)) DestroyWindow(overlay);
        Pump();
        if(browser) {browser->Unadvise(cookie); browser->Destroy();}
        events->Release(); folderView.Reset(); browser.Reset();
        DestroyWindow(frame); StopTabVisibilityObserver();
    }
    // The real message loop: every queued message reaches its own window.
    void Pump() {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&message); DispatchMessageW(&message);}
    }
    bool Alive() const {return IsWindow(overlay) && State(overlay)==state;}
    // The owner consumes renderer receipts on its own message loop; its
    // accepted sequence is the acknowledgement that the latest frame showed.
    bool Presented() const {
        return state->presenter && state->graphicsReady && state->presentationSequence &&
            state->presentationAcceptedSequence>=state->presentationSequence;
    }
    // Settled once work is idle and a frame submitted after `since` (the
    // change) was acknowledged; continuous animation cannot starve this.
    void Settle(const char* what,unsigned long long since) {
        const bool settled=Eventually([&] {
            Pump();
            if(!Alive()) return true;
            RECT update{};
            return state->loaded && !state->directoryPending && !state->watchedChangePending && !state->zoomAnimating &&
                !state->rowTransitionActive && !state->columnTransitionActive && !GetUpdateRect(overlay,&update,FALSE) &&
                !state->iconResultsPending && !state->detailResultsPending && !state->viewportDemandPending &&
                (!state->visible || (state->presenter && state->graphicsReady &&
                    state->presentationAcceptedSequence>=std::min(state->presentationSequence,since+1)));
        },30000);
        Check(Alive(),"overlay was destroyed by an option change");
        if(!settled) std::cout<<"LIVE settle timeout "<<what<<" directory="<<state->directoryPending<<" watch="<<state->watchedChangePending
            <<" zoom="<<state->zoomAnimating<<" rows="<<state->rowTransitionActive<<" columns="<<state->columnTransitionActive
            <<" visible="<<state->visible<<" presented="<<Presented()<<std::endl;
        Check(settled,what);
    }
    void SetMode(UINT mode,int size) {
        {
            // The mod's own scope for a user view command; programmatic Shell
            // calls raise none of Explorer's command notifications.
            NativeViewInput input(state,true);
            Check(SUCCEEDED(folderView->SetViewModeAndIconSize(FOLDERVIEWMODE(mode),size)),"live native view mode");
        }
        if(!Eventually([&] {Pump(); return state->cachedNativeMode==mode && state->gridView==IsIconViewMode(mode);},30000)) {
            FOLDERVIEWMODE nativeMode{}; int nativeSize=0; folderView->GetViewModeAndIconSize(&nativeMode,&nativeSize);
            std::cout<<"LIVE mode requested="<<mode<<" native="<<nativeMode<<" size="<<nativeSize<<" cached="<<state->cachedNativeMode
                <<" grid="<<state->gridView<<" visible="<<state->visible<<" active="<<state->detailsViewActive<<std::endl;
        }
        Check(state->cachedNativeMode==mode && state->gridView==IsIconViewMode(mode),"GPU view did not follow the native view mode");
        Settle("view mode change did not settle",state->presentationSequence);
    }
};

// Values exercised for every option, derived from the schema; integers use
// the catalog's limits and free-form text uses explicit probes.
inline std::vector<std::wstring> ProbeValues(const OptionCatalog::Option& option) {
    std::vector<std::wstring> values;
    if(!option.choices.empty()) {
        for(const auto& choice:option.choices) if(choice!=option.initial) values.push_back(choice);
    } else if(option.initial==L"true" || option.initial==L"false") {
        values.push_back(option.initial==L"true" ? L"false" : L"true");
    } else if(option.key==L"Appearance.fontFamily") {
        values={L"Courier New",L"  Segoe UI  ",L"No Such Font Family 4d2f",std::wstring(300,L'W')};
    } else if(option.key==L"Selection.selectionColor") {
        values={L"#FF0000",L"12aBef",L"not a colour"};
    } else {
        const auto integer=std::find_if(std::begin(OptionCatalog::integers),std::end(OptionCatalog::integers),
            [&](const auto& candidate) {return option.key==candidate.key;});
        if(integer==std::end(OptionCatalog::integers))
            throw std::runtime_error("option sweep has no probe values for "+Regression::Narrow(option.key)+
                "; add it to OptionCatalog or ProbeValues");
        values={std::to_wstring(integer->minimum),std::to_wstring(integer->maximum),
            std::to_wstring((integer->minimum+integer->maximum)/2)};
    }
    values.push_back(option.initial);  // Always finish on the default.
    return values;
}

// Interactions that are in flight when the option changes.
enum class Busy { Idle, Zoom, Rename, HoverScroll, Pinch, ScrollbarDrag, Count };
inline const char* BusyName(Busy busy) {
    switch(busy) {
    case Busy::Zoom: return "Ctrl+wheel zoom in flight";
    case Busy::Rename: return "rename editor open";
    case Busy::HoverScroll: return "hover, selection and smooth scroll in flight";
    case Busy::Pinch: return "touchpad pinch active";
    case Busy::ScrollbarDrag: return "scrollbar thumb held";
    default: return "idle";
    }
}
inline void BeginBusy(LiveExplorer& live,Busy busy) {
    auto& s=*live.state;
    RECT client{}; GetClientRect(live.overlay,&client);
    switch(busy) {
    case Busy::Zoom:
        SendMessageW(live.overlay,WM_MOUSEWHEEL,MAKEWPARAM(MK_CONTROL,WHEEL_DELTA*2),0); break;
    case Busy::Rename:
        if(s.total) BeginRename(s,0); break;
    case Busy::HoverScroll:
        if(s.total>2) {::Select(s,1); BeginHoverTransition(s,2,true); MoveScroll(s,s.rowPx*3);} break;
    case Busy::Pinch:
        ApplyManipulationStatus(s,1); ApplyManipulationPinch(s,std::log(1.2f)); break;
    case Busy::ScrollbarDrag: {
        const auto bar=Scrollbar(s);
        if(bar.usable) BeginScrollbarInteraction(s,int((bar.left+bar.right)*.5f),int(bar.thumbStart+bar.thumbLength*.5f));
        break;
    }
    default: break;
    }
}
inline void EndBusy(LiveExplorer& live,Busy busy) {
    if(!live.Alive()) return;
    auto& s=*live.state;
    if(busy==Busy::Pinch) ApplyManipulationStatus(s,0);
    if(busy==Busy::Rename) CancelRename(s);
    if(s.draggingScrollbar || s.draggingHorizontal) SendMessageW(live.overlay,WM_CANCELMODE,0,0);
    StopHoverAnimation(s); s.hovered=-1;
}

// Effect checks that hold in any state after a settings change.
inline void CheckLiveResult(LiveExplorer& live,const Settings& settings) {
    auto& s=*live.state;
    Check(s.visible && s.inScope,"option change fell back to the native view");
    Check(s.graphicsReady && SUCCEEDED(s.graphicsFailure),"option change failed GPU rendering");
    const int dpi=ViewDpi(s);
    const int row96=g_row96.load();
    const int font=s.gridView ? ListFont(std::min(row96,96),g_gridFontPercent.load(),dpi) : ListFont(row96,g_fontPercent.load(),dpi);
    Check(std::abs(s.fontPx-float(font))<.01f,"text size after an option change does not match the options");
    if(!s.gridView) Check(std::abs(s.rowPx-float(Scale96(row96,dpi)))<.01f,"row height after an option change does not match the option");
    {
        // The immutable model the renderer draws from carries the new metrics.
        const auto model=CapturePresentationModel(s);
        Check(model && std::abs(model->rowPx-s.rowPx)<.01f && std::abs(model->fontPx-s.fontPx)<.01f,
            "renderer model did not receive the changed metrics");
    }
    const bool toggle=settings.values.at(L"Navigation.showNativeViewToggleButton")==L"true";
    Check((IsWindow(s.viewToggle) && ::IsWindowVisible(s.viewToggle))==toggle,"native view toggle button does not follow its option");
    // Name sort with folders "b-folder" and "n-folder" among files.
    if(s.sort.explicitOrder && ClassifySort(s.sort.key)==SortKind::Name && s.sort.direction==SORT_ASCENDING) {
        std::vector<bool> folders; for(int i=0;i<s.total;++i) folders.push_back(s.entries[size_t(i)].folder);
        const auto placement=g_folderPlacement.load();
        const auto firstFile=std::find(folders.begin(),folders.end(),false)-folders.begin();
        const auto lastFolder=folders.rend()-std::find(folders.rbegin(),folders.rend(),true);
        if(placement==FolderPlacement::AlwaysTop || placement==FolderPlacement::ExplorerDefault)
            Check(lastFolder<=firstFile,"folders were not placed first");
        else if(placement==FolderPlacement::AlwaysBottom)
            Check(std::find(folders.begin(),folders.end(),true)-folders.begin()>=std::count(folders.begin(),folders.end(),false),
                "folders were not placed last");
        else Check(!(lastFolder<=firstFile),"mixed folder placement kept folders together");
    }
    if(settings.values.at(L"Columns.preSort")==L"false") Check(!s.sort.explicitOrder || s.sort.explicitOrder==s.displayedSort.explicitOrder,
        "disabling Explorer sorting left an inconsistent sort state");
}

// Real-world folder content: images that have thumbnails (tiny, typical,
// very large and extreme aspect), a shortcut, an archive, hidden/system,
// empty, extensionless and long-named files beside ordinary ones.
inline void WriteImage(const std::filesystem::path& path,UINT width,UINT height,REFGUID container) {
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE)) &&
        SUCCEEDED(factory->CreateEncoder(container,nullptr,&encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame,nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(width,height)),"option fixture image encoder");
    WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;
    Check(SUCCEEDED(frame->SetPixelFormat(&format)),"option fixture image format");
    const UINT stride=(width*(format==GUID_WICPixelFormat32bppBGRA ? 4u : 3u)+3u)&~3u;
    std::vector<BYTE> pixels(size_t(stride)*height);
    for(UINT y=0;y<height;++y) for(UINT x=0;x<stride;++x) pixels[size_t(y)*stride+x]=BYTE((x*7+y*3)&255);
    Check(SUCCEEDED(frame->WritePixels(height,stride,UINT(pixels.size()),pixels.data())) &&
        SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()),"option fixture image pixels");
}
inline void MakeLiveFolder(const std::filesystem::path& folder) {
    std::vector<std::wstring> files={L"a.txt",L"c.txt",L"m-file.txt",L"z.txt",L".dotfile.txt",L"no-extension",
        L"A very long filename that will need trimming in narrow columns and grid captions.txt",
        L"Café ünïcødé 日本語.txt"};
    for(int i=1;i<=60;++i) files.push_back(L"item-"+std::to_wstring(i)+L".txt");
    MakeFiles(folder,files,{L"b-folder",L"n-folder"});
    WriteImage(folder/L"photo.jpg",1600,1200,GUID_ContainerFormatJpeg);
    WriteImage(folder/L"tiny.png",1,1,GUID_ContainerFormatPng);
    WriteImage(folder/L"huge.png",4000,3000,GUID_ContainerFormatPng);
    WriteImage(folder/L"strip.png",3000,10,GUID_ContainerFormatPng);
    WriteImage(folder/L"bitmap.bmp",64,64,GUID_ContainerFormatBmp);
    HANDLE empty=CreateFileW((folder/L"empty.txt").c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
    Check(empty!=INVALID_HANDLE_VALUE,"option fixture empty file"); CloseHandle(empty);
    Check(SetFileAttributesW((folder/L"z.txt").c_str(),FILE_ATTRIBUTE_HIDDEN) &&
        SetFileAttributesW((folder/L"c.txt").c_str(),FILE_ATTRIBUTE_SYSTEM),"option fixture attributes");
    ComPtr<IShellLinkW> link; ComPtr<IPersistFile> persist;
    Check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))) &&
        SUCCEEDED(link->SetPath((folder/L"photo.jpg").c_str())) && SUCCEEDED(link.As(&persist)) &&
        SUCCEEDED(persist->Save((folder/L"shortcut.lnk").c_str(),TRUE)),"option fixture shortcut");
    ProviderSelectionFixture::WriteZip(folder/L"archive.zip",ProviderSelectionFixture::ZipBytes({{"inside.txt","zip"}}));
}

// After a change, every item is drawn under the new value: scroll to the end
// and back, letting icons, thumbnails and Details values arrive for each page.
inline void VisitAllItems(LiveExplorer& live) {
    auto& s=*live.state;
    const float end=std::max(0.f,DocumentHeight(s)-float(ViewHeight(s)));
    for(float target=0.f;;target=std::min(end,target+float(ViewHeight(s)))) {
        const auto submitted=s.presentationSequence;
        SetScrollImmediate(s,target); QueueVisibleDataFrame(s); Redraw(s);
        live.Settle("page did not settle while visiting every item",submitted);
        if(target>=end) break;
    }
    const auto submitted=s.presentationSequence;
    SetScrollImmediate(s,0); Redraw(s);
    live.Settle("returning to the first item did not settle",submitted);
}

// Windhawk calls Wh_ModSettingsChanged on its own thread, not Explorer's UI thread.
inline void ChangeSettingsFromWindhawkThread() {
    std::thread([] {Wh_ModSettingsChanged();}).join();
}

static void TestLiveSweep(const std::wstring& fixture,std::initializer_list<std::pair<UINT,int>> modes,const wchar_t* test,
                          bool startup) {
    Check(SUCCEEDED(OleInitialize(nullptr)),"live sweep STA");
    if(!g_writeFactory) Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.GetAddressOf()))),"live sweep fonts");
    const auto folder=std::filesystem::path(fixture)/(std::wstring(L"option-live-")+test);
    MakeLiveFolder(folder);
    size_t applied=0,started=0;
    {
        FocusFixture::Session focus(nullptr,nullptr);
        SelectionLogFixture::FormatAll formatting;  // Logging on formats every line, as Windhawk does.
        Settings settings;
        {
            LiveExplorer live(folder.wstring());
            for(auto [mode,size]:modes) {
                Regression::Scope view(std::string("view=")+ModeName(mode,size));
                live.SetMode(mode,size);
                const float baselineRow=live.state->rowPx, baselineFont=live.state->fontPx;
                int busy=0;
                for(const auto& option:OptionCatalog::Schema()) for(const auto& value:ProbeValues(option)) {
                    const auto state=Busy(busy++%int(Busy::Count));
                    Regression::Scope scope(Text(option.key,value)+" while "+BusyName(state));
                    BeginBusy(live,state);
                    const auto submitted=live.state->presentationSequence;
                    settings.Set(option.key,value);
                    ChangeSettingsFromWindhawkThread();
                    live.Settle("option change did not settle",submitted);
                    EndBusy(live,state);
                    live.Settle("interaction did not settle after an option change",live.state->presentationSequence);
                    CheckLiveResult(live,settings);
                    VisitAllItems(live);
                    ++applied;
                }
                Check(std::abs(live.state->rowPx-baselineRow)<.01f && std::abs(live.state->fontPx-baselineFont)<.01f,
                    "restoring every option to its default did not restore the original layout");
            }
        }
        // Explorer opened while a non-default value is already configured.
        if(startup) for(auto [mode,size]:modes) for(const auto& option:OptionCatalog::Schema())
            for(const auto& value:ProbeValues(option)) {
                if(value==option.initial) continue;
                Regression::Scope scope(Text(option.key,value)+" at startup / view="+ModeName(mode,size));
                settings.Set(option.key,value); LoadSettings();
                {
                    LiveExplorer live(folder.wstring(),mode,size);
                    live.Settle("startup with a configured option did not settle",live.state->presentationSequence);
                    CheckLiveResult(live,settings);
                    VisitAllItems(live);
                }
                settings.Reset(option.key); LoadSettings();
                ++started;
            }
    }
    std::cout<<"PASS: live sweep applied "<<applied<<" option values from Windhawk's thread on a genuine DefView with "
        "images, shortcut, archive and hidden/system files, while idle, zooming, renaming, hovering/scrolling, pinching "
        "and dragging the scrollbar, visiting every item after each change; "<<started<<" startups with a configured "
        "value; exact metrics, renderer model, toggle button and folder placement each time\n";
    Check(Eventually([] {
        std::lock_guard lock(g_backgroundLifetimeMutex);
        return std::none_of(g_backgroundLifetimes->begin(),g_backgroundLifetimes->end(),
            [](const auto& work) {return work->stopping.load() && work->presentation.thread &&
                WaitForSingleObject(work->presentation.thread,0)!=WAIT_OBJECT_0;});
    }),"live sweep renderer did not drain");
    g_writeFactory.Reset();
    OleUninitialize();
}
}
